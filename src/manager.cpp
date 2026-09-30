#include "manager.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <print>

#include <glm/glm.hpp>

#include "chunk.h"
#include "config.h"
#include "frustum.h"
#include "renderer/renderer.hpp"

namespace {

constexpr float kChunkBlockExtent =
    static_cast<float>(Constants::Chunk::LENGTH);
constexpr float kChunkCenterOffset = kChunkBlockExtent * 0.5f;
// Chunk meshes are drawn with vertices in [pos*L, pos*L+L] (see
// ChunkManager::render's model translation and chunk.cpp's local block coords),
// so the true chunk center is pos*L + L/2, not pos*L - L/2.


} // namespace

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
    } catch (const std::exception& e) {
      std::println("Failed to load terrain compute shader, falling back to CPU noise: {}", e.what());
    }
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
        // The worker only flags meshReady on this path; the mesh itself is
        // built here, so this is the point where the chunk is ready to upload.
        // Without it the chunk would be re-meshed every frame and never
        // promoted out of m_ProcessingChunks.
        result.uploadReady.store(true, std::memory_order_release);
      }
    }

    if (!result.uploadReady.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }

    const glm::ivec2 position = it->first;

    // Range-check before uploading: if the camera has moved away since this
    // task was enqueued, discard the chunk here instead of paying the GPU
    // upload cost. This is only safe now that uploadReady is set — it is the
    // worker's final action, so no task can still be touching `result`.
    // Erasing earlier would leave the worker meshing into freed memory.
    if (getChunkDistanceSquared(position, cameraPosition) > renderDistSq) {
      result.chunk.cleanup();
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

  const int32_t currentChunkX = static_cast<int32_t>(
      std::floor((cameraPosition.x + kChunkCenterOffset) / kChunkBlockExtent));
  const int32_t currentChunkZ = static_cast<int32_t>(
      std::floor((cameraPosition.z + kChunkCenterOffset) / kChunkBlockExtent));

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
          // Batched GPU path: dispatch into this chunk's slot of the shared
          // SSBO and defer both meshing and readback. The main thread never
          // blocks on a per-chunk GPU sync here; the readback happens later,
          // once the slot has been given a full frame to complete.
          const uint32_t slot = m_NextGpuSlot++ % kGpuSlots;
          result.slot = slot;
          m_ComputeShader.bindBufferBase(*m_HeightMapSSBO, 0);
          result.chunk.generateHeightMapGPU(position, slot, m_ComputeShader);
          return m_ThreadPool.tryEnqueue(
              [&result]() {
                result.meshReady.store(true, std::memory_order_release);
              },
              kMaxPendingTasks);
        }

        return m_ThreadPool.tryEnqueue(
            [&result, position]() {
              result.chunk.generateMeshData(position);
              result.uploadReady.store(true, std::memory_order_release);
            },
            kMaxPendingTasks);
      }();

      if (!enqueued) {
        // The pool is at its backlog cap. Drop the placeholder entry so the
        // chunk is retried on a later frame; leaving it in place would strand
        // the position in m_ProcessingPositions forever, since nothing will
        // ever flag the chunk ready. Safe to erase now precisely because no
        // task was queued and therefore none holds a reference to `result`.
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
  const int32_t chunkX = static_cast<int32_t>(
      std::floor((cameraPosition.x + kChunkCenterOffset) / kChunkBlockExtent));
  const int32_t chunkZ = static_cast<int32_t>(
      std::floor((cameraPosition.z + kChunkCenterOffset) / kChunkBlockExtent));

  const glm::ivec2 chunkPosition = {static_cast<float>(chunkX),
                                   static_cast<float>(chunkZ)};

  const int32_t worldX = static_cast<int32_t>(std::floor(cameraPosition.x));
  const int32_t worldZ = static_cast<int32_t>(std::floor(cameraPosition.z));

  int32_t localX = worldX - (chunkX * Constants::Chunk::LENGTH);
  int32_t localZ = worldZ - (chunkZ * Constants::Chunk::LENGTH);

  localX =
      std::clamp(localX, 0, static_cast<int32_t>(Constants::Chunk::LENGTH - 1));
  localZ =
      std::clamp(localZ, 0, static_cast<int32_t>(Constants::Chunk::LENGTH - 1));

  if (m_ProcessedChunks.contains(chunkPosition)) {
    Chunk &chunk = m_ProcessedChunks.at(chunkPosition);
    return static_cast<float>(chunk.getHighestBlockY(
        static_cast<uint32_t>(localX), static_cast<uint32_t>(localZ)));
  }

  return static_cast<float>(Constants::Chunk::HEIGHT);
}
