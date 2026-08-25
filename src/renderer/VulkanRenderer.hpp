#pragma once

#include <functional>
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
#include "core/ClusterGrid.hpp"
#include "renderer/ShadowCache.hpp"
#include "renderer/MeshRegistry.hpp"
#include "renderer/TextureRegistry.hpp"
#include "renderer/ShadowMap.hpp"
#include "renderer/EnvironmentProbe.hpp"
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

    // How the UI should look, applied at the one instant it can be.
    //
    // The renderer used to call EditorFonts::Load and Theme::ApplyEngineDarkTheme
    // itself, which meant a Vulkan renderer that could not be compiled without
    // the editor's fonts and colour scheme - a packaged game linked the editor's
    // appearance in order to draw a triangle.
    //
    // It arrives as a callback rather than as data because of the timing, which
    // is not negotiable: fonts must be added after the ImGui context exists and
    // BEFORE the Vulkan backend is initialised. Adding one afterwards destroys
    // and re-uploads a font texture the backend may already have recorded into
    // an unsubmitted command buffer. The renderer is the only thing that knows
    // when that moment is; it is not the thing that should know what a font is.
    //
    // The float is the monitor's DPI scale, which the renderer does know,
    // having just asked GLFW which monitor the window landed on. Empty is
    // valid and means ImGui's built-in style.
    using UiStyleCallback = std::function<void(float dpiScale)>;

    VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window,
                   UiStyleCallback styleUi = {});
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

    // Whether to draw the parts of the scene pass that exist for an editor and
    // for nothing else - today the infinite ground grid.
    //
    // A packaged game drew it. Not as an option anybody chose, but because the
    // draw sat at the end of the scene pass with nothing in front of it: a
    // shipped title opened on its own level with the editor's construction grid
    // blended over the horizon. Set once at startup from the manifest, because
    // that is when the answer is known and it never changes afterwards.
    void SetEditorOverlaysVisible(bool visible) { m_editorOverlays = visible; }
    bool EditorOverlaysVisible() const { return m_editorOverlays; }

    // The persistent, disk-backed, driver-UUID-validated pipeline cache. Handed
    // out so the editor's offscreen chain can use it too rather than compiling
    // its pipelines from SPIR-V on every viewport resize.
    vk::PipelineCache GetPipelineCache() const;

    // The renderer's transfer/graphics command pool, for one-off work like a
    // screenshot readback.
    vk::CommandPool GetCommandPool() const { return m_commandPool; }

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

    // Rewrites ONLY the two environment bindings on the sets that already
    // exist.
    //
    // Not createDescriptorSets again: the pool is sized for exactly one set per
    // frame in flight, so allocating a second round exhausts it - the first
    // scene that named an HDRI died on ErrorOutOfPoolMemory before it drew
    // anything. A descriptor set can be written repeatedly; it just cannot be
    // written while a command buffer using it is still running, which is why
    // this waits for the device first.
    void updateEnvironmentDescriptors();
    void initImGui();

    UiStyleCallback m_styleUi;

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
                           std::vector<GpuLight>& outLights,
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

    // The sky, drawn at the far plane after opaque geometry.
    std::unique_ptr<VulkanPipeline> m_skyPipeline;
    std::unique_ptr<VulkanPipeline> m_gridPipeline;

    // Depth-only pass from the primary directional light.
    std::unique_ptr<ShadowMap> m_shadowMap;
    std::unique_ptr<PointShadowMap> m_pointShadowMap;

    // The scene's surroundings, as the two cubemaps a shader lights from.
    // Always present so its descriptor slots are always written; whether the
    // shader looks at them is a flag in the scene block.
    // Slot 0 is the scene-wide environment; the rest are probes. Every slot
    // always holds a valid probe object, because every slot is always written
    // into the descriptor set - reading a descriptor nobody wrote is undefined
    // even inside a branch the shader never takes.
    std::array<std::unique_ptr<EnvironmentProbe>, VulkanPipeline::kMaxEnvironmentProbes>
        m_environments;

    // What the probe was last asked for, so a scene that names the same HDRI
    // every frame is not reconvolved every frame - that integral is the
    // expensive part of the whole feature.
    std::array<std::string, VulkanPipeline::kMaxEnvironmentProbes> m_environmentPaths;
    std::array<float, VulkanPipeline::kMaxEnvironmentProbes> m_environmentIntensities{};
    std::array<bool, VulkanPipeline::kMaxEnvironmentProbes> m_environmentHasMap{};

    // One array image, a layer per shadow-casting spot light.
    std::unique_ptr<ShadowMap> m_spotShadowMap;

    // Rebuilt each frame by gatherLights, consumed by the cube shadow pass.
    mutable std::vector<PointShadowCaster> m_pointShadowCasters;
    mutable std::vector<SpotShadowCaster> m_spotShadowCasters;

    // How many lights the scene had when the over-cap message was last logged,
    // so it is said once per change rather than sixty times a second. Zero
    // means the scene is within the cap and the next overflow should report.
    mutable size_t m_lightCapReportedFor{0};
    std::unique_ptr<PipelineCache> m_pipelineCache;

    // Every skinned entity's joint matrices for the frame, back to back. The
    // per-draw push constant holds an index into this rather than the matrices
    // themselves: 128 joints is 8 KB against the 128-byte push constant budget.
    static constexpr uint32_t kMaxPaletteMatrices = 1024;
    std::vector<std::unique_ptr<VulkanBuffer>> m_jointPaletteBuffers;

    // The clustered light data, one set per frame in flight: every light in
    // the frame, the per-froxel (offset, count) table, and the flat index list
    // that table points into. What used to be a fixed array of eight inside the
    // uniform block.
    std::vector<std::unique_ptr<VulkanBuffer>> m_lightBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_clusterRangeBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_lightIndexBuffers;

    // Rebuilt every frame and kept between them so a scene full of lamps does
    // not allocate three vectors per frame.
    std::vector<GpuLight> m_lightScratch;
    std::vector<ClusterGrid::LocalLight> m_localLightScratch;
    std::vector<uint32_t> m_lightIndexScratch;

    // Said once per change rather than once per frame, like the light cap.
    uint32_t m_clusterOverflowReportedFor{0};

    // Reused between frames so the gather does not allocate every frame.
    std::vector<glm::mat4> m_paletteScratch;
    RenderSystem::Stats m_renderStats{};

    // Rebuilt once per frame and read by all eighteen depth passes. A member
    // rather than a local so the storage is reused: it is the same handful of
    // entities every frame, and eighteen passes reading one vector should not
    // also mean one allocation per frame.
    std::vector<RenderSystem::ShadowCaster> m_shadowCasters;

    // True in the editor, false in a packaged game. See SetEditorOverlaysVisible.
    bool m_editorOverlays{true};

    // Which depth passes were recorded last frame and from what, so a pass
    // whose inputs have not changed is not recorded again.
    ShadowCache m_shadowCache;
    std::unique_ptr<VulkanPipeline> m_shadowPipeline;

    // The same depth pass for casters that occlude only where their albedo is
    // opaque. Separate because it needs a fragment stage that discards and a
    // cull mode that keeps both faces - neither of which a crate should pay
    // for, and eighteen depth passes a frame is where paying shows.
    std::unique_ptr<VulkanPipeline> m_shadowCutoutPipeline;

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
