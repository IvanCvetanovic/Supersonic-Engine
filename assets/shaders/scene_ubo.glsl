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

// Must match ShadowCascades::kCascadeCount, SpotLight::kMaxShadowCasters,
// PointShadow::kMaxShadowCasters and ClusterGrid's grid dimensions.
//
// There is no MAX_LIGHTS any more, and that is the change: the lights live in
// a storage buffer below, so their number is a runtime count rather than an
// array size compiled into every shader that reads this block.
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
    vec4 clusterParams;       // xy = render target size in pixels, z = near, w = far
    vec4 environmentParams;   // x = 1 when a cubemap is bound, y = prefiltered mip count
} ubo;

// The froxel grid. Must match core/ClusterGrid.hpp.
#define CLUSTER_TILES_X 16
#define CLUSTER_TILES_Y 9
#define CLUSTER_SLICES  24

// Every light in the frame, DIRECTIONALS FIRST. ubo.lightCount.x is how many
// there are and .y how many of the leading ones are directional.
//
// readonly and std430: std140 would round the struct's stride up and put every
// light somewhere the C++ side did not write it, which is not a validation
// error - it is just the wrong numbers, member by member, exactly the drift
// this file was extracted to stop.
layout(std430, set = 0, binding = 5) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

// One (offset, count) pair per froxel, in x + y*TILES_X + z*TILES_X*TILES_Y
// order - the same order the fragment computes its own index in.
layout(std430, set = 0, binding = 6) readonly buffer ClusterBuffer {
    uvec2 clusters[];
} clusterBuffer;

// The flat list those pairs point into. Absolute indices into lights[].
layout(std430, set = 0, binding = 7) readonly buffer LightIndexBuffer {
    uint indices[];
} lightIndexBuffer;

// The environment, as the two things lighting from one actually needs: what a
// diffuse surface facing a direction receives, and what a mirror facing it
// reflects at each roughness.
//
// Both are always bound. Reading a descriptor nobody wrote is undefined even
// inside a branch that is never taken, so a scene with no environment binds a
// one-colour cube and ubo.environmentParams.x says to ignore it.
// Two, matching VulkanPipeline::kMaxEnvironmentProbes. Declared as arrays of
// separate sampler descriptors rather than as a cube ARRAY image, so no optional
// device feature is needed - the same choice binding 3 makes for the point
// shadow cubes.
#define MAX_ENV_PROBES 2

layout(set = 0, binding = 8) uniform samplerCube irradianceMaps[MAX_ENV_PROBES];
layout(set = 0, binding = 9) uniform samplerCube prefilteredMaps[MAX_ENV_PROBES];

// How far in FRONT of the camera a world position is.
//
// One definition, because there were briefly two: the cascade lookup wrote
// -(ubo.view * vec4(p,1)).z and the light loop wrote out the same dot product by
// hand against the view matrix's third row. Identical arithmetic, two
// expressions, one shader - which is the drift this header was extracted to
// stop, and it had reappeared inside a single file.
float viewDepthOf(vec3 worldPos) {
    return -(ubo.view * vec4(worldPos, 1.0)).z;
}

// Which froxel a fragment is in.
//
// A TRANSLITERATION of ClusterGrid::ClusterForFragment, line for line, and it
// is written that way on purpose: this is one of two descriptions of the same
// mapping, and the other one is the only one a test can reach. Changing either
// without the other is how the row index came to be mirrored about the horizon
// in the first place.
//
// gl_FragCoord is in the pixels of the image being rasterised into, which is
// the editor's offscreen target and not the window - so the size has to come
// from the UBO rather than from anything the shader could guess.
//
// viewZ is positive distance in FRONT of the camera. The slice distribution is
// exponential, matching ClusterGrid::SliceForDepth exactly: a uniform division
// would put almost every froxel out where the frustum is enormous and nothing
// is standing.
uint clusterIndexFor(vec2 fragCoord, float viewZ) {
    vec2 targetSize = max(ubo.clusterParams.xy, vec2(1.0));
    vec2 tileSize = targetSize / vec2(float(CLUSTER_TILES_X), float(CLUSTER_TILES_Y));

    // Y IS FLIPPED, and this is not a detail. The projection multiplies its
    // second row by -1 for Vulkan, so view-space +Y - up - lands at
    // gl_FragCoord.y = 0, the TOP of the image. The grid on the CPU numbers its
    // rows in view space, bottom first, because that is the space the froxel
    // bounds are computed in.
    //
    // Left unflipped, every light is looked up in the row mirrored about the
    // horizon: a lamp lighting the floor lights the ceiling instead, and the
    // scene still looks lit, which is what makes it hard to see.
    vec2 gridCoord = vec2(fragCoord.x, targetSize.y - fragCoord.y);

    uvec2 tile = uvec2(clamp(gridCoord / tileSize,
                             vec2(0.0),
                             vec2(float(CLUSTER_TILES_X - 1), float(CLUSTER_TILES_Y - 1))));

    float nearPlane = max(ubo.clusterParams.z, 1e-4);
    float farPlane = max(ubo.clusterParams.w, nearPlane * 1.0001);

    float ratio = log(max(viewZ, nearPlane) / nearPlane) / log(farPlane / nearPlane);
    uint slice = uint(clamp(ratio * float(CLUSTER_SLICES),
                            0.0, float(CLUSTER_SLICES - 1)));

    return tile.x + tile.y * uint(CLUSTER_TILES_X) +
           slice * uint(CLUSTER_TILES_X) * uint(CLUSTER_TILES_Y);
}

#endif // SUPERSONIC_SCENE_UBO_GLSL
