#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class IRenderer;
class IShaderProgram;
class IBuffer;

class Shader {
 public:
  explicit Shader(IRenderer* renderer);
  ~Shader();

  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&& other) noexcept;
  Shader& operator=(Shader&& other) noexcept;

  void load(const char* vertexPath, const char* fragmentPath);
  void loadCompute(const char* computePath);
  void dispatch(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ);
  void bindBufferBase(IBuffer& buffer, uint32_t bindingPoint);
  void use();
  [[nodiscard]] std::uint32_t getId() const;

  void newUniform(const char* name);
  void setUniformMat4(const char* name, const glm::mat4& values);
  void setUniformInt(const char* name, int value);
  void setUniformIntArray(const char* name, const int* values, int count);
  void setUniformFloat(const char* name, float value);
  void setUniformUInt(const char* name, uint32_t value);
  void setUniformVec2(const char* name, glm::vec2 value);
  void setUniformVec3(const char* name, glm::vec3 value);

 private:
  IRenderer* m_Renderer = nullptr;
  std::unique_ptr<IShaderProgram> m_Program;
  std::unordered_map<std::string, int> m_Uniforms;
};
