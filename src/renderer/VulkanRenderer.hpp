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
#include "core/RenderSystem.hpp"
#include "renderer/MeshRegistry.hpp"
#include "renderer/TextureRegistry.hpp"
#include "renderer/ShadowMap.hpp"
#include "renderer/PipelineCache.hpp"
#include "platform/Window.hpp"

struct ImDrawData;

namespace Supersonic {

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

    // Last frame's culling counters, for the editor's statistics panel.
    const RenderSystem::Stats& GetRenderStats() const { return m_renderStats; }

    MeshRegistry& GetMeshRegistry() { return *m_meshRegistry; }
    TextureRegistry& GetTextureRegistry() { return *m_textureRegistry; }

private:
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void destroySyncObjects();
    void createGraphicsPipeline();

    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void initImGui();

    // Fills the UBO from the scene's lights and returns the light-space matrix
    // used by both the shadow pass and the shadow lookup.
    glm::mat4 gatherLights(entt::registry& registry, UniformBufferObject& ubo) const;

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

    // Depth-only pass from the primary directional light.
    std::unique_ptr<ShadowMap> m_shadowMap;
    std::unique_ptr<PipelineCache> m_pipelineCache;
    RenderSystem::Stats m_renderStats{};
    std::unique_ptr<VulkanPipeline> m_shadowPipeline;

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
    std::unique_ptr<TextureRegistry> m_textureRegistry;

    // UBO Buffers (1 per frame in flight)
    std::vector<std::unique_ptr<VulkanBuffer>> m_uniformBuffers;

    // Per-frame scene descriptor sets (UBO + shadow map).
    vk::DescriptorPool m_descriptorPool{nullptr};
    std::vector<vk::DescriptorSet> m_descriptorSets;

    // ImGui Dedicated Descriptor Pool
    vk::DescriptorPool m_imguiPool{nullptr};

    uint32_t m_currentFrame{0};
};

} // namespace Supersonic
