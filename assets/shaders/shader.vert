#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec4 fragLightSpacePos;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
struct Light {
    vec4 positionOrDirection;   // xyz = position (point) or direction (dir), w = type
    vec4 colorAndIntensity;     // rgb = colour, a = intensity
    vec4 attenuation;           // x = range
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
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    fragColor = inColor;
    fragTexCoord = inTexCoord;

    // Same transform the shadow pass rasterised with, so the comparison in the
    // fragment stage is consistent regardless of clip-space handedness.
    fragLightSpacePos = ubo.lightSpace * worldPos;
}
