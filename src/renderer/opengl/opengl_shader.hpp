#pragma once

#include "../renderer.hpp"

#include <string>

class OpenGLShader : public IShader {
 public:
  explicit OpenGLShader(ShaderType type, const char* source);
  ~OpenGLShader() override;

  OpenGLShader(const OpenGLShader&) = delete;
  OpenGLShader& operator=(const OpenGLShader&) = delete;
  OpenGLShader(OpenGLShader&& other) noexcept;
  OpenGLShader& operator=(OpenGLShader&& other) noexcept;

  ShaderType getType() const override { return m_Type; }
  bool isCompiled() const override { return m_Compiled; }
  std::string getCompileLog() const override { return m_CompileLog; }
  uint32_t getId() const override { return m_Id; }

 private:
  uint32_t m_Id = 0;
  ShaderType m_Type;
  bool m_Compiled = false;
  std::string m_CompileLog;
};

class OpenGLShaderProgram : public IShaderProgram {
 public:
  explicit OpenGLShaderProgram(std::vector<std::unique_ptr<IShader>> shaders);
  ~OpenGLShaderProgram() override;

  OpenGLShaderProgram(const OpenGLShaderProgram&) = delete;
  OpenGLShaderProgram& operator=(const OpenGLShaderProgram&) = delete;
  OpenGLShaderProgram(OpenGLShaderProgram&& other) noexcept;
  OpenGLShaderProgram& operator=(OpenGLShaderProgram&& other) noexcept;

  void use() override;
  int getUniformLocation(const char* name) override;
  void setUniformMatrix4fv(int location, const float* value) override;
  void setUniform1i(int location, int value) override;
  void setUniform1iv(int location, int count, const int* values) override;
  void setUniform1f(int location, float value) override;
  void setUniform1ui(int location, uint32_t value) override;
  void setUniform2f(int location, float x, float y) override;
  void setUniform3f(int location, float x, float y, float z) override;
  uint32_t getId() const override { return m_Id; }

 private:
  uint32_t m_Id = 0;
};