#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"
#include "renderer/ShadowCascades.hpp"

namespace Supersonic {

struct LightComponent;

// Cascaded shadow map for the primary directional light.
//
// The scene is rendered depth-only from the light's point of view, once per
// cascade, into the layers of one 2D array image. The fragment shader picks a
// layer by view depth and samples it through a single sampler2DArray - an array
// of separate samplers would need a dynamically-uniform index, which a
// per-fragment cascade choice is not.
class ShadowMap {
public:
    // layerCount is how many depth slices the array holds. Four is the
    // cascade count and the reason this class exists; a spot light wants one
    // layer per caster and needs nothing else this class does differently, so
    // it borrows the same render pass, format selection and per-layer
    // framebuffers rather than duplicating them.
    explicit ShadowMap(VulkanDevice& device, uint32_t resolution = 2048,
                       uint32_t layerCount = kShadowCascadeCount);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    vk::RenderPass GetRenderPass() const { return m_renderPass; }

    // One framebuffer per cascade, each attached to a single layer.
    vk::Framebuffer GetFramebuffer(uint32_t layer) const {
        return layer < m_framebuffers.size() ? m_framebuffers[layer] : nullptr;
    }
    uint32_t GetLayerCount() const { return m_layerCount; }

    // The 2D_ARRAY view covering every layer, which is what the scene pass
    // samples.
    vk::ImageView GetImageView() const { return m_depthImage->GetImageView(); }
    vk::Sampler GetSampler() const { return m_depthImage->GetSampler(); }
    uint32_t GetResolution() const { return m_resolution; }

private:
    void createRenderPass();
    void createResources();
    void createFramebuffers();

    VulkanDevice& m_deviceRef;
    uint32_t m_resolution;
    uint32_t m_layerCount;

    // Chosen at construction: must be both a depth attachment and sampleable,
    // which the generic depth-format query does not guarantee.
    vk::Format m_format{vk::Format::eD32Sfloat};

    // Linear depth filtering is an optional format feature; falls back to
    // nearest where the driver does not advertise it.
    vk::Filter m_filter{vk::Filter::eLinear};
    vk::RenderPass m_renderPass{nullptr};
    std::unique_ptr<VulkanImage> m_depthImage;
    std::vector<vk::Framebuffer> m_framebuffers;
};

} // namespace Supersonic
