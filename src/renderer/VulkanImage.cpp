#include "renderer/VulkanImage.hpp"

#include <iostream>
#include <stdexcept>

namespace Engine {

VulkanImage::VulkanImage(
    VulkanDevice& device,
    uint32_t width,
    uint32_t height,
    vk::Format format,
    vk::ImageUsageFlags usage,
    vk::ImageAspectFlags aspectFlags)
    : m_deviceRef(device), m_allocator(device.GetAllocator()), m_width(width), m_height(height), m_format(format) {

    if (!m_allocator) {
        throw std::runtime_error("Cannot create VulkanImage with null VmaAllocator!");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = static_cast<VkFormat>(m_format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = static_cast<VkImageUsageFlags>(usage);
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage rawImage = VK_NULL_HANDLE;
    VkResult result = vmaCreateImage(
        m_allocator,
        &imageInfo,
        &allocInfo,
        &rawImage,
        &m_allocation,
        nullptr
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate 2D Image via VMA! VkResult: " + std::to_string(static_cast<int>(result)));
    }

    m_image = rawImage;

    // Create Image View
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = m_image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    m_imageView = m_deviceRef.GetDevice().createImageView(viewInfo);
}

VulkanImage::~VulkanImage() {
    vk::Device device = m_deviceRef.GetDevice();

    if (m_sampler) {
        device.destroySampler(m_sampler);
        m_sampler = nullptr;
    }

    if (m_imageView) {
        device.destroyImageView(m_imageView);
        m_imageView = nullptr;
    }

    if (m_image && m_allocation && m_allocator) {
        vmaDestroyImage(m_allocator, static_cast<VkImage>(m_image), m_allocation);
        m_image = nullptr;
        m_allocation = VK_NULL_HANDLE;
    }
}

void VulkanImage::CreateSampler(vk::Filter filter, vk::SamplerAddressMode addressMode) {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

    m_sampler = m_deviceRef.GetDevice().createSampler(samplerInfo);
}

void VulkanImage::TransitionLayout(
    VulkanDevice& device,
    vk::CommandPool commandPool,
    vk::Image image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout) {

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = device.GetDevice().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    commandBuffer.pipelineBarrier(
        sourceStage, destinationStage,
        vk::DependencyFlags(),
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vk::Result submitRes = device.GetGraphicsQueue().submit(1, &submitInfo, nullptr);
    if (submitRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to submit image layout transition command buffer!");
    }

    device.GetGraphicsQueue().waitIdle();
    device.GetDevice().freeCommandBuffers(commandPool, 1, &commandBuffer);
}

void VulkanImage::CopyBufferToImage(
    VulkanDevice& device,
    vk::CommandPool commandPool,
    vk::Buffer buffer,
    vk::Image image,
    uint32_t width,
    uint32_t height) {

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = device.GetDevice().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);
    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vk::Result submitRes = device.GetGraphicsQueue().submit(1, &submitInfo, nullptr);
    if (submitRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to submit buffer-to-image copy command buffer!");
    }

    device.GetGraphicsQueue().waitIdle();
    device.GetDevice().freeCommandBuffers(commandPool, 1, &commandBuffer);
}

} // namespace Engine
