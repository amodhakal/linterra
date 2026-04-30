#pragma once
#define GL_SILENCE_DEPRECATION
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <sys/types.h>

#include <glm/glm.hpp>

namespace Constants {
constexpr auto VERTEX_PATH = "./shaders/shaders.vert";
constexpr auto FRAGMENT_PATH = "./shaders/shaders.frag";

constexpr uint SCR_WIDTH = 800;
constexpr uint SCR_HEIGHT = 600;

constexpr auto BG_COLOR = glm::vec4(0.3, 0.5, 0.6, 1.0);
constexpr auto FOG_COLOR = glm::vec3(0.3, 0.5, 0.6);

constexpr bool DO_TRIANGLE_LINE = false;
constexpr bool DO_GRAVITY = false;

namespace Camera {
constexpr float JUMP_VELOCTY = 15.0;
constexpr float ACCELERATION = 50.0;
constexpr float MAX_VELOCITY = 20.0;
constexpr float SPEED = 20.5;
constexpr float SENSITIVITY = 0.2;
constexpr float NEAR = 0.1;
constexpr float FAR = 1000.0;

constexpr float DEFAULT_FOV = 45.0;
constexpr float DEFAULT_YAW = -90.0;
constexpr float DEAULT_PITCH = 0.0;

constexpr float PITCH_MAX = 89.0;
constexpr float PITCH_MIN = -89.0;
constexpr float FOV_MIN = 1.0;
constexpr float FOV_MAX = 45.0;

constexpr glm::vec3 DEFAULT_POSITION = {5, 155, 5};
constexpr glm::vec3 DEFAULT_FRONT = {0, 0, -1};
constexpr glm::vec3 DEFAULT_UP = {0, 1, 0};
}  // namespace Camera

namespace Chunk {
constexpr int LENGTH = 16;
constexpr int HEIGHT = 256;
constexpr int RENDER_DISTANCE_CHUNKS = 2;
constexpr int RENDER_DISTANCE_BLOCKS = RENDER_DISTANCE_CHUNKS * LENGTH;
  constexpr float FOG_START = RENDER_DISTANCE_BLOCKS / 2.0f;
  constexpr float FOG_END = RENDER_DISTANCE_BLOCKS;
constexpr int MAX_BLOCK_HEIGHT = HEIGHT / 1.5;
constexpr int MAX_GENERATION_THREADS = 100;
}  // namespace Chunk

namespace Noise {
constexpr int FRACTAL_OCTAVE = 8;
constexpr float FRACTAL_GAIN = 0.4f;
constexpr float FRACTAL_LACUNARITY = 2.0f;
constexpr float FREQUENCY = 0.005f;

}  // namespace Noise

}  // namespace Constants
