#include "renderer/ShadowMap.hpp"

#include <array>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

ShadowMap::ShadowMap(VulkanDevice& device, uint32_t resolution)
    : m_deviceRef(device), m_resolution(resolution == 0 ? 1024 : resolution) {

    // This image is SAMPLED, not just used as an attachment, so the generic
    // FindDepthFormat is not sufficient: it can return a depth/stencil format,
    // which would need a single-aspect view to sample, and linear filtering of
    // depth is only guaranteed where the driver advertises it.
    const vk::PhysicalDevice physical = m_deviceRef.GetPhysicalDevice();

    const auto supports = [&](vk::Format format, vk::FormatFeatureFlags features) {
        const vk::FormatProperties properties = physical.getFormatProperties(format);
        return (properties.optimalTilingFeatures & features) == features;
    };

    constexpr auto kAttachment = vk::FormatFeatureFlagBits::eDepthStencilAttachment;
    constexpr auto kSampled = vk::FormatFeatureFlagBits::eSampledImage;

    if (supports(vk::Format::eD32Sfloat, kAttachment | kSampled)) {
        m_format = vk::Format::eD32Sfloat;
    } else if (supports(vk::Format::eD16Unorm, kAttachment | kSampled)) {
        m_format = vk::Format::eD16Unorm;
    } else {
        m_format = m_deviceRef.FindDepthFormat();
        std::cerr << "[ShadowMap] No sampleable depth-only format; falling back to "
                  << vk::to_string(m_format) << "." << std::endl;
    }

    // Linear filtering of a depth image is an optional feature. Falling back to
    // nearest costs almost nothing here because the 3x3 PCF kernel in
    // shader.frag already does the smoothing.
    m_filter = supports(m_format, vk::FormatFeatureFlagBits::eSampledImageFilterLinear)
             ? vk::Filter::eLinear
             : vk::Filter::eNearest;

    createRenderPass();
    createResources();
    createFramebuffer();

    std::cout << "[ShadowMap] Created " << m_resolution << "x" << m_resolution
              << " directional shadow map (" << vk::to_string(m_format) << ", "
              << (m_filter == vk::Filter::eLinear ? "linear" : "nearest") << " filter)." << std::endl;
}

ShadowMap::~ShadowMap() {
    vk::Device device = m_deviceRef.GetDevice();

    if (m_framebuffer) {
        device.destroyFramebuffer(m_framebuffer);
        m_framebuffer = nullptr;
    }
    m_depthImage.reset();

    if (m_renderPass) {
        device.destroyRenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

void ShadowMap::createRenderPass() {
    // Depth only: no colour attachment at all.
    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = m_format;
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;  // sampled afterwards
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

    vk::AttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    // Same two-sided hazard as the offscreen target: this image is written here
    // and sampled by the scene pass, with frames overlapping.
    std::array<vk::SubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader
                                 | vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead
                                  | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

    vk::RenderPassCreateInfo info{};
    info.attachmentCount = 1;
    info.pAttachments = &depthAttachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();

    m_renderPass = m_deviceRef.GetDevice().createRenderPass(info);
}

void ShadowMap::createResources() {
    m_depthImage = std::make_unique<VulkanImage>(
        m_deviceRef, m_resolution, m_resolution, m_format,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eDepth);

    // Clamp to edge: fragments outside the light frustum sample the border
    // depth rather than wrapping and producing phantom shadows on the far side
    // of the scene. The shader also range-checks the projected coordinate.
    m_depthImage->CreateSampler(m_filter, vk::SamplerAddressMode::eClampToEdge);
}

void ShadowMap::createFramebuffer() {
    vk::ImageView attachment = m_depthImage->GetImageView();

    vk::FramebufferCreateInfo info{};
    info.renderPass = m_renderPass;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.width = m_resolution;
    info.height = m_resolution;
    info.layers = 1;

    m_framebuffer = m_deviceRef.GetDevice().createFramebuffer(info);
}

glm::mat4 ShadowMap::ComputeLightSpaceMatrix(const glm::vec3& lightDirection,
                                             float halfExtent, float distance) {
    // lightDirection points TOWARD the light, matching LightComponent and the
    // shader's L vector, so the eye sits along it and looks back at the origin.
    glm::vec3 dir = lightDirection;
    if (glm::length(dir) < 1e-4f) dir = glm::vec3(0.0f, 1.0f, 0.0f);
    dir = glm::normalize(dir);

    const glm::vec3 eye = dir * distance;

    // Avoid a degenerate up vector when the light is straight overhead.
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                 : glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), up);

    glm::mat4 proj = glm::ortho(-halfExtent, halfExtent,
                                -halfExtent, halfExtent,
                                0.1f, distance * 2.0f);
    proj[1][1] *= -1.0f; // Vulkan clip space, same convention as the camera

    return proj * view;
}

} // namespace Supersonic
