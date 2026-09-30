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
  size_t getSize() const override { return m_Size; }

  // Records the allocation made by glBufferData so getBufferSubData can
  // bounds-check before it reaches the driver.
  void setSize(size_t size) { m_Size = size; }

 private:
  uint32_t m_Id = 0;
  BufferType m_Type;
  // Last size passed to glBufferData. Not queried from GL, which has no
  // portable way to ask; it mirrors what this class asked for.
  size_t m_Size = 0;
};