#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanSwapchain.hpp"
#include "renderer/VulkanPipeline.hpp"
#include "platform/Window.hpp"

namespace Engine {

class VulkanRenderer {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void DrawFrame();

    vk::RenderPass GetRenderPass() const { return m_renderPass; }

private:
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createGraphicsPipeline();

    VulkanDevice& m_deviceRef;
    VulkanSwapchain& m_swapchainRef;
    Window& m_windowRef;

    vk::RenderPass m_renderPass{nullptr};
    std::vector<vk::Framebuffer> m_framebuffers;

    std::unique_ptr<VulkanPipeline> m_pipeline;

    vk::CommandPool m_commandPool{nullptr};
    std::vector<vk::CommandBuffer> m_commandBuffers;

    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;

    uint32_t m_currentFrame{0};
};

} // namespace Engine
