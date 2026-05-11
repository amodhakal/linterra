#pragma once

#include "../renderer.hpp"

#include <cstdint>

class OpenGLBuffer : public IBuffer {
 public:
  explicit OpenGLBuffer(BufferType type);
  ~OpenGLBuffer() override;

  OpenGLBuffer(const OpenGLBuffer&) = delete;
  OpenGLBuffer& operator=(const OpenGLBuffer&) = delete;
  OpenGLBuffer(OpenGLBuffer&& other) noexcept;
  OpenGLBuffer& operator=(OpenGLBuffer&& other) noexcept;

  void bind() override;
  void unbind() override;
  uint32_t getId() const override { return m_Id; }
  BufferType getType() const { return m_Type; }

 private:
  uint32_t m_Id = 0;
  BufferType m_Type;
};