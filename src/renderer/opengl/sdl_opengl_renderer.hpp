#pragma once

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "../renderer.hpp"

#include <memory>

class OpenGLBuffer;
class OpenGLVertexArray;
class OpenGLShader;
class OpenGLShaderProgram;
class OpenGLTexture;

class SDLOpenGLRenderer : public IRenderer {
 public:
  SDLOpenGLRenderer();
  ~SDLOpenGLRenderer() override;

  void clear(const glm::vec4& clearColor) override;
  void setViewport(int x, int y, int width, int height) override;
  void enable(Feature feature) override;
  void disable(Feature feature) override;
  void setPolygonMode(bool wireframe) override;

  std::unique_ptr<IBuffer> createBuffer(BufferType type) override;
  void setBufferData(IBuffer& buffer, const void* data, size_t size,
                     BufferUsage usage) override;
  void bindBufferBase(IBuffer& buffer, uint32_t bindingPoint) override;
  void getBufferSubData(IBuffer& buffer, size_t offset, size_t size, void* data) override;
  void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;

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

  void initializeWindowing() override;
  void terminateWindowing() override;
  void configureWindowHints() override;
  bool createWindow(int width, int height, const char* title) override;
  void makeContextCurrent() override;
  void setCursorDisabled() override;
  void setEventContext(void* pointer) override;
  void setCursorPosCallback(CursorPosCallback callback) override;
  void setScrollCallback(ScrollCallback callback) override;
  void setFramebufferSizeCallback(FramebufferSizeCallback callback) override;
  bool loadContextFunctions() override;
  void getFramebufferSize(int* width, int* height) override;
  bool windowShouldClose() override;
  void swapBuffers() override;
  void pollEvents() override;
  float getTimeSeconds() override;
  bool isKeyPressed(Key key) override;
  void setWindowShouldClose(bool shouldClose) override;
  void* getNativeWindow() override;

  void bindFramebuffer(std::uint32_t framebufferId) override;
  std::uint32_t getLastError() override;
  void bindTexture2D(std::uint32_t textureId, std::int32_t unit) override;
  void resizeOffscreenTarget(std::uint32_t width,
                             std::uint32_t height) override;
  void bindOffscreenTarget() override;
  void bindOffscreenColorTexture(std::int32_t unit) override;

 private:
  void destroyOffscreenTarget();
  static int convertKey(Key key);
  static void handleWindowEvent(void* userdata, const SDL_Event* event);

  static GLenum convertBufferUsage(BufferUsage usage);
  static GLenum convertDataType(DataType type);
  static GLenum convertIndexType(IndexType type);
  static GLenum convertPrimitiveType(PrimitiveType type);
  static GLenum convertFeature(Feature feature);

  SDL_Window* m_Window = nullptr;
  SDL_GLContext m_GLContext = nullptr;
  void* m_EventContext = nullptr;
  CursorPosCallback m_CursorPosCallback = nullptr;
  ScrollCallback m_ScrollCallback = nullptr;
  FramebufferSizeCallback m_FramebufferSizeCallback = nullptr;
  bool m_ShouldClose = false;

  std::uint32_t m_OffscreenFbo = 0;
  std::uint32_t m_OffscreenColorTexture = 0;
  std::uint32_t m_OffscreenDepthRbo = 0;
  std::uint32_t m_OffscreenWidth = 0;
  std::uint32_t m_OffscreenHeight = 0;
};