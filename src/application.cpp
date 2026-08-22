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

namespace {

// Return a human-readable description for an OpenGL error code so logs
// carry the full error information instead of just the numeric value.
const char* describeGlError(std::uint32_t err) {
  switch (err) {
    case 0x0500: return "GL_INVALID_ENUM: an unacceptable value was specified for an enumerated argument";
    case 0x0501: return "GL_INVALID_VALUE: a numeric argument is out of range";
    case 0x0502: return "GL_INVALID_OPERATION: the specified operation is not allowed in the current state";
    case 0x0503: return "GL_STACK_OVERFLOW: this command would cause a stack overflow";
    case 0x0504: return "GL_STACK_UNDERFLOW: this command would cause a stack underflow";
    case 0x0505: return "GL_OUT_OF_MEMORY: there is not enough memory left to execute the command";
    case 0x0506: return "GL_INVALID_FRAMEBUFFER_OPERATION: the framebuffer object is not complete";
    case 0x0507: return "GL_CONTEXT_LOST: the OpenGL context has been lost, due to a reset or driver failure";
    default: return "unknown GL error";
  }
}

}  // namespace

Application::Application(const char* title, const uint width, const uint height,
                         glm::vec4 bgColor)
    : m_Renderer(createRenderer(RenderBackend::OpenGL)),
      m_RenderShader(m_Renderer.get()),
      m_FogShader(m_Renderer.get()),
      m_ChunkManager(m_Renderer.get()),
      m_Player(Constants::Camera::DEFAULT_POSITION),
      m_TextureArray(m_Renderer.get()),
      m_BgColor(bgColor),
      m_lastFrame(0)

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
  m_Renderer->setScrollCallback(handleScrollCallback);
  m_Renderer->setFramebufferSizeCallback(handleResizeCallback);

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
        {"resources/blocks/grass_top.png", "resources/blocks/dirt.png",
         "resources/water.jpg"});
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
  m_Renderer->resizeOffscreenTarget(m_FrameWidth, m_FrameHeight);
}

Application::~Application() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  m_Renderer->terminateWindowing();
}

bool Application::isRunning() {
  return !m_Renderer->windowShouldClose();
}

void Application::update() {
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
  float deltaTime = currentFrame - m_lastFrame;
  m_lastFrame = currentFrame;

  return deltaTime;
}

void Application::processMouseInput(double xPosition, double yPosition) {
  return m_Player.processMouseInput(xPosition, yPosition);
}

void Application::processScrollInput([[maybe_unused]] double xOffset,
                                   [[maybe_unused]] double yOffset) {}

void Application::handleKeyPress(float deltaTime) {
  if (m_Renderer->isKeyPressed(Key::Escape)) {
    m_Renderer->setWindowShouldClose(true);
    return;
  }

  m_Player.processKeyInput(*m_Renderer, deltaTime);
}

void Application::handleResizeCallback(void* context, int width, int height) {
  auto* application = static_cast<Application*>(context);
  application->m_Renderer->setViewport(0, 0, width, height);
  application->m_FrameWidth = static_cast<std::uint32_t>(width);
  application->m_FrameHeight = static_cast<std::uint32_t>(height);
  application->m_Renderer->resizeOffscreenTarget(
      static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
}

void Application::handleMouseCallback(void* context, double xPosition,
                                      double yPosition) {
  auto* application = static_cast<Application*>(context);
  application->processMouseInput(xPosition, yPosition);
}

void Application::handleScrollCallback(void* context, double xOffset,
                                      double yOffset) {
  auto* application = static_cast<Application*>(context);
  application->processScrollInput(xOffset, yOffset);
}
