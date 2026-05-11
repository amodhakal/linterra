#include "opengl_vertex_array.hpp"

#include <glad/glad.h>

OpenGLVertexArray::OpenGLVertexArray() {
  glGenVertexArrays(1, &m_Id);
}

OpenGLVertexArray::~OpenGLVertexArray() {
  if (m_Id != 0) {
    glDeleteVertexArrays(1, &m_Id);
    m_Id = 0;
  }
}

OpenGLVertexArray::OpenGLVertexArray(OpenGLVertexArray&& other) noexcept
    : m_Id(other.m_Id) {
  other.m_Id = 0;
}

OpenGLVertexArray& OpenGLVertexArray::operator=(
    OpenGLVertexArray&& other) noexcept {
  if (this != &other) {
    if (m_Id != 0) {
      glDeleteVertexArrays(1, &m_Id);
    }
    m_Id = other.m_Id;
    other.m_Id = 0;
  }
  return *this;
}

void OpenGLVertexArray::bind() {
  glBindVertexArray(m_Id);
}

void OpenGLVertexArray::unbind() {
  glBindVertexArray(0);
}