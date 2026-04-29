#pragma once
#include <sys/types.h>

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class Shader {
 public:
  Shader();
  void load(const char* vertexPath, const char* fragmentPath);
  void use();
  uint getId();

  void newUniform(const char* name);
  void setUniformMat4(const char* name, const glm::mat4& values);
  void setUniformInt(const char* name, int value);
  void setUniformIntArray(const char* name, const int* values, int count);
  void setUniformFloat(const char* name, float value);
  void setUniformVec3(const char* name, glm::vec3 value);

 private:
  uint m_Id;
  std::unordered_map<std::string, int> m_Uniforms;
};
