#include "manager.h"

#include <sys/types.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <glm/glm.hpp>

#include "chunk.h"
#include "config.h"
#include "Frustum.h"
#include "terrain.h"

#include <iostream>

constexpr int TERRAIN_TILE_SIZE = 512;

struct TerrainTile {
    glm::vec2 position;
    std::vector<float> data;
    bool ready = false;
};

std::vector<std::shared_ptr<TerrainTile>> g_TerrainTiles;
std::mutex g_TerrainMutex;

std::shared_ptr<TerrainTile> getOrCreateTile(const glm::vec2& tilePos) {
    std::lock_guard<std::mutex> lock(g_TerrainMutex);
    for (auto& tile : g_TerrainTiles) {
        if (tile->position == tilePos && tile->ready) {
            return tile;
        }
    }
    auto newTile = std::make_shared<TerrainTile>();
    newTile->position = tilePos;
    newTile->data.resize(TERRAIN_TILE_SIZE * TERRAIN_TILE_SIZE);
    g_TerrainTiles.push_back(newTile);
    return newTile;
}

void ChunkManager::load() {
    std::cout << "[Manager] Initializing terrain generator..." << std::endl;
    g_TerrainGenerator.init();
    std::cout << "[Manager] Terrain generator ready" << std::endl;
}

void ChunkManager::render(const Camera *camera, Shader &shader) {
  glm::vec3 cameraPosition = camera->m_Position;

  for (auto it = m_ProcessedChunks.begin(); it != m_ProcessedChunks.end();) {
    const glm::vec2 &position = it->first;
    float chunkCenterX = position.s * Constants::Chunk::LENGTH -
                         (Constants::Chunk::LENGTH / 2.0f);
    float chunkCenterZ = position.t * Constants::Chunk::LENGTH -
                         (Constants::Chunk::LENGTH / 2.0f);

    float xDisplacement = chunkCenterX - cameraPosition.x;
    float zDisplacement = chunkCenterZ - cameraPosition.z;

    float distanceSquared =
        (xDisplacement * xDisplacement) + (zDisplacement * zDisplacement);
    if (distanceSquared > Constants::Chunk::RENDER_DISTANCE_BLOCKS *
                              Constants::Chunk::RENDER_DISTANCE_BLOCKS) {
      it->second.cleanup();
      it = m_ProcessedChunks.erase(it);
      continue;
    }

    ++it;
  }

  for (auto it = m_ProcessingChunks.begin(); it != m_ProcessingChunks.end();) {
    const glm::vec2 position = it->first;
    TaskResult &result = it->second;

    if (!result.ready.load()) {
      ++it;
      continue;
    }

    result.chunk.pass();

    m_ProcessingPositions.erase(position);
    result.chunk.printHeightMap();
    m_ProcessedChunks[position] = std::move(result.chunk);
    it = m_ProcessingChunks.erase(it);
  }

  int currentChunkX = static_cast<int>(
      std::floor((cameraPosition.x + (Constants::Chunk::LENGTH / 2.0f)) /
                 Constants::Chunk::LENGTH));
  int currentChunkZ = static_cast<int>(
      std::floor((cameraPosition.z + (Constants::Chunk::LENGTH / 2.0f)) /
                 Constants::Chunk::LENGTH));

  for (int chunkX = currentChunkX - Constants::Chunk::RENDER_DISTANCE_CHUNKS;
       chunkX <= currentChunkX + Constants::Chunk::RENDER_DISTANCE_CHUNKS;
       chunkX++) {
    for (int chunkZ = currentChunkZ - Constants::Chunk::RENDER_DISTANCE_CHUNKS;
         chunkZ <= currentChunkZ + Constants::Chunk::RENDER_DISTANCE_CHUNKS;
         chunkZ++) {
      glm::vec2 position = {chunkX, chunkZ};
      float chunkCenterX = position.s * Constants::Chunk::LENGTH -
                           (Constants::Chunk::LENGTH / 2.0f);
      float chunkCenterZ = position.t * Constants::Chunk::LENGTH -
                           (Constants::Chunk::LENGTH / 2.0f);

      float xDisplacement = chunkCenterX - cameraPosition.x;
      float zDisplacement = chunkCenterZ - cameraPosition.z;

      float distanceSquared =
          (xDisplacement * xDisplacement) + (zDisplacement * zDisplacement);
      if (distanceSquared > Constants::Chunk::RENDER_DISTANCE_BLOCKS *
                                Constants::Chunk::RENDER_DISTANCE_BLOCKS) {
        // Chunk outside of the radius
        continue;
      }

      if (m_ProcessedChunks.contains(position)) {
        // Chunk already processed
        continue;
      }

      if (m_ProcessingPositions.contains(position)) {
        continue;
      }

      m_ProcessingPositions.insert(position);
      TaskResult &result = m_ProcessingChunks[position];

      glm::vec2 tilePos(
          std::floor(position.s * Constants::Chunk::LENGTH / TERRAIN_TILE_SIZE) * TERRAIN_TILE_SIZE,
          std::floor(position.t * Constants::Chunk::LENGTH / TERRAIN_TILE_SIZE) * TERRAIN_TILE_SIZE);

      uint32_t worldSeed = 1551611252;
      uint32_t seed = worldSeed + static_cast<uint32_t>(std::hash<float>{}(tilePos.x) ^ (std::hash<float>{}(tilePos.y) << 1));

      auto tile = getOrCreateTile(tilePos);

      if (!tile->ready) {
          std::cout << "[Manager] Generating terrain tile at (" << tilePos.x << ", " << tilePos.y << ")" << std::endl;
          tile->data = g_TerrainGenerator.generate(tilePos, seed);
          tile->ready = true;
      }

      m_ThreadPool.enqueue([&result, position, tile]() {
        result.chunk.generateMeshData(position, tile->data.data());
        result.ready.store(true);
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
  // Compute chunk coordinates using same logic as rendering to match keys
  int chunkX = static_cast<int>(
      std::floor((cameraPosition.x + (Constants::Chunk::LENGTH / 2.0f)) /
                 Constants::Chunk::LENGTH));
  int chunkZ = static_cast<int>(
      std::floor((cameraPosition.z + (Constants::Chunk::LENGTH / 2.0f)) /
                 Constants::Chunk::LENGTH));

  glm::vec2 chunkPosition = {chunkX, chunkZ};

  // Compute integer world block coords (floor) and convert to local chunk
  // indices
  int worldX = static_cast<int>(std::floor(cameraPosition.x));
  int worldZ = static_cast<int>(std::floor(cameraPosition.z));

  int localX = worldX - (chunkX * static_cast<int>(Constants::Chunk::LENGTH));
  int localZ = worldZ - (chunkZ * static_cast<int>(Constants::Chunk::LENGTH));

  // Ensure local indices are inside [0, LENGTH-1]
  localX =
      std::clamp(localX, 0, static_cast<int>(Constants::Chunk::LENGTH - 1));
  localZ =
      std::clamp(localZ, 0, static_cast<int>(Constants::Chunk::LENGTH - 1));

  if (m_ProcessedChunks.contains(chunkPosition)) {
    Chunk &chunk = m_ProcessedChunks[chunkPosition];
    return static_cast<float>(chunk.getHighestBlockY(
        static_cast<uint>(localX), static_cast<uint>(localZ)));
  }

  return static_cast<float>(Constants::Chunk::HEIGHT);
}
