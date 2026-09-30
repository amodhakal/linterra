#include "doctest/doctest.h"

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "camera.h"
#include "frustum.h"
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

  // ---------------------------------------------------------------------
  // Aspect ratio (#12)
  //
  // m_Aspect was assigned once in the constructor from the compile-time
  // SCR_WIDTH/SCR_HEIGHT and never updated. Both getProjection and the
  // frustum's side planes are built from it, so every resize left the
  // projection describing a different shape than the window, and left culling
  // describing a different shape than the projection.
  // ---------------------------------------------------------------------

  TEST_CASE("the aspect ratio starts at the default framebuffer shape") {
    Camera cam(glm::vec3(0.0f));
    CHECK(cam.m_Aspect ==
          doctest::Approx(static_cast<float>(Constants::SCR_WIDTH) /
                          static_cast<float>(Constants::SCR_HEIGHT)));
  }

  TEST_CASE("setAspect tracks the framebuffer shape") {
    Camera cam(glm::vec3(0.0f));
    const std::uint32_t shapes[][2] = {
        {1920, 1080}, {1080, 1920}, {1024, 768}, {800, 600}, {2560, 1440}};

    for (const auto& shape : shapes) {
      cam.setAspect(shape[0], shape[1]);
      CAPTURE(shape[0]);
      CAPTURE(shape[1]);
      CHECK(cam.m_Aspect == doctest::Approx(
                               static_cast<float>(shape[0]) /
                               static_cast<float>(shape[1])));
    }
  }

  TEST_CASE("a zero-height framebuffer leaves the aspect ratio alone") {
    // Reachable while a window is being minimised. Dividing by it would put an
    // infinite or NaN aspect into both the projection and the side planes.
    Camera cam(glm::vec3(0.0f));
    cam.setAspect(1920, 1080);
    const float before = cam.m_Aspect;

    cam.setAspect(1920, 0);
    CHECK(cam.m_Aspect == doctest::Approx(before));
    CHECK(std::isfinite(cam.m_Aspect));

    // And the projection stays usable rather than becoming NaN.
    const glm::mat4 projection = cam.getProjection();
    CHECK(std::isfinite(projection[0][0]));
  }

  TEST_CASE("the projection changes shape when the aspect ratio does") {
    // A wide window must widen the horizontal field of view, which shows up as
    // a smaller [0][0] in the perspective matrix (the x scale is 1/(aspect*tan)).
    Camera wide(glm::vec3(0.0f));
    wide.setAspect(1920, 1080);
    Camera square(glm::vec3(0.0f));
    square.setAspect(1000, 1000);

    CHECK(wide.getProjection()[0][0] < square.getProjection()[0][0]);
  }

  TEST_CASE("the frustum agrees with the projection after a resize") {
    // The point of the fix. A chunk that is inside the frustum built at the
    // old aspect must still be inside the frustum built at the new one, or
    // culling and rendering have drifted apart. Both are derived from
    // m_Aspect, so this holds only if the resize path updates it.
    const glm::vec3 eye(0.0f, 100.0f, 0.0f);

    Camera before(eye);
    before.setAspect(800, 600);
    Camera after(eye);
    after.setAspect(1920, 1080);

    CHECK(before.m_Aspect != doctest::Approx(after.m_Aspect));

    Frustum beforeFrustum(&before);
    Frustum afterFrustum(&after);

    // Widen the window: everything culled before must still be culled after
    // (a wider frustum is a superset), and nothing already culled may be kept
    // only because the aspect was never updated.
    int newlyKept = 0;
    int dropped = 0;
    for (int x = -40; x <= 40; ++x) {
      for (int z = -40; z <= 40; ++z) {
        const bool wasKept = beforeFrustum.isChunkInside(glm::ivec2(x, z));
        const bool isKept = afterFrustum.isChunkInside(glm::ivec2(x, z));
        if (isKept && !wasKept) ++newlyKept;
        if (wasKept && !isKept) ++dropped;
      }
    }

    CAPTURE(newlyKept);
    CAPTURE(dropped);
    // 1920x1080 is wider than 4:3, so the new frustum keeps strictly more.
    CHECK(newlyKept > 0);
    CHECK(dropped == 0);
  }
}
