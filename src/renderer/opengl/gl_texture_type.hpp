#pragma once

// Shared GL enum translation for texture targets.
//
// This used to live in an anonymous namespace in opengl_texture.cpp, which
// meant OpenGLRenderer could not reach it and hardcoded GL_TEXTURE_2D_ARRAY
// instead. Two mappings of the same enum is exactly the kind of drift that
// produces "why is this 2D texture behaving like an array" -- so there is one
// definition, declared here and defined in opengl_texture.cpp.

#include <glad/glad.h>

#include "renderer/renderer_fwd.hpp"

GLenum textureTypeToGL(TextureType type);
