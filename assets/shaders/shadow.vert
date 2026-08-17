#version 450

// Depth-only pass from the light's point of view, run once per shadow cascade.
// Only the position matters; the other vertex attributes are declared so this
// pipeline can share the same vertex input layout as the scene pipeline.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;

// The cascade's light-space transform arrives through the push constant rather
// than being read out of the scene UBO by index.
//
// The UBO is per-frame and bound once, so a cascade index would have to be a
// second push constant anyway - and the depth pass has no use for any of the
// UBO's other 700-odd bytes. This also keeps this stage free of the UBO block
// entirely, so it cannot drift out of step with the other three declarations.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 cascadeViewProj;
} push;

void main() {
    gl_Position = push.cascadeViewProj * push.model * vec4(inPosition, 1.0);
}
