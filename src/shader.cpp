#include "config.h"
#include "shader.h"

#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <string>
#include <utility>

#include "io.h"

namespace {

GLuint setupShaders(const char* filePath, GLenum shaderType) {
  const std::string shaderCodeRaw = IO::getFullFileContents(filePath);
  const char* shaderCode = shaderCodeRaw.c_str();

  GLuint shader = glCreateShader(shaderType);
  glShaderSource(shader, 1, &shaderCode, nullptr);
  glCompileShader(shader);

  GLint success = 0;
  char infoLog[512];

  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(shader, 512, nullptr, infoLog);
    throw std::runtime_error("Shader (" + std::string(filePath) +
                             ") compilation: " + std::string(infoLog));
  }

  return shader;
}

}  // namespace

Shader::Shader() = default;

Shader::~Shader() {
  if (m_Id != 0) {
    glDeleteProgram(m_Id);
    m_Id = 0;
  }
}

Shader::Shader(Shader&& other) noexcept
    : m_Id(other.m_Id), m_Uniforms(std::move(other.m_Uniforms)) {
  other.m_Id = 0;
  other.m_Uniforms.clear();
}

Shader& Shader::operator=(Shader&& other) noexcept {
  if (this != &other) {
    if (m_Id != 0) {
      glDeleteProgram(m_Id);
    }
    m_Id = other.m_Id;
    m_Uniforms = std::move(other.m_Uniforms);
    other.m_Id = 0;
    other.m_Uniforms.clear();
  }
  return *this;
}

void Shader::load(const char* vertexPath, const char* fragmentPath) {
  if (m_Id != 0) {
    glDeleteProgram(m_Id);
    m_Id = 0;
    m_Uniforms.clear();
  }

  GLuint vertexShader = setupShaders(vertexPath, GL_VERTEX_SHADER);
  GLuint fragmentShader = setupShaders(fragmentPath, GL_FRAGMENT_SHADER);

  GLint success = 0;
  char infoLog[512];

  GLuint shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    throw std::runtime_error("Shader linking: " + std::string(infoLog));
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  m_Id = shaderProgram;
}

void Shader::use() {
  glUseProgram(m_Id);
}

std::uint32_t Shader::getId() const {
  return m_Id;
}

void Shader::newUniform(const char* name) {
  m_Uniforms[name] = glGetUniformLocation(m_Id, name);
}

void Shader::setUniformMat4(const char* name, const glm::mat4& values) {
  glUniformMatrix4fv(m_Uniforms[name], 1, GL_FALSE, glm::value_ptr(values));
}

void Shader::setUniformInt(const char* name, int value) {
  glUniform1i(m_Uniforms[name], value);
}

void Shader::setUniformIntArray(const char* name, const int* values, int count) {
  glUniform1iv(m_Uniforms[name], count, values);
}

void Shader::setUniformFloat(const char* name, float value) {
  glUniform1f(m_Uniforms[name], value);
}

void Shader::setUniformVec3(const char* name, glm::vec3 value) {
  glUniform3f(m_Uniforms[name], value.x, value.y, value.z);
}
