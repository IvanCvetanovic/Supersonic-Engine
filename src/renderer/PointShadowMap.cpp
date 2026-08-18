#include "renderer/PointShadowMap.hpp"

#include <array>
#include <iostream>

namespace Supersonic {

PointShadowMap::PointShadowMap(VulkanDevice& device, uint32_t resolution)
    : m_deviceRef(device), m_resolution(resolution == 0 ? 512 : resolution) {

    // Same requirement as the cascaded map: sampled as well as attached, so a
    // depth/stencil format would need a single-aspect view and is avoided.
    const vk::PhysicalDevice physical = m_deviceRef.GetPhysicalDevice();
    const auto supports = [&](vk::Format format, vk::FormatFeatureFlags features) {
        return (physical.getFormatProperties(format).optimalTilingFeatures & features) == features;
    };

    constexpr auto kNeeded = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
                             vk::FormatFeatureFlagBits::eSampledImage;

    if (supports(vk::Format::eD32Sfloat, kNeeded)) {
        m_format = vk::Format::eD32Sfloat;
    } else if (supports(vk::Format::eD16Unorm, kNeeded)) {
        m_format = vk::Format::eD16Unorm;
    } else {
        m_format = m_deviceRef.FindDepthFormat();
        std::cerr << "[PointShadowMap] No sampleable depth-only format; falling back to "
                  << vk::to_string(m_format) << "." << std::endl;
    }

    createRenderPass();
    createResources();

    std::cout << "[PointShadowMap] Created " << PointShadow::kMaxShadowCasters << " x "
              << m_resolution << "x" << m_resolution << " cube shadow map(s) ("
              << vk::to_string(m_format) << ")." << std::endl;
}

PointShadowMap::~PointShadowMap() {
    vk::Device device = m_deviceRef.GetDevice();

    for (auto framebuffer : m_framebuffers) {
        if (framebuffer) device.destroyFramebuffer(framebuffer);
    }
    m_framebuffers.clear();
    m_descriptorInfos.clear();

    for (auto& cube : m_cubes) cube.reset();

    if (m_renderPass) {
        device.destroyRenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

void PointShadowMap::createRenderPass() {
    vk::AttachmentDescription depth{};
    depth.format = m_format;
    depth.samples = vk::SampleCountFlagBits::e1;
    depth.loadOp = vk::AttachmentLoadOp::eClear;
    depth.storeOp = vk::AttachmentStoreOp::eStore;   // sampled by the scene pass
    depth.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depth.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth.initialLayout = vk::ImageLayout::eUndefined;
    depth.finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

    vk::AttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    // Two-sided, like every other target here: written in this pass, sampled by
    // the scene pass, and frames overlap.
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
    info.pAttachments = &depth;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();

    m_renderPass = m_deviceRef.GetDevice().createRenderPass(info);
}

void PointShadowMap::createResources() {
    m_framebuffers.reserve(PointShadow::kMaxShadowCasters * PointShadow::kFaceCount);
    m_descriptorInfos.reserve(PointShadow::kMaxShadowCasters);

    for (uint32_t caster = 0; caster < PointShadow::kMaxShadowCasters; ++caster) {
        m_cubes[caster] = std::make_unique<VulkanImage>(
            m_deviceRef, m_resolution, m_resolution, m_format,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eDepth,
            PointShadow::kFaceCount,
            vk::SampleCountFlagBits::e1,
            /*cubeCompatible=*/true);

        // Clamp: a direction that lands exactly on a face edge must not wrap to
        // the opposite side of the cube and shadow the fragment with geometry
        // behind the light.
        m_cubes[caster]->CreateSampler(vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge);

        // One framebuffer per face, each attached to that face's single layer.
        for (uint32_t face = 0; face < PointShadow::kFaceCount; ++face) {
            vk::ImageView attachment = m_cubes[caster]->GetLayerView(face);

            vk::FramebufferCreateInfo info{};
            info.renderPass = m_renderPass;
            info.attachmentCount = 1;
            info.pAttachments = &attachment;
            info.width = m_resolution;
            info.height = m_resolution;
            info.layers = 1;

            m_framebuffers.push_back(m_deviceRef.GetDevice().createFramebuffer(info));
        }

        // Populated for every slot whether or not a light is using it, so the
        // descriptor set is always complete. An unwritten descriptor is
        // undefined behaviour the moment any shader reads the array, even in a
        // branch it never takes.
        vk::DescriptorImageInfo descriptor{};
        descriptor.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        descriptor.imageView = m_cubes[caster]->GetImageView();
        descriptor.sampler = m_cubes[caster]->GetSampler();
        m_descriptorInfos.push_back(descriptor);
    }
}

vk::Framebuffer PointShadowMap::GetFramebuffer(uint32_t caster, uint32_t face) const {
    const size_t index = static_cast<size_t>(caster) * PointShadow::kFaceCount + face;
    return index < m_framebuffers.size() ? m_framebuffers[index] : nullptr;
}

} // namespace Supersonic
