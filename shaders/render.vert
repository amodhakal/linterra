#version 330 core

layout(location = 0) in uint inPacked;

out vec3 fragColor;
flat out int fragNormal;
out vec3 fragPosition;
out vec3 fragTexData;
// View-space distance from camera, packed into alpha so the separate fog
// post-process pass can reconstruct it without a depth-texture attachment.
out float fragViewDepth;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    int x = int(inPacked              ) & 0xFF;
    int z = int(inPacked >> 8        ) & 0xFF;
    int y = int(inPacked >> 16       ) & 0x3FF;
    int normal = int(inPacked >> 26  ) & 0x3;
    int texId = int(inPacked >> 28     ) & 0x3;
    int corner = int(inPacked >> 30    ) & 0x3;

    bool isU = (corner & 1) != 0;
    bool isV = (corner & 2) != 0;

    vec3 position = vec3(float(x), float(y), float(z));
    vec2 uv = vec2(isU ? 1.0 : 0.0, isV ? 1.0 : 0.0);
    vec3 texData = vec3(float(texId), uv.x, uv.y);

    vec4 clip = uProjection * uView * uModel * vec4(position, 1.0);
    gl_Position = clip;
    fragPosition = position;
    fragTexData = texData;
    fragNormal = normal;
    fragViewDepth = clip.w;
}
