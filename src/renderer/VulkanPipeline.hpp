#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

// GLM configuration lives on the CMake target - see core/Components.hpp.
#include <glm/glm.hpp>

namespace Supersonic {

// Maximum simultaneous lights. Kept small and fixed so the whole set fits in a
// plain UBO with no storage buffer or bindless machinery.
inline constexpr int kMaxLights = 8;

enum class LightType : int {
    Directional = 0,
    Point = 1,
};

// std140 layout: every member is vec4-aligned so the C++ and GLSL views match.
struct GpuLight {
    alignas(16) glm::vec4 positionOrDirection{0.0f, 1.0f, 0.0f, 0.0f}; // w = type
    alignas(16) glm::vec4 colorAndIntensity{1.0f};                     // rgb, a = intensity
    alignas(16) glm::vec4 attenuation{25.0f, 0.0f, 0.0f, 0.0f};        // x = range
};

// Per-frame scene constants. Light and camera data travel to the GPU instead of
// being hardcoded literals in shader.frag, and there is now an array of lights
// rather than a single hardcoded direction.
struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 lightSpace;      // shadow lookup transform
    alignas(16) glm::vec4 cameraPosition;  // xyz = world position
    alignas(16) glm::vec4 ambientColor;    // rgb = ambient term
    alignas(16) glm::vec4 lightCount;      // x = active light count
    GpuLight lights[kMaxLights];
};

// 96 bytes, comfortably inside the 128-byte guaranteed minimum.
struct PushConstantData {
    glm::mat4 model;         // 0..63   (vertex)
    glm::vec4 albedoColor;   // 64..79  (fragment)
    glm::vec4 material;      // 80..95  x=roughness y=metallic z=ao (fragment)
};

class VulkanPipeline {
public:
    struct Options {
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
    };

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
