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
      m_Shader(m_Renderer.get()),
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
  glfwSwapInterval(0);

  glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetCursorPosCallback(m_Window, handleMouseCallback);
  glfwSetScrollCallback(m_Window, handleScrollCallback);
  glfwSetFramebufferSizeCallback(m_Window, handleResizeCallback);
  glfwSetWindowUserPointer(m_Window, this);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    throw std::runtime_error("Failed to initialize GLAD");
  }

  m_Shader.load(Constants::VERTEX_PATH, Constants::FRAGMENT_PATH);
  try {
    m_TextureArray.loadFromFiles(
        {"resources/blocks/grass_top.png", "resources/blocks/dirt.png"});
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Failed to load textures: ") +
                            e.what());
  }

  m_TextureArray.bindToUnit(0);

  m_ChunkManager.load();

  m_Renderer->enable(Feature::DepthTest);
  m_Renderer->clear(bgColor);

  m_Shader.newUniform("uModel");
  m_Shader.newUniform("uView");
  m_Shader.newUniform("uProjection");
  m_Shader.newUniform("uTextureArray");
  m_Shader.newUniform("uFogStart");
  m_Shader.newUniform("uFogEnd");
  m_Shader.newUniform("uFogColor");

  m_Shader.use();
  m_Shader.setUniformInt("uTextureArray", 0);
  m_Shader.setUniformFloat("uFogStart", Constants::Chunk::FOG_START);
  m_Shader.setUniformFloat("uFogEnd", Constants::Chunk::FOG_END);
  m_Shader.setUniformVec3("uFogColor", Constants::FOG_COLOR);
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
  m_Renderer->clear(m_BgColor);

  glm::mat4 view = m_Player.getView();
  glm::mat4 projection = m_Player.getProjection();

  m_Shader.setUniformMat4("uView", view);
  m_Shader.setUniformMat4("uProjection", projection);

  auto cameraPtr = m_Player.getCamera();
  m_ChunkManager.render(cameraPtr, m_Shader);

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
  glViewport(0, 0, width, height);
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