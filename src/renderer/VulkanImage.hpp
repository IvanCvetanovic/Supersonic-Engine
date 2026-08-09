#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "renderer/VulkanDevice.hpp"

namespace Engine {

class VulkanImage {
public:
    VulkanImage(
        VulkanDevice& device,
        uint32_t width,
        uint32_t height,
        vk::Format format = vk::Format::eR8G8B8A8Srgb,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor
    );
    ~VulkanImage();

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

    vk::Image GetImage() const { return m_image; }
    vk::ImageView GetImageView() const { return m_imageView; }
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
    vk::Sampler m_sampler{nullptr};

    uint32_t m_width{0};
    uint32_t m_height{0};
    vk::Format m_format{vk::Format::eR8G8B8A8Srgb};
};

} // namespace Engine
