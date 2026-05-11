#include "opengl_shader.hpp"

#include <glad/glad.h>
#include <stdexcept>
#include <vector>

namespace {

GLenum shaderTypeToGL(ShaderType type) {
  switch (type) {
    case ShaderType::Vertex:
      return GL_VERTEX_SHADER;
    case ShaderType::Fragment:
      return GL_FRAGMENT_SHADER;
    case ShaderType::Compute:
      return GL_VERTEX_SHADER;
  }
  return GL_VERTEX_SHADER;
}

}  // namespace

OpenGLShader::OpenGLShader(ShaderType type, const char* source)
    : m_Type(type) {
  GLuint shader = glCreateShader(shaderTypeToGL(type));
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(shader, 512, nullptr, infoLog);
    m_CompileLog = infoLog;
    m_Compiled = false;
    glDeleteShader(shader);
    return;
  }

  m_Id = shader;
  m_Compiled = true;
}

OpenGLShader::~OpenGLShader() {
  if (m_Id != 0) {
    glDeleteShader(m_Id);
    m_Id = 0;
  }
}

OpenGLShader::OpenGLShader(OpenGLShader&& other) noexcept
    : m_Id(other.m_Id),
      m_Type(other.m_Type),
      m_Compiled(other.m_Compiled),
      m_CompileLog(std::move(other.m_CompileLog)) {
  other.m_Id = 0;
}

OpenGLShader& OpenGLShader::operator=(OpenGLShader&& other) noexcept {
  if (this != &other) {
    if (m_Id != 0) {
      glDeleteShader(m_Id);
    }
    m_Id = other.m_Id;
    m_Type = other.m_Type;
    m_Compiled = other.m_Compiled;
    m_CompileLog = std::move(other.m_CompileLog);
    other.m_Id = 0;
  }
  return *this;
}

OpenGLShaderProgram::OpenGLShaderProgram(
    std::vector<std::unique_ptr<IShader>> shaders) {
  m_Id = glCreateProgram();

  for (auto& shader : shaders) {
    glAttachShader(m_Id, shader->getId());
  }

  glLinkProgram(m_Id);

  GLint success = 0;
  glGetProgramiv(m_Id, GL_LINK_STATUS, &success);

  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(m_Id, 512, nullptr, infoLog);
    glDeleteProgram(m_Id);
    m_Id = 0;
    throw std::runtime_error(std::string("Shader linking: ") + infoLog);
  }
}

OpenGLShaderProgram::~OpenGLShaderProgram() {
  if (m_Id != 0) {
    glDeleteProgram(m_Id);
    m_Id = 0;
  }
}

OpenGLShaderProgram::OpenGLShaderProgram(OpenGLShaderProgram&& other) noexcept
    : m_Id(other.m_Id) {
  other.m_Id = 0;
}

OpenGLShaderProgram& OpenGLShaderProgram::operator=(
    OpenGLShaderProgram&& other) noexcept {
  if (this != &other) {
    if (m_Id != 0) {
      glDeleteProgram(m_Id);
    }
    m_Id = other.m_Id;
    other.m_Id = 0;
  }
  return *this;
}

void OpenGLShaderProgram::use() {
  glUseProgram(m_Id);
}

int OpenGLShaderProgram::getUniformLocation(const char* name) {
  return glGetUniformLocation(m_Id, name);
}

void OpenGLShaderProgram::setUniformMatrix4fv(int location, const float* value) {
  glUniformMatrix4fv(location, 1, GL_FALSE, value);
}

void OpenGLShaderProgram::setUniform1i(int location, int value) {
  glUniform1i(location, value);
}

void OpenGLShaderProgram::setUniform1iv(int location, int count,
                                        const int* values) {
  glUniform1iv(location, count, values);
}

void OpenGLShaderProgram::setUniform1f(int location, float value) {
  glUniform1f(location, value);
}

void OpenGLShaderProgram::setUniform3f(int location, float x, float y, float z) {
  glUniform3f(location, x, y, z);
}