#include "texture.h"

#include <glad/glad.h>

#include <stdexcept>
#include <string>
#include <vector>

void Texture::loadFromFiles(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    throw std::runtime_error("No texture paths provided");
  }

  std::vector<stbi_uc*> images;
  std::vector<int> widths;
  std::vector<int> heights;

  for (const auto& path : paths) {
    int width, height, channels;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
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

  m_Width = widths[0];
  m_Height = heights[0];
  m_Layers = static_cast<int>(paths.size());

  glGenTextures(1, &m_Id);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_Id);

  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, m_Width, m_Height, m_Layers, 0,
              GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

  for (int layer = 0; layer < m_Layers; ++layer) {
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, m_Width, m_Height, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, images[layer]);
  }

  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

  for (auto* img : images) {
    stbi_image_free(img);
  }
}

void Texture::bindToUnit(int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_Id);
}

Texture::~Texture() {
  if (m_Id != 0) {
    glDeleteTextures(1, &m_Id);
  }
}
