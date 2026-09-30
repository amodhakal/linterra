#pragma once
#include "camera.h"

class Frustum {
 public:
  Frustum(const Camera *camera);
  bool isChunkInside(const glm::ivec2 &position);

  Plane m_NearFace;
  Plane m_FarFace;
  Plane m_RightFace;
  Plane m_LeftFace;
  Plane m_TopFace;
  Plane m_BottomFace;
};
