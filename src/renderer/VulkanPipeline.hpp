#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Engine {

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

struct PushConstantData {
    glm::mat4 model;
};

class VulkanPipeline {
public:
    VulkanPipeline(vk::Device device, vk::RenderPass renderPass, const std::string& vertPath, const std::string& fragPath);
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

    vk::Device m_device{nullptr};
    vk::DescriptorSetLayout m_descriptorSetLayout{nullptr};
    vk::PipelineLayout m_pipelineLayout{nullptr};
    vk::Pipeline m_graphicsPipeline{nullptr};
};

} // namespace Engine
