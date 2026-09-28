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

// --- A light's own shadows (opt-in) ---------------------------------------------
//
// Ethanon 0.7.12 baked each static light into every static sprite's lightmap
// with the shadows static entities cast from it (ETHRenderEntity::
// GenerateLightmap): the light's pass drawn alone into a scratch target, each
// shadow drawn black over THAT target, and the result added into the lightmap.
// So a baked shadow took away its own light and nothing else - not the room's
// ambient, not another lamp - where a shadow drawn over the finished frame
// darkens everything under it.
//
// Here a light carries the shadows it casts (Light2DShadowsComponent), and a
// sprite that asks (Sprite2DLight::lightShadows) multiplies each light's add by
// what of that light survives them at the fragment:
//   keep = the product, over the light's strips covering the fragment's world
//          xy, of (1 - opacity * mask(uv))
// A light without strips, a sprite without the switch and a frame without a
// strip are the arithmetic they always were.
//
// A strip is Ethanon's projected shadow (dynaShadowVS.cg, drawn with gs2d's
// RM_THREE_TRIANGLES): five corners in world xy whose texture coordinates are
// fixed, (0,0) (0,1) (0.5,0) (1,1) (1,0), making the triangles (0,1,2) (2,1,3)
// (2,3,4). The mask (Light2DShadowMask) is the shadow image's alpha, sampled
// bilinearly between texel centres with its edges clamped.

// How many strips one frame may carry, over every light.
inline constexpr uint32_t kMaxShadows2D = 256;

// The largest mask, in texels (32 x 32, the size of Ethanon's shadow.dds).
inline constexpr uint32_t kMaxShadowMask2DTexels = 1024;

// The strip's texture coordinates and its triangles, corner by corner. Must
// match SHADOW_STRIP_UV and SHADOW_STRIP_TRIANGLES in shader.frag.
inline constexpr float kShadowStripUv[5][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {0.5f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
inline constexpr uint32_t kShadowStripTriangles[3][3] = {{0, 1, 2}, {2, 1, 3}, {2, 3, 4}};

// One strip as the shader reads it (shader.frag's Shadow2D, scene binding 13).
// std430: a vec4, then five vec2 at a stride of 8, then two scalars - 64 bytes.
struct GpuShadow2D {
    glm::vec4 bounds{0.0f};     // min x, min y, max x, max y of the corners
    glm::vec2 corners[5]{};     // world xy
    float opacity{0.0f};
    float pad{0.0f};
};
static_assert(sizeof(GpuShadow2D) == 64, "GpuShadow2D must match its std430 stride");
static_assert(offsetof(GpuShadow2D, bounds) == 0, "GpuShadow2D layout shifted");
static_assert(offsetof(GpuShadow2D, corners) == 16, "GpuShadow2D layout shifted");
static_assert(offsetof(GpuShadow2D, opacity) == 56, "GpuShadow2D layout shifted");

// The front of scene binding 13: how many strips, and the mask's size (0 x 0
// when the frame has none, which the shader reads as a mask of 1).
struct GpuShadow2DHeader {
    uint32_t count{0};
    uint32_t maskWidth{0};
    uint32_t maskHeight{0};
    uint32_t pad{0};
};
static_assert(sizeof(GpuShadow2DHeader) == 16, "the 2D shadow buffer's header is 16 bytes");

// Scene binding 13, in bytes: the header; one (first, count) per gathered light,
// in GatherLights2D's order, kMaxLights2D of them; the mask, row by row from
// v = 0, kMaxShadowMask2DTexels floats; then the strips. Must match
// shader.frag's Shadow2DBuffer, which test_materials reads.
inline constexpr uint32_t kShadow2DRangesOffset = static_cast<uint32_t>(sizeof(GpuShadow2DHeader));
inline constexpr uint32_t kShadow2DMaskOffset = kShadow2DRangesOffset + 8u * kMaxLights2D;
inline constexpr uint32_t kShadow2DStripsOffset = kShadow2DMaskOffset + 4u * kMaxShadowMask2DTexels;
inline constexpr uint32_t kShadow2DBufferBytes =
    kShadow2DStripsOffset + static_cast<uint32_t>(sizeof(GpuShadow2D)) * kMaxShadows2D;
static_assert(kShadow2DStripsOffset % 16 == 0, "the strips start on their vec4's alignment");

// On a Light2DComponent's entity: the shadows that light casts, which darken
// its own add on the sprites that ask for them (Sprite2DLight::lightShadows) and
// nothing else. Runtime only, like Light2DEye: nothing saves it, and a game sets
// it each frame from whatever casts.
struct Light2DShadowsComponent {
    struct Strip {
        glm::vec2 corners[5]{};   // world xy, in the strip's order (kShadowStripUv)
        float opacity{1.0f};      // how much of the light the mask's full alpha removes
    };
    std::vector<Strip> strips;
};

// The shadow image's alpha, which every strip samples (kShadowStripUv). In the
// registry's context, runtime only. Without one, or with a size that does not
// match its texels or exceeds kMaxShadowMask2DTexels, a strip removes its full
// opacity wherever it covers.
struct Light2DShadowMask {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<float> alpha;   // width x height, rows from v = 0, 0..1
};

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
// Returns how many were written. `out` is cleared first. With `outEntities`,
// the entity each packed light came from, index for index (cleared first too):
// GatherShadows2D's input.
uint32_t GatherLights2D(const entt::registry& registry,
                        std::vector<GpuLight2D>& out,
                        uint32_t capacity,
                        uint32_t* outDropped = nullptr,
                        std::vector<entt::entity>* outEntities = nullptr);

// The shadows of the lights GatherLights2D packed (`lights`, its outEntities),
// light by light in that order: `outRanges[i]` is light i's (first, count) into
// `outStrips`, (0, 0) for a light without a Light2DShadowsComponent. Each
// strip's bounds are its corners' box. Past `capacity` a strip is dropped and
// counted into `outDropped`; a light keeps those of its strips that fit.
// Returns how many strips were written. Both outputs are cleared first.
uint32_t GatherShadows2D(const entt::registry& registry,
                         const std::vector<entt::entity>& lights,
                         std::vector<glm::uvec2>& outRanges,
                         std::vector<GpuShadow2D>& outStrips,
                         uint32_t capacity,
                         uint32_t* outDropped = nullptr);

// Scene binding 13 as the renderer uploads it (kShadow2DBufferBytes at most):
// the header alone when `strips` is empty - the shader reads nothing past the
// count then - else the header, kMaxLights2D ranges (zeros past `ranges`), the
// context's Light2DShadowMask padded with zeros to kMaxShadowMask2DTexels, and
// the strips. `out` is resized to the bytes to upload.
void PackShadows2D(const entt::registry& registry,
                   const std::vector<glm::uvec2>& ranges,
                   const std::vector<GpuShadow2D>& strips,
                   std::vector<uint8_t>& out);

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

// A light's own shadows (Sprite2DLight::lightShadows), as shadeSprite2D reads
// scene binding 13: shadowMask2D, shadowStripUv2D and lightShadowKeep2D.

// The mask at `uv`: bilinear between texel centres, edges clamped. 1 when the
// size is zero (no mask) or past kMaxShadowMask2DTexels.
float ShadowMaskAt(const float* mask, uint32_t width, uint32_t height, const glm::vec2& uv);

// Whether `at` (world xy) lies in the strip, and where in its texture: the
// first of the three triangles that holds it, so a point on an edge two share is
// counted once. A degenerate triangle holds nothing.
bool ShadowStripUv(const GpuShadow2D& strip, const glm::vec2& at, glm::vec2& outUv);

// What of a light survives its strips `range` (first, count into `strips`) at
// `at`: the product of (1 - opacity * mask(uv)) over those that hold it.
float ShadowKeep(const std::vector<GpuShadow2D>& strips, const glm::uvec2& range,
                 const float* mask, uint32_t maskWidth, uint32_t maskHeight, const glm::vec2& at);

} // namespace Light2D

} // namespace Supersonic
