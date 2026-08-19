#include "renderer/VulkanOffscreen.hpp"
#include "core/Log.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace Supersonic {

VulkanOffscreen::VulkanOffscreen(VulkanDevice& device, uint32_t width, uint32_t height)
    : m_deviceRef(device), m_width(width > 0 ? width : 1), m_height(height > 0 ? height : 1) {
    
    // Chosen before anything else: the render pass, the images and every
    // pipeline drawing into this target all have to agree on it.
    m_samples = m_deviceRef.GetMaxUsableSampleCount(vk::SampleCountFlagBits::e4);

    createRenderPass();
    createResources();
    createFramebuffer();
    createSamplerAndTextureID();
    createBloom();

    SUPERSONIC_LOG_INFO("VulkanOffscreen") << "Created Offscreen Render Target (" << m_width << "x" << m_height
              << ", " << static_cast<uint32_t>(m_samples) << "x MSAA)." << std::endl;
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

    // Before the images it samples.
    m_bloom.reset();

    m_depthImage.reset();
    m_colorImage.reset();
    m_resolveImage.reset();
}

void VulkanOffscreen::RequestResize(uint32_t width, uint32_t height) {
    // Only records intent. Actually destroying and rebuilding the framebuffer
    // here would be a use-after-free: this is called while building the ImGui
    // frame, at which point DrawFrame has already recorded a render pass
    // referencing the current framebuffer into a command buffer that has not
    // been submitted yet. vkDeviceWaitIdle does not help - it drains submitted
    // work, and the offending buffer is still in the recording state.
    m_pendingWidth = width;
    m_pendingHeight = height;
}

bool VulkanOffscreen::ApplyPendingResize() {
    if (m_pendingWidth == 0 || m_pendingHeight == 0) return false;
    if (m_pendingWidth == m_width && m_pendingHeight == m_height) return false;

    m_width = m_pendingWidth;
    m_height = m_pendingHeight;

    // Safe here: called from the top of the frame, before any recording begins.
    m_deviceRef.GetDevice().waitIdle();
    cleanup();

    createResources();
    createFramebuffer();
    createSamplerAndTextureID();
    createBloom();

    SUPERSONIC_LOG_INFO("VulkanOffscreen") << "Resized Offscreen Viewport Target (" << m_width << "x" << m_height << ")." << std::endl;
    return true;
}

void VulkanOffscreen::createRenderPass() {
    // 1. Offscreen Color Attachment (Final layout = eShaderReadOnlyOptimal for ImGui sampling)
    //
    // UNORM, not SRGB. shader.frag already encodes to sRGB itself; an SRGB
    // attachment would encode a second time on store, and because ImGui samples
    // this image and writes it to the swapchain without any colour conversion,
    // nothing downstream cancels it. UNORM here means the shader's encoded
    // value is stored and presented verbatim - one encode, as intended.
    const bool multisampled = m_samples != vk::SampleCountFlagBits::e1;

    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = kColorFormat;
    colorAttachment.samples = m_samples;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    // Nothing reads the multisampled image after the pass: the resolve is what
    // survives, so storing the samples would be pure bandwidth.
    colorAttachment.storeOp = multisampled ? vk::AttachmentStoreOp::eDontCare
                                           : vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = multisampled ? vk::ImageLayout::eColorAttachmentOptimal
                                               : vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    // The single-sample image ImGui samples. A multisampled image cannot be
    // sampled in a shader at all, so with MSAA on this is the only one that can
    // be shown.
    vk::AttachmentDescription resolveAttachment{};
    resolveAttachment.format = kColorFormat;
    resolveAttachment.samples = vk::SampleCountFlagBits::e1;
    resolveAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
    resolveAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    resolveAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    resolveAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    resolveAttachment.initialLayout = vk::ImageLayout::eUndefined;
    resolveAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference resolveAttachmentRef{};
    resolveAttachmentRef.attachment = 2;
    resolveAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    // 2. Offscreen Depth Attachment
    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = m_deviceRef.FindDepthFormat();
    depthAttachment.samples = m_samples;
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
    subpass.pResolveAttachments = multisampled ? &resolveAttachmentRef : nullptr;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Two dependencies, because this image is both written here and sampled by
    // ImGui in the swapchain pass, with MAX_FRAMES_IN_FLIGHT frames overlapping.
    std::array<vk::SubpassDependency, 2> dependencies{};

    // Frame N+1 must not touch this image while frame N is still using it. A
    // single colour/depth target is shared by MAX_FRAMES_IN_FLIGHT frames, so
    // there are two distinct hazards to cover:
    //   WRITE_AFTER_READ  - N+1's clear vs N's ImGui sample (eFragmentShader /
    //                       eShaderRead)
    //   WRITE_AFTER_WRITE - N+1's loadOp clear vs N's storeOp write
    //                       (eColorAttachmentWrite / eDepthStencilAttachmentWrite)
    // Declaring only eShaderRead left the write-after-write uncovered, which
    // synchronization validation reports at every vkQueueSubmit.
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput
                                 | vk::PipelineStageFlagBits::eLateFragmentTests
                                 | vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead
                                  | vk::AccessFlagBits::eColorAttachmentWrite
                                  | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput
                                 | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite
                                  | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    // READ_AFTER_WRITE: make the colour write visible to ImGui's sampled read.
    // Without this the viewport can present a stale or torn frame.
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

    const std::array<vk::AttachmentDescription, 3> attachments = {
        colorAttachment, depthAttachment, resolveAttachment
    };

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = multisampled ? 3u : 2u;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    m_renderPass = m_deviceRef.GetDevice().createRenderPass(renderPassInfo);
}

void VulkanOffscreen::createResources() {
    const bool multisampled = m_samples != vk::SampleCountFlagBits::e1;

    // eTransientAttachment would be the ideal usage for the multisampled colour
    // and depth, since neither outlives the pass - but it requires lazily
    // allocated memory that VMA is not being asked for here, so plain
    // attachment usage it is.
    m_colorImage = std::make_unique<VulkanImage>(
        m_deviceRef,
        m_width,
        m_height,
        kColorFormat,
        multisampled ? vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment)
                     : (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled),
        vk::ImageAspectFlagBits::eColor,
        1,
        m_samples
    );

    vk::Format depthFormat = m_deviceRef.FindDepthFormat();
    m_depthImage = std::make_unique<VulkanImage>(
        m_deviceRef,
        m_width,
        m_height,
        depthFormat,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::ImageAspectFlagBits::eDepth,
        1,
        m_samples
    );

    if (multisampled) {
        m_resolveImage = std::make_unique<VulkanImage>(
            m_deviceRef,
            m_width,
            m_height,
            kColorFormat,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor
        );
    }
}

void VulkanOffscreen::createFramebuffer() {
    const bool multisampled = m_resolveImage != nullptr;

    const std::array<vk::ImageView, 3> attachments = {
        m_colorImage->GetImageView(),
        m_depthImage->GetImageView(),
        multisampled ? m_resolveImage->GetImageView() : vk::ImageView{}
    };

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = multisampled ? 3u : 2u;
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

    // Only the sampler is made here; the view ImGui shows is decided in
    // createBloom(), because the scene image is floating point and un-encoded -
    // showing it directly would present linear values as if they were sRGB.
}


void VulkanOffscreen::createBloom() {
    // The scene image the chain reads: the resolve when multisampling, because
    // a multisampled image cannot be sampled at all.
    const vk::ImageView sceneView = m_resolveImage ? m_resolveImage->GetImageView()
                                                   : m_colorImage->GetImageView();

    m_bloom = std::make_unique<BloomPass>(m_deviceRef, m_width, m_height, sceneView, m_sampler);

    // ImGui shows the bloom chain's output, not the scene image. The scene
    // image is linear and floating point; presenting it directly would show
    // un-tone-mapped, un-encoded values.
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(m_bloom->GetOutputSampler()),
        static_cast<VkImageView>(m_bloom->GetOutputView()),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    m_textureID = (ImTextureID)(uintptr_t)ds;
}

void VulkanOffscreen::RecordPostProcess(vk::CommandBuffer commandBuffer) const {
    if (m_bloom) m_bloom->Record(commandBuffer);
}

} // namespace Supersonic
