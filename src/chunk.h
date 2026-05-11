#pragma once

#include <cstdint>
#include <vector>

#include <glad/glad.h>

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
  Chunk();

  ~Chunk();

  Chunk(const Chunk &) = delete;
  Chunk &operator=(const Chunk &) = delete;
  Chunk(Chunk &&other) noexcept;
  Chunk &operator=(Chunk &&other) noexcept;

  void generateMeshData(const glm::vec2 &position);

  void pass();
  void render();
  void cleanup();

  uint16_t getHighestBlockY(uint32_t blockX, uint32_t blockZ);

private:
  uint32_t m_VAO;
  uint32_t m_VBO;
  uint32_t m_EBO;
  uint32_t m_VboSize;
  uint32_t m_IndexCount;

  std::vector<PackedVertex> m_Data;
  std::vector<uint32_t> m_Indices;

  static constexpr uint32_t kExtSide =
      static_cast<uint32_t>(Constants::Chunk::LENGTH) + 2u;

  uint16_t m_HeightMap[Constants::Chunk::LENGTH][Constants::Chunk::LENGTH];

  /** Halo for neighbor lookups: local block (lx,lz) in [-1, LENGTH] maps to [(uint32_t)lx + 1]. */
  uint16_t m_ExtendedHeightMap[kExtSide][kExtSide];
};
