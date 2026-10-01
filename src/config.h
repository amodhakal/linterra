#pragma once

// GLAD/GLFW are only required by code that touches OpenGL directly. Guard them
// so pure subsystems (noise, camera math, frustum) can be compiled and tested
// without pulling in GL headers (e.g. the unit-test target).
#if !defined(LINTERRA_NO_OPENGL)
#define GL_SILENCE_DEPRECATION
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

// (no global type aliases here; use <cstdint> fixed-width types explicitly)

namespace Constants {
constexpr auto VERTEX_PATH = "./shaders/shaders.vert";
constexpr auto FRAGMENT_PATH = "./shaders/shaders.frag";
constexpr auto RENDER_VERTEX_PATH = "./shaders/render.vert";
constexpr auto RENDER_FRAGMENT_PATH = "./shaders/render.frag";
constexpr auto FOG_VERTEX_PATH = "./shaders/fog.vert";
constexpr auto FOG_FRAGMENT_PATH = "./shaders/fog.frag";
constexpr auto TERRAIN_COMPUTE_PATH = "./shaders/terrain.comp";

// Block and water textures. These live here rather than inline at the
// call site for two reasons: it is the one place that lists on-disk assets,
// and it makes the ./ prefix uniform with the shader paths above. The
// original spellings differed -- shaders as "./shaders/render.vert", textures
// as "resources/blocks/grass_top.png" -- which meant a reader could not tell
// that one class of path was CWD-independent and the other was not (#146).
//
// Every path here must be resolved through IO::resolvePath, not handed
// straight to an open() or fopen(). stbi_load, for one, opens with a plain
// fopen and so resolves against the working directory with no fallback.
constexpr auto GRASS_TOP_TEXTURE_PATH = "./resources/blocks/grass_top.png";
constexpr auto DIRT_TEXTURE_PATH = "./resources/blocks/dirt.png";
constexpr auto WATER_TEXTURE_PATH = "./resources/water.jpg";

// Every uniform the terrain compute shader declares. This is the single
// source of truth: ChunkManager registers exactly these names, and
// tests/test_shader_uniforms.cpp asserts the list stays equal to the set of
// uniforms declared in terrain.comp.
//
// It has to be one list rather than a call per uniform at the registration
// site because Shader::setUniform* silently drops a write to a name that was
// never registered. A name added to the GLSL without being added here is
// therefore invisible at runtime -- the shader keeps its default value and
// nothing reports an error. That is not hypothetical: uSlot was missing from
// this list, so every GPU chunk computed base == 0 and overwrote SSBO slot 0.
constexpr std::array<const char *, 9> TERRAIN_COMPUTE_UNIFORMS = {
    "uChunkPos", "uFrequency", "uMaxHeight", "uExtSide", "uSeed",
    "uOctaves",  "uGain",      "uLacunarity", "uSlot",
};

constexpr std::uint32_t SCR_WIDTH = 800;
constexpr std::uint32_t SCR_HEIGHT = 600;

constexpr auto BG_COLOR = glm::vec4(0.3, 0.5, 0.6, 1.0);
constexpr auto FOG_COLOR = glm::vec3(0.3, 0.5, 0.6);

constexpr bool DO_GRAVITY = false;

namespace Camera {
constexpr float JUMP_VELOCITY = 15.0f;
constexpr float ACCELERATION = 50.0f;
constexpr float MAX_VELOCITY = 20.0f;
constexpr float SPEED = 20.5f;
constexpr float SENSITIVITY = 0.2f;
constexpr float NEAR = 0.1f;
constexpr float FAR = 1000.0f;

constexpr float DEFAULT_FOV = 45.0f;
constexpr float DEFAULT_YAW = -90.0f;
constexpr float DEFAULT_PITCH = 0.0f;

constexpr float PITCH_MAX = 89.0f;
constexpr float PITCH_MIN = -89.0f;
constexpr float FOV_MIN = 1.0f;
constexpr float FOV_MAX = 45.0f;

constexpr glm::vec3 DEFAULT_POSITION = {5, 155, 5};
constexpr glm::vec3 DEFAULT_FRONT = {0, 0, -1};
constexpr glm::vec3 DEFAULT_UP = {0, 1, 0};
}  // namespace Camera

namespace Chunk {
constexpr std::int32_t LENGTH = 16;
constexpr std::int32_t HEIGHT = 256;
constexpr std::int32_t RENDER_DISTANCE_CHUNKS = 32;
constexpr std::int32_t RENDER_DISTANCE_BLOCKS = RENDER_DISTANCE_CHUNKS * LENGTH;
constexpr float FOG_START = static_cast<float>(RENDER_DISTANCE_BLOCKS) / 2.0f;
constexpr float FOG_END = static_cast<float>(RENDER_DISTANCE_BLOCKS);
constexpr std::int32_t MAX_BLOCK_HEIGHT =
    static_cast<std::int32_t>(HEIGHT / 1.5);
// Columns are filled with opaque WATER blocks from WATER_LEVEL down to the
// solid terrain surface, so lakes/pools read as flat water at this height.
constexpr std::int32_t WATER_LEVEL = 45;

/** True when a column with this terrain surface is under the water line.
 *
 *  This is the predicate Chunk::generateMesh's water pass uses, restated here
 *  so the mesher and the ground query read it from one place. It has to be one
 *  place: the mesher draws a flat, *opaque* water plane at WATER_LEVEL over
 *  every submerged column, into the same mesh as the terrain, so for those
 *  columns the surface a player can see and stand on is the water line and
 *  the terrain surface is up to 45 blocks below it and completely hidden
 *  (#134). Two copies of the rule would drift, and the drift is invisible
 *  until the player is standing inside an opaque blue box with no horizon and
 *  no way to tell why. */
[[nodiscard]] constexpr bool isSubmerged(std::uint16_t terrainHeight) {
  return terrainHeight < static_cast<std::uint16_t>(WATER_LEVEL);
}

/** The surface a player can stand on: the water line if the column is
 *  submerged, otherwise the terrain surface.
 *
 *  This is what the ground query answers with, not the raw heightmap value. */
[[nodiscard]] constexpr float walkableSurfaceY(std::uint16_t terrainHeight) {
  return isSubmerged(terrainHeight) ? static_cast<float>(WATER_LEVEL)
                                   : static_cast<float>(terrainHeight);
}

constexpr std::int32_t MAX_GENERATION_THREADS = 100;
}  // namespace Chunk

namespace Noise {
constexpr std::int32_t FRACTAL_OCTAVE = 8;
constexpr float FRACTAL_GAIN = 0.4f;
constexpr float FRACTAL_LACUNARITY = 2.0f;
constexpr float FREQUENCY = 0.005f;
#if defined(__APPLE__)
constexpr bool USE_GPU = false;
#else
constexpr bool USE_GPU = true;
#endif

}  // namespace Noise

}  // namespace Constants
