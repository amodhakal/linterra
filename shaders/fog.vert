#version 330 core

// Fullscreen-triangle post-process vertex shader. No vertex buffer needed:
// gl_VertexID drives a single oversized triangle that covers the screen and
// produces clip-space positions and [0,1] UVs in one cheap pass.

out vec2 fragUV;

void main() {
    // 0 -> (-1,-1), 1 -> (3,-1), 2 -> (-1,3): a triangle larger than the viewport.
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    fragUV = pos;                 // [0,2] range, interpolates across the screen
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
