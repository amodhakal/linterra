#include "renderer.hpp"

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