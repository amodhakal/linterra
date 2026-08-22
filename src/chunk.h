#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <glad/glad.h>

#include "config.h"

class IRenderer;
class IBuffer;
class IVertexArray;
class Shader;

enum BlockType : uint8_t { AIR = 0, GRASS = 1, DIRT = 2, WATER = 3 };

enum BlockNormal : uint8_t {
  RIGHT_LEFT_NORMAL = 0,
  FRONT_BACK_NORMAL = 1,
  BOTTOM_NORMAL = 2,
  TOP_NORMAL = 3
};

union PackedVertex {
  uint32_t bits;
  struct {
    // Y gets 10 bits (supports worlds up to y=1023); the former 2-bit
    // _pad is consumed so the vertex still packs into a uint32_t.
    uint32_t x      : 8;
    uint32_t z      : 8;
    uint32_t y      : 10;
    uint32_t normal : 2;
    uint32_t texId   : 2;
    uint32_t corner  : 2;
  };
};

class Chunk {
public:
  explicit Chunk(IRenderer* renderer);

  ~Chunk();

  Chunk(const Chunk &) = delete;
  Chunk &operator=(const Chunk &) = delete;
  Chunk(Chunk &&other) noexcept;
  Chunk &operator=(Chunk &&other) noexcept;

  void generateMeshData(const glm::ivec2 &position);
  void generateHeightMapCPU(const glm::ivec2 &position);
  void generateMesh();

  /**
   * Stage 1 of the GPU path: dispatch the compute shader into the chunk's
   * slot of the batched SSBO. Does NOT read back — the caller must call
   * finishHeightMapGPU() for this chunk later (deferred, after the GPU has
   * had time to run) before using the height maps.
   */
  void generateHeightMapGPU(const glm::vec2 &position, uint32_t slotOffset,
                            Shader &computeShader);

  /** Stage 2 of the GPU path: read back the SSBO slot into the height maps.
   *  Must be called on the GL thread after generateHeightMapGPU(), ideally
   *  deferred by at least one frame so the GPU runs asynchronously. */
  void finishHeightMapGPU(uint32_t slotOffset, IBuffer &ssbo);

  /** True once the heightmap data has been read back from the GPU. */
  bool isGpuHeightMapReady() const { return m_GpuHeightMapReady; }

  void pass();
  void render();
  void cleanup();

  /** Moved-from state: a moved-from Chunk is valid but empty —
   *  m_Renderer is nullptr, GPU resources (VBO/EBO/VAO) and mesh data
   *  (m_Data/m_Indices) are transferred to the destination, size/count
   *  fields are zeroed. Heightmaps are copied rather than moved, so the
   *  moved-from chunk retains its (now stale) heightmap values; they are
   *  safe to read but must be regenerated before reuse. */

  uint16_t getHighestBlockY(uint32_t blockX, uint32_t blockZ);

  static constexpr uint32_t kExtSide =
      static_cast<uint32_t>(Constants::Chunk::LENGTH) + 2u;

private:
  void resetMovedFrom(Chunk &other) noexcept;

  IRenderer* m_Renderer = nullptr;
  std::unique_ptr<IBuffer> m_VBO;
  std::unique_ptr<IBuffer> m_EBO;
  std::unique_ptr<IVertexArray> m_VAO;
  uint32_t m_VboSize;
  uint32_t m_IndexCount;

  std::vector<PackedVertex> m_Data;
  std::vector<uint32_t> m_Indices;

  uint16_t m_HeightMap[Constants::Chunk::LENGTH][Constants::Chunk::LENGTH];

  /** Halo for neighbor lookups: local block (lx,lz) in [-1, LENGTH] maps to [(uint32_t)lx + 1]. */
  uint16_t m_ExtendedHeightMap[kExtSide][kExtSide];

  /** True after the deferred GPU readback has filled the height maps. */
  bool m_GpuHeightMapReady = false;
};
