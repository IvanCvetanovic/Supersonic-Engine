#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

// GLM configuration lives on the CMake target - see core/Components.hpp.
#include <glm/glm.hpp>

#include "renderer/ShadowCascades.hpp"
#include "renderer/PointShadow.hpp"
#include "renderer/SpotLight.hpp"
#include "core/Components.hpp"   // LightType

namespace Supersonic {

// How many lights one frame may carry.
//
// This was EIGHT, and it was a cap on how many lights could touch one FRAGMENT,
// because every fragment looped over every light in the scene and that loop had
// to be bounded. It is now a cap on how many can exist in the frame at all: the
// lights live in a storage buffer and the fragment only walks the ones the
// froxel grid says can reach it, so the number here buys memory rather than
// shading cost.
//
// Two hundred and fifty-six is a room with a great many lamps in it. A scene
// past this still falls back on LightSelection to decide which ones survive,
// which is now a genuinely exceptional path rather than the everyday one.
inline constexpr int kMaxLights = 256;

// std140 layout: every member is vec4-aligned so the C++ and GLSL views match.
struct GpuLight {
    alignas(16) glm::vec4 positionOrDirection{0.0f, 1.0f, 0.0f, 0.0f}; // w = type
    alignas(16) glm::vec4 colorAndIntensity{1.0f};                     // rgb, a = intensity
    // y is the index of this light's cube shadow map, or -1 when it does not
    // cast. Carried in the light rather than a parallel array so the shader
    // cannot pair a light with someone else's shadow.
    //
    // z is the cosine of a spot's inner angle and w is its shadow slot, kept
    // here rather than in a fourth vec4 because a light is already the widest
    // thing in this block and every one of them pays for the padding.
    alignas(16) glm::vec4 attenuation{25.0f, -1.0f, 1.0f, -1.0f};      // x = range, y = cube slot, z = cos inner, w = spot slot

    // Spot lights: xyz is the direction the cone points, w the cosine of the
    // outer angle. A spot needs both a position and a direction, and
    // positionOrDirection can only carry one of them.
    alignas(16) glm::vec4 spotDirection{0.0f, -1.0f, 0.0f, 0.0f};
};

// Per-frame scene constants. Light and camera data travel to the GPU instead of
// being hardcoded literals in shader.frag, and there is now an array of lights
// rather than a single hardcoded direction.
struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;

    // One light-space transform per shadow cascade, replacing the single
    // lightSpace matrix. view and proj deliberately stay first: grid.vert reads
    // only those two, at offsets 0 and 64, so its own copy of this block does
    // not have to care about anything after them.
    alignas(16) glm::mat4 cascadeViewProj[kShadowCascadeCount];

    // x..w = view-space distance to each cascade's far plane. A vec4 rather than
    // float[4] because a std140 float array has a 16-byte stride per element,
    // which would silently desync from the C++ side.
    alignas(16) glm::vec4 cascadeSplits;

    // x..w = world size of one shadow texel per cascade, for the normal offset.
    alignas(16) glm::vec4 cascadeTexelWorld;

    alignas(16) glm::vec4 cameraPosition;  // xyz = world position
    alignas(16) glm::vec4 ambientColor;    // rgb = sky ambient, above the horizon
    alignas(16) glm::vec4 ambientGround;   // rgb = ground bounce, below it
    alignas(16) glm::vec4 lightCount;      // x = active light count

    // One transform per shadow-casting spot light. A spot is a single frustum,
    // so unlike a point light it is sampled exactly like a cascade: project,
    // compare.
    alignas(16) glm::mat4 spotViewProj[SpotLight::kMaxShadowCasters];

    // rgb = fog colour, a = density. Appended AFTER the matrices and before the
    // light array purely because appending anywhere earlier would shift every
    // offset after it - and the one thing this block has already been wrong
    // about is offsets.
    //
    // A density of zero is no fog, which is why there is no separate enable
    // flag: the shader's exp2(-(d*density)^2) is exactly 1.0 at density 0, so
    // the disabled path is the same arithmetic rather than a branch that can
    // disagree with it.
    alignas(16) glm::vec4 fogColorAndDensity{0.0f};

    // What the fragment stage needs to find its own froxel: the size of the
    // target it is rasterising into, and the camera's depth range.
    //
    // The render target rather than the window, because the scene draws into
    // the editor's offscreen image and gl_FragCoord is in THAT image's pixels.
    // Taking the window's size instead would shift every tile boundary by the
    // ratio between them, which reads as the lighting sliding around the screen
    // as the viewport is dragged.
    alignas(16) glm::vec4 clusterParams{1.0f, 1.0f, 0.1f, 100.0f}; // xy = target size, z = near, w = far

    // x is 1 when an environment map is bound and 0 when the shader should use
    // the analytic hemisphere instead; y is how many roughness levels the
    // prefiltered chain has, which the shader needs to turn a roughness into a
    // mip. A vec4 rather than two floats because a uniform block pads to
    // sixteen bytes either way.
    alignas(16) glm::vec4 environmentParams{0.0f, 1.0f, 0.0f, 0.0f};

    // The light array USED to live here, a fixed eight of them. It is a storage
    // buffer now (set 0, binding 5) so a scene may hold as many as it likes,
    // and lightCount.y says how many of the leading entries are directional -
    // those are never clustered, because a light that reaches everywhere is in
    // every froxel and putting it in the grid would only cost memory to say so.
};

// 128 bytes, exactly the guaranteed minimum, with nothing to spare.
struct PushConstantData {
    glm::mat4 model;         // 0..63   (vertex)
    glm::vec4 albedoColor;   // 64..79  (fragment)
    glm::vec4 material;      // 80..95  x=roughness y=metallic z=ao w=alphaCutoff (fragment)

    // Light this surface emits regardless of what falls on it. rgb is added
    // after shading; W CARRIES THE OCCLUSION STRENGTH, which has nothing to do
    // with emission and everything to do with there being a spare float here.
    // The alternative was a fifth vec4 and eight more bytes of a 128-byte
    // budget, for one number that is 1.0 on almost every material.
    //
    // Placed BEFORE the two ints rather than appended, because a vec4 needs
    // 16-byte alignment: after them it would start at 112 and take the struct
    // to exactly 128, which is the guaranteed minimum with nothing left. Here
    // it lands at 96 and the whole block is 120.
    //
    // Deliberately allowed above 1.0. The scene target is floating point and
    // the bright pass thresholds at 1.0, so an emissive material is the natural
    // way to author something that glows - which is why it belongs here rather
    // than being folded into albedo.
    glm::vec4 emissive{0.0f};     // 96..111 (fragment)

    // Where this draw's joint matrices start in the frame's palette buffer.
    // The -1 default is load-bearing: the particle path builds these with
    // `PushConstantData push{}` and never touches these fields, and a zero would
    // make every particle skin itself against whatever is in palette slot 0.
    int32_t skinPaletteBase{-1};  // 112..115 (vertex)
    int32_t skinJointCount{0};    // 116..119 (vertex)

    // Which bound environment lights this draw.
    //
    // Defaults to 0, NOT to -1 like skinPaletteBase above, and the difference
    // matters. Slot 0 is the scene-wide environment, so a draw that never sets
    // this - the particle path builds `PushConstantData push{}` and touches
    // nothing else - gets the environment it would have got before probes
    // existed. A -1 default would silently drop every such draw back to the
    // analytic hemisphere, which looks like a descriptor bug and is not one.
    int32_t probeIndex{0};        // 120..123 (fragment)

    // Per-draw switches, one bit each: kUnlit, kSprite2D, kNormalYDown and
    // kPremultiplied, then the UV slot and the 2D light mask above them.
    //
    // This takes the block to exactly 128 bytes, which is the guaranteed
    // minimum every Vulkan implementation must offer - so it is the last thing
    // that fits. Anything after it needs a uniform buffer or a bigger limit
    // than the spec promises, and should not be quietly added here.
    //
    // A bitfield rather than a bool because the next per-draw switch should not
    // cost another four bytes there are not any of.
    int32_t flags{0};             // 124..127 (fragment)

    // Skip lighting entirely and emit the authored colour.
    //
    // A ColorRect has no normal, no roughness and no relationship to any light
    // in the scene, and a 2D game is made of them. Without this the only way to
    // get a flat colour out of a PBR shader is to fight it - emissive at
    // exactly 1.0, ambient tuned to nothing - and the result still moves when
    // somebody adds a lamp.
    static constexpr int32_t kUnlit = 1 << 0;

    // A 2D sprite (MaterialComponent::sprite2D), drawn by shader.frag's
    // shadeSprite2D. Only with kUnlit. It gives the fields the unlit path never
    // reads a second meaning, so the record does not grow past 128 bytes:
    //
    //   field            PBR / plain unlit        with kSprite2D
    //   model            model matrix             model matrix
    //   albedoColor.rgb  tint                     tint x ambient
    //   albedoColor.a    alpha factor             alpha factor (unchanged)
    //   material.x       roughness                overlay strength
    //   material.y, .z   metallic, ao             0, unused
    //   material.w       alpha cutoff             alpha cutoff (unchanged)
    //   emissive.rgb     emission                 tint WITHOUT ambient, for a light term
    //   emissive.w       occlusion strength       lighting height, world units
    //   flags 8..19      UV slot                  UV slot (unchanged)
    //   flags 20..27     unused                   2D light mask (PackLightMask)
    //
    // The light term that reads emissive.rgb, emissive.w, the mask and
    // kNormalYDown is not built yet; the record carries them already so that
    // adding it changes the shader and not this layout.
    static constexpr int32_t kSprite2D = 1 << 1;

    // With kSprite2D: the normal map's green channel points down the image.
    static constexpr int32_t kNormalYDown = 1 << 2;

    // Output premultiplied colour, rgb x alpha, for a pipeline blending One,
    // OneMinusSrcAlpha (MaterialComponent::BlendMode::Premultiplied). Honoured
    // by every exit of shader.frag.
    static constexpr int32_t kPremultiplied = 1 << 3;

    // The low byte is switches; the twelve bits above it are a UV transform
    // slot. The packing itself, and the reasons for its shape, are in
    // Components.hpp beside UvTransform - it is a protocol shared with the
    // gather and the shader, and this is the only one of the three that can
    // include a Vulkan header.
    int32_t UvSlot() const { return UnpackUvSlot(flags); }
    void SetUvSlot(int32_t slot) { flags = PackUvSlot(flags, slot); }
};

// One quad of the screen overlay (core/ScreenOverlay.hpp, screen_overlay.vert):
// where it is as fractions of the image, which part of its texture it shows,
// and the display-referred colour multiplied into it. 48 bytes.
struct ScreenOverlayPushConstants {
    glm::vec4 rect{0.0f, 0.0f, 1.0f, 1.0f}; // xy top-left, zw bottom-right, +y down
    glm::vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};   // xy uvMin, zw uvMax
    glm::vec4 color{1.0f};
};

// The depth pass has its own, because it needs a different second half: which
// cascade is being rasterised, as its full transform.
//
// Passing the matrix rather than an index into the scene UBO is what keeps
// shadow.vert free of the UBO block entirely - and therefore unable to drift out
// of step with the three other declarations of it, which is a mismatch nothing
// diagnoses.
//
// 112 bytes, still inside the 128 every device guarantees.
struct ShadowPushConstantData {
    // cascadeViewProj * model, premultiplied on the CPU.
    //
    // Two matrices would be 128 bytes exactly, leaving nothing for the skinning
    // indices. Premultiplying is free here - the depth pass has no use for the
    // model matrix on its own - and matrix multiplication is associative, so
    // the per-vertex skin matrix still composes correctly on the right.
    glm::mat4 viewProjModel;      // 0..63   (vertex)
    int32_t skinPaletteBase{-1};  // 64..67  (vertex)
    int32_t skinJointCount{0};    // 68..71  (vertex)

    // The cut, and the alpha being cut. Read only by shadow_cutout.frag, and
    // only ever pushed with a cutoff above zero - the opaque depth pipeline
    // has no fragment stage worth the name and never looks at either.
    //
    // Two named floats rather than a vec2: they are not a vector, they come
    // from different fields of the material, and the scene pass keeps them
    // apart too (push.material.w and push.albedoColor.a).
    float alphaCutoff{0.0f};      // 72..75  (fragment)
    float baseAlpha{1.0f};        // 76..79  (fragment)

    // The same transform the scene pass reads out of a storage buffer, taken
    // BY VALUE here.
    //
    // Two transports for one layout, and the asymmetry is the reason: this
    // block has forty-eight bytes to spare where the scene pass's has none, so
    // the depth pass needs no descriptor, no gather order and no slot. What
    // keeps the two in step is that they are the same type and that its size
    // is asserted.
    //
    // Read in the VERTEX stage, unlike the two floats above it. A UV transform
    // is affine, so transforming coordinates once per vertex and interpolating
    // gives the same answer as interpolating and then transforming - and
    // shadow.vert already hands its texture coordinates to the cut-out shader,
    // so this costs a multiply per vertex instead of one per fragment.
    //
    // Without it a cut-out caster samples its albedo through UNTRANSFORMED
    // coordinates and casts the silhouette it used to have: a scrolled leaf
    // card shadows holes that have moved. Nothing diagnoses that.
    UvTransform uvTransform{};    // 80..111 (vertex)
    // 112 bytes total, and a multiple of 4 as vkCmdPushConstants requires.
};

// Pipeline creation switches, at namespace scope rather than nested inside
// VulkanPipeline.
//
// It has to be: the constructor takes `const Options& = {}`, and a nested
// class's default member initializers may not be used inside the enclosing
// class's own definition. MSVC accepts it as an extension, so this compiled
// here for weeks while GCC and Clang rejected every translation unit that
// included the header - which is what the Linux CI job had been failing on.
//
// The alias inside the class keeps every VulkanPipeline::Options call site
// working unchanged.
// Which equation a blended pipeline composites with. MaterialComponent::blend
// says which a surface wants; the blended pass binds one pipeline per run of
// equal equation (RenderSystem::BlendRun).
//
//   Mix            src * srcAlpha + dst * (1 - srcAlpha)
//   Add            src * srcAlpha + dst; the destination's alpha is left as it
//                  was, since a glow does not make what is behind it any more
//                  or less opaque
//   Premultiplied  src + dst * (1 - srcAlpha), colour and alpha alike, for a
//                  shader that has already multiplied its colour by its alpha
//                  (PushConstantData::kPremultiplied)
enum class BlendEquation : uint8_t { Mix, Add, Premultiplied };

struct VulkanPipelineOptions {
    bool depthWrite{true};
    bool blendEnable{false};

    // Meaningful only with blendEnable.
    BlendEquation blendEquation{BlendEquation::Mix};

    vk::CullModeFlags cullMode{vk::CullModeFlagBits::eBack};
    bool useVertexInput{true};

    // What the vertices mean. Triangles for everything that is a surface, and
    // lines for the world-space shape layer - an indicator, a range ring, a
    // path preview. A line list is not a degenerate triangle list; it is the
    // primitive those things are actually made of, and rasterising them as
    // triangles would need every line thickened into a quad on the CPU.
    vk::PrimitiveTopology topology{vk::PrimitiveTopology::eTriangleList};

    // An alternative vertex layout, for a pipeline whose input is not the
    // scene's Vertex.
    //
    // Empty means "use Vertex", which is every pipeline that draws geometry.
    // The shape layer's vertex is a position and a colour and nothing else -
    // no normal, no UV, no tangent, no skinning - and declaring the scene's
    // full layout for it would be describing attributes the buffer does not
    // contain, which is a validation error rather than wasted space.
    //
    // Pointers into the caller's storage, so both must outlive the
    // constructor call. They are read during pipeline creation and never
    // again.
    const vk::VertexInputBindingDescription* vertexBinding{nullptr};
    const vk::VertexInputAttributeDescription* vertexAttributes{nullptr};
    uint32_t vertexAttributeCount{0};

    // 0 for the depth-only shadow pass, whose render pass has no colour
    // attachment; a mismatch here is a pipeline/render-pass incompatibility.
    uint32_t colorAttachmentCount{1};

    // Constant + slope-scaled depth bias, which is what keeps a surface
    // from shadowing itself with acne.
    bool depthBias{false};
    float depthBiasConstant{1.25f};
    float depthBiasSlope{1.75f};

    // Optional. Null still works - it just means the driver recompiles the
    // SPIR-V from scratch, which is what happened for every pipeline on
    // every launch before PipelineCache existed.
    vk::PipelineCache cache{nullptr};

    // The depth pass declares a different push constant block from the scene
    // pass, so the range cannot be one fixed size for every pipeline.
    uint32_t pushConstantSize{static_cast<uint32_t>(sizeof(PushConstantData))};

    // Must match the render pass's attachments exactly; a mismatch is an
    // invalid pipeline, not a quality difference.
    vk::SampleCountFlagBits samples{vk::SampleCountFlagBits::e1};

    // Which stages the push constant range covers. vkCmdPushConstants requires
    // the stageFlags passed at record time to include EVERY stage the range
    // declares, so a range covering both stages cannot be pushed for one - the
    // depth pass declares vertex only because shadow.frag has no push block.
    vk::ShaderStageFlags pushConstantStages{
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
};

// The colour attachment's blend state the options above ask for.
//
// Out here and pure so a test can read the factors without a device: which of
// two blend equations a pipeline uses is otherwise visible only in a picture,
// and a swapped factor draws a glow as a dark square rather than failing.
vk::PipelineColorBlendAttachmentState ColorBlendFor(const VulkanPipelineOptions& options);

class VulkanPipeline {
public:
    using Options = VulkanPipelineOptions;

    VulkanPipeline(vk::Device device, vk::RenderPass renderPass,
                   const std::string& vertPath, const std::string& fragPath,
                   const Options& options = {});
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    vk::Pipeline GetPipeline() const { return m_graphicsPipeline; }
    vk::PipelineLayout GetLayout() const { return m_pipelineLayout; }

    // Set 0: per-frame scene data (UBO). Set 1: per-material texture.
    // Splitting these is what allows each entity to carry its own texture
    // instead of every draw sampling one globally-bound checkerboard.
    vk::DescriptorSetLayout GetSceneSetLayout() const { return m_sceneSetLayout; }
    vk::DescriptorSetLayout GetMaterialSetLayout() const { return m_materialSetLayout; }

    // How many environments can be bound at once.
    //
    // TWO, and the shader is written for exactly two. A sampler ARRAY indexed by
    // a push constant needs shaderSampledImageArrayDynamicIndexing, which this
    // device does not enable - so shader.frag picks between the slots at LITERAL
    // indices and lets the selection choose between the results, which is the
    // same shape pointShadowFactor already uses and for the same reason. Raising
    // this without rewriting that unroll is undefined behaviour that validates
    // cleanly and renders correctly on every desktop driver anyone would try.
    static constexpr uint32_t kMaxEnvironmentProbes = 2;
    static_assert(kMaxEnvironmentProbes == 2,
                  "shader.frag unrolls the probe selection at literal indices because "
                  "shaderSampledImageArrayDynamicIndexing is not enabled - raising this "
                  "means rewriting that unroll or enabling the feature");

    // How many combined image samplers ONE scene set consumes, so the pool that
    // feeds it can be sized from the same number the layout is built from.
    //
    // Spelled out here rather than counted again in the pool, because it has
    // already been wrong once: image-based lighting added bindings 8 and 9 and
    // the pool went on budgeting for the bindings that existed before them. A
    // pool size counts DESCRIPTORS, not bindings, and being short by two is a
    // spec violation that a lenient driver hands you anyway - so it works on
    // the machine it was written on and fails on somebody else's.
    //
    //   binding 1  shadowMaps          1
    //   binding 3  pointShadowMaps     PointShadow::kMaxShadowCasters
    //   binding 4  spotShadowMaps      1
    //   binding 8  irradianceMap       1
    //   binding 9  prefilteredMap      1
    static constexpr uint32_t kSamplersPerSceneSet =
        2u + PointShadow::kMaxShadowCasters + 2u * kMaxEnvironmentProbes;

    static constexpr uint32_t kSceneSet = 0;
    static constexpr uint32_t kMaterialSet = 1;

    // How many samplers a material set holds: albedo, normal, the packed
    // occlusion/roughness/metallic map, and the additive overlay
    // (MaterialComponent::overlayTexturePath), in binding order.
    //
    // Public because TextureRegistry sizes its descriptor pool from it. That
    // used to be a literal 2 in each of the two files, which is the shape of
    // mistake that does not fail: a pool sized for two bindings while the
    // layout declares three simply runs out of sets a third early, hundreds of
    // materials later, in a scene nobody was testing.
    static constexpr uint32_t kMaterialBindingCount = 4;

    // Where the overlay sits in the set, and in TextureRegistry's key.
    static constexpr uint32_t kOverlayBinding = 3;

private:
    void createDescriptorSetLayout();
    vk::ShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    void destroy() noexcept;

    vk::Device m_device{nullptr};
    vk::DescriptorSetLayout m_sceneSetLayout{nullptr};
    vk::DescriptorSetLayout m_materialSetLayout{nullptr};
    vk::PipelineLayout m_pipelineLayout{nullptr};
    vk::Pipeline m_graphicsPipeline{nullptr};
};

} // namespace Supersonic
