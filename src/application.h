#pragma once

#include <memory>
#include <print>

#include "config.h"
#include "framebuffer.h"
#include "manager.h"
#include "player.h"
#include "shader.h"
#include "texture.h"

class IRenderer;

class Application {
 public:
  Application(const char* title, const uint width = Constants::SCR_WIDTH,
              const uint height = Constants::SCR_HEIGHT,
              glm::vec4 bgColor = Constants::BG_COLOR);
  ~Application();

  bool isRunning();
  void update();

 private:
  GLFWwindow* m_Window;
  std::unique_ptr<IRenderer> m_Renderer;
  Shader m_RenderShader;   // scene pass: textures + lighting, no fog
  Shader m_FogShader;      // post pass: composites fog over the scene
  Framebuffer m_Framebuffer;
  std::unique_ptr<IVertexArray> m_FullscreenVao;  // empty VAO for the fog pass
  ChunkManager m_ChunkManager;
  Player m_Player;
  Texture m_TextureArray;

  glm::vec4 m_BgColor;
  uint m_FpsAttempts;
  float m_CombinedDeltaTime;
  float m_lastFrame;
  std::uint32_t m_FrameWidth;   // drawing-buffer size (pixels, may differ from
  std::uint32_t m_FrameHeight;  // window logical size on HiDPI displays)

  float getDeltaTime();

  void processMouseInput(double xPosition, double yPosition);
  void processScrollInput(double xOffset, double yOffset);

  void handleKeyPress(float deltaTime);
  static void handleResizeCallback(GLFWwindow* window, int width, int height);
  static void handleMouseCallback(GLFWwindow* window, double xPosition,
                                  double yPosition);
  static void handleScrollCallback(GLFWwindow* window, double xOffset,
                                   double yOffset);
};
