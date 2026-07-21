#include "config.h"

#include "player.h"

#include <cmath>
#include "renderer/renderer.hpp"

Player::Player(const glm::vec3& position)
    : m_Camera(position), m_Velocity(0.0f, 0.0f, 0.0f), m_AllowJumping(false) {}

void Player::update(float deltaTime, std::int32_t currentY) {
  if (Constants::DO_GRAVITY) {
    m_Velocity.y -= Constants::Camera::ACCELERATION * deltaTime;
    if (m_Velocity.y < -Constants::Camera::MAX_VELOCITY) {
      m_Velocity.y = -Constants::Camera::MAX_VELOCITY;
    }

    m_Camera.m_Position.y += m_Velocity.y * deltaTime;

    if (m_Camera.m_Position.y <= static_cast<float>(currentY) + 2.0f) {
      m_Camera.m_Position.y = static_cast<float>(currentY) + 2.0f;
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
    m_AllowJumping = true;
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
  float xOffset = static_cast<float>(xPosition - m_Camera.m_LastX);
  float yOffset = static_cast<float>(m_Camera.m_LastY - yPosition);

  m_Camera.m_LastX = xPosition;
  m_Camera.m_LastY = yPosition;

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
