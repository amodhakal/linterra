#include "framebuffer.h"

#include <glad/glad.h>

Framebuffer::Framebuffer() = default;

Framebuffer::~Framebuffer() { destroy(); }

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_Fbo(other.m_Fbo),
      m_ColorTexture(other.m_ColorTexture),
      m_DepthRbo(other.m_DepthRbo),
      m_Width(other.m_Width),
      m_Height(other.m_Height) {
  other.m_Fbo = 0;
  other.m_ColorTexture = 0;
  other.m_DepthRbo = 0;
  other.m_Width = 0;
  other.m_Height = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
  if (this != &other) {
    destroy();
    m_Fbo = other.m_Fbo;
    m_ColorTexture = other.m_ColorTexture;
    m_DepthRbo = other.m_DepthRbo;
    m_Width = other.m_Width;
    m_Height = other.m_Height;
    other.m_Fbo = 0;
    other.m_ColorTexture = 0;
    other.m_DepthRbo = 0;
    other.m_Width = 0;
    other.m_Height = 0;
  }
  return *this;
}

void Framebuffer::destroy() {
  if (m_Fbo != 0) {
    glDeleteFramebuffers(1, &m_Fbo);
    m_Fbo = 0;
  }
  if (m_ColorTexture != 0) {
    glDeleteTextures(1, &m_ColorTexture);
    m_ColorTexture = 0;
  }
  if (m_DepthRbo != 0) {
    glDeleteRenderbuffers(1, &m_DepthRbo);
    m_DepthRbo = 0;
  }
}

void Framebuffer::resize(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0) return;
  if (m_Width == width && m_Height == height && m_Fbo != 0) return;

  destroy();

  m_Width = width;
  m_Height = height;

  glGenFramebuffers(1, &m_Fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, m_Fbo);

  glGenTextures(1, &m_ColorTexture);
  glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width),
               static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_ColorTexture, 0);

  glGenRenderbuffers(1, &m_DepthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, m_DepthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                        static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height));
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, m_DepthRbo);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    throw std::runtime_error("Framebuffer is not complete");
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, m_Fbo);
}

void Framebuffer::bindColorTexture(std::int32_t unit) const {
  glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
  glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
}
