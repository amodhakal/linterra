#pragma once

// GLAD/GLFW are only required by code that touches OpenGL directly. Guard them
// so pure subsystems (noise, camera math, frustum) can be compiled and tested
// without pulling in GL headers (e.g. the unit-test target).
#if !defined(LINTERRA_NO_OPENGL)
#define GL_SILENCE_DEPRECATION
#include <glad/glad.h>
#include <glfw/glfw3.h>
#endif

#include <cstdint>
#include <glm/glm.hpp>

// application.h signatures use uint; derive from fixed-width aliases.
using uint = std::uint32_t;

namespace Constants {
constexpr auto VERTEX_PATH = "./shaders/shaders.vert";
constexpr auto FRAGMENT_PATH = "./shaders/shaders.frag";
constexpr auto RENDER_VERTEX_PATH = "./shaders/render.vert";
constexpr auto RENDER_FRAGMENT_PATH = "./shaders/render.frag";
constexpr auto FOG_VERTEX_PATH = "./shaders/fog.vert";
constexpr auto FOG_FRAGMENT_PATH = "./shaders/fog.frag";
constexpr auto TERRAIN_COMPUTE_PATH = "./shaders/terrain.comp";

constexpr std::uint32_t SCR_WIDTH = 800;
constexpr std::uint32_t SCR_HEIGHT = 600;

constexpr auto BG_COLOR = glm::vec4(0.3, 0.5, 0.6, 1.0);
constexpr auto FOG_COLOR = glm::vec3(0.3, 0.5, 0.6);

constexpr bool DO_TRIANGLE_LINE = false;
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
