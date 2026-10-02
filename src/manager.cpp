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
#include "level_of_detail.h"
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


// Nearest squared distance from the camera to the world-block AABB of an octree
// cell, in blocks.
//
// The eviction sweep needs this to decide a cell in one test instead of one test
// per chunk in it. The cell is only safe to drop when even its NEAREST chunk is
// out of range, so the test is against the near corner of the cell's footprint
// in chunk-centre terms -- the same quantity getChunkDistanceSquared computes
// for a single chunk, generalised to a square of them.
float getCellDistanceSquared(const glm::ivec2 &cellOrigin, int level,
                             const glm::vec3 &cameraPos) {
  const float side = static_cast<float>(1 << level);
  const float minX = static_cast<float>(cellOrigin.x) * kChunkBlockExtent +
                     kChunkCenterOffset;
  const float minZ = static_cast<float>(cellOrigin.y) * kChunkBlockExtent +
                     kChunkCenterOffset;
  const float maxX = minX + (side - 1.0f) * kChunkBlockExtent;
  const float maxZ = minZ + (side - 1.0f) * kChunkBlockExtent;
  // Clamp the camera into the cell's footprint, then measure. Only the x/z axes
  // matter: distance has always been horizontal here, because a chunk is a
  // column and its height does not affect whether it is worth keeping.
  const float dx = std::max(minX - cameraPos.x, 0.0f) +
                   std::max(cameraPos.x - maxX, 0.0f);
  const float dz = std::max(minZ - cameraPos.z, 0.0f) +
                   std::max(cameraPos.z - maxZ, 0.0f);
  return dx * dx + dz * dz;
}

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
  m_ProcessingChunks.forEachValue(
      [](const glm::ivec2 &, TaskResult &result) { result.chunk.cleanup(); });
  m_ProcessingChunks.clear();
  m_ProcessingPositions.clear();

  m_ProcessedChunks.forEachValue(
      [](const glm::ivec2 &, Chunk &chunk) { chunk.cleanup(); });
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

  // Eviction, as a tree walk. The old loop tested every resident chunk's
  // distance on every frame; a cell whose nearest chunk is out of range is
  // dropped in one test, and a cell wholly inside the range is not descended
  // into at all. A cell that straddles the boundary -- which is most of the
  // interesting ones, since the boundary is a circle -- is descended into, so
  // the outcome is identical to the per-chunk test it replaces.
  //
  // cleanup() runs per evicted chunk, before the value is destroyed: the octree
  // knows nothing about VBO/VAO lifetimes and must not free one silently.
  m_ProcessedChunks.eraseSubtrees(
      [&](int level, const glm::ivec2 &origin) {
        return getCellDistanceSquared(origin, level, cameraPosition) >
               renderDistSq;
      },
      [](Chunk &chunk) { chunk.cleanup(); });

  // LOD management: retire a resident chunk whose tier has moved on, so the
  // spawn scan below re-requests it at the new stride.
  //
  // This is where the hysteresis is actually applied. Lod::selectTier needs the
  // tier the chunk is AT, which is why Chunk carries its stride; asking only
  // "what tier does this distance map to" would re-mesh a chunk on every frame
  // the camera sits on a tier boundary, and each of those is both a stream of
  // work and a visible pop.
  m_ProcessedChunks.forEachEntry([&](const glm::ivec2 &position,
                                      Chunk &chunk) -> bool {
    const float distance =
        std::sqrt(getChunkDistanceSquared(position, cameraPosition));
    const Lod::Tier current = Lod::tierForStep(chunk.lodStep());
    if (Lod::selectTier(distance, current) == current) {
      return true;
    }
    // Tier changed: drop the mesh and let the spawn scan queue a fresh one at
    // the new stride. cleanup() before the drop, while the GL objects are still
    // reachable.
    chunk.cleanup();
    return false;
  });

  // Promotion, as a traversal that is told whether to keep each entry. The old
  // loop promoted or dropped entries while iterating a flat map and reassigning
  // its iterator; a callback states the same thing without an iterator that
  // erase() could invalidate mid-walk, and without the tree having to keep a
  // begin()/end() pair stable across a mutation.
  //
  // Returning false erases the entry. The octree defers the free until the walk
  // has finished, so nothing it is standing on is released underneath it.
  m_ProcessingChunks.forEachEntry([&](const glm::ivec2 &position,
                                      TaskResult &result) -> bool {
    // GPU path: the meshing task only flags meshReady. Before promoting,
    // finish the deferred heightmap readback for this chunk's SSBO slot.
    // By now the compute dispatch is at least a frame old, so the GPU has
    // almost always finished and the readback doesn't stall the pipeline.
    if (Constants::Noise::USE_GPU && m_HeightMapSSBO &&
        m_ComputeShader.getId() != 0) {
      if (!result.meshReady.load(std::memory_order_acquire)) {
        return true;
      }
      if (!result.chunk.isGpuHeightMapReady()) {
        m_ComputeShader.bindBufferBase(*m_HeightMapSSBO, 0);
        result.chunk.finishHeightMapGPU(result.slot, *m_HeightMapSSBO);
        result.chunk.generateMesh(result.lodStep);
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
      std::lock_guard<std::mutex> lock(m_ProcessingMutex);
      m_ProcessingPositions.erase(position);
      return false;
    }

    if (!result.uploadReady.load(std::memory_order_acquire)) {
      return true;
    }

    // Range-check before uploading: if the camera has moved away since this
    // task was enqueued, discard the chunk here instead of paying the GPU
    // upload cost. This is only safe now that uploadReady is set -- it is the
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
      std::lock_guard<std::mutex> lock(m_ProcessingMutex);
      m_ProcessingPositions.erase(position);
      return false;
    }

    result.chunk.pass();
    Chunk promoted = std::move(result.chunk);

    {
      std::lock_guard<std::mutex> lock(m_ProcessingMutex);
      m_ProcessingPositions.erase(position);
    }
    // Do NOT erase from m_ProcessingChunks here. Returning false is what tells
    // forEachEntry to drop the entry, and it does the bookkeeping exactly once.
    // Erasing as well would decrement the entry count twice for one removal.
    promoted.setLodStep(result.lodStep);
    m_ProcessedChunks.emplace(position, std::move(promoted));
    return false;
  });

  // Streaming window origin. The chunk the camera is *inside*, which is an
  // index of containment and takes no half-chunk bias (#132).
  const int32_t currentChunkX = ChunkGrid::chunkIndexFor(cameraPosition.x);
  const int32_t currentChunkZ = ChunkGrid::chunkIndexFor(cameraPosition.z);

  // Spawn scan. The old form was an unconditional double loop over
  // (2 * RENDER_DISTANCE_CHUNKS + 1)^2 = 4225 grid positions, every frame,
  // whether or not the player had moved: one getChunkDistanceSquared call, one
  // lock_guard on m_ProcessingMutex and two hash lookups per position, to
  // discover that the positions already had chunks.
  //
  // The tree makes the same question cheap in both directions. A cell whose
  // chunks are all resident is skipped whole. A cell with no subtree at all is
  // reported once for the whole cell rather than 4^level times, which is what
  // makes a cold start cheaper than the loop it replaces rather than dearer.
  //
  // The distance test is unchanged and still runs per chunk, so which chunks
  // get enqueued is exactly the same set as before. Only the cost of finding
  // out is different.
  const glm::ivec2 windowLo{currentChunkX - Constants::Chunk::RENDER_DISTANCE_CHUNKS,
                            currentChunkZ - Constants::Chunk::RENDER_DISTANCE_CHUNKS};
  const glm::ivec2 windowHi{currentChunkX + Constants::Chunk::RENDER_DISTANCE_CHUNKS + 1,
                            currentChunkZ + Constants::Chunk::RENDER_DISTANCE_CHUNKS + 1};

  m_ProcessedChunks.forEachCellIn(
      windowLo, windowHi,
      [](int level, const glm::ivec2 &, const ChunkOctree<Chunk>::Node *node) {
        return (node != nullptr && node->complete)
                   ? ChunkOctree<Chunk>::Action::Skip
                   : ChunkOctree<Chunk>::Action::Descend;
      },
      [&](int level, const glm::ivec2 &cellOrigin, const Chunk *) {
        // A whole-cell report means every chunk in it needs streaming. Expand it
        // to individual chunks -- clipped to the window, because the cell grid
        // and the window do not line up and enqueueing outside the window would
        // be terrain the render distance does not ask for.
        const int side = 1 << level;
        const int loX = std::max(cellOrigin.x, windowLo.x);
        const int loZ = std::max(cellOrigin.y, windowLo.y);
        const int hiX = std::min(cellOrigin.x + side, windowHi.x);
        const int hiZ = std::min(cellOrigin.y + side, windowHi.y);
        for (int32_t chunkX = loX; chunkX < hiX; ++chunkX) {
          for (int32_t chunkZ = loZ; chunkZ < hiZ; ++chunkZ) {
            const glm::ivec2 position{chunkX, chunkZ};

            if (getChunkDistanceSquared(position, cameraPosition) >
                renderDistSq) {
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
              resultPtr = m_ProcessingChunks.emplace(position, m_Renderer);
            }

            TaskResult &result = *resultPtr;
            // The LOD stride is chosen ONCE, when the chunk is enqueued, and
            // carried with the task. It is deliberately not re-evaluated while
            // the chunk is in flight: a worker holds a reference to `result`
            // across frames, and rewriting the stride under it would change what
            // the mesher is emitting half way through.
            // Tier comes from the raw distance here, not from selectTier: a
            // chunk being (re-)requested has no current tier, because the LOD
            // pass above has just retired the old mesh if the tier had moved.
            // The hysteresis has already done its job by keeping the chunk
            // resident until that point; asking for a tier from scratch here is
            // exactly right.
            result.lodStep = static_cast<std::uint32_t>(
                Lod::sampleTierForDistance(
                    std::sqrt(getChunkDistanceSquared(position,
                                                      cameraPosition))));
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
                    // The pool itself now guarantees an exception cannot escape
                    // a worker, but only this task body knows *which* chunk
                    // failed -- without that the promotion loop would wait on
                    // uploadReady for a chunk that is never coming, stranding
                    // its position forever (#141).
                    try {
                      result.chunk.generateMeshData(position, result.lodStep);
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
              // The pool is at its backlog cap. Drop the placeholder entry so
              // the chunk is retried on a later frame; leaving it in place would
              // strand the position in m_ProcessingPositions forever, since
              // nothing will ever flag the chunk ready. Safe to erase now
              // precisely because no task was queued and therefore none holds a
              // reference to `result`.
              //
              // The GPU slot has already been taken and the compute dispatch
              // has already been issued, but nothing will ever read it back now,
              // so it has to be released here too. Without this the pool's
              // backlog cap leaks slots, and after 64 such frames the GPU path
              // is disabled for the rest of the session.
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
      });

  shader.use();
  Frustum frustum(camera);

  // LOD management: retire a resident chunk whose tier has moved on, so the
  // spawn scan below re-requests it at the new stride.
  //
  // This is where the hysteresis is actually applied. Lod::selectTier needs the
  // tier the chunk is AT, which is why Chunk carries its stride; asking only
  // "what tier does this distance map to" would re-mesh a chunk on every frame
  // the camera sits on a tier boundary, and each of those is both a stream of
  // work and a visible pop.
  m_ProcessedChunks.forEachEntry([&](const glm::ivec2 &position,
                                      Chunk &chunk) -> bool {
    const float distance =
        std::sqrt(getChunkDistanceSquared(position, cameraPosition));
    const Lod::Tier current = Lod::tierForStep(chunk.lodStep());
    if (Lod::selectTier(distance, current) == current) {
      return true;
    }
    // Tier changed: drop the mesh and let the spawn scan queue a fresh one at
    // the new stride. cleanup() before the drop, while the GL objects are still
    // reachable.
    chunk.cleanup();
    return false;
  });

  // Draw, as a hierarchical cull: test a cell against the frustum and skip its
  // whole subtree if the cell is entirely outside the view cone. The per-chunk
  // test this replaces cost one AABB test per resident chunk per frame and could
  // only ever reject one chunk at a time; a 64 x 64 chunk cell that is behind
  // the camera is now rejected in a single test.
  //
  // Conservative, and that is the whole safety argument. A cell is dropped only
  // when all eight of its corners are outside the same plane, so a cell that
  // straddles the view boundary is kept and descended into. Anything the
  // projection puts on screen therefore survives both the cell test and the
  // leaf test, which is the property tests/test_frustum.cpp checks against the
  // real projection and tests/test_hierarchical_cull.cpp checks for this walk.
  //
  // The SAME predicate call at level 0 is the per-chunk test the old draw loop
  // did, box for box: at level 0 the side is 1 * LENGTH, so the cell box is
  // exactly Frustum::isChunkInside's box. Nothing is skipped for being in a
  // tree; a chunk is drawn precisely when the old code would have drawn it, and
  // the only difference is how many chunks were tested to find that out.
  m_ProcessedChunks.forEachValuePruned(
      [&](int level, const glm::ivec2 &origin) {
        const float side = static_cast<float>(1 << level) *
                           static_cast<float>(Constants::Chunk::LENGTH);
        return frustum.isBoundsInside(
            {static_cast<float>(origin.x) *
                 static_cast<float>(Constants::Chunk::LENGTH),
             0.0f,
             static_cast<float>(origin.y) *
                 static_cast<float>(Constants::Chunk::LENGTH)},
            {static_cast<float>(origin.x) *
                 static_cast<float>(Constants::Chunk::LENGTH) + side,
             static_cast<float>(Constants::Chunk::HEIGHT),
             static_cast<float>(origin.y) *
                 static_cast<float>(Constants::Chunk::LENGTH) + side});
      },
      [&](const glm::ivec2 &position, Chunk &chunk) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(
            model,
            glm::vec3(
                static_cast<float>(position.s * Constants::Chunk::LENGTH),
                0.0f,
                static_cast<float>(position.t * Constants::Chunk::LENGTH)));

        // Terrain pass: scene shader draws both the terrain and the (blue)
        // water surface, which is folded into the same mesh.
        shader.use();
        shader.setUniformMat4("uModel", model);
        chunk.render();
      });
}

bool ChunkManager::tryGetGroundHeight(const glm::vec3 &cameraPosition,
                                      float &outHeight) const {
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

  // One descent rather than contains() followed by at(), which would walk the
  // tree twice to reach the same node.
  //
  // This is on the player/collision path, so it is the one place the octree is
  // a straight cost rather than a saving: locate is a fixed
  // kSubtreeLevels + 1 = 7 dependent hops, where the flat map's find was one
  // hash and one bucket walk. Bounded and small, but not free -- noted in
  // src/chunk_octree.h rather than left to be discovered.
  const Chunk *chunk = m_ProcessedChunks.find(chunkPosition);
  if (chunk == nullptr) {
    // Not yet streamed in. Report nothing rather than a height: the player
    // should keep falling until the chunk arrives, not be placed on a floor
    // that is not there (#133). outHeight is deliberately left untouched.
    return false;
  }

  // The surface, not the terrain. The mesher draws a flat opaque water plane
  // at WATER_LEVEL over every submerged column into the same mesh, so for
  // those columns the terrain height is invisible and unreachable -- asking
  // for it would place the player on the lake bed under an opaque ceiling, up
  // to 45 blocks below the surface they are actually looking at (#134).
  // Constants::Chunk::walkableSurfaceY is the same predicate the mesher uses,
  // so the two cannot drift.
  outHeight = Constants::Chunk::walkableSurfaceY(chunk->getHighestBlockY(
      static_cast<uint32_t>(localX), static_cast<uint32_t>(localZ)));
  return true;
}
