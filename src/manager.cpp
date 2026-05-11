#include "manager.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include <glm/glm.hpp>

#include "chunk.h"
#include "config.h"
#include "frustum.h"

namespace {

constexpr float kChunkBlockExtent =
    static_cast<float>(Constants::Chunk::LENGTH);
constexpr float kChunkCenterOffset = kChunkBlockExtent * 0.5f;

} // namespace

float ChunkManager::getChunkDistanceSquared(const glm::vec2 &chunkPos,
                                            const glm::vec3 &cameraPos) {
  const float chunkCenterX = chunkPos.s * kChunkBlockExtent - kChunkCenterOffset;
  const float chunkCenterZ = chunkPos.t * kChunkBlockExtent - kChunkCenterOffset;
  const float dx = chunkCenterX - cameraPos.x;
  const float dz = chunkCenterZ - cameraPos.z;
  return dx * dx + dz * dz;
}

void ChunkManager::load() {}

void ChunkManager::render(const Camera *camera, Shader &shader) {
  const glm::vec3 cameraPosition = camera->m_Position;

  const float renderDistBlocks =
      static_cast<float>(Constants::Chunk::RENDER_DISTANCE_BLOCKS);
  const float renderDistSq = renderDistBlocks * renderDistBlocks;

  for (auto it = m_ProcessedChunks.begin(); it != m_ProcessedChunks.end();) {
    const glm::vec2 &position = it->first;

    if (getChunkDistanceSquared(position, cameraPosition) > renderDistSq) {
      it->second.cleanup();
      it = m_ProcessedChunks.erase(it);
      continue;
    }

    ++it;
  }

  for (auto it = m_ProcessingChunks.begin(); it != m_ProcessingChunks.end();) {
    TaskResult &result = it->second;

    if (!result.uploadReady.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }

    const glm::vec2 position = it->first;
    result.chunk.pass();
    Chunk promoted = std::move(result.chunk);

    {
      std::lock_guard<std::mutex> lock(m_ProcessingMutex);
      m_ProcessingPositions.erase(position);
      it = m_ProcessingChunks.erase(it);
    }

    m_ProcessedChunks[position] = std::move(promoted);
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
      const glm::vec2 position = {static_cast<float>(chunkX),
                                  static_cast<float>(chunkZ)};

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
        resultPtr = &m_ProcessingChunks[position];
      }

      TaskResult &result = *resultPtr;
      m_ThreadPool.enqueue([&result, position]() {
        result.chunk.generateMeshData(position);
        result.uploadReady.store(true, std::memory_order_release);
      });
    }
  }

  shader.use();
  Frustum frustum(camera);

  for (auto &value : m_ProcessedChunks) {
    const glm::vec2 &position = value.first;

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
    shader.setUniformMat4("uModel", model);

    chunk.render();
  }
}

float ChunkManager::getPositionHighestY(const glm::vec3 &cameraPosition) {
  const int32_t chunkX = static_cast<int32_t>(
      std::floor((cameraPosition.x + kChunkCenterOffset) / kChunkBlockExtent));
  const int32_t chunkZ = static_cast<int32_t>(
      std::floor((cameraPosition.z + kChunkCenterOffset) / kChunkBlockExtent));

  const glm::vec2 chunkPosition = {static_cast<float>(chunkX),
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
    Chunk &chunk = m_ProcessedChunks[chunkPosition];
    return static_cast<float>(chunk.getHighestBlockY(
        static_cast<uint32_t>(localX), static_cast<uint32_t>(localZ)));
  }

  return static_cast<float>(Constants::Chunk::HEIGHT);
}
