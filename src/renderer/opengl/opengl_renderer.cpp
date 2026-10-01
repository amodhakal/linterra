#include "opengl_renderer.hpp"

#include "gl_texture_type.hpp"

#include <glad/glad.h>
#include "config.h"
#include "opengl_buffer.hpp"
#include "opengl_shader.hpp"
#include "opengl_texture.hpp"
#include "opengl_vertex_array.hpp"

#include <cstdio>
#include <stdexcept>

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() {
  // Two rules, and both orderings matter:

  // 1. Release every GL object this renderer owns *before* terminating
  //    windowing. ~Application used to call glfwTerminate() from its
  //    destructor body, which runs before any member is destroyed -- so
  //    destroyOffscreenTarget() here, ~ChunkManager's per-chunk
  //    glDeleteBuffers/glDeleteVertexArrays, ~TextureArray's
  //    glDeleteTextures and ~Shader's glDeleteProgram all ran with no
  //    current context and GLAD's pointers aimed at an unloaded driver
  //    image. Undefined behaviour: GL_INVALID_OPERATION spam at best, a hard
  //    abort inside the driver on Mesa and on strict contexts, across
  //    thousands of deletions rather than one (#128).
  destroyOffscreenTarget();

  // 2. Tear down windowing here rather than leaving it to a caller. A
  //    renderer that owns GL objects owns the context they live in; letting
  //    the owner decide when to destroy the context is what made the ordering
  //    possible to get wrong in the first place.
  terminateWindowing();
}

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
               convertBufferUsage(usage, glBuffer.getType()));
  glBuffer.setSize(size);
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

bool OpenGLRenderer::getBufferSubData(IBuffer& buffer, size_t offset, size_t size, void* data) {
  // Order the shader-storage writes issued by dispatchCompute before reading.
  // The invariant Chunk::finishHeightMapGPU relies on is documented in
  // chunk.cpp but used to live three files away here, so a second readback
  // path or a reorder of dispatchCompute would silently break it. The barrier
  // belongs at the read site, where the requirement actually is.
  //
  // GLAD resolves GL 3.1+ entry points as function pointers, and a context
  // older than 3.1 leaves this one null -- calling it is a jump to address 0.
  // macOS caps at 4.1 for core profile but still exposes the barrier through
  // the extension path, so guard on the pointer rather than on a version
  // number. The same guard applies to dispatchCompute's call.
  if (glad_glMemoryBarrier != nullptr) {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  }

  // Bounds-check before the call. glGetBufferSubData raises GL_INVALID_VALUE
  // when offset + size exceeds the buffer and then writes nothing, which the
  // caller cannot distinguish from a successful read of zeroes.
  if (offset > buffer.getSize() || size > buffer.getSize() - offset) {
    std::fprintf(stderr,
                 "OpenGLRenderer::getBufferSubData: range [%zu, %zu) exceeds "
                 "the %zu-byte buffer\\n",
                 offset, offset + size, buffer.getSize());
    return false;
  }

  buffer.bind();
  auto& glBuffer = dynamic_cast<OpenGLBuffer&>(buffer);
  GLenum target = (glBuffer.getType() == BufferType::Index) 
                  ? GL_ELEMENT_ARRAY_BUFFER 
                  : (glBuffer.getType() == BufferType::Storage)
                  ? GL_SHADER_STORAGE_BUFFER
                  : GL_ARRAY_BUFFER;
  glGetBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
  buffer.unbind();

  // Drain the whole error queue rather than taking a single glGetError: one
  // call can return an error that was already pending before this function
  // ran, which would blame this read for someone else's mistake. Draining
  // here also keeps the failure local -- it cannot be misattributed to a
  // later call the way a once-per-frame drain can.
  const GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    std::fprintf(stderr,
                 "OpenGLRenderer::getBufferSubData: GL error 0x%04x reading "
                 "[%zu, %zu) from a %zu-byte buffer\\n",
                 static_cast<unsigned>(error), offset, offset + size,
                 buffer.getSize());
    return false;
  }
  return true;
}

void OpenGLRenderer::dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) {
  glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
  // See getBufferSubData: glad_glMemoryBarrier is null on a pre-3.1 context.
  if (glad_glMemoryBarrier != nullptr) {
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  }
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
  // Target comes from the texture, not a hardcoded 2D_ARRAY: glTexParameteri
  // applies to whichever target is named, so naming the wrong one configures a
  // texture that is not the one being set up.
  glTexParameteri(textureTypeToGL(texture.getType()), pname, value);
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
  glGenerateMipmap(textureTypeToGL(texture.getType()));
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
  if (glfwInit() == GLFW_FALSE) {
    throw std::runtime_error("Failed to initialize GLFW");
  }
}

void OpenGLRenderer::terminateWindowing() {
  // Idempotent. ~OpenGLRenderer calls this too, so an explicit call from the
  // owner (or a second one) must not run glfwTerminate twice.
  if (m_WindowingTerminated) {
    return;
  }
  m_WindowingTerminated = true;
  glfwTerminate();
}

void OpenGLRenderer::configureWindowHints() {
  // The compute path (GLSL compute shaders) requires OpenGL 4.3; on Apple the
  // max core profile is 4.1 and the GPU path is disabled, so request 3.3.
  // USE_GPU is a constexpr, which the preprocessor can't see, so the branch
  // stays in C++ and only the __APPLE__ half lives in an #if.
#if defined(__APPLE__)
  constexpr int kContextMajor = 3;
#else
  constexpr int kContextMajor = Constants::Noise::USE_GPU ? 4 : 3;
#endif
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kContextMajor);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
}

void OpenGLRenderer::setWindowVisible(bool visible) {
  // GLFW reads this hint when the window is created, so it has to be set
  // before createWindow() rather than toggled afterwards.
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
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

  // Arm the callback only once the GL entry points exist. GLFW dispatches
  // buffered events whenever the platform feels like it, not only from
  // glfwPollEvents, so a callback registered before GLAD is loaded can run
  // against a context where every gl* symbol is still a null function
  // pointer. handleResizeCallback reaches resizeOffscreenTarget, whose first
  // call is glGenFramebuffers -- a jump to address 0, with no exception and
  // no diagnostic.
  //
  // Refusing is strictly better than arming: the caller can register the
  // callback before GLAD if it likes, and it simply takes effect on the next
  // loadContextFunctions() rather than crashing in between. Nothing is lost,
  // because a resize that happens before the context exists has no GL work to
  // do anyway.
  if (!m_ContextFunctionsLoaded) {
    m_PendingFramebufferSizeCallback = callback;
    return;
  }

  glfwSetFramebufferSizeCallback(m_Window, dispatchFramebufferSizeCallback);
}

bool OpenGLRenderer::loadContextFunctions() {
  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
    return false;
  }
  m_ContextFunctionsLoaded = true;

  // Arm anything that was registered before the entry points existed.
  if (m_PendingFramebufferSizeCallback != nullptr) {
    glfwSetFramebufferSizeCallback(m_Window,
                                   dispatchFramebufferSizeCallback);
    m_PendingFramebufferSizeCallback = nullptr;
  }
  return true;
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

bool OpenGLRenderer::resizeOffscreenTarget(std::uint32_t width,
                                           std::uint32_t height) {
  // Never throws. This is reached from a GLFW C callback by way of
  // glfwPollEvents, and GLFW's frames carry no exception tables, so an
  // exception unwinding out of one is undefined behaviour -- on the macOS
  // toolchain it walks into C frames with no handler and lands at the thread
  // entry, giving "terminate called after throwing an instance of
  // std::runtime_error" and SIGABRT with no stack preserved (#142).
  //
  // The failure is self-reinforcing: an incomplete framebuffer is exactly
  // what a driver gives when it refuses an allocation, and a window resize is
  // when allocation pressure peaks.
  if (width == 0 || height == 0) {
    return false;
  }
  if (m_OffscreenWidth == width && m_OffscreenHeight == height &&
      m_OffscreenFbo != 0) {
    return true;
  }

  // Validate against the driver's limits before destroying anything. Both
  // matter and they are not the same limit: some drivers report
  // GL_MAX_TEXTURE_SIZE below GL_MAX_RENDERBUFFER_SIZE, so a width that
  // passes a naive renderbuffer check still fails glTexImage2D. Asking for
  // either allocation above its limit raises GL_INVALID_VALUE and leaves the
  // object unallocated, which then shows up one step later as an incomplete
  // framebuffer -- by which point the old target is already gone.
  GLint maxRenderbufferSize = 0;
  GLint maxTextureSize = 0;
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  const auto limit = std::min(maxRenderbufferSize, maxTextureSize);
  if (limit > 0 && (width > static_cast<std::uint32_t>(limit) ||
                    height > static_cast<std::uint32_t>(limit))) {
    std::fprintf(stderr,
                 "resizeOffscreenTarget: %ux%u exceeds the driver limit of "
                 "%d (GL_MAX_RENDERBUFFER_SIZE=%d, GL_MAX_TEXTURE_SIZE=%d)\n",
                 width, height, limit, maxRenderbufferSize, maxTextureSize);
    return false;
  }

  // Build into locals. Publishing the requested size to m_OffscreenWidth /
  // m_OffscreenHeight before the framebuffer is known to be complete is what
  // wedged the target permanently: the early-out above trusts the cache when
  // `m_OffscreenFbo != 0`, and a failed resize leaves exactly that -- a
  // nonzero name with incomplete attachments. Resizing back to the size the
  // user just came from then hit the early-out and did nothing at all, so the
  // renderer drew into a permanently incomplete framebuffer with no error, no
  // crash and no log line. The unrecoverable case was incomplete size A, then
  // a different incomplete size B, then back to A.
  //
  // So: the previous target stays alive until the replacement is proven
  // usable, and the members are written only at the end.
  GLuint fbo = 0;
  GLuint colorTexture = 0;
  GLuint depthRbo = 0;

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  glGenTextures(1, &colorTexture);
  glBindTexture(GL_TEXTURE_2D, colorTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width),
               static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colorTexture, 0);

  glGenRenderbuffers(1, &depthRbo);
  glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                        static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height));
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            depthRbo);

  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    const GLenum error = glGetError();
    std::fprintf(stderr,
                 "resizeOffscreenTarget: framebuffer incomplete (status "
                 "0x%04x, GL error 0x%04x) at %ux%u\n",
                 static_cast<unsigned>(status), static_cast<unsigned>(error),
                 width, height);

    // Discard the replacement and keep the previous target. Unbind first:
    // leaving an incomplete FBO bound sends every subsequent draw nowhere,
    // which turns one rejected resize into a silently blank frame rather than
    // a diagnosable one.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colorTexture);
    glDeleteRenderbuffers(1, &depthRbo);
    return false;
  }

  // Complete: the previous target is now genuinely redundant.
  destroyOffscreenTarget();
  m_OffscreenFbo = fbo;
  m_OffscreenColorTexture = colorTexture;
  m_OffscreenDepthRbo = depthRbo;
  m_OffscreenWidth = width;
  m_OffscreenHeight = height;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void OpenGLRenderer::bindOffscreenTarget() {
  if (m_OffscreenFbo == 0) {
    // Binding 0 here means the window's back buffer. The scene pass then
    // draws straight to the swap chain instead of the offscreen colour
    // target, the fog pass clears that same back buffer and wipes the scene
    // just drawn, and sampling the offscreen colour texture afterwards is a
    // framebuffer/texture feedback loop -- GL_INVALID_OPERATION, every frame,
    // reported by the generic drain with no indication which call caused it.
    //
    // Refusing and reporting makes this a loud, local failure rather than a
    // quiet wrong render. Callers that legitimately have no target -- a
    // minimized window, whose drawing buffer is 0x0 -- should skip the frame
    // instead, which is what Application::update now does (#147).
    std::fprintf(stderr,
                 "bindOffscreenTarget: no offscreen framebuffer is "
                 "allocated; refusing to bind the default framebuffer\n");
    return;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFbo);
}

void OpenGLRenderer::bindOffscreenColorTexture(std::int32_t unit) {
  if (m_OffscreenColorTexture == 0) {
    // Same reasoning as bindOffscreenTarget. Texture 0 is the default
    // texture, so binding it and sampling it while its would-be attachment is
    // bound is precisely the feedback loop described there.
    std::fprintf(stderr,
                 "bindOffscreenColorTexture: no offscreen colour texture is "
                 "allocated; refusing to bind the default texture\n");
    return;
  }
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

// A storage buffer takes a *_STORAGE usage, not a *_DRAW one: glBufferData
// raises GL_INVALID_ENUM for GL_SHADER_STORAGE_BUFFER given GL_STATIC_DRAW,
// GL_DYNAMIC_DRAW or GL_STREAM_DRAW, and silently allocates nothing. That is
// not hypothetical -- the engine's heightmap SSBO is the only storage buffer
// in the codebase, and it is created with BufferUsage::Dynamic, so on every
// non-Apple build its glBufferData failed and the deferred readback then read
// from a buffer with no allocation.
//
// Mapping is per buffer type, so this takes the type as well. The draw-usage
// spellings are unchanged for vertex/index buffers.
GLenum OpenGLRenderer::convertBufferUsage(BufferUsage usage, BufferType type) {
  if (type == BufferType::Storage) {
    // Spelled numerically: this vendored GLAD does not define the *_STORAGE
    // usage enums, though it does define GL_SHADER_STORAGE_BUFFER. These are
    // the GL 4.4 core values (Table 23.13) and are fixed by the spec.
    //
    // Note GL_DYNAMIC_STORAGE and GL_DYNAMIC_DRAW share the value 0x88E8,
    // and GL_STREAM_STORAGE (0x88E9) collides with GL_DYNAMIC_READ. Only the
    // target distinguishes them, which is why this branch exists.
    constexpr GLenum kStaticStorage = 0x88E0;
    constexpr GLenum kDynamicStorage = 0x88E8;
    constexpr GLenum kStreamStorage = 0x88E9;
    switch (usage) {
      case BufferUsage::Static:
        return kStaticStorage;
      case BufferUsage::Dynamic:
        return kDynamicStorage;
      case BufferUsage::Stream:
        return kStreamStorage;
    }
    return kStaticStorage;
  }
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