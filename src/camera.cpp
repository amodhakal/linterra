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
  setAspect(Constants::SCR_WIDTH, Constants::SCR_HEIGHT);

  m_Yaw = Constants::Camera::DEFAULT_YAW;
  m_Pitch = Constants::Camera::DEFAULT_PITCH;
}

glm::mat4 Camera::getView() {
  return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
}

void Camera::setAspect(std::uint32_t framebufferWidth,
                       std::uint32_t framebufferHeight) {
  // A minimised window reports a zero dimension. Keep the previous value
  // rather than producing an infinite or NaN aspect, which would poison both
  // the projection and the frustum's side planes.
  if (framebufferHeight == 0) {
    return;
  }
  m_Aspect = static_cast<float>(framebufferWidth) /
             static_cast<float>(framebufferHeight);
}

glm::mat4 Camera::getProjection() {
  return glm::perspective(glm::radians(m_Fov), m_Aspect,
                          m_Near, m_Far);
}

glm::vec3 Camera::getRight() const {
  return glm::normalize(glm::cross(m_Front, m_Up));
}