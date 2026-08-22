#define STB_IMAGE_IMPLEMENTATION

#include "texture.h"

#include <glad/glad.h>
#include <stdexcept>
#include <utility>
#include <vector>

#include "renderer/renderer.hpp"

Texture::Texture(IRenderer* renderer) : m_Renderer(renderer) {}

Texture::Texture(Texture&& other) noexcept
    : m_Renderer(other.m_Renderer),
      m_Texture(std::move(other.m_Texture)),
      m_Width(other.m_Width),
      m_Height(other.m_Height),
      m_Layers(other.m_Layers) {
  other.m_Renderer = nullptr;
  other.m_Width = 0;
  other.m_Height = 0;
  other.m_Layers = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
  if (this != &other) {
    m_Renderer = other.m_Renderer;
    m_Texture = std::move(other.m_Texture);
    m_Width = other.m_Width;
    m_Height = other.m_Height;
    m_Layers = other.m_Layers;
    other.m_Renderer = nullptr;
    other.m_Width = 0;
    other.m_Height = 0;
    other.m_Layers = 0;
  }
  return *this;
}

void Texture::loadFromFiles(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    throw std::runtime_error("No texture paths provided");
  }

  std::vector<stbi_uc*> images;
  std::vector<int> widths;
  std::vector<int> heights;

  for (const auto& path : paths) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
      for (auto* img : images) {
        stbi_image_free(img);
      }
      throw std::runtime_error(std::string("Failed to load texture: ") + path);
    }
    images.push_back(data);
    widths.push_back(width);
    heights.push_back(height);
  }

  // Validate texture array inputs: differing image sizes would corrupt the
  // array, and non-power-of-two textures combined with GL_REPEAT cause seams.
  m_Width = widths[static_cast<size_t>(0)];
  m_Height = heights[static_cast<size_t>(0)];
  for (size_t i = 1; i < paths.size(); ++i) {
    if (widths[i] != m_Width || heights[i] != m_Height) {
      for (auto* img : images) {
        stbi_image_free(img);
      }
      throw std::runtime_error(
          std::string("Texture size mismatch for '") + paths[i] + "': got " +
          std::to_string(widths[i]) + "x" + std::to_string(heights[i]) +
          ", expected " + std::to_string(m_Width) + "x" +
          std::to_string(m_Height));
    }
  }
  if ((m_Width & (m_Width - 1)) != 0 || (m_Height & (m_Height - 1)) != 0) {
    for (auto* img : images) {
      stbi_image_free(img);
    }
    throw std::runtime_error(
        "Texture dimensions must be power-of-two for GL_REPEAT wrapping "
        "(got " + std::to_string(m_Width) + "x" + std::to_string(m_Height) +
        ")");
  }

  m_Layers = static_cast<std::int32_t>(paths.size());

  m_Texture = m_Renderer->createTexture(TextureType::Texture2DArray);

  m_Texture->bind(0);

  m_Renderer->setTextureParameter(*m_Texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
  m_Renderer->setTextureParameter(*m_Texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
  m_Renderer->setTextureParameter(*m_Texture, GL_TEXTURE_MIN_FILTER,
                                  GL_LINEAR_MIPMAP_LINEAR);
  m_Renderer->setTextureParameter(*m_Texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  m_Renderer->setTextureImage2DArray(*m_Texture, m_Width, m_Height, m_Layers, nullptr);

  for (std::int32_t layer = 0; layer < m_Layers; ++layer) {
    m_Texture->bind(0);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, m_Width, m_Height, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, images[static_cast<size_t>(layer)]);
  }

  m_Renderer->generateMipmaps(*m_Texture);

  for (auto* img : images) {
    stbi_image_free(img);
  }
}

void Texture::bindToUnit(std::int32_t unit) const {
  if (m_Texture) {
    m_Texture->bind(unit);
  }
}

std::uint32_t Texture::getId() const {
  return m_Texture ? m_Texture->getId() : 0;
}

Texture::~Texture() = default;