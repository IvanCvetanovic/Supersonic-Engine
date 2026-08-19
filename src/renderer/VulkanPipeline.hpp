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

// Maximum simultaneous lights. Kept small and fixed so the whole set fits in a
// plain UBO with no storage buffer or bindless machinery.
inline constexpr int kMaxLights = 8;

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
    GpuLight lights[kMaxLights];
};

// 120 bytes, inside the 128-byte guaranteed minimum.
struct PushConstantData {
    glm::mat4 model;         // 0..63   (vertex)
    glm::vec4 albedoColor;   // 64..79  (fragment)
    glm::vec4 material;      // 80..95  x=roughness y=metallic z=ao (fragment)

    // Light this surface emits regardless of what falls on it. rgb is added
    // after shading; a is unused padding.
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
};

// The depth pass has its own, because it needs a different second half: which
// cascade is being rasterised, as its full transform.
//
// Passing the matrix rather than an index into the scene UBO is what keeps
// shadow.vert free of the UBO block entirely - and therefore unable to drift out
// of step with the three other declarations of it, which is a mismatch nothing
// diagnoses.
//
// 128 bytes exactly: the guaranteed minimum, and all of it.
struct ShadowPushConstantData {
    // cascadeViewProj * model, premultiplied on the CPU.
    //
    // Two matrices would be 128 bytes exactly, leaving nothing for the skinning
    // indices. Premultiplying is free here - the depth pass has no use for the
    // model matrix on its own - and matrix multiplication is associative, so
    // the per-vertex skin matrix still composes correctly on the right.
    glm::mat4 viewProjModel;      // 0..63
    int32_t skinPaletteBase{-1};  // 64..67
    int32_t skinJointCount{0};    // 68..71
    // 72 bytes total, and a multiple of 4 as vkCmdPushConstants requires.
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
struct VulkanPipelineOptions {
    bool depthWrite{true};
    bool blendEnable{false};
    vk::CullModeFlags cullMode{vk::CullModeFlagBits::eBack};
    bool useVertexInput{true};

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

    static constexpr uint32_t kSceneSet = 0;
    static constexpr uint32_t kMaterialSet = 1;

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
