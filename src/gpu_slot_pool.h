#pragma once

// Free-list of GPU buffer slots for the batched heightmap compute path.
//
// A slot may be handed out only while it is free, and it becomes free again
// only once the chunk that owns it has been read back. This matters because
// the readback is deferred by at least one frame: the compute dispatch happens
// in the spawn loop, while the readback happens in the promote loop of a
// *later* render() call.
//
// The code this replaced handed out slots unconditionally
// (`m_NextGpuSlot++ % kGpuSlots`). A cold start dispatches up to
// kMaxPendingTasks chunks in a single frame, so with 64 slots every slot was
// overwritten 16 times before the first readback could run, and 100% of
// readbacks returned whichever chunk had been dispatched last.
//
// Deliberately free of OpenGL and of any ChunkManager state, so the policy can
// be tested directly by the unit suite -- which has no GL context.

#include <cstdint>
#include <optional>
#include <vector>

class GpuSlotPool {
 public:
  explicit GpuSlotPool(uint32_t slotCount) : m_SlotCount(slotCount) {
    m_FreeSlots.reserve(slotCount);
    // Popped from the back, so fill in reverse to hand out ascending slots
    // first; that only affects which chunk gets which slot, not correctness.
    for (uint32_t i = slotCount; i > 0; --i) {
      m_FreeSlots.push_back(i - 1);
    }
  }

  // Take a free slot, or nothing if every slot is in flight. Returning
  // nothing is the normal, expected outcome under load, not an error: the
  // caller falls back to the CPU path for that position.
  [[nodiscard]] std::optional<uint32_t> acquire() {
    if (m_FreeSlots.empty()) {
      return std::nullopt;
    }
    const uint32_t slot = m_FreeSlots.back();
    m_FreeSlots.pop_back();
    return slot;
  }

  // Return a slot. Releasing a slot that is already free is ignored rather
  // than duplicating it: a double release would otherwise hand the same slot
  // to two chunks, which is precisely the corruption this pool exists to
  // prevent.
  void release(uint32_t slot) {
    for (const uint32_t free : m_FreeSlots) {
      if (free == slot) {
        return;
      }
    }
    if (slot < m_SlotCount) {
      m_FreeSlots.push_back(slot);
    }
  }

  [[nodiscard]] std::size_t freeCount() const { return m_FreeSlots.size(); }
  [[nodiscard]] std::size_t slotCount() const { return m_SlotCount; }
  [[nodiscard]] std::size_t inFlightCount() const {
    return m_SlotCount - m_FreeSlots.size();
  }

  // True if `slot` is currently available for dispatch.
  [[nodiscard]] bool isFree(uint32_t slot) const {
    for (const uint32_t free : m_FreeSlots) {
      if (free == slot) {
        return true;
      }
    }
    return false;
  }

 private:
  // m_SlotCount is declared first and initialised in the member-init list
  // because the constructor body reads it. Reversing these two silently
  // yields an uninitialised count.
  std::size_t m_SlotCount;
  std::vector<uint32_t> m_FreeSlots;
};
