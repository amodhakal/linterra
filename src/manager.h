#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "camera.h"
#include "chunk.h"
#include "shader.h"
#include "threadpool.h"

class IRenderer;

struct TaskResult {
  Chunk chunk;
  std::atomic<bool> uploadReady{false};

  TaskResult() = delete;
  explicit TaskResult(IRenderer* renderer) : chunk(renderer) {}

  TaskResult(TaskResult &&other) noexcept
      : chunk(std::move(other.chunk)),
        uploadReady(
            other.uploadReady.load(std::memory_order_relaxed)) {}

  TaskResult &operator=(TaskResult &&other) noexcept {
    if (this != &other) {
      chunk = std::move(other.chunk);
      uploadReady.store(
          other.uploadReady.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
    }
    return *this;
  }

  // Upload the chunk's mesh to the GPU.
  void pass() {
    chunk.pass();
  }

  TaskResult(const TaskResult &) = delete;
  TaskResult &operator=(const TaskResult &) = delete;
};

namespace std {

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
  static float getChunkDistanceSquared(const glm::vec2 &chunkPos,
                                      const glm::vec3 &cameraPos);

  IRenderer* m_Renderer = nullptr;
  std::unique_ptr<IBuffer> m_HeightMapSSBO;
  Shader m_ComputeShader;
  std::unordered_map<glm::ivec2, Chunk> m_ProcessedChunks;
  std::unordered_set<glm::ivec2> m_ProcessingPositions;

  std::unordered_map<glm::ivec2, TaskResult> m_ProcessingChunks;
  std::mutex m_ProcessingMutex;
  ThreadPool m_ThreadPool;
};
