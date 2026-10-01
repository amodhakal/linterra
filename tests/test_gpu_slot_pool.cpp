// Regression tests for #126: SSBO slots were recycled while their previous
// occupant was still awaiting deferred readback.
//
// The bug was `m_NextGpuSlot++ % kGpuSlots`, which hands out a slot
// unconditionally. Readback is deferred by at least one frame (the worker
// task only sets `meshReady`; the readback happens in the promote loop of a
// later render() call), and a cold-start frame dispatches up to
// kMaxPendingTasks chunks. With 64 slots and a 1024-task backlog cap, every
// slot was overwritten 16 times before the first readback could run.
//
// The property under test is the one that actually matters: a slot must never
// be handed to a second chunk while the first is still waiting to read it.
// GpuSlotPool is deliberately free of GL and of ChunkManager state so this can
// be checked directly in the GL-free unit suite.

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "config.h"
#include "doctest/doctest.h"
#include "gpu_slot_pool.h"

namespace {

// Same value as ChunkManager::kGpuSlots.
constexpr uint32_t kGpuSlots = 64;

}  // namespace

TEST_SUITE("GpuSlotPool") {
  TEST_CASE("hands out each slot at most once before it is released") {
    GpuSlotPool pool{kGpuSlots};
    std::set<uint32_t> seen;
    for (uint32_t i = 0; i < kGpuSlots; ++i) {
      const auto slot = pool.acquire();
      REQUIRE(slot.has_value());
      CAPTURE(*slot);
      CHECK(seen.insert(*slot).second);
    }
    CHECK(pool.inFlightCount() == kGpuSlots);
  }

  TEST_CASE("exhaustion is reported, not silently aliased") {
    // The whole defect in one assertion: the old code returned a valid-looking
    // slot when none was free. This must return nothing instead.
    GpuSlotPool pool{4};
    for (uint32_t i = 0; i < 4; ++i) {
      REQUIRE(pool.acquire().has_value());
    }
    CHECK_FALSE(pool.acquire().has_value());
  }

  TEST_CASE("a released slot becomes available again") {
    GpuSlotPool pool{2};
    const auto first = pool.acquire();
    const auto second = pool.acquire();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(pool.inFlightCount() == 2);

    pool.release(*first);
    CHECK(pool.freeCount() == 1);
    CHECK(pool.isFree(*first));
    CHECK_FALSE(pool.isFree(*second));

    const auto third = pool.acquire();
    REQUIRE(third.has_value());
    CHECK(*third == *first);
  }

  TEST_CASE("a double release cannot duplicate a slot") {
    // A duplicate entry would let two chunks be handed the same slot, which is
    // exactly the corruption this pool exists to prevent.
    GpuSlotPool pool{3};
    const auto slot = pool.acquire();
    REQUIRE(slot.has_value());
    CHECK(pool.freeCount() == 2);
    CHECK(pool.inFlightCount() == 1);

    pool.release(*slot);
    pool.release(*slot);
    pool.release(*slot);
    // All three are free again -- releasing three times must not leave four
    // entries in a three-slot pool.
    CHECK(pool.freeCount() == 3);
    CHECK(pool.inFlightCount() == 0);
  }

  TEST_CASE("out-of-range releases are ignored") {
    GpuSlotPool pool{2};
    pool.release(9999);
    CHECK(pool.freeCount() == 2);
  }

  TEST_CASE("every in-flight chunk reads back its own data") {
    // End-to-end simulation of ChunkManager::render's real ordering:
    // promote loop (readback) runs before the spawn loop (dispatch) in a
    // frame, so a dispatch on frame N is read on frame N+1 at the earliest.
    //
    // Runs the real constants: a 65x65 = 4225 position chunk window and a
    // 1024-task per-frame backlog cap against 64 slots.
    constexpr int kRadius = Constants::Chunk::RENDER_DISTANCE_CHUNKS;
    constexpr std::size_t kBacklogCap = 1024;
    const int kWindow = (2 * kRadius + 1) * (2 * kRadius + 1);

    GpuSlotPool pool{kGpuSlots};
    // Which slot currently holds each chunk's heightmap, and which chunk wrote
    // it. A readback is corrupt if the slot no longer holds this chunk.
    std::map<std::pair<int, int>, uint32_t> pending;
    std::map<uint32_t, std::pair<int, int>> slotOwner;

    long long dispatches = 0;
    long long readbacks = 0;
    long long corrupted = 0;

    for (int frame = 0; frame < 60; ++frame) {
      // --- promote loop: read back everything the pool flagged ------------
      std::vector<std::pair<int, int>> ready;
      ready.reserve(pending.size());
      for (const auto &[pos, slot] : pending) {
        (void)slot;
        ready.push_back(pos);
      }
      for (const auto &pos : ready) {
        const uint32_t slot = pending.at(pos);
        ++readbacks;
        const auto owner = slotOwner.find(slot);
        if (owner == slotOwner.end() || owner->second != pos) {
          ++corrupted;
        }
        slotOwner.erase(slot);
        pending.erase(pos);
        pool.release(slot);
      }

      // --- spawn loop: dispatch, backlog-capped ---------------------------
      std::size_t dispatchedThisFrame = 0;
      for (int cx = -kRadius; cx <= kRadius; ++cx) {
        for (int cz = -kRadius; cz <= kRadius; ++cz) {
          const std::pair<int, int> pos{cx, cz};
          if (pending.count(pos)) {
            continue;
          }
          if (dispatchedThisFrame >= kBacklogCap) {
            continue;  // retried on a later frame
          }
          const auto slot = pool.acquire();
          if (!slot) {
            // All 64 in flight: the CPU path takes over for this position.
            // That is the intended fallback, not a failure.
            continue;
          }
          slotOwner[*slot] = pos;
          pending[pos] = *slot;
          ++dispatches;
          ++dispatchedThisFrame;
        }
      }
    }

    CHECK(dispatches > 0);
    CHECK(readbacks > 0);
    // The regression: this was 60416 / 60416, i.e. every single readback.
    CHECK_MESSAGE(corrupted == 0,
                  "an SSBO slot was handed to a second chunk before the first "
                  "had read it back");
    CHECK(kWindow > static_cast<int>(kGpuSlots));
  }

  TEST_CASE("the old modulo scheme fails this simulation") {
    // Pins the measurement in the commit message, so the numbers cannot rot:
    // `next++ % kGpuSlots` returns a valid-looking slot even when every slot is
    // still in flight, so a readback returns whichever chunk most recently
    // wrote that slot.
    //
    // A cold-start frame dispatches the whole 4225-position window against 64
    // slots. Only the final kGpuSlots chunks still own their slots when the
    // next frame reads them back; the other 4161 read a slot someone else has
    // since overwritten.
    constexpr int kWindow = 4225;
    constexpr uint32_t kSlots = 64;

    std::map<uint32_t, int> slotOwner;
    std::map<int, uint32_t> pending;
    uint32_t next = 0;
    long long corrupted = 0;
    long long readbacks = 0;

    // One frame: 4225 positions dispatched, 64 slots.
    for (int i = 0; i < kWindow; ++i) {
      const uint32_t slot = next++ % kSlots;
      slotOwner[slot] = i;
      pending[i] = slot;
    }
    // Next frame's promote loop reads every one of them back.
    for (const auto &[chunk, slot] : pending) {
      ++readbacks;
      if (slotOwner[slot] != chunk) {
        ++corrupted;
      }
    }

    CHECK(readbacks == kWindow);
    // Only the last 64 dispatches are still intact.
    CHECK(corrupted == kWindow - kSlots);
    CHECK_MESSAGE(corrupted * 1000 / kWindow > 980,
                  "this is the pre-fix behaviour the pool test above rules "
                  "out: >98% of readbacks returned another chunk's heights");
  }
}
