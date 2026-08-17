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
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
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

    uint32_t m_width{0};
    uint32_t m_height{0};
    vk::Format m_format{vk::Format::eR8G8B8A8Srgb};
};

} // namespace Supersonic
