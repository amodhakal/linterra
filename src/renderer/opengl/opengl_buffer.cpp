#include "opengl_buffer.hpp"

#include <glad/glad.h>

OpenGLBuffer::OpenGLBuffer(BufferType type) : m_Type(type) {
  glGenBuffers(1, &m_Id);
}

OpenGLBuffer::~OpenGLBuffer() {
  if (m_Id != 0) {
    glDeleteBuffers(1, &m_Id);
    m_Id = 0;
  }
}

OpenGLBuffer::OpenGLBuffer(OpenGLBuffer&& other) noexcept
    : m_Id(other.m_Id), m_Type(other.m_Type) {
  other.m_Id = 0;
}

OpenGLBuffer& OpenGLBuffer::operator=(OpenGLBuffer&& other) noexcept {
  if (this != &other) {
    if (m_Id != 0) {
      glDeleteBuffers(1, &m_Id);
    }
    m_Id = other.m_Id;
    m_Type = other.m_Type;
    other.m_Id = 0;
  }
  return *this;
}

void OpenGLBuffer::bind() {
  GLenum target = (m_Type == BufferType::Index) ? GL_ELEMENT_ARRAY_BUFFER
                : (m_Type == BufferType::Storage) ? GL_SHADER_STORAGE_BUFFER
                : GL_ARRAY_BUFFER;
  glBindBuffer(target, m_Id);
}

void OpenGLBuffer::unbind() {
  GLenum target = (m_Type == BufferType::Index) ? GL_ELEMENT_ARRAY_BUFFER
                : (m_Type == BufferType::Storage) ? GL_SHADER_STORAGE_BUFFER
                : GL_ARRAY_BUFFER;
  glBindBuffer(target, 0);
}