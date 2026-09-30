#include <cmath>
#include <cstdint>
#include <limits>

#include "doctest/doctest.h"
#include "heightmap.h"

// The noise value -> block height conversion in src/heightmap.h.
//
// The bug this guards: the sampler used to end in a bare
//   static_cast<uint16_t>(std::floor(noiseY * MAX_BLOCK_HEIGHT))
// with no clamp. fbm has not been observed to leave [-1, 1] -- measured over
// 1.6M samples across 8 seeds it stayed within [-0.87, 0.89] -- so this was
// latent rather than an active wrong-terrain bug. But the bound was only ever
// checked at 250 sampled points, and a negative float cast to uint16_t is
// undefined behaviour that wraps to 65535. That would surface far from here,
// as a broken neighbour-height lookup in Chunk::isBlockExposed.

TEST_SUITE("Heightmap") {
  TEST_CASE("noise in [-1, 1] maps into the block height range") {
    const auto maxHeight = static_cast<float>(Constants::Chunk::MAX_BLOCK_HEIGHT);

    for (int i = 0; i <= 100; ++i) {
      const float noise =
          -1.0f + 2.0f * (static_cast<float>(i) / 100.0f);
      const auto height = Heightmap::heightFromNoise(noise);
      CHECK(height <= static_cast<std::uint16_t>(Constants::Chunk::MAX_BLOCK_HEIGHT));
      CHECK(static_cast<float>(height) <= maxHeight);
    }
  }

  TEST_CASE("the extremes of the valid range land on the expected heights") {
    // -1 -> 0, +1 -> MAX_BLOCK_HEIGHT, after floor().
    CHECK(Heightmap::heightFromNoise(-1.0f) == 0u);
    CHECK(Heightmap::heightFromNoise(1.0f) ==
          static_cast<std::uint16_t>(Constants::Chunk::MAX_BLOCK_HEIGHT));
    // 0.0 is the midpoint of [-1, 1] -> 0.5 -> floor(0.5 * MAX_BLOCK_HEIGHT).
    const auto expectedMid = static_cast<std::uint16_t>(
        std::floor(0.5f * static_cast<float>(Constants::Chunk::MAX_BLOCK_HEIGHT)));
    CHECK(Heightmap::heightFromNoise(0.0f) == expectedMid);
  }

  TEST_CASE("out-of-range noise clamps instead of invoking undefined behaviour") {
    // Each of these is a float-to-unsigned conversion of a value outside the
    // destination's range, which is UB without a clamp. A negative input is the
    // dangerous one: it would wrap to 65535.
    const float below = -5.0f;
    const float above = 5.0f;
    const float far_below = -1.0e9f;
    const float far_above = 1.0e9f;

    CHECK(Heightmap::heightFromNoise(below) == 0u);
    CHECK(Heightmap::heightFromNoise(far_below) == 0u);
    CHECK(Heightmap::heightFromNoise(above) ==
          static_cast<std::uint16_t>(Constants::Chunk::MAX_BLOCK_HEIGHT));
    CHECK(Heightmap::heightFromNoise(far_above) ==
          static_cast<std::uint16_t>(Constants::Chunk::MAX_BLOCK_HEIGHT));
  }

  TEST_CASE("the conversion is monotonic in the noise value") {
    // A clamp must not reorder inputs. If it did, terrain would develop
    // discontinuities that look like noise but are not.
    std::uint16_t previous = Heightmap::heightFromNoise(-2.0f);
    for (int i = 1; i <= 400; ++i) {
      const float noise = -2.0f + 4.0f * (static_cast<float>(i) / 400.0f);
      const auto height = Heightmap::heightFromNoise(noise);
      CHECK(height >= previous);
      previous = height;
    }
  }

  TEST_CASE("every result is a real block height, never a wrapped one") {
    // A wrapped negative would appear as a height far above MAX_BLOCK_HEIGHT.
    // Sweep a wide band and assert the postcondition rather than exact values.
    const auto maxHeight =
        static_cast<std::uint16_t>(Constants::Chunk::MAX_BLOCK_HEIGHT);
    for (int i = 0; i < 2000; ++i) {
      const float noise = -3.0f + 6.0f * (static_cast<float>(i) / 2000.0f);
      const auto height = Heightmap::heightFromNoise(noise);
      CHECK(height <= maxHeight);
    }
  }

  TEST_CASE("sampleWorld agrees with heightFromNoise for the same fbm value") {
    // sampleWorld is what Chunk::generateHeightMapCPU calls; this pins that it
    // is a thin wrapper and not a second, divergent implementation.
    for (int i = 0; i < 25; ++i) {
      const float x = 3.0f + static_cast<float>(i);
      const float z = -7.0f + static_cast<float>(i) * 0.5f;
      const auto n = Noise::fbm(
          glm::vec2(x, z) * Constants::Noise::FREQUENCY,
          Constants::Noise::FRACTAL_OCTAVE, Constants::Noise::FRACTAL_LACUNARITY,
          Constants::Noise::FRACTAL_GAIN);
      CHECK(Heightmap::sampleWorld(x, z) == Heightmap::heightFromNoise(n.value));
    }
  }

  TEST_CASE("sampleWorld output stays within the world height") {
    const auto maxHeight =
        static_cast<std::uint16_t>(Constants::Chunk::MAX_BLOCK_HEIGHT);
    for (int i = 0; i < 500; ++i) {
      const float x = static_cast<float>(i) * 0.31f - 80.0f;
      const float z = static_cast<float>(i) * -0.17f + 40.0f;
      CHECK(Heightmap::sampleWorld(x, z) <= maxHeight);
    }
  }
}
