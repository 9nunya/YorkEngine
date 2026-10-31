#version 450

layout(location = 0) in vec2 vUv;

// Shared push block with the vertex shader: the model matrix at [0, 64) is
// unused here; the material parameters are read from [64, 88).
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float alphaCutoff;
    uint reservedAlphaMode;
} push;

layout(set = 1, binding = 0) uniform sampler2D materialSampler;

layout(location = 0) out vec4 fragColor;

void main() {
    // Base-color textures are sRGB-encoded; their view format converts them
    // into linear values while sampling, and the glTF factor is already linear.
    fragColor = texture(materialSampler, vUv) * push.baseColorFactor;
}
