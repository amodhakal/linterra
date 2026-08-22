#include "camera.h"

#include <cmath>
#include <print>

#include "config.h"

Camera::Camera(glm::vec3 position) {
  m_Position = position;
  m_Front = Constants::Camera::DEFAULT_FRONT;
  m_Up = Constants::Camera::DEFAULT_UP;
  m_WorldUp = m_Up;

  m_Near = Constants::Camera::NEAR;
  m_Far = Constants::Camera::FAR;

  m_Fov = Constants::Camera::DEFAULT_FOV;
  m_Aspect = (float)Constants::SCR_WIDTH / (float)Constants::SCR_HEIGHT;

  m_Yaw = Constants::Camera::DEFAULT_YAW;
  m_Pitch = Constants::Camera::DEFAULT_PITCH;

  m_LastX = Constants::SCR_WIDTH / 2.0;
  m_LastY = Constants::SCR_HEIGHT / 2.0;

  m_IsFirstMouse = true;
}

// void Camera::processScrollInput(double xOffset, double yOffset) {
//   m_Fov -= yOffset;
//   m_Fov = fmax(m_Fov, Constants::Camera::FOV_MIN);
//   m_Fov = fmin(m_Fov, Constants::Camera::FOV_MAX);
// }

glm::mat4 Camera::getView() {
  return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
}

glm::mat4 Camera::getProjection() {
  return glm::perspective(glm::radians(m_Fov), m_Aspect,
                          m_Near, m_Far);
}

glm::vec3 Camera::getRight() const {
  return glm::normalize(glm::cross(m_Front, m_Up));
}