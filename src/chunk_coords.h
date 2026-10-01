#pragma once

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "config.h"

// World-block <-> chunk-coordinate arithmetic.
//
// This lives apart from manager.cpp for the same reason src/frametime.h does:
// manager.h includes chunk.h, which includes <glad/glad.h> unconditionally, so
// nothing in the unit suite can reach a helper declared in manager.h even
// though the arithmetic itself is pure and needs no GL. The chunk *geometry*
// the helpers below encode is defined by the model translation in
// ChunkManager::render (vertices at [pos*L, pos*L+L]) and by Chunk's local
// block coordinates in chunk.cpp, so it is worth stating once, here, next to
// the arithmetic that depends on it.

namespace ChunkGrid {

/** The chunk containing a world block coordinate.
 *
 *  A chunk at index p occupies world blocks [p*L, (p+1)*L), so the index is a
 *  plain floor of the quotient with no bias. The mesh is centred at p*L + L/2,
 *  which is what getChunkDistanceSquared needs and what a containment test
 *  must NOT have: adding the half-chunk offset makes
 *
 *      floor((w + L/2) / L) == floor(w / L) + 1
 *
 *  for every w in the back half of its chunk, so the query resolves the next
 *  chunk and the derived local coordinate lands in [-L/2, -1] (#132). */
[[nodiscard]] inline std::int32_t chunkIndexFor(float worldCoordinate) {
  return static_cast<std::int32_t>(std::floor(
      worldCoordinate / static_cast<float>(Constants::Chunk::LENGTH)));
}

/** The block coordinate within its own chunk, i.e. the local x or z that
 *  indexes a chunk's L x L heightmap.
 *
 *  Floor the world coordinate to the block it names, then subtract the chunk
 *  origin. By construction of chunkIndexFor the result is in [0, L) for every
 *  input, negative coordinates included: block -1 is chunk -1, local L-1. */
[[nodiscard]] inline std::int32_t localBlockFor(float worldCoordinate) {
  return static_cast<std::int32_t>(std::floor(worldCoordinate)) -
         chunkIndexFor(worldCoordinate) * Constants::Chunk::LENGTH;
}

/** The chunk origin in world blocks: the coordinate local block 0 names. */
[[nodiscard]] inline std::int32_t chunkOriginFor(float worldCoordinate) {
  return chunkIndexFor(worldCoordinate) * Constants::Chunk::LENGTH;
}

}  // namespace ChunkGrid
