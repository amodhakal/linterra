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
#include "chunk_octree.h"
#include "gpu_slot_pool.h"
#include "shader.h"
#include "threadpool.h"

class IRenderer;

struct TaskResult {
  Chunk chunk;
  std::atomic<bool> meshReady{false};
  std::atomic<bool> uploadReady{false};
  // Set when a worker task threw. Without it the promotion loop waits on
  // uploadReady forever: the chunk's position stays in m_ProcessingPositions
  // and its TaskResult stays in m_ProcessingChunks, so the position is never
  // released and the chunk is never re-queued -- exactly the stranded-position
  // failure the enqueue-failure path goes to such lengths to avoid (#141).
  std::atomic<bool> failed{false};
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
        failed(other.failed.load(std::memory_order_relaxed)),
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
      failed.store(other.failed.load(std::memory_order_relaxed),
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

  // Stop the mesher pool, discarding whatever is still queued.
  //
  // Called from ~Application before any teardown, because the pool drains
  // otherwise: every queued task is a full 16 x 256 chunk mesh and the backlog
  // reaches 1024, so pressing Escape leaves the process at 100% CPU with no
  // window for as long as it takes to mesh the remainder -- potentially
  // seconds, for chunks nothing will ever upload or draw (#157).
  //
  // Also releases the chunks themselves while the GL context is still alive,
  // rather than leaving that to member destruction after the context is gone.
  void shutdown();

  // The height of the surface under `cameraPosition`, or false if there is
  // none to report.
  //
  // Absence has to be representable. The function used to answer
  // Constants::Chunk::HEIGHT (256) for a chunk that had not streamed in yet,
  // which is a value that is always above the player, so Player::update read
  // it as an authoritative floor and snapped the player *up* to 258 --
  // permanently, since nothing above the world can ever be fallen onto
  // (#133). A bool plus an out-parameter makes that impossible to ignore at
  // the call site: outHeight is written only on a true return, and callers
  // that ignore the bool are ignoring it visibly.
  [[nodiscard]] bool tryGetGroundHeight(const glm::vec3 &cameraPosition,
                                        float &outHeight) const;

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

  // Chunk storage, as a sparse quadtree keyed on chunk coordinates rather than
  // a flat hash map (#6). The map had no spatial structure, so the per-frame
  // spawn scan could only answer "which chunks are near the camera" by
  // visiting every one of the (2 * RENDER_DISTANCE_CHUNKS + 1)^2 = 4225 grid
  // positions, every frame. The tree answers it with a descent that skips a
  // cell whose chunks are all resident, or reports a cell with no chunks in it
  // as a single fact.
  //
  // The interface deliberately matches the unordered_map it replaced --
  // contains/find/emplace/erase, forEachValue in place of iteration -- so this
  // is a change of container rather than a rewrite of every loop above. See
  // src/chunk_octree.h for the full mapping.
  ChunkOctree<Chunk> m_ProcessedChunks;

  // Positions with a meshing task in flight. Still a flat set, and deliberately
  // so: it is a membership guard, not a spatial index, and the octree stores the
  // task itself. Consolidating these two into one structure is #52 and is
  // tracked there, not here.
  std::unordered_set<glm::ivec2> m_ProcessingPositions;

  // In-flight work, as a tree for the same reason as m_ProcessedChunks: it is
  // walked in full each frame by the promotion pass and pruned by the discard
  // paths, and it needs the same stable-address guarantee for TaskResult, which
  // a worker thread holds a reference to across frames.
  ChunkOctree<TaskResult> m_ProcessingChunks;
  std::mutex m_ProcessingMutex;
  ThreadPool m_ThreadPool;
};
