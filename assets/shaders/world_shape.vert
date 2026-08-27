#version 450

// The world-space shape layer: lines drawn IN the scene, against its depth.
//
// Deliberately the plainest vertex shader in the tree. A shape is a position
// and a colour - no normal, no UV, no lighting - because an indicator, a range
// ring and a path preview are not surfaces and shading them would be wrong.

#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = inColor;
    gl_Position = ubo.proj * ubo.view * vec4(inPosition, 1.0);
}
