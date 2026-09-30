#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <type_traits>

#include "chunk.h"
#include "doctest/doctest.h"

// PackedVertex is the 4-byte vertex format from Milestone 4. It is a union of a
// raw uint32_t and a bitfield, so a silent change to a field width or position
// would corrupt every mesh while still compiling cleanly. These tests pin the
// layout.
//
// Orderable ivec3 for the set comparisons below.
struct Vec3Less {
  bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
  }
};

// The field struct is named (`f`) rather than anonymous; an anonymous struct
// inside a union is a GNU extension and would trip -Wpedantic.

TEST_CASE("PackedVertex is exactly one uint32_t") {
  CHECK(sizeof(PackedVertex) == 4);
  CHECK(std::is_trivially_copyable_v<PackedVertex>);
  // The union must be usable as a flat array of 32-bit words on the GPU.
  CHECK(alignof(PackedVertex) == alignof(std::uint32_t));
}

TEST_CASE("PackedVertex fields occupy their documented bit ranges") {
  PackedVertex v{};
  v.bits = 0;

  // Set one field at a time to all-ones within its width and check that no
  // neighbouring bit moved.
  v.f.x = 0xFFu;
  CHECK(v.bits == 0x000000FFu);  // bits 0-7
  v.bits = 0;

  v.f.z = 0xFFu;
  CHECK(v.bits == 0x0000FF00u);  // bits 8-15
  v.bits = 0;

  v.f.y = 0x3FFu;
  CHECK(v.bits == 0x03FF0000u);  // bits 16-25 (10 bits, y up to 1023)
  v.bits = 0;

  v.f.normal = 0x3u;
  CHECK(v.bits == 0x0C000000u);  // bits 26-27
  v.bits = 0;

  v.f.texId = 0x3u;
  CHECK(v.bits == 0x30000000u);  // bits 28-29
  v.bits = 0;

  v.f.corner = 0x3u;
  CHECK(v.bits == 0xC0000000u);  // bits 30-31
}

TEST_CASE("PackedVertex field widths match their masks") {
  PackedVertex v{};

  // Read every bit-field into a named local before asserting: doctest binds
  // its expression by reference, and a non-const reference cannot bind to a
  // bit-field at all.
  v.f.x = 0xFFu;
  const std::uint32_t x = v.f.x;
  CHECK(x == 0xFFu);
  v.bits = 0;

  // The 10-bit y field is the one that silently truncates if someone rebalances
  // the bit budget; the comment promises worlds up to y=1023.
  v.f.y = 1023u;
  const std::uint32_t yMax = v.f.y;
  CHECK(yMax == 1023u);
  v.bits = 0;

  // One past the field's range must wrap within y and must not bleed upwards
  // into `normal`. Volatile keeps the value opaque so the compiler does not
  // turn this into a compile-time bit-field truncation diagnostic.
  volatile std::uint32_t outOfRange = 1024u;
  v.f.y = outOfRange;
  const std::uint32_t yWrapped = v.f.y;
  const std::uint32_t normalBits = v.f.normal;
  CHECK(yWrapped == 0u);
  CHECK(normalBits == 0u);
  CHECK((v.bits & 0x0C000000u) == 0u);
}

TEST_CASE("PackedVertex round-trips a full vertex through the raw word") {
  PackedVertex v{};
  v.bits = 0;
  v.f.x = 0xAB;
  v.f.z = 0xCD;
  v.f.y = 0x3FF;
  v.f.normal = 0x2;
  v.f.texId = 0x1;
  v.f.corner = 0x3;

  // The exact word matters: this is what gets uploaded to the VBO, and a
  // dependency on bitfield layout is implementation-defined, so assert the
  // composed value rather than re-deriving it from the same fields.
  CHECK(v.bits == 0xDBFFCDABu);

  PackedVertex readBack{};
  readBack.bits = v.bits;

  const std::uint32_t x = readBack.f.x;
  const std::uint32_t z = readBack.f.z;
  const std::uint32_t y = readBack.f.y;
  const std::uint32_t normal = readBack.f.normal;
  const std::uint32_t texId = readBack.f.texId;
  const std::uint32_t corner = readBack.f.corner;

  CHECK(x == 0xABu);
  CHECK(z == 0xCDu);
  CHECK(y == 0x3FFu);
  CHECK(normal == 0x2u);
  CHECK(texId == 0x1u);
  CHECK(corner == 0x3u);
}

TEST_CASE("PackedVertex is 32 bits wide with no padding") {
  PackedVertex v{};
  v.bits = 0xFFFFFFFFu;
  CHECK(v.bits == 0xFFFFFFFFu);
  CHECK(sizeof(v) == sizeof(std::uint32_t));
}

// ---------------------------------------------------------------------------
// Face winding (#140)
//
// addQuad emits its first triangle as (a, a+dv, a+du), so that triangle's
// right-hand-rule normal is dv x du = -(du x dv). With GL_CULL_FACE enabled and
// the default GL_CCW front-face convention, a quad is visible from outside the
// block only when that normal equals the face's outward normal.
//
// Getting this backwards is invisible: the geometry is still emitted, it is
// just culled, so the world quietly loses every bottom face. Nothing else in
// the build notices.
// ---------------------------------------------------------------------------

TEST_SUITE("FaceWinding") {
  namespace {
  glm::ivec3 windingNormal(const FaceWinding& w) {
    const glm::vec3 du(w.duX, w.duY, w.duZ);
    const glm::vec3 dv(w.dvX, w.dvY, w.dvZ);
    const glm::ivec3 n = glm::ivec3(-glm::cross(du, dv));
    return n;
  }
  }  // namespace

  TEST_CASE("every face winding produces its outward normal") {
    // The table carries the intended outward normal, so this compares the
    // winding against an independent statement of intent rather than
    // re-deriving the same formula on both sides.
    for (int direction = 0; direction < 6; ++direction) {
      const FaceWinding w = faceWindingForDirection(direction);
      CAPTURE(direction);
      CHECK(windingNormal(w) == glm::ivec3(w.outX, w.outY, w.outZ));
    }
  }

  TEST_CASE("the six face windings are the six distinct axis directions") {
    // Each face must point somewhere different, and the set must be exactly
    // the six axis directions. A duplicate here is how two faces end up wound
    // the same way and one of them silently disappears.
    std::set<glm::ivec3, Vec3Less> normals;
    for (int direction = 0; direction < 6; ++direction) {
      normals.insert(windingNormal(faceWindingForDirection(direction)));
    }

    REQUIRE(normals.size() == 6);
    const std::set<glm::ivec3, Vec3Less> expected{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    CHECK(normals == expected);
  }

  TEST_CASE("the bottom face points down") {
    // The specific regression. Face 3 previously reused face 2's edge vectors,
    // so a -Y face was wound to produce a +Y normal and was back-face culled
    // from every angle.
    const FaceWinding bottom = faceWindingForDirection(3);
    CHECK(windingNormal(bottom) == glm::ivec3(0, -1, 0));
    CHECK(glm::ivec3(bottom.outX, bottom.outY, bottom.outZ) ==
          glm::ivec3(0, -1, 0));
    // And it must not be the top face's winding.
    const FaceWinding top = faceWindingForDirection(2);
    CHECK(std::memcmp(&bottom, &top, sizeof(FaceWinding)) != 0);
  }

  TEST_CASE("an invalid face direction is rejected") {
    // Better a throw at the call site than a silently zero winding, which would
    // produce a degenerate normal and a face that is never culled.
    CHECK_THROWS_AS(faceWindingForDirection(6), std::out_of_range);
    CHECK_THROWS_AS(faceWindingForDirection(-1), std::out_of_range);
  }
}
