#include "renderer/VulkanOffscreen.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace Engine {

VulkanOffscreen::VulkanOffscreen(VulkanDevice& device, uint32_t width, uint32_t height)
    : m_deviceRef(device), m_width(width > 0 ? width : 1), m_height(height > 0 ? height : 1) {
    
    createRenderPass();
    createResources();
    createFramebuffer();
    createSamplerAndTextureID();

    std::cout << "[VulkanOffscreen] Created Offscreen Render Target (" << m_width << "x" << m_height << ")." << std::endl;
}

VulkanOffscreen::~VulkanOffscreen() {
    cleanup();
    if (m_renderPass) {
        m_deviceRef.GetDevice().destroyRenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

void VulkanOffscreen::cleanup() {
    vk::Device device = m_deviceRef.GetDevice();

    if (m_textureID != 0) {
        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_textureID));
        m_textureID = 0;
    }

    if (m_sampler) {
        device.destroySampler(m_sampler);
        m_sampler = nullptr;
    }

    if (m_framebuffer) {
        device.destroyFramebuffer(m_framebuffer);
        m_framebuffer = nullptr;
    }

    m_depthImage.reset();
    m_colorImage.reset();
}

void VulkanOffscreen::Recreate(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;

    m_width = width;
    m_height = height;

    m_deviceRef.GetDevice().waitIdle();
    cleanup();

    createResources();
    createFramebuffer();
    createSamplerAndTextureID();

    std::cout << "[VulkanOffscreen] Resized Offscreen Viewport Target (" << m_width << "x" << m_height << ")." << std::endl;
}

void VulkanOffscreen::createRenderPass() {
    // 1. Offscreen Color Attachment (Final layout = eShaderReadOnlyOptimal for ImGui sampling)
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = vk::Format::eR8G8B8A8Srgb;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    // 2. Offscreen Depth Attachment
    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = m_deviceRef.FindDepthFormat();
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    m_renderPass = m_deviceRef.GetDevice().createRenderPass(renderPassInfo);
}

void VulkanOffscreen::createResources() {
    m_colorImage = std::make_unique<VulkanImage>(
        m_deviceRef,
        m_width,
        m_height,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eColor
    );

    vk::Format depthFormat = m_deviceRef.FindDepthFormat();
    m_depthImage = std::make_unique<VulkanImage>(
        m_deviceRef,
        m_width,
        m_height,
        depthFormat,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::ImageAspectFlagBits::eDepth
    );
}

void VulkanOffscreen::createFramebuffer() {
    std::array<vk::ImageView, 2> attachments = {
        m_colorImage->GetImageView(),
        m_depthImage->GetImageView()
    };

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_width;
    framebufferInfo.height = m_height;
    framebufferInfo.layers = 1;

    m_framebuffer = m_deviceRef.GetDevice().createFramebuffer(framebufferInfo);
}

void VulkanOffscreen::createSamplerAndTextureID() {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

    m_sampler = m_deviceRef.GetDevice().createSampler(samplerInfo);

    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(m_sampler),
        static_cast<VkImageView>(m_colorImage->GetImageView()),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    m_textureID = (ImTextureID)(uintptr_t)ds;
}

} // namespace Engine
