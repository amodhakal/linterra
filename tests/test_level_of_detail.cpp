// Level-of-detail selection: which tier a chunk belongs at, and how finely it is
// meshed.
//
// Pure policy, no GL and no Chunk, which is the whole reason Lod lives in its
// own header rather than inside manager.cpp: the tier a chunk gets is decided by
// a function of (distance, current tier) and nothing else, so every rule about
// it -- the boundaries, the hysteresis band, the sampling stride -- can be
// checked in a headless suite instead of by flying a camera and squinting.

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "doctest/doctest.h"
#include "level_of_detail.h"

namespace {

// Walk a distance from `from` to `to` in `stepBlocks` increments, applying the
// tier rule at each point. This is how a real player moves, and it is the only
// way to ask the question that matters: a tier is a function of two arguments,
// so "what tier is 300 blocks away" has no single answer.
std::vector<Lod::Tier> walkTiers(float from, float to, float stepBlocks) {
  std::vector<Lod::Tier> tiers;
  const int steps = static_cast<int>((to - from) / stepBlocks) + 1;
  Lod::Tier current = Lod::Tier::Near;
  for (int i = 0; i < steps; ++i) {
    const float distance = from + static_cast<float>(i) * stepBlocks;
    current = Lod::selectTier(distance, current);
    tiers.push_back(current);
  }
  return tiers;
}

int countTierChanges(const std::vector<Lod::Tier> &tiers) {
  int changes = 0;
  for (std::size_t i = 1; i < tiers.size(); ++i) {
    if (tiers[i] != tiers[i - 1]) {
      ++changes;
    }
  }
  return changes;
}

}  // namespace

TEST_SUITE("LOD tier boundaries") {
  TEST_CASE("the boundaries are the fog constants, not literals") {
    // A tier boundary in front of FOG_START would be a visible seam in clear
    // view; one behind FOG_END would be a tier nothing ever reaches. Pinning
    // the relationship means moving the fog moves the LOD with it.
    CHECK(Lod::kMidBlocks == doctest::Approx(Constants::Chunk::FOG_START));
    CHECK(Lod::kFarBlocks == doctest::Approx(Constants::Chunk::FOG_END));
    CHECK(Lod::kMidBlocks < Lod::kFarBlocks);
    CHECK(Lod::kFarBlocks ==
          doctest::Approx(static_cast<float>(
              Constants::Chunk::RENDER_DISTANCE_BLOCKS)));
  }

  TEST_CASE("the raw tier is a pure function of distance") {
    // Ignoring hysteresis, the rule is three intervals. Checked at the
    // boundaries themselves, because an off-by-one there puts a tier change
    // exactly where the fog is densest.
    CHECK(Lod::rawTierFor(0.0f) == Lod::Tier::Near);
    CHECK(Lod::rawTierFor(Lod::kMidBlocks - 0.5f) == Lod::Tier::Near);
    CHECK(Lod::rawTierFor(Lod::kMidBlocks) == Lod::Tier::Mid);
    CHECK(Lod::rawTierFor(Lod::kFarBlocks - 0.5f) == Lod::Tier::Mid);
    CHECK(Lod::rawTierFor(Lod::kFarBlocks) == Lod::Tier::Far);
    CHECK(Lod::rawTierFor(100000.0f) == Lod::Tier::Far);
  }

  TEST_CASE("the raw tier is monotone: further is never finer") {
    // If this fails, a chunk can be meshed at full detail while a nearer one is
    // coarse, which is not a LOD scheme at all.
    Lod::Tier previous = Lod::Tier::Near;
    for (float d = 0.0f; d <= 1200.0f; d += 1.0f) {
      const Lod::Tier current = Lod::rawTierFor(d);
      CAPTURE(d);
      CHECK(static_cast<std::uint8_t>(current) >=
            static_cast<std::uint8_t>(previous));
      previous = current;
    }
  }
}

TEST_SUITE("LOD hysteresis") {
  TEST_CASE("walking outward changes tier exactly twice") {
    // 0 to 1000 blocks at one block per step, the whole render distance and
    // well past it. Two boundaries, two changes -- and the tier it settles on is
    // the coarsest.
    const std::vector<Lod::Tier> tiers = walkTiers(0.0f, 1000.0f, 1.0f);
    CHECK(countTierChanges(tiers) == 2);
    CHECK(tiers.front() == Lod::Tier::Near);
    CHECK(tiers.back() == Lod::Tier::Far);
  }

  TEST_CASE("walking back changes tier exactly twice, and returns to Near") {
    // The same path in reverse. If the return trip cost more changes than the
    // outward one, the tier rule would be history-dependent beyond the stated
    // argument, which is the thing hysteresis exists to prevent.
    const std::vector<Lod::Tier> outward = walkTiers(0.0f, 1000.0f, 1.0f);
    std::vector<Lod::Tier> inward;
    Lod::Tier current = Lod::Tier::Far;
    for (int i = 1000; i >= 0; --i) {
      current = Lod::selectTier(static_cast<float>(i), current);
      inward.push_back(current);
    }
    CHECK(countTierChanges(inward) == 2);
    CHECK(inward.back() == Lod::Tier::Near);
    CHECK(static_cast<std::size_t>(inward.size()) == outward.size());
  }

  TEST_CASE("oscillating across a boundary never changes tier") {
    // THE reason this function takes the current tier. A player pacing back and
    // forth over a boundary, inside the band, must not re-mesh a chunk on every
    // frame: each transition is a full re-mesh, and each one is also visible.
    //
    // The band is one chunk wide (kHysteresisBlocks == LENGTH == 16), so this
    // walks the 16 blocks straddling the near boundary.
    const float boundary = Lod::kMidBlocks;
    const float span = Lod::kHysteresisBlocks;
    std::vector<Lod::Tier> tiers;
    Lod::Tier current = Lod::Tier::Near;
    for (int i = 0; i < 40; ++i) {
      // Alternate between just inside and just outside the band.
      const float offset = (i % 2 == 0) ? -0.25f : 0.25f;
      const float distance = boundary + offset;
      // Only the half of the oscillation that is inside the band can hold the
      // tier; the other half must not be able to push it over.
      if (distance > boundary + span) {
        continue;
      }
      current = Lod::selectTier(distance, current);
      tiers.push_back(current);
      CAPTURE(distance);
    }
    // Still inside the band on both sides, so nothing moved.
    CHECK(countTierChanges(tiers) == 0);
    CHECK(current == Lod::Tier::Near);
  }

  TEST_CASE("the band is one chunk wide on each side") {
    // Sized as LENGTH on purpose: a chunk cannot change tier while the player is
    // still standing on it, because the player cannot cross a chunk faster than
    // they can traverse one.
    CHECK(Lod::kHysteresisBlocks ==
          doctest::Approx(static_cast<float>(Constants::Chunk::LENGTH)));
  }

  TEST_CASE("a chunk inside the band keeps the tier it arrived with") {
    // Both directions, stated separately, because a symmetric band would leave
    // a chunk oscillating just outside a boundary stuck at whatever tier it
    // happened to start in -- the same thrash with a different period.
    const float boundary = Lod::kMidBlocks;

    // Outward, still short of the band: stays Near.
    CHECK(Lod::selectTier(boundary + 1.0f, Lod::Tier::Near) == Lod::Tier::Near);
    // Outward, past the band: coarsens.
    CHECK(Lod::selectTier(boundary + Lod::kHysteresisBlocks, Lod::Tier::Near) ==
          Lod::Tier::Mid);

    // Inward, still short of the band: stays Mid.
    CHECK(Lod::selectTier(boundary - 1.0f, Lod::Tier::Mid) == Lod::Tier::Mid);
    // Inward, past the band: refines.
    CHECK(Lod::selectTier(boundary - Lod::kHysteresisBlocks, Lod::Tier::Mid) ==
          Lod::Tier::Near);
  }

  TEST_CASE("selecting the tier a chunk is already at is a no-op") {
    // The common case: a chunk re-evaluated every frame must not drift.
    for (float d = 0.0f; d <= 1200.0f; d += 7.0f) {
      for (const Lod::Tier tier :
           {Lod::Tier::Near, Lod::Tier::Mid, Lod::Tier::Far}) {
        if (Lod::rawTierFor(d) == tier) {
          CAPTURE(d);
          CHECK(Lod::selectTier(d, tier) == tier);
        }
      }
    }
  }
}

TEST_SUITE("LOD sampling") {
  TEST_CASE("the stride divides the chunk exactly, at every tier") {
    // A partial lattice block at the chunk edge would need a second quad width.
    // A stride that did not divide LENGTH would silently produce a mesh that
    // does not reach the chunk border -- missing terrain, not a crash.
    for (const Lod::Tier tier :
         {Lod::Tier::Near, Lod::Tier::Mid, Lod::Tier::Far}) {
      const std::uint32_t step = Lod::sampleStep(tier);
      CAPTURE(step);
      CHECK(step >= 1);
      CHECK(static_cast<std::uint32_t>(Constants::Chunk::LENGTH) % step == 0);
    }
  }

  TEST_CASE("column count falls as the square of the stride") {
    // The saving, stated as the number the mesher's column loop actually
    // iterates. 16 x 16 = 256 at full detail, 64 at stride 2, 16 at stride 4.
    CHECK(Lod::sampleStep(Lod::Tier::Near) == 1);
    CHECK(Lod::sampleStep(Lod::Tier::Mid) == 2);
    CHECK(Lod::sampleStep(Lod::Tier::Far) == 4);

    CHECK(Lod::columnCount(Lod::Tier::Near) == 256);
    CHECK(Lod::columnCount(Lod::Tier::Mid) == 64);
    CHECK(Lod::columnCount(Lod::Tier::Far) == 16);
    // Each tier is 4x the coarser one: two steps, four times less work.
    CHECK(Lod::columnCount(Lod::Tier::Near) ==
          4 * Lod::columnCount(Lod::Tier::Mid));
    CHECK(Lod::columnCount(Lod::Tier::Mid) ==
          4 * Lod::columnCount(Lod::Tier::Far));
  }

  TEST_CASE("the tiers actually span a range of distances") {
    // A degenerate scheme -- every tier empty, or one tier covering everything --
    // would pass every test above. Stated as a measurement: how much of the
    // render distance each tier is responsible for.
    const float midSpan = Lod::kFarBlocks - Lod::kMidBlocks;
    const float farSpan = 2.0f * Lod::kFarBlocks - Lod::kFarBlocks;
    INFO("Near covers 0.." << Lod::kMidBlocks << " blocks, Mid covers "
                           << Lod::kMidBlocks << ".." << Lod::kFarBlocks
                           << " (" << midSpan << " blocks), Far covers "
                           << Lod::kFarBlocks << ".. and beyond ("
                           << farSpan << " blocks to the diagonal corner)");
    CHECK(Lod::kMidBlocks > 0.0f);
    CHECK(midSpan > 0.0f);
    CHECK(farSpan > 0.0f);
  }
}
