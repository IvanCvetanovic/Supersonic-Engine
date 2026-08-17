#include "renderer/VulkanPipeline.hpp"
#include "core/Components.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Engine {

namespace {

// Destroys a shader module when it leaves scope. Without this, a throw between
// creating the vertex module and finishing the pipeline leaked it: the
// destructor never runs for an object whose constructor did not complete.
class ScopedShaderModule {
public:
    ScopedShaderModule(vk::Device device, vk::ShaderModule module)
        : m_device(device), m_module(module) {}
    ~ScopedShaderModule() {
        if (m_module) m_device.destroyShaderModule(m_module);
    }
    ScopedShaderModule(const ScopedShaderModule&) = delete;
    ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;

    vk::ShaderModule get() const { return m_module; }

private:
    vk::Device m_device;
    vk::ShaderModule m_module;
};

} // namespace

VulkanPipeline::VulkanPipeline(vk::Device device, vk::RenderPass renderPass,
                               const std::string& vertPath, const std::string& fragPath,
                               const Options& options)
    : m_device(device) {

    if (!m_device) {
        throw std::runtime_error("Cannot create VulkanPipeline with null logical device!");
    }
    if (!renderPass) {
        throw std::runtime_error("Cannot create VulkanPipeline with null RenderPass!");
    }

    // Anything acquired past this point must survive a throw, so every failure
    // path runs destroy() before rethrowing.
    try {
        createDescriptorSetLayout();

        const auto vertShaderCode = readFile(vertPath);
        const auto fragShaderCode = readFile(fragPath);

        ScopedShaderModule vertModule(m_device, createShaderModule(vertShaderCode));
        ScopedShaderModule fragModule(m_device, createShaderModule(fragShaderCode));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
        vertShaderStageInfo.module = vertModule.get();
        vertShaderStageInfo.pName = "main";

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
        fragShaderStageInfo.module = fragModule.get();
        fragShaderStageInfo.pName = "main";

        const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        // Vertex Input State: Configured from Vertex struct
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        if (options.useVertexInput) {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        }

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        const std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = options.cullMode;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = options.depthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;
        if (options.blendEnable) {
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        } else {
            colorBlendAttachment.blendEnable = VK_FALSE;
        }

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // One range spanning both stages: the vertex stage reads the model
        // matrix, the fragment stage reads albedo and material parameters.
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstantData);
        static_assert(sizeof(PushConstantData) <= 128,
                      "Push constants must fit the 128-byte guaranteed minimum");

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);
        if (!m_pipelineLayout) {
            throw std::runtime_error("Failed to create Vulkan Pipeline Layout!");
        }

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create Vulkan Graphics Pipeline! Result code: "
                                     + std::to_string(static_cast<int>(result.result)));
        }
        m_graphicsPipeline = result.value;
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanPipeline::~VulkanPipeline() {
    destroy();
}

void VulkanPipeline::destroy() noexcept {
    if (!m_device) return;

    if (m_graphicsPipeline) {
        m_device.destroyPipeline(m_graphicsPipeline);
        m_graphicsPipeline = nullptr;
    }
    if (m_pipelineLayout) {
        m_device.destroyPipelineLayout(m_pipelineLayout);
        m_pipelineLayout = nullptr;
    }
    if (m_descriptorSetLayout) {
        m_device.destroyDescriptorSetLayout(m_descriptorSetLayout);
        m_descriptorSetLayout = nullptr;
    }
}

void VulkanPipeline::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboLayoutBinding.descriptorCount = 1;
    // The fragment stage now reads light and camera data from the same UBO.
    uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
        uboLayoutBinding,
        samplerLayoutBinding
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    m_descriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
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
        throw std::runtime_error(
            "Failed to open shader file at path: " + filename +
            "\nThe engine resolves asset paths relative to the working directory; "
            "launch it from the project root.");
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));

    if (!file) {
        throw std::runtime_error("Failed to read shader file: " + filename);
    }
    return buffer;
}

} // namespace Engine
