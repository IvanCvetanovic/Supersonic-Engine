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
// a cube ARRAY image, so no optional device feature is needed.
//
// It is sampled at CONSTANT indices in pointShadowFactor, with the slot only
// selecting between the results. That used to be unnecessary: the slot came from
// the light at the loop counter, so it was uniform across a quad. The loop walks
// a per-froxel list now, two fragments of one quad can sit in different froxels,
// and the index stopped being uniform - which this device has no feature enabled
// to permit. Must match PointShadow::kMaxShadowCasters.
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
// Occlusion, roughness and metallic packed into one image, in the channels
// glTF packs them into: R is ambient occlusion, G roughness, B metallic.
//
// One map rather than three: that is what an exporter writes, what an author
// paints, and it is one sampler and one descriptor rather than three for data
// that is a byte each. Materials without one sample a 1x1 white DATA texture,
// so the multiply below is a no-op and no branch is needed here either.
layout(set = 1, binding = 2) uniform sampler2D ormMap;
// An additive map, sampled at the surface's coordinates and added after the
// multiply by the 2D sprite path only (shadeSprite2D). Materials without one
// bind a 1x1 black texture, so the add needs no branch either.
layout(set = 1, binding = 3) uniform sampler2D overlayMap;
// How much of a 2D light's specular highlight each texel takes, per channel,
// sampled by shadeSprite2D alone and only while the draw's specular strength is
// above zero. Materials without one bind a 1x1 white texture: a strength alone
// is then a uniform gloss.
layout(set = 1, binding = 4) uniform sampler2D glossMap;

// Must match Engine::PushConstantData.
// A push constant block must be declared identically in every stage of a
// pipeline that declares one, so these two ints are here even though the
// fragment stage never reads them.
// PER-DRAW DATA, ONE ENTRY PER INSTANCE.
//
// This was a push constant, and the block was exactly 128 bytes - the
// guaranteed minimum, with the comment on `flags` saying outright that nothing
// else fits. It moves here for a different reason though: a push constant is
// written per draw, so every drawable cost a vkCmdPushConstants and a
// vkCmdDrawIndexed of its own. Read from a buffer and indexed by the instance,
// a thousand identical cubes are ONE draw.
//
// The layout is unchanged and still matches Engine::PushConstantData byte for
// byte - std430 gives mat4 a 16-byte alignment and a 64-byte size, each vec4
// 16, each int 4, for 128 with no padding, which is the stride the C++ side
// asserts.
struct InstanceData {
    mat4 model;
    vec4 albedoColor;
    vec4 material;      // x = roughness, y = metallic, z = ao, w = alpha cutoff
    vec4 emissive;      // rgb added after shading, may exceed 1.0; w = occlusion
    int skinPaletteBase;   // -1 = not skinned
    int skinJointCount;
    int probeIndex;        // which environment lights this draw
    int flags;             // bit 0 = unlit, 1 = 2D sprite, 2 = normal y down,
                           // 3 = premultiplied; bits 8..19 = uv slot, 20..27 = 2D light mask
};

layout(std430, set = 0, binding = 11) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

// Handed over by the vertex stage - see shader.vert.
layout(location = 6) flat in int fragInstance;

// Must match VulkanPipeline::PushConstantData::kUnlit.
const int FLAG_UNLIT = 1;

// Must match PushConstantData::kSprite2D, kNormalYDown, kPremultiplied and
// kVertical2D, and Components.hpp's kLightMaskShift and kLightMaskBits.
// test_materials reads these lines. The mask, the normal switch and the
// stand-up are read by shadeSprite2D's light loop.
const int FLAG_SPRITE2D      = 1 << 1;
const int FLAG_NORMAL_Y_DOWN = 1 << 2;
const int FLAG_PREMULTIPLIED = 1 << 3;
const int FLAG_VERTICAL_2D   = 1 << 4;
// Must match PushConstantData::kBakedEye2D and kLightAlphaTest2D. Both are read
// only by shadeSprite2D's light loop, and only when set.
const int FLAG_BAKED_EYE_2D  = 1 << 5;
const int FLAG_LIGHT_ALPHA_TEST_2D = 1 << 6;
const int LIGHT_MASK_SHIFT   = 20;
const uint LIGHT_MASK_BITS   = 0xFFu;

// Set 0, binding 12: every 2D point light in the frame (Light2DComponent),
// gathered by Light2D::GatherLights2D. Must match core/Light2D.hpp's GpuLight2D
// and GpuLight2DHeader: std430 puts the array at 16, after the count, the
// specular eye's two numbers and the pass alpha's intensity (a word of padding
// until the alpha test needed it), with a 32-byte stride. The
// renderer writes the header every frame, zero included, because the loop below
// reads the count for every 2D sprite.
struct Light2D {
    vec3  position;   // world x, world y; z = the light's height
    float range;
    vec3  color;      // colour x intensity, may exceed 1
    uint  layers;
};
layout(std430, set = 0, binding = 12) readonly buffer Light2DBuffer {
    uint  count;
    float eyeMirrorY;   // Light2DEye: a light at L is seen from
    float eyeHeight;    //   (L.x, 2 * eyeMirrorY - L.y, eyeHeight)
    float passAlphaIntensity;   // Light2DAlphaTest; 0 without one
    Light2D lights[];
} light2D;

// Must match kMaxLights2D. The buffer is sized for this many, so a count above
// it (which the renderer never writes) would read past the end.
const uint MAX_LIGHTS_2D = 64u;

// Light2DComponent::baked, above the layer byte of a light's layers. Must match
// kLight2DBakedBit.
const uint LIGHT_BAKED_BIT = 0x100u;

// Direct3D 9's ALPHAREF 1 with GREATER on an 8-bit alpha: a light pass whose
// alpha rounds to 1/255 or less draws nothing. Must match kLightPassAlphaRef.
const float LIGHT_PASS_ALPHA_REF = 1.5 / 255.0;

// The UV slot lives in the twelve bits ABOVE the switches. Must match
// PushConstantData::kUvSlotShift and kUvSlotMask.
const int UV_SLOT_SHIFT = 8;
const int UV_SLOT_MASK = 0xFFF;

// Must match Supersonic::UvTransform. std430 gives this a 32-byte stride, which
// is what the C++ side asserts its own size againstances[fragInstance].
struct UvTransform {
    vec4 axes;      // xy = where U points, zw = where V points
    vec4 offset;    // xy = translation, zw unused
};

// Every texture coordinate transform in the frame. SLOT 0 IS ALWAYS THE
// IDENTITY and is written whether or not anything scrolls, which is what lets
// the lookup below run with no branch and no bounds test: a draw that never
// asked for a transform pushed a zero and reads its coordinates back unchanged.
layout(std430, set = 0, binding = 10) readonly buffer UvTransformBuffer {
    UvTransform transforms[];
} uvBuffer;

// The one place texture coordinates are turned into the ones actually sampled.
//
// Called ONCE and the result reused for all three maps, rather than per sample.
// Albedo, normal and ORM describe the same surface, and sliding one off the
// others is never what anybody meant - a normal map that scrolls while its
// albedo stands still lights a texture that is not there.
vec2 transformedUV(vec2 uv) {
    UvTransform t = uvBuffer.transforms[(instances[fragInstance].flags >> UV_SLOT_SHIFT) & UV_SLOT_MASK];
    return mat2(t.axes.x, t.axes.y, t.axes.z, t.axes.w) * uv + t.offset.xy;
}

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
    float viewDepth = viewDepthOf(worldPos);

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

    // Both cubes sampled at CONSTANT indices, and the slot only chooses between
    // the results. That looks wasteful and is the correctness fix.
    //
    // `slot` comes out of a light this fragment found in its own froxel, and two
    // fragments of the same quad can be in different froxels - so slot is no
    // longer uniform across a quad, which is exactly what indexing a descriptor
    // ARRAY with it requires. This device enables neither
    // shaderSampledImageArrayNonUniformIndexing nor even the dynamic-indexing
    // feature, so the old form was undefined behaviour that validated cleanly
    // and rendered correctly on every desktop driver anyone would try it on.
    //
    // Cheap at two casters: every cube is written whether or not a light claimed
    // it, so the unused sample is a read of a valid image. Anybody raising
    // PointShadow::kMaxShadowCasters past a handful has to revisit this rather
    // than lengthen the chain.
    float lit = 0.0;
    for (int tap = 0; tap < 5; ++tap) {
        vec3 dir = fragToLight + offsets[tap] * radius;
        float closest = (slot == 0) ? texture(pointShadowMaps[0], dir).r
                                    : texture(pointShadowMaps[1], dir).r;
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

// A 2D sprite (PushConstantData::kSprite2D): the texel times the tint times the
// ambient, plus the overlay, clamped the way a fixed-point target clamps one
// draw's output, plus one clamped term per 2D light that reaches it. The
// record's fields mean what that path gives them: albedoColor.rgb is already
// tint x ambient (RenderSystem::ApplySprite2D), material.x is the overlay's
// strength, emissive.rgb is the tint WITHOUT the ambient and emissive.w the
// surface's lighting height. material.y is a standing sprite's base line
// (FLAG_VERTICAL_2D), material.z the specular strength, and probeIndex the
// specular power's bits while that strength is above zero.
//
// The overlay is NOT scaled by the tint or the ambient: a baked light term
// already carries its surface's albedo, and the engines it reproduces add it
// straight (Ethanon's add1.ps, gl_FragColor = v_color * diffuse + t1).
//
// The light loop has a CPU twin, Light2D::WorldNormal, StandUp, Contribution
// and SpecularContribution (core/Light2D.cpp), which test_light2d tests. Change
// both or neither.
vec4 shadeSprite2D(vec2 uv, vec4 albedoTex) {
    int flags = instances[fragInstance].flags;

    // A fully transparent texel carries no colour, so it cannot show through a
    // premultiplied blend, and will not be lit.
    vec3 texel = albedoTex.a > 0.0 ? albedoTex.rgb * fragColor : vec3(0.0);
    float alpha = albedoTex.a * instances[fragInstance].albedoColor.a;

    vec3 base = clamp(texel * instances[fragInstance].albedoColor.rgb
                      + texture(overlayMap, uv).rgb * instances[fragInstance].material.x,
                      0.0, 1.0);

    // Each light's add is clamped on its own, the way a fixed-point target
    // clamps the separate One, One draw a GLES2-era engine gives every light.
    // The sum is not clamped here: a float target keeps it, and the composite
    // clamps once, which equals clamping after every add of non-negative terms.
    vec3 lit = vec3(0.0);
    uint mask = (uint(flags) >> LIGHT_MASK_SHIFT) & LIGHT_MASK_BITS;
    uint count = min(light2D.count, MAX_LIGHTS_2D);
    if (mask != 0u && count > 0u) {
        // Decoded, not renormalised: the length of the stored vector is part of
        // the look. A material without a normal map samples the flat
        // (0.5, 0.5, 1.0) texture, which decodes to straight out of the screen.
        vec3 c = texture(normalMap, uv).rgb * 2.0 - 1.0;
        if ((flags & FLAG_NORMAL_Y_DOWN) != 0) c.y = -c.y;

        // The sprite's own axes, so a rotated or mirrored sprite turns its
        // normals with it. z is towards the viewer, where the heights are.
        mat4 model = instances[fragInstance].model;
        vec3 n = normalize(model[0].xyz) * c.x + normalize(model[1].xyz) * c.y + vec3(0.0, 0.0, c.z);

        // emissive.w is the lighting height; the fragment's own z is only its
        // draw depth, a slot in the sprites' order.
        vec3 p = vec3(fragWorldPos.xy, instances[fragInstance].emissive.w);

        // STOOD UP (Sprite2DLight::vertical): the flat frame above turned a
        // quarter turn about the world x axis, through the base line
        // (material.y) at the surface's height, the point and the normal
        // alike. A row higher up the sprite is higher in the lighting space,
        // every row stands on the base line, and the image's face looks down
        // the screen, towards -y - Ethanon's verticalSprite_ppl and
        // vPixelLight's (n.x, n.z, -n.y), mirrored into a y-up world.
        if ((flags & FLAG_VERTICAL_2D) != 0) {
            float baseY = instances[fragInstance].material.y;
            p = vec3(fragWorldPos.x, baseY, p.z + (fragWorldPos.y - baseY));
            n = vec3(n.x, -n.z, n.y);
        }

        // emissive.rgb is the colour WITHOUT ambient: a light is not dimmed by
        // the room it is in.
        vec3 tint = texel * instances[fragInstance].emissive.rgb;

        // THE LIGHT PASS'S ALPHA TEST (Sprite2DLight::lightAlphaTest). Ethanon
        // drew each light as its own pass under Direct3D 9's alpha test, and a
        // pass whose alpha came out below LIGHT_PASS_ALPHA_REF drew nothing.
        // That alpha is everything the pass multiplied except the light's
        // colour, whose alpha Ethanon held at 1: the texel's alpha (twice for a
        // flat sprite, whose whole pass hPixelLight weights by it), the draw's,
        // the facing, the falloff and the scene-wide intensity
        // (Light2DAlphaTest; 1 when the frame gives none). Off, no light is
        // skipped and the arithmetic below is what it always was.
        bool alphaTest = (flags & FLAG_LIGHT_ALPHA_TEST_2D) != 0;
        float passIntensity = light2D.passAlphaIntensity > 0.0 ? light2D.passAlphaIntensity : 1.0;
        float colorAlpha = instances[fragInstance].albedoColor.a;
        float passAlphaScale = albedoTex.a * colorAlpha * passIntensity
                             * ((flags & FLAG_VERTICAL_2D) != 0 ? 1.0 : albedoTex.a);

        // material.z is the specular strength, and zero is no highlight: this
        // loop is then the one the path always ran, unchanged, rather than a
        // second one fed zeros that a compiler may contract differently.
        float specularStrength = instances[fragInstance].material.z;
        if (specularStrength <= 0.0) {
            for (uint i = 0u; i < count; ++i) {
                if ((light2D.lights[i].layers & mask) == 0u) continue;
                vec3 v = light2D.lights[i].position - p;
                float d2 = dot(v, v);
                float r2 = light2D.lights[i].range * light2D.lights[i].range;
                if (d2 >= r2) continue;                       // the falloff is exactly 0 there
                float attenuation = 1.0 - d2 / r2;
                float facing = dot(v, n) * inversesqrt(max(d2, 1e-12));
                if (alphaTest && passAlphaScale * attenuation * facing < LIGHT_PASS_ALPHA_REF) continue;
                // A light behind the surface is a negative colour, clamped to
                // nothing rather than subtracted.
                lit += clamp(tint * light2D.lights[i].color * (attenuation * facing), 0.0, 1.0);
            }
        } else {
            // THE HIGHLIGHT (Sprite2DLight::specularStrength): Blinn, from the
            // frame's fake eye, times the gloss, the strength and the texel's
            // OWN alpha - hPixelLight/vPixelLight's mainSpecular. The power
            // rides in probeIndex as a float's bits; only the PBR exit reads
            // that field as an index, and a sprite never reaches it.
            vec4 glossTexel = texture(glossMap, uv);
            vec3 gloss = glossTexel.rgb * (specularStrength * albedoTex.a);
            float specularPower = intBitsToFloat(instances[fragInstance].probeIndex);
            // The highlight's share of the pass's alpha (the test above).
            float glossAlpha = glossTexel.a * specularStrength;

            // THE BAKED EYE (Sprite2DLight::bakedEye): a light marked baked is
            // seen from the sprite's own fixed eye, as Ethanon's lightmap bake
            // saw it - the light's x, the y that rides in skinJointCount, and
            // the frame eye's height above the sprite's own height. Only on a
            // draw that is not skinned, which is the only kind that carries it.
            bool bakedEye = (flags & FLAG_BAKED_EYE_2D) != 0 && instances[fragInstance].skinPaletteBase < 0;
            float bakedEyeY = intBitsToFloat(instances[fragInstance].skinJointCount);
            float bakedEyeZ = instances[fragInstance].emissive.w + light2D.eyeHeight;

            for (uint i = 0u; i < count; ++i) {
                if ((light2D.lights[i].layers & mask) == 0u) continue;
                vec3 v = light2D.lights[i].position - p;
                float d2 = dot(v, v);
                float r2 = light2D.lights[i].range * light2D.lights[i].range;
                if (d2 >= r2) continue;
                float attenuation = 1.0 - d2 / r2;
                float facing = dot(v, n) * inversesqrt(max(d2, 1e-12));

                // The eye is a point PER LIGHT: the light mirrored across the
                // line y = eyeMirrorY, at a fixed height (Light2DEye). Both
                // halves of the half vector are guarded like the facing, and
                // so is their sum, which is zero when eye and light are exactly
                // opposite.
                vec3 l = light2D.lights[i].position;
                vec3 e = (bakedEye && (light2D.lights[i].layers & LIGHT_BAKED_BIT) != 0u)
                       ? vec3(l.x, bakedEyeY, bakedEyeZ) - p
                       : vec3(l.x, 2.0 * light2D.eyeMirrorY - l.y, light2D.eyeHeight) - p;
                vec3 h = v * inversesqrt(max(d2, 1e-12)) + e * inversesqrt(max(dot(e, e), 1e-12));
                h *= inversesqrt(max(dot(h, h), 1e-12));
                float nh = clamp(dot(n, h), 0.0, 1.0);
                float shine = nh > 0.0 ? pow(nh, specularPower) : 0.0;

                if (alphaTest && albedoTex.a * attenuation
                                     * (colorAlpha * facing * passIntensity + shine * glossAlpha * passIntensity)
                                 < LIGHT_PASS_ALPHA_REF) {
                    continue;
                }

                // Summed with the diffuse term BEFORE the clamp, as the one
                // pass that drew both did: a light behind the surface takes
                // back part of its own highlight.
                lit += clamp(tint * light2D.lights[i].color * (attenuation * facing)
                             + light2D.lights[i].color * gloss * (shine * attenuation), 0.0, 1.0);
            }
        }
    }

    // Premultiplied, the light goes on at FULL weight over a partly transparent
    // texel, as the separate additive pass it reproduces does; only the base is
    // weighted by alpha. With nothing lit, lit is exactly zero and both lines
    // are the arithmetic this path had before the loop.
    if ((flags & FLAG_PREMULTIPLIED) != 0) {
        return vec4(base * alpha + lit, alpha);
    }
    return vec4(base + lit, alpha);
}

void main() {
    // Every map on this material samples through the same transform. See
    // transformedUV above for why it is computed once rather than three times.
    vec2 uv = transformedUV(fragTexCoord);

    vec4 albedoTex = texture(albedoMap, uv);

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
    float alphaCutoff = instances[fragInstance].material.w;
    if (alphaCutoff > 0.0 && albedoTex.a * instances[fragInstance].albedoColor.a < alphaCutoff) {
        discard;
    }

    vec3 albedo = albedoTex.rgb * fragColor * instances[fragInstance].albedoColor.rgb;

    // UNLIT: the authored colour, and nothing else touches it.
    //
    // A single early exit rather than a flag threaded through the terms below,
    // because "smallest" and "cheapest" are the same answer here. Returning
    // from this point skips the ORM fetch, the normal-map fetch and the TBN
    // rebuild, the froxel light loop with its shadow taps, both IBL fetches,
    // the emissive add and the fog. A branch placed lower down would compute
    // all of it and then throw it away, on the one kind of material that exists
    // precisely because it does not want any of it.
    //
    // Deliberately ABOVE the tone mapper's reach in one respect: albedoColor is
    // not clamped here, so a value above 1.0 comes out above 1.0 and blooms.
    // That is not an oversight - it is Godot's `modulate` past white, which is
    // how this game flashes a unit that has been hit.
    if ((instances[fragInstance].flags & FLAG_UNLIT) != 0) {
        // A 2D sprite gives the record's unused fields a meaning of their own,
        // so it leaves here rather than share the plain exit below.
        if ((instances[fragInstance].flags & FLAG_SPRITE2D) != 0) {
            outColor = shadeSprite2D(uv, albedoTex);
            return;
        }
        // Without either new switch, the arithmetic this exit always did.
        float alpha = albedoTex.a * instances[fragInstance].albedoColor.a;
        outColor = vec4(albedo, alpha);
        if ((instances[fragInstance].flags & FLAG_PREMULTIPLIED) != 0) outColor.rgb *= alpha;
        return;
    }

    // The map MULTIPLIES the material's constants rather than replacing them.
    //
    // Replacing would make the two ways of authoring a surface exclusive: a
    // material would either be uniformly rough or entirely at the mercy of a
    // texture, with no way to take a map and dial the whole thing smoother.
    // Multiplying makes the constant a master control over the map, and makes
    // the no-map case exactly the old arithmetic - the neutral texture is 1 in
    // every channel, so nothing shifts for a material that has never heard of
    // this binding.
    //
    // The floor on roughness stays where it was and stays LAST, after the
    // multiply: a GGX lobe at zero roughness is a division by zero, and a map
    // is now a second way to arrive there.
    vec3 orm = texture(ormMap, uv).rgb;

    float roughness = clamp(instances[fragInstance].material.x * orm.g, 0.02, 1.0);
    float metallic  = clamp(instances[fragInstance].material.y * orm.b, 0.0, 1.0);

    // Occlusion, gated by how much of the red channel is actually occlusion.
    //
    // glTF says of a metallic-roughness texture that "the red and alpha
    // channels are not specified and their values are ignored", and exporters
    // write zero there. Believed, that zeroes the ambient term for the whole
    // surface - a valid file rendering pitch black wherever no light directly
    // reaches it. The importer reports whether an occlusion texture vouched for
    // the red channel, and this is glTF's own formula for spending that: at
    // strength 0 it is exactly 1.0, so ignoring the channel is the SAME
    // arithmetic rather than a branch that can disagree with it.
    float occlusion = 1.0 + instances[fragInstance].emissive.w * (orm.r - 1.0);
    float ao        = clamp(instances[fragInstance].material.z * occlusion, 0.0, 1.0);

    // Re-orthonormalise the interpolated basis: interpolation across a
    // triangle does not preserve orthogonality, and a skewed basis tilts the
    // mapped normal.
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent - N * dot(N, fragTangent));
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);

    // Unpack from [0,1] to [-1,1] and rotate into world space.
    vec3 sampledNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    N = normalize(TBN * sampledNormal);

    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Which lights this fragment pays for.
    //
    // It used to be all of them, up to a hard eight, and that eight was the cap
    // the roadmap kept listing: nine lamps in one room meant one of them was
    // dropped for the whole frame. Now the directionals - which reach
    // everywhere, so clustering them would cost an index per froxel to say
    // nothing - are the leading entries and always run, and everything local
    // comes out of this fragment's own froxel.
    //
    // The same depth the cascade lookup uses, from the same function. A varying
    // would save the multiply and cost a slot in every vertex shader that feeds
    // this one, for a value only these two places want.
    float viewZ = viewDepthOf(fragWorldPos);

    uint cluster = clusterIndexFor(gl_FragCoord.xy, viewZ);
    uvec2 slice = clusterBuffer.clusters[cluster];

    int directionalCount = int(ubo.lightCount.y);
    int totalCount = int(ubo.lightCount.x);
    int localCount = int(slice.y);

    for (int slot = 0; slot < directionalCount + localCount; ++slot) {
        // The directionals are a prefix of the light buffer; everything after
        // them is looked up through this froxel's slice of the index list.
        int index = slot < directionalCount
                  ? slot
                  : int(lightIndexBuffer.indices[slice.x + uint(slot - directionalCount)]);

        // Cheap insurance against a stale or overflowing index list: an index
        // past the end of the light buffer would read whatever follows it.
        if (index < 0 || index >= totalCount) continue;

        Light light = lightBuffer.lights[index];

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

        // The light at INDEX ZERO is the one the cascades were rendered from -
        // the index into the light buffer, not the position in this loop, which
        // are no longer the same thing now that the loop walks a froxel's slice.
        // Point lights carry the index of their own cube, or -1 when they missed
        // out on one, so a lamp shining through a wall is no longer the only
        // possible outcome.
        float shadow = 1.0;
        if (index == 0 && light.positionOrDirection.w < 0.5) {
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
    // The environment, or the two-colour hemisphere it replaces.
    //
    // A branch rather than a blend, and the fallback path is the ORIGINAL
    // expression untouched: a scene that names no environment has to render
    // exactly as it did before any of this existed, to the last bit, or the
    // feature cannot be landed without re-checking every scene in the project.
    // Which slot, and whether that slot holds anything.
    //
    // environmentParams.z is a BITMASK, one bit per slot, rather than a count -
    // a count cannot say that slot 1 is loaded and slot 0 is not, which is
    // exactly the case a scene with a probe and no global environment produces.
    //
    // Selected at LITERAL indices. A sampler array indexed by a push constant
    // needs shaderSampledImageArrayDynamicIndexing, which this device does not
    // enable; pointShadowFactor already learned that the hard way and takes the
    // same shape. It costs one extra cube fetch per fragment and is correct on
    // every driver rather than on the ones that happen to be lenient.
    uint probeMask = uint(ubo.environmentParams.z);
    int probe = clamp(instances[fragInstance].probeIndex, 0, MAX_ENV_PROBES - 1);
    bool hasEnvironment = (probeMask & (1u << uint(probe))) != 0u;

    vec3 irradiance = hasEnvironment
        ? ((probe == 0) ? texture(irradianceMaps[0], N).rgb
                        : texture(irradianceMaps[1], N).rgb)
        : hemisphere(N);

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
    // Rougher surfaces read a blurrier level of the prefiltered chain, which is
    // the whole reason it is a chain: one integral per roughness, computed once
    // rather than per fragment.
    float maxLevel = max(ubo.environmentParams.y - 1.0, 0.0);
    vec3 reflected = hasEnvironment
        ? ((probe == 0) ? textureLod(prefilteredMaps[0], R, roughness * maxLevel).rgb
                        : textureLod(prefilteredMaps[1], R, roughness * maxLevel).rgb)
        : hemisphere(R);

    vec3 ambientSpecular = reflected * (F0 * envBRDF.x + envBRDF.y);

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
    color += instances[fragInstance].emissive.rgb;

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

    outColor = vec4(color, albedoTex.a * instances[fragInstance].albedoColor.a);

    // For a premultiplied blend (MaterialComponent::BlendMode::Premultiplied).
    // Unset, which is every material that never chose it, this is not reached.
    if ((instances[fragInstance].flags & FLAG_PREMULTIPLIED) != 0) outColor.rgb *= outColor.a;
}
