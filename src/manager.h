#pragma once
#include <noise/noise.h>
#include <sys/types.h>

#include <atomic>
#include <unordered_map>
#include <unordered_set>

#include "camera.h"
#include "chunk.h"
#include "shader.h"
#include "threadpool.h"

struct TaskResult {
  Chunk chunk;
  std::atomic<bool> ready{false};
};

namespace std {
template <> struct hash<glm::vec2> {
  size_t operator()(glm::vec2 const &v) const noexcept {
    size_t h1 = std::hash<float>{}(v.s);
    size_t h2 = std::hash<float>{}(v.t);
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
  std::unordered_map<glm::vec2, Chunk> m_ProcessedChunks;
  std::unordered_set<glm::vec2> m_ProcessingPositions;

  std::unordered_map<glm::vec2, TaskResult> m_ProcessingChunks;
  ThreadPool m_ThreadPool;
};
