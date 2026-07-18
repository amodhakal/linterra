#include "application.h"

#include <cstdint>

#include <glm/gtc/type_ptr.hpp>
#include <print>
#include <string>
#include <vector>

#include "config.h"
#include "player.h"
#include "renderer/renderer.hpp"
#include "shader.h"

Application::Application(const char* title, const uint width, const uint height,
                         glm::vec4 bgColor)
    : m_Renderer(createRenderer(RenderBackend::OpenGL)),
      m_RenderShader(m_Renderer.get()),
      m_FogShader(m_Renderer.get()),
      m_ChunkManager(m_Renderer.get()),
      m_Player(Constants::Camera::DEFAULT_POSITION),
      m_TextureArray(m_Renderer.get()),
      m_BgColor(bgColor),
      m_FpsAttempts(0),
      m_CombinedDeltaTime(0),
      m_lastFrame(0)

{
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!m_Window) {
    throw std::runtime_error("Failed to create window");
  }

  glfwMakeContextCurrent(m_Window);
  glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetCursorPosCallback(m_Window, handleMouseCallback);
  glfwSetScrollCallback(m_Window, handleScrollCallback);
  glfwSetFramebufferSizeCallback(m_Window, handleResizeCallback);
  glfwSetWindowUserPointer(m_Window, this);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    throw std::runtime_error("Failed to initialize GLAD");
  }

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
  glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
  m_FrameWidth = static_cast<std::uint32_t>(fbWidth);
  m_FrameHeight = static_cast<std::uint32_t>(fbHeight);
  m_Framebuffer.resize(m_FrameWidth, m_FrameHeight);
}

Application::~Application() {
  glfwTerminate();
}

bool Application::isRunning() {
  return !glfwWindowShouldClose(m_Window);
}

void Application::update() {
  float deltaTime = getDeltaTime();
  handleKeyPress(deltaTime);

  glm::mat4 view = m_Player.getView();
  glm::mat4 projection = m_Player.getProjection();

  // --- Pass 1: render the scene into the offscreen framebuffer ---
  m_Framebuffer.bind();
  m_Renderer->setViewport(0, 0, static_cast<int>(m_Framebuffer.getWidth()),
                          static_cast<int>(m_Framebuffer.getHeight()));
  m_Renderer->clear(m_BgColor);

  m_RenderShader.use();
  m_RenderShader.setUniformMat4("uView", view);
  m_RenderShader.setUniformMat4("uProjection", projection);

  auto cameraPtr = m_Player.getCamera();
  m_ChunkManager.render(cameraPtr, m_RenderShader);

  // --- Pass 2: fog post-process onto the default framebuffer ---
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_Renderer->setViewport(0, 0, static_cast<int>(m_FrameWidth),
                          static_cast<int>(m_FrameHeight));
  m_Renderer->clear(m_BgColor);

  m_Framebuffer.bindColorTexture(0);
  m_FogShader.use();
  m_Renderer->bindVertexArray(*m_FullscreenVao);
  m_Renderer->draw(PrimitiveType::Triangles, 3, 0);  // fullscreen triangle

  glfwSwapBuffers(m_Window);
  glfwPollEvents();

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    std::println("OpenGL Error: {}", err);
  }

  glm::vec3 cameraPosition = m_Player.getCamera()->m_Position;
  float highestY = m_ChunkManager.getPositionHighestY(cameraPosition);
  m_Player.update(deltaTime, highestY);
}

float Application::getDeltaTime() {
  float currentFrame = glfwGetTime();
  float deltaTime = currentFrame - m_lastFrame;
  m_lastFrame = currentFrame;

  m_CombinedDeltaTime += deltaTime;
  m_FpsAttempts++;

  constexpr std::uint32_t MAX_ATTEMPTS = 100;
  if (m_FpsAttempts >= MAX_ATTEMPTS) {
    std::println("FPS: {}",
                 std::to_string(1 / (m_CombinedDeltaTime / MAX_ATTEMPTS)));
    m_CombinedDeltaTime = 0;
    m_FpsAttempts = 0;
  }

  return deltaTime;
}

void Application::processMouseInput(double xPosition, double yPosition) {
  return m_Player.processMouseInput(xPosition, yPosition);
}

void Application::processScrollInput([[maybe_unused]] double xOffset,
                                   [[maybe_unused]] double yOffset) {}

void Application::handleKeyPress(float deltaTime) {
  if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(m_Window, true);
    return;
  }

  m_Player.processKeyInput(m_Window, deltaTime);
}

void Application::handleResizeCallback(GLFWwindow* window, int width,
                                       int height) {
  auto* context = static_cast<Application*>(glfwGetWindowUserPointer(window));
  glViewport(0, 0, width, height);
  context->m_FrameWidth = static_cast<std::uint32_t>(width);
  context->m_FrameHeight = static_cast<std::uint32_t>(height);
  context->m_Framebuffer.resize(static_cast<uint>(width),
                                static_cast<uint>(height));
}

void Application::handleMouseCallback(GLFWwindow* window, double xPosition,
                                      double yPosition) {
  auto* context = static_cast<Application*>(glfwGetWindowUserPointer(window));
  context->processMouseInput(xPosition, yPosition);
}

void Application::handleScrollCallback(GLFWwindow* window, double xOffset,
                                      double yOffset) {
  auto* context = static_cast<Application*>(glfwGetWindowUserPointer(window));
  context->processScrollInput(xOffset, yOffset);
}
