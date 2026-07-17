#include "doctest/doctest.h"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "camera.h"
#include "config.h"

TEST_SUITE("Camera") {
  TEST_CASE("default constructor places the camera") {
    Camera cam(Constants::Camera::DEFAULT_POSITION);
    CHECK(cam.m_Position.x == doctest::Approx(Constants::Camera::DEFAULT_POSITION.x));
    CHECK(cam.m_Position.y == doctest::Approx(Constants::Camera::DEFAULT_POSITION.y));
    CHECK(cam.m_Position.z == doctest::Approx(Constants::Camera::DEFAULT_POSITION.z));
  }

  // Regression test: getRight() used to return cross(m_Up, m_WorldUp), which is
  // the zero vector for the default orientation -> normalize() yields NaN and
  // silently breaks frustum culling. The right vector must be finite and unit
  // length, and equal to +X for the default look-down-(-Z) view.
  TEST_CASE("getRight returns a finite +X unit vector by default") {
    Camera cam(glm::vec3(0.0f));
    glm::vec3 r = cam.getRight();
    CHECK(std::isfinite(r.x));
    CHECK(std::isfinite(r.y));
    CHECK(std::isfinite(r.z));
    CHECK(glm::length(r) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(r.x == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(std::abs(r.y) < 1e-4f);
    CHECK(std::abs(r.z) < 1e-4f);
  }

  TEST_CASE("getView produces a finite matrix") {
    Camera cam(glm::vec3(5.0f, 155.0f, 5.0f));
    glm::mat4 v = cam.getView();
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) CHECK(std::isfinite(v[i][j]));
  }

  TEST_CASE("getProjection produces a finite matrix") {
    Camera cam(glm::vec3(0.0f));
    glm::mat4 p = cam.getProjection();
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) CHECK(std::isfinite(p[i][j]));
  }
}
