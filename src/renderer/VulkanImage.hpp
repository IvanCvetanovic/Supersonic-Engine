#pragma once

#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

class VulkanImage {
public:
    VulkanImage(
        VulkanDevice& device,
        uint32_t width,
        uint32_t height,
        vk::Format format = vk::Format::eR8G8B8A8Srgb,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,

        // More than one layer makes this a 2D array image, and GetImageView()
        // returns a 2D_ARRAY view. Needed by cascaded shadow maps: sampling N
        // cascades through one array view avoids indexing an array of
        // descriptors, which requires a dynamically-uniform index and therefore
        // cannot be driven per fragment.
        uint32_t arrayLayers = 1,

        // More than one sample makes this a multisample attachment. Such an
        // image can never be sampled in a shader or copied from - it is
        // resolved into a single-sample image by the render pass instead.
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,

        // Makes this a cube map: exactly six layers, created with the
        // cube-compatible flag, and GetImageView() returns a CUBE view. Six
        // layers on their own are just an array - the sampler will not do the
        // direction-to-face selection without this, which is the entire point
        // of using a cube for an omnidirectional shadow.
        bool cubeCompatible = false,

        // Allocate a full mip chain and sample through it.
        //
        // Every texture in the engine was created with mipLevels = 1, so a
        // floor viewed at a glancing angle sampled full-resolution texels that
        // fell far below one pixel each, and shimmered as the camera moved.
        // MSAA does not help: it anti-aliases geometry edges, not texture
        // minification.
        //
        // The image gains eTransferSrc as well as eTransferDst, because the
        // chain is built by blitting each level from the one above it.
        bool generateMipmaps = false,

        // A mip count the CALLER decides, for a chain that is computed rather
        // than filtered.
        //
        // generateMipmaps builds each level by blitting the one above it, which
        // is right for a texture and wrong for a prefiltered environment: there
        // each level is its own integral over a different roughness, not a box
        // filter of the sharper one. Zero keeps the existing behaviour, so
        // every caller that had none is unchanged.
        uint32_t explicitMipLevels = 0
    );
    ~VulkanImage();

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

    vk::Image GetImage() const { return m_image; }
    vk::ImageView GetImageView() const { return m_imageView; }

    // Single-layer view, for use as a framebuffer attachment: a render pass
    // instance writes one layer, while sampling reads the whole array.
    vk::ImageView GetLayerView(uint32_t layer) const {
        return layer < m_layerViews.size() ? m_layerViews[layer] : m_imageView;
    }
    uint32_t GetArrayLayers() const { return m_arrayLayers; }
    vk::Sampler GetSampler() const { return m_sampler; }

    // The wrap GetSampler() reads with, as CreateSampler was given it.
    vk::SamplerAddressMode GetAddressMode() const { return m_addressMode; }

    // This image read with another wrap: GetSampler() itself when `addressMode`
    // is the one it was created with, and otherwise a second sampler identical
    // to it but for the wrap - filter, anisotropy and mip range copied - made
    // on the first request and destroyed with the image.
    //
    // One image, two ways to read its edge, and no second upload. The wrap
    // cannot belong to the image alone: TextureRegistry keeps one image per
    // path, and a file drawn once, edge to edge, as a 2D sprite wants its edge
    // clamped, while the same file tiled across a floor wants it repeated
    // (MaterialComponent::clampToEdge).
    //
    // Null before CreateSampler.
    vk::Sampler GetSampler(vk::SamplerAddressMode addressMode);

    uint32_t GetMipLevels() const { return m_mipLevels; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    void CreateSampler(vk::Filter filter = vk::Filter::eLinear, vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat);

    static void TransitionLayout(
        VulkanDevice& device,
        vk::CommandPool commandPool,
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout
    );

    // Builds levels 1..n by successive linear blits and leaves EVERY level in
    // eShaderReadOnlyOptimal, so this replaces the post-copy transition rather
    // than following it.
    //
    // Returns false when the format cannot be linearly filtered, in which case
    // it leaves the image with its levels transitioned but only level 0
    // populated - blitting with eLinear against a format that does not support
    // it is invalid usage, not merely lower quality. This is the same check
    // ShadowMap already makes before choosing its depth format.
    static bool GenerateMipmaps(
        VulkanDevice& device,
        vk::CommandPool commandPool,
        vk::Image image,
        vk::Format format,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels
    );

    // Fills every layer and every mip of an image from one staging buffer, in
    // one command buffer.
    //
    // The two helpers below take neither a layer nor a level - they were
    // written for a single 2D texture, and CopyBufferToImage hardcodes layer
    // zero. A cubemap is six layers with a mip chain that is COMPUTED rather
    // than blitted (each roughness is its own integral, not a box filter of the
    // one above), so it needs both, and generalising the existing pair would
    // change code that four other callers depend on.
    //
    // Transitions the whole image in and out, so the caller hands over an image
    // in eUndefined and gets one in eShaderReadOnlyOptimal.
    static void UploadLayeredImage(
        VulkanDevice& device,
        vk::CommandPool commandPool,
        vk::Buffer staging,
        vk::Image image,
        uint32_t layerCount,
        uint32_t mipLevels,
        const std::vector<vk::BufferImageCopy>& regions
    );

    static void CopyBufferToImage(
        VulkanDevice& device,
        vk::CommandPool commandPool,
        vk::Buffer buffer,
        vk::Image image,
        uint32_t width,
        uint32_t height
    );

private:
    // The one place a sampler is described, so the other-wrap samplers
    // GetSampler(addressMode) makes differ from the image's own in the wrap
    // and nothing else.
    vk::Sampler makeSampler(vk::Filter filter, vk::SamplerAddressMode addressMode) const;

    VulkanDevice& m_deviceRef;
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    
    vk::Image m_image{nullptr};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    vk::ImageView m_imageView{nullptr};
    std::vector<vk::ImageView> m_layerViews;
    uint32_t m_arrayLayers{1};
    vk::SampleCountFlagBits m_samples{vk::SampleCountFlagBits::e1};
    vk::Sampler m_sampler{nullptr};
    vk::Filter m_filter{vk::Filter::eLinear};
    vk::SamplerAddressMode m_addressMode{vk::SamplerAddressMode::eRepeat};

    // Made by GetSampler(addressMode), at most one per wrap. A vector rather
    // than a map: there are five wraps, and an image asked for any but its own
    // is asked for one.
    std::vector<std::pair<vk::SamplerAddressMode, vk::Sampler>> m_otherWraps;

    uint32_t m_mipLevels{1};
    uint32_t m_width{0};
    uint32_t m_height{0};
    vk::Format m_format{vk::Format::eR8G8B8A8Srgb};
};

} // namespace Supersonic
