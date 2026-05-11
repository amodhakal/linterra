#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class Shader {
 public:
  Shader();
  ~Shader();

  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&& other) noexcept;
  Shader& operator=(Shader&& other) noexcept;

  void load(const char* vertexPath, const char* fragmentPath);
  void use();
  [[nodiscard]] std::uint32_t getId() const;

  void newUniform(const char* name);
  void setUniformMat4(const char* name, const glm::mat4& values);
  void setUniformInt(const char* name, int value);
  void setUniformIntArray(const char* name, const int* values, int count);
  void setUniformFloat(const char* name, float value);
  void setUniformVec3(const char* name, glm::vec3 value);

 private:
  std::uint32_t m_Id = 0;
  std::unordered_map<std::string, int> m_Uniforms;
};
