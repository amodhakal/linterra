#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Plane {
  glm::vec3 point = {0.0f, 0.0f, 0.0f};
  glm::vec3 normal = {0.0f, 1.0f, 0.0f};
};

struct Camera {
  glm::vec3 m_Position, m_Front, m_Up, m_WorldUp;
  float m_Near, m_Far;

  float m_Aspect, m_Fov;
  float m_Yaw, m_Pitch;
  float m_LastX, m_LastY;
  bool m_IsFirstMouse;

  Camera(glm::vec3 position);

  /** Recompute the projection aspect from a framebuffer size.
   *
   * The aspect ratio has to track the window: both getProjection and the
   * frustum's side planes are built from it, so leaving it at its initial
   * value stretches the projection and desynchronises culling from rendering
   * after any resize. A zero height is ignored rather than dividing by it,
   * which is reachable while a window is being minimised. */
  void setAspect(std::uint32_t framebufferWidth,
                 std::uint32_t framebufferHeight);

  glm::mat4 getView();
  glm::mat4 getProjection();
  glm::vec3 getRight() const;
};
