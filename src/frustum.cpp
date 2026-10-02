#include "frustum.h"

#include <cmath>

#include "config.h"

Frustum::Frustum(const Camera *camera) {
  // m_Fov is in degrees, so it has to be converted before it reaches tan().
  // Camera::getProjection does the same conversion; without it here the
  // culling cone and the rendered cone disagree, and because tan() goes
  // negative across part of the FOV_MIN..FOV_MAX range the side-plane normals
  // invert and the frustum collapses.
  const float halfVSide =
      camera->m_Far * std::tan(glm::radians(camera->m_Fov) * 0.5f);
  const float halfHSide = halfVSide * camera->m_Aspect;
  const glm::vec3 frontMultFar = camera->m_Far * camera->m_Front;

  m_NearFace = {camera->m_Position + camera->m_Near * camera->m_Front,
                camera->m_Front};
  m_FarFace = {camera->m_Position + frontMultFar, -camera->m_Front};
  m_RightFace = {
      camera->m_Position,
      glm::cross(frontMultFar - camera->getRight() * halfHSide, camera->m_Up)};
  m_LeftFace = {
      camera->m_Position,
      glm::cross(camera->m_Up, frontMultFar + camera->getRight() * halfHSide)};
  m_TopFace = {
      camera->m_Position,
      glm::cross(camera->getRight(), frontMultFar - camera->m_Up * halfVSide)};
  m_BottomFace = {
      camera->m_Position,
      glm::cross(frontMultFar + camera->m_Up * halfVSide, camera->getRight())};
}

bool Frustum::isChunkInside(const glm::ivec2 &position) {
  // Chunk geometry occupies [pos*L, pos*L+L] in X/Z (see ChunkManager::render's
  // model translation and chunk.cpp's local block coordinates). The box must
  // match that exactly; previously it was offset by -L/2 (half a chunk).
  return isBoundsInside(
      {static_cast<float>(position.x * Constants::Chunk::LENGTH), 0.0f,
       static_cast<float>(position.y * Constants::Chunk::LENGTH)},
      {static_cast<float>(position.x * Constants::Chunk::LENGTH) +
           static_cast<float>(Constants::Chunk::LENGTH),
       static_cast<float>(Constants::Chunk::HEIGHT),
       static_cast<float>(position.y * Constants::Chunk::LENGTH) +
           static_cast<float>(Constants::Chunk::LENGTH)});
}

bool Frustum::isBoundsInside(const glm::vec3 &boxMin, const glm::vec3 &boxMax) {
  glm::vec3 chunkCorner1 = boxMin;
  glm::vec3 chunkCorner2 = boxMax;

  glm::vec3 corners[8];
  int i = 0;
  for (int xi = 0; xi < 2; ++xi) {
    for (int yi = 0; yi < 2; ++yi) {
      for (int zi = 0; zi < 2; ++zi) {
        corners[i++] = glm::vec3(xi ? chunkCorner2.x : chunkCorner1.x,
                                 yi ? chunkCorner2.y : chunkCorner1.y,
                                 zi ? chunkCorner2.z : chunkCorner1.z);
      }
    }
  }

  const Plane planes[6] = {m_TopFace,  m_BottomFace, m_RightFace,
                           m_LeftFace, m_FarFace,    m_NearFace};

  for (const Plane &p : planes) {
    int outsideCount = 0;
    for (const glm::vec3 &c : corners) {
      float distance = glm::dot(c - p.point, p.normal);
      if (distance < 0.0f) {
        ++outsideCount;
      }
    }
    if (outsideCount == 8) {
      return false;
    }
  }

  return true;
}
