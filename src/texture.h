#pragma once

#include <cstdint>

#include <stb/image.h>
#include <string>
#include <vector>

class Texture {
 public:
  Texture() = default;
  ~Texture();

  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& other) noexcept;
  Texture& operator=(Texture&& other) noexcept;

  void loadFromFiles(const std::vector<std::string>& paths);

  void bindToUnit(std::int32_t unit) const;

  [[nodiscard]] std::uint32_t getId() const { return m_Id; }

 private:
  std::uint32_t m_Id = 0;
  std::int32_t m_Width = 0;
  std::int32_t m_Height = 0;
  std::int32_t m_Layers = 0;
};
