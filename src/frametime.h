#pragma once

#include <algorithm>

// Frame-to-frame time stepping.
//
// This lives apart from Application::update so the step can be exercised
// without a GLFW context: application.cpp is excluded from every test target
// (linterra_tests, because it needs the event loop and ImGui, and
// linterra_smoke, because it is the game's own main()), so a clamp that only
// existed inline in Application::getDeltaTime would be untestable. The header
// is deliberately GL-free and header-only, so linterra_tests includes it
// directly with no new library or CMake source entry.

namespace FrameTime {

// One frame at 30 fps. Generous for a machine that is genuinely running at
// 30 fps, and a hard ceiling on what a single hitch can inject into one step.
inline constexpr double kMaxDeltaTime = 1.0 / 30.0;

/** Bound a raw wall-clock gap into a usable physics step.
 *
 *  glfwGetTime keeps advancing while the process makes no progress -- a window
 *  drag on macOS blocks the main loop and the swap, a GPU stall or a debugger
 *  pause do the same -- so the raw difference between two samples is
 *  unbounded and every consumer of deltaTime is a multiplier. At
 *  Constants::Camera::SPEED = 20.5 a 1.4 s gap is 28.7 blocks of displacement
 *  in one step: far enough to leave the loaded world entirely, since only
 *  chunks within RENDER_DISTANCE_CHUNKS are streamed (#145).
 *
 *  A non-positive gap collapses to zero rather than being passed through. A
 *  negative delta is not a thing the simulation can do anything sensible
 *  with -- gravity integration would run backwards and lift the player -- and
 *  the comparison is written as !(x > 0.0) so a NaN from a bad clock source
 *  takes the same path rather than propagating through the comparison.
 *
 *  The cost of the clamp is that the world runs slow rather than losing time
 *  under a sustained stall. That is the right trade here: a single-player
 *  voxel world that pauses for a frame is invisible, one that jumps hundreds
 *  of blocks is not. */
inline double clampDeltaSeconds(double rawDeltaSeconds) {
  if (!(rawDeltaSeconds > 0.0)) {
    return 0.0;
  }
  return std::min(rawDeltaSeconds, kMaxDeltaTime);
}

}  // namespace FrameTime
