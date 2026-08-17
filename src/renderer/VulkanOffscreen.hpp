#pragma once

#include <memory>
#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

namespace Engine {

class VulkanOffscreen {
public:
    // Manual sRGB encoding happens in shader.frag, so the attachment must be
    // UNORM or the value is gamma-encoded twice. See createRenderPass().
    static constexpr vk::Format kColorFormat = vk::Format::eR8G8B8A8Unorm;

    VulkanOffscreen(VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanOffscreen();

    VulkanOffscreen(const VulkanOffscreen&) = delete;
    VulkanOffscreen& operator=(const VulkanOffscreen&) = delete;

    // Records a desired size. Safe to call at any point in the frame, including
    // while ImGui is being built.
    void RequestResize(uint32_t width, uint32_t height);

    // Performs any pending resize. MUST only be called at the top of a frame,
    // before command buffer recording begins. Returns true if it resized.
    bool ApplyPendingResize();

    vk::RenderPass GetRenderPass() const { return m_renderPass; }
    vk::Framebuffer GetFramebuffer() const { return m_framebuffer; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    ImTextureID GetTextureID() const { return m_textureID; }

private:
    void createRenderPass();
    void createResources();
    void createFramebuffer();
    void createSamplerAndTextureID();
    void cleanup();

    VulkanDevice& m_deviceRef;
    uint32_t m_width{1280};
    uint32_t m_height{720};
    uint32_t m_pendingWidth{0};
    uint32_t m_pendingHeight{0};

    vk::RenderPass m_renderPass{nullptr};
    std::unique_ptr<VulkanImage> m_colorImage;
    std::unique_ptr<VulkanImage> m_depthImage;
    vk::Framebuffer m_framebuffer{nullptr};

    vk::Sampler m_sampler{nullptr};
    ImTextureID m_textureID{0};
};

} // namespace Engine
