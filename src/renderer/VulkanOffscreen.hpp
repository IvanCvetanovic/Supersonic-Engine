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
    VulkanOffscreen(VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanOffscreen();

    VulkanOffscreen(const VulkanOffscreen&) = delete;
    VulkanOffscreen& operator=(const VulkanOffscreen&) = delete;

    void Recreate(uint32_t width, uint32_t height);

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

    vk::RenderPass m_renderPass{nullptr};
    std::unique_ptr<VulkanImage> m_colorImage;
    std::unique_ptr<VulkanImage> m_depthImage;
    vk::Framebuffer m_framebuffer{nullptr};

    vk::Sampler m_sampler{nullptr};
    ImTextureID m_textureID{0};
};

} // namespace Engine
