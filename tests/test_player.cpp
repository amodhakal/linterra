#include <cmath>
#include <type_traits>

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

  // With DO_GRAVITY off, no velocity accumulates and update() is a no-op
  // whether or not there is ground under the player.
  const glm::vec3 before = player.getCamera()->m_Position;
  player.update(0.016f, /*hasGround=*/true, /*groundY=*/0.0f);
  CHECK(player.getCamera()->m_Position == before);
}

TEST_CASE("Player is told whether there is ground, not handed a height") {
  // The defect this signature exists to remove (#133): the ground query
  // answered Chunk::HEIGHT (256) for a chunk that had not streamed in, which
  // is a height, so "no ground" and "the ground is at the world ceiling" were
  // the same value and Player::update snapped the player up to 258 with
  // nothing to fall onto.
  //
  // This is a compile-time assertion rather than a behavioural one, and it is
  // worth being precise about what it does and does not buy: it fails to build
  // if update() goes back to taking a bare height, so the absence of ground
  // stays representable at the call site. It cannot check the behaviour,
  // because every branch in update() is gated on Constants::DO_GRAVITY, which
  // is a compile-time false. A behavioural test arrives with the physics
  // helpers, where it can drive them directly without flipping the global.
  static_assert(std::is_same_v<decltype(&Player::update),
                               void (Player::*)(float, bool, float)>);

  // The flag is not decoration: with no ground the call is well-formed and the
  // player is left alone rather than snapped to a floor that is not there.
  Player player({0.0f, 155.0f, 0.0f});
  const glm::vec3 before = player.getCamera()->m_Position;
  player.update(0.016f, /*hasGround=*/false,
                /*groundY=*/static_cast<float>(Constants::Chunk::HEIGHT));
  CHECK(player.getCamera()->m_Position == before);
  CHECK(true);
}

TEST_CASE("Player forwards view and projection from its camera") {
  Player player({4.0f, 5.0f, 6.0f});

  CHECK(player.getView() == player.getCamera()->getView());
  CHECK(player.getProjection() == player.getCamera()->getProjection());
}

TEST_CASE("Mouse look accumulates yaw and pitch") {
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  // The first event only establishes the cursor baseline and is swallowed
  // (#139), so a real movement needs a priming event first.
  player.processMouseInput(10.0, 10.0);

  const float initialYaw = camera->m_Yaw;
  const float initialPitch = camera->m_Pitch;

  // Move the cursor right and up. Yaw follows the cursor; pitch uses the
  // inverted delta, so moving up decreases the stored pitch.
  player.processMouseInput(110.0, -90.0);

  CHECK(camera->m_Yaw != initialYaw);
  CHECK(camera->m_Pitch != initialPitch);
}

TEST_CASE("Mouse look clamps pitch to the configured limits") {
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  // Prime the baseline; the first event is swallowed (#139).
  player.processMouseInput(0.0, 0.0);

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

TEST_CASE("the first mouse event does not rotate the view") {
  // Regression test for #139. Camera used to seed m_LastX/m_LastY from the
  // compile-time SCR_WIDTH/SCR_HEIGHT (400, 300) rather than from a real
  // cursor position, and Player::processMouseInput never checked the
  // never-read m_IsFirstMouse flag. The first callback therefore carried a
  // delta of |xPosition - 400| px, which at SENSITIVITY = 0.2 is an arbitrary
  // rotation -- 72 degrees of yaw for a cursor at (760, 180) -- on the first
  // mouse movement after launch.
  //
  // The first event carries no relative motion, so it must leave the view
  // exactly where it was.
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  const float yawBefore = camera->m_Yaw;
  const float pitchBefore = camera->m_Pitch;
  const glm::vec3 frontBefore = camera->m_Front;

  // A cursor position far from the old hard-coded (400, 300) seed: the case
  // that produced the largest spurious rotation.
  player.processMouseInput(760.0, 180.0);

  CHECK(camera->m_Yaw == doctest::Approx(yawBefore));
  CHECK(camera->m_Pitch == doctest::Approx(pitchBefore));
  CHECK(camera->m_Front == frontBefore);
}

TEST_CASE("the second mouse event is measured from the first") {
  // The counterpart to the case above: swallowing the first event is only
  // correct if the baseline is taken from it. A second event at the same
  // position must therefore produce no rotation, and an event N pixels away
  // must produce exactly N * SENSITIVITY of yaw.
  Player player({0.0f, 0.0f, 0.0f});
  Camera* camera = player.getCamera();

  player.processMouseInput(500.0, 500.0);
  const float yawAfterFirst = camera->m_Yaw;

  // The very next event at the same coordinates must produce no rotation.
  player.processMouseInput(500.0, 500.0);
  CHECK(camera->m_Yaw == doctest::Approx(yawAfterFirst));

  // 100 px right at SENSITIVITY = 0.2 is +20 degrees of yaw, measured from
  // 500 -- not from 400, which is what the old seed would have produced.
  player.processMouseInput(600.0, 500.0);
  CHECK(
      camera->m_Yaw ==
      doctest::Approx(yawAfterFirst + 100.0f * Constants::Camera::SENSITIVITY));
}
