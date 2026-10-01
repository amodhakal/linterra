#include <cmath>
#include <limits>

#include "doctest/doctest.h"

#include "config.h"
#include "frametime.h"

// Application::getDeltaTime is where the clamp is consumed, but
// application.cpp is excluded from both test targets -- linterra_tests because
// it needs the GLFW event loop and ImGui, linterra_smoke because it is the
// game's own main(). The step therefore lives in src/frametime.h, which is
// GL-free and header-only, and is exercised directly here.

TEST_SUITE("FrameTime") {
  TEST_CASE("a normal frame's delta passes through unchanged") {
    // 60 Hz and 144 Hz are both well inside the ceiling, so the clamp must not
    // be observable on a healthy frame.
    CHECK(FrameTime::clampDeltaSeconds(1.0 / 60.0) ==
          doctest::Approx(1.0 / 60.0));
    CHECK(FrameTime::clampDeltaSeconds(1.0 / 144.0) ==
          doctest::Approx(1.0 / 144.0));
  }

  TEST_CASE("a window-drag stall is clamped to one frame at 30 fps") {
    // The case from #145: dragging the window on macOS blocks the main loop
    // and the swap for ~0.5-2 s, and glfwGetTime reports the whole interval in
    // one frame. At SPEED = 20.5 the unclamped 1.4 s step is 28.7 blocks of
    // displacement -- further than RENDER_DISTANCE_CHUNKS = 32 chunks, so the
    // player leaves the streamed world entirely and falls through the void.
    constexpr double kDragStall = 1.4;

    const double clamped = FrameTime::clampDeltaSeconds(kDragStall);

    CHECK(clamped == doctest::Approx(FrameTime::kMaxDeltaTime));
    CHECK(FrameTime::kMaxDeltaTime == doctest::Approx(1.0 / 30.0));

    // The displacement that actually survives the clamp, in blocks.
    const double blocks = clamped * Constants::Camera::SPEED;
    CHECK(blocks == doctest::Approx((1.0 / 30.0) * 20.5));

    // ...and the factor the clamp bought, which is the number the issue quotes.
    INFO("stall of " << kDragStall << "s: displacement "
                     << (kDragStall * Constants::Camera::SPEED) << " -> "
                     << blocks << " blocks");
    CHECK(kDragStall * Constants::Camera::SPEED ==
          doctest::Approx(28.7).epsilon(0.01));
    CHECK(blocks < 1.0);
  }

  TEST_CASE("the clamp is monotonic up to the ceiling and flat above it") {
    double previous = 0.0;
    for (double raw = 0.0; raw <= 2.0; raw += 1.0 / 240.0) {
      const double clamped = FrameTime::clampDeltaSeconds(raw);
      REQUIRE(clamped >= previous);
      REQUIRE(clamped <= FrameTime::kMaxDeltaTime);
      previous = clamped;
    }
    CHECK(FrameTime::clampDeltaSeconds(1.0e9) ==
          doctest::Approx(FrameTime::kMaxDeltaTime));
  }

  TEST_CASE("a non-positive delta cannot reverse the simulation") {
    // A backwards clock source -- or a NaN from a broken one -- would otherwise
    // be multiplied straight into gravity integration, so a negative step
    // lifts the player instead of dropping them. Both take the same path.
    CHECK(FrameTime::clampDeltaSeconds(-1.4) == doctest::Approx(0.0));
    const double tinyNegative = -std::numeric_limits<double>::epsilon();
    CHECK(FrameTime::clampDeltaSeconds(tinyNegative) == doctest::Approx(0.0));
    CHECK(FrameTime::clampDeltaSeconds(0.0) == doctest::Approx(0.0));

    // A zero step must leave an integrating body exactly where it was: this is
    // the property that matters, and it is what a negative step would break.
    float position = 100.0f;
    const float velocity = -20.0f;
    const auto step = static_cast<float>(FrameTime::clampDeltaSeconds(-1.4));
    position += velocity * step;
    CHECK(position == doctest::Approx(100.0f));
  }

  TEST_CASE("a NaN delta does not propagate") {
    // Written as !(x > 0.0) rather than x <= 0.0 precisely so a NaN cannot
    // slip past: every comparison against NaN is false, so std::min alone
    // would return one of its arguments unchanged.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double clamped = FrameTime::clampDeltaSeconds(nan);

    CHECK(std::isfinite(clamped));
    CHECK(clamped == doctest::Approx(0.0));
  }

  TEST_CASE("the ceiling leaves room for a genuinely slow machine") {
    // A clamp below ~1/30 s would start eating real time on a machine actually
    // running at 30 fps, so state the intent rather than leaving it implicit.
    CHECK(FrameTime::kMaxDeltaTime >= 1.0 / 30.0);
    CHECK(FrameTime::kMaxDeltaTime > 1.0 / 60.0);
  }
}
