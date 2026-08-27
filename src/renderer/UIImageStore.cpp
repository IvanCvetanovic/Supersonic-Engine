#include "renderer/UIImageStore.hpp"

#include "core/Log.hpp"
#include "renderer/VulkanBuffer.hpp"
#include "renderer/VulkanDevice.hpp"

#include "backends/imgui_impl_vulkan.h"

namespace Supersonic {

UIImageStore::UIImageStore(VulkanDevice& device, vk::CommandPool commandPool)
    : m_device(device), m_commandPool(commandPool) {}

UIImageStore::~UIImageStore() = default;

uint64_t UIImageStore::Create(const uint8_t* pixels, uint32_t width, uint32_t height) {
    if (pixels == nullptr || width == 0 || height == 0) return 0;

    const vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * 4u;

    VulkanBuffer staging(m_device.GetAllocator(), size, vk::BufferUsageFlagBits::eTransferSrc,
                         VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(pixels, size);

    Entry entry;
    entry.width = width;
    entry.height = height;

    // UNORM rather than sRGB. ImGui composites the UI with no colour
    // conversion, so a texture the hardware decoded to linear arrives washed
    // out - the same decision the editor's thumbnail cache records.
    entry.image = std::make_unique<VulkanImage>(m_device, width, height,
                                                vk::Format::eR8G8B8A8Unorm);
    entry.image->CreateSampler(vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge);

    VulkanImage::TransitionLayout(m_device, m_commandPool, entry.image->GetImage(),
                                  vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(m_device, m_commandPool, staging.GetBuffer(),
                                   entry.image->GetImage(), width, height);
    VulkanImage::TransitionLayout(m_device, m_commandPool, entry.image->GetImage(),
                                  vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);

    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(entry.image->GetSampler()),
        static_cast<VkImageView>(entry.image->GetImageView()),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (set == VK_NULL_HANDLE) {
        SUPERSONIC_LOG_ERROR("UIImageStore")
            << "ImGui refused a descriptor for a " << width << "x" << height << " image."
            << std::endl;
        return 0;
    }

    const uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(set));
    entry.handle = handle;
    m_images.emplace(handle, std::move(entry));
    return handle;
}

bool UIImageStore::Update(uint64_t handle, const uint8_t* pixels, uint32_t width,
                          uint32_t height) {
    const auto it = m_images.find(handle);
    if (it == m_images.end() || pixels == nullptr) return false;

    // A resize is a NEW image, refused rather than silently reallocated: the
    // descriptor ImGui already holds describes the old extent, and replacing
    // the image under it would draw the new pixels through the old view.
    if (it->second.width != width || it->second.height != height) return false;

    const vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * 4u;

    VulkanBuffer staging(m_device.GetAllocator(), size, vk::BufferUsageFlagBits::eTransferSrc,
                         VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(pixels, size);

    // Back to transfer-destination and then to shader-read again. Both
    // transitions submit and wait, which is what makes this safe without
    // tracking frames in flight - and what makes it the wrong call for a
    // texture updated every frame.
    VulkanImage::TransitionLayout(m_device, m_commandPool, it->second.image->GetImage(),
                                  vk::ImageLayout::eShaderReadOnlyOptimal,
                                  vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(m_device, m_commandPool, staging.GetBuffer(),
                                   it->second.image->GetImage(), width, height);
    VulkanImage::TransitionLayout(m_device, m_commandPool, it->second.image->GetImage(),
                                  vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);
    return true;
}

void UIImageStore::Destroy(uint64_t handle) {
    const auto it = m_images.find(handle);
    if (it == m_images.end()) return;

    ImGui_ImplVulkan_RemoveTexture(
        reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(handle)));
    m_images.erase(it);
}

} // namespace Supersonic
