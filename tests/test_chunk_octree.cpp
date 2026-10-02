// Sparse voxel octree over chunk coordinates: locate, insert, erase, traverse.
//
// The point of these tests is that the tree answers the two questions the flat
// std::unordered_map<glm::ivec2, Chunk> could not answer without visiting
// everything: "is this chunk here" in bounded steps, and "which cells near the
// camera are NOT already fully resident" in work proportional to the answer
// rather than to the size of the render window.
//
// Pure data-structure tests. No GL, no renderer, no ChunkManager -- the tree is
// templated on its payload precisely so the suite can instantiate it with an
// int, which is what makes any of this runnable in a headless ctest.

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "chunk_octree.h"
#include "config.h"
#include "doctest/doctest.h"

namespace {

using Tree = ChunkOctree<int>;

// The render window the manager walks every frame, in chunk coordinates.
constexpr std::int32_t kRenderDistanceChunks = Constants::Chunk::RENDER_DISTANCE_CHUNKS;
constexpr std::int32_t kWindowSide = 2 * kRenderDistanceChunks + 1;

// glm has no operator< for vec2, so a std::set of chunk coordinates needs one.
// Lexicographic on (x, y) -- an arbitrary but total order, which is all a set
// needs.
struct Ivec2Less {
  bool operator()(const glm::ivec2 &a, const glm::ivec2 &b) const {
    return a.x != b.x ? a.x < b.x : a.y < b.y;
  }
};

struct CellRange {
  glm::ivec2 lo;
  glm::ivec2 hi;
};

CellRange windowAround(std::int32_t chunkX, std::int32_t chunkZ) {
  return {glm::ivec2{chunkX - kRenderDistanceChunks,
                     chunkZ - kRenderDistanceChunks},
          glm::ivec2{chunkX + kRenderDistanceChunks + 1,
                     chunkZ + kRenderDistanceChunks + 1}};
}

// Populate a tree with every chunk of a window, so the tree is in the state the
// manager reaches once streaming has settled.
void fillWindow(Tree &tree, const CellRange &range) {
  for (int32_t x = range.lo.x; x < range.hi.x; ++x) {
    for (int32_t z = range.lo.y; z < range.hi.y; ++z) {
      tree.emplace(glm::ivec2{x, z}, x * 1000 + z);
    }
  }
}

// Expand a whole-cell report into the individual chunk coordinates of the query
// window that fall inside it.
//
// The clip matters. A report for an unstreamed cell covers 2^level x 2^level
// chunks, but the query window does not line up with the cell grid, so an
// un-clipped expansion reports chunks the caller never asked about. A spawn loop
// that trusted it would enqueue terrain outside its own render distance.
void expandCell(int level, const glm::ivec2 &origin, const CellRange &range,
                std::vector<glm::ivec2> &out) {
  const int side = 1 << level;
  const int loX = std::max(origin.x, range.lo.x);
  const int loZ = std::max(origin.y, range.lo.y);
  const int hiX = std::min(origin.x + side, range.hi.x);
  const int hiZ = std::min(origin.y + side, range.hi.y);
  for (int x = loX; x < hiX; ++x) {
    for (int z = loZ; z < hiZ; ++z) {
      out.push_back(glm::ivec2(x, z));
    }
  }
}

}  // namespace

TEST_SUITE("ChunkOctree locate") {
  TEST_CASE("an absent chunk is absent") {
    Tree tree;
    CHECK(tree.find(glm::ivec2{0, 0}) == nullptr);
    CHECK_FALSE(tree.contains(glm::ivec2{0, 0}));
    CHECK(tree.size() == 0);
    CHECK(tree.empty());
  }

  TEST_CASE("insert then find returns the same value, everywhere") {
    // Includes negative coordinates, which is the half of the integer line a
    // truncating index gets wrong. floorShift is the derivation and this is the
    // check: every one of these must land in the top-level cell that
    // topCellFor names, or the tree has a cell it can never search again.
    Tree tree;
    const std::vector<glm::ivec2> positions = {
        {0, 0},   {1, 1},   {-1, -1},  {-1, 0},   {0, -1},
        {63, 63}, {64, 64}, {65, 65},  {-64, -64}, {-65, -65},
        {-1000, 1000}, {1000, -1000}, {12345, -67890},
    };
    int next = 0;
    for (const glm::ivec2 &position : positions) {
      int *stored = tree.emplace(position, next++);
      REQUIRE(stored != nullptr);
      CAPTURE(position.x);
      CAPTURE(position.y);
      CHECK(*stored == next - 1);
    }
    CHECK(tree.size() == positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
      CAPTURE(positions[i].x);
      CAPTURE(positions[i].y);
      const int *found = tree.find(positions[i]);
      REQUIRE(found != nullptr);
      CHECK(*found == static_cast<int>(i));
      CHECK(tree.contains(positions[i]));
    }
  }

  TEST_CASE("emplace on an occupied chunk returns the existing value") {
    // The unordered_map try_emplace contract the manager relies on: the
    // in-flight map must not replace a TaskResult a worker thread is writing
    // into, and the spawn loop must not create a second entry for a position it
    // already owns.
    Tree tree;
    int *first = tree.emplace(glm::ivec2{5, -9}, 111);
    int *second = tree.emplace(glm::ivec2{5, -9}, 222);
    CHECK(first == second);
    CHECK(*first == 111);
    CHECK(tree.size() == 1);
  }

  TEST_CASE("the returned pointer survives later insertions") {
    // The stability the manager depends on. A worker task captures
    // `TaskResult &` at enqueue time and dereferences it frames later, while
    // the main thread keeps inserting. If the tree moved its payload on
    // growth, that reference would dangle -- the same hazard an unordered_map
    // node-based storage avoids and a vector would not.
    Tree tree;
    int *pinned = tree.emplace(glm::ivec2{0, 0}, 7);
    REQUIRE(pinned != nullptr);
    *pinned = 4242;
    for (int32_t x = -70; x <= 70; ++x) {
      for (int32_t z = -70; z <= 70; ++z) {
        tree.emplace(glm::ivec2{x, z}, 0);
      }
    }
    CHECK(*pinned == 4242);
    CHECK(*tree.find(glm::ivec2{0, 0}) == 4242);
  }

  TEST_CASE("erase removes the value and reports whether it was there") {
    Tree tree;
    tree.emplace(glm::ivec2{2, 3}, 1);
    CHECK(tree.erase(glm::ivec2{2, 3}) == 1);
    CHECK(tree.erase(glm::ivec2{2, 3}) == 0);
    CHECK(tree.find(glm::ivec2{2, 3}) == nullptr);
    CHECK(tree.size() == 0);
    // A neighbour in the same parent cell must survive: the prune is upward and
    // stops at the first ancestor that still holds something.
    tree.emplace(glm::ivec2{3, 3}, 2);
    tree.erase(glm::ivec2{3, 3});
    CHECK(tree.find(glm::ivec2{2, 3}) == nullptr);
  }

  TEST_CASE("erasing everything leaves no nodes behind") {
    // A leak here is invisible until the player has walked far enough for the
    // node count to matter, so it is asserted directly.
    Tree tree;
    fillWindow(tree, windowAround(0, 0));
    const std::size_t populated = tree.nodeCount();
    CHECK(populated > tree.size());
    for (int32_t x = -kRenderDistanceChunks; x <= kRenderDistanceChunks; ++x) {
      for (int32_t z = -kRenderDistanceChunks; z <= kRenderDistanceChunks; ++z) {
        tree.erase(glm::ivec2{x, z});
      }
    }
    CHECK(tree.size() == 0);
    CHECK(tree.nodeCount() == 0);
    CHECK(tree.topLevelCellCount() == 0);
  }
}

TEST_SUITE("ChunkOctree aggregates") {
  TEST_CASE("a cell is complete only when every chunk in it is resident") {
    // The property the spawn scan leans on. A level-3 cell is 8 x 8 = 64 chunk
    // coordinates, so completeness has to mean all 64 and not "some".
    constexpr int kLevel = 3;
    const int side = 1 << kLevel;
    Tree tree;
    const glm::ivec2 origin{0, 0};
    CHECK_FALSE(tree.isCellComplete(kLevel, origin));

    // side x side insertions, one per chunk of the cell, asserting the cell is
    // still incomplete right up to the last one. Filling side*side per axis
    // would overflow the cell, and the assertion would then be passing for the
    // wrong reason.
    for (int x = 0; x < side; ++x) {
      for (int z = 0; z < side; ++z) {
        CAPTURE(x);
        CAPTURE(z);
        CHECK_FALSE(tree.isCellComplete(kLevel, origin));
        tree.emplace(glm::ivec2{x, z}, 1);
      }
    }
    CHECK(tree.isCellComplete(kLevel, origin));
    CHECK(tree.cellCount(kLevel, origin) ==
          static_cast<std::size_t>(side * side));
  }

  TEST_CASE("a hole in a cell makes it incomplete again") {
    // The other direction, and the one an eviction sweep produces: streaming
    // fills a cell, the camera walks away, chunks are evicted, and the cell has
    // to stop claiming it is finished or the spawn scan would never refill it.
    constexpr int kLevel = 2;
    const int side = 1 << kLevel;
    Tree tree;
    for (int x = 0; x < side; ++x) {
      for (int z = 0; z < side; ++z) {
        tree.emplace(glm::ivec2{x, z}, 1);
      }
    }
    REQUIRE(tree.isCellComplete(kLevel, glm::ivec2{0, 0}));
    tree.erase(glm::ivec2{1, 1});
    CHECK_FALSE(tree.isCellComplete(kLevel, glm::ivec2{0, 0}));
    CHECK(tree.cellCount(kLevel, glm::ivec2{0, 0}) ==
          static_cast<std::size_t>(side * side - 1));
    // ...and re-filling the hole restores it.
    tree.emplace(glm::ivec2{1, 1}, 1);
    CHECK(tree.isCellComplete(kLevel, glm::ivec2{0, 0}));
  }

  TEST_CASE("completeness is tracked in a window that straddles the origin") {
    // Negative coordinates land in a different top-level cell, so the aggregate
    // has to be maintained per subtree and not globally.
    constexpr int kLevel = 2;
    const int side = 1 << kLevel;
    Tree tree;
    for (int x = -side; x < 0; ++x) {
      for (int z = -side; z < 0; ++z) {
        tree.emplace(glm::ivec2{x, z}, 1);
      }
    }
    CHECK(tree.isCellComplete(kLevel, glm::ivec2{-side, -side}));
    CHECK_FALSE(tree.isCellComplete(kLevel, glm::ivec2{0, 0}));
  }
}

TEST_SUITE("ChunkOctree traversal") {
  TEST_CASE("every resident chunk is visited exactly once") {
    Tree tree;
    const CellRange range = windowAround(0, 0);
    fillWindow(tree, range);

    std::vector<glm::ivec2> visited;
    const std::size_t count = tree.forEachValue(
        [&](const glm::ivec2 &position, int &) { visited.push_back(position); });

    CHECK(count == static_cast<std::size_t>(kWindowSide * kWindowSide));
    CHECK(visited.size() == count);
    // Every position exactly once: a duplicate would be a chunk drawn twice.
    const std::set<glm::ivec2, Ivec2Less> unique(visited.begin(), visited.end());
    CHECK(unique.size() == visited.size());
    for (const glm::ivec2 &position : unique) {
      CAPTURE(position.x);
      CAPTURE(position.y);
      CHECK(position.x >= range.lo.x);
      CHECK(position.x < range.hi.x);
      CHECK(position.y >= range.lo.y);
      CHECK(position.y < range.hi.y);
    }
  }

  TEST_CASE("the walk order is deterministic") {
    // A hash map at the top level would give an arbitrary order, so the tree
    // sorts the top-level cells. That is what makes a traversal reproducible
    // frame to frame, and it is what lets a test assert an order at all.
    Tree tree;
    fillWindow(tree, windowAround(3, -7));

    std::vector<glm::ivec2> first;
    tree.forEachValue(
        [&](const glm::ivec2 &position, int &) { first.push_back(position); });
    for (int attempt = 0; attempt < 4; ++attempt) {
      std::vector<glm::ivec2> again;
      tree.forEachValue(
          [&](const glm::ivec2 &position, int &) { again.push_back(position); });
      CHECK(again == first);
    }
  }

  TEST_CASE("a fully resident window is skipped whole by the completeness test") {
    // THE measurement. The old spawn loop walked all
    // (2 * RENDER_DISTANCE_CHUNKS + 1)^2 = 4225 grid positions every frame and
    // discovered, one hash lookup at a time, that every one of them was already
    // meshed. With the aggregates, a settled region costs a handful of cell
    // tests.
    Tree tree;
    const CellRange range = windowAround(0, 0);
    fillWindow(tree, range);

    int resident = 0;
    int absent = 0;
    const Tree::WalkStats stats = tree.forEachCellIn(
        range.lo, range.hi,
        [](int level, const glm::ivec2 &, const Tree::Node *node) {
          return (node != nullptr && node->complete)
                     ? Tree::Action::Skip
                     : Tree::Action::Descend;
        },
        [&](int level, const glm::ivec2 &, const int *value) {
          CAPTURE(level);
          if (value != nullptr) {
            ++resident;
          } else {
            ++absent;
          }
        });

    INFO("grid positions: " << kWindowSide * kWindowSide
                             << "  cells visited: " << stats.cellsVisited
                             << "  nodes visited: " << stats.nodesVisited
                             << "  subtrees skipped: " << stats.subtreesSkipped);
    // Nothing needed doing, so nothing should have been examined at leaf level.
    CHECK(resident == 0);
    CHECK(absent == 0);
    // The top-level cell is 64 x 64 chunk coordinates and the window is 65 x 65,
    // so a window centred on the origin straddles four top-level cells and each
    // contributes a 1-chunk-wide strip. Those strips are incomplete, so they are
    // descended; the interior is not.
    CHECK(stats.cellsVisited < 400);
    CHECK(stats.nodesVisited < 600);
  }

  TEST_CASE("a window with one hole visits only the cells containing it") {
    // The complement of the previous case, and the reason the traversal is worth
    // having: one missing chunk out of 4225 should cost work proportional to the
    // cell that contains it, not to the window.
    Tree tree;
    const CellRange range = windowAround(0, 0);
    fillWindow(tree, range);
    const glm::ivec2 hole{7, -11};
    tree.erase(hole);

    // Reports are collected whole-cell, so an erased hole can arrive either as a
    // single level-0 report or, when the hole emptied its whole 2 x 2 cell, as
    // one report for the cell. Expanding both ways gives the same chunk set,
    // which is the point: the caller's answer does not depend on how the tree
    // happens to be shaped at that moment.
    std::vector<glm::ivec2> visited;
    const Tree::WalkStats stats = tree.forEachCellIn(
        range.lo, range.hi,
        [](int level, const glm::ivec2 &, const Tree::Node *node) {
          return (node != nullptr && node->complete)
                     ? Tree::Action::Skip
                     : Tree::Action::Descend;
        },
        [&](int level, const glm::ivec2 &position, const int *value) {
          if (level > 0) {
            // A whole unstreamed cell: every chunk in it needs streaming, and
            // there is nothing resident to keep.
            REQUIRE(value == nullptr);
            expandCell(level, position, range, visited);
            return;
          }
          if (value == nullptr) {
            visited.push_back(position);
          }
        });

    INFO("cells visited: " << stats.cellsVisited
                            << "  empty cells: " << stats.emptyCellsReported
                            << "  nodes: " << stats.nodesVisited);
    // Exactly the hole, and nothing else. One missing chunk out of 4225 must not
    // drag in its neighbours.
    CHECK(visited.size() == 1);
    CHECK(visited.front() == hole);
    // The saving: the whole 65 x 65 window, minus the one cell containing the
    // hole, is skipped by the completeness test. The old loop paid 4225
    // iterations to learn the same thing.
    CHECK(stats.cellsVisited + stats.emptyCellsReported < 8);
  }

  TEST_CASE("the cell walk reports absent chunks, which a resident-only walk cannot") {
    // The spawn loop's actual job: find positions that have NO chunk. A traversal
    // that only visited resident values could not express it, which is why
    // forEachCellIn hands out level-0 cells with a possibly-null value.
    Tree tree;
    const CellRange range = windowAround(0, 0);
    tree.emplace(glm::ivec2{0, 0}, 1);

    std::vector<glm::ivec2> wanted;
    tree.forEachCellIn(
        range.lo, range.hi,
        [](int, const glm::ivec2 &, const Tree::Node *) {
          return Tree::Action::Descend;
        },
        [&](int level, const glm::ivec2 &position, const int *value) {
          if (level == 0) {
            if (value == nullptr) {
              wanted.push_back(position);
            }
            return;
          }
          // A whole unstreamed cell: expand it, because the caller has to be
          // able to spawn every chunk in it.
          expandCell(level, position, range, wanted);
        });

    CHECK(wanted.size() == static_cast<std::size_t>(kWindowSide * kWindowSide) - 1);
  }

  TEST_CASE("a cell that does not exist at all is still reported") {
    // A null node is not a node with no value. The manager has to see the
    // distinction: an empty cell may hold chunks that need spawning, and a
    // visitor that cannot tell them apart would either skip streaming or
    // re-stream forever.
    Tree tree;
    const CellRange range = windowAround(0, 0);
    int nullNodes = 0;
    int valuedNodes = 0;
    tree.forEachCellIn(
        range.lo, range.hi,
        [](int, const glm::ivec2 &, const Tree::Node *) {
          return Tree::Action::Descend;
        },
        [&](int level, const glm::ivec2 &position, const int *value) {
          if (value != nullptr) {
            ++valuedNodes;
            return;
          }
          std::vector<glm::ivec2> expanded;
          expandCell(level, position, range, expanded);
          nullNodes += static_cast<int>(expanded.size());
        });
    CHECK(nullNodes == static_cast<int>(kWindowSide * kWindowSide));
    CHECK(valuedNodes == 0);
  }

  TEST_CASE("the window bounds exclude chunks outside them") {
    // The old loop's bounds were a double-sided for over a square grid; the
    // replacement takes a half-open range and has to agree with it exactly, or
    // the streaming window silently grows or shrinks by a chunk.
    //
    // The tree is empty, so every report is a whole-cell one and the callback
    // has to expand and CLIP it to the range. Without the clip this reports a
    // whole 64 x 64 top-level cell -- 4096 chunks -- for a 9 x 9 window.
    Tree tree;
    const CellRange range{glm::ivec2{-4, -4}, glm::ivec2{5, 5}};
    std::vector<glm::ivec2> visited;
    tree.forEachCellIn(
        range.lo, range.hi,
        [](int, const glm::ivec2 &, const Tree::Node *) {
          return Tree::Action::Descend;
        },
        [&](int level, const glm::ivec2 &position, const int *) {
          expandCell(level, position, range, visited);
        });
    CHECK(visited.size() == 81);
    for (const glm::ivec2 &position : visited) {
      CHECK(position.x >= -4);
      CHECK(position.x < 5);
      CHECK(position.y >= -4);
      CHECK(position.y < 5);
    }
  }
}

TEST_SUITE("ChunkOctree mutation during traversal") {
  TEST_CASE("a dropped entry is gone once the walk finishes") {
    // The promote loop's shape. It promotes or discards entries as it walks, so
    // the container has to survive removal during traversal -- and a removal that
    // frees a node the walk is standing on is the failure this guards.
    Tree tree;
    for (int32_t x = -40; x <= 40; ++x) {
      for (int32_t z = -40; z <= 40; ++z) {
        tree.emplace(glm::ivec2{x, z}, x * 100 + z);
      }
    }
    const std::size_t before = tree.size();

    std::size_t kept = 0;
    std::size_t dropped = 0;
    tree.forEachEntry([&](const glm::ivec2 &position, int &value) {
      (void)value;
      if ((position.x + position.y) % 2 == 0) {
        ++kept;
        return true;
      }
      ++dropped;
      return false;
    });

    CHECK(kept + dropped == before);
    CHECK(tree.size() == kept);
    CHECK(dropped > 0);
    for (int32_t x = -40; x <= 40; ++x) {
      for (int32_t z = -40; z <= 40; ++z) {
        const bool shouldSurvive = ((x + z) % 2 == 0);
        CAPTURE(x);
        CAPTURE(z);
        CHECK(tree.contains(glm::ivec2{x, z}) == shouldSurvive);
      }
    }
  }

  TEST_CASE("dropping every entry empties the tree completely") {
    // The worst case for a deferred erase: every node in the walk is queued for
    // deletion, so the whole subtree structure has to unwind and no node may
    // survive.
    Tree tree;
    for (int32_t x = -70; x <= 70; ++x) {
      for (int32_t z = -70; z <= 70; ++z) {
        tree.emplace(glm::ivec2{x, z}, 1);
      }
    }
    const std::size_t before = tree.size();
    REQUIRE(before > 0);

    tree.forEachEntry([](const glm::ivec2 &, int &) { return false; });

    CHECK(tree.size() == 0);
    CHECK(tree.nodeCount() == 0);
    CHECK(tree.topLevelCellCount() == 0);
    CHECK(tree.find(glm::ivec2{0, 0}) == nullptr);
  }

  TEST_CASE("the tree is usable again after a traversal dropped entries") {
    // The manager re-inserts on the very next frame, into a tree whose nodes
    // were just freed. If the freelist bookkeeping were wrong this is where it
    // shows.
    Tree tree;
    for (int32_t x = -20; x <= 20; ++x) {
      for (int32_t z = -20; z <= 20; ++z) {
        tree.emplace(glm::ivec2{x, z}, 1);
      }
    }
    tree.forEachEntry([](const glm::ivec2 &, int &) { return false; });
    REQUIRE(tree.nodeCount() == 0);

    tree.emplace(glm::ivec2{3, 4}, 99);
    CHECK(tree.find(glm::ivec2{3, 4}) != nullptr);
    CHECK(*tree.find(glm::ivec2{3, 4}) == 99);
    CHECK(tree.size() == 1);
  }

  TEST_CASE("eraseSubtrees evicts whole cells and reports each value") {
    // The eviction sweep. onEvict is where the caller releases GL objects, so it
    // has to run exactly once per evicted chunk and before the value dies.
    Tree tree;
    const CellRange range = windowAround(0, 0);
    fillWindow(tree, range);
    const std::size_t before = tree.size();
    REQUIRE(before == static_cast<std::size_t>(kWindowSide * kWindowSide));

    // Evict every 8 x 8 cell whose origin has both coordinates even.
    constexpr int kEvictLevel = 3;
    const int side = 1 << kEvictLevel;
    std::size_t onEvictCalls = 0;
    const std::size_t evicted = tree.eraseSubtrees(
        [=](int level, const glm::ivec2 &origin) {
          if (level != kEvictLevel) {
            return false;
          }
          return (origin.x % (2 * side) == 0) && (origin.y % (2 * side) == 0);
        },
        [&](int &) { ++onEvictCalls; });

    CHECK(evicted == onEvictCalls);
    CHECK(evicted > 0);
    CHECK(tree.size() == before - evicted);

    // Every survivor is still findable, and every evicted cell is empty. A cell
    // either went whole or not at all: half an evicted cell is a subtree whose
    // parent was freed while a child was still wanted.
    std::set<glm::ivec2, Ivec2Less> survivors;
    tree.forEachValue(
        [&](const glm::ivec2 &position, int &) { survivors.insert(position); });
    for (int32_t x = range.lo.x; x < range.hi.x; ++x) {
      for (int32_t z = range.lo.y; z < range.hi.y; ++z) {
        const glm::ivec2 position{x, z};
        const bool cellEvicted =
            Tree::floorShift(x, kEvictLevel) % 2 == 0 &&
            Tree::floorShift(z, kEvictLevel) % 2 == 0;
        CAPTURE(x);
        CAPTURE(z);
        CHECK(survivors.count(position) == (cellEvicted ? 0u : 1u));
        CHECK(tree.contains(position) == (cellEvicted ? false : true));
      }
    }
  }
}

TEST_SUITE("ChunkOctree geometry") {
  TEST_CASE("floorShift floors rather than truncates, including negatives") {
    // floor(a / 2^shift). Truncating would put the back half of every cell in
    // the wrong parent, which is the #132 bug one level up: a chunk at -1 has
    // to belong to the cell whose origin is -1, not to the cell at 0.
    for (int shift = 1; shift <= 12; ++shift) {
      for (int a = -300; a <= 300; ++a) {
        CAPTURE(shift);
        CAPTURE(a);
        CHECK(Tree::floorShift(a, shift) * (1 << shift) <= a);
        CHECK(a < (Tree::floorShift(a, shift) + 1) * (1 << shift));
      }
    }
    CHECK(Tree::floorShift(-1, 6) == -1);
    CHECK(Tree::floorShift(-64, 6) == -1);
    CHECK(Tree::floorShift(-65, 6) == -2);
    CHECK(Tree::floorShift(0, 6) == 0);
    CHECK(Tree::floorShift(64, 6) == 1);
  }

  TEST_CASE("the top-level cell is a power-of-two square, twice the render distance") {
    // Derivation of kSubtreeLevels. RENDER_DISTANCE_BLOCKS is 512 and LENGTH is
    // 16, so the streaming window spans 65 chunk coordinates; the top-level cell
    // is 64, i.e. 1024 blocks -- it does not have to contain the whole window
    // (which is why the walk iterates top-level cells rather than assuming one
    // root), but it is the same order, so the number of them stays small.
    CHECK(Constants::Chunk::RENDER_DISTANCE_BLOCKS ==
          Constants::Chunk::RENDER_DISTANCE_CHUNKS * Constants::Chunk::LENGTH);
    CHECK(Tree::kSubtreeSide * Constants::Chunk::LENGTH == 1024);
    CHECK(Tree::kSubtreeSide == 64);
  }

  TEST_CASE("childIndex picks the child that actually contains the position") {
    // The descent is only correct if the index it computes is the child whose
    // cell contains the coordinate, at every level. Checked against the child's
    // cell bounds rather than against a restatement of the formula, so a
    // wrong convention fails here instead of silently mis-filing chunks.
    //
    // childIndex takes the PARENT's level: a parent cell of side 2^L splits at
    // 2^(L-1), so bit (L-1) of the coordinate names the child.
    for (int parentLevel = 1; parentLevel <= Tree::kSubtreeLevels; ++parentLevel) {
      const int parentSide = 1 << parentLevel;
      const int childSide = 1 << (parentLevel - 1);
      for (int x = -200; x <= 200; ++x) {
        for (int y = -200; y <= 200; ++y) {
          const glm::ivec2 position{x, y};
          // The parent cell is the top-level cell's descendant at this level.
          const glm::ivec2 parentOrigin{
              Tree::floorShift(x, parentLevel) * parentSide,
              Tree::floorShift(y, parentLevel) * parentSide};
          const int index = Tree::childIndex(position, parentLevel);
          const glm::ivec2 childOrigin =
              parentOrigin +
              glm::ivec2((index & 1) * childSide, ((index >> 1) & 1) * childSide);
          CAPTURE(parentLevel);
          CAPTURE(x);
          CAPTURE(y);
          CHECK(x >= childOrigin.x);
          CHECK(x < childOrigin.x + childSide);
          CHECK(y >= childOrigin.y);
          CHECK(y < childOrigin.y + childSide);
        }
      }
    }
  }

  TEST_CASE("cellsOverlap accepts every in-range cell and rejects the far ones") {
    // The walk's bounding test. It has to accept every cell that contains a
    // chunk of the range, or streaming silently loses the corners of the
    // window -- the kind of bug that only shows up as missing terrain at the
    // edge of vision, and only sometimes.
    //
    // The converse is deliberately NOT asserted: a cell may overlap the range
    // while containing no chunk of it (a cell straddling one edge), and
    // descending into such a cell is correct, just slightly more work. Asserting
    // overlap == "contains an in-range chunk" would be asserting that the test
    // is wrong.
    const glm::ivec2 lo(-9, -9);
    const glm::ivec2 hi(11, 11);
    int cellsContainingRange = 0;
    for (int level = 0; level <= 5; ++level) {
      const int side = 1 << level;
      // Every cell origin on a grid of that side, spanning well past the range.
      for (int ox = -48; ox <= 48; ox += side) {
        for (int oy = -48; oy <= 48; oy += side) {
          const glm::ivec2 origin{ox, oy};
          CAPTURE(level);
          CAPTURE(ox);
          CAPTURE(oy);
          const bool overlap = Tree::cellsOverlap(lo, hi, level, origin);

          // A cell entirely below/left of the range, or entirely at/above it,
          // cannot contain any chunk of the range and must be rejected.
          const bool entirelyOutside = origin.x + side <= lo.x ||
                                       origin.y + side <= lo.y ||
                                       origin.x >= hi.x || origin.y >= hi.y;
          if (entirelyOutside) {
            CHECK_FALSE(overlap);
            continue;
          }
          // A cell fully inside the range must be accepted.
          if (origin.x >= lo.x && origin.y >= lo.y &&
              origin.x + side <= hi.x && origin.y + side <= hi.y) {
            CHECK(overlap);
            ++cellsContainingRange;
          }
        }
      }
    }
    CHECK(cellsContainingRange > 0);
  }

  TEST_CASE("cellsOverlap is empty for an empty range") {
    // A zero-width window is what a degenerate camera position would produce.
    // Walking it would otherwise loop over cells and hand out phantom chunks.
    const glm::ivec2 at{4, 4};
    CHECK_FALSE(
        Tree::cellsOverlap(at, at, 0, glm::ivec2(4, 4)));
    CHECK(Tree::cellsOverlap(at, glm::ivec2(5, 5), 0, glm::ivec2(4, 4)));
  }
}
