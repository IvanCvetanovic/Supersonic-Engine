#include "renderer/ScreenCapture.hpp"

#include "renderer/VulkanBuffer.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstring>
#include <vector>

namespace Supersonic::ScreenCapture {

bool WritePng(VulkanDevice& device, vk::CommandPool commandPool, vk::Image image,
              uint32_t width, uint32_t height, const std::string& path,
              std::string& outError) {
    if (!image) {
        outError = "no image to capture (has a frame been rendered yet?)";
        return false;
    }
    if (width == 0 || height == 0) {
        outError = "capture size is zero";
        return false;
    }

    const vk::DeviceSize byteCount = static_cast<vk::DeviceSize>(width) * height * 4;

    // Host-visible destination. VulkanBuffer already knows how to make one of
    // these for staging uploads; this is the same thing in the other direction.
    VulkanBuffer staging(device.GetAllocator(), byteCount,
                         vk::BufferUsageFlagBits::eTransferDst,
                         VMA_MEMORY_USAGE_CPU_ONLY,
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    vk::CommandBuffer cmd = device.GetDevice().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    // The composite leaves its output in eShaderReadOnlyOptimal, because ImGui
    // samples it. Copying from an image requires eTransferSrcOptimal, so it
    // goes there and back - back, because the next frame's ImGui pass still
    // expects to sample it.
    const auto barrier = [&](vk::ImageLayout from, vk::ImageLayout to,
                             vk::AccessFlags srcAccess, vk::AccessFlags dstAccess,
                             vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage) {
        vk::ImageMemoryBarrier b{};
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = 1;
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        cmd.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferRead,
            vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer);

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;    // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal,
                          staging.GetBuffer(), 1, &region);

    barrier(vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead,
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    if (device.GetGraphicsQueue().submit(1, &submitInfo, nullptr) != vk::Result::eSuccess) {
        device.GetDevice().freeCommandBuffers(commandPool, 1, &cmd);
        outError = "failed to submit the capture command buffer";
        return false;
    }
    device.GetGraphicsQueue().waitIdle();
    device.GetDevice().freeCommandBuffers(commandPool, 1, &cmd);

    void* mapped = nullptr;
    staging.Map(&mapped);
    if (!mapped) {
        outError = "could not map the capture buffer";
        return false;
    }

    // The image is R8G8B8A8Unorm and already sRGB-encoded by the composite, so
    // the bytes go to the PNG verbatim. Re-encoding here would be the second
    // encode this whole colour pipeline is arranged to avoid.
    std::vector<uint8_t> pixels(static_cast<size_t>(byteCount));
    std::memcpy(pixels.data(), mapped, pixels.size());
    staging.Unmap();

    // Fully opaque. The composite writes alpha 1.0, but a surprise here would
    // produce a PNG that looks empty in some viewers and fine in others.
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;

    return WriteRgbaPng(pixels.data(), width, height, path, outError);
}

bool WriteRgbaPng(const uint8_t* rgba, uint32_t width, uint32_t height,
                  const std::string& path, std::string& outError) {
    if (!rgba || width == 0 || height == 0) {
        outError = "no pixels to write";
        return false;
    }
    const int written = stbi_write_png(path.c_str(), static_cast<int>(width),
                                       static_cast<int>(height), 4, rgba,
                                       static_cast<int>(width) * 4);
    if (!written) {
        outError = "stbi_write_png failed for " + path;
        return false;
    }
    return true;
}

} // namespace Supersonic::ScreenCapture
