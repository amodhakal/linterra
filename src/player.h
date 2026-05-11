#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "camera.h"

struct GLFWwindow;

class Player {
 public:
  Player(const glm::vec3& position);
  void update(float deltaTime, std::int32_t currentY);
  void jump(float cameraSpeed);

  Camera* getCamera();

  glm::mat4 getView();
  glm::mat4 getProjection();

  void processKeyInput(GLFWwindow* window, float deltaTime);
  void processMouseInput(double xPosition, double yPosition);

 private:
  glm::vec3 m_Velocity;
  Camera m_Camera;

  bool m_AllowJumping;
};
