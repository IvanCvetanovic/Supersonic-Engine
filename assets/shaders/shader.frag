#version 450

// Must match SpotLight::kMaxShadowCasters.

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;

layout(location = 0) out vec4 outColor;

// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp.
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

// One array image, sampled with a per-fragment layer. An array of separate
// sampler2Ds would need a dynamically-uniform index, and the cascade choice
// differs within a quad at every seam - which is undefined behaviour, not a
// style preference.
layout(set = 0, binding = 1) uniform sampler2DArray shadowMaps;

// One cube per point light that can cast. An array of cube samplers rather than
// a cube ARRAY image, so no optional device feature is needed; indexing it by
// the light loop's counter is dynamically uniform, which is what the rule
// actually requires. Must match PointShadow::kMaxShadowCasters.
layout(set = 0, binding = 3) uniform samplerCube pointShadowMaps[POINT_SHADOW_CASTERS];

// One layer per shadow-casting spot light. A 2D array rather than an array of
// samplers, because a spot is one frustum and is sampled exactly like a
// cascade - project, compare - so the layer index can travel in the light.
layout(set = 0, binding = 4) uniform sampler2DArray spotShadowMaps;

// Set 1: per-material. Rebound per draw, which is what gives each entity its
// own texture instead of every object sampling one global checkerboard.
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
// Tangent-space normal map. Materials without one sample a 1x1 flat
// (0.5, 0.5, 1.0) texture, so no branch is needed here.
layout(set = 1, binding = 1) uniform sampler2D normalMap;

// Must match Engine::PushConstantData.
// A push constant block must be declared identically in every stage of a
// pipeline that declares one, so these two ints are here even though the
// fragment stage never reads them.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedoColor;
    vec4 material;   // x = roughness, y = metallic, z = ao
    vec4 emissive;      // rgb added after shading, may exceed 1.0
    int skinPaletteBase;
    int skinJointCount;
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

// Which cascade covers this view depth. Returns 4 - one past the last - for
// anything beyond the shadow distance, and every caller must test for that
// BEFORE indexing, since both cascade arrays hold exactly four elements.
int selectCascade(float viewDepth) {
    int layer = 0;
    for (int i = 0; i < 4; ++i) {
        if (viewDepth > ubo.cascadeSplits[i]) layer = i + 1;
    }
    return layer;
}

// 3x3 PCF against one cascade. Returns 1.0 when fully lit, 0.0 when fully
// shadowed.
float sampleCascade(int layer, vec3 worldPos, vec3 N, float NdotL) {
    // Normal offset, scaled by the world size of a texel in THIS cascade. Moving
    // the lookup off the surface along its own normal is what removes acne
    // without the depth bias having to be large enough to detach contact
    // shadows - and it has to scale per cascade, because a distant cascade's
    // texel covers far more world space than a near one's.
    float texelWorld = ubo.cascadeTexelWorld[layer];
    float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    vec3 offsetPos = worldPos + N * (texelWorld * (1.0 + 2.0 * slope));

    vec4 lightSpacePos = ubo.cascadeViewProj[layer] * vec4(offsetPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;

    // xy to [0,1] texture space. z is already [0,1] because the projection is
    // built with GLM_FORCE_DEPTH_ZERO_TO_ONE.
    vec2 uv = proj.xy * 0.5 + 0.5;

    // Outside this cascade there is no information, so treat it as lit rather
    // than inventing a shadow.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0 || proj.z < 0.0) {
        return 1.0;
    }

    // A small residual constant bias, now that the normal offset does the work.
    float bias = 0.0006;

    float lit = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(shadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(shadowMaps, vec3(uv + vec2(x, y) * texel, float(layer))).r;
            lit += (proj.z - bias) > closest ? 0.0 : 1.0;
        }
    }
    return lit / 9.0;
}

float shadowFactor(vec3 worldPos, vec3 N, float NdotL) {
    float viewDepth = -(ubo.view * vec4(worldPos, 1.0)).z;

    int layer = selectCascade(viewDepth);
    if (layer >= 4) return 1.0;   // past the shadow distance

    float result = sampleCascade(layer, worldPos, N, NdotL);

    // Cross-fade the last tenth of a cascade into the next one. Without it the
    // split is a hard line across the ground where the penumbra width changes,
    // which reads as a rendering error rather than as a level-of-detail change.
    if (layer < 3) {
        float start = (layer == 0) ? 0.0 : ubo.cascadeSplits[layer - 1];
        float end = ubo.cascadeSplits[layer];
        float band = (end - start) * 0.1;
        if (band > 0.0 && viewDepth > end - band) {
            float t = clamp((viewDepth - (end - band)) / band, 0.0, 1.0);
            result = mix(result, sampleCascade(layer + 1, worldPos, N, NdotL), t);
        }
    }
    return result;
}

// Ambient arriving at a surface facing `dir`: sky above, ground bounce below,
// blended across the horizon. The blend is by height rather than a hard split,
// so a surface tilting past horizontal does not change colour in one step.
vec3 hemisphere(vec3 dir) {
    float up = dir.y * 0.5 + 0.5;
    return mix(ubo.ambientGround.rgb, ubo.ambientColor.rgb, up);
}

// Fresnel with roughness folded in. The plain Schlick term goes to white at
// grazing angles regardless of roughness, which on a rough ambient-lit surface
// produces a bright rim that is not there.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Karis's analytic fit to the split-sum environment BRDF, standing in for the
// usual lookup texture. Two fewer descriptor bindings and one fewer image to
// generate, for an error well below what is visible here.
vec2 EnvBRDFApprox(float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Shadowing for one point light, sampled by direction.
//
// The cube stores the same projected depth its faces were rendered with, so the
// value to compare against can be reconstructed from the distance along the
// major axis alone: the face looks straight down that axis with a 90-degree
// frustum, so the other two components move the fragment within the face and
// never along its depth. That is what makes this one texture fetch instead of
// six matrices in a uniform block. test_pointshadow pins the two against each
// other.
float pointShadowFactor(int slot, vec3 fragToLight, float range, float NdotL) {
    // The near plane must match PointShadow::kNearPlane.
    const float nearPlane = 0.05;
    float far = max(range, nearPlane * 2.0);

    vec3 magnitude = abs(fragToLight);
    float major = max(magnitude.x, max(magnitude.y, magnitude.z));
    if (major <= nearPlane) return 1.0;

    float a = far / (nearPlane - far);
    float b = (nearPlane * far) / (nearPlane - far);
    float current = (-a * major + b) / major;

    // Slope-scaled, in the same units as the stored depth. A constant bias is
    // either useless up close or peels the shadow off contact at a distance,
    // because projected depth is not linear in world units.
    float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    float bias = (0.0015 + 0.006 * slope) * (1.0 - current) * (1.0 - current) * 20.0;
    bias = clamp(bias, 0.00005, 0.02);

    // Four taps around the direction rather than a full PCF kernel: a cube
    // lookup is not separable, and the softening this buys is enough at the
    // resolution these maps run at.
    const vec3 offsets[5] = vec3[5](
        vec3( 0.0,  0.0,  0.0),
        vec3( 1.0,  1.0,  1.0),
        vec3(-1.0, -1.0,  1.0),
        vec3( 1.0, -1.0, -1.0),
        vec3(-1.0,  1.0, -1.0)
    );
    float radius = major * 0.006;

    float lit = 0.0;
    for (int tap = 0; tap < 5; ++tap) {
        float closest = texture(pointShadowMaps[slot], fragToLight + offsets[tap] * radius).r;
        lit += (current - bias <= closest) ? 1.0 : 0.0;
    }
    return lit * 0.2;
}

// How much of the cone reaches this fragment: 1 inside the inner angle,
// fading to 0 at the outer one.
//
// Compared as cosines, which is why the tests for this live in C++ against the
// same arithmetic: no inverse trigonometry per fragment, and the comparison
// flips because cosine decreases as the angle grows.
float spotCone(vec3 spotDirection, vec3 fromLight, float cosInner, float cosOuter) {
    float cosAngle = dot(spotDirection, fromLight);
    if (cosAngle <= cosOuter) return 0.0;
    if (cosAngle >= cosInner) return 1.0;

    float span = cosInner - cosOuter;
    if (span <= 1e-6) return 1.0;

    float t = (cosAngle - cosOuter) / span;
    // Smoothstep rather than linear: a linear ramp in cosine leaves a visible
    // crease where the falloff meets full brightness.
    return t * t * (3.0 - 2.0 * t);
}

// Shadowing for one spot light. The same projection the cascades use, because
// a spot is the same shape of problem: one frustum, one depth map, project and
// compare.
float spotShadowFactor(int slot, float NdotL) {
    vec4 lightSpace = ubo.spotViewProj[slot] * vec4(fragWorldPos, 1.0);
    if (lightSpace.w <= 0.0) return 1.0;

    vec3 projected = lightSpace.xyz / lightSpace.w;

    // Outside the cone's frustum there is no depth to compare against, and
    // treating "no data" as shadow would put a black border around every spot.
    if (projected.z < 0.0 || projected.z > 1.0) return 1.0;

    vec2 uv = projected.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;

    // Slope-scaled: a surface nearly edge-on to the light needs more bias, and
    // a constant large enough for it would peel the shadow off contact points.
    float bias = clamp(0.0015 * tan(acos(clamp(NdotL, 0.0, 1.0))), 0.0005, 0.01);

    // 3x3, like the cascades, so the two kinds of shadow soften alike.
    float lit = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(spotShadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(spotShadowMaps,
                                    vec3(uv + vec2(x, y) * texel, float(slot))).r;
            lit += (projected.z - bias <= closest) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

void main() {
    vec4 albedoTex = texture(albedoMap, fragTexCoord);

    // Alpha cutout, before anything is shaded.
    //
    // A leaf card, a chain-link fence, a grate: surfaces that are mostly holes.
    // Without this they had to be marked transparent and go through the blended
    // pass, where they sort against THEMSELVES - one leaf card in front of
    // another composites in whichever order the sort happened to pick, and the
    // result flickers as the camera moves. A hard edge is what they are asking
    // for, and a discard is what draws one.
    //
    // Zero means no cutout, so the disabled path costs one compare rather than
    // a shader variant - the same reason a fog density of zero is how fog is
    // turned off. The test is against the SAME alpha the blend pass would have
    // used, texture times factor, so a material reads the same either way.
    //
    // Early, because a discarded fragment should not pay for eight lights and
    // eighteen shadow taps first.
    float alphaCutoff = push.material.w;
    if (alphaCutoff > 0.0 && albedoTex.a * push.albedoColor.a < alphaCutoff) {
        discard;
    }

    vec3 albedo = albedoTex.rgb * fragColor * push.albedoColor.rgb;

    float roughness = clamp(push.material.x, 0.02, 1.0);
    float metallic  = clamp(push.material.y, 0.0, 1.0);
    float ao        = clamp(push.material.z, 0.0, 1.0);

    // Re-orthonormalise the interpolated basis: interpolation across a
    // triangle does not preserve orthogonality, and a skewed basis tilts the
    // mapped normal.
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent - N * dot(N, fragTangent));
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);

    // Unpack from [0,1] to [-1,1] and rotate into world space.
    vec3 sampledNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
    N = normalize(TBN * sampledNormal);

    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    int count = min(int(ubo.lightCount.x), 8);

    for (int i = 0; i < count; ++i) {
        Light light = ubo.lights[i];

        vec3 L;
        float attenuation = 1.0;

        float cone = 1.0;

        if (light.positionOrDirection.w < 0.5) {
            // Directional: xyz already points toward the light.
            L = normalize(light.positionOrDirection.xyz);
        } else {
            // Point and spot both fall off with distance from a position.
            vec3 toLight = light.positionOrDirection.xyz - fragWorldPos;
            float dist = length(toLight);
            if (dist > light.attenuation.x) continue;
            L = dist > 0.0001 ? toLight / dist : vec3(0.0, 1.0, 0.0);

            // Windowed inverse-square, not the legacy constant table.
            //
            // The old curve was 1/(1 + 0.09d + 0.032d^2) with `range` used only
            // as a hard cutoff. Two problems followed from that. It is not
            // inverse-square, so `intensity` meant nothing physical and every
            // light had to be re-tuned by eye whenever its distance changed.
            // And at d = range the curve has NOT reached zero - at range 25 it
            // evaluates to about 0.043 - so the `continue` above chopped it off
            // mid-slope and every point light ended in a visible hard-edged
            // sphere.
            //
            // The window is Karis's: (1 - (d/range)^4)^2, clamped, which
            // reaches exactly zero AT range and meets the cutoff smoothly. The
            // +1 in the denominator keeps the term finite as d approaches zero,
            // where true inverse-square goes to infinity.
            float distOverRange = dist / max(light.attenuation.x, 0.0001);
            float window = clamp(1.0 - distOverRange * distOverRange * distOverRange * distOverRange,
                                 0.0, 1.0);
            attenuation = (window * window) / (dist * dist + 1.0);

            if (light.positionOrDirection.w > 1.5) {
                // A spot is a point light that only shines within a cone. -L
                // runs from the light toward the fragment, the same way the
                // cone points.
                cone = spotCone(normalize(light.spotDirection.xyz), -L,
                                light.attenuation.z, light.spotDirection.w);
                if (cone <= 0.0) continue;
                attenuation *= cone;
            }
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

        // The first light is the one the cascades were rendered from. Point
        // lights carry the index of their own cube, or -1 when they missed out
        // on one, so a lamp shining through a wall is no longer the only
        // possible outcome.
        float shadow = 1.0;
        if (i == 0 && light.positionOrDirection.w < 0.5) {
            shadow = shadowFactor(fragWorldPos, N, NdotL);
        } else if (light.positionOrDirection.w > 1.5) {
            int spotSlot = int(light.attenuation.w);
            if (spotSlot >= 0 && spotSlot < SPOT_SHADOW_CASTERS) {
                shadow = spotShadowFactor(spotSlot, NdotL);
            }
        } else if (light.positionOrDirection.w >= 0.5) {
            int slot = int(light.attenuation.y);
            if (slot >= 0 && slot < POINT_SHADOW_CASTERS) {
                shadow = pointShadowFactor(slot, fragWorldPos - light.positionOrDirection.xyz,
                                           light.attenuation.x, NdotL);
            }
        }

        Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadow;
    }

    // Ambient, hemispherically. A flat term lit the underside of everything
    // exactly as brightly as its top, which is the single most obvious way a
    // render reads as untextured plastic - nothing outdoors is lit like that.
    vec3 irradiance = hemisphere(N);

    // Metals have no diffuse response at all. The old term multiplied ambient
    // by albedo unconditionally, so a mirror picked up a flat wash of ambient
    // colour it should not have had - the one case where ambient was not
    // merely crude but wrong.
    vec3 F_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD_ambient = (vec3(1.0) - F_ambient) * (1.0 - metallic);

    vec3 ambientDiffuse = irradiance * albedo * kD_ambient;

    // And a specular lobe along the reflection, so a smooth surface picks up
    // the sky where it is pointing rather than an average of everything. The
    // split-sum BRDF is Karis's analytic fit, which is close enough at this
    // scale to not be worth a lookup texture and a descriptor binding.
    vec3 R = reflect(-V, N);
    vec2 envBRDF = EnvBRDFApprox(roughness, max(dot(N, V), 0.0));
    vec3 ambientSpecular = hemisphere(R) * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient = (ambientDiffuse + ambientSpecular) * ao;
    vec3 color = ambient + Lo;

    // Linear, unbounded, un-encoded.
    //
    // Tone mapping and the sRGB encode used to happen here, which was right
    // while this shader produced the final image. The scene target is now
    // floating point and feeds a bloom chain, so squashing values into 0..1
    // here would destroy exactly the above-white highlights bloom exists to
    // find. Both steps moved to bloom_composite.frag, which is now the only
    // place either happens.
    // Emission, added after everything else and after ambient occlusion.
    //
    // A surface that emits light is not lit BY anything, so it must not be
    // scaled by shadow, by attenuation or by AO - occluding a lamp's own glow
    // because it sits in a corner is exactly the artefact that makes emissive
    // materials look painted on rather than lit.
    //
    // Not clamped. Above 1.0 is the point: the target is floating point and the
    // bright pass thresholds at 1.0, so an intensity above one is how a
    // material is authored to actually glow rather than merely to be pale.
    color += push.emissive.rgb;

    // Distance fog, applied last and in LINEAR space.
    //
    // Last because fog is what the air between the camera and the surface does
    // to the light leaving it, so anything added afterwards - emission most
    // obviously - would arrive at the eye undimmed by a kilometre of haze.
    // Linear because this shader's output feeds the bloom chain and the tone
    // map; fogging after the encode would blend two display-referred colours
    // and produce a haze that is too dark in the midtones.
    //
    // exp2(-(d * density)^2) rather than a linear ramp between two distances:
    // it needs one parameter instead of two, it never produces a visible edge
    // where the ramp starts, and it is exactly 1.0 when density is 0 - so
    // "no fog" is the same arithmetic rather than a branch that can disagree
    // with it.
    float fogDensity = ubo.fogColorAndDensity.a;
    if (fogDensity > 0.0) {
        float viewDistance = length(ubo.cameraPosition.xyz - fragWorldPos);
        float f = viewDistance * fogDensity;
        float visibility = clamp(exp2(-f * f), 0.0, 1.0);
        color = mix(ubo.fogColorAndDensity.rgb, color, visibility);
    }

    outColor = vec4(color, albedoTex.a * push.albedoColor.a);
}
