#include "renderer/VulkanPipeline.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Engine {

VulkanPipeline::VulkanPipeline(vk::Device device, vk::RenderPass renderPass, const std::string& vertPath, const std::string& fragPath)
    : m_device(device) {
    
    if (!m_device) {
        throw std::runtime_error("Cannot create VulkanPipeline with null logical device!");
    }
    if (!renderPass) {
        throw std::runtime_error("Cannot create VulkanPipeline with null RenderPass!");
    }

    auto vertShaderCode = readFile(vertPath);
    auto fragShaderCode = readFile(fragPath);

    vk::ShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    vk::ShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo,
        fragShaderStageInfo
    };

    // Vertex Input State: Empty (vertices hardcoded in vertex shader)
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Input Assembly State: eTriangleList
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport & Scissor (using dynamic state)
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization State: eFill, eCullBack, eClockwise
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling State: 1 sample
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Color Blend Attachment State: Opaque
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                          vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB |
                                          vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);
    if (!m_pipelineLayout) {
        throw std::runtime_error("Failed to create Vulkan Pipeline Layout!");
    }

    // Graphics Pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create Vulkan Graphics Pipeline! Result code: " + std::to_string(static_cast<int>(result.result)));
    }
    m_graphicsPipeline = result.value;

    // Immediate cleanup of temporary shader modules after pipeline creation
    m_device.destroyShaderModule(fragShaderModule);
    m_device.destroyShaderModule(vertShaderModule);

    std::cout << "[VulkanPipeline] Graphics Pipeline created successfully." << std::endl;
}

VulkanPipeline::~VulkanPipeline() {
    if (m_graphicsPipeline) {
        m_device.destroyPipeline(m_graphicsPipeline);
        m_graphicsPipeline = nullptr;
    }
    if (m_pipelineLayout) {
        m_device.destroyPipelineLayout(m_pipelineLayout);
        m_pipelineLayout = nullptr;
    }
    std::cout << "[VulkanPipeline] Graphics Pipeline resources destroyed." << std::endl;
}

vk::ShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    if (code.empty() || code.size() % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V bytecode size!");
    }

    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return m_device.createShaderModule(createInfo);
}

std::vector<char> VulkanPipeline::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file at path: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

} // namespace Engine
