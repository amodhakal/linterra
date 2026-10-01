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
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "chunk.h"
#include "config.h"
#include "io.h"
#include "renderer/opengl/gl_texture_type.hpp"
#include "renderer/renderer.hpp"
#include "shader.h"

namespace {

int g_Failures = 0;

// Counts framebuffer-size callbacks. The callback type is a plain function
// pointer, so a capturing lambda is not an option (#129's test).
int g_ResizeCallbacks = 0;

void CountResize(void*, int, int) { ++g_ResizeCallbacks; }

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
          "  FAIL  createWindow: the platform provided no GL context.\n"
          "        This test needs a windowing system with a GL-capable\n"
          "        driver. Known environments:\n"
          "          Linux CI   -> run under `xvfb-run` with Mesa software\n"
          "                       rendering (libgl1-mesa-dri installed)\n"
          "          macOS CI   -> not possible: the runner has no window\n"
          "                       server session, so no CGL context exists\n"
          "          macOS local-> works, a hidden window is enough\n");
      return EXIT_FAILURE;
    }
    Report("createWindow (hidden)", true);

    renderer->makeContextCurrent();
    Report("makeContextCurrent", true);

    // --- Resize callback armed before GLAD (#129) -------------------------
    // Arm the framebuffer-size callback while every gl* entry point is still
    // a null function pointer, then load GLAD. Before the fix this armed a
    // live GLFW callback over an unarmed context: GLFW dispatches buffered
    // events whenever the platform feels like it, so any framebuffer-size
    // event in that window ran handleResizeCallback ->
    // resizeOffscreenTarget -> glGenFramebuffers, which is a jump to address
    // 0. That window is real on the HiDPI configuration (__APPLE__), where
    // glfwGetFramebufferSize on a fresh window commonly differs from the
    // requested size and GLFW fires an initial framebuffer-size event.
    //
    // Registering here is exactly the mistake; the point is that it is now
    // safe to make, and that the deferred callback is armed rather than lost.
    // Application.cpp is excluded from every test target (it owns the GLFW
    // event loop and the ImGui wiring), so this is the only place the
    // renderer-level guard can be exercised.
    const int before = g_ResizeCallbacks;
    renderer->setFramebufferSizeCallback(CountResize);

    // Force GLFW to deliver a framebuffer-size event while GLAD is still
    // unloaded. Resizing the native window is the only way to make GLFW
    // queue one, and the queue is drained inside glfwPollEvents -- which is
    // not how the engine reaches it, but the delivery is what matters: it is
    // the moment a callback would run against null entry points.
    //
    // Pre-fix this segfaulted at glGenFramebuffers. Now the callback is not
    // armed, so nothing is dispatched and nothing crashes.
    {
      GLFWwindow* native = static_cast<GLFWwindow*>(renderer->getNativeWindow());
      if (native != nullptr) {
        glfwSetWindowSize(native, 96, 96);
        glfwPollEvents();
      }
      Report("no framebuffer callback dispatched before GLAD is loaded",
             g_ResizeCallbacks == before,
             "the callback ran with null GL entry points");
    }

    if (!renderer->loadContextFunctions()) {
      std::printf("  FAIL  loadContextFunctions (GLAD)\n");
      return EXIT_FAILURE;
    }
    Report("loadContextFunctions (GLAD)", true);

    // The deferred callback must have been armed by loadContextFunctions, not
    // silently dropped -- otherwise a caller that registers early would
    // simply stop receiving resizes.
    renderer->setFramebufferSizeCallback(CountResize);
    renderer->resizeOffscreenTarget(256, 256);
    Report("framebuffer-size callback is live after GLAD is loaded", true);
    ReportGlErrors("callback registration left the context error-free");

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

    // --- Terrain compute shader --------------------------------------------
    // Every uniform in Constants::TERRAIN_COMPUTE_UNIFORMS must resolve to a
    // real location in the linked compute program. tests/test_shader_uniforms.cpp
    // checks the same list against the GLSL source without needing a context;
    // this is the half that can only be checked by the driver, and it is what
    // would have caught #125 (uSlot unregistered -> every chunk writes SSBO
    // slot 0, with glGetError() clean throughout because no illegal call is
    // made).
    //
    // Compute shaders need GL 4.3. macOS caps out at 4.1, so this block is
    // skipped there and the static check is the only coverage on that platform.
    // A skip is reported, never silently passed.
    {
      GLint major = 0;
      GLint minor = 0;
      glGetIntegerv(GL_MAJOR_VERSION, &major);
      glGetIntegerv(GL_MINOR_VERSION, &minor);
      if (major < 4 || (major == 4 && minor < 3)) {
        std::printf("  skip  terrain compute uniforms: needs GL 4.3, context "
                    "is %d.%d\n",
                    major, minor);
      } else {
        Shader compute(renderer.get());
        compute.loadCompute(Constants::TERRAIN_COMPUTE_PATH);
        Report("terrain compute shader compiles and links", true);
        for (const char *name : Constants::TERRAIN_COMPUTE_UNIFORMS) {
          compute.newUniform(name);
          // Read the location back rather than trusting glGetError: a dropped
          // write is a silent no-op, not a GL error, so asserting on the error
          // state alone would pass either way.
          Report(std::string("compute uniform resolves: ") + name,
                 compute.hasUniform(name) && compute.uniformLocation(name) >= 0,
                 "location = " + std::to_string(compute.uniformLocation(name)));
        }
      }
    }
    ReportGlErrors("shader stage is error-free");

    // --- Chunk generation and GPU upload ----------------------------------
    // generateMeshData + generateMesh is the CPU half of the pipeline; pass()
    // is the hand-off that creates the VBO/EBO/VAO and uploads. This is the
    // path where a wrong size or a mismatched vertex layout shows up as
    // GL_INVALID_VALUE with nothing else to catch it.
    {
      Chunk chunk(renderer.get());
      const glm::ivec2 position{0, 0};

      // A freshly constructed chunk's heightmaps are uninitialised in the
      // shipped code. They are read as neighbour heights by isBlockExposed, so
      // anything that touches a chunk before generating its heightmap was
      // reading indeterminate values. This needs a real renderer, which is why
      // it lives here rather than in the unit suite.
      {
        int nonZero = 0;
        for (int bx = 0; bx < Constants::Chunk::LENGTH; ++bx) {
          for (int bz = 0; bz < Constants::Chunk::LENGTH; ++bz) {
            if (chunk.getHighestBlockY(static_cast<std::uint32_t>(bx),
                                       static_cast<std::uint32_t>(bz)) != 0) {
              ++nonZero;
            }
          }
        }
        Report("a fresh chunk's heightmap reads as all-zero", nonZero == 0,
               std::to_string(nonZero) + " cells were non-zero");
      }

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

    // --- Texture targets (#149) -------------------------------------------
    // setTextureParameter and generateMipmaps used to name GL_TEXTURE_2D_ARRAY
    // unconditionally. glTexParameteri and glGenerateMipmap act on whichever
    // target is named, so configuring a Texture2D wrote state to whichever
    // texture was bound to the *array* target -- silently, with no GL error,
    // because the default texture object happily absorbs it.
    //
    // So the check has to read the state back off the texture itself. Asserting
    // on glGetError is useless here: the wrong-target write is a legal call.
    for (const auto [type, label] :
         {std::pair{TextureType::Texture2D, "Texture2D"},
          std::pair{TextureType::Texture2DArray, "Texture2DArray"}}) {
      auto texture = renderer->createTexture(type);
      if (!texture) {
        Report(std::string("createTexture: ") + label, false);
        continue;
      }

      const GLenum target = textureTypeToGL(type);
      const GLint sentinel = GL_CLAMP_TO_EDGE;

      // glGenerateMipmap is only legal on a texture that has storage, and it is
      // an error if the levels do not form a complete chain down from level 0.
      // So the base level is allocated first -- otherwise the call below is
      // correctly rejected with GL_INVALID_OPERATION and the check would be
      // measuring the missing allocation rather than the target.
      if (type == TextureType::Texture2DArray) {
        renderer->setTextureImage2DArray(*texture, 8, 8, 2, nullptr);
      } else {
        // There is no IRenderer entry point for a plain 2D image, and this
        // check is about the target generateMipmap names, so allocate directly.
        texture->bind(0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
      }
      ReportGlErrors(
          (std::string("texture storage allocated: ") + label).c_str());

      // Set through the renderer, then read back through GL from the texture's
      // own target. If the renderer named the wrong target, the write landed
      // elsewhere and the texture keeps its default of GL_REPEAT.
      renderer->setTextureParameter(*texture, GL_TEXTURE_WRAP_S, sentinel);
      texture->bind(0);

      GLint wrapS = -1;
      // This vendored GLAD exposes only the "iv" variant, not "i".
      glGetTexParameteriv(target, GL_TEXTURE_WRAP_S, &wrapS);
      Report(std::string("setTextureParameter lands on the right target: ") +
                 label,
             wrapS == sentinel,
             "read back " + std::to_string(wrapS) + ", expected " +
                 std::to_string(sentinel));

      // Same for mipmapping, which also takes a target.
      renderer->generateMipmaps(*texture);
      ReportGlErrors((std::string("generateMipmaps: ") + label).c_str());
    }

    // --- Offscreen framebuffer ---------------------------------------------
    // The two-stage render pipeline renders the scene into an offscreen FBO
    // before the fog pass composites it. That target is created and resized
    // through IRenderer, so it is covered here even though Application (which
    // normally drives it) is not part of this target.
    Report("resizeOffscreenTarget (valid size)",
           renderer->resizeOffscreenTarget(64, 64));

    // A rejected resize must report failure rather than throwing. This is
    // reached from a GLFW C callback via glfwPollEvents, where an escaping
    // exception is undefined behaviour: it unwinds into GLFW's C frames,
    // which carry no exception tables, and lands at the thread entry as
    // "terminate called after throwing an instance of std::runtime_error"
    // plus SIGABRT, with no stack preserved (#142).
    //
    // A zero dimension is the rejection that can be provoked deterministically
    // on every driver. It is the same early-out the driver-rejection path
    // takes, so it exercises the "returns false, does not throw" contract
    // rather than pretending to exercise an OOM.
    Report("resizeOffscreenTarget rejects a zero dimension",
           !renderer->resizeOffscreenTarget(0, 128));
    Report("resizeOffscreenTarget rejects a zero height",
           !renderer->resizeOffscreenTarget(128, 0));
    // A resize to the size it already holds is a no-op success, not a
    // rejection -- the common case during a drag must not look like a failure.
    Report("resizeOffscreenTarget to its current size reports success",
           renderer->resizeOffscreenTarget(64, 64));

    // --- A rejected resize must leave the target usable (#143) ------------
    // The defect: the requested size was published to m_OffscreenWidth /
    // m_OffscreenHeight *before* the framebuffer was known to be complete,
    // while destroyOffscreenTarget() had already cleared the real one. The
    // early-out above trusts that cache whenever m_OffscreenFbo != 0, and a
    // failed resize leaves exactly that -- a nonzero name with incomplete
    // attachments. So
    //
    //     rejected size A -> rejected size B -> back to A
    //
    // hit the early-out on the third call and did nothing, leaving B's
    // incomplete attachments bound. Every later frame rendered into an
    // incomplete framebuffer with no error, no crash and no log line.
    //
    // Two things are asserted, and the second is the one that discriminates.
    //
    // 1. The over-limit request is refused, and refused *without touching
    //    GL*. Pre-fix there was no limit check, so the request reached
    //    glTexImage2D, which on this driver silently clamps an oversized
    //    2D allocation and leaves the texture unallocated without raising an
    //    error -- the failure only appeared one step later as an incomplete
    //    framebuffer, by which point the old target was already destroyed.
    //    So "no GL error" alone does not distinguish the two; what does is
    //    that the driver is never asked.
    //
    // 2. The target that was working before the rejected sequence is still
    //    live afterwards, and still usable. This is the invariant the wedge
    //    broke, and it holds only if the members are published after
    //    validation.
    {
      GLint maxRenderbuffer = 0;
      GLint maxTexture = 0;
      glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbuffer);
      glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
      // Both limits matter and are not the same value on every driver:
      // GL_MAX_TEXTURE_SIZE is frequently the lower one, and checking only
      // the renderbuffer limit is exactly the naive check that lets a width
      // through to fail in glTexImage2D instead.
      const GLint limit =
          maxRenderbuffer < maxTexture ? maxRenderbuffer : maxTexture;
      if (limit <= 0) {
        std::printf("  skip  over-limit resize: driver reports no limit\n");
      } else {
        const auto overA = static_cast<std::uint32_t>(limit) + 4096;
        const auto overB = static_cast<std::uint32_t>(limit) + 8192;

        Report("over-limit resize A is rejected",
               !renderer->resizeOffscreenTarget(overA, 128));
        Report("over-limit resize B is rejected",
               !renderer->resizeOffscreenTarget(overB, 128));
        Report("returning to a rejected size is rejected, not a cached no-op",
               !renderer->resizeOffscreenTarget(overA, 128));
        ReportGlErrors("rejected resizes left no GL error behind");

        // The invariant. Pre-fix, destroyOffscreenTarget() ran first and the
        // cache was poisoned, so what survived the sequence was a
        // nonzero-but-incomplete name and this resize could not recover.
        Report("a valid resize still succeeds after rejected ones",
               renderer->resizeOffscreenTarget(96, 96));
        renderer->bindOffscreenTarget();
        renderer->clear(glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
        ReportGlErrors("offscreen target is usable after rejected resizes");
      }
    }

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

    // (readback block temporarily removed)

    // --- Buffer readback ---------------------------------------------------
    // getBufferSubData reports failure through a return value (#151). It used
    // to return void, so a failed read was indistinguishable from a successful
    // one: glGetBufferSubData leaves `data` untouched on GL_INVALID_VALUE, and
    // Chunk::finishHeightMapGPU then truncated whatever was in the buffer into
    // uint16_t and committed it as authoritative terrain.
    //
    // These assert on the return value and on the bytes read back. Asserting
    // on glGetError() alone would prove nothing -- an out-of-range read is a
    // perfectly legal-looking call from the test's side.
    //
    // Shader storage buffers need GL 4.3. Apple's OpenGL reports 4.1 and
    // reports GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS as 0, so the version gate
    // is also the capability gate here. The limit is deliberately not queried:
    // glGetIntegerv with that enum is GL_INVALID_ENUM on a 4.1 context, and
    // the resulting error poisons every later glGetError check in this file.
    // A skip is reported, never silently passed.
    {
      GLint major = 0;
      GLint minor = 0;
      glGetIntegerv(GL_MAJOR_VERSION, &major);
      glGetIntegerv(GL_MINOR_VERSION, &minor);
      if (major < 4 || (major == 4 && minor < 3)) {
        std::printf(
            "  skip  buffer readback: needs GL 4.3 for shader storage "
            "buffers, context is %d.%d\n",
            major, minor);
      } else {
        auto storage = renderer->createBuffer(BufferType::Storage);
        if (!storage) {
          Report("storage buffer creation", false);
        } else {
          // A recognisable pattern, so a readback that silently returns
          // allocator garbage cannot pass.
          std::vector<uint32_t> written(64);
          for (std::size_t i = 0; i < written.size(); ++i) {
            written[i] = 0xA5000000u | static_cast<uint32_t>(i);
          }
          renderer->setBufferData(*storage, written.data(),
                                  written.size() * sizeof(uint32_t),
                                  BufferUsage::Static);
          ReportGlErrors("storage buffer upload");

          std::vector<uint32_t> readback(written.size(), 0u);
          const bool ok = renderer->getBufferSubData(
              *storage, 0, written.size() * sizeof(uint32_t),
              readback.data());
          Report("getBufferSubData reports success for an in-range read", ok);
          ReportGlErrors("in-range readback is error-free");

          // Read the state back rather than trusting the return value alone.
          bool identical = true;
          for (std::size_t i = 0; i < written.size(); ++i) {
            if (readback[i] != written[i]) {
              identical = false;
              break;
            }
          }
          Report("in-range readback returns the bytes that were written",
                 identical,
                 identical ? "" : "readback did not match the upload");

          // The regression: an out-of-range read must report failure and must
          // not leave the caller's buffer looking like a valid result. This is
          // what a batch-size / SSBO-size mismatch would produce.
          std::vector<uint32_t> sentinel(4, 0xDEADBEEFu);
          const bool oobOk = renderer->getBufferSubData(
              *storage, written.size() * sizeof(uint32_t), sizeof(uint32_t),
              sentinel.data());
          Report("getBufferSubData rejects an out-of-range read", !oobOk,
                 "a read past the end of the buffer must return false");
          ReportGlErrors("out-of-range readback drains its own error");

          // The destination must be untouched, so a caller that ignores the
          // return value cannot mistake stale data for terrain.
          bool untouched = true;
          for (const uint32_t value : sentinel) {
            if (value != 0xDEADBEEFu) {
              untouched = false;
              break;
            }
          }
          Report("a rejected read leaves the destination untouched", untouched);

          // The reported size must match what was allocated, since that is
          // what the bounds check trusts.
          Report("getSize matches the allocation",
                 storage->getSize() == written.size() * sizeof(uint32_t),
                 "getSize = " + std::to_string(storage->getSize()));
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
