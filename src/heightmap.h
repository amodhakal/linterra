#pragma once

// Terrain height sampling, shared between the CPU heightmap generator and the
// unit tests.
//
// Deliberately free of OpenGL and of the Chunk class, so the conversion from a
// noise sample to a block height can be exercised without a GL context.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "config.h"
#include "noise/noise.h"

namespace Heightmap {

// Convert one fbm sample into a block height in
// [0, Constants::Chunk::MAX_BLOCK_HEIGHT].
//
// `value` is expected in [-1, 1]: the sampler rescales it to [0, 1] before
// scaling by the maximum height. That bound is asserted by the noise tests, but
// only at sampled points, and the caller's own arithmetic can leave the
// interval by a hair. The conversion is therefore clamped rather than trusted,
// because a floating-point value outside the range of the destination integer
// type is undefined behaviour on the cast -- a negative height would wrap to
// 65535 and show up much later as a broken neighbour lookup in
// Chunk::isBlockExposed, nowhere near this line.
//
// Total by construction: any float, including NaN-free out-of-range input and
// infinities, produces a height inside the interval above.
inline std::uint16_t heightFromNoise(float value) {
  // Rescale [-1, 1] -> [0, 1], clamping both ends in one step.
  const float normalized = std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f);
  const float scaled =
      std::floor(normalized *
                 static_cast<float>(Constants::Chunk::MAX_BLOCK_HEIGHT));
  // floor() of a value in [0, 1] scaled by MAX_BLOCK_HEIGHT is already within
  // [0, MAX_BLOCK_HEIGHT]; this second clamp states the postcondition and keeps
  // it true if MAX_BLOCK_HEIGHT is ever changed to something non-integral.
  return static_cast<std::uint16_t>(
      std::clamp(scaled, 0.0f,
                 static_cast<float>(Constants::Chunk::MAX_BLOCK_HEIGHT)));
}

// Sample terrain height at a world block position using the engine's fbm
// parameters. This is the exact call the chunk heightmap generator makes.
inline std::uint16_t sampleWorld(float worldBlockX, float worldBlockZ) {
  const auto n = Noise::fbm(
      glm::vec2(worldBlockX, worldBlockZ) * Constants::Noise::FREQUENCY,
      Constants::Noise::FRACTAL_OCTAVE, Constants::Noise::FRACTAL_LACUNARITY,
      Constants::Noise::FRACTAL_GAIN);
  return heightFromNoise(n.value);
}

}  // namespace Heightmap
