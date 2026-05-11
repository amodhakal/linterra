#pragma once

#include <glad/glad.h>

#include "../renderer.hpp"

#include <memory>

class OpenGLBuffer;
class OpenGLVertexArray;
class OpenGLShader;
class OpenGLShaderProgram;
class OpenGLTexture;

class OpenGLRenderer : public IRenderer {
 public:
  OpenGLRenderer();
  ~OpenGLRenderer() override;

  void clear(const glm::vec4& clearColor) override;
  void setViewport(int x, int y, int width, int height) override;
  void enable(Feature feature) override;
  void disable(Feature feature) override;
  void setPolygonMode(bool wireframe) override;

  std::unique_ptr<IBuffer> createBuffer(BufferType type) override;
  void setBufferData(IBuffer& buffer, const void* data, size_t size,
                     BufferUsage usage) override;

  std::unique_ptr<IVertexArray> createVertexArray() override;
  void setVertexAttribute(IVertexArray& va, uint32_t index, int size,
                         DataType type, bool normalized, size_t stride,
                         size_t offset) override;
  void enableVertexAttribute(IVertexArray& va, uint32_t index) override;

  std::unique_ptr<IShader> createShader(ShaderType type,
                                         const char* source) override;
  std::unique_ptr<IShaderProgram> createShaderProgram(
      std::vector<std::unique_ptr<IShader>> shaders) override;

  std::unique_ptr<ITexture> createTexture(TextureType type) override;
  void setTextureParameter(ITexture& texture, int pname, int value) override;
  void setTextureImage2DArray(ITexture& texture, int width, int height,
                              int layers, const void* data) override;
  void generateMipmaps(ITexture& texture) override;

  void draw(PrimitiveType type, size_t count, size_t offset) override;
  void drawIndexed(PrimitiveType type, size_t count, size_t offset,
                   IndexType indexType) override;
  void bindVertexArray(IVertexArray& va) override;

  void activeTexture(int unit) override;

 private:
  static GLenum convertBufferUsage(BufferUsage usage);
  static GLenum convertDataType(DataType type);
  static GLenum convertIndexType(IndexType type);
  static GLenum convertPrimitiveType(PrimitiveType type);
  static GLenum convertFeature(Feature feature);
};