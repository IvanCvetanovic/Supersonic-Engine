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
    // `cache` is the renderer's persistent pipeline cache. Optional only so a
    // caller without one still compiles; passing it is what stops a viewport
    // drag recompiling three pipelines from source on every resize, while the
    // renderer's own cache - which exists, persists to disk and is validated
    // against the driver UUID - sat unused two frames away.
    BloomPass(VulkanDevice& device, uint32_t width, uint32_t height,
              vk::ImageView sceneView, vk::Sampler sceneSampler,
              vk::PipelineCache cache = nullptr);
    ~BloomPass();

    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    // Records the whole chain into an already-recording command buffer, after
    // the scene render pass has ended.
    void Record(vk::CommandBuffer commandBuffer) const;

    // The tone-mapped, sRGB-encoded result. This is what the editor shows.
    vk::ImageView GetOutputView() const { return m_outputImage->GetImageView(); }

    // The composited, tone-mapped, sRGB-encoded image - the one ImGui shows.
    // Exposed for --screenshot, which must capture what was actually displayed
    // rather than the linear HDR scene target behind it.
    vk::Image GetOutputImage() const { return m_outputImage->GetImage(); }
    vk::Sampler GetOutputSampler() const { return m_outputSampler; }

    // Scene colour is floating point so highlights can exceed 1.0; the output is
    // UNORM because it is already encoded and is blitted without conversion.
    static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;
    static constexpr vk::Format kOutputFormat = vk::Format::eR8G8B8A8Unorm;

    // The look, as values rather than as constants.
    //
    // These were compile-time constants with a comment calling that a choice -
    // "one look, chosen". It is a defensible position for an engine with one
    // scene and an indefensible one for an engine meant to host somebody's
    // game: a night level and a bright exterior do not share a bloom
    // threshold, and neither of them should require a rebuild.
    //
    // The shaders already took all four through push constants. Only the C++
    // side was fixed, so this changes what feeds them and nothing else.
    struct Settings {
        // Where a highlight starts to bloom. The scene is HDR at this point, so
        // 1.0 means "brighter than white".
        float threshold{1.0f};

        // How gradually it starts, so a surface drifting past the threshold
        // fades in instead of popping.
        float softKnee{0.5f};

        // How much of the blurred image is added back.
        float intensity{0.55f};

        // Applied in the composite, before the tone map.
        float exposure{1.0f};
    };

    void SetSettings(const Settings& settings) { m_settings = settings; }
    const Settings& GetSettings() const { return m_settings; }
    Settings& MutableSettings() { return m_settings; }

private:
    Settings m_settings{};

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

    vk::PipelineCache m_pipelineCache{nullptr};
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
