#pragma once

#include "../renderer.hpp"

#include <cstdint>

class OpenGLTexture : public ITexture {
 public:
  explicit OpenGLTexture(TextureType type);
  ~OpenGLTexture() override;

  OpenGLTexture(const OpenGLTexture&) = delete;
  OpenGLTexture& operator=(const OpenGLTexture&) = delete;
  OpenGLTexture(OpenGLTexture&& other) noexcept;
  OpenGLTexture& operator=(OpenGLTexture&& other) noexcept;

  void bind(int unit) override;
  void unbind() override;
  uint32_t getId() const override { return m_Id; }

 private:
  uint32_t m_Id = 0;
  TextureType m_Type;
};