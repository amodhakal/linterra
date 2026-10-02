// Hierarchical culling: dropping whole subtrees of the spatial tree whose
// bounding cell is outside the view cone.
//
// The property that makes this safe, and the reason it is worth a test at all:
// a chunk must be drawn by the tree walk if and only if the flat per-chunk
// frustum test would have drawn it. Not "at least as many" -- exactly as many.
// Over-admitting is a wasted draw call and under-admitting is missing terrain,
// and only the second one is a bug worth failing a build for; but a test that
// only checks the safe direction cannot tell a correct walk from one that
// rejects everything, which is why the direction is pinned exactly here and the
// count of chunks actually drawn is compared too.
//
// Both sides of that comparison are computable with no GL context, because
// Frustum, Camera and ChunkOctree are all free of it. That is why this is a
// unit test and not a note in the commit message.

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "camera.h"
#include "chunk_octree.h"
#include "config.h"
#include "doctest/doctest.h"
#include "frustum.h"

namespace {

using Tree = ChunkOctree<int>;

// glm has no operator< for vec2.
struct Ivec2Less {
  bool operator()(const glm::ivec2 &a, const glm::ivec2 &b) const {
    return a.x != b.x ? a.x < b.x : a.y < b.y;
  }
};

using ChunkSet = std::set<glm::ivec2, Ivec2Less>;

// The world-box of a cell at `level` whose lowest chunk coordinate is `origin`.
// Identical arithmetic to the manager's draw-loop predicate, and at level 0
// identical to Frustum::isChunkInside's box -- which is the fact the whole
// equivalence rests on.
void cellBox(int level, const glm::ivec2 &origin, glm::vec3 &min,
             glm::vec3 &max) {
  const float chunk = static_cast<float>(Constants::Chunk::LENGTH);
  const float side = static_cast<float>(1 << level) * chunk;
  min = {static_cast<float>(origin.x) * chunk, 0.0f,
         static_cast<float>(origin.y) * chunk};
  max = {static_cast<float>(origin.x) * chunk + side,
         static_cast<float>(Constants::Chunk::HEIGHT),
         static_cast<float>(origin.y) * chunk + side};
}

constexpr std::int32_t kRenderDistanceChunks = Constants::Chunk::RENDER_DISTANCE_CHUNKS;

// Fills `tree` with every chunk of the streaming window around a chunk
// position, which is the state the manager reaches once streaming has settled.
// Takes the tree by reference because ChunkOctree is deliberately
// non-copyable, and returning one would need a move that its node-at-a-time
// ownership does not make free.
void fillSettledWorld(Tree &tree, std::int32_t centreX, std::int32_t centreZ) {
  for (std::int32_t x = centreX - kRenderDistanceChunks;
       x <= centreX + kRenderDistanceChunks; ++x) {
    for (std::int32_t z = centreZ - kRenderDistanceChunks;
         z <= centreZ + kRenderDistanceChunks; ++z) {
      tree.emplace(glm::ivec2{x, z}, 1);
    }
  }
}

// What the flat per-chunk test draws. This is the reference the hierarchical
// walk has to reproduce exactly.
ChunkSet drawnByPerChunkTest(const Tree &tree, Frustum &frustum) {
  ChunkSet drawn;
  tree.forEachValue([&](const glm::ivec2 &position, int &) {
    if (frustum.isChunkInside(position)) {
      drawn.insert(position);
    }
  });
  return drawn;
}

// What the hierarchical walk draws, and how many cells it had to test to do it.
ChunkSet drawnByTreeWalk(const Tree &tree, Frustum &frustum,
                         std::size_t &cellsTested) {
  ChunkSet drawn;
  tree.forEachValuePruned(
      [&](int level, const glm::ivec2 &origin) {
        glm::vec3 min;
        glm::vec3 max;
        cellBox(level, origin, min, max);
        return frustum.isBoundsInside(min, max);
      },
      [&](const glm::ivec2 &position, int &) { drawn.insert(position); });
  cellsTested = tree.lastCellsTested();
  return drawn;
}

// A camera at `position` with its heading derived from `yaw` degrees, matching
// the engine's convention: yaw -90 looks down -Z, which is DEFAULT_FRONT.
//
// The heading is set by writing m_Front directly rather than through a helper,
// because Camera has no updateVectors() -- m_Front is computed once in the
// constructor and m_Yaw is carried separately, and it is m_Front that Frustum
// and getView() actually read. A sweep that set m_Yaw and left m_Front alone
// would have swept twenty copies of the same camera.
Camera cameraWithYaw(const glm::vec3 &position, float yawDegrees) {
  Camera camera(position);
  const float yaw = glm::radians(yawDegrees);
  camera.m_Front = glm::normalize(glm::vec3(std::sin(yaw), 0.0f,
                                            -std::cos(yaw)));
  camera.m_Up = Constants::Camera::DEFAULT_UP;
  camera.m_WorldUp = camera.m_Up;
  camera.m_Yaw = yawDegrees;
  return camera;
}

Camera cameraLookingDownNegativeZ(float x, float y, float z) {
  return cameraWithYaw(glm::vec3(x, y, z),
                       Constants::Camera::DEFAULT_YAW);
}

}  // namespace

TEST_SUITE("Hierarchical culling") {
  TEST_CASE("the tree walk draws exactly what the per-chunk test draws") {
    // THE property. Swept over camera position and heading, because a walk that
    // agrees in one place can still differ in another: the cell boxes are larger
    // than the chunk boxes, so a subtree is rejected where an individual chunk
    // would not have been, and the only thing standing between that and missing
    // terrain is the cell test being conservative.
    int camerasExercised = 0;
    int chunksDrawn = 0;
    std::size_t worstCellsTested = 0;
    std::size_t totalCellsTested = 0;
    int comparisons = 0;

    const std::vector<glm::vec3> positions = {
        {8.0f, 100.0f, 8.0f},     {0.0f, 155.0f, 0.0f},
        {200.0f, 60.0f, -150.0f}, {-400.0f, 200.0f, 300.0f},
    };
    const std::vector<float> yaws = {-90.0f, 0.0f, 45.0f, 135.0f, 180.0f};

    for (const glm::vec3 &position : positions) {
      for (const float yaw : yaws) {
        Camera camera = cameraWithYaw(position, yaw);

        // The world is centred on the camera's own chunk, which is where the
        // manager keeps it.
        Tree tree;
        fillSettledWorld(
            tree,
            static_cast<std::int32_t>(std::floor(position.x /
                                                 Constants::Chunk::LENGTH)),
            static_cast<std::int32_t>(std::floor(position.z /
                                                 Constants::Chunk::LENGTH)));
        REQUIRE(tree.size() ==
                static_cast<std::size_t>((2 * kRenderDistanceChunks + 1) *
                                         (2 * kRenderDistanceChunks + 1)));

        Frustum frustum(&camera);
        const ChunkSet reference = drawnByPerChunkTest(tree, frustum);
        std::size_t cellsTested = 0;
        const ChunkSet actual = drawnByTreeWalk(tree, frustum, cellsTested);

        CAPTURE(position.x);
        CAPTURE(position.y);
        CAPTURE(position.z);
        CAPTURE(yaw);
        CHECK_MESSAGE(actual == reference,
                      "at camera (" << position.x << ", " << position.y << ", "
                                    << position.z << ") yaw " << yaw
                                    << ": hierarchical walk drew "
                                    << actual.size() << " chunks, per-chunk test "
                                    << "drew " << reference.size());
        if (!reference.empty()) {
          ++camerasExercised;
        }
        chunksDrawn += static_cast<int>(reference.size());
        worstCellsTested = std::max(worstCellsTested, cellsTested);
        totalCellsTested += cellsTested;
        ++comparisons;
      }
    }

    // A sweep where nothing was ever in view would pass every equality above
    // while testing nothing, which is the failure mode a "conservative" test is
    // most prone to.
    REQUIRE_MESSAGE(camerasExercised >= 10,
                    "only " << camerasExercised
                            << " of " << comparisons
                            << " camera setups had any visible geometry");
    // MESSAGE rather than INFO, so the measurement is in the log on a PASS.
    // INFO only prints on failure, and a saving nobody can see in the test
    // output is a saving nobody re-checks after the next change to the tree.
    MESSAGE("chunk draws compared: " << chunksDrawn
                                     << "  cells tested, worst case: "
                                     << worstCellsTested
                                     << "  cells tested, total: "
                                     << totalCellsTested);
  }

  TEST_CASE("the tree walk tests far fewer cells than there are chunks") {
    // The reason for doing this at all. A 65 x 65 window is 4225 chunks; testing
    // each one costs an eight-corner AABB test against six planes. Rejecting a
    // 64 x 64 chunk cell behind the camera costs the same test once and skips
    // everything under it.
    const Camera camera =
        cameraLookingDownNegativeZ(8.0f, 100.0f, 8.0f);
    Tree tree;
    fillSettledWorld(tree, 0, 0);
    Frustum frustum(&camera);

    std::size_t cellsTested = 0;
    const ChunkSet drawn = drawnByTreeWalk(tree, frustum, cellsTested);

    const std::size_t chunks = tree.size();
    MESSAGE("chunks resident: " << chunks << "  chunks drawn: " << drawn.size()
                               << "  cells tested: " << cellsTested);
    REQUIRE(chunks == 4225);
    // A leaf is tested only if every ancestor was kept, so the cell count is at
    // least the drawn count.
    CHECK(cellsTested >= drawn.size());
    // The threshold is deliberately modest. The measured factor is about 4x,
    // not 64x, and saying so is the point: a 64 x 64 chunk cell only prunes if
    // the view cone rejects it wholesale, and at a 45-degree FOV with a
    // 32-chunk render distance most of the resident window is in or near the
    // cone, so most cells are kept and descended. The win is real and bounded;
    // claiming a large one would be claiming something this geometry does not
    // deliver.
    CHECK_MESSAGE(cellsTested * 4 < chunks,
                  "tested " << cellsTested << " cells for " << chunks
                            << " chunks -- the hierarchy is not paying for "
                               "itself");
  }

  TEST_CASE("a predicate that rejects nothing is exactly forEachValue") {
    // The degenerate case, and the one that catches a traversal that visits
    // chunks the caller did not ask about -- a pruned walk that visited
    // everything regardless of the predicate would pass the equivalence test in
    // the first case whenever the frustum kept the whole world, which is not a
    // situation the sweep above can guarantee.
    Tree tree;
    fillSettledWorld(tree, 0, 0);
    ChunkSet viaPredicate;
    tree.forEachValuePruned(
        [](int, const glm::ivec2 &) { return true; },
        [&](const glm::ivec2 &position, int &) { viaPredicate.insert(position); });

    ChunkSet viaValue;
    tree.forEachValue(
        [&](const glm::ivec2 &position, int &) { viaValue.insert(position); });

    CHECK(viaPredicate == viaValue);
    CHECK(viaPredicate.size() == 4225);
  }

  TEST_CASE("a predicate that rejects everything draws nothing") {
    // The opposite degenerate case. A walk whose pruning was broken in the other
    // direction -- never descending, say -- would pass the "no false culls"
    // half of the first test trivially, and this is what rules that out.
    Tree tree;
    fillSettledWorld(tree, 0, 0);
    ChunkSet drawn;
    const std::size_t visited = tree.forEachValuePruned(
        [](int, const glm::ivec2 &) { return false; },
        [&](const glm::ivec2 &position, int &) { drawn.insert(position); });
    CHECK(visited == 0);
    CHECK(drawn.empty());
    // One cell tested per top-level cell, each rejected at the root, and
    // nothing below. A 65 x 65 window around a chunk at the origin straddles
    // four 64 x 64 top-level cells, so the count is 4 and NOT 1: a walk that
    // descended before testing would report far more.
    CHECK(tree.lastCellsTested() == 4);
  }

  TEST_CASE("a cell is never dropped while a chunk inside it is visible") {
    // THE conservative property, stated exhaustively over a grid of cells and
    // camera headings instead of as a hand-picked "this one straddles" case.
    //
    // Hand-picking a straddling cell is how this test gets written to pass
    // against a broken implementation: the geometry has to be chosen to
    // straddle, and choosing it wrong makes the assertion vacuous. Enumerating
    // every cell and every heading cannot be gamed that way, and it is the
    // property the whole pruning scheme rests on -- if a cell is ever dropped
    // while a visible chunk is inside it, that chunk is never drawn.
    int droppedWithVisibleChunk = 0;
    int cellsExercised = 0;
    int visibleChunksCovered = 0;

    for (const float yaw : {-90.0f, -45.0f, 0.0f, 60.0f, 135.0f}) {
      const Camera camera = cameraWithYaw(glm::vec3(40.0f, 120.0f, -25.0f), yaw);
      Frustum frustum(&camera);

      for (int level = 0; level <= 4; ++level) {
        const int side = 1 << level;
        for (int ox = -80; ox <= 80; ox += side) {
          for (int oz = -80; oz <= 80; oz += side) {
            const glm::ivec2 origin{ox, oz};
            glm::vec3 min;
            glm::vec3 max;
            cellBox(level, origin, min, max);
            if (!frustum.isBoundsInside(min, max)) {
              // Rejected. Nothing inside may be visible.
              for (int x = ox; x < ox + side; ++x) {
                for (int z = oz; z < oz + side; ++z) {
                  if (frustum.isChunkInside(glm::ivec2(x, z))) {
                    CAPTURE(yaw);
                    CAPTURE(level);
                    CAPTURE(ox);
                    CAPTURE(oz);
                    CAPTURE(x);
                    CAPTURE(z);
                    ++droppedWithVisibleChunk;
                  }
                }
              }
              continue;
            }
            ++cellsExercised;
            for (int x = ox; x < ox + side; ++x) {
              for (int z = oz; z < oz + side; ++z) {
                if (frustum.isChunkInside(glm::ivec2(x, z))) {
                  ++visibleChunksCovered;
                }
              }
            }
          }
        }
      }
    }

    CHECK_MESSAGE(droppedWithVisibleChunk == 0,
                  "a cell was culled while " << droppedWithVisibleChunk
                                              << " chunks inside it were "
                                                 "visible");
    // The kept half of the sweep has to have contained visible geometry, or the
    // rejection half proves nothing.
    REQUIRE_MESSAGE(cellsExercised > 1000,
                    "only " << cellsExercised << " cells were kept");
    MESSAGE("cells kept: " << cellsExercised
                           << "  visible chunk visits under kept cells: "
                           << visibleChunksCovered);
  }

  TEST_CASE("the AABB test and the chunk test agree at level 0") {
    // The equivalence the whole walk rests on: a level-0 cell's box IS a
    // chunk's box, so the cell test at the leaf is the per-chunk test the draw
    // loop used to do, and not a slightly different one.
    const Camera camera = cameraLookingDownNegativeZ(0.0f, 100.0f, 0.0f);
    Frustum frustum(&camera);
    int disagreements = 0;
    int compared = 0;
    for (int x = -40; x <= 40; ++x) {
      for (int z = -40; z <= 40; ++z) {
        glm::vec3 min;
        glm::vec3 max;
        cellBox(0, glm::ivec2(x, z), min, max);
        ++compared;
        if (frustum.isBoundsInside(min, max) !=
            frustum.isChunkInside(glm::ivec2(x, z))) {
          CAPTURE(x);
          CAPTURE(z);
          ++disagreements;
        }
      }
    }
    MESSAGE("chunks compared: " << compared);
    CHECK(compared == 6561);
    CHECK(disagreements == 0);
  }

  TEST_CASE("a cell entirely behind the camera is rejected") {
    // The other direction: the pruning has to be able to say no, or the whole
    // exercise is a traversal that always descends. A cell of side s starting at
    // chunk 4s is entirely at positive z, well behind a camera looking down -Z.
    const Camera camera = cameraLookingDownNegativeZ(0.0f, 100.0f, 0.0f);
    Frustum frustum(&camera);
    for (int level = 1; level <= 4; ++level) {
      const int side = 1 << level;
      const glm::ivec2 origin{0, 4 * side};
      glm::vec3 min;
      glm::vec3 max;
      cellBox(level, origin, min, max);
      CAPTURE(level);
      CHECK_FALSE(frustum.isBoundsInside(min, max));
    }
  }

  TEST_CASE("streaming is unaffected by the draw-loop cull") {
    // The two questions have to stay separate. The draw walk is driven by what
    // is RESIDENT and must never report a position as needing a chunk, because a
    // spawn loop that took its output for that would try to spawn terrain the
    // player cannot see -- and, worse, the pruning means a culled cell reports
    // nothing at all, which a naive caller reads as "empty".
    //
    // So: the pruned walk is not a substitute for the streaming walk, and the
    // test is that the streaming walk still reports every unstreamed position
    // in the window regardless of any frustum.
    Tree tree;
    tree.emplace(glm::ivec2{0, 0}, 1);
    const glm::ivec2 lo{-4, -4};
    const glm::ivec2 hi{5, 5};

    std::vector<glm::ivec2> wanted;
    tree.forEachCellIn(
        lo, hi,
        [](int, const glm::ivec2 &, const Tree::Node *) {
          return Tree::Action::Descend;
        },
        [&](int level, const glm::ivec2 &origin, const int *value) {
          if (level > 0 || value == nullptr) {
            const int side = 1 << level;
            for (int x = std::max(origin.x, lo.x);
                 x < std::min(origin.x + side, hi.x); ++x) {
              for (int z = std::max(origin.y, lo.y);
                   z < std::min(origin.y + side, hi.y); ++z) {
                wanted.push_back(glm::ivec2(x, z));
              }
            }
            return;
          }
        });
    CHECK(wanted.size() == 80);
  }
}
