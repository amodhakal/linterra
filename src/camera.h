#pragma once

#include <cstdint>
#include <optional>

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

  /** Last reported cursor position, or unset before the first movement event.
   *
   *  There is no meaningful value to seed this with: GLFW reports the
   *  platform's absolute cursor position once the pointer is grabbed, and that
   *  is essentially never the centre of the window. Seeding from
   *  SCR_WIDTH/SCR_HEIGHT (as this used to) made the first event carry a
   *  spurious delta of however far the pointer was from (400, 300), which at
   *  SENSITIVITY = 0.2 is tens of degrees of yaw on the first mouse movement.
   *  Being unset until the first event is what lets Player::processMouseInput
   *  swallow that event instead (#139). */
  std::optional<glm::vec2> m_LastCursorPos;

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
