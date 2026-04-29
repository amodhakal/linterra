#pragma once

#include <print>

#include "config.h"
#include "manager.h"
#include "player.h"
#include "shader.h"
#include "texture.h"

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
  Shader m_Shader;
  ChunkManager m_ChunkManager;
  Player m_Player;
  Texture m_TextureArray;

  uint m_FpsAttempts;
  float m_CombinedDeltaTime;
  float m_lastFrame;

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
