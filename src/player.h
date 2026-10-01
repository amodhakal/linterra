#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "camera.h"
#include "renderer/renderer_fwd.hpp"

class Player {
 public:
  Player(const glm::vec3& position);

  // Advance one step.
  //
  // `hasGround` is false when the chunk under the player has not streamed in
  // yet. It has to be a separate signal rather than a magic height: the
  // ground query used to answer Chunk::HEIGHT (256) for a missing chunk, which
  // is above the player, so the contact test below always passed and the
  // player was snapped *up* to 258 with nothing to fall onto (#133). With the
  // flag, a missing chunk means "keep falling", and the situation resolves
  // itself as soon as the chunk arrives.
  void update(float deltaTime, bool hasGround, float groundY);
  void jump(float cameraSpeed);

  Camera* getCamera();

  glm::mat4 getView();
  glm::mat4 getProjection();

  void processKeyInput(IRenderer& renderer, float deltaTime);
  void processMouseInput(double xPosition, double yPosition);

 private:
  glm::vec3 m_Velocity;
  Camera m_Camera;

  bool m_AllowJumping;
};
