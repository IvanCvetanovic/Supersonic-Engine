#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec4 fragLightSpacePos;

layout(location = 0) out vec4 outColor;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
struct Light {
    vec4 positionOrDirection;   // xyz, w = type (0 = directional, 1 = point)
    vec4 colorAndIntensity;     // rgb, a = intensity
    vec4 attenuation;           // x = range
};

// Set 0: per-frame scene data.
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpace;
    vec4 cameraPosition;
    vec4 ambientColor;
    vec4 lightCount;
    Light lights[8];
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

// Set 1: per-material. Rebound per draw, which is what gives each entity its
// own texture instead of every object sampling one global checkerboard.
layout(set = 1, binding = 0) uniform sampler2D albedoMap;

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
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.0001);
}

// Schlick-GGX geometry term, direct-lighting k remap
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, L), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 3x3 PCF. Returns 1.0 when fully lit, 0.0 when fully shadowed.
float shadowFactor(vec4 lightSpacePos, float NdotL) {
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;

    // xy to [0,1] texture space. z is already [0,1] because the projection is
    // built with GLM_FORCE_DEPTH_ZERO_TO_ONE.
    vec2 uv = proj.xy * 0.5 + 0.5;

    // Outside the light frustum there is no information, so treat it as lit
    // rather than inventing a shadow on the far side of the scene.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }

    // Slope-scaled bias on top of the rasteriser's depth bias: grazing angles
    // need more margin, head-on surfaces need almost none.
    float bias = max(0.0015 * (1.0 - NdotL), 0.0004);

    float lit = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(shadowMap, uv + vec2(x, y) * texel).r;
            lit += (proj.z - bias) > closest ? 0.0 : 1.0;
        }
    }
    return lit / 9.0;
}

void main() {
    vec4 albedoTex = texture(albedoMap, fragTexCoord);
    vec3 albedo = albedoTex.rgb * fragColor * push.albedoColor.rgb;

    float roughness = clamp(push.material.x, 0.02, 1.0);
    float metallic  = clamp(push.material.y, 0.0, 1.0);
    float ao        = clamp(push.material.z, 0.0, 1.0);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    int count = min(int(ubo.lightCount.x), 8);

    for (int i = 0; i < count; ++i) {
        Light light = ubo.lights[i];

        vec3 L;
        float attenuation = 1.0;

        if (light.positionOrDirection.w < 0.5) {
            // Directional: xyz already points toward the light.
            L = normalize(light.positionOrDirection.xyz);
        } else {
            // Point: falls off with distance, cut off at its range.
            vec3 toLight = light.positionOrDirection.xyz - fragWorldPos;
            float dist = length(toLight);
            if (dist > light.attenuation.x) continue;
            L = dist > 0.0001 ? toLight / dist : vec3(0.0, 1.0, 0.0);
            attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        }

        vec3 H = normalize(V + L);
        vec3 radiance = light.colorAndIntensity.rgb * light.colorAndIntensity.a * attenuation;

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (NDF * G * F) /
            max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.0001);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);

        // Only the first light casts; it is the one the shadow map was
        // rendered from.
        float shadow = (i == 0) ? shadowFactor(fragLightSpacePos, NdotL) : 1.0;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadow;
    }

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
