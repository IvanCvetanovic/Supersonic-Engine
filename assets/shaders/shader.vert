#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;   // xyz tangent, w handedness

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
struct Light {
    vec4 positionOrDirection;   // xyz = position (point) or direction (dir), w = type
    vec4 colorAndIntensity;     // rgb = colour, a = intensity
    vec4 attenuation;           // x = range
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;      // view-space far depth per cascade
    vec4 cascadeTexelWorld;  // world size of one shadow texel per cascade
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

    // Normals need the inverse-transpose (non-uniform scale would shear them
    // off the surface); tangents are true directions and take the model matrix.
    mat3 normalMatrix = mat3(transpose(inverse(push.model)));
    fragNormal = normalMatrix * inNormal;
    fragTangent = mat3(push.model) * inTangent.xyz;
    // Handedness in w reconstructs the bitangent without storing it.
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;
    fragColor = inColor;
    fragTexCoord = inTexCoord;

    // No light-space position is interpolated any more: which cascade to use
    // depends on the fragment's view depth, and the lookup is offset along the
    // shaded normal, so both have to happen in the fragment stage.
}
