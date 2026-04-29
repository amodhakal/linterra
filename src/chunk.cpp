#include "chunk.h"
#include <noise/noise.h>

#include <cassert>
#include <cmath>

#include "config.h"

void Chunk::generateMeshData(const glm::vec2 &position) {
  const size_t BX = Constants::Chunk::LENGTH;
  const size_t BY = Constants::Chunk::HEIGHT;
  const size_t BZ = Constants::Chunk::LENGTH;

  auto getGrassHeight = [&](int bx, int bz) -> ushort {
    float baseX = position.s * Constants::Chunk::LENGTH;
    float baseZ = position.t * Constants::Chunk::LENGTH;
    float noiseX = baseX + bx;
    float noiseZ = baseZ + bz;

    auto n = Noise::fbm(
        glm::vec2(noiseX, noiseZ) * Constants::Noise::FREQUENCY,
        Constants::Noise::FRACTAL_OCTAVE, Constants::Noise::FRACTAL_LACUNARITY, Constants::Noise::FRACTAL_GAIN);
    float noiseY = n.value;
    noiseY /= 2.0f;
    noiseY += 0.5f;

    return static_cast<ushort>(
        std::floor(noiseY * Constants::Chunk::MAX_BLOCK_HEIGHT));
  };

  auto getNeighborHeight = [&](int nx, int nz) -> ushort {
    int worldX = static_cast<int>(position.s) * Constants::Chunk::LENGTH + nx;
    int worldZ = static_cast<int>(position.t) * Constants::Chunk::LENGTH + nz;

    auto n = Noise::fbm(
        glm::vec2(static_cast<float>(worldX), static_cast<float>(worldZ)) * Constants::Noise::FREQUENCY,
        Constants::Noise::FRACTAL_OCTAVE, Constants::Noise::FRACTAL_LACUNARITY, Constants::Noise::FRACTAL_GAIN);
    float noiseY = n.value;
    noiseY /= 2.0f;
    noiseY += 0.5f;

    return static_cast<ushort>(
        std::floor(noiseY * Constants::Chunk::MAX_BLOCK_HEIGHT));
  };

  for (uint blockX = 0; blockX < Constants::Chunk::LENGTH; blockX++) {
    for (uint blockZ = 0; blockZ < Constants::Chunk::LENGTH; blockZ++) {
      ushort grassHeight = getGrassHeight(blockX, blockZ);
      m_HeightMap[blockX][blockZ] = grassHeight;
    }
  }

  m_Data.clear();
  m_Indices.clear();

  // Map (block type, face normal) -> texture unit id
  auto blockTextureId = [&](BlockType t, BlockNormal n) -> int {
    switch (t) {
    case BlockType::GRASS:
      if (n == BlockNormal::TOP_NORMAL)
        return 0; // grass_top
      if (n == BlockNormal::BOTTOM_NORMAL)
        return 2; // dirt underside
      return 1;   // grass_side
    case BlockType::DIRT:
      return 2; // dirt
    default:
      return 2;
    }
  };

  auto addQuad = [&](const glm::ivec3 &a, const glm::ivec3 &du,
                     const glm::ivec3 &dv, BlockNormal normalId, int texId,
                     bool flipV) {
    glm::vec3 p0 = glm::vec3(static_cast<float>(a.x), static_cast<float>(a.y),
                             static_cast<float>(a.z));
    glm::vec3 p1 = glm::vec3(static_cast<float>(a.x + du.x),
                             static_cast<float>(a.y + du.y),
                             static_cast<float>(a.z + du.z));
    glm::vec3 p2 = glm::vec3(static_cast<float>(a.x + dv.x),
                             static_cast<float>(a.y + dv.y),
                             static_cast<float>(a.z + dv.z));
    glm::vec3 p3 = glm::vec3(static_cast<float>(a.x + du.x + dv.x),
                             static_cast<float>(a.y + du.y + dv.y),
                             static_cast<float>(a.z + du.z + dv.z));

    size_t base = m_Data.size() / 7;

    auto pushVertex = [&](const glm::vec3 &p, const glm::vec2 &uv) {
      m_Data.push_back(p.x);
      m_Data.push_back(p.y);
      m_Data.push_back(p.z);
      m_Data.push_back(static_cast<float>(normalId));
      m_Data.push_back(static_cast<float>(texId));
      m_Data.push_back(uv.x);
      m_Data.push_back(uv.y);
    };

    float width =
        static_cast<float>(std::abs(du.x) + std::abs(du.y) + std::abs(du.z));
    float height =
        static_cast<float>(std::abs(dv.x) + std::abs(dv.y) + std::abs(dv.z));

    if (!flipV) {
      pushVertex(p0, glm::vec2(0.0f, 0.0f));
      pushVertex(p1, glm::vec2(width, 0.0f));
      pushVertex(p2, glm::vec2(0.0f, height));
      pushVertex(p3, glm::vec2(width, height));
    } else {
      pushVertex(p0, glm::vec2(0.0f, height));
      pushVertex(p1, glm::vec2(width, height));
      pushVertex(p2, glm::vec2(0.0f, 0.0f));
      pushVertex(p3, glm::vec2(width, 0.0f));
    }

    m_Indices.push_back(static_cast<uint>(base + 0));
    m_Indices.push_back(static_cast<uint>(base + 2));
    m_Indices.push_back(static_cast<uint>(base + 1));
    m_Indices.push_back(static_cast<uint>(base + 1));
    m_Indices.push_back(static_cast<uint>(base + 2));
    m_Indices.push_back(static_cast<uint>(base + 3));
  };

auto isBlockExposed = [&](int x, int y, int z, int dir) -> bool {
    int nx = x, ny = y, nz = z;
    switch (dir) {
      case 0: nx = x + 1; break;
      case 1: nx = x - 1; break;
      case 2: ny = y + 1; break;
      case 3: ny = y - 1; break;
      case 4: nz = z + 1; break;
      case 5: nz = z - 1; break;
    }

    if (ny < 0 || ny >= static_cast<int>(BY)) {
      return true;
    }

    ushort neighborHeight;
    if (nx < 0 || nz < 0 || nx >= static_cast<int>(BX) ||
        nz >= static_cast<int>(BZ)) {
      neighborHeight = getNeighborHeight(nx, nz);
    } else {
      neighborHeight = m_HeightMap[nx][nz];
    }

    return static_cast<ushort>(ny) >= neighborHeight;
  };

  for (int x = 0; x < static_cast<int>(BX); ++x) {
    for (int z = 0; z < static_cast<int>(BZ); ++z) {
      ushort height = m_HeightMap[x][z];
      for (int y = height; y >= 0; --y) {
        bool hasExposedFace = false;

        for (int d = 0; d < 6; ++d) {
          if (!isBlockExposed(x, y, z, d)) continue;

          hasExposedFace = true;

          BlockType cur = (y == static_cast<int>(height)) ? BlockType::GRASS : BlockType::DIRT;

          glm::ivec3 a(x, y, z);
          glm::ivec3 du(0, 0, 0), dv(0, 0, 0);
          BlockNormal normalId;
          bool flipV = false;

          switch (d) {
            case 0:
              a.x = x + 1;
              du = {0, 1, 0};
              dv = {0, 0, 1};
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
              if (y == 0) continue;
              du = {1, 0, 0};
              dv = {0, 0, 1};
              normalId = BlockNormal::BOTTOM_NORMAL;
              break;
            case 4:
              a.z = z + 1;
              du = {1, 0, 0};
              dv = {0, 1, 0};
              normalId = BlockNormal::FRONT_BACK_NORMAL;
              break;
            case 5:
              du = {1, 0, 0};
              dv = {0, 1, 0};
              normalId = BlockNormal::FRONT_BACK_NORMAL;
              flipV = true;
              break;
          }

          int texId = blockTextureId(cur, normalId);
          addQuad(a, du, dv, normalId, texId, flipV);
        }

        if (!hasExposedFace) break;
      }
    }
  }
}

void Chunk::pass() {
  glGenVertexArrays(1, &m_VAO);
  glGenBuffers(1, &m_VBO);
  glGenBuffers(1, &m_EBO);

  glBindVertexArray(m_VAO);

  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, m_Data.size() * sizeof(float), m_Data.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Indices.size() * sizeof(uint),
               m_Indices.data(), GL_STATIC_DRAW);

  m_IndexCount = static_cast<uint>(m_Indices.size());
  m_VboSize = m_Data.size();

  m_Data.clear();
  m_Indices.clear();
  m_Data.shrink_to_fit();
  m_Indices.shrink_to_fit();

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                        (void *)(4 * sizeof(float)));
  glEnableVertexAttribArray(2);

  if (Constants::DO_TRIANGLE_LINE) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
}

void Chunk::cleanup() {
  glDeleteBuffers(1, &m_VBO);
  glDeleteBuffers(1, &m_EBO);
  glDeleteVertexArrays(1, &m_VAO);
}

void Chunk::render() {
  glBindVertexArray(m_VAO);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_IndexCount),
                 GL_UNSIGNED_INT, 0);
}

ushort Chunk::getHighestBlockY(uint blockX, uint blockZ) {
  return m_HeightMap[blockX][blockZ];
}
