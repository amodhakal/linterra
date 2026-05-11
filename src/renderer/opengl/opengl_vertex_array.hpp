#pragma once

#include "../renderer.hpp"

#include <cstdint>

class OpenGLVertexArray : public IVertexArray {
 public:
  OpenGLVertexArray();
  ~OpenGLVertexArray() override;

  OpenGLVertexArray(const OpenGLVertexArray&) = delete;
  OpenGLVertexArray& operator=(const OpenGLVertexArray&) = delete;
  OpenGLVertexArray(OpenGLVertexArray&& other) noexcept;
  OpenGLVertexArray& operator=(OpenGLVertexArray&& other) noexcept;

  void bind() override;
  void unbind() override;
  uint32_t getId() const override { return m_Id; }

 private:
  uint32_t m_Id = 0;
};