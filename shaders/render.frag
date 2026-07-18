#version 330 core

// Scene pass: samples the texture atlas and applies directional face lighting.
// No fog here — that is handled by the separate fog post-process pass. The
// view-space distance is carried in the output alpha channel for the fog pass.

const int RIGHT_LEFT_NORMAL = 0;
const int FRONT_BACK_NORMAL = 1;
const int BOTTOM_NORMAL = 2;
const int TOP_NORMAL = 3;

uniform sampler2DArray uTextureArray;
uniform float uFogEnd;

flat in int fragNormal;
in vec3 fragPosition;
in vec3 fragTexData; // x = texId, y = u, z = v
in float fragViewDepth;

out vec4 outColor;

void main() {
    int texId = int(fragTexData.x);
    vec2 uv = fragTexData.yz;

    vec4 texColor = texture(uTextureArray, vec3(uv, float(texId)));

    float light = 1.0;
    if (fragNormal == RIGHT_LEFT_NORMAL) {
        light = 0.8;
    } else if (fragNormal == FRONT_BACK_NORMAL) {
        light = 0.9;
    } else if (fragNormal == TOP_NORMAL) {
        light = 1.0;
    } else if (fragNormal == BOTTOM_NORMAL) {
        light = 0.7;
    }

    vec3 shaded = texColor.rgb * light;
    outColor = vec4(shaded, clamp(fragViewDepth / uFogEnd, 0.0, 1.0));
}
