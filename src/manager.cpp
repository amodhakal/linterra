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
#include "Frustum.h"
#include "renderer/renderer.hpp"

namespace {

constexpr float kChunkBlockExtent =
    static_cast<float>(Constants::Chunk::LENGTH);
constexpr float kChunkCenterOffset = kChunkBlockExtent * 0.5f;

} // namespace

ChunkManager::ChunkManager(IRenderer* renderer)
    : m_Renderer(renderer), m_ComputeShader(renderer) {}

float ChunkManager::getChunkDistanceSquared(const glm::ivec2 &chunkPos,
                                            const glm::vec3 &cameraPos) {
  const float chunkCenterX = static_cast<float>(chunkPos.x) * kChunkBlockExtent - kChunkCenterOffset;
  const float chunkCenterZ = static_cast<float>(chunkPos.y) * kChunkBlockExtent - kChunkCenterOffset;
  const float dx = chunkCenterX - cameraPos.x;
  const float dz = chunkCenterZ - cameraPos.z;
  return dx * dx + dz * dz;
}

void ChunkManager::load() {
  if (Constants::Noise::USE_GPU) {
    try {
      m_ComputeShader.loadCompute(Constants::TERRAIN_COMPUTE_PATH);
      m_ComputeShader.newUniform("uChunkPos");
      m_ComputeShader.newUniform("uFrequency");
      m_ComputeShader.newUniform("uMaxHeight");
      m_ComputeShader.newUniform("uExtSide");
      m_ComputeShader.newUniform("uSeed");
      m_ComputeShader.newUniform("uOctaves");
      m_ComputeShader.newUniform("uGain");
      m_ComputeShader.newUniform("uLacunarity");

      size_t kExtSide = Chunk::kExtSide;
      size_t ssboSize = kExtSide * kExtSide * sizeof(uint32_t);
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
    const glm::ivec2 position = it->first;

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

    const glm::ivec2 position = it->first;
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
      if (Constants::Noise::USE_GPU && m_HeightMapSSBO && m_ComputeShader.getId() != 0) {
        m_ComputeShader.bindBufferBase(*m_HeightMapSSBO, 0);
        result.chunk.generateHeightMapGPU(position, m_ComputeShader, *m_HeightMapSSBO);
        m_ThreadPool.enqueue([&result]() {
          result.chunk.generateMesh();
          result.uploadReady.store(true, std::memory_order_release);
        });
      } else {
        m_ThreadPool.enqueue([&result, position]() {
          result.chunk.generateMeshData(position);
          result.uploadReady.store(true, std::memory_order_release);
        });
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
        glm::vec3(static_cast<float>(position.x * Constants::Chunk::LENGTH),
                  0.0f,
                  static_cast<float>(position.y * Constants::Chunk::LENGTH)));

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

  const glm::ivec2 chunkPosition = {chunkX, chunkZ};

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
