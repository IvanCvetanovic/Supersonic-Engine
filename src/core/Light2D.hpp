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
    uint32_t layers{0};        // the layer byte; kLight2DBakedBit above it
};

// Light2DComponent::baked, carried in GpuLight2D::layers above the layer byte,
// which is all a sprite's mask is ever tested against. Must match shader.frag's
// LIGHT_BAKED_BIT, which test_materials reads.
inline constexpr uint32_t kLight2DBakedBit = 1u << 8;
static_assert(sizeof(GpuLight2D) == 32, "GpuLight2D must match its std430 stride");
static_assert(offsetof(GpuLight2D, position) == 0, "GpuLight2D layout shifted");
static_assert(offsetof(GpuLight2D, range) == 12, "GpuLight2D layout shifted");
static_assert(offsetof(GpuLight2D, color) == 16, "GpuLight2D layout shifted");
static_assert(offsetof(GpuLight2D, layers) == 28, "GpuLight2D layout shifted");

// What precedes the array in the buffer. A runtime-sized array in std430 starts
// at its element's alignment, and a struct holding a vec3 aligns to 16, so the
// count takes sixteen bytes and not four - and two of the twelve it left spare
// carry the frame's specular eye (Light2DEye), which is per frame like the
// count, and read only by a sprite with a highlight.
//
// The last four bytes were padding; they carry the frame's Light2DAlphaTest,
// read only by a sprite that asks for the light pass's alpha test.
struct GpuLight2DHeader {
    uint32_t count{0};
    float eyeMirrorY{0.0f};
    float eyeHeight{0.0f};
    float passAlphaIntensity{0.0f};
};
static_assert(sizeof(GpuLight2DHeader) == 16, "the 2D light buffer's header is 16 bytes");
static_assert(offsetof(GpuLight2DHeader, count) == 0, "GpuLight2DHeader layout shifted");
static_assert(offsetof(GpuLight2DHeader, eyeMirrorY) == 4, "GpuLight2DHeader layout shifted");
static_assert(offsetof(GpuLight2DHeader, eyeHeight) == 8, "GpuLight2DHeader layout shifted");
static_assert(offsetof(GpuLight2DHeader, passAlphaIntensity) == 12, "GpuLight2DHeader layout shifted");

// Where the specular highlight of every 2D sprite is seen from, this frame
// (MaterialComponent::Sprite2DLight::specularStrength).
//
// Not one point but one PER LIGHT: a light at L is seen from
//   Eye = (L.x, 2 * mirrorY - L.y, height)
// the light mirrored across the world line y = mirrorY, at a fixed height.
// That is Ethanon's fake eye (ETHFakeEyePositionManager, set by 0.7.12 in its
// light pass), which in its y-down pixels was (L.x, 2 camY + 1.5 screenH - L.y,
// 768): the line three quarters of the way down the screen, at 768. A port
// with a y-up world, one unit per pixel, has
//   mirrorY = -(camY + 0.75 * screenH),  height = 768
// and has to set it whenever its camera moves, because the eye moves with it.
//
// In the registry's context, runtime only, like ViewportInfo: nothing saves it,
// and a scene without one sees highlights from (L.x, -L.y, 0). It costs nothing
// to a sprite without a highlight, which never reads it.
struct Light2DEye {
    float mirrorY{0.0f};
    float height{0.0f};
};

// The light intensity a light pass's ALPHA is multiplied by, for the sprites
// that ask for Sprite2DLight::lightAlphaTest.
//
// Ethanon kept its scene-wide lightIntensity apart from each light's colour
// (the colour's alpha held at 1), so the alpha its light pass was tested on
// carried the intensity and not the colour. A Light2DComponent folds the two,
// and a game that tests the pass hands the intensity over here. Penumbra's is
// its scenes' lightIntensity, 2.
//
// In the registry's context, runtime only, like Light2DEye. Without one the
// header's word is the zero it was as padding, and a sprite asking for the test
// takes the intensity as 1.
struct Light2DAlphaTest {
    float intensity{1.0f};
};

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

// The header the renderer writes in front of those lights: their count, the
// registry context's Light2DEye and Light2DAlphaTest, or zeros for either it
// has none of.
GpuLight2DHeader MakeHeader(const entt::registry& registry, uint32_t count);

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

// What the shader reads once per fragment for the light pass's alpha test
// (Sprite2DLight::lightAlphaTest). A light is skipped when its pass's alpha is
// below kLightPassAlphaRef.
struct PassAlpha {
    float albedoAlpha{1.0f};   // the albedo texel's alpha
    float colorAlpha{1.0f};    // albedoColor.a
    float intensity{1.0f};     // Light2DAlphaTest::intensity (1 when the header's word is 0)
    bool vertical{false};      // a standing sprite's pass is not weighted by the texel's alpha twice
    // With a highlight: the gloss texel's alpha x specularStrength.
    float glossAlpha{0.0f};
};

// Direct3D 9's ALPHAREF 1 with GREATER, on an 8-bit alpha: a pass whose alpha
// rounds to 1/255 or less draws nothing. Must match shader.frag's
// LIGHT_PASS_ALPHA_REF.
inline constexpr float kLightPassAlphaRef = 1.5f / 255.0f;

// The pass's alpha, as Sprite2DLight::lightAlphaTest spells it: without a
// highlight albedoAlpha x colorAlpha x facing x attenuation x intensity, times
// albedoAlpha again when flat; with one (`highlighted`)
// albedoAlpha x attenuation x (colorAlpha x facing x intensity + shine x glossAlpha x intensity).
float PassAlphaOf(const PassAlpha& pass, float facing, float attenuation, bool highlighted = false,
                  float shine = 0.0f);

// What one light adds to one fragment, before the blend:
//   clamp(tint * light.color * (1 - d2 / r2) * dot(L - P, N) / d, 0, 1)
// zero when the light's layers share no bit with `mask`, and exactly zero at or
// beyond the range. `tint` is the texel times the draw's colour WITHOUT the
// ambient (the record's emissive.rgb), and `surface` is (fragment x, fragment y,
// the surface's height).
glm::vec3 Contribution(const GpuLight2D& light, uint8_t mask,
                       const glm::vec3& surface, const glm::vec3& normal,
                       const glm::vec3& tint, const PassAlpha* passAlpha = nullptr);

// A standing sprite's point and normal (Sprite2DLight::vertical), as the shader
// turns the flat ones before the loop: a quarter turn about the world x axis
// through the line y = baseY at the surface's height,
//   point  = (flat.x, baseY, flat.z + (flat.y - baseY))
//   normal = (n.x, -n.z, n.y)
// `flatPoint` is (fragment x, fragment y, the surface's height) and `flatNormal`
// what WorldNormal gives.
struct StoodUp {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
};
StoodUp StandUp(const glm::vec3& flatPoint, const glm::vec3& flatNormal, float baseY);

// What the shader reads once per fragment for the highlight
// (Sprite2DLight::specularStrength above zero).
struct Highlight {
    // gloss.rgb x specularStrength x the albedo texel's alpha.
    glm::vec3 gloss{0.0f};
    float power{50.0f};
    // The frame's Light2DEye.
    float eyeMirrorY{0.0f};
    float eyeHeight{0.0f};
    // Sprite2DLight::bakedEye and bakedEyeY, and the sprite's own height (the
    // record's emissive.w, NOT a standing fragment's): a light carrying
    // kLight2DBakedBit is then seen from BakedEyeFor's point.
    bool bakedEye{false};
    float bakedEyeY{0.0f};
    float spriteHeight{0.0f};
};

// Where a light at `lightPosition` is seen from (Light2DEye's formula).
glm::vec3 EyeFor(const glm::vec3& lightPosition, float eyeMirrorY, float eyeHeight);

// Where a baked light at `lightPosition` is seen from by a sprite with a baked
// eye (Sprite2DLight::bakedEye): (L.x, bakedEyeY, spriteHeight + eyeHeight).
glm::vec3 BakedEyeFor(const glm::vec3& lightPosition, float bakedEyeY, float spriteHeight, float eyeHeight);

// What one light adds WITH the highlight: the diffuse term above plus
//   color * gloss * pow(saturate(dot(N, H)), power) * attenuation,
//   H = normalize(normalize(L - P) + normalize(Eye - P)),
// summed BEFORE the one clamp. Zero when the layers share no bit with `mask`,
// and at or beyond the range.
//
// With `passAlpha` (Sprite2DLight::lightAlphaTest) it is also zero when the
// pass's alpha (PassAlphaOf) is below kLightPassAlphaRef; Contribution takes
// the same argument for a sprite without a highlight.
glm::vec3 SpecularContribution(const GpuLight2D& light, uint8_t mask,
                               const glm::vec3& surface, const glm::vec3& normal,
                               const glm::vec3& tint, const Highlight& highlight,
                               const PassAlpha* passAlpha = nullptr);

} // namespace Light2D

} // namespace Supersonic
