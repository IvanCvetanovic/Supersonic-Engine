#pragma once

#include <memory>
#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"
#include "renderer/BloomPass.hpp"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

namespace Supersonic {

class VulkanOffscreen {
public:
    // Floating point, so highlights can exceed 1.0 and the bloom pass has
    // something to threshold. Tone mapping and the sRGB encode moved out of
    // shader.frag and into BloomPass's composite, which is the only place
    // either now happens - see BloomPass for why that order is required.
    static constexpr vk::Format kColorFormat = BloomPass::kHdrFormat;

    VulkanOffscreen(VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanOffscreen();

    // The renderer's persistent pipeline cache, handed down so the bloom chain
    // stops recompiling its pipelines from SPIR-V on every viewport resize -
    // while that cache, which persists to disk and is validated against the
    // driver UUID, sat unused two frames away. Set before the first resize; a
    // null cache is legal and merely slow.
    void SetPipelineCache(vk::PipelineCache cache) { m_pipelineCache = cache; }

    VulkanOffscreen(const VulkanOffscreen&) = delete;
    VulkanOffscreen& operator=(const VulkanOffscreen&) = delete;

    // Records a desired size. Safe to call at any point in the frame, including
    // while ImGui is being built.
    void RequestResize(uint32_t width, uint32_t height);

    // Performs any pending resize. MUST only be called at the top of a frame,
    // before command buffer recording begins. Returns true if it resized.
    bool ApplyPendingResize();

    vk::RenderPass GetRenderPass() const { return m_renderPass; }

    // Pipelines drawing into this pass must declare the same count.
    vk::SampleCountFlagBits GetSampleCount() const { return m_samples; }

    // Runs the bloom chain and the tone map. Called after the scene render pass
    // has ended, on the same command buffer.
    void RecordPostProcess(vk::CommandBuffer commandBuffer) const;
    vk::Framebuffer GetFramebuffer() const { return m_framebuffer; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    ImTextureID GetTextureID() const { return m_textureID; }

    // The composited image, for --screenshot. Null before the first resize.
    vk::Image GetPresentedImage() const;

private:
    void createRenderPass();
    void createResources();
    void createFramebuffer();
    void createSamplerAndTextureID();
    void createBloom();
    void cleanup();

    VulkanDevice& m_deviceRef;
    uint32_t m_width{1280};
    uint32_t m_height{720};
    uint32_t m_pendingWidth{0};
    uint32_t m_pendingHeight{0};

    vk::RenderPass m_renderPass{nullptr};

    // Multisampled colour and depth are what the scene rasterises into;
    // m_resolveImage is the single-sample copy the render pass resolves to and
    // the only one that can be sampled.
    vk::SampleCountFlagBits m_samples{vk::SampleCountFlagBits::e1};
    std::unique_ptr<VulkanImage> m_colorImage;
    std::unique_ptr<VulkanImage> m_depthImage;
    std::unique_ptr<VulkanImage> m_resolveImage;

    // Rebuilt with the target, because every image in the chain is sized to it.
    vk::PipelineCache m_pipelineCache{nullptr};
    std::unique_ptr<BloomPass> m_bloom;
    vk::Framebuffer m_framebuffer{nullptr};

    vk::Sampler m_sampler{nullptr};
    ImTextureID m_textureID{0};
};

} // namespace Supersonic
