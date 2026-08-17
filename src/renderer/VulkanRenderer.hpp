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
#include "renderer/VulkanOffscreen.hpp"
#include "renderer/MeshRegistry.hpp"
#include "platform/Window.hpp"

struct ImDrawData;

namespace Engine {

// Records and submits frames. Deliberately knows nothing about the editor:
// the offscreen target and the finished ImGui draw data are handed in, so the
// UI can no longer mutate GPU resource lifetimes mid-recording.
class VulkanRenderer {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // Starts an ImGui frame. The caller builds its UI, calls ImGui::Render(),
    // then passes the resulting draw data to DrawFrame.
    void NewImGuiFrame();

    void DrawFrame(entt::registry& registry,
                   VulkanOffscreen& offscreen,
                   ImDrawData* drawData,
                   const glm::mat4& viewMatrix,
                   const glm::mat4& projMatrix,
                   const glm::vec3& cameraPosition);

    void RecreateSwapchain();

    vk::RenderPass GetRenderPass() const { return m_renderPass; }
    vk::RenderPass GetOffscreenRenderPass() const { return m_offscreenRenderPass; }
    void SetOffscreenRenderPass(vk::RenderPass pass);

    MeshRegistry& GetMeshRegistry() { return *m_meshRegistry; }

private:
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void destroySyncObjects();
    void createGraphicsPipeline();

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

    // Render pass the 3D scene pipeline is built against; owned by the
    // offscreen target, cached here so the pipeline can be rebuilt.
    vk::RenderPass m_offscreenRenderPass{nullptr};
    std::unique_ptr<VulkanPipeline> m_pipeline;
    std::unique_ptr<VulkanPipeline> m_gridPipeline;

    vk::CommandPool m_commandPool{nullptr};
    std::vector<vk::CommandBuffer> m_commandBuffers;

    // imageAvailable + inFlight are per frame-in-flight. renderFinished is per
    // swapchain image: a submit must not re-signal a semaphore whose present
    // wait has not been consumed yet, which two semaphores cannot guarantee
    // across a three-image swapchain.
    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;
    std::vector<vk::Fence> m_imagesInFlight;

    std::unique_ptr<MeshRegistry> m_meshRegistry;

    // UBO Buffers (1 per frame in flight)
    std::vector<std::unique_ptr<VulkanBuffer>> m_uniformBuffers;

    // Texture Image & Sampler
    std::unique_ptr<VulkanImage> m_textureImage;

    // Descriptors
    vk::DescriptorPool m_descriptorPool{nullptr};
    std::vector<vk::DescriptorSet> m_descriptorSets;

    // ImGui Dedicated Descriptor Pool
    vk::DescriptorPool m_imguiPool{nullptr};

    uint32_t m_currentFrame{0};
};

} // namespace Engine
