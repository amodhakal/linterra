#include "shader.h"

#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <string>
#include <utility>

#include "io.h"
#include "renderer/renderer.hpp"

namespace {

std::string getShaderSource(const char* filePath) {
  return IO::getFullFileContents(filePath);
}

}  // namespace

Shader::Shader(IRenderer* renderer) : m_Renderer(renderer) {}

Shader::~Shader() = default;

Shader::Shader(Shader&& other) noexcept
    : m_Renderer(other.m_Renderer),
      m_Program(std::move(other.m_Program)),
      m_Uniforms(std::move(other.m_Uniforms)) {
  other.m_Renderer = nullptr;
}

Shader& Shader::operator=(Shader&& other) noexcept {
  if (this != &other) {
    m_Renderer = other.m_Renderer;
    m_Program = std::move(other.m_Program);
    m_Uniforms = std::move(other.m_Uniforms);
    other.m_Renderer = nullptr;
  }
  return *this;
}

void Shader::load(const char* vertexPath, const char* fragmentPath) {
  m_Program.reset();

  auto vertexShader =
      m_Renderer->createShader(ShaderType::Vertex, getShaderSource(vertexPath).c_str());
  if (!vertexShader->isCompiled()) {
    throw std::runtime_error("Vertex shader compilation: " + vertexShader->getCompileLog());
  }

  auto fragmentShader =
      m_Renderer->createShader(ShaderType::Fragment, getShaderSource(fragmentPath).c_str());
  if (!fragmentShader->isCompiled()) {
    throw std::runtime_error("Fragment shader compilation: " + fragmentShader->getCompileLog());
  }

  std::vector<std::unique_ptr<IShader>> shaders;
  shaders.push_back(std::move(vertexShader));
  shaders.push_back(std::move(fragmentShader));

  m_Program = m_Renderer->createShaderProgram(std::move(shaders));
}

void Shader::loadCompute(const char* computePath) {
  m_Program.reset();

  auto computeShader =
      m_Renderer->createShader(ShaderType::Compute, getShaderSource(computePath).c_str());
  if (!computeShader->isCompiled()) {
    throw std::runtime_error("Compute shader compilation: " + computeShader->getCompileLog());
  }

  std::vector<std::unique_ptr<IShader>> shaders;
  shaders.push_back(std::move(computeShader));

  m_Program = m_Renderer->createShaderProgram(std::move(shaders));
}

void Shader::dispatch(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) {
  if (m_Program) {
    m_Program->use();
    m_Renderer->dispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
  }
}

void Shader::bindBufferBase(IBuffer& buffer, uint32_t bindingPoint) {
  m_Renderer->bindBufferBase(buffer, bindingPoint);
}

void Shader::use() {
  if (m_Program) {
    m_Program->use();
  }
}

std::uint32_t Shader::getId() const {
  return m_Program ? m_Program->getId() : 0;
}

void Shader::newUniform(const char* name) {
  if (m_Program) {
    int location = m_Program->getUniformLocation(name);
    if (location < 0) {
      std::fprintf(stderr,
                   "Shader::newUniform: uniform \"%s\" not found in shader "
                   "program (location = -1)\n",
                   name);
    }
    m_Uniforms[name] = location;
  }
}

void Shader::setUniformMat4(const char* name, const glm::mat4& values) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniformMatrix4fv(m_Uniforms[name], glm::value_ptr(values));
  }
}

void Shader::setUniformInt(const char* name, int value) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniform1i(m_Uniforms[name], value);
  }
}

void Shader::setUniformIntArray(const char* name, const int* values, int count) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniform1iv(m_Uniforms[name], count, values);
  }
}

void Shader::setUniformFloat(const char* name, float value) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniform1f(m_Uniforms[name], value);
  }
}

void Shader::setUniformUInt(const char* name, uint32_t value) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniform1ui(m_Uniforms[name], value);
  }
}

void Shader::setUniformVec2(const char* name, glm::vec2 value) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniform2f(m_Uniforms[name], value.x, value.y);
  }
}

void Shader::setUniformVec3(const char* name, glm::vec3 value) {
  if (m_Program && m_Uniforms.contains(name)) {
    m_Program->setUniform3f(m_Uniforms[name], value.x, value.y, value.z);
  }
}