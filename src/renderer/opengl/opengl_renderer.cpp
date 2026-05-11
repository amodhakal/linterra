#include "opengl_renderer.hpp"

#include <glad/glad.h>

#include "opengl_buffer.hpp"
#include "opengl_shader.hpp"
#include "opengl_texture.hpp"
#include "opengl_vertex_array.hpp"

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() = default;

void OpenGLRenderer::clear(const glm::vec4& clearColor) {
  glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::setViewport(int x, int y, int width, int height) {
  glViewport(x, y, width, height);
}

void OpenGLRenderer::enable(Feature feature) {
  glEnable(convertFeature(feature));
}

void OpenGLRenderer::disable(Feature feature) {
  glDisable(convertFeature(feature));
}

void OpenGLRenderer::setPolygonMode(bool wireframe) {
  glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
}

std::unique_ptr<IBuffer> OpenGLRenderer::createBuffer(BufferType type) {
  return std::make_unique<OpenGLBuffer>(type);
}

void OpenGLRenderer::setBufferData(IBuffer& buffer, const void* data,
                                   size_t size, BufferUsage usage) {
  buffer.bind();
  auto& glBuffer = dynamic_cast<OpenGLBuffer&>(buffer);
  GLenum target = (glBuffer.getType() == BufferType::Index) 
                  ? GL_ELEMENT_ARRAY_BUFFER 
                  : GL_ARRAY_BUFFER;
  glBufferData(target, static_cast<GLsizeiptr>(size), data,
               convertBufferUsage(usage));
}

std::unique_ptr<IVertexArray> OpenGLRenderer::createVertexArray() {
  return std::make_unique<OpenGLVertexArray>();
}

void OpenGLRenderer::setVertexAttribute(IVertexArray& va, uint32_t index,
                                        int size, DataType type,
                                        bool normalized, size_t stride,
                                        size_t offset) {
  va.bind();
  glVertexAttribPointer(index, size, convertDataType(type), normalized ? GL_TRUE : GL_FALSE,
                       static_cast<GLsizei>(stride),
                       reinterpret_cast<void*>(offset));
}

void OpenGLRenderer::enableVertexAttribute(IVertexArray& va, uint32_t index) {
  va.bind();
  glEnableVertexAttribArray(index);
}

std::unique_ptr<IShader> OpenGLRenderer::createShader(ShaderType type,
                                                      const char* source) {
  return std::make_unique<OpenGLShader>(type, source);
}

std::unique_ptr<IShaderProgram> OpenGLRenderer::createShaderProgram(
    std::vector<std::unique_ptr<IShader>> shaders) {
  return std::make_unique<OpenGLShaderProgram>(std::move(shaders));
}

std::unique_ptr<ITexture> OpenGLRenderer::createTexture(TextureType type) {
  return std::make_unique<OpenGLTexture>(type);
}

void OpenGLRenderer::setTextureParameter(ITexture& texture, int pname,
                                        int value) {
  texture.bind(0);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, pname, value);
}

void OpenGLRenderer::setTextureImage2DArray(ITexture& texture, int width,
                                            int height, int layers,
                                            const void* data) {
  texture.bind(0);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, width, height, layers, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void OpenGLRenderer::generateMipmaps(ITexture& texture) {
  texture.bind(0);
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

void OpenGLRenderer::draw(PrimitiveType type, size_t count, size_t offset) {
  glDrawArrays(convertPrimitiveType(type), static_cast<GLint>(offset),
               static_cast<GLsizei>(count));
}

void OpenGLRenderer::drawIndexed(PrimitiveType type, size_t count,
                                 size_t offset, IndexType indexType) {
  glDrawElements(convertPrimitiveType(type), static_cast<GLsizei>(count),
                convertIndexType(indexType),
                reinterpret_cast<void*>(offset));
}

void OpenGLRenderer::bindVertexArray(IVertexArray& va) {
  va.bind();
}

void OpenGLRenderer::activeTexture(int unit) {
  glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
}

GLenum OpenGLRenderer::convertBufferUsage(BufferUsage usage) {
  switch (usage) {
    case BufferUsage::Static:
      return GL_STATIC_DRAW;
    case BufferUsage::Dynamic:
      return GL_DYNAMIC_DRAW;
    case BufferUsage::Stream:
      return GL_STREAM_DRAW;
  }
  return GL_STATIC_DRAW;
}

GLenum OpenGLRenderer::convertDataType(DataType type) {
  switch (type) {
    case DataType::Float:
      return GL_FLOAT;
    case DataType::Int:
      return GL_INT;
    case DataType::UnsignedInt:
      return GL_UNSIGNED_INT;
    case DataType::Byte:
      return GL_BYTE;
    case DataType::UnsignedByte:
      return GL_UNSIGNED_BYTE;
  }
  return GL_FLOAT;
}

GLenum OpenGLRenderer::convertIndexType(IndexType type) {
  switch (type) {
    case IndexType::UnsignedByte:
      return GL_UNSIGNED_BYTE;
    case IndexType::UnsignedShort:
      return GL_UNSIGNED_SHORT;
    case IndexType::UnsignedInt:
      return GL_UNSIGNED_INT;
  }
  return GL_UNSIGNED_INT;
}

GLenum OpenGLRenderer::convertPrimitiveType(PrimitiveType type) {
  switch (type) {
    case PrimitiveType::Triangles:
      return GL_TRIANGLES;
    case PrimitiveType::Lines:
      return GL_LINES;
    case PrimitiveType::Points:
      return GL_POINTS;
    case PrimitiveType::TriangleStrip:
      return GL_TRIANGLE_STRIP;
  }
  return GL_TRIANGLES;
}

GLenum OpenGLRenderer::convertFeature(Feature feature) {
  switch (feature) {
    case Feature::DepthTest:
      return GL_DEPTH_TEST;
    case Feature::Blending:
      return GL_BLEND;
    case Feature::Culling:
      return GL_CULL_FACE;
    case Feature::ScissorTest:
      return GL_SCISSOR_TEST;
  }
  return GL_DEPTH_TEST;
}