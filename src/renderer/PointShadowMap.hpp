#pragma once

#include <array>
#include <memory>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/PointShadow.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"

namespace Supersonic {

// Cube shadow maps for point lights.
//
// Fixed count, allocated once at startup rather than per light. A scene can
// create and destroy lights at any moment, including during play, and tying
// image lifetime to that would put allocation on the same code path as the
// swapchain rebuild - which is where the awkward synchronisation bugs live and
// is far harder to stress than a window resize.
//
// Lights beyond the cap simply do not cast, which is a visible and predictable
// degradation rather than a stutter or a validation error.
class PointShadowMap {
public:
    explicit PointShadowMap(VulkanDevice& device, uint32_t resolution = 1024);
    ~PointShadowMap();

    PointShadowMap(const PointShadowMap&) = delete;
    PointShadowMap& operator=(const PointShadowMap&) = delete;

    vk::RenderPass GetRenderPass() const { return m_renderPass; }

    // One framebuffer per face of each cube.
    vk::Framebuffer GetFramebuffer(uint32_t caster, uint32_t face) const;

    // CUBE views, one per caster, in the order the descriptor array expects.
    // Always fully populated, so every descriptor is valid whether the scene
    // has that many shadow-casting lights or none at all.
    const std::vector<vk::DescriptorImageInfo>& GetDescriptorInfos() const {
        return m_descriptorInfos;
    }

    uint32_t GetResolution() const { return m_resolution; }
    static constexpr uint32_t Capacity() { return PointShadow::kMaxShadowCasters; }

private:
    void createRenderPass();
    void createResources();

    VulkanDevice& m_deviceRef;
    uint32_t m_resolution;
    vk::Format m_format{vk::Format::eD32Sfloat};
    vk::RenderPass m_renderPass{nullptr};

    std::array<std::unique_ptr<VulkanImage>, PointShadow::kMaxShadowCasters> m_cubes{};
    std::vector<vk::Framebuffer> m_framebuffers;
    std::vector<vk::DescriptorImageInfo> m_descriptorInfos;
};

} // namespace Supersonic
