#version 450

// Depth-only pass from the light's point of view. Only the position matters;
// the other vertex attributes are declared so this pipeline can share the same
// vertex input layout as the scene pipeline.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
struct Light {
    vec4 positionOrDirection;
    vec4 colorAndIntensity;
    vec4 attenuation;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpace;
    vec4 cameraPosition;
    vec4 ambientColor;
    vec4 lightCount;
    Light lights[8];
} ubo;

// Must match Engine::PushConstantData.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedoColor;
    vec4 material;
} push;

void main() {
    gl_Position = ubo.lightSpace * push.model * vec4(inPosition, 1.0);
}
