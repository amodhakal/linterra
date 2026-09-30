#pragma once

#include <cstdint>
#include <memory>

#include <stb/image.h>
#include <string>
#include <vector>

class IRenderer;
class ITexture;

/** True if `dimension` is a power of two, as GL_REPEAT wrapping requires.
 *
 *  Written as an explicit zero check plus the usual bit trick, because the bit
 *  trick alone cannot reject zero: 0 & (0 - 1) == 0, so every bit of zero is
 *  already zero and a 0-sized dimension passes. That matters because the
 *  dimensions come from stbi_load decoding external image files rather than
 *  from compile-time constants. */
constexpr bool isPowerOfTwoDimension(std::int32_t dimension) noexcept {
  return dimension > 0 && (dimension & (dimension - 1)) == 0;
}

class Texture {
 public:
  explicit Texture(IRenderer* renderer);
  ~Texture();

  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& other) noexcept;
  Texture& operator=(Texture&& other) noexcept;

  void loadFromFiles(const std::vector<std::string>& paths);

  void bindToUnit(std::int32_t unit) const;

  [[nodiscard]] std::uint32_t getId() const;

 private:
  IRenderer* m_Renderer = nullptr;
  std::unique_ptr<ITexture> m_Texture;
  std::int32_t m_Width = 0;
  std::int32_t m_Height = 0;
  std::int32_t m_Layers = 0;
};
