#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "camera.h"
#include "chunk.h"
#include "gpu_slot_pool.h"
#include "shader.h"
#include "threadpool.h"

class IRenderer;

struct TaskResult {
  Chunk chunk;
  std::atomic<bool> meshReady{false};
  std::atomic<bool> uploadReady{false};
  // The SSBO slot this chunk's heightmap was dispatched into, or kNoGpuSlot
  // if it took the CPU path. Tracked so the slot is returned to the free list
  // exactly once -- on readback, and on the discard path too, where the chunk
  // is dropped before it is ever read back.
  static constexpr uint32_t kNoGpuSlot = 0xFFFFFFFFu;
  uint32_t slot = kNoGpuSlot;

  TaskResult() = delete;
  explicit TaskResult(IRenderer* renderer) : chunk(renderer) {}

  TaskResult(TaskResult &&other) noexcept
      : chunk(std::move(other.chunk)),
        meshReady(other.meshReady.load(std::memory_order_relaxed)),
        uploadReady(
            other.uploadReady.load(std::memory_order_relaxed)),
        slot(other.slot) {}

  TaskResult &operator=(TaskResult &&other) noexcept {
    if (this != &other) {
      chunk = std::move(other.chunk);
      meshReady.store(
          other.meshReady.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      uploadReady.store(
          other.uploadReady.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      slot = other.slot;
    }
    return *this;
  }

  TaskResult(const TaskResult &) = delete;
  TaskResult &operator=(const TaskResult &) = delete;
};

namespace std {

template <>
// Chunks are keyed on integer chunk coordinates (not float world
// coordinates) so hashing stays exact no matter how far from the origin
// the player walks.
struct hash<glm::ivec2> {
  std::size_t operator()(glm::ivec2 const &v) const noexcept {
    std::size_t h1 = hash<int>{}(v.x);
    std::size_t h2 = hash<int>{}(v.y);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }
};

} // namespace std

class ChunkManager {
public:
  explicit ChunkManager(IRenderer* renderer);
  void load();
  void loadComputeShader(const char* computePath);

  void render(const Camera *camera, Shader &shader);

  float getPositionHighestY(const glm::vec3 &cameraPosition);

private:
  static float getChunkDistanceSquared(const glm::ivec2 &chunkPos,
                                       const glm::vec3 &cameraPos);

  IRenderer* m_Renderer = nullptr;
  std::unique_ptr<IBuffer> m_HeightMapSSBO;
  Shader m_ComputeShader;

  // Batched GPU heightmap generation: each in-flight chunk gets a slot of
  // the shared SSBO; readback is deferred so the main thread never stalls
  // on a per-chunk pipeline sync.
  //
  // A slot may only be handed out while it is free, and it becomes free again
  // only once that chunk's readback has completed. The previous code used
  // `m_NextGpuSlot++ % kGpuSlots`, which recycles unconditionally: a cold
  // start dispatches up to kMaxPendingTasks chunks in one frame, so each of
  // the 64 slots was overwritten 16 times before the first readback could
  // run. Measured 100% of readbacks returned another chunk's heights (#126).
  static constexpr uint32_t kGpuSlots = 64;

  // Slots currently free for dispatch. A slot is handed out by acquire() and
  // returned by release() once the owning chunk has been read back. Held on
  // the main thread only -- render() is single-threaded, and the worker task
  // touches neither this pool nor any slot.
  GpuSlotPool m_GpuSlots{kGpuSlots};

  // Cap on how many meshing tasks may sit in the pool's queue at once. The
  // render window spans (2 * RENDER_DISTANCE_CHUNKS + 1)^2 == 4225 chunk
  // slots, so a cap well below that lets streaming fill in steadily instead
  // of flooding the pool with thousands of tasks on the first frame. Chunks
  // that miss the cap are retried on a later frame.
  static constexpr std::size_t kMaxPendingTasks = 1024;

  std::unordered_map<glm::ivec2, Chunk> m_ProcessedChunks;
  std::unordered_set<glm::ivec2> m_ProcessingPositions;

  std::unordered_map<glm::ivec2, TaskResult> m_ProcessingChunks;
  std::mutex m_ProcessingMutex;
  ThreadPool m_ThreadPool;
};
