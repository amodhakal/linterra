#include <cmath>

#include "config.h"
#include "doctest/doctest.h"
#include "player.h"

// Player's constructor, camera forwarding, and mouse-look are pure math and
// need no GL context, so they are directly testable now that player.cpp is in
// linterra_core.
//
// Not covered here, and why:
//   - processKeyInput needs a concrete IRenderer to query isKeyPressed against.
//     A mock renderer is #89's work.
//   - The gravity path in update()/jump() is gated on Constants::DO_GRAVITY,
//     which is a compile-time false, so it cannot be exercised at runtime
//     without changing that constant. #14 (M13) is the fix.

TEST_CASE("Player places its camera at the requested position") {
  const glm::vec3 spawn{1.0f, 2.0f, 3.0f};
  Player player(spawn);

  const Camera* camera = player.getCamera();
  REQUIRE(camera != nullptr);
  CHECK(camera->m_Position == spawn);
}

TEST_CASE("Player starts at rest") {
  Player player({0.0f, 0.0f, 0.0f});

  // With DO_GRAVITY off, no velocity accumulates and update() is a no-op.
  const glm::vec3 before = player.getCamera()->m_Position;
  player.update(0.016f, 0);
  CHECK(player.getCamera()->m_Position == before);
}

TEST_CASE("Player forwards view and projection from its camera") {
  Player player({4.0f, 5.0f, 6.0f});

  CHECK(player.getView() == player.getCamera()->getView());
  CHECK(player.getProjection() == player.getCamera()->getProjection());
}

TEST_CASE("Mouse look accumulates yaw and pitch") {
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  const float initialYaw = camera->m_Yaw;
  const float initialPitch = camera->m_Pitch;

  // Move the cursor right and up. Yaw follows the cursor; pitch uses the
  // inverted delta, so moving up decreases the stored pitch.
  player.processMouseInput(100.0, 100.0);

  CHECK(camera->m_Yaw != initialYaw);
  CHECK(camera->m_Pitch != initialPitch);
}

TEST_CASE("Mouse look clamps pitch to the configured limits") {
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  // A single huge downward jump must saturate at PITCH_MIN rather than letting
  // the view flip over the poles.
  player.processMouseInput(0.0, 100000.0);
  CHECK(camera->m_Pitch == Constants::Camera::PITCH_MIN);

  // ...and a single huge upward jump at PITCH_MAX.
  player.processMouseInput(0.0, -100000.0);
  CHECK(camera->m_Pitch == Constants::Camera::PITCH_MAX);
}

TEST_CASE("Mouse look keeps the view vector normalised") {
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  for (int i = 0; i < 50; ++i) {
    player.processMouseInput(100.0 + i * 37.0, 100.0 - i * 23.0);
    const float length = glm::length(camera->m_Front);
    CHECK(std::fabs(length - 1.0f) < 1e-5f);
  }
}

TEST_CASE("Mouse look measures deltas from the previous event") {
  // The first event must not be treated as a large movement. Camera's
  // m_LastX/m_LastY start at zero, so the first sample does produce a delta
  // equal to the absolute position -- which is the snap tracked in #139. Pin
  // the current behaviour here so the fix is a visible test change.
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  const float yawBefore = camera->m_Yaw;
  player.processMouseInput(500.0, 500.0);
  const float yawAfterFirst = camera->m_Yaw;
  CHECK(yawAfterFirst != yawBefore);

  // The very next event at the same coordinates must produce no rotation at
  // all, which is the part that is correct today.
  player.processMouseInput(500.0, 500.0);
  CHECK(camera->m_Yaw == yawAfterFirst);
}
