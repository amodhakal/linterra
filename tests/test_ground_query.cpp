#include <cmath>

#include "doctest/doctest.h"

#include "chunk_coords.h"
#include "config.h"

// Ground-query arithmetic: which chunk a world coordinate resolves to, and
// which block within that chunk.
//
// These helpers live in src/chunk_coords.h rather than in manager.h because
// manager.h includes chunk.h, which includes <glad/glad.h> unconditionally --
// so the unit suite cannot include it even though the arithmetic is pure. That
// keeps the cheapest real test in this milestone runnable: no GL, no
// ChunkManager, and therefore no renderer to construct.

namespace {

// The containment property, stated once. A chunk at index p owns world blocks
// [p*L, (p+1)*L), half-open at the top so the intervals tile the line.
bool contains(int worldBlock, int chunkIndex) {
  const int origin = chunkIndex * Constants::Chunk::LENGTH;
  return origin <= worldBlock && worldBlock < origin + Constants::Chunk::LENGTH;
}

}  // namespace

TEST_SUITE("ChunkGrid containment") {
  TEST_CASE("the chunk index contains the block it was derived from") {
    // The core invariant, over the range the issue names. For every world
    // coordinate the index the manager computes must satisfy
    // floor(w / L) * L <= w < (floor(w / L) + 1) * L.
    for (int worldX = 0; worldX < 1024; ++worldX) {
      const int index = ChunkGrid::chunkIndexFor(static_cast<float>(worldX));
      CAPTURE(worldX);
      CAPTURE(index);
      CHECK(contains(worldX, index));
    }
  }

  TEST_CASE("the back half of every chunk resolves to its own chunk") {
    // The specific failure. The old code computed floor((w + L/2) / L), which
    // agrees with floor(w / L) only for w mod L in [0, L/2). Worked through for
    // w = 10 with L = 16:
    //
    //   old: floor((10 + 8) / 16) = floor(1.125) = 1
    //   new: floor(10 / 16)       = 0
    //   localX = worldX - chunkX * L = 10 - 16 = -6
    //
    // and the std::clamp that used to follow pinned that -6 to 0, so the
    // query returned chunk 1's local x = 0 column -- world block x = 16,
    // six blocks from where the player was standing.
    constexpr int L = Constants::Chunk::LENGTH;
    for (int chunk = 0; chunk < 64; ++chunk) {
      for (int local = 0; local < L; ++local) {
        const int world = chunk * L + local;
        const int index = ChunkGrid::chunkIndexFor(static_cast<float>(world));
        CAPTURE(world);
        CHECK(index == chunk);
        CHECK(ChunkGrid::localBlockFor(static_cast<float>(world)) == local);
      }
    }
  }

  TEST_CASE("the back-half bias would have broken exactly half of them") {
    // Stated as a measurement rather than asserted, so the size of the defect
    // is visible in the log. With L = 16, the biased index disagrees for
    // local in [8, 16), which is 8 of every 16 blocks -- half the world.
    constexpr int L = Constants::Chunk::LENGTH;
    int disagreements = 0;
    int total = 0;
    for (int world = 0; world < 1024; ++world) {
      const int correct = ChunkGrid::chunkIndexFor(static_cast<float>(world));
      const int biased = static_cast<int>(
          std::floor((static_cast<float>(world) + L / 2.0f) / L));
      ++total;
      if (correct != biased) {
        ++disagreements;
      }
    }
    INFO("the half-chunk bias disagreed on " << disagreements << " of " << total
                                            << " world coordinates");
    CHECK(disagreements == total / 2);
  }

  TEST_CASE("the local block coordinate is always in range") {
    // What the removed std::clamp was papering over. If this ever fails, the
    // containment above is broken and the clamp would have hidden it.
    for (int worldX = -2048; worldX < 2048; ++worldX) {
      const int local = ChunkGrid::localBlockFor(static_cast<float>(worldX));
      CAPTURE(worldX);
      CAPTURE(local);
      CHECK(local >= 0);
      CHECK(local < Constants::Chunk::LENGTH);
    }
  }

  TEST_CASE("fractional coordinates name the block they sit on") {
    // The camera is a float position; the heightmap is indexed by block. A
    // camera at 10.75 is standing on block 10, local 10 of chunk 0 -- not
    // block 11, and not chunk 1.
    CHECK(ChunkGrid::chunkIndexFor(10.75f) == 0);
    CHECK(ChunkGrid::localBlockFor(10.75f) == 10);

    // Exactly on a seam: 16.0 is block 16, which belongs to chunk 1 with
    // local 0. Truncating instead of flooring would put it in chunk 0 local
    // 16, which is out of range for a L x L heightmap.
    CHECK(ChunkGrid::chunkIndexFor(16.0f) == 1);
    CHECK(ChunkGrid::localBlockFor(16.0f) == 0);

    // ...and just below the seam, 15.999, is still chunk 0 local 15.
    CHECK(ChunkGrid::chunkIndexFor(15.999f) == 0);
    CHECK(ChunkGrid::localBlockFor(15.999f) == 15);

    // The negative side has to floor rather than truncate too: block -1 is in
    // chunk -1 as its last column, not in chunk 0 as local -1.
    CHECK(ChunkGrid::chunkIndexFor(-0.5f) == -1);
    CHECK(ChunkGrid::localBlockFor(-0.5f) == Constants::Chunk::LENGTH - 1);
    CHECK(ChunkGrid::chunkIndexFor(-16.0f) == -1);
    CHECK(ChunkGrid::localBlockFor(-16.0f) == 0);
    CHECK(ChunkGrid::chunkIndexFor(-17.0f) == -2);
    CHECK(ChunkGrid::localBlockFor(-17.0f) == Constants::Chunk::LENGTH - 1);
  }

  TEST_CASE("the chunk origin agrees with the index and the local block") {
    for (int worldX = -512; worldX < 512; ++worldX) {
      const auto world = static_cast<float>(worldX);
      const int origin = ChunkGrid::chunkOriginFor(world);
      CAPTURE(worldX);
      CHECK(ChunkGrid::localBlockFor(world) == worldX - origin);
      CHECK(ChunkGrid::chunkIndexFor(static_cast<float>(origin)) ==
            ChunkGrid::chunkIndexFor(world));
    }
  }

  TEST_CASE("a chunk index maps back to exactly one block in the chunk") {
    // The two-step round trip the ground query actually performs: world
    // coordinate -> chunk index -> local block, and the local block must be
    // the one the world coordinate names.
    for (int worldX = 0; worldX < 1024; ++worldX) {
      const auto world = static_cast<float>(worldX);
      const int index = ChunkGrid::chunkIndexFor(world);
      const int local = ChunkGrid::localBlockFor(world);
      const int reconstructed = ChunkGrid::chunkOriginFor(world) + local;
      CAPTURE(worldX);
      CHECK(reconstructed == worldX);
      CHECK(index * Constants::Chunk::LENGTH + local == worldX);
    }
  }
}
