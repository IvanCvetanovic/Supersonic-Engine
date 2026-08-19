#include "renderer/VulkanImage.hpp"

#include <iostream>
#include <stdexcept>

namespace Supersonic {

VulkanImage::VulkanImage(
    VulkanDevice& device,
    uint32_t width,
    uint32_t height,
    vk::Format format,
    vk::ImageUsageFlags usage,
    vk::ImageAspectFlags aspectFlags,
    uint32_t arrayLayers,
    vk::SampleCountFlagBits samples,
    bool cubeCompatible,
    bool generateMipmaps)
    : m_deviceRef(device), m_allocator(device.GetAllocator()), m_width(width), m_height(height), m_format(format) {

    m_arrayLayers = arrayLayers == 0 ? 1 : arrayLayers;
    m_samples = samples;

    // floor(log2(max(w, h))) + 1 levels, i.e. down to 1x1.
    //
    // Multisampled images are excluded outright: a mip chain on one is invalid,
    // and such an image is resolved rather than sampled anyway.
    if (generateMipmaps && m_samples == vk::SampleCountFlagBits::e1) {
        uint32_t largest = width > height ? width : height;
        while (largest > 1) { largest >>= 1; ++m_mipLevels; }
    }

    // A cube is six faces, no more and no less. Asking for a cube with any
    // other layer count is a caller bug, and creating a plain array instead
    // would fail later at descriptor binding with a message about view types
    // that says nothing about why.
    if (cubeCompatible && m_arrayLayers != 6) {
        throw std::runtime_error("A cube-compatible image needs exactly 6 layers, got " +
                                 std::to_string(m_arrayLayers));
    }

    if (!m_allocator) {
        throw std::runtime_error("Cannot create VulkanImage with null VmaAllocator!");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = m_mipLevels;
    imageInfo.arrayLayers = m_arrayLayers;
    imageInfo.format = static_cast<VkFormat>(m_format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Building the chain blits level N-1 into level N, so the image is both a
    // transfer source and a destination. Requested here rather than by the
    // caller: forgetting it produces a validation error at blit time, a long
    // way from the create call that caused it.
    if (m_mipLevels > 1) {
        usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    }
    imageInfo.usage = static_cast<VkImageUsageFlags>(usage);
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(m_samples);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (cubeCompatible) imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

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
    viewInfo.viewType = cubeCompatible ? vk::ImageViewType::eCube
                      : (m_arrayLayers > 1 ? vk::ImageViewType::e2DArray
                                           : vk::ImageViewType::e2D);
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_arrayLayers;

    m_imageView = m_deviceRef.GetDevice().createImageView(viewInfo);

    // Per-layer views for framebuffer attachments. Only worth creating for an
    // array image; a single-layer image's own view already is one.
    if (m_arrayLayers > 1) {
        m_layerViews.reserve(m_arrayLayers);
        for (uint32_t layer = 0; layer < m_arrayLayers; ++layer) {
            vk::ImageViewCreateInfo layerInfo = viewInfo;
            layerInfo.viewType = vk::ImageViewType::e2D;
            layerInfo.subresourceRange.baseArrayLayer = layer;
            layerInfo.subresourceRange.layerCount = 1;
            m_layerViews.push_back(m_deviceRef.GetDevice().createImageView(layerInfo));
        }
    }
}

VulkanImage::~VulkanImage() {
    vk::Device device = m_deviceRef.GetDevice();

    if (m_sampler) {
        device.destroySampler(m_sampler);
        m_sampler = nullptr;
    }

    for (auto view : m_layerViews) {
        if (view) device.destroyImageView(view);
    }
    m_layerViews.clear();

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
    // Both were hardcoded off. Anisotropy is the filtering that matters at a
    // glancing angle, which is exactly where a mip chain alone goes blurry -
    // the two are complementary, not alternatives.
    const bool anisotropy = m_deviceRef.SupportsAnisotropy();
    samplerInfo.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = anisotropy ? m_deviceRef.MaxAnisotropy() : 1.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    // maxLod defaults to 0, which pins sampling to the base level and makes a
    // mip chain that exists but is never read.
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);

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
    // VK_REMAINING_MIP_LEVELS: this helper is used on images with a chain now,
    // and transitioning only level 0 would leave the rest in eUndefined - which
    // a sampler reading them treats as containing anything at all.
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
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

bool VulkanImage::GenerateMipmaps(
    VulkanDevice& device,
    vk::CommandPool commandPool,
    vk::Image image,
    vk::Format format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels) {

    // Blitting with eLinear against a format whose optimal tiling does not
    // support linear filtering is invalid usage, not a quality compromise. The
    // caller is told so it can decide; the levels are still transitioned, so
    // the image remains samplable with only level 0 populated rather than left
    // in eUndefined.
    const vk::FormatProperties properties =
        device.GetPhysicalDevice().getFormatProperties(format);
    const bool canFilter = static_cast<bool>(
        properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    vk::CommandBuffer cmd = device.GetDevice().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    // Every level arrives in eTransferDstOptimal, because the whole image was
    // transitioned there before the buffer copy that filled level 0.
    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    const uint32_t levels = canFilter ? mipLevels : 1;

    for (uint32_t level = 1; level < levels; ++level) {
        // Level-1 becomes a transfer source, and waits for the write that
        // produced it - the buffer copy for level 0, the previous blit after.
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eTransfer,
                            vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrier);

        // Halve, but never below 1: a 512x8 texture reaches height 1 while its
        // width is still 64, and a zero extent is an invalid blit.
        const int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        const int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        vk::ImageBlit blit{};
        blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1] = vk::Offset3D{nextWidth, nextHeight, 1};
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
                      image, vk::ImageLayout::eTransferDstOptimal,
                      1, &blit, vk::Filter::eLinear);

        // And straight to shader-read, since nothing else will touch it.
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // The last level was never a blit source, so it is still eTransferDst.
    // When the format cannot be filtered this covers every level from 0 up,
    // which is what leaves the image samplable rather than undefined.
    barrier.subresourceRange.baseMipLevel = levels - 1;
    barrier.subresourceRange.levelCount = canFilter ? 1 : VK_REMAINING_MIP_LEVELS;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrier);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    const vk::Result submitRes = device.GetGraphicsQueue().submit(1, &submitInfo, nullptr);
    if (submitRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to submit mipmap generation command buffer!");
    }
    device.GetGraphicsQueue().waitIdle();
    device.GetDevice().freeCommandBuffers(commandPool, 1, &cmd);

    return canFilter;
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

} // namespace Supersonic
