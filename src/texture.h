#pragma once

#include <cstdint>
#include <memory>

#include <stb/image.h>
#include <string>
#include <vector>

class IRenderer;
class ITexture;

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
