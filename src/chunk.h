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

union PackedVertex {
  uint32_t bits;
  struct {
    uint32_t x      : 8;
    uint32_t z      : 8;
    uint32_t y      : 8;
    uint32_t normal : 2;
    uint32_t texId   : 2;
    uint32_t corner  : 2;
    uint32_t _pad   : 2;
  };
};

class Chunk {
public:
  Chunk() = default;

  void generateMeshData(const glm::vec2 &position, const float* terrainData = nullptr);

  void pass();
  void render();
  void cleanup();
  void printHeightMap() const;
  ushort getHighestBlockY(uint blockX, uint blockZ);

private:
  uint m_VAO;
  uint m_VBO;
  uint m_EBO;
  uint m_VboSize;
  uint m_IndexCount;

  std::vector<PackedVertex> m_Data;
  std::vector<uint> m_Indices;

  ushort m_HeightMap[Constants::Chunk::LENGTH][Constants::Chunk::LENGTH];
};
