#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <entt/entt.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanSwapchain.hpp"
#include "renderer/VulkanPipeline.hpp"
#include "renderer/VulkanBuffer.hpp"
#include "renderer/VulkanImage.hpp"
#include "platform/Window.hpp"
#include "editor/EditorLayer.hpp"

namespace Engine {

class VulkanRenderer {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void DrawFrame(entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projMatrix);
    void RecreateSwapchain();

    vk::RenderPass GetRenderPass() const { return m_renderPass; }
    VulkanPipeline& GetPipeline() const { return *m_pipeline; }
    EditorLayer& GetEditorLayer() { return m_editorLayer; }

private:
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createGraphicsPipeline();

    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffers();
    void createTextureImage();
    void createDescriptorPool();
    void createDescriptorSets();
    void initImGui();

    void cleanupSwapchain();

    VulkanDevice& m_deviceRef;
    VulkanSwapchain& m_swapchainRef;
    Window& m_windowRef;

    // Swapchain Render Pass (ImGui UI Only)
    vk::RenderPass m_renderPass{nullptr};
    std::vector<vk::Framebuffer> m_framebuffers;

    std::unique_ptr<VulkanPipeline> m_pipeline;

    vk::CommandPool m_commandPool{nullptr};
    std::vector<vk::CommandBuffer> m_commandBuffers;

    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;

    // Geometry Buffers
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<VulkanBuffer> m_indexBuffer;
    uint32_t m_indexCount{0};

    // UBO Buffers (1 per frame in flight)
    std::vector<std::unique_ptr<VulkanBuffer>> m_uniformBuffers;

    // Texture Image & Sampler
    std::unique_ptr<VulkanImage> m_textureImage;

    // Descriptors
    vk::DescriptorPool m_descriptorPool{nullptr};
    std::vector<vk::DescriptorSet> m_descriptorSets;

    // ImGui Dedicated Descriptor Pool
    vk::DescriptorPool m_imguiPool{nullptr};

    // Editor Subsystem
    EditorLayer m_editorLayer;

    uint32_t m_currentFrame{0};
};

} // namespace Engine
