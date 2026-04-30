#include <cstdint>
#include <glm/glm.hpp>
#include <noise/noise.h>

namespace {
uint32_t g_Seed = 0;

inline float hash21(int32_t x, int32_t y) {
  uint32_t n = static_cast<uint32_t>(x) + static_cast<uint32_t>(y) * 57u;
  n = (n << 13) ^ n;
  n ^= g_Seed;
  uint32_t nn = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
  return 1.0f - static_cast<float>(nn) / 1073741824.0f; // -> [-1,1]
}

inline float quintic(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float quintic_d(float t) {
  return 30.0f * t * t * (t * (t - 1.0f) + 2.0f);
}

inline float hash01(int32_t x, int32_t y) {
  return 0.5f * (hash21(x, y) + 1.0f);
}
} // namespace

namespace Noise {

Noise2D noised(const glm::vec2 &p) {
  glm::vec2 P = glm::floor(p);
  glm::vec2 w = glm::fract(p);

  float ux = quintic(w.x);
  float uy = quintic(w.y);
  float dux = quintic_d(w.x);
  float duy = quintic_d(w.y);

  int32_t ix = static_cast<int32_t>(P.x);
  int32_t iy = static_cast<int32_t>(P.y);

  float a = hash01(ix + 0, iy + 0);
  float b = hash01(ix + 1, iy + 0);
  float c = hash01(ix + 0, iy + 1);
  float d = hash01(ix + 1, iy + 1);

  float k0 = a;
  float k1 = b - a;
  float k2 = c - a;
  float k3 = a - b - c + d;

  float n = k0 + k1 * ux + k2 * uy + k3 * ux * uy;

  float dn_dx = (k1 + k3 * uy) * dux;
  float dn_dy = (k2 + k3 * ux) * duy;

  Noise2D out = {.value = -1.0f + 2.0f * n,
                 .deriv = glm::vec2(2.0f * dn_dx, 2.0f * dn_dy)};
  return out;
}

Noise2D fbm(const glm::vec2 &p, int octaves, float lacunarity, float gain) {
  glm::vec2 pos = p;
  float amplitude = 1.0f;
  float value = 0.0f;
  glm::vec2 deriv(0.0f);

  float maxAmp = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    Noise2D n = noised(pos);
    value += amplitude * n.value;
    deriv += amplitude * n.deriv;
    maxAmp += amplitude;
    amplitude *= gain;
    pos *= lacunarity;
  }

  if (maxAmp > 0.0f) {
    value /= maxAmp;
    deriv /= maxAmp;
  }

  Noise2D out = {value, deriv};
  return out;
}

void setSeed(uint32_t seed) {
  g_Seed = seed;
}

uint32_t getSeed() {
  return g_Seed;
}

} // namespace Noise
