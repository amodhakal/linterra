#include "opengl_renderer.hpp"

#include <glad/glad.h>
#include "opengl_buffer.hpp"
#include "opengl_shader.hpp"
#include "opengl_texture.hpp"
#include "opengl_vertex_array.hpp"

#include <stdexcept>

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() { destroyOffscreenTarget(); }

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
                  : (glBuffer.getType() == BufferType::Storage)
                  ? GL_SHADER_STORAGE_BUFFER
                  : GL_ARRAY_BUFFER;
  glBufferData(target, static_cast<GLsizeiptr>(size), data,
               convertBufferUsage(usage));
}

void OpenGLRenderer::bindBufferBase(IBuffer& buffer, uint32_t bindingPoint) {
  buffer.bind();
  auto& glBuffer = dynamic_cast<OpenGLBuffer&>(buffer);
  GLenum target = (glBuffer.getType() == BufferType::Index) 
                  ? GL_ELEMENT_ARRAY_BUFFER 
                  : (glBuffer.getType() == BufferType::Storage)
                  ? GL_SHADER_STORAGE_BUFFER
                  : GL_ARRAY_BUFFER;
  glBindBufferBase(target, bindingPoint, buffer.getId());
}

void OpenGLRenderer::getBufferSubData(IBuffer& buffer, size_t offset, size_t size, void* data) {
  buffer.bind();
  auto& glBuffer = dynamic_cast<OpenGLBuffer&>(buffer);
  GLenum target = (glBuffer.getType() == BufferType::Index) 
                  ? GL_ELEMENT_ARRAY_BUFFER 
                  : (glBuffer.getType() == BufferType::Storage)
                  ? GL_SHADER_STORAGE_BUFFER
                  : GL_ARRAY_BUFFER;
  glGetBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
  buffer.unbind();
}

void OpenGLRenderer::dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) {
  glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
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

void OpenGLRenderer::initializeWindowing() {
  glfwInit();
}

void OpenGLRenderer::terminateWindowing() {
  glfwTerminate();
}

void OpenGLRenderer::configureWindowHints() {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
}

bool OpenGLRenderer::createWindow(int width, int height, const char* title) {
  m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (m_Window == nullptr) {
    return false;
  }
  glfwSetWindowUserPointer(m_Window, this);
  return true;
}

void OpenGLRenderer::makeContextCurrent() {
  glfwMakeContextCurrent(m_Window);
}

void OpenGLRenderer::setCursorDisabled() {
  glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void OpenGLRenderer::setEventContext(void* pointer) {
  m_EventContext = pointer;
}

void OpenGLRenderer::setCursorPosCallback(CursorPosCallback callback) {
  m_CursorPosCallback = callback;
  glfwSetCursorPosCallback(m_Window, dispatchCursorPosCallback);
}

void OpenGLRenderer::setScrollCallback(ScrollCallback callback) {
  m_ScrollCallback = callback;
  glfwSetScrollCallback(m_Window, dispatchScrollCallback);
}

void OpenGLRenderer::setFramebufferSizeCallback(FramebufferSizeCallback callback) {
  m_FramebufferSizeCallback = callback;
  glfwSetFramebufferSizeCallback(m_Window, dispatchFramebufferSizeCallback);
}

bool OpenGLRenderer::loadContextFunctions() {
  return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) !=
         0;
}

void OpenGLRenderer::getFramebufferSize(int* width, int* height) {
  glfwGetFramebufferSize(m_Window, width, height);
}

bool OpenGLRenderer::windowShouldClose() {
  return glfwWindowShouldClose(m_Window) != 0;
}

void OpenGLRenderer::swapBuffers() {
  glfwSwapBuffers(m_Window);
}

void OpenGLRenderer::pollEvents() {
  glfwPollEvents();
}

float OpenGLRenderer::getTimeSeconds() {
  return static_cast<float>(glfwGetTime());
}

bool OpenGLRenderer::isKeyPressed(Key key) {
  return glfwGetKey(m_Window, convertKey(key)) == GLFW_PRESS;
}

void OpenGLRenderer::setWindowShouldClose(bool shouldClose) {
  glfwSetWindowShouldClose(m_Window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
}

void* OpenGLRenderer::getNativeWindow() {
  return m_Window;
}

void OpenGLRenderer::bindFramebuffer(std::uint32_t framebufferId) {
  glBindFramebuffer(GL_FRAMEBUFFER, framebufferId);
}

std::uint32_t OpenGLRenderer::getLastError() {
  return glGetError();
}

void OpenGLRenderer::resizeOffscreenTarget(std::uint32_t width,
                                           std::uint32_t height) {
  if (width == 0 || height == 0) return;
  if (m_OffscreenWidth == width && m_OffscreenHeight == height &&
      m_OffscreenFbo != 0) {
    return;
  }

  destroyOffscreenTarget();
  m_OffscreenWidth = width;
  m_OffscreenHeight = height;

  glGenFramebuffers(1, &m_OffscreenFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFbo);

  glGenTextures(1, &m_OffscreenColorTexture);
  glBindTexture(GL_TEXTURE_2D, m_OffscreenColorTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width),
               static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_OffscreenColorTexture, 0);

  glGenRenderbuffers(1, &m_OffscreenDepthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, m_OffscreenDepthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                        static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height));
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            m_OffscreenDepthRbo);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    throw std::runtime_error("Framebuffer is not complete");
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLRenderer::bindOffscreenTarget() {
  glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFbo);
}

void OpenGLRenderer::bindOffscreenColorTexture(std::int32_t unit) {
  bindTexture2D(m_OffscreenColorTexture, unit);
}

void OpenGLRenderer::destroyOffscreenTarget() {
  if (m_OffscreenFbo != 0) {
    glDeleteFramebuffers(1, &m_OffscreenFbo);
    m_OffscreenFbo = 0;
  }
  if (m_OffscreenColorTexture != 0) {
    glDeleteTextures(1, &m_OffscreenColorTexture);
    m_OffscreenColorTexture = 0;
  }
  if (m_OffscreenDepthRbo != 0) {
    glDeleteRenderbuffers(1, &m_OffscreenDepthRbo);
    m_OffscreenDepthRbo = 0;
  }
  m_OffscreenWidth = 0;
  m_OffscreenHeight = 0;
}

void OpenGLRenderer::bindTexture2D(std::uint32_t textureId, std::int32_t unit) {
  glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
  glBindTexture(GL_TEXTURE_2D, textureId);
}

void OpenGLRenderer::dispatchCursorPosCallback(GLFWwindow* window, double x,
                                               double y) {
  auto* renderer =
      static_cast<OpenGLRenderer*>(glfwGetWindowUserPointer(window));
  if (renderer->m_CursorPosCallback != nullptr) {
    renderer->m_CursorPosCallback(renderer->m_EventContext, x, y);
  }
}

void OpenGLRenderer::dispatchScrollCallback(GLFWwindow* window, double x,
                                            double y) {
  auto* renderer =
      static_cast<OpenGLRenderer*>(glfwGetWindowUserPointer(window));
  if (renderer->m_ScrollCallback != nullptr) {
    renderer->m_ScrollCallback(renderer->m_EventContext, x, y);
  }
}

void OpenGLRenderer::dispatchFramebufferSizeCallback(GLFWwindow* window,
                                                     int width, int height) {
  auto* renderer =
      static_cast<OpenGLRenderer*>(glfwGetWindowUserPointer(window));
  if (renderer->m_FramebufferSizeCallback != nullptr) {
    renderer->m_FramebufferSizeCallback(renderer->m_EventContext, width, height);
  }
}

int OpenGLRenderer::convertKey(Key key) {
  switch (key) {
    case Key::Escape:
      return GLFW_KEY_ESCAPE;
    case Key::W:
      return GLFW_KEY_W;
    case Key::A:
      return GLFW_KEY_A;
    case Key::S:
      return GLFW_KEY_S;
    case Key::D:
      return GLFW_KEY_D;
    case Key::Space:
      return GLFW_KEY_SPACE;
    case Key::LeftShift:
      return GLFW_KEY_LEFT_SHIFT;
  }
  return GLFW_KEY_UNKNOWN;
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