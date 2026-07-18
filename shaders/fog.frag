#version 330 core

// Fog post-process pass: composites exponential fog over the rendered scene
// using the view-space distance carried in the scene color's alpha channel.

uniform sampler2D uScene;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uFogColor;

in vec2 fragUV;

out vec4 outColor;

void main() {
    vec4 scene = texture(uScene, fragUV);
    vec3 color = scene.rgb;
    // Alpha holds the view distance normalized by uFogEnd (8-bit precision).
    float viewDepth = scene.a * uFogEnd;

    float fogFactor = clamp((viewDepth - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    outColor = vec4(mix(color, uFogColor, fogFactor), 1.0);
}
