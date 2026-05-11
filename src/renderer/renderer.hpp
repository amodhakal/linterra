#pragma once

#include "renderer_fwd.hpp"

#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

enum class BufferType : uint8_t {
  Vertex = 0,
  Index = 1,
  Uniform = 2
};

enum class BufferUsage : uint8_t {
  Static = 0,
  Dynamic = 1,
  Stream = 2
};

enum class ShaderType : uint8_t {
  Vertex = 0,
  Fragment = 1,
  Compute = 2
};

enum class TextureType : uint8_t {
  Texture2D = 0,
  Texture2DArray = 1,
  Texture3D = 2,
  Cubemap = 3
};

enum class PrimitiveType : uint8_t {
  Triangles = 0,
  Lines = 1,
  Points = 2,
  TriangleStrip = 3
};

enum class DataType : uint8_t {
  Float = 0,
  Int = 1,
  UnsignedInt = 2,
  Byte = 3,
  UnsignedByte = 4
};

enum class IndexType : uint8_t {
  UnsignedByte = 0,
  UnsignedShort = 1,
  UnsignedInt = 2
};

enum class Feature : uint8_t {
  DepthTest = 0,
  Blending = 1,
  Culling = 2,
  ScissorTest = 3
};

enum class RenderBackend : uint8_t {
  OpenGL = 0,
  Metal = 1,
  Vulkan = 2
};

class IBuffer {
 public:
  virtual ~IBuffer() = default;
  virtual void bind() = 0;
  virtual void unbind() = 0;
  virtual uint32_t getId() const = 0;
};

class IVertexArray {
 public:
  virtual ~IVertexArray() = default;
  virtual void bind() = 0;
  virtual void unbind() = 0;
  virtual uint32_t getId() const = 0;
};

class IShader {
 public:
  virtual ~IShader() = default;
  virtual ShaderType getType() const = 0;
  virtual bool isCompiled() const = 0;
  virtual std::string getCompileLog() const = 0;
  virtual uint32_t getId() const = 0;
};

class IShaderProgram {
 public:
  virtual ~IShaderProgram() = default;
  virtual void use() = 0;
  virtual int getUniformLocation(const char* name) = 0;
  virtual void setUniformMatrix4fv(int location, const float* value) = 0;
  virtual void setUniform1i(int location, int value) = 0;
  virtual void setUniform1iv(int location, int count, const int* values) = 0;
  virtual void setUniform1f(int location, float value) = 0;
  virtual void setUniform3f(int location, float x, float y, float z) = 0;
  virtual uint32_t getId() const = 0;
};

class ITexture {
 public:
  virtual ~ITexture() = default;
  virtual void bind(int unit) = 0;
  virtual void unbind() = 0;
  virtual uint32_t getId() const = 0;
};

class IRenderer {
 public:
  virtual ~IRenderer() = default;

  virtual void clear(const glm::vec4& clearColor) = 0;
  virtual void setViewport(int x, int y, int width, int height) = 0;
  virtual void enable(Feature feature) = 0;
  virtual void disable(Feature feature) = 0;
  virtual void setPolygonMode(bool wireframe) = 0;

  virtual std::unique_ptr<IBuffer> createBuffer(BufferType type) = 0;
  virtual void setBufferData(IBuffer& buffer, const void* data, size_t size,
                              BufferUsage usage) = 0;

  virtual std::unique_ptr<IVertexArray> createVertexArray() = 0;
  virtual void setVertexAttribute(IVertexArray& va, uint32_t index, int size,
                                  DataType type, bool normalized, size_t stride,
                                  size_t offset) = 0;
  virtual void enableVertexAttribute(IVertexArray& va, uint32_t index) = 0;

  virtual std::unique_ptr<IShader> createShader(ShaderType type,
                                                 const char* source) = 0;
  virtual std::unique_ptr<IShaderProgram> createShaderProgram(
      std::vector<std::unique_ptr<IShader>> shaders) = 0;

  virtual std::unique_ptr<ITexture> createTexture(TextureType type) = 0;
  virtual void setTextureParameter(ITexture& texture, int pname, int value) = 0;
  virtual void setTextureImage2DArray(ITexture& texture, int width, int height,
                                      int layers, const void* data) = 0;
  virtual void generateMipmaps(ITexture& texture) = 0;

  virtual void draw(PrimitiveType type, size_t count, size_t offset) = 0;
  virtual void drawIndexed(PrimitiveType type, size_t count, size_t offset,
                           IndexType indexType) = 0;
  virtual void bindVertexArray(IVertexArray& va) = 0;

  virtual void activeTexture(int unit) = 0;
};

std::unique_ptr<IRenderer> createRenderer(RenderBackend backend);