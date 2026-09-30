#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

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

/** The two edge vectors a face is built from, plus the outward normal that
 *  winding is required to produce.
 *
 *  Chunk::addQuad emits its first triangle as (a, a+dv, a+du), so the
 *  right-hand-rule normal of that triangle is dv x du, i.e. -(du x dv). With
 *  GL_CULL_FACE enabled and the default GL_CCW front-face convention, a quad
 *  is only visible from outside the block if that normal equals the face's
 *  outward normal. Getting this backwards does not look like a bug -- the
 *  geometry is still there, it is just culled -- so the requirement is stated
 *  here next to the vectors it constrains and asserted in the test suite.
 *
 *  `outX/outY/outZ` is deliberately carried rather than recomputed by the
 *  caller, so the test compares the winding against an independent statement
 *  of intent instead of re-deriving the same formula twice. */
struct FaceWinding {
  int32_t duX, duY, duZ;
  int32_t dvX, dvY, dvZ;
  int32_t outX, outY, outZ;
};

/** Winding table for the six face directions (0..5) used by generateMeshData.
 *  Throws std::out_of_range for any other direction.
 *
 *  Defined inline in the header rather than in chunk.cpp so the unit suite can
 *  assert the winding without linking chunk.cpp, which needs a renderer. */
inline FaceWinding faceWindingForDirection(int direction) {
  switch (direction) {
    case 0:  // +X
      return {0, 0, 1, 0, 1, 0, 1, 0, 0};
    case 1:  // -X
      return {0, 1, 0, 0, 0, 1, -1, 0, 0};
    case 2:  // +Y
      return {1, 0, 0, 0, 0, 1, 0, 1, 0};
    case 3:  // -Y
      // du and dv are swapped relative to face 2. That is the fix: the pair
      // (1,0,0) x (0,0,1) gives +Y, and -(1,0,0) x (0,0,1) is -Y only if the
      // cross is taken the other way round, which the edge order controls.
      return {0, 0, 1, 1, 0, 0, 0, -1, 0};
    case 4:  // +Z
      return {0, 1, 0, 1, 0, 0, 0, 0, 1};
    case 5:  // -Z
      return {1, 0, 0, 0, 1, 0, 0, 0, -1};
    default:
      throw std::out_of_range("invalid face direction: " +
                              std::to_string(direction));
  }
}

// Packs a chunk vertex into a single uint32_t (see README, Milestone 4).
// The field struct is named rather than anonymous: an anonymous struct inside
// a union is a GNU extension, not standard C++, and trips -Wpedantic.
union PackedVertex {
  uint32_t bits;
  struct Fields {
    // Y gets 10 bits (supports worlds up to y=1023); the former 2-bit
    // _pad is consumed so the vertex still packs into a uint32_t.
    uint32_t x      : 8;
    uint32_t z      : 8;
    uint32_t y      : 10;
    uint32_t normal : 2;
    uint32_t texId   : 2;
    uint32_t corner  : 2;
  } f;
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
