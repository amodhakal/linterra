#pragma once

#include <memory>
#include <print>

#include "config.h"
#include "manager.h"
#include "player.h"
#include "renderer/renderer_fwd.hpp"
#include "shader.h"
#include "texture.h"

class IRenderer;

class Application {
 public:
  Application(const char* title, const std::uint32_t width = Constants::SCR_WIDTH,
              const std::uint32_t height = Constants::SCR_HEIGHT,
              glm::vec4 bgColor = Constants::BG_COLOR);
  ~Application();

  bool isRunning();
  void update();

 private:
  std::unique_ptr<IRenderer> m_Renderer;
  Shader m_RenderShader;   // scene pass: textures + lighting, no fog
  Shader m_FogShader;      // post pass: composites fog over the scene
  std::unique_ptr<IVertexArray> m_FullscreenVao;  // empty VAO for the fog pass
  ChunkManager m_ChunkManager;
  Player m_Player;
  Texture m_TextureArray;

  glm::vec4 m_BgColor;
  float m_lastFrame;
  std::uint32_t m_FrameWidth;   // drawing-buffer size (pixels, may differ from
  std::uint32_t m_FrameHeight;  // window logical size on HiDPI displays)

  float getDeltaTime();

  void processMouseInput(double xPosition, double yPosition);
  void processScrollInput(double xOffset, double yOffset);

  void handleKeyPress(float deltaTime);
  static void handleResizeCallback(void* context, int width, int height);
  static void handleMouseCallback(void* context, double xPosition,
                                  double yPosition);
  static void handleScrollCallback(void* context, double xOffset,
                                   double yOffset);
};
