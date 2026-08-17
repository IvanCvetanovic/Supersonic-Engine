#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientColor;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

// Must match Engine::PushConstantData.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedoColor;
    vec4 material;   // x = roughness, y = metallic, z = ao
} push;

const float PI = 3.14159265359;

// Cook-Torrance GGX Normal Distribution (Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0001);
}

// Schlick-GGX Geometry Shadowing, direct-lighting k remap
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / max(denom, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
}

// Schlick Fresnel
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4 albedoTex = texture(texSampler, fragTexCoord);
    vec3 albedo = albedoTex.rgb * fragColor * push.albedoColor.rgb;

    // Material parameters come from MaterialComponent via push constants, so
    // the Inspector's roughness/metallic controls affect the image.
    float roughness = clamp(push.material.x, 0.02, 1.0);
    float metallic  = clamp(push.material.y, 0.0, 1.0);
    float ao        = clamp(push.material.z, 0.0, 1.0);

    vec3 N = normalize(fragNormal);

    // Real camera position, so the specular highlight tracks the viewer instead
    // of staying pinned to a hardcoded point.
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Directional light from LightComponent.
    vec3 L = normalize(ubo.lightDirection.xyz);
    vec3 H = normalize(V + L);
    vec3 lightColor = ubo.lightColor.rgb * ubo.lightColor.a;

    // F0 is 0.04 for dielectrics, the albedo itself for metals.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * lightColor * NdotL;

    vec3 ambient = ubo.ambientColor.rgb * albedo * ao;
    vec3 color = ambient + Lo;

    // Reinhard tone map, then encode to sRGB.
    //
    // This manual encode is why the offscreen colour attachment is UNORM
    // (VulkanOffscreen::kColorFormat). An sRGB attachment would encode a second
    // time on store and nothing downstream cancels it, because ImGui samples
    // this image and blits it to the swapchain without colour conversion.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, albedoTex.a * push.albedoColor.a);
}
