#include "doctest/doctest.h"

#include <cmath>
#include <glm/glm.hpp>

#include "noise/noise.h"

TEST_SUITE("Noise") {
  TEST_CASE("fbm is deterministic for a fixed seed") {
    Noise::setSeed(1337);
    Noise::Noise2D a = Noise::fbm(glm::vec2(1.5f, 2.5f));
    Noise::Noise2D b = Noise::fbm(glm::vec2(1.5f, 2.5f));
    CHECK(a.value == doctest::Approx(b.value));
    CHECK(a.deriv.x == doctest::Approx(b.deriv.x));
    CHECK(a.deriv.y == doctest::Approx(b.deriv.y));
  }

  TEST_CASE("fbm value stays within [-1, 1]") {
    Noise::setSeed(42);
    for (int i = 0; i < 250; ++i) {
      float x = (i * 0.137f) - 10.0f;
      float y = (i * 0.311f) - 5.0f;
      float v = Noise::fbm(glm::vec2(x, y)).value;
      CHECK(v >= -1.0f);
      CHECK(v <= 1.0f);
    }
  }

  TEST_CASE("noised is continuous (small input delta -> small output delta)") {
    Noise::setSeed(7);
    glm::vec2 p(3.3f, 4.4f);
    Noise::Noise2D n0 = Noise::noised(p);
    Noise::Noise2D n1 = Noise::noised(p + glm::vec2(1e-3f, 1e-3f));
    CHECK(std::abs(n1.value - n0.value) < 1e-1f);
  }

  TEST_CASE("setSeed / getSeed round-trips") {
    Noise::setSeed(999);
    CHECK(Noise::getSeed() == 999);
    Noise::setSeed(0);
    CHECK(Noise::getSeed() == 0);
  }

  TEST_CASE("different seeds produce different output") {
    Noise::setSeed(1);
    float v1 = Noise::fbm(glm::vec2(2.0f, 3.0f)).value;
    Noise::setSeed(2);
    float v2 = Noise::fbm(glm::vec2(2.0f, 3.0f)).value;
    CHECK(v1 != v2);
  }
}
