#pragma once

#include <sys/types.h>

#include <vector>

#include "config.h"

struct ChunkPosition {
  int xPosition;
  int zPosition;

  bool operator==(const ChunkPosition& other) const {
    return xPosition == other.xPosition && zPosition == other.zPosition;
  }
};

template <>
struct std::hash<ChunkPosition> {
  std::size_t operator()(const ChunkPosition& pos) const noexcept {
    std::size_t h1 = std::hash<int>{}(pos.xPosition);
    std::size_t h2 = std::hash<int>{}(pos.zPosition);
    return h1 ^ (h2 << 1);
  }
};

enum BlockType { AIR, GRASS };

typedef BlockType BlockStore[Constants::Chunk::LENGTH][Constants::Chunk::HEIGHT]
                            [Constants::Chunk::LENGTH];

class Chunk {
 public:
  Chunk() = default;
  Chunk(const ChunkPosition& position);
  void render();

 private:
  uint m_VAO;
  uint m_VBO;

  std::vector<float> m_VboData;
};
