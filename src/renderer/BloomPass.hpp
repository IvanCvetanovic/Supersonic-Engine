#pragma once

#include <array>
#include <memory>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"

namespace Supersonic {

// Bloom, and the tone map that has to follow it.
//
// The chain is: threshold the scene into a half-resolution image, blur that
// separably twice, then add it back and tone map. Half resolution because a
// blur is a low-frequency effect - running it at full resolution costs four
// times the bandwidth to produce an image nobody can tell apart.
//
// This pass also owns the final tone map and sRGB encode. They used to live at
// the end of shader.frag, which was right while that shader produced the image
// on screen; with a floating-point scene target feeding this chain, tone mapping
// before the bright pass would flatten exactly the highlights it looks for.
class BloomPass {
public:
    // sceneView/sceneSampler are the resolved scene image. They are sampled, so
    // they must already be in eShaderReadOnlyOptimal when Record runs.
    BloomPass(VulkanDevice& device, uint32_t width, uint32_t height,
              vk::ImageView sceneView, vk::Sampler sceneSampler);
    ~BloomPass();

    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    // Records the whole chain into an already-recording command buffer, after
    // the scene render pass has ended.
    void Record(vk::CommandBuffer commandBuffer) const;

    // The tone-mapped, sRGB-encoded result. This is what the editor shows.
    vk::ImageView GetOutputView() const { return m_outputImage->GetImageView(); }
    vk::Sampler GetOutputSampler() const { return m_outputSampler; }

    // Scene colour is floating point so highlights can exceed 1.0; the output is
    // UNORM because it is already encoded and is blitted without conversion.
    static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;
    static constexpr vk::Format kOutputFormat = vk::Format::eR8G8B8A8Unorm;

    // Tunables, deliberately constants rather than settings: one look, chosen.
    static constexpr float kThreshold = 1.0f;
    static constexpr float kSoftKnee = 0.5f;
    static constexpr float kIntensity = 0.55f;
    static constexpr float kExposure = 1.0f;

private:
    // 16 bytes, well inside the guaranteed push constant range. Every stage in
    // the chain takes four floats and interprets them for itself.
    struct Params {
        glm::vec4 value{0.0f};
    };

    void createRenderPasses();
    void createImages();
    void createFramebuffers();
    void createDescriptors(vk::ImageView sceneView, vk::Sampler sceneSampler);
    void createPipelines();

    vk::Pipeline buildPipeline(const std::string& fragmentPath, vk::RenderPass pass,
                               vk::PipelineLayout layout) const;
    void recordPass(vk::CommandBuffer cmd, vk::RenderPass pass, vk::Framebuffer framebuffer,
                    uint32_t width, uint32_t height, vk::Pipeline pipeline,
                    vk::PipelineLayout layout, vk::DescriptorSet set, const Params& params) const;

    VulkanDevice& m_deviceRef;
    uint32_t m_width{1};
    uint32_t m_height{1};
    uint32_t m_halfWidth{1};
    uint32_t m_halfHeight{1};

    vk::RenderPass m_hdrPass{nullptr};     // half-res float targets
    vk::RenderPass m_outputPass{nullptr};  // full-res encoded target

    std::unique_ptr<VulkanImage> m_brightImage;
    std::unique_ptr<VulkanImage> m_blurImage;
    std::unique_ptr<VulkanImage> m_outputImage;

    vk::Framebuffer m_brightFramebuffer{nullptr};
    vk::Framebuffer m_blurFramebuffer{nullptr};
    vk::Framebuffer m_outputFramebuffer{nullptr};

    vk::Sampler m_sampler{nullptr};        // for the intermediate images
    vk::Sampler m_outputSampler{nullptr};

    vk::DescriptorPool m_descriptorPool{nullptr};
    vk::DescriptorSetLayout m_singleLayout{nullptr};
    vk::DescriptorSetLayout m_doubleLayout{nullptr};
    vk::PipelineLayout m_singlePipelineLayout{nullptr};
    vk::PipelineLayout m_doublePipelineLayout{nullptr};

    vk::DescriptorSet m_sceneSet{nullptr};      // scene      -> bright
    vk::DescriptorSet m_brightSet{nullptr};     // bright     -> blur
    vk::DescriptorSet m_blurSet{nullptr};       // blur       -> bright
    vk::DescriptorSet m_compositeSet{nullptr};  // scene+bloom-> output

    vk::Pipeline m_brightPipeline{nullptr};
    vk::Pipeline m_blurPipeline{nullptr};
    vk::Pipeline m_compositePipeline{nullptr};
};

} // namespace Supersonic
