#pragma once

#include <cstdint>
#include <memory>

#include "config.h"

// Offscreen render target. The scene pass renders into this framebuffer's
// color texture; the fog post-process pass then samples it. The scene color's
// alpha channel carries the view-space distance so the fog pass can reconstruct
// it without a separate depth-texture attachment.
//
// Owns its GL objects directly (FBO + 2D color texture + depth renderbuffer),
// matching the existing Texture/Shader pattern in this codebase.
class Framebuffer {
 public:
  Framebuffer();
  ~Framebuffer();

  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;
  Framebuffer(Framebuffer&& other) noexcept;
  Framebuffer& operator=(Framebuffer&& other) noexcept;

  void resize(std::uint32_t width, std::uint32_t height);

  // Bind as the draw target.
  void bind() const;
  // Bind the color texture to a sampler unit for the post-process pass.
  void bindColorTexture(std::int32_t unit) const;

  std::uint32_t getColorTextureId() const { return m_ColorTexture; }

  std::uint32_t getWidth() const { return m_Width; }
  std::uint32_t getHeight() const { return m_Height; }

 private:
  void destroy();

  std::uint32_t m_Fbo = 0;
  std::uint32_t m_ColorTexture = 0;
  std::uint32_t m_DepthRbo = 0;
  std::uint32_t m_Width = 0;
  std::uint32_t m_Height = 0;
};
