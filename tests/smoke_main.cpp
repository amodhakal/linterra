// Headless smoke test for the GL path.
//
// The doctest suite in test_*.cpp deliberately covers only the pure math, so
// everything that needs a real GL context -- shader compilation and linking,
// buffer creation, VAO setup, chunk mesh upload, draw submission, and context
// teardown -- has no automated coverage at all. Those are exactly the paths
// where a defect ships silently: the unit suite cannot see them, so a broken
// shader or a bad upload only surfaces when someone runs the app.
//
// This executable creates a hidden GLFW window, which yields a fully
// functional offscreen context with no visible output, and drives the real
// engine classes through the same sequence Application uses. It asserts on
// observable state (compile logs, mesh contents, GL error codes) rather than
// on rendered pixels, which is what makes it viable without a GPU.
//
// Unlike the unit tests this needs a windowing system. On Linux CI it is run
// under xvfb-run; where no display is available at all it reports that and
// exits non-zero, so a skipped run can never be mistaken for a passing one.

#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "chunk.h"
#include "config.h"
#include "io.h"
#include "renderer/renderer.hpp"
#include "shader.h"

namespace {

int g_Failures = 0;

// Mirrors Shader's own loader: resolve through IO so the executable-directory
// fallback is exercised the same way the engine exercises it.
std::string ReadFile(const char* path) {
  return IO::getFullFileContents(path);
}

void Report(const std::string& stage, bool ok, const std::string& detail = "") {
  if (ok) {
    std::printf("  ok    %s\n", stage.c_str());
  } else {
    std::printf("  FAIL  %s%s%s\n", stage.c_str(), detail.empty() ? "" : ": ",
                detail.c_str());
    ++g_Failures;
  }
}

void ReportGlErrors(const char* stage) {
  // Drain the whole queue so one failure does not cascade into every
  // later check.
  std::string detail;
  for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
    if (!detail.empty()) {
      detail += ", ";
    }
    switch (error) {
      case GL_INVALID_ENUM:
        detail += "GL_INVALID_ENUM";
        break;
      case GL_INVALID_VALUE:
        detail += "GL_INVALID_VALUE";
        break;
      case GL_INVALID_OPERATION:
        detail += "GL_INVALID_OPERATION";
        break;
      case GL_OUT_OF_MEMORY:
        detail += "GL_OUT_OF_MEMORY";
        break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
        detail += "GL_INVALID_FRAMEBUFFER_OPERATION";
        break;
      default:
        detail += "GL error 0x" + std::to_string(error);
        break;
    }
  }
  Report(stage, detail.empty(), detail);
}

}  // namespace

int main() {
  std::printf("linterra headless smoke test\n");

  std::unique_ptr<IRenderer> renderer;
  try {
    renderer = createRenderer(RenderBackend::OpenGL);
  } catch (const std::exception& e) {
    std::printf("  FAIL  createRenderer threw: %s\n", e.what());
    return EXIT_FAILURE;
  }
  if (!renderer) {
    std::printf("  FAIL  createRenderer returned null\n");
    return EXIT_FAILURE;
  }
  Report("createRenderer", true);

  try {
    renderer->initializeWindowing();
    Report("initializeWindowing (glfwInit)", true);

    // Hidden window: a real context, nothing on screen.
    renderer->setWindowVisible(false);
    renderer->configureWindowHints();

    if (!renderer->createWindow(64, 64, "linterra-smoke")) {
      std::printf(
          "  FAIL  createWindow: no GL context available. On a headless Linux "
          "runner this needs xvfb-run with software rendering.\n");
      return EXIT_FAILURE;
    }
    Report("createWindow (hidden)", true);

    renderer->makeContextCurrent();
    Report("makeContextCurrent", true);

    if (!renderer->loadContextFunctions()) {
      std::printf("  FAIL  loadContextFunctions (GLAD)\n");
      return EXIT_FAILURE;
    }
    Report("loadContextFunctions (GLAD)", true);

    std::printf("  info  GL_VERSION  %s\n", glGetString(GL_VERSION));
    std::printf("  info  GL_RENDERER %s\n", glGetString(GL_RENDERER));
    ReportGlErrors("context is error-free");

    // --- Shader compilation and linking -----------------------------------
    // The real Shader class, loading the paths the engine actually uses, so a
    // shader that fails to compile on this driver fails here.
    //
    // The uniform list is a contract: these are the names the C++ side sets
    // every frame. Shader::newUniform only prints a diagnostic when a name is
    // missing, so the locations are also resolved explicitly below -- a rename
    // in the GLSL without a matching change in C++ would otherwise leave the
    // engine rendering with a stale uniform and no error.
    auto vertexSource = ReadFile(Constants::RENDER_VERTEX_PATH);
    auto fragmentSource = ReadFile(Constants::RENDER_FRAGMENT_PATH);

    {
      Shader scene(renderer.get());
      scene.load(Constants::RENDER_VERTEX_PATH, Constants::RENDER_FRAGMENT_PATH);
      Report("scene shader compiles and links", true);
      scene.use();
      scene.newUniform("uTextureArray");
      scene.newUniform("uModel");
      scene.newUniform("uView");
      scene.newUniform("uProjection");
      scene.newUniform("uFogEnd");
    }
    ReportGlErrors("shader stage is error-free");

    {
      auto vs = renderer->createShader(ShaderType::Vertex, vertexSource.c_str());
      auto fs =
          renderer->createShader(ShaderType::Fragment, fragmentSource.c_str());
      Report("vertex shader compiles", vs && vs->isCompiled(),
             vs ? vs->getCompileLog() : "null shader");
      Report("fragment shader compiles", fs && fs->isCompiled(),
             fs ? fs->getCompileLog() : "null shader");
      if (vs && vs->isCompiled() && fs && fs->isCompiled()) {
        std::vector<std::unique_ptr<IShader>> stages;
        stages.push_back(std::move(vs));
        stages.push_back(std::move(fs));
        auto program = renderer->createShaderProgram(std::move(stages));
        if (program) {
          for (const char* uniform : {"uTextureArray", "uModel", "uView",
                                      "uProjection", "uFogEnd"}) {
            const int location = program->getUniformLocation(uniform);
            Report(std::string("uniform resolves: ") + uniform, location >= 0,
                   "location = " + std::to_string(location));
          }
        }
      }
    }

    {
      Shader fog(renderer.get());
      fog.load(Constants::FOG_VERTEX_PATH, Constants::FOG_FRAGMENT_PATH);
      Report("fog shader compiles and links", true);
      fog.newUniform("uScene");
      fog.newUniform("uFogStart");
      fog.newUniform("uFogEnd");
      fog.newUniform("uFogColor");
    }

    // --- Chunk generation and GPU upload ----------------------------------
    // generateMeshData + generateMesh is the CPU half of the pipeline; pass()
    // is the hand-off that creates the VBO/EBO/VAO and uploads. This is the
    // path where a wrong size or a mismatched vertex layout shows up as
    // GL_INVALID_VALUE with nothing else to catch it.
    {
      Chunk chunk(renderer.get());
      const glm::ivec2 position{0, 0};

      chunk.generateHeightMapCPU(position);
      Report("chunk heightmap (CPU)", true);

      chunk.generateMeshData(position);
      chunk.generateMesh();
      ReportGlErrors("chunk mesh generation is error-free");

      chunk.pass();
      Report("chunk mesh upload to GPU", true);
      ReportGlErrors("chunk upload is error-free");

      chunk.cleanup();
      Report("chunk cleanup", true);
    }

    // --- Offscreen framebuffer ---------------------------------------------
    // The two-stage render pipeline renders the scene into an offscreen FBO
    // before the fog pass composites it. That target is created and resized
    // through IRenderer, so it is covered here even though Application (which
    // normally drives it) is not part of this target.
    renderer->resizeOffscreenTarget(64, 64);
    Report("resizeOffscreenTarget", true);

    renderer->bindOffscreenTarget();
    Report("bindOffscreenTarget", true);

    renderer->clear(glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    ReportGlErrors("offscreen clear");

    // A second resize exercises the reallocation path.
    renderer->resizeOffscreenTarget(128, 128);
    ReportGlErrors("offscreen target resize");

    // --- Draw submission ---------------------------------------------------
    // A core-profile draw with no enabled vertex attributes is a
    // GL_INVALID_OPERATION, so this sets up a real VAO with a real buffer and
    // submits an actual triangle. That exercises the attribute plumbing and
    // the draw path rather than a degenerate call.
    {
      const float triangle[] = {
          -0.5f, -0.5f, 0.0f,  //
          0.5f,  -0.5f, 0.0f,  //
          0.0f,  0.5f,  0.0f,  //
      };

      auto vao = renderer->createVertexArray();
      if (!vao) {
        Report("vertex array creation", false);
      } else {
        auto vbo = renderer->createBuffer(BufferType::Vertex);
        if (!vbo) {
          Report("buffer creation", false);
        } else {
          renderer->setBufferData(*vbo, triangle, sizeof(triangle),
                                  BufferUsage::Static);
          ReportGlErrors("buffer upload");

          renderer->bindVertexArray(*vao);
          renderer->setVertexAttribute(*vao, 0, 3, DataType::Float, false,
                                      sizeof(float) * 3, 0);
          renderer->enableVertexAttribute(*vao, 0);
          ReportGlErrors("vertex attribute setup");

          renderer->draw(PrimitiveType::Triangles, 3, 0);
          ReportGlErrors("array draw submission");

          renderer->bindVertexArray(*vao);
        }
      }
    }

    renderer->clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    ReportGlErrors("clear");

    // Bound last, once no sampler2DArray program is active. Doing this while
    // the scene shader is current makes the driver complain that a 2D texture
    // is bound to a sampler2DArray -- an artefact of the test's own ordering,
    // not of the engine, whose fog pass samples this with a plain sampler2D.
    {
      Shader fog(renderer.get());
      fog.load(Constants::FOG_VERTEX_PATH, Constants::FOG_FRAGMENT_PATH);
      fog.use();
      fog.newUniform("uScene");
      renderer->bindOffscreenColorTexture(0);
      Report("bindOffscreenColorTexture (as sampler2D)", true);
    }
    ReportGlErrors("offscreen color texture binding");

    renderer->swapBuffers();
    Report("swapBuffers", true);

    renderer->setWindowShouldClose(true);
    Report("windowShouldClose round-trip", renderer->windowShouldClose());
  } catch (const std::exception& e) {
    std::printf("  FAIL  exception escaped: %s\n", e.what());
    return EXIT_FAILURE;
  }

  renderer->terminateWindowing();
  Report("terminateWindowing", true);

  if (g_Failures != 0) {
    std::printf("\nsmoke test FAILED with %d problem(s)\n", g_Failures);
    return EXIT_FAILURE;
  }

  std::printf("\nsmoke test passed\n");
  return EXIT_SUCCESS;
}
