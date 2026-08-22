#include "doctest/doctest.h"

#include "camera.h"
#include "config.h"
#include "Frustum.h"

#include <glm/glm.hpp>

TEST_SUITE("Frustum") {
  // Camera at the origin region, looking down -Z (the engine default).
  // Chunk (0,-1) occupies world X [0,16], Z [-16,0] — directly under/in front
  // of the camera once boxes are aligned with rendered geometry.
  TEST_CASE("chunk under/near the camera is inside the frustum") {
    Camera cam(glm::vec3(0.0f, 100.0f, 0.0f));
    Frustum f(&cam);
    CHECK(f.isChunkInside(glm::vec2(0.0f, -1.0f)) == true);
  }

  TEST_CASE("chunk entirely behind the camera is culled") {
    Camera cam(glm::vec3(0.0f, 100.0f, 0.0f));
    Frustum f(&cam);
    // Chunk (0,0) occupies world Z [0,16] — fully behind a -Z-facing camera.
    CHECK(f.isChunkInside(glm::vec2(0.0f, 0.0f)) == false);
  }

  TEST_CASE("chunk far beyond the far plane is culled") {
    Camera cam(glm::vec3(0.0f, 100.0f, 0.0f));
    Frustum f(&cam);
    // Block Z = -2000*16 is far in front of the camera but past FAR (1000).
    CHECK(f.isChunkInside(glm::vec2(0.0f, -2000.0f)) == false);
  }

  // Exercises the side planes (which depend on getRight()). With a correct
  // right vector, a chunk far to the side yet in front is culled; with the old
  // NaN right vector it would never be culled.
  TEST_CASE("chunk far to the side is culled") {
    Camera cam(glm::vec3(0.0f, 100.0f, 0.0f));
    Frustum f(&cam);
    // Block X = 62.5*16 ~= 1000 (far right), Block Z = -6.25*16 ~= -100 (in front).
    CHECK(f.isChunkInside(glm::vec2(62.5f, -6.25f)) == false);
  }

  TEST_CASE("isChunkInside is deterministic") {
    Camera cam(glm::vec3(10.0f, 50.0f, -20.0f));
    Frustum f(&cam);
    bool a = f.isChunkInside(glm::vec2(3.0f, 4.0f));
    bool b = f.isChunkInside(glm::vec2(3.0f, 4.0f));
    CHECK(a == b);
  }
}
