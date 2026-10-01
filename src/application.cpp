#include "application.h"

#include <cstdint>

#include <glm/gtc/type_ptr.hpp>
#include <print>
#include <string>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "config.h"
#include "player.h"
#include "renderer/renderer.hpp"
#include "shader.h"

Application::Application(const char* title, const std::uint32_t width, const std::uint32_t height,
                         glm::vec4 bgColor)
    : m_Renderer(createRenderer(RenderBackend::OpenGL)),
      m_RenderShader(m_Renderer.get()),
      m_FogShader(m_Renderer.get()),
      m_ChunkManager(m_Renderer.get()),
      m_Player(Constants::Camera::DEFAULT_POSITION),
      m_TextureArray(m_Renderer.get()),
      m_BgColor(bgColor),
      m_LastFrame(0)

{
  m_Renderer->initializeWindowing();
  m_Renderer->configureWindowHints();

  if (!m_Renderer->createWindow(static_cast<int>(width), static_cast<int>(height),
                                title)) {
    throw std::runtime_error("Failed to create window");
  }

  m_Renderer->makeContextCurrent();
  m_Renderer->setCursorDisabled();
  m_Renderer->setEventContext(this);
  m_Renderer->setCursorPosCallback(handleMouseCallback);

  if (!m_Renderer->loadContextFunctions()) {
    throw std::runtime_error("Failed to initialize GLAD");
  }

  // Initialize ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(m_Renderer->getNativeWindow()), true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  // Empty VAO required to issue the attribute-less fullscreen-triangle draw
  // in a core OpenGL context.
  m_FullscreenVao = m_Renderer->createVertexArray();

  m_RenderShader.load(Constants::RENDER_VERTEX_PATH,
                      Constants::RENDER_FRAGMENT_PATH);
  m_FogShader.load(Constants::FOG_VERTEX_PATH, Constants::FOG_FRAGMENT_PATH);

  try {
    m_TextureArray.loadFromFiles(
        {Constants::GRASS_TOP_TEXTURE_PATH, Constants::DIRT_TEXTURE_PATH,
         Constants::WATER_TEXTURE_PATH});
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Failed to load textures: ") +
                            e.what());
  }

  m_TextureArray.bindToUnit(0);

  m_ChunkManager.load();

  m_Renderer->enable(Feature::DepthTest);
  m_Renderer->enable(Feature::Culling);

  // Scene pass uniforms.
  m_RenderShader.newUniform("uModel");
  m_RenderShader.newUniform("uView");
  m_RenderShader.newUniform("uProjection");
  m_RenderShader.newUniform("uTextureArray");
  m_RenderShader.newUniform("uFogEnd");
  m_RenderShader.use();
  m_RenderShader.setUniformInt("uTextureArray", 0);
  m_RenderShader.setUniformFloat("uFogEnd", Constants::Chunk::FOG_END);

  // Fog post-process uniforms.
  m_FogShader.newUniform("uScene");
  m_FogShader.newUniform("uFogStart");
  m_FogShader.newUniform("uFogEnd");
  m_FogShader.newUniform("uFogColor");
  m_FogShader.use();
  m_FogShader.setUniformInt("uScene", 0);
  m_FogShader.setUniformFloat("uFogStart", Constants::Chunk::FOG_START);
  m_FogShader.setUniformFloat("uFogEnd", Constants::Chunk::FOG_END);
  m_FogShader.setUniformVec3("uFogColor", Constants::FOG_COLOR);

  // Allocate the offscreen target at the current (drawing-buffer) size.
  int fbWidth = 0, fbHeight = 0;
  m_Renderer->getFramebufferSize(&fbWidth, &fbHeight);
  m_FrameWidth = static_cast<std::uint32_t>(fbWidth);
  m_FrameHeight = static_cast<std::uint32_t>(fbHeight);
  // Guarded: if the process starts with a 0x0 drawable -- headless, or a
  // window minimized before the first frame -- there is nothing to allocate,
  // and without this the engine would hold no valid render target for its
  // entire lifetime, because the only re-allocation trigger is a resize
  // callback that may never fire with a non-zero size (#147).
  if (m_FrameWidth > 0 && m_FrameHeight > 0) {
    m_Renderer->resizeOffscreenTarget(m_FrameWidth, m_FrameHeight);
  }

  // Only now is the resize callback safe to arm. Both preconditions hold:
  // GLAD has resolved the entry points, so handleResizeCallback no longer
  // calls through null function pointers; and the offscreen target exists, so
  // resizeOffscreenTarget has something to act on.
  //
  // Registering this before loadContextFunctions() left a window in which
  // GLFW could dispatch a framebuffer-size event into an unarmed context.
  // That window is not theoretical on the primary target: the __APPLE__
  // branches make this the HiDPI configuration, where glfwGetFramebufferSize
  // on a fresh window commonly differs from the requested 800x600, and GLFW
  // fires the initial framebuffer-size event on the first glfwPollEvents.
  m_Renderer->setFramebufferSizeCallback(handleResizeCallback);
}

Application::~Application() {
  // Only the ImGui teardown belongs here. Windowing is torn down by
  // ~OpenGLRenderer, which runs after this body and after every member below
  // has been destroyed -- so the context outlives every GL object (#128).
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

bool Application::isRunning() {
  return !m_Renderer->windowShouldClose();
}

void Application::update() {
  // A minimized window reports a 0x0 drawing buffer -- on macOS that is what
  // GLFW delivers, and it fires the resize callback with 0, 0.
  //
  // There is no valid render target at that size: resizeOffscreenTarget
  // refuses to allocate one, because a 0x0 framebuffer attachment is itself
  // a GL_INVALID_VALUE. So without this guard the frame proceeds with no
  // target and three things silently degrade to the default objects:
  //
  //   bindOffscreenTarget()      -> glBindFramebuffer(GL_FRAMEBUFFER, 0),
  //                                 i.e. the window's back buffer, so the
  //                                 scene pass draws to the swap chain;
  //   bindOffscreenColorTexture(0) -> binds texture 0, and sampling texture 0
  //                                 while framebuffer 0 is bound is a
  //                                 framebuffer/texture feedback loop, which
  //                                 is GL_INVALID_OPERATION every frame;
  //   draw                       -> rasterizes nothing through a 0x0 viewport.
  //
  // The error drain then prints one line per frame for as long as the window
  // stays minimized -- thousands of lines a second in the background, on a
  // minimized app (#147).
  if (m_FrameWidth == 0 || m_FrameHeight == 0) {
    m_Renderer->pollEvents();
    return;
  }

  float deltaTime = getDeltaTime();
  handleKeyPress(deltaTime);

  glm::mat4 view = m_Player.getView();
  glm::mat4 projection = m_Player.getProjection();

  // --- Pass 1: render the scene into the offscreen framebuffer ---
  m_Renderer->bindOffscreenTarget();
  m_Renderer->setViewport(0, 0, static_cast<int>(m_FrameWidth),
                          static_cast<int>(m_FrameHeight));
  m_Renderer->clear(m_BgColor);

  m_RenderShader.use();
  m_RenderShader.setUniformMat4("uView", view);
  m_RenderShader.setUniformMat4("uProjection", projection);

  auto cameraPtr = m_Player.getCamera();
  m_ChunkManager.render(cameraPtr, m_RenderShader);

  // --- Pass 2: fog post-process onto the default framebuffer ---
  m_Renderer->bindFramebuffer(0);
  m_Renderer->setViewport(0, 0, static_cast<int>(m_FrameWidth),
                          static_cast<int>(m_FrameHeight));
  m_Renderer->clear(m_BgColor);

  m_Renderer->bindOffscreenColorTexture(0);
  m_FogShader.use();
  m_Renderer->bindVertexArray(*m_FullscreenVao);
  m_Renderer->draw(PrimitiveType::Triangles, 3, 0);  // fullscreen triangle

  // --- Pass 3: ImGui UI ---
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 280.0f, 10.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(270.0f, 110.0f), ImGuiCond_Always);
  ImGui::Begin("Debug Info", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
  const float fps = io.Framerate;
  const float ms_per_frame = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
  ImGui::Text("FPS: %.1f (%.3f ms)", fps, ms_per_frame);
  if (auto cam = m_Player.getCamera()) {
    ImGui::Text("Pos: X: %.2f Y: %.2f Z: %.2f", cam->m_Position.x, cam->m_Position.y, cam->m_Position.z);
    ImGui::Text("Dir: X: %.2f Y: %.2f Z: %.2f", cam->m_Front.x, cam->m_Front.y, cam->m_Front.z);
  }
  ImGui::End();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  m_Renderer->swapBuffers();
  m_Renderer->pollEvents();

  // Drain all pending OpenGL errors, printing each with a human-readable
  // description so the numeric code alone doesn't have to be looked up.
  for (int drained = 0; drained < 16; ++drained) {
    std::uint32_t err = m_Renderer->getLastError();
    if (err == 0) {
      break;
    }
    std::println("OpenGL Error: {} ({:#06x})", describeGlError(err), err);
  }

  glm::vec3 cameraPosition = m_Player.getCamera()->m_Position;
  float highestY = m_ChunkManager.getPositionHighestY(cameraPosition);
  m_Player.update(deltaTime, highestY);
}

float Application::getDeltaTime() {
  float currentFrame = m_Renderer->getTimeSeconds();
  if (m_FirstFrame) {
    // The clock has been running since renderer init, so the raw delta on the
    // first frame is huge and would teleport the player. Treat the first
    // frame as a zero-length step instead.
    m_FirstFrame = false;
    m_LastFrame = currentFrame;
    return 0.0f;
  }
  float deltaTime = currentFrame - m_LastFrame;
  m_LastFrame = currentFrame;

  return deltaTime;
}

void Application::processMouseInput(double xPosition, double yPosition) {
  return m_Player.processMouseInput(xPosition, yPosition);
}

void Application::handleKeyPress(float deltaTime) {
  if (m_Renderer->isKeyPressed(Key::Escape)) {
    m_Renderer->setWindowShouldClose(true);
    return;
  }

  m_Player.processKeyInput(*m_Renderer, deltaTime);
}

void Application::handleResizeCallback(void* context, int width, int height) {
  auto* application = static_cast<Application*>(context);

  // Clamp before narrowing. GLFW reports 0 for a minimized window, and
  // casting a negative to uint32_t would wrap to ~4 billion rather than 0,
  // which would sail past the 0 checks everywhere downstream (#147).
  const auto safeWidth = static_cast<std::uint32_t>(std::max(width, 0));
  const auto safeHeight = static_cast<std::uint32_t>(std::max(height, 0));
  application->m_FrameWidth = safeWidth;
  application->m_FrameHeight = safeHeight;

  // A 0x0 resize is recorded but acted on no further: there is no render
  // target at that size, and Application::update now skips the frame for the
  // same reason. Without this, setViewport(0, 0, 0, 0) and the aspect-ratio
  // update would run against a size at which nothing can be drawn.
  if (safeWidth == 0 || safeHeight == 0) {
    return;
  }

  application->m_Renderer->setViewport(0, 0, width, height);
  // The projection and the frustum's side planes are both built from the
  // aspect ratio, so it has to be updated here too -- otherwise a resize
  // stretches the view and leaves culling describing a different shape than
  // the one being drawn.
  application->m_Player.getCamera()->setAspect(application->m_FrameWidth,
                                                application->m_FrameHeight);

  // Reported, not thrown: this runs inside a GLFW C callback, where an
  // escaping exception is undefined behaviour. resizeOffscreenTarget has
  // already written a diagnostic naming the size and the GL status, so
  // repeating it here would add nothing -- what matters is that the rejection
  // is not swallowed by a caller that looks like it succeeded.
  if (!application->m_Renderer->resizeOffscreenTarget(
          static_cast<std::uint32_t>(width),
          static_cast<std::uint32_t>(height))) {
    // The offscreen target still holds the previous size. m_FrameWidth /
    // m_FrameHeight have already been updated above, so they now disagree
    // with it; #143 fixes that ordering by validating before publishing.
    std::println(stderr,
                 "resize rejected by the driver; the offscreen target is "
                 "still {}x{}",
                 application->m_FrameWidth, application->m_FrameHeight);
  }
}

void Application::handleMouseCallback(void* context, double xPosition,
                                      double yPosition) {
  auto* application = static_cast<Application*>(context);
  application->processMouseInput(xPosition, yPosition);
}
