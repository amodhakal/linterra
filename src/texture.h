#pragma once
#include <sys/types.h>
#include <stb/image.h>
#include <vector>
#include <string>


class Texture {
 public:
  Texture() = default;
  ~Texture();

  void loadFromFiles(const std::vector<std::string>& paths);

  void bindToUnit(int unit) const;

  unsigned int getId() const { return m_Id; }

 private:
  unsigned int m_Id = 0;
  int m_Width = 0;
  int m_Height = 0;
  int m_Layers = 0;
};
