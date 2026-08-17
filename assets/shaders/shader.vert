#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPos;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientColor;
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
}
