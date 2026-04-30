#pragma once
#include <glm/glm.hpp>

namespace Noise {
struct Noise2D {
  float value;
  glm::vec2 deriv;
};

Noise2D noised(const glm::vec2 &p);

Noise2D fbm(const glm::vec2 &p, int octaves = 6, float lacunarity = 2.0f,
            float gain = 0.5f);

void setSeed(uint32_t seed);
uint32_t getSeed();
}
