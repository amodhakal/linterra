#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class IRenderer;
class IBuffer;
class IVertexArray;
class IShader;
class IShaderProgram;
class ITexture;

enum class BufferType : uint8_t;
enum class BufferUsage : uint8_t;
enum class ShaderType : uint8_t;
enum class TextureType : uint8_t;
enum class PrimitiveType : uint8_t;
enum class DataType : uint8_t;
enum class IndexType : uint8_t;
enum class Feature : uint8_t;
enum class RenderBackend : uint8_t;