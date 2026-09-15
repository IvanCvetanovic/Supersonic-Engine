#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Supersonic {

// How many 2D point lights one frame may carry.
//
// Sixty-four, and the number is about memory, not shading: the shader loops the
// flat list for every sprite fragment whose mask is non-zero, so what bounds the
// cost is how many lights a scene actually has, not this. The game this was built
// for places at most two per level and adds a handful of moving ones (a shot, an
// explosion). Must match MAX_LIGHTS_2D in shader.frag, which test_materials reads.
inline constexpr uint32_t kMaxLights2D = 64;

// One Light2DComponent as the shader reads it (shader.frag's Light2D, scene
// binding 12). std430: a vec3 followed by a scalar packs into sixteen bytes, so
// the stride is 32 with no padding, and the offsets below are pinned because a
// shift would light every sprite from somebody else's numbers without a message.
struct GpuLight2D {
    glm::vec3 position{0.0f};  // world x, world y, and z = the light's HEIGHT
    float range{0.0f};
    glm::vec3 color{0.0f};     // colour x intensity, folded on the CPU, may exceed 1
    uint32_t layers{0};
};
static_assert(sizeof(GpuLight2D) == 32, "GpuLight2D must match its std430 stride");
static_assert(offsetof(GpuLight2D, position) == 0, "GpuLight2D layout shifted");
static_assert(offsetof(GpuLight2D, range) == 12, "GpuLight2D layout shifted");
static_assert(offsetof(GpuLight2D, color) == 16, "GpuLight2D layout shifted");
static_assert(offsetof(GpuLight2D, layers) == 28, "GpuLight2D layout shifted");

// What precedes the array in the buffer. A runtime-sized array in std430 starts
// at its element's alignment, and a struct holding a vec3 aligns to 16, so the
// count takes sixteen bytes and not four.
struct GpuLight2DHeader {
    uint32_t count{0};
    uint32_t pad0{0};
    uint32_t pad1{0};
    uint32_t pad2{0};
};
static_assert(sizeof(GpuLight2DHeader) == 16, "the 2D light buffer's header is 16 bytes");

// The buffer's size at capacity: what the renderer allocates per frame in flight
// and what its descriptor names.
inline constexpr uint32_t kLight2DBufferBytes =
    static_cast<uint32_t>(sizeof(GpuLight2DHeader) + sizeof(GpuLight2D) * kMaxLights2D);

// The 2D lights, gathered and shaded without Vulkan, like ClusterGrid beside it:
// the gather is a walk over components and the shading is arithmetic, and the
// suites touch no Vulkan entry point.
namespace Light2D {

// Every enabled Light2DComponent in the registry, packed for the shader.
//
// - x and y come from the entity's world position (LightWorldPosition: the world
//   matrix once the hierarchy is resolved, the local transform before), so a
//   lamp parented to a character goes with the character. z is the component's
//   height, not the transform's.
// - colour x intensity is folded here, so the shader multiplies once.
// - A light whose folded colour is zero is not packed at all. It could add
//   nothing, and Ethanon leaves such a light out of its list the same way
//   (ETHEntityRenderingManager.cpp:161-172, read for the Magic Portals port).
// - Past `capacity` a light is dropped, in the registry's own order, and
//   counted into `outDropped` when given, so the caller can say so. A dropped
//   light is a sprite left darker than it should be, with nothing else to tell.
//
// Returns how many were written. `out` is cleared first.
uint32_t GatherLights2D(const entt::registry& registry,
                        std::vector<GpuLight2D>& out,
                        uint32_t capacity,
                        uint32_t* outDropped = nullptr);

// A TRANSLITERATION of shadeSprite2D's light loop in assets/shaders/shader.frag,
// kept beside it the way ClusterGrid::ClusterForFragment is kept beside
// clusterIndexFor. The shader is the one description that draws and the one no
// suite can reach, so the suites test this, and a change to either has to be
// made to both.

// The surface's normal in world axes, as the shader builds it once per fragment
// before the loop: the map's texel decoded to -1..1 and NOT renormalised (the
// length is part of the look), green flipped when the map points down the image,
// then carried along the sprite's own right and up (the model's first two
// columns, normalised) so a rotated or mirrored sprite turns its normals with it.
// z stays z: towards the viewer, the direction the heights are measured in.
glm::vec3 WorldNormal(const glm::vec3& encodedTexel, bool normalYDown, const glm::mat4& model);

// What one light adds to one fragment, before the blend:
//   clamp(tint * light.color * (1 - d2 / r2) * dot(L - P, N) / d, 0, 1)
// zero when the light's layers share no bit with `mask`, and exactly zero at or
// beyond the range. `tint` is the texel times the draw's colour WITHOUT the
// ambient (the record's emissive.rgb), and `surface` is (fragment x, fragment y,
// the surface's height).
glm::vec3 Contribution(const GpuLight2D& light, uint8_t mask,
                       const glm::vec3& surface, const glm::vec3& normal,
                       const glm::vec3& tint);

} // namespace Light2D

} // namespace Supersonic
