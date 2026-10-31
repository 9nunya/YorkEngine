#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
} ubo;
layout(push_constant) uniform ObjectTransform {
    mat4 model;
} objectTransform;

layout(location = 0) out vec2 vUv;

void main() {
    gl_Position =
    ubo.proj * ubo.view * objectTransform.model
    * vec4(inPosition, 1.0);
    vUv = inUv;
}
