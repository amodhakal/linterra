#include "doctest/doctest.h"

#include "camera.h"
#include "config.h"
#include "frustum.h"

#include <cmath>
#include <initializer_list>

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

  // ---------------------------------------------------------------------
  // Plane-normal construction and AABB conventions (#66)
  //
  // #127 fixed the degrees/radians defect and added a conservative-cull
  // invariant. What was still unpinned was *how* the six planes are built and
  // *where* a chunk's box is anchored, which is what the tests below fix.
  // ---------------------------------------------------------------------

  TEST_CASE("side-plane normals match the projection cone exactly") {
    // Derivation, for a camera looking down -Z with right (1,0,0) and up
    // (0,1,0), far = F:
    //   right.normal  = cross(F*front - right*halfHSide, up) = (F, 0, -halfHSide)
    //   left.normal   = cross(up, F*front + right*halfHSide) = (-F, 0, -halfHSide)
    //   top.normal    = cross(right, F*front - up*halfVSide) = (0, F, -halfVSide)
    //   bottom.normal = cross(F*front + up*halfVSide, right) = (0, -F, -halfVSide)
    // with halfVSide = F*tan(fov/2) and halfHSide = halfVSide*aspect.
    //
    // The half-angle is the radians conversion, so this is an exact restatement
    // of the projection cone rather than a measurement. It fails on the
    // degrees defect (tan(22.5 rad) != tan(22.5 deg)) and on any axis or sign
    // error, and needs no grid to be well defined.
    for (const float fov : {10.0f, 25.0f, 45.0f, 90.0f}) {
      Camera camera = cameraWithFov(fov);
      Frustum frustum(&camera);
      const float far = camera.m_Far;
      const float halfV = far * std::tan(glm::radians(fov) * 0.5f);
      const float halfH = halfV * camera.m_Aspect;

      CAPTURE(fov);

      // Both sides are normalised: the construction leaves the far-plane scale
      // in the normal, and the expected directions above are not themselves
      // unit length (|(0,-1,-1)| is sqrt(2) at fov 90), so comparing a
      // normalised actual against a raw expected measures nothing useful.
      const auto dirOf = [](const glm::vec3& n) { return glm::normalize(n); };
      const float eps = 1e-4f;

      CHECK(glm::length(dirOf(frustum.m_RightFace.normal) -
                        dirOf(glm::vec3(1.0f, 0.0f, -halfH / far))) < eps);
      CHECK(glm::length(dirOf(frustum.m_LeftFace.normal) -
                        dirOf(glm::vec3(-1.0f, 0.0f, -halfH / far))) < eps);
      CHECK(glm::length(dirOf(frustum.m_TopFace.normal) -
                        dirOf(glm::vec3(0.0f, 1.0f, -halfV / far))) < eps);
      CHECK(glm::length(dirOf(frustum.m_BottomFace.normal) -
                        dirOf(glm::vec3(0.0f, -1.0f, -halfV / far))) < eps);

      // Near and Far are taken straight from the view direction.
      CHECK(glm::length(frustum.m_NearFace.normal - camera.m_Front) < eps);
      CHECK(glm::length(frustum.m_FarFace.normal + camera.m_Front) < eps);
    }
  }

  TEST_CASE("every plane normal points inward") {
    // isChunkInside treats a negative signed distance as "outside", so a normal
    // pointing the wrong way inverts the whole test for that plane -- it would
    // cull everything instead of nothing. A point a little in front of the
    // camera is unambiguously inside all six.
    for (const float fov : {25.0f, 45.0f, 75.0f}) {
      Camera camera = cameraWithFov(fov);
      Frustum frustum(&camera);
      const glm::vec3 inside =
          camera.m_Position + camera.m_Front * (camera.m_Near * 4.0f);

      CAPTURE(fov);
      const Plane* planes[] = {&frustum.m_NearFace,    &frustum.m_FarFace,
                               &frustum.m_RightFace,   &frustum.m_LeftFace,
                               &frustum.m_TopFace,     &frustum.m_BottomFace};
      for (const Plane* plane : planes) {
        CHECK_MESSAGE(glm::dot(inside - plane->point, plane->normal) >= 0.0f,
                      "a point inside the frustum is on the negative side of "
                      "a plane");
      }
    }
  }

  TEST_CASE("plane normals stay finite and non-degenerate across the FOV range") {
    // A zero-length normal makes every distance 0, which the "< 0" test reads as
    // "not outside" -- so the chunk is kept by that plane whatever its position.
    // Degenerate geometry must not be silently tolerated.
    for (float fov = Constants::Camera::FOV_MIN; fov <= Constants::Camera::FOV_MAX;
         fov += 0.5f) {
      Camera camera = cameraWithFov(fov);
      Frustum frustum(&camera);

      const Plane* planes[] = {&frustum.m_NearFace,    &frustum.m_FarFace,
                               &frustum.m_RightFace,   &frustum.m_LeftFace,
                               &frustum.m_TopFace,     &frustum.m_BottomFace};
      for (const Plane* plane : planes) {
        CAPTURE(fov);
        CHECK(std::isfinite(plane->normal.x));
        CHECK(std::isfinite(plane->normal.y));
        CHECK(std::isfinite(plane->normal.z));
        CHECK(glm::length(plane->normal) > 0.0f);
      }
    }
  }

  TEST_CASE("a chunk box is anchored at pos*L and is L wide, not centred") {
    // The AABB convention. isChunkInside builds
    //   X in [pos.x*L, pos.x*L + L],  Z in [pos.y*L, pos.y*L + L],  Y in [0, HEIGHT]
    // so chunk k's box is [kL, (k+1)L] -- it is *not* centred on kL. A previous
    // revision was offset by -L/2, and the observable consequence is which
    // chunks the side planes clip.
    //
    // The consequence is easiest to see at the far plane, where the box's near
    // edge decides the verdict. The last chunk kept along -Z is the one whose
    // nearest edge is still inside the far plane:
    //     kept  <=>  -(k*L) + L >= -far   <=>   k <= (far + L - 1) / L
    // With far=1000 and L=16 that is k=63. A half-chunk-centred box would give
    // k=62, so this single assertion separates the two conventions.
    const int L = Constants::Chunk::LENGTH;
    Camera camera = cameraWithFov(45.0f);
    camera.m_Far = 1000.0f;
    Frustum frustum(&camera);

    const int lastKept = (camera.m_Far + L - 1) / L;
    CHECK(frustum.isChunkInside(glm::ivec2(0, -lastKept)));
    CHECK_FALSE(frustum.isChunkInside(glm::ivec2(0, -(lastKept + 1))));
  }

  TEST_CASE("culling is symmetric about the view axis for mirrored chunks") {
    // Because a box is [kL, (k+1)L] rather than centred, the mirror of chunk k
    // is chunk -(k+1), not -k. Comparing k against -k -- the obvious symmetry to
    // reach for -- compares two boxes that are not mirrors and legitimately
    // disagree near the side planes. That trap is worth encoding explicitly.
    Camera camera = cameraWithFov(45.0f);
    Frustum frustum(&camera);

    int mismatches = 0;
    for (int z = -60; z <= 60; ++z) {
      for (int k = 0; k < 60; ++k) {
        if (frustum.isChunkInside(glm::ivec2(k, z)) !=
            frustum.isChunkInside(glm::ivec2(-k - 1, z))) {
          ++mismatches;
        }
      }
    }
    CHECK_MESSAGE(mismatches == 0,
                  mismatches << " mirrored chunk pairs disagreed");
  }

  TEST_CASE("the frustum is not wastefully wider than the projection") {
    // The other direction from #127's invariant. Kept is legitimately larger
    // than visible -- a whole AABB is tested against six planes, so chunks
    // whose corners are all outside are still kept -- but an over-wide frustum
    // is a pure cost, and the shipped degrees version was measurably wider at
    // the default FOV.
    //
    // Measured on this exact grid, FOV 45, aspect 4/3, far 1000, 129x129:
    //   corrected  2142 visible, 2290 kept -> 1.069
    //   shipped    2142 visible, 3060 kept -> 1.429
    // A 1.35 bound sits between the two with room on both sides. The grid is
    // pinned because the ratio is grid-dependent: the same corrected frustum
    // measures 1.19 on a smaller grid, as near-camera boundary chunks dominate.
    Camera camera = cameraWithFov(Constants::Camera::DEFAULT_FOV);
    Frustum frustum(&camera);

    int visible = 0;
    int kept = 0;
    for (int x = -64; x <= 64; ++x) {
      for (int z = -64; z <= 64; ++z) {
        visible += visibleByProjection(camera, x, z) ? 1 : 0;
        kept += frustum.isChunkInside(glm::ivec2(x, z)) ? 1 : 0;
      }
    }

    REQUIRE(visible > 0);
    CAPTURE(visible);
    CAPTURE(kept);
    CHECK_MESSAGE(kept <= static_cast<int>(visible * 1.35f),
                  "kept " << kept << " of " << visible << " visible ("
                          << (static_cast<double>(kept) / visible)
                          << "x); the frustum is wider than the projection");
  }
}
