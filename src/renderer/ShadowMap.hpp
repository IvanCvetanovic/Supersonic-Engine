#pragma once

#include <memory>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"

namespace Engine {

struct LightComponent;

// Single-cascade shadow map for the primary directional light.
//
// The scene is rendered depth-only from the light's point of view into this
// image, which the main fragment shader then samples to decide what is in
// shadow. There were no shadows at all before this.
class ShadowMap {
public:
    explicit ShadowMap(VulkanDevice& device, uint32_t resolution = 2048);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    vk::RenderPass GetRenderPass() const { return m_renderPass; }
    vk::Framebuffer GetFramebuffer() const { return m_framebuffer; }
    vk::ImageView GetImageView() const { return m_depthImage->GetImageView(); }
    vk::Sampler GetSampler() const { return m_depthImage->GetSampler(); }
    uint32_t GetResolution() const { return m_resolution; }

    // Orthographic light-space matrix covering a box of the given half-extent
    // around the origin. A single fixed cascade: enough for a scene of this
    // size, and the obvious thing to replace with CSM later.
    static glm::mat4 ComputeLightSpaceMatrix(const glm::vec3& lightDirection,
                                             float halfExtent = 24.0f,
                                             float distance = 40.0f);

private:
    void createRenderPass();
    void createResources();
    void createFramebuffer();

    VulkanDevice& m_deviceRef;
    uint32_t m_resolution;

    vk::Format m_format{vk::Format::eD32Sfloat};
    vk::RenderPass m_renderPass{nullptr};
    std::unique_ptr<VulkanImage> m_depthImage;
    vk::Framebuffer m_framebuffer{nullptr};
};

} // namespace Engine
