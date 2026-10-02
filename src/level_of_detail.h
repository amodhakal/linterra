#pragma once

// Level of detail: how finely a chunk is meshed, as a function of its distance
// from the camera.
//
// WHAT THIS IS, AND WHAT IT IS NOT
//
// Issue #6 asks for "LOD keyed to camera distance" along two axes. This file is
// the SELECTION half: given a distance and the tier currently in use, which tier
// should this chunk be at, and how finely should it be meshed. It is a pure
// function of (distance, current tier) with no Chunk, no renderer and no GL, so
// the whole policy is exercisable by the headless unit suite.
//
// It is not the geometry half. What a coarser tier actually emits -- the
// lattice sampling in Chunk::generateMesh -- lives in chunk.cpp, and the visible
// consequences of it (popping at a tier boundary, cracks between chunks meshed
// at different steps) are NOT verified by anything in this repository. See the
// coverage note at the bottom of Chunk::generateMesh.
//
// WHY INTRA-CHUNK SAMPLING AND NOT INTER-CHUNK MERGING
//
// The issue also proposes collapsing distant chunks to a coarser
// representation spanning several chunks. That halves the leaf count but needs
// a second mesh format and a second draw path, and -- the part that actually
// blocks it -- it leaves holes in the world wherever a coarse region abuts a
// fine one until the coarse representation exists. Sampling a step through the
// heightmap a chunk has already generated has neither problem: every chunk
// still produces a mesh, and the reduction is a stride in a loop that already
// exists.
//
// A consequence worth stating: because the LOD samples a heightmap that has
// already been materialised, it does NOT touch the GPU heightmap path. The
// shared SSBO is still kGpuSlots * kExtSide * kExtSide and uExtSide is still
// Chunk::kExtSide (src/manager.cpp, src/chunk.h). The issue lists making those
// LOD-dependent as an integration point; on this design that work is not
// needed, and the reason is this paragraph rather than an omission.
//
// HYSTERESIS, AND WHY IT IS HERE AT ALL
//
// A player walking back and forth across a tier boundary must not have a
// subtree thrash between two representations: every transition re-meshes a
// chunk, and a boundary that flips every frame is a stream of re-meshes that
// also shows up on screen. So a tier change requires the distance to clear the
// boundary by kHysteresisBlocks, and the band is applied in the direction of
// travel: coarsening is delayed, refining is early. The result is a pair of
// dead bands around every boundary, and a chunk that oscillates inside a band
// never changes tier at all.
//
// The consequence to be aware of when reading a test: selectLodTier is a
// function of TWO arguments, not one. Asking "what tier is 300 blocks away"
// has no single answer -- it depends on which way the chunk was travelling.
// That is the point, and it is why the tests below walk a distance rather than
// sampling it.

#include <cstdint>

#include "config.h"

namespace Lod {

// Tiers, ordered near to far. The enumerator values are the tier index, so
// comparing two tiers is comparing their distance ordering.
enum class Tier : std::uint8_t { Near = 0, Mid = 1, Far = 2 };

// Where the tiers change, in world blocks from the camera.
//
// The near tier ends at FOG_START (256 blocks), which is where the fog starts:
// a chunk that far out is already half-hidden, so dropping its detail is
// something the fog is doing anyway. The far tier ends at FOG_END (512), the
// render distance, so there is no tier boundary in clear view.
//
// These are stated in terms of the fog constants rather than as literals so
// that moving the fog moves the LOD with it. A boundary in front of FOG_START
// would be a visible seam; a boundary behind FOG_END would be a tier nothing
// ever reaches.
constexpr float kMidBlocks = Constants::Chunk::FOG_START;
constexpr float kFarBlocks = Constants::Chunk::FOG_END;

// How far past a boundary a chunk must travel before it changes tier.
//
// Sized as a chunk (LENGTH = 16 blocks) on purpose: one chunk of hysteresis is
// the smallest band that guarantees a chunk cannot change tier while the player
// is still standing on it, because the player cannot cross a chunk faster than
// they can traverse one.
constexpr float kHysteresisBlocks =
    static_cast<float>(Constants::Chunk::LENGTH);

/** The heightmap sampling stride for a tier.
 *
 *  A stride of s means the mesher keeps one column per s x s block of the
 *  heightmap, so a 16 x 16 chunk is meshed as (16/s)^2 columns: 256, 64, or 16.
 *  The stride divides LENGTH exactly, so no tier ever has a partial block at
 *  the chunk edge -- a partial block would need a different quad width at the
 *  border, which is two code paths for no gain. */
[[nodiscard]] constexpr std::uint32_t sampleStep(Tier tier) {
  switch (tier) {
  case Tier::Near:
    return 1;
  case Tier::Mid:
    return 2;
  case Tier::Far:
    return 4;
  }
  return 1;
}

/** The tier a given sampling stride belongs to.
 *
 *  The inverse of sampleStep, and needed because a resident chunk is asked the
 *  question in this direction: the LOD rule is a function of the tier a chunk
 *  is currently at, and what a chunk stores is its stride. Without the inverse
 *  the engine could only ask "what tier is this distance", which is the half of
 *  the rule that has no hysteresis in it. */
[[nodiscard]] constexpr Tier tierForStep(std::uint32_t sampleStep) {
  switch (sampleStep) {
  case 2:
    return Tier::Mid;
  case 4:
    return Tier::Far;
  default:
    return Tier::Near;
  }
}

/** Columns meshed per chunk at a tier, i.e. (LENGTH / step)^2.
 *
 *  The vertex work of a chunk's top surface, and so the memory it costs, falls
 *  as the square of the step. Asserted in the tests against the count of lattice
 *  nodes the mesher actually walks. */
[[nodiscard]] constexpr std::uint32_t columnCount(Tier tier) {
  const std::uint32_t side = static_cast<std::uint32_t>(
      Constants::Chunk::LENGTH) / sampleStep(tier);
  return side * side;
}

/** The tier a chunk at this distance belongs to, ignoring hysteresis.
 *
 *  Exposed because the hysteresis rule is stated in terms of it, and a test
 *  that cannot see the raw tier cannot check that the band is applied at the
 *  right boundaries rather than merely somewhere near them. */
[[nodiscard]] constexpr Tier rawTierFor(float distanceBlocks) {
  if (distanceBlocks < kMidBlocks) {
    return Tier::Near;
  }
  if (distanceBlocks < kFarBlocks) {
    return Tier::Mid;
  }
  return Tier::Far;
}

/** The stride to mesh a chunk at this distance, for a chunk with no current
 *  tier.
 *
 *  This is the raw rule with no hysteresis, and it is correct for exactly one
 *  caller: a chunk that is being requested for the first time, or one whose
 *  previous mesh has just been retired because its tier changed. A chunk that
 *  still HAS a tier must go through selectTier, or it changes tier on the frame
 *  the camera crosses the boundary -- which is the thrash the band exists to
 *  stop. */
[[nodiscard]] constexpr std::uint32_t
sampleTierForDistance(float distanceBlocks) {
  return sampleStep(rawTierFor(distanceBlocks));
}

/** The tier a chunk should be meshed at.
 *
 *  `current` is the tier the chunk is at now, which is what makes the answer
 *  deterministic under motion rather than a pure function of distance: a chunk
 *  at 260 blocks is Mid if it was already coarse, and still Near if it is
 *  walking outward and has not yet cleared the band.
 *
 *  The band is one-sided per direction. Coarsening waits for
 *  boundary + kHysteresisBlocks, refining happens at boundary -
 *  kHysteresisBlocks. Both are needed: a symmetric band would leave a chunk that
 *  oscillates just outside a boundary stuck at whatever tier it happened to
 *  start in, which is the same thrash with a different period. */
[[nodiscard]] constexpr Tier selectTier(float distanceBlocks, Tier current) {
  const Tier raw = rawTierFor(distanceBlocks);
  if (raw == current) {
    return current;
  }
  // raw is farther out than current: coarsen only past the boundary plus band.
  if (static_cast<std::uint8_t>(raw) > static_cast<std::uint8_t>(current)) {
    const float boundary = current == Tier::Near ? kMidBlocks : kFarBlocks;
    return distanceBlocks >= boundary + kHysteresisBlocks ? raw : current;
  }
  // raw is nearer than current: refine as soon as the band is behind us.
  const float boundary = current == Tier::Far ? kFarBlocks : kMidBlocks;
  return distanceBlocks <= boundary - kHysteresisBlocks ? raw : current;
}

}  // namespace Lod
