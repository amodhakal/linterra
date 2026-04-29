#pragma once
#include <sys/types.h>

#include <cstdint>
#include <vector>

#include "config.h"

enum BlockType : uint8_t { AIR = 0, GRASS = 1, DIRT = 2 };

enum BlockNormal : uint8_t {
  RIGHT_LEFT_NORMAL = 0,
  FRONT_BACK_NORMAL = 1,
  BOTTOM_NORMAL = 2,
  TOP_NORMAL = 3
};

class Chunk {
public:
  Chunk() = default;

  void generateMeshData(const glm::vec2 &position);

  void pass();
  void render();
  void cleanup();
  ushort getHighestBlockY(uint blockX, uint blockZ);

private:
  uint m_VAO;
  uint m_VBO;
  uint m_EBO;
  uint m_VboSize;
  uint m_IndexCount;

  std::vector<float> m_Data;
  std::vector<uint> m_Indices;

  ushort m_HeightMap[Constants::Chunk::LENGTH][Constants::Chunk::LENGTH];
};
