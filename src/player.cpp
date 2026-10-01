#include "config.h"

#include "player.h"

#include <cmath>
#include "renderer/renderer.hpp"

Player::Player(const glm::vec3& position)
    : m_Velocity(0.0f, 0.0f, 0.0f), m_Camera(position), m_AllowJumping(false) {}

void Player::update(float deltaTime, bool hasGround, float groundY) {
  if (Constants::DO_GRAVITY) {
    m_Velocity.y -= Constants::Camera::ACCELERATION * deltaTime;
    if (m_Velocity.y < -Constants::Camera::MAX_VELOCITY) {
      m_Velocity.y = -Constants::Camera::MAX_VELOCITY;
    }

    m_Camera.m_Position.y += m_Velocity.y * deltaTime;

    // Only a ground the query actually found can stop the fall. Without the
    // guard, an unstreamed chunk's height is always above the player and this
    // branch always fires, so the player is lifted clear of the world and
    // never comes back down (#133).
    if (hasGround && m_Camera.m_Position.y <= groundY + 2.0f) {
      m_Camera.m_Position.y = groundY + 2.0f;
      m_Velocity.y = 0;
      m_AllowJumping = true;
    } else {
      m_AllowJumping = false;
    }
  }
}

void Player::jump(float cameraSpeed) {
  if (m_AllowJumping && Constants::DO_GRAVITY) {
    m_Velocity.y = Constants::Camera::JUMP_VELOCITY;
    m_AllowJumping = false;
  } else {
    m_Camera.m_Position.y += cameraSpeed;
  }
}

Camera* Player::getCamera() {
  return &m_Camera;
}

glm::mat4 Player::getView() {
  return m_Camera.getView();
}

glm::mat4 Player::getProjection() {
  return m_Camera.getProjection();
}

void Player::processKeyInput(IRenderer& renderer, float deltaTime) {
  float cameraSpeed = Constants::Camera::SPEED * deltaTime;
  float previousY = m_Camera.m_Position.y;

  if (renderer.isKeyPressed(Key::W)) {
    m_Camera.m_Position += cameraSpeed * m_Camera.m_Front;
  }
  if (renderer.isKeyPressed(Key::S)) {
    m_Camera.m_Position -= cameraSpeed * m_Camera.m_Front;
  }
  if (renderer.isKeyPressed(Key::A)) {
    m_Camera.m_Position -=
        glm::normalize(glm::cross(m_Camera.m_Front, m_Camera.m_Up)) *
        cameraSpeed;
  }
  if (renderer.isKeyPressed(Key::D)) {
    m_Camera.m_Position +=
        glm::normalize(glm::cross(m_Camera.m_Front, m_Camera.m_Up)) *
        cameraSpeed;
  }
  if (renderer.isKeyPressed(Key::Space)) {
    jump(cameraSpeed);
  }
  if (renderer.isKeyPressed(Key::LeftShift)) {
    if (!Constants::DO_GRAVITY) {
      m_Camera.m_Position[1] -= cameraSpeed;
    }
  }

  if (Constants::DO_GRAVITY) {
    m_Camera.m_Position.y = previousY;
  }
}

void Player::processMouseInput(double xPosition, double yPosition) {
  // The first event after the cursor is grabbed carries no relative motion at
  // all: it just reports where the platform says the pointer is. It exists only
  // to establish the baseline, so it is swallowed rather than integrated --
  // otherwise the view snaps by however far the pointer was from the centre of
  // the window, tens of degrees at SENSITIVITY = 0.2 (#139).
  const glm::vec2 cursor{static_cast<float>(xPosition),
                         static_cast<float>(yPosition)};
  if (!m_Camera.m_LastCursorPos.has_value()) {
    m_Camera.m_LastCursorPos = cursor;
    return;
  }

  float xOffset = cursor.x - m_Camera.m_LastCursorPos->x;
  float yOffset = m_Camera.m_LastCursorPos->y - cursor.y;

  m_Camera.m_LastCursorPos = cursor;

  xOffset *= Constants::Camera::SENSITIVITY;
  yOffset *= Constants::Camera::SENSITIVITY;

  m_Camera.m_Yaw += xOffset;
  m_Camera.m_Pitch += yOffset;

  m_Camera.m_Pitch =
      std::fmax(m_Camera.m_Pitch, Constants::Camera::PITCH_MIN);
  m_Camera.m_Pitch =
      std::fmin(m_Camera.m_Pitch, Constants::Camera::PITCH_MAX);

  glm::vec3 front;
  front.x = static_cast<float>(
      cos(glm::radians(m_Camera.m_Yaw)) * cos(glm::radians(m_Camera.m_Pitch)));
  front.y = static_cast<float>(sin(glm::radians(m_Camera.m_Pitch)));
  front.z = static_cast<float>(
      sin(glm::radians(m_Camera.m_Yaw)) * cos(glm::radians(m_Camera.m_Pitch)));

  m_Camera.m_Front = glm::normalize(front);
}
