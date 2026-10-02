#include "chunk.h"
#include <noise/noise.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>

#include "config.h"
#include "heightmap.h"
#include "renderer/renderer.hpp"
#include "shader.h"

Chunk::Chunk(IRenderer* renderer)
    : m_Renderer(renderer), m_VboSize(0), m_IndexCount(0) {}

Chunk::Chunk(Chunk &&other) noexcept
    : m_Renderer(other.m_Renderer),
      m_VBO(std::move(other.m_VBO)),
      m_EBO(std::move(other.m_EBO)),
      m_VAO(std::move(other.m_VAO)),
      m_VboSize(other.m_VboSize),
      m_IndexCount(other.m_IndexCount),
      m_Data(std::move(other.m_Data)),
      m_Indices(std::move(other.m_Indices)) {
  static_assert(std::is_trivially_copyable_v<decltype(m_HeightMap)> &&
                    std::is_trivially_copyable_v<decltype(m_ExtendedHeightMap)>,
                "Heightmaps must remain trivially copyable for bulk moves");
  std::memcpy(m_HeightMap, other.m_HeightMap, sizeof(m_HeightMap));
  std::memcpy(m_ExtendedHeightMap, other.m_ExtendedHeightMap,
              sizeof(m_ExtendedHeightMap));

  resetMovedFrom(other);
}

Chunk &Chunk::operator=(Chunk &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  cleanup();
  m_Renderer = other.m_Renderer;
  m_VBO = std::move(other.m_VBO);
  m_EBO = std::move(other.m_EBO);
  m_VAO = std::move(other.m_VAO);
  m_VboSize = other.m_VboSize;
  m_IndexCount = other.m_IndexCount;
  m_Data = std::move(other.m_Data);
  m_Indices = std::move(other.m_Indices);
  std::memcpy(m_HeightMap, other.m_HeightMap, sizeof(m_HeightMap));
  std::memcpy(m_ExtendedHeightMap, other.m_ExtendedHeightMap,
              sizeof(m_ExtendedHeightMap));

  resetMovedFrom(other);

  return *this;
}

void Chunk::resetMovedFrom(Chunk &other) noexcept {
  other.m_Renderer = nullptr;
  other.m_VBO = nullptr; // unique_ptr already null after move
  other.m_EBO = nullptr;
  other.m_VAO = nullptr;
  other.m_VboSize = 0;
  other.m_IndexCount = 0;
  // Heightmap data is copied (not moved), so the moved-from chunk keeps its
  // heightmap values. They are stale but safe to read.
}

Chunk::~Chunk() { cleanup(); }

void Chunk::generateHeightMapGPU(const glm::vec2 &position, uint32_t slotOffset,
                                 Shader &computeShader) {
  // Stage 1: dispatch only. The compute shader writes this chunk's slot in
  // the batched SSBO; readback is deferred to finishHeightMapGPU() so the
  // main thread never stalls on a full pipeline sync per chunk.
  m_GpuHeightMapReady = false;

  computeShader.use();
  computeShader.setUniformVec2("uChunkPos", position);
  computeShader.setUniformUInt("uSlot", slotOffset);
  computeShader.setUniformFloat("uFrequency",
                                 Constants::Noise::FREQUENCY);
  computeShader.setUniformUInt("uMaxHeight",
                                static_cast<uint32_t>(Constants::Chunk::MAX_BLOCK_HEIGHT));
  computeShader.setUniformUInt("uExtSide", kExtSide);
  computeShader.setUniformUInt("uSeed", Noise::getSeed());
  computeShader.setUniformInt("uOctaves", Constants::Noise::FRACTAL_OCTAVE);
  computeShader.setUniformFloat("uGain", Constants::Noise::FRACTAL_GAIN);
  computeShader.setUniformFloat("uLacunarity",
                                 Constants::Noise::FRACTAL_LACUNARITY);

  // Dispatch enough groups to cover kExtSide x kExtSide.
  // local_size is 16x16, so we need ceil(kExtSide / 16) groups per axis.
  constexpr uint32_t kLocalSize = 16;
  uint32_t groups = (kExtSide + kLocalSize - 1) / kLocalSize;
  computeShader.dispatch(groups, groups, 1);
}

void Chunk::finishHeightMapGPU(uint32_t slotOffset, IBuffer &ssbo) {
  if (m_GpuHeightMapReady) {
    return;
  }

  // Stage 2: read back this chunk's slot of the batched SSBO. Called
  // deferred (at least one frame after dispatch) so the GPU has usually
  // already finished. The renderer issues the GL_BUFFER_UPDATE barrier at the
  // read site itself, so the visibility guarantee no longer depends on
  // dispatchCompute happening to do the right thing.
  size_t byteCount = kExtSide * kExtSide * sizeof(uint32_t);
  size_t slotBytes = static_cast<size_t>(slotOffset) * byteCount;

  // A member rather than a local: resize is a no-op once it has reached
  // kExtSide*kExtSide, so steady-state streaming no longer allocates and frees
  // 1296 bytes per chunk per readback (#48 tracks the broader allocator
  // pressure in this pipeline).
  m_ReadbackScratch.resize(kExtSide * kExtSide);

  if (!m_Renderer->getBufferSubData(ssbo, slotBytes, byteCount,
                                    m_ReadbackScratch.data())) {
    // Leave m_GpuHeightMapReady false so this is retried on a later frame.
    // Consuming the buffer anyway would truncate whatever the allocator
    // handed back into uint16_t and install it as authoritative terrain, with
    // m_GpuHeightMapReady = true guaranteeing the bad heights are never
    // re-derived. A readback failure is transient (context recovery, a
    // configuration mismatch), so retrying is also what turns a silent
    // permanent corruption into a visible, debuggable symptom.
    return;
  }

  const std::vector<uint32_t> &gpuHeights = m_ReadbackScratch;

  for (uint32_t ex = 0; ex < kExtSide; ex++) {
    for (uint32_t ez = 0; ez < kExtSide; ez++) {
      m_ExtendedHeightMap[ex][ez] =
          static_cast<uint16_t>(gpuHeights[ez * kExtSide + ex]);
    }
  }

  // Copy the interior into the regular height map (same as CPU path).
  for (int32_t x = 0; x < Constants::Chunk::LENGTH; ++x) {
    for (int32_t z = 0; z < Constants::Chunk::LENGTH; ++z) {
      m_HeightMap[x][z] = m_ExtendedHeightMap[static_cast<uint32_t>(x + 1)]
                                           [static_cast<uint32_t>(z + 1)];
    }
  }

  m_GpuHeightMapReady = true;
}

void Chunk::generateMeshData(const glm::ivec2 &position,
                             std::uint32_t sampleStep) {
  generateHeightMapCPU(position);
  generateMesh(sampleStep);
}

void Chunk::generateHeightMapCPU(const glm::ivec2 &position) {
  const float baseX = static_cast<float>(position.x) * static_cast<float>(Constants::Chunk::LENGTH);
  const float baseZ = static_cast<float>(position.y) * static_cast<float>(Constants::Chunk::LENGTH);

  auto sampleGrassHeightWorld = [&](float worldBlockX, float worldBlockZ) {
    return Heightmap::sampleWorld(worldBlockX, worldBlockZ);
  };

  for (uint32_t ex = 0; ex < kExtSide; ex++) {
    for (uint32_t ez = 0; ez < kExtSide; ez++) {
      const float noiseX =
          baseX + static_cast<float>(static_cast<int32_t>(ex) - 1);
      const float noiseZ =
          baseZ + static_cast<float>(static_cast<int32_t>(ez) - 1);
      m_ExtendedHeightMap[ex][ez] =
          sampleGrassHeightWorld(noiseX, noiseZ);
    }
  }

  for (int32_t x = 0; x < Constants::Chunk::LENGTH; ++x) {
    for (int32_t z = 0; z < Constants::Chunk::LENGTH; ++z) {
      m_HeightMap[x][z] = m_ExtendedHeightMap[static_cast<uint32_t>(x + 1)]
                                         [static_cast<uint32_t>(z + 1)];
    }
  }
}

void Chunk::generateMesh(std::uint32_t sampleStep) {
  // The LOD stride. 1 is the original full-resolution mesher and must produce
  // byte-identical output, because everything inside FOG_START -- the whole
  // visible foreground -- is at stride 1 and a regression there would be a
  // regression in the terrain the player is standing on. The stride divides
  // LENGTH exactly, so no lattice block is ever partial.
  const auto step = static_cast<int32_t>(sampleStep);
  const int32_t BX = Constants::Chunk::LENGTH;
  const size_t BY = static_cast<size_t>(Constants::Chunk::HEIGHT);
  const int32_t BZ = Constants::Chunk::LENGTH;
  // Lattice nodes per axis. At step 1 this is 16, the column count the
  // original mesher walked, and every node maps to exactly one column.
  const int32_t NX = latticeSide(sampleStep);
  const int32_t NZ = latticeSide(sampleStep);

  // Lattice height at a node: the MAXIMUM over the step x step block of columns
  // the node stands for.
  //
  // Max, not min or mean, and the reason is holes. A coarse node whose surface
  // sat below the true surface would let the neighbouring finer node's side face
  // be drawn where it should be hidden, and -- worse -- a coarse node lower than
  // its neighbour leaves a gap between the two chunk meshes along the seam.
  // Taking the max means a coarse surface is never below anything it covers, so
  // a step-2 chunk always hides the terrain a step-1 chunk would have shown
  // underneath it. It can over-represent a peak by up to one block, which fog
  // covers at the distances these tiers apply at.
  auto latticeHeight = [&](int32_t nx, int32_t nz) -> uint16_t {
    uint16_t highest = 0;
    for (int32_t x = nx * step; x < nx * step + step; ++x) {
      for (int32_t z = nz * step; z < nz * step + step; ++z) {
        highest = std::max(highest, m_HeightMap[x][z]);
      }
    }
    return highest;
  };

  // Skirt depth for this chunk's border faces.
  //
  // A chunk at stride 2 and its neighbour at stride 4 sample different
  // lattices, so along the seam between them the two surfaces are not the same
  // polygon and a crack opens wherever the coarse side is lower. Extruding the
  // chunk's outer border straight down by at least the chunk's own height range
  // closes it: the two skirts overlap for at least the full relief of the
  // terrain, so no gap can show through.
  //
  // The relief, not a constant, because a constant small enough to be
  // unobtrusive on flat ground is far too small next to a cliff, and a constant
  // large enough for a cliff is a wall of geometry hanging off every flat
  // chunk. Capped at MAX_BLOCK_HEIGHT so it cannot run away.
  uint16_t chunkMinHeight = 0xFFFF;
  uint16_t chunkMaxHeight = 0;
  for (int32_t x = 0; x < BX; ++x) {
    for (int32_t z = 0; z < BZ; ++z) {
      chunkMinHeight = std::min(chunkMinHeight, m_HeightMap[x][z]);
      chunkMaxHeight = std::max(chunkMaxHeight, m_HeightMap[x][z]);
    }
  }
  const int32_t relief = static_cast<int32_t>(chunkMaxHeight) -
                         static_cast<int32_t>(chunkMinHeight);
  const int32_t skirt = std::min(relief, static_cast<int32_t>(
                                              Constants::Chunk::MAX_BLOCK_HEIGHT)) + 1;

  m_Data.clear();
  m_Indices.clear();

  // Reserve up front so the inner push_back loops cannot reallocate
  // mid-mesh. A reallocation there is where std::bad_alloc comes from, and
  // the mesher runs on a worker thread where an escaping exception used to
  // take the whole process down (#141).
  //
  // The absolute worst case is BX*BZ*BY quads (every column exposed at every
  // Y level), which for 16x16x256 is 65536 quads -- 3.7 MB of vertex and index
  // storage per chunk, times up to 4225 resident chunks. Reserving that is
  // far worse than the reallocations it avoids.
  //
  // What is actually emitted is the *surface* of the terrain: one top quad
  // per column, one bottom quad per column, and side quads down the height
  // differences between neighbouring columns. For a heightmap-driven world
  // that is bounded by a small multiple of the column count plus the column
  // heights, not by the volume, so reserve from that instead. Over-reserving
  // costs one allocation of capacity; under-reserving costs a geometric
  // reallocation, which is the thing being avoided.
  // Scoped to the lattice, not the chunk: at stride 4 a chunk emits 16 columns
  // worth of quads, and reserving for 256 would allocate 16x the memory for every
  // in-flight chunk (up to kMaxPendingTasks of them) to hold a mesh that is
  // 16x smaller. The skirt needs room too, hence the explicit term.
  const size_t columns = static_cast<size_t>(NX) * static_cast<size_t>(NZ);
  // Top + bottom per column, plus a margin for side faces on uneven terrain,
  // plus a floor so a flat world does not reserve almost nothing and then
  // grow anyway. Four extra columns' worth covers the chunk's four skirt faces.
  constexpr size_t kQuadsPerColumn = 8;
  const size_t reserveQuads = columns * kQuadsPerColumn + columns + 4;
  m_Data.reserve(reserveQuads * 4);
  m_Indices.reserve(reserveQuads * 6);

  // NOTE: `n` is accepted but ignored, so every face of a block currently
  // resolves to the same texture layer. Making this honor BlockNormal is
  // tracked in #60 (M14) and is deliberately not fixed here.
  auto blockTextureId = [&](BlockType t, [[maybe_unused]] BlockNormal n) -> int32_t {
    switch (t) {
    case BlockType::GRASS:
      return 0;
    case BlockType::DIRT:
      return 1;
    case BlockType::WATER:
      return 2;
    default:
      return 1;
    }
  };

  auto addQuad = [&](const glm::ivec3 &a, const glm::ivec3 &du,
                     const glm::ivec3 &dv, BlockNormal normalId, int32_t texId,
                     bool flipV) {
    int32_t x = a.x;
    int32_t y = a.y;
    int32_t z = a.z;

    int32_t duX = du.x, duY = du.y, duZ = du.z;
    int32_t dvX = dv.x, dvY = dv.y, dvZ = dv.z;

    size_t base = m_Data.size();

    auto pushVertex = [&](int32_t bx, int32_t by, int32_t bz, float uvX,
                          float uvY) {
      uint32_t corner = 0;
      if (uvX > 0.0f)
        corner |= 1;
      if (uvY > 0.0f)
        corner |= 2;

      PackedVertex v;
      v.bits = 0;
      v.f.x = static_cast<uint32_t>(bx & 0xFF);
      v.f.z = static_cast<uint32_t>(bz & 0xFF);
      v.f.y = static_cast<uint32_t>(by & 0x3FF);
      v.f.normal = static_cast<uint32_t>(normalId);
      v.f.texId = static_cast<uint32_t>(texId & 3);
      v.f.corner = corner;
      m_Data.push_back(v);
    };

    float width =
        static_cast<float>(std::abs(duX) + std::abs(duY) + std::abs(duZ));
    float height =
        static_cast<float>(std::abs(dvX) + std::abs(dvY) + std::abs(dvZ));

    if (!flipV) {
      pushVertex(x, y, z, 0.0f, 0.0f);
      pushVertex(x + duX, y + duY, z + duZ, width, 0.0f);
      pushVertex(x + dvX, y + dvY, z + dvZ, 0.0f, height);
      pushVertex(x + duX + dvX, y + duY + dvY, z + duZ + dvZ, width,
                 height);
    } else {
      pushVertex(x, y, z, 0.0f, height);
      pushVertex(x + duX, y + duY, z + duZ, width, height);
      pushVertex(x + dvX, y + dvY, z + dvZ, 0.0f, 0.0f);
      pushVertex(x + duX + dvX, y + duY + dvY, z + duZ + dvZ, width, 0.0f);
    }

    const uint32_t b0 = static_cast<uint32_t>(base + 0);
    const uint32_t b1 = static_cast<uint32_t>(base + 1);
    const uint32_t b2 = static_cast<uint32_t>(base + 2);
    const uint32_t b3 = static_cast<uint32_t>(base + 3);

    m_Indices.push_back(b0);
    m_Indices.push_back(b2);
    m_Indices.push_back(b1);
    m_Indices.push_back(b1);
    m_Indices.push_back(b2);
    m_Indices.push_back(b3);
  };

  // Neighbour node height in one of the four horizontal directions, or the
  // extended heightmap's value where the neighbour is outside this chunk.
  //
  // Outside the chunk the halo is read at FULL resolution and maxed over the
  // step columns, because the neighbour chunk is free to be at a different
  // stride: its border is a fine column whatever this chunk's lattice says, and
  // sampling the halo at this chunk's stride would lose the peak that decides
  // whether a side face is needed at all.
  auto neighbourHeight = [&](int32_t nx, int32_t nz, int dir) -> uint16_t {
    int32_t fx = nx * step;
    int32_t fz = nz * step;
    switch (dir) {
    case 0: // +X
      ++nx;
      if (nx < NX) {
        return latticeHeight(nx, nz);
      }
      fx = BX;
      break;
    case 1: // -X
      --nx;
      if (nx >= 0) {
        return latticeHeight(nx, nz);
      }
      fx = -1;
      break;
    case 4: // +Z
      ++nz;
      if (nz < NZ) {
        return latticeHeight(nx, nz);
      }
      fz = BZ;
      break;
    default: // -Z
      --nz;
      if (nz >= 0) {
        return latticeHeight(nx, nz);
      }
      fz = -1;
      break;
    }
    // fx / fz now name a halo column; the other axis walks the node's columns.
    const uint32_t ex = static_cast<uint32_t>(fx + 1);
    uint16_t highest = 0;
    const bool xIsHalo = (fx == -1 || fx == BX);
    const bool zIsHalo = (fz == -1 || fz == BZ);
    const int32_t xStart = xIsHalo ? 0 : fx;
    const int32_t zStart = zIsHalo ? 0 : fz;
    for (int32_t dx = 0; dx < step; ++dx) {
      for (int32_t dz = 0; dz < step; ++dz) {
        const uint32_t hx =
            xIsHalo ? ex : static_cast<uint32_t>(xStart + dx + 1);
        const uint32_t hz =
            zIsHalo ? static_cast<uint32_t>(fz + 1) : static_cast<uint32_t>(zStart + dz + 1);
        highest = std::max(highest, m_ExtendedHeightMap[hx][hz]);
      }
    }
    return highest;
  };

  for (int32_t nx = 0; nx < NX; ++nx) {
    for (int32_t nz = 0; nz < NZ; ++nz) {
      const int32_t x = nx * step;
      const int32_t z = nz * step;
      const int32_t height = static_cast<int32_t>(latticeHeight(nx, nz));

      // Horizontal faces. Four directions rather than six: the vertical ones are
      // emitted once per node instead of once per y level, which is the entire
      // saving at this level of the hierarchy.
      //
      // The side stack runs from the node's own surface down to the neighbour's,
      // and on the chunk's outer border down past y = 0 to the skirt, so the
      // seam between chunks at different strides cannot show through.
      for (const int dir : {0, 1, 4, 5}) {
        const int32_t neighbour =
            static_cast<int32_t>(neighbourHeight(nx, nz, dir));
        const bool onChunkBorder = (dir == 0 && nx == NX - 1) ||
                                   (dir == 1 && nx == 0) ||
                                   (dir == 4 && nz == NZ - 1) ||
                                   (dir == 5 && nz == 0);
        const int32_t lowest =
            onChunkBorder ? std::min(-1, neighbour - skirt) : neighbour;
        if (height <= lowest) {
          continue;
        }
        const FaceWinding winding = faceWindingForDirection(dir);
        const glm::ivec3 du{winding.duX * step, winding.duY * step,
                            winding.duZ * step};
        const glm::ivec3 dv{winding.dvX * step, winding.dvY * step,
                            winding.dvZ * step};
        glm::ivec3 a(x, height, z);
        if (dir == 0) {
          a.x = x + step;
        } else if (dir == 4) {
          a.z = z + step;
        }
        const bool flipV = dir == 5;
        // GRASS at the top course of the stack, DIRT below, matching the
        // per-column mesher this replaces.
        for (int32_t y = height; y > lowest; --y) {
          const BlockType cur =
              (y == height) ? BlockType::GRASS : BlockType::DIRT;
          addQuad(a, du, dv,
                  winding.outX > 0 ? BlockNormal::RIGHT_LEFT_NORMAL
                                   : BlockNormal::FRONT_BACK_NORMAL,
                  blockTextureId(cur, BlockNormal::TOP_NORMAL), flipV);
          a.y = y - 1;
        }
      }

      // Top face: one quad spanning the whole step x step block.
      addQuad(glm::ivec3(x, height, z), glm::ivec3(step, 0, 0),
              glm::ivec3(0, 0, step), BlockNormal::TOP_NORMAL,
              blockTextureId(BlockType::GRASS, BlockNormal::TOP_NORMAL), false);

      // Bottom face at y = 0, as the per-column mesher emitted: one per column,
      // so one per step x step block here.
      addQuad(glm::ivec3(x, 0, z), glm::ivec3(step, 0, 0),
              glm::ivec3(0, 0, step), BlockNormal::BOTTOM_NORMAL,
              blockTextureId(BlockType::DIRT, BlockNormal::BOTTOM_NORMAL),
              false);
    }
  }

  // --- Water surface --------------------------------------------------------
  // Columns whose terrain surface sits below WATER_LEVEL get a flat opaque water
  // top face at WATER_LEVEL, textured with the WATER (blue) array layer. It is
  // added to the SAME mesh as the terrain, so it is drawn by the scene shader
  // with no custom program. Submerged faces are omitted (hidden by the opaque
  // surface above) to keep the mesh thin.
  //
  // A lattice node gets a water quad only when the WHOLE step x step block is
  // below the line, tested on the block maximum. A partially-submerged node is
  // left dry rather than being covered by a full-width water quad, because that
  // quad would be drawn over the part of the block that is above water and
  // would hide terrain. At stride 1 this is exactly the original per-column
  // rule; at a coarser stride it under-covers water at the shoreline of a
  // submerged block, which is only reachable at the distances those tiers
  // apply at.
  for (int32_t nx = 0; nx < NX; ++nx) {
    for (int32_t nz = 0; nz < NZ; ++nz) {
      if (latticeHeight(nx, nz) >= Constants::Chunk::WATER_LEVEL) {
        continue;
      }
      const int32_t topY = Constants::Chunk::WATER_LEVEL;
      glm::ivec3 a(nx * step, topY, nz * step);
      addQuad(a, {step, 0, 0}, {0, 0, step}, BlockNormal::TOP_NORMAL,
              static_cast<int32_t>(BlockType::WATER), false);
    }
  }
}

void Chunk::pass() {
  if (!m_Renderer) return;

  m_VAO = m_Renderer->createVertexArray();
  m_VBO = m_Renderer->createBuffer(BufferType::Vertex);
  m_EBO = m_Renderer->createBuffer(BufferType::Index);

  m_Renderer->bindVertexArray(*m_VAO);

  m_VBO->bind();
  m_Renderer->setBufferData(*m_VBO, m_Data.data(),
                            m_Data.size() * sizeof(PackedVertex),
                            BufferUsage::Static);

  m_EBO->bind();
  m_Renderer->setBufferData(*m_EBO, m_Indices.data(),
                            m_Indices.size() * sizeof(uint32_t),
                            BufferUsage::Static);

  m_IndexCount = static_cast<uint32_t>(m_Indices.size());
  m_VboSize = static_cast<uint32_t>(m_Data.size());

  m_Data.clear();
  m_Indices.clear();
  m_Data.shrink_to_fit();
  m_Indices.shrink_to_fit();

  m_Renderer->setVertexAttribute(*m_VAO, 0, 1, DataType::UnsignedInt, false,
                                sizeof(PackedVertex), 0);
  m_Renderer->enableVertexAttribute(*m_VAO, 0);
}

void Chunk::cleanup() {
  m_VBO.reset();
  m_EBO.reset();
  m_VAO.reset();
  m_IndexCount = 0;
  m_VboSize = 0;
}

void Chunk::render() {
  if (m_VAO && m_Renderer) {
    m_Renderer->bindVertexArray(*m_VAO);
    m_Renderer->drawIndexed(PrimitiveType::Triangles, m_IndexCount, 0,
                            IndexType::UnsignedInt);
  }
}

uint16_t Chunk::getHighestBlockY(uint32_t blockX, uint32_t blockZ) const {
  // The heightmap is exactly LENGTH x LENGTH, so anything outside that is
  // already past the end of the array. Clamping would quietly answer with a
  // neighbouring column's height, which is worse than failing.
  assert(blockX < static_cast<uint32_t>(Constants::Chunk::LENGTH) &&
         blockZ < static_cast<uint32_t>(Constants::Chunk::LENGTH) &&
         "getHighestBlockY called with an out-of-range block coordinate");
  return m_HeightMap[blockX][blockZ];
}