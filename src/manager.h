#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "camera.h"
#include "chunk.h"
#include "shader.h"
#include "threadpool.h"

struct TaskResult {
  Chunk chunk;
  std::atomic<bool> uploadReady{false};

  TaskResult() = default;

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

  TaskResult(const TaskResult &) = delete;
  TaskResult &operator=(const TaskResult &) = delete;
};

namespace std {

template <>
struct hash<glm::vec2> {
  std::size_t operator()(glm::vec2 const &v) const noexcept {
    std::size_t h1 = hash<float>{}(v.s);
    std::size_t h2 = hash<float>{}(v.t);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }
};

} // namespace std

class ChunkManager {
public:
  ChunkManager() = default;

  void load();

  void render(const Camera *camera, Shader &shader);

  float getPositionHighestY(const glm::vec3 &cameraPosition);

private:
  static float getChunkDistanceSquared(const glm::vec2 &chunkPos,
                                      const glm::vec3 &cameraPos);

  std::unordered_map<glm::vec2, Chunk> m_ProcessedChunks;
  std::unordered_set<glm::vec2> m_ProcessingPositions;

  std::unordered_map<glm::vec2, TaskResult> m_ProcessingChunks;
  std::mutex m_ProcessingMutex;
  ThreadPool m_ThreadPool;
};
