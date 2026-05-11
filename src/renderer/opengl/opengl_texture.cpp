#include "opengl_texture.hpp"

#include <glad/glad.h>

namespace {

GLenum textureTypeToGL(TextureType type) {
  switch (type) {
    case TextureType::Texture2D:
      return GL_TEXTURE_2D;
    case TextureType::Texture2DArray:
      return GL_TEXTURE_2D_ARRAY;
    case TextureType::Texture3D:
      return GL_TEXTURE_3D;
    case TextureType::Cubemap:
      return GL_TEXTURE_CUBE_MAP;
  }
  return GL_TEXTURE_2D;
}

}  // namespace

OpenGLTexture::OpenGLTexture(TextureType type) : m_Type(type) {
  glGenTextures(1, &m_Id);
}

OpenGLTexture::~OpenGLTexture() {
  if (m_Id != 0) {
    glDeleteTextures(1, &m_Id);
    m_Id = 0;
  }
}

OpenGLTexture::OpenGLTexture(OpenGLTexture&& other) noexcept
    : m_Id(other.m_Id), m_Type(other.m_Type) {
  other.m_Id = 0;
}

OpenGLTexture& OpenGLTexture::operator=(OpenGLTexture&& other) noexcept {
  if (this != &other) {
    if (m_Id != 0) {
      glDeleteTextures(1, &m_Id);
    }
    m_Id = other.m_Id;
    m_Type = other.m_Type;
    other.m_Id = 0;
  }
  return *this;
}

void OpenGLTexture::bind(int unit) {
  glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
  glBindTexture(textureTypeToGL(m_Type), m_Id);
}

void OpenGLTexture::unbind() {
  glBindTexture(textureTypeToGL(m_Type), 0);
}