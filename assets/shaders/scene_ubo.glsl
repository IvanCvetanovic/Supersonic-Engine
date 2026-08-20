// The per-frame scene block, declared once.
//
// This used to be written out by hand in shader.vert, shader.frag and
// grid.vert, each with its own copy of the Light struct and its own idea of
// which members existed. Two of the three had already drifted: both declared
// Light as three vec4 against GpuLight's four, and both omitted ambientGround
// and spotViewProj. Under std140 that puts lights[] at offset 464 with a
// 48-byte stride where C++ writes 608 with 64 - so every light a vertex shader
// read would have been somebody else's data, member by member.
//
// It was inert only by luck: shader.vert and grid.vert read ubo.view and
// ubo.proj, which sit at offsets 0 and 64 and are therefore correct in every
// version of the block. The first vertex shader to want a light would have
// found garbage, and nothing would have reported it - a uniform block mismatch
// is not a validation error, it is just wrong numbers.
//
// Must match Engine::UniformBufferObject in renderer/VulkanPipeline.hpp. That
// is now one place to check instead of four.

#ifndef SUPERSONIC_SCENE_UBO_GLSL
#define SUPERSONIC_SCENE_UBO_GLSL

// Must match VulkanPipeline.hpp's kMaxLights, ShadowCascades::kCascadeCount,
// SpotLight::kMaxShadowCasters and PointShadow::kMaxShadowCasters.
#define MAX_LIGHTS           8
#define SHADOW_CASCADES      4
#define SPOT_SHADOW_CASTERS  2
#define POINT_SHADOW_CASTERS 2

struct Light {
    vec4 positionOrDirection;   // xyz, w = type (0 = directional, 1 = point, 2 = spot)
    vec4 colorAndIntensity;     // rgb, a = intensity
    vec4 attenuation;           // x = range, y = cube slot, z = cos inner, w = spot slot
    vec4 spotDirection;         // xyz = aim, w = cos outer
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[SHADOW_CASCADES];
    vec4 cascadeSplits;      // view-space far depth per cascade
    vec4 cascadeTexelWorld;  // world size of one shadow texel per cascade
    vec4 cameraPosition;
    vec4 ambientColor;
    vec4 ambientGround;
    vec4 lightCount;
    mat4 spotViewProj[SPOT_SHADOW_CASTERS];
    vec4 fogColorAndDensity;  // rgb = colour, a = density (0 = no fog)
    Light lights[MAX_LIGHTS];
} ubo;

#endif // SUPERSONIC_SCENE_UBO_GLSL
