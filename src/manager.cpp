#include "manager.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <print>

#include <glm/glm.hpp>
#include "chunk.h"
#include "chunk_coords.h"
#include "config.h"
#include "frustum.h"
#include "renderer/renderer.hpp"
#include "shader.h"

namespace {

constexpr float kChunkBlockExtent =
    static_cast<float>(Constants::Chunk::LENGTH);
// The chunk *centre* is at pos*L + L/2, because chunk meshes are drawn with
// vertices in [pos*L, pos*L+L] (see ChunkManager::render's model translation
// and chunk.cpp's local block coords).
//
// That half-chunk bias belongs HERE and only here, because only here is a
// centre what is wanted -- the distance is compared against
// RENDER_DISTANCE_BLOCKS. It is not what a containment test wants: a block
// coordinate resolves to floor(w / L), with no bias at all, or the back half of
// every chunk resolves to the *next* chunk (#132). ChunkGrid::chunkIndexFor
// states that, and both call sites now go through it.
constexpr float kChunkCenterOffset = kChunkBlockExtent * 0.5f;


}  // namespace


ChunkManager::ChunkManager(IRenderer* renderer)
    : m_Renderer(renderer), m_ComputeShader(renderer) {}

float ChunkManager::getChunkDistanceSquared(const glm::ivec2 &chunkPos,
                                            const glm::vec3 &cameraPos) {
  const float chunkCenterX = chunkPos.s * kChunkBlockExtent + kChunkCenterOffset;
  const float chunkCenterZ = chunkPos.t * kChunkBlockExtent + kChunkCenterOffset;
  const float dx = chunkCenterX - cameraPos.x;
  const float dz = chunkCenterZ - cameraPos.z;
  return dx * dx + dz * dz;
}

void ChunkManager::load() {
  if (Constants::Noise::USE_GPU) {
    try {
      m_ComputeShader.loadCompute(Constants::TERRAIN_COMPUTE_PATH);
      // Register exactly the uniforms terrain.comp declares. The list lives in
      // config.h so the test suite can check it against the GLSL source;
      // registering by hand is what let uSlot go missing.
      for (const char *uniform : Constants::TERRAIN_COMPUTE_UNIFORMS) {
        m_ComputeShader.newUniform(uniform);
      }

      size_t kExtSide = Chunk::kExtSide;
      // One slot per in-flight chunk so multiple compute dispatches can be
      // batched before a single deferred readback pass.
      size_t ssboSize = static_cast<size_t>(kGpuSlots) * kExtSide * kExtSide *
                        sizeof(uint32_t);
      m_HeightMapSSBO = m_Renderer->createBuffer(BufferType::Storage);
      m_Renderer->setBufferData(*m_HeightMapSSBO, nullptr, ssboSize, BufferUsage::Dynamic);
      // Every slot starts free. They are returned only after their chunk's
      // readback completes, so a slot is never handed out twice while an
      // earlier dispatch into it is still unread.
      m_GpuSlots = GpuSlotPool{kGpuSlots};
    } catch (const std::exception& e) {
      std::println("Failed to load terrain compute shader, falling back to CPU noise: {}", e.what());
    }
  }
}

void ChunkManager::shutdown() {
  // Discard the backlog first, so the workers stop picking up new meshing work
  // before the chunks they are meshing into start being released.
  m_ThreadPool.requestShutdown(/*discardPending=*/true);

  // Release every chunk's GL objects now, while the context is still current.
  // Left to member destruction this happens after ~OpenGLRenderer has
  // terminated windowing (#128), so this is the ordering fix and the discard
  // is the responsiveness fix -- both matter here.
  for (auto &entry : m_ProcessingChunks) {
    entry.second.chunk.cleanup();
  }
  m_ProcessingChunks.clear();
  m_ProcessingPositions.clear();

  for (auto &entry : m_ProcessedChunks) {
    entry.second.cleanup();
  }
  m_ProcessedChunks.clear();

  if (m_HeightMapSSBO) {
    m_HeightMapSSBO.reset();
  }
}

void ChunkManager::render(const Camera *camera, Shader &shader) {
  const glm::vec3 cameraPosition = camera->m_Position;

  const float renderDistBlocks =
      static_cast<float>(Constants::Chunk::RENDER_DISTANCE_BLOCKS);
  const float renderDistSq = renderDistBlocks * renderDistBlocks;

  for (auto it = m_ProcessedChunks.begin(); it != m_ProcessedChunks.end();) {
    const glm::ivec2 &position = it->first;

    if (getChunkDistanceSquared(position, cameraPosition) > renderDistSq) {
      it->second.cleanup();
      it = m_ProcessedChunks.erase(it);
      continue;
    }

    ++it;
  }

  for (auto it = m_ProcessingChunks.begin(); it != m_ProcessingChunks.end();) {
    TaskResult &result = it->second;

    // GPU path: the meshing task only flags meshReady. Before promoting,
    // finish the deferred heightmap readback for this chunk's SSBO slot.
    // By now the compute dispatch is at least a frame old, so the GPU has
    // almost always finished and the readback doesn't stall the pipeline.
    if (Constants::Noise::USE_GPU && m_HeightMapSSBO &&
        m_ComputeShader.getId() != 0) {
      if (!result.meshReady.load(std::memory_order_acquire)) {
        ++it;
        continue;
      }
      if (!result.chunk.isGpuHeightMapReady()) {
        m_ComputeShader.bindBufferBase(*m_HeightMapSSBO, 0);
        result.chunk.finishHeightMapGPU(result.slot, *m_HeightMapSSBO);
        result.chunk.generateMesh();
        // The slot's contents have now been consumed, so it can serve another
        // dispatch. Releasing it here rather than at dispatch time is what
        // makes the slot's lifetime exactly one readback (#126).
        if (result.slot != TaskResult::kNoGpuSlot) {
          m_GpuSlots.release(result.slot);
          result.slot = TaskResult::kNoGpuSlot;
        }
        // The worker only flags meshReady on this path; the mesh itself is
        // built here, so this is the point where the chunk is ready to upload.
        // Without it the chunk would be re-meshed every frame and never
        // promoted out of m_ProcessingChunks.
        result.uploadReady.store(true, std::memory_order_release);
      }
    }

    const glm::ivec2 position = it->first;

    // Reap a chunk whose meshing threw. This has to come *before* the
    // uploadReady check, because a task that threw never sets uploadReady --
    // so without this the entry sits in m_ProcessingChunks forever, its
    // position is never released, and the chunk is never retried (#141).
    // Dropping it is the right outcome: one bad chunk instead of a leak that
    // grows until the render window is full of entries that can never resolve.
    if (result.failed.load(std::memory_order_acquire)) {
      result.chunk.cleanup();
      // A GPU-path chunk may still hold an SSBO slot: the dispatch was
      // issued, but the readback will never run for it.
      if (result.slot != TaskResult::kNoGpuSlot) {
        m_GpuSlots.release(result.slot);
        result.slot = TaskResult::kNoGpuSlot;
      }
      {
        std::lock_guard<std::mutex> lock(m_ProcessingMutex);
        m_ProcessingPositions.erase(position);
        it = m_ProcessingChunks.erase(it);
      }
      continue;
    }

    if (!result.uploadReady.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }

    // Range-check before uploading: if the camera has moved away since this
    // task was enqueued, discard the chunk here instead of paying the GPU
    // upload cost. This is only safe now that uploadReady is set — it is the
    // worker's final action, so no task can still be touching `result`.
    // Erasing earlier would leave the worker meshing into freed memory.
    if (getChunkDistanceSquared(position, cameraPosition) > renderDistSq) {
      result.chunk.cleanup();
      // The chunk is being dropped without ever being read back, so its slot
      // would otherwise stay marked in-flight forever. After enough camera
      // movement every slot leaks and the GPU path silently stops being used
      // at all.
      if (result.slot != TaskResult::kNoGpuSlot) {
        m_GpuSlots.release(result.slot);
        result.slot = TaskResult::kNoGpuSlot;
      }
      {
        std::lock_guard<std::mutex> lock(m_ProcessingMutex);
        m_ProcessingPositions.erase(position);
        it = m_ProcessingChunks.erase(it);
      }
      continue;
    }

    result.chunk.pass();
    Chunk promoted = std::move(result.chunk);

    {
      std::lock_guard<std::mutex> lock(m_ProcessingMutex);
      m_ProcessingPositions.erase(position);
      it = m_ProcessingChunks.erase(it);
    }

    m_ProcessedChunks.try_emplace(position, std::move(promoted));
  }

  // Streaming window origin. The chunk the camera is *inside*, which is an
  // index of containment and takes no half-chunk bias (#132).
  const int32_t currentChunkX = ChunkGrid::chunkIndexFor(cameraPosition.x);
  const int32_t currentChunkZ = ChunkGrid::chunkIndexFor(cameraPosition.z);

  for (int32_t chunkX = currentChunkX - Constants::Chunk::RENDER_DISTANCE_CHUNKS;
       chunkX <= currentChunkX + Constants::Chunk::RENDER_DISTANCE_CHUNKS;
       chunkX++) {
    for (int32_t chunkZ = currentChunkZ - Constants::Chunk::RENDER_DISTANCE_CHUNKS;
         chunkZ <= currentChunkZ + Constants::Chunk::RENDER_DISTANCE_CHUNKS;
         chunkZ++) {
      const glm::ivec2 position = {chunkX, chunkZ};

      if (getChunkDistanceSquared(position, cameraPosition) > renderDistSq) {
        continue;
      }

      TaskResult *resultPtr = nullptr;
      {
        std::lock_guard<std::mutex> lock(m_ProcessingMutex);
        if (m_ProcessedChunks.contains(position)) {
          continue;
        }
        if (m_ProcessingPositions.contains(position)) {
          continue;
        }
        m_ProcessingPositions.insert(position);
        auto [it, inserted] = m_ProcessingChunks.try_emplace(position, m_Renderer);
        resultPtr = &it->second;
      }

      TaskResult &result = *resultPtr;
      const bool enqueued = [&] {
        if (Constants::Noise::USE_GPU && m_HeightMapSSBO &&
            m_ComputeShader.getId() != 0) {
          // Batched GPU path: dispatch into a free slot of the shared SSBO
          // and defer both meshing and readback. The main thread never
          // blocks on a per-chunk GPU sync here; the readback happens
          // later, once the slot has been given a full frame to complete.
          //
          // A slot is only taken if one is free. The previous code used
          // `m_NextGpuSlot++ % kGpuSlots`, which handed out slots that were
          // still in flight: one frame dispatches up to kMaxPendingTasks
          // chunks, so all 64 slots were overwritten 16x before the first
          // readback, and every chunk was meshed from whichever chunk had
          // been dispatched last (#126). With the slots exhausted, fall
          // through to the CPU path below for this position rather than
          // corrupting the terrain.
          if (const auto slot = m_GpuSlots.acquire()) {
            result.slot = *slot;
            m_ComputeShader.bindBufferBase(*m_HeightMapSSBO, 0);
            result.chunk.generateHeightMapGPU(position, *slot, m_ComputeShader);
            return m_ThreadPool.tryEnqueue(
                [&result]() {
                  result.meshReady.store(true, std::memory_order_release);
                },
                kMaxPendingTasks);
          }
        }

        return m_ThreadPool.tryEnqueue(
            [&result, position]() {
              // The pool itself now guarantees an exception cannot escape a
              // worker, but only this task body knows *which* chunk failed --
              // without that the promotion loop would wait on uploadReady for
              // a chunk that is never coming, stranding its position forever
              // (#141).
              try {
                result.chunk.generateMeshData(position);
                result.uploadReady.store(true, std::memory_order_release);
              } catch (const std::exception &e) {
                std::println(stderr,
                             "ChunkManager: meshing ({}, {}) failed: {}",
                             position.x, position.y, e.what());
                result.failed.store(true, std::memory_order_release);
              } catch (...) {
                std::println(stderr,
                             "ChunkManager: meshing ({}, {}) failed",
                             position.x, position.y);
                result.failed.store(true, std::memory_order_release);
              }
            },
            kMaxPendingTasks);
      }();

      if (!enqueued) {
        // The pool is at its backlog cap. Drop the placeholder entry so the
        // chunk is retried on a later frame; leaving it in place would strand
        // the position in m_ProcessingPositions forever, since nothing will
        // ever flag the chunk ready. Safe to erase now precisely because no
        // task was queued and therefore none holds a reference to `result`.
        //
        // The GPU slot has already been taken and the compute dispatch has
        // already been issued, but nothing will ever read it back now, so it
        // has to be released here too. Without this the pool's backlog cap
        // leaks slots, and after 64 such frames the GPU path is disabled for
        // the rest of the session.
        if (result.slot != TaskResult::kNoGpuSlot) {
          m_GpuSlots.release(result.slot);
          result.slot = TaskResult::kNoGpuSlot;
        }
        std::lock_guard<std::mutex> lock(m_ProcessingMutex);
        m_ProcessingChunks.erase(position);
        m_ProcessingPositions.erase(position);
      }
    }
  }

  shader.use();
  Frustum frustum(camera);

  for (auto &value : m_ProcessedChunks) {
    const glm::ivec2 &position = value.first;

    if (!frustum.isChunkInside(position)) {
      continue;
    }

    Chunk &chunk = value.second;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(
        model,
        glm::vec3(static_cast<float>(position.s * Constants::Chunk::LENGTH),
                  0.0f,
                  static_cast<float>(position.t * Constants::Chunk::LENGTH)));

    // Terrain pass: scene shader draws both the terrain and the (blue)
    // water surface, which is folded into the same mesh.
    shader.use();
    shader.setUniformMat4("uModel", model);
    chunk.render();
  }
}

float ChunkManager::getPositionHighestY(const glm::vec3 &cameraPosition) {
  // Containment, so no bias: floor(w / L), which is the chunk that actually
  // contains world block w (#132).
  const int32_t chunkX = ChunkGrid::chunkIndexFor(cameraPosition.x);
  const int32_t chunkZ = ChunkGrid::chunkIndexFor(cameraPosition.z);

  const glm::ivec2 chunkPosition{chunkX, chunkZ};

  const int32_t worldX = static_cast<int32_t>(std::floor(cameraPosition.x));
  const int32_t worldZ = static_cast<int32_t>(std::floor(cameraPosition.z));

  const int32_t localX = worldX - (chunkX * Constants::Chunk::LENGTH);
  const int32_t localZ = worldZ - (chunkZ * Constants::Chunk::LENGTH);

  // In [0, L) by construction, so there is nothing to clamp. The clamp that
  // used to be here is what made the off-by-half-chunk bug invisible: it
  // turned a -6 into a 0, so the query quietly returned the neighbouring
  // chunk's first column -- up to 15 blocks away -- with nothing reporting
  // it. Asserted instead, so a regression is a loud failure rather than a
  // plausible-looking number (#132).
  assert(localX >= 0 && localX < Constants::Chunk::LENGTH);
  assert(localZ >= 0 && localZ < Constants::Chunk::LENGTH);

  if (m_ProcessedChunks.contains(chunkPosition)) {
    Chunk &chunk = m_ProcessedChunks.at(chunkPosition);
    return static_cast<float>(chunk.getHighestBlockY(
        static_cast<uint32_t>(localX), static_cast<uint32_t>(localZ)));
  }

  return static_cast<float>(Constants::Chunk::HEIGHT);
}
