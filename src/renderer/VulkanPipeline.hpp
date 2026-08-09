#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace Engine {

class VulkanPipeline {
public:
    VulkanPipeline(vk::Device device, vk::RenderPass renderPass, const std::string& vertPath, const std::string& fragPath);
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    vk::Pipeline GetPipeline() const { return m_graphicsPipeline; }
    vk::PipelineLayout GetLayout() const { return m_pipelineLayout; }

private:
    vk::ShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    vk::Device m_device{nullptr};
    vk::PipelineLayout m_pipelineLayout{nullptr};
    vk::Pipeline m_graphicsPipeline{nullptr};
};

} // namespace Engine
