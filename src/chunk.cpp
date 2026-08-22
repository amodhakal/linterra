#include "chunk.h"
#include <noise/noise.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>

#include "config.h"
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

void Chunk::generateHeightMapGPU(const glm::ivec2 &position,
                                  Shader &computeShader, IBuffer &ssbo) {
  // Dispatch the compute shader to fill the SSBO with height values.
  // The SSBO is already bound at binding point 0 by the caller.
  computeShader.use();
  computeShader.setUniformVec2(
      "uChunkPos",
      glm::vec2(static_cast<float>(position.x), static_cast<float>(position.y)));
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

  // Read back the SSBO into the extended height map.
  size_t byteCount = kExtSide * kExtSide * sizeof(uint32_t);
  std::vector<uint32_t> gpuHeights(kExtSide * kExtSide);
  m_Renderer->getBufferSubData(ssbo, 0, byteCount, gpuHeights.data());

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
}

void Chunk::generateMeshData(const glm::ivec2 &position) {
  generateHeightMapCPU(position);
  generateMesh();
}

void Chunk::generateHeightMapCPU(const glm::ivec2 &position) {
  const float baseX = static_cast<float>(position.x) * static_cast<float>(Constants::Chunk::LENGTH);
  const float baseZ = static_cast<float>(position.y) * static_cast<float>(Constants::Chunk::LENGTH);

  auto sampleGrassHeightWorld = [&](float worldBlockX, float worldBlockZ) -> uint16_t {
    auto n = Noise::fbm(
        glm::vec2(worldBlockX, worldBlockZ) * Constants::Noise::FREQUENCY,
        Constants::Noise::FRACTAL_OCTAVE, Constants::Noise::FRACTAL_LACUNARITY,
        Constants::Noise::FRACTAL_GAIN);
    float noiseY = n.value;
    noiseY /= 2.0f;
    noiseY += 0.5f;

    return static_cast<uint16_t>(
        std::floor(noiseY * static_cast<float>(Constants::Chunk::MAX_BLOCK_HEIGHT)));
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

void Chunk::generateMesh() {
  const int32_t BX = Constants::Chunk::LENGTH;
  const size_t BY = static_cast<size_t>(Constants::Chunk::HEIGHT);
  const int32_t BZ = Constants::Chunk::LENGTH;

  m_Data.clear();
  m_Indices.clear();

  auto blockTextureId = [&](BlockType t, BlockNormal n) -> int32_t {
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
      v.x = static_cast<uint32_t>(bx & 0xFF);
      v.z = static_cast<uint32_t>(bz & 0xFF);
      v.y = static_cast<uint32_t>(by & 0x3FF);
      v.normal = static_cast<uint32_t>(normalId);
      v.texId = static_cast<uint32_t>(texId & 3);
      v.corner = corner;
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

  auto isBlockExposed = [&](int32_t x, int32_t y, int32_t z, int32_t dir) -> bool {
    int32_t nx = x, ny = y, nz = z;
    switch (dir) {
    case 0:
      nx = x + 1;
      break;
    case 1:
      nx = x - 1;
      break;
    case 2:
      ny = y + 1;
      break;
    case 3:
      ny = y - 1;
      break;
    case 4:
      nz = z + 1;
      break;
    case 5:
      nz = z - 1;
      break;
    }

    if (ny < 0 || ny >= static_cast<int32_t>(BY)) {
      return true;
    }

    // Halo lookup: local coords in [-1, LENGTH] map to [0, kExtSide).
    assert(nx >= -1 && nx <= static_cast<int32_t>(Constants::Chunk::LENGTH));
    assert(nz >= -1 && nz <= static_cast<int32_t>(Constants::Chunk::LENGTH));

    const uint32_t ex = static_cast<uint32_t>(nx + 1);
    const uint32_t ez = static_cast<uint32_t>(nz + 1);
    const uint16_t neighborHeight = m_ExtendedHeightMap[ex][ez];

    return static_cast<uint16_t>(ny) >= neighborHeight;
  };

  for (int32_t x = 0; x < BX; ++x) {
    for (int32_t z = 0; z < BZ; ++z) {
      uint16_t height = m_HeightMap[x][z];
      for (int32_t y = static_cast<int32_t>(height); y >= 0; --y) {
        bool hasExposedFace = false;

        for (int32_t d = 0; d < 6; ++d) {
          if (!isBlockExposed(x, y, z, d))
            continue;

          hasExposedFace = true;

          BlockType cur = (y == static_cast<int32_t>(height)) ? BlockType::GRASS
                                                             : BlockType::DIRT;

          glm::ivec3 a(x, y, z);
          glm::ivec3 du(0, 0, 0), dv(0, 0, 0);
          BlockNormal normalId;
          bool flipV = false;

          switch (d) {
  case 0:
    a.x = x + 1;
    du = {0, 0, 1};
    dv = {0, 1, 0};
    normalId = BlockNormal::RIGHT_LEFT_NORMAL;
    break;
          case 1:
            du = {0, 1, 0};
            dv = {0, 0, 1};
            normalId = BlockNormal::RIGHT_LEFT_NORMAL;
            break;
          case 2:
            a.y = y + 1;
            du = {1, 0, 0};
            dv = {0, 0, 1};
            normalId = BlockNormal::TOP_NORMAL;
            break;
          case 3:
            if (y == 0)
              continue;
            du = {1, 0, 0};
            dv = {0, 0, 1};
            normalId = BlockNormal::BOTTOM_NORMAL;
            break;
  case 4:
    a.z = z + 1;
    du = {0, 1, 0};
    dv = {1, 0, 0};
    normalId = BlockNormal::FRONT_BACK_NORMAL;
    break;
          case 5:
            du = {1, 0, 0};
            dv = {0, 1, 0};
            normalId = BlockNormal::FRONT_BACK_NORMAL;
            flipV = true;
            break;
          }

          int32_t texId = blockTextureId(cur, normalId);
          addQuad(a, du, dv, normalId, texId, flipV);
        }

        if (!hasExposedFace)
          break;
      }
    }
  }

  // --- Water surface --------------------------------------------------------
  // Columns whose terrain surface sits below WATER_LEVEL get a flat opaque water
  // top face at WATER_LEVEL, textured with the WATER (blue) array layer. It is
  // added to the SAME mesh as the terrain, so it is drawn by the scene shader
  // with no custom program. Submerged faces are omitted (hidden by the opaque
  // surface above) to keep the mesh thin.
  for (int32_t x = 0; x < BX; ++x) {
    for (int32_t z = 0; z < BZ; ++z) {
      const uint16_t surface = m_HeightMap[x][z];
      if (surface >= Constants::Chunk::WATER_LEVEL) {
        continue;  // terrain already at/above the water line: no water here
      }
      const int32_t topY = Constants::Chunk::WATER_LEVEL;
      glm::ivec3 a(x, topY, z);
      addQuad(a, {1, 0, 0}, {0, 0, 1}, BlockNormal::TOP_NORMAL,
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

  if (Constants::DO_TRIANGLE_LINE) {
    m_Renderer->setPolygonMode(true);
  }
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

uint16_t Chunk::getHighestBlockY(uint32_t blockX, uint32_t blockZ) {
  return m_HeightMap[blockX][blockZ];
}