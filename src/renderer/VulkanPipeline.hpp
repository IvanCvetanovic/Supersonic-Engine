#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

// GLM configuration lives on the CMake target - see core/Components.hpp.
#include <glm/glm.hpp>

namespace Engine {

// Per-frame scene constants. Light and camera data now travel to the GPU
// instead of being hardcoded literals in shader.frag, so LightComponent and the
// Inspector's light controls actually affect the image.
struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 cameraPosition;  // xyz = world position
    alignas(16) glm::vec4 lightDirection;  // xyz = direction toward the light
    alignas(16) glm::vec4 lightColor;      // rgb = colour, a = intensity
    alignas(16) glm::vec4 ambientColor;    // rgb = ambient term
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
    };

    VulkanPipeline(vk::Device device, vk::RenderPass renderPass,
                   const std::string& vertPath, const std::string& fragPath,
                   const Options& options = {});
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    vk::Pipeline GetPipeline() const { return m_graphicsPipeline; }
    vk::PipelineLayout GetLayout() const { return m_pipelineLayout; }
    vk::DescriptorSetLayout GetDescriptorSetLayout() const { return m_descriptorSetLayout; }

private:
    void createDescriptorSetLayout();
    vk::ShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    void destroy() noexcept;

    vk::Device m_device{nullptr};
    vk::DescriptorSetLayout m_descriptorSetLayout{nullptr};
    vk::PipelineLayout m_pipelineLayout{nullptr};
    vk::Pipeline m_graphicsPipeline{nullptr};
};

} // namespace Engine
