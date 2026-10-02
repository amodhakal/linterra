#pragma once
#include "camera.h"

#include <glm/glm.hpp>

class Frustum {
 public:
  Frustum(const Camera *camera);

  bool isChunkInside(const glm::ivec2 &position);

  /** True unless the whole axis-aligned box is strictly outside some plane.
   *
   *  The arbitrary-AABB form, so a cell of the spatial tree can be tested
   *  without instantiating the chunks inside it. Hierarchical culling needs
   *  this: a subtree that is entirely outside the view cone has to be droppable
   *  in one test, and isChunkInside's hardcoded chunk extents cannot express
   *  "a 64 x 64 chunk cell".
   *
   *  Conservative by construction, which is the property that makes it safe to
   *  prune with: a box is rejected only when ALL EIGHT of its corners are
   *  outside the SAME plane. A box that straddles a plane is kept, so anything
   *  the projection puts on screen survives as long as the box that contains it
   *  is tested with this function. tests/test_frustum.cpp checks that against
   *  the real projection rather than against a restatement of this rule.
   *
   *  Note the box is half-open in nothing and closed in everything: it is tested
   *  as the 8 corners, so a box exactly touching a plane is kept. Over-admitting
   *  at a boundary costs a draw call; under-admitting loses terrain. */
  bool isBoundsInside(const glm::vec3 &boxMin, const glm::vec3 &boxMax);

  Plane m_NearFace;
  Plane m_FarFace;
  Plane m_RightFace;
  Plane m_LeftFace;
  Plane m_TopFace;
  Plane m_BottomFace;
};
