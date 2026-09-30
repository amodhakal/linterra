#include "doctest/doctest.h"

#include "camera.h"
#include "config.h"
#include "frustum.h"

#include <cmath>

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

  // ---------------------------------------------------------------------
  // Conservative-cull invariant (#127)
  //
  // The four cases above all pass whether or not the side planes are correct,
  // because each is decided by the near, far, or left plane. There was no
  // assertion anywhere in the suite that the frustum keeps everything the
  // projection says is on screen, so a frustum that is far too wide, or whose
  // side-plane normals have inverted, passed exactly as cleanly as a correct
  // one.
  //
  // The invariant below is one-directional: anything visible by the real
  // projection must not be culled. It fails on both failure modes -- an
  // over-wide frustum still passes it (it only costs performance), but an
  // inverted-normal frustum fails loudly, which is the one that made the world
  // disappear.
  // ---------------------------------------------------------------------

  namespace {

  // Reference visibility: project a chunk AABB's 8 corners through the real
  // getProjection() * getView() and ask whether any lands inside NDC.
  bool visibleByProjection(Camera& camera, int chunkX, int chunkZ) {
    const int L = Constants::Chunk::LENGTH;
    const float H = static_cast<float>(Constants::Chunk::HEIGHT);
    const glm::mat4 viewProj = camera.getProjection() * camera.getView();

    const float x0 = static_cast<float>(chunkX * L);
    const float z0 = static_cast<float>(chunkZ * L);
    for (int xi = 0; xi < 2; ++xi) {
      for (int yi = 0; yi < 2; ++yi) {
        for (int zi = 0; zi < 2; ++zi) {
          const glm::vec3 world{xi ? x0 + L : x0, yi ? H : 0.0f,
                                zi ? z0 + L : z0};
          const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
          if (clip.w <= 0.0f) continue;  // behind the camera
          const glm::vec3 ndc = glm::vec3(clip) / clip.w;
          // The z test matters as much as x/y: without it a chunk entirely
          // beyond the far plane still projects inside the NDC square and would
          // be reported visible, making the frustum's far-plane cull look like a
          // false cull.
          if (ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f &&
              ndc.y <= 1.0f && ndc.z >= -1.0f && ndc.z <= 1.0f) {
            return true;
          }
        }
      }
    }
    return false;
  }

  Camera cameraWithFov(float fov) {
    Camera camera(glm::vec3(0.0f, 100.0f, 0.0f));
    camera.m_Fov = fov;
    camera.m_Far = 1000.0f;
    camera.m_Near = 0.1f;
    camera.m_Aspect = static_cast<float>(Constants::SCR_WIDTH) /
                      static_cast<float>(Constants::SCR_HEIGHT);
    return camera;
  }

  }  // namespace

  TEST_CASE("the frustum never culls a chunk the projection says is visible") {
    // Sweeps FOV_MIN..FOV_MAX, because tan() goes negative across part of that
    // band and that is what inverted the side-plane normals. A change to
    // FOV_MIN/FOV_MAX that re-enters the bad band would fail here.
    int meaningful = 0;
    for (const float fov : {Constants::Camera::FOV_MIN, 5.0f, 10.0f, 15.0f,
                            20.0f, 25.0f, 30.0f, 35.0f, 40.0f, 45.0f,
                            Constants::Camera::FOV_MAX}) {
      Camera camera = cameraWithFov(fov);
      Frustum frustum(&camera);

      int visible = 0;
      int falseCulls = 0;
      // The grid has to be wide. A chunk AABB spans y in [0, HEIGHT] while the
      // camera sits at y=100, so its corners are 100-156 blocks off-axis; at a
      // narrow FOV they only fall inside the cone at a distance, so a small grid
      // reports "nothing visible" and the invariant goes vacuous.
      for (int x = -64; x <= 64; ++x) {
        for (int z = -64; z <= 64; ++z) {
          if (!visibleByProjection(camera, x, z)) continue;
          ++visible;
          if (!frustum.isChunkInside(glm::ivec2(x, z))) {
            ++falseCulls;
          }
        }
      }

      CAPTURE(fov);
      CHECK_MESSAGE(falseCulls == 0,
                    "frustum culled " << falseCulls << " of " << visible
                                      << " visible chunks at fov " << fov);

      // A very narrow cone puts no chunk fully inside the swept grid, so the
      // invariant is vacuous there rather than violated. Count the FOVs that
      // were actually exercised so the sweep cannot silently pass by testing
      // nothing.
      if (visible > 0) {
        ++meaningful;
      }
    }

    REQUIRE_MESSAGE(meaningful >= 6,
                    "only " << meaningful
                            << " FOVs in the sweep had visible geometry; the "
                               "invariant is not being exercised");
  }

  TEST_CASE("the side-plane half-angles match the projection cone") {
    // Pins the arithmetic itself, so the units bug cannot come back as a
    // silently different number: at fov 45 the vertical half-angle is 22.5
    // degrees, so the side extent at the far plane is far * tan(22.5 deg).
    const float fov = 45.0f;
    Camera camera = cameraWithFov(fov);
    const float halfAngle = glm::radians(fov) * 0.5f;
    const float expectedHalfVSide = camera.m_Far * std::tan(halfAngle);

    // Degenerate input would make the plane normals meaningless.
    REQUIRE(expectedHalfVSide > 0.0f);

    // The frustum keeps a chunk that sits just inside the vertical edge, and
    // culls one just outside. Probing the boundary is what distinguishes a
    // correct side plane from an inflated one.
    Frustum frustum(&camera);
    const float reach =
        camera.m_Far * static_cast<float>(Constants::Chunk::LENGTH);
    const int insideChunk = static_cast<int>(expectedHalfVSide / reach) - 1;
    const int outsideChunk = insideChunk + 3;

    CHECK(frustum.isChunkInside(glm::ivec2(0, insideChunk)));
    CHECK_FALSE(frustum.isChunkInside(glm::ivec2(0, outsideChunk)));
  }

  TEST_CASE("the frustum is conservative at every FOV in the sweep") {
    // The direction that carries the safety guarantee: kept >= visible. The
    // opposite bound (kept is not wastefully large) is deliberately not
    // asserted here -- it is grid-dependent, since a smaller grid raises the
    // ratio by adding near-camera boundary chunks. Measuring "too wide" needs
    // a defined grid and a tolerance, which is #66's territory.
    for (const float fov : {20.0f, 25.0f, 30.0f, 35.0f, 40.0f, 45.0f}) {
      Camera camera = cameraWithFov(fov);
      Frustum frustum(&camera);

      int visible = 0;
      int kept = 0;
      for (int x = -64; x <= 64; ++x) {
        for (int z = -64; z <= 64; ++z) {
          visible += visibleByProjection(camera, x, z) ? 1 : 0;
          kept += frustum.isChunkInside(glm::ivec2(x, z)) ? 1 : 0;
        }
      }

      CAPTURE(fov);
      REQUIRE(visible > 0);
      CHECK_MESSAGE(kept >= visible,
                    "frustum kept " << kept << " but " << visible
                                     << " were visible at fov " << fov);
    }
  }
}
