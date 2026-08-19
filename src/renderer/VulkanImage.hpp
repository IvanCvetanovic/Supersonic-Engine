#pragma once

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
        bool generateMipmaps = false
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

    static void CopyBufferToImage(
        VulkanDevice& device,
        vk::CommandPool commandPool,
        vk::Buffer buffer,
        vk::Image image,
        uint32_t width,
        uint32_t height
    );

private:
    VulkanDevice& m_deviceRef;
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    
    vk::Image m_image{nullptr};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    vk::ImageView m_imageView{nullptr};
    std::vector<vk::ImageView> m_layerViews;
    uint32_t m_arrayLayers{1};
    vk::SampleCountFlagBits m_samples{vk::SampleCountFlagBits::e1};
    vk::Sampler m_sampler{nullptr};

    uint32_t m_mipLevels{1};
    uint32_t m_width{0};
    uint32_t m_height{0};
    vk::Format m_format{vk::Format::eR8G8B8A8Srgb};
};

} // namespace Supersonic
