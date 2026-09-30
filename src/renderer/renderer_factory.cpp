#include "renderer.hpp"

#include "config.h"
#include "opengl/opengl_renderer.hpp"

std::unique_ptr<IRenderer> createRenderer(RenderBackend backend) {
  switch (backend) {
    case RenderBackend::OpenGL:
      return std::make_unique<OpenGLRenderer>();
    case RenderBackend::Metal:
      return nullptr;
    case RenderBackend::Vulkan:
      return nullptr;
  }
  return nullptr;
}

std::string_view describeGlError(std::uint32_t error) {
  switch (error) {
    case GL_INVALID_ENUM:
      return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
      return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
      return "GL_INVALID_OPERATION";
    case GL_STACK_OVERFLOW:
      return "GL_STACK_OVERFLOW";
    case GL_STACK_UNDERFLOW:
      return "GL_STACK_UNDERFLOW";
    case GL_OUT_OF_MEMORY:
      return "GL_OUT_OF_MEMORY";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_CONTEXT_LOST:
      return "GL_CONTEXT_LOST";
    default:
      return "Unknown GL error";
  }
}
