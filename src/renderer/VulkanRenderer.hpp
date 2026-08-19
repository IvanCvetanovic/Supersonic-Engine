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
#include "renderer/PointShadowMap.hpp"
#include "renderer/SpotLight.hpp"
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

    // Monotonic across the run, unlike m_currentFrame which cycles 0..N-1.
    // Deferred destruction needs to compare frames that are far apart.
    uint64_t m_absoluteFrame{0};

    VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // Starts an ImGui frame. The caller builds its UI, calls ImGui::Render(),
    // then passes the resulting draw data to DrawFrame.
    void NewImGuiFrame();

    // Takes the camera itself rather than pre-derived matrices: cascade fitting
    // needs the field of view, the aspect ratio and the near/far planes, and
    // recovering those from a projection matrix is a worse idea than passing
    // the thing they came from.
    void DrawFrame(entt::registry& registry,
                   VulkanOffscreen& offscreen,
                   ImDrawData* drawData,
                   const CameraComponent& camera);

    void RecreateSwapchain();

    vk::RenderPass GetRenderPass() const { return m_renderPass; }
    vk::RenderPass GetOffscreenRenderPass() const { return m_offscreenRenderPass; }
    void SetOffscreenRenderPass(vk::RenderPass pass, vk::SampleCountFlagBits samples);

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
    // A spot light that was given a shadow layer this frame.
    struct SpotShadowCaster {
        glm::mat4 viewProj{1.0f};
        uint32_t slot{0};
    };

    // A point light that was given a cube this frame.
    struct PointShadowCaster {
        glm::vec3 position{0.0f};
        float range{25.0f};
        uint32_t slot{0};
    };

    // Fills the light block and returns the direction the cascades should be
    // fitted to. Point lights that win a cube are appended to outCasters, in
    // slot order.
    glm::vec3 gatherLights(entt::registry& registry, UniformBufferObject& ubo,
                           std::vector<PointShadowCaster>& outCasters,
                           std::vector<SpotShadowCaster>& outSpots) const;

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
    vk::SampleCountFlagBits m_offscreenSamples{vk::SampleCountFlagBits::e1};
    std::unique_ptr<VulkanPipeline> m_pipeline;

    // Same shaders and same layouts, blended and depth-write-off. A second
    // pipeline rather than dynamic state because blend and depthWrite are not
    // dynamic in core Vulkan 1.2 without EXT_extended_dynamic_state3.
    std::unique_ptr<VulkanPipeline> m_transparentPipeline;
    std::unique_ptr<VulkanPipeline> m_gridPipeline;

    // Depth-only pass from the primary directional light.
    std::unique_ptr<ShadowMap> m_shadowMap;
    std::unique_ptr<PointShadowMap> m_pointShadowMap;

    // One array image, a layer per shadow-casting spot light.
    std::unique_ptr<ShadowMap> m_spotShadowMap;

    // Rebuilt each frame by gatherLights, consumed by the cube shadow pass.
    mutable std::vector<PointShadowCaster> m_pointShadowCasters;
    mutable std::vector<SpotShadowCaster> m_spotShadowCasters;
    std::unique_ptr<PipelineCache> m_pipelineCache;

    // Every skinned entity's joint matrices for the frame, back to back. The
    // per-draw push constant holds an index into this rather than the matrices
    // themselves: 128 joints is 8 KB against the 128-byte push constant budget.
    static constexpr uint32_t kMaxPaletteMatrices = 1024;
    std::vector<std::unique_ptr<VulkanBuffer>> m_jointPaletteBuffers;

    // Reused between frames so the gather does not allocate every frame.
    std::vector<glm::mat4> m_paletteScratch;
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
