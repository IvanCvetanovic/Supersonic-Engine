#include "editor/ThumbnailCache.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "backends/imgui_impl_vulkan.h"
#include "renderer/VulkanBuffer.hpp"

// The implementation lives in TextureRegistry.cpp; this only needs the
// declarations.
#include <stb_image.h>

namespace Supersonic {

namespace {

// Box-filter downscale to the thumbnail size.
//
// The alternative is uploading the source image at full resolution, which for a
// folder of 2K textures is hundreds of megabytes of VRAM to draw tiles the size
// of a postage stamp.
std::vector<uint8_t> downscale(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                               uint32_t dstW, uint32_t dstH) {
    std::vector<uint8_t> out(static_cast<size_t>(dstW) * dstH * 4);

    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t y0 = (y * srcH) / dstH;
        const uint32_t y1 = std::max(y0 + 1, ((y + 1) * srcH) / dstH);

        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t x0 = (x * srcW) / dstW;
            const uint32_t x1 = std::max(x0 + 1, ((x + 1) * srcW) / dstW);

            uint32_t accum[4] = {0, 0, 0, 0};
            uint32_t samples = 0;
            for (uint32_t sy = y0; sy < y1 && sy < srcH; ++sy) {
                for (uint32_t sx = x0; sx < x1 && sx < srcW; ++sx) {
                    const size_t i = (static_cast<size_t>(sy) * srcW + sx) * 4;
                    for (int c = 0; c < 4; ++c) accum[c] += src[i + static_cast<size_t>(c)];
                    ++samples;
                }
            }
            if (samples == 0) samples = 1;

            const size_t o = (static_cast<size_t>(y) * dstW + x) * 4;
            for (int c = 0; c < 4; ++c) {
                out[o + static_cast<size_t>(c)] = static_cast<uint8_t>(accum[c] / samples);
            }
        }
    }
    return out;
}

} // namespace

ThumbnailCache::ThumbnailCache(VulkanDevice& device) : m_deviceRef(device) {
    vk::CommandPoolCreateInfo info{};
    info.queueFamilyIndex = m_deviceRef.GetQueueFamilyIndices().graphicsFamily.value();
    // Every buffer from this pool is one-shot: allocate, upload, free.
    info.flags = vk::CommandPoolCreateFlagBits::eTransient;
    m_commandPool = m_deviceRef.GetDevice().createCommandPool(info);
}

ThumbnailCache::~ThumbnailCache() {
    // The descriptor sets belong to ImGui's pool, so they are released through
    // ImGui rather than destroyed here. This has to happen before
    // ImGui_ImplVulkan_Shutdown, which is why the editor tears down first.
    for (auto& [path, entry] : m_entries) {
        if (entry.id != 0) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(entry.id));
            entry.id = 0;
        }
    }
    m_entries.clear();

    if (m_commandPool) {
        m_deviceRef.GetDevice().destroyCommandPool(m_commandPool);
        m_commandPool = nullptr;
    }
}

bool ThumbnailCache::IsImage(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
}

ImTextureID ThumbnailCache::upload(const std::vector<uint8_t>& rgba, uint32_t width,
                                   uint32_t height, Entry& entry) {
    const vk::DeviceSize size = static_cast<vk::DeviceSize>(rgba.size());

    VulkanBuffer staging(m_deviceRef.GetAllocator(), size,
                         vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(rgba.data(), size);

    // UNORM, not sRGB: ImGui draws this without any colour conversion, so
    // letting the hardware decode to linear here would wash every thumbnail out.
    entry.image = std::make_unique<VulkanImage>(m_deviceRef, width, height,
                                                vk::Format::eR8G8B8A8Unorm);
    entry.image->CreateSampler(vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge);

    VulkanImage::TransitionLayout(m_deviceRef, m_commandPool, entry.image->GetImage(),
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(m_deviceRef, m_commandPool, staging.GetBuffer(),
                                   entry.image->GetImage(), width, height);
    VulkanImage::TransitionLayout(m_deviceRef, m_commandPool, entry.image->GetImage(),
                                  vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);

    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(entry.image->GetSampler()),
        static_cast<VkImageView>(entry.image->GetImageView()),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return reinterpret_cast<ImTextureID>(set);
}

ImTextureID ThumbnailCache::Get(const std::filesystem::path& path) {
    if (!IsImage(path)) return 0;

    const std::string key = path.string();
    if (const auto it = m_entries.find(key); it != m_entries.end()) {
        return it->second.id;
    }

    if (m_entries.size() >= kMaxThumbnails) return 0;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(key.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        // Remembered as a miss, so a file that only looks like an image is not
        // decoded again on every frame the browser is open.
        m_entries.emplace(key, Entry{});
        return 0;
    }

    const auto srcW = static_cast<uint32_t>(width);
    const auto srcH = static_cast<uint32_t>(height);

    // Preserve aspect: a stretched thumbnail is worse than a small one.
    uint32_t dstW = kThumbnailSize;
    uint32_t dstH = kThumbnailSize;
    if (srcW > srcH) {
        dstH = std::max(1u, (kThumbnailSize * srcH) / srcW);
    } else if (srcH > srcW) {
        dstW = std::max(1u, (kThumbnailSize * srcW) / srcH);
    }

    const std::vector<uint8_t> scaled = downscale(pixels, srcW, srcH, dstW, dstH);
    stbi_image_free(pixels);

    Entry entry;
    try {
        entry.id = upload(scaled, dstW, dstH, entry);
    } catch (const std::exception& e) {
        SUPERSONIC_LOG_ERROR("ThumbnailCache") << "Could not upload " << key << ": " << e.what() << std::endl;
        entry.image.reset();
        entry.id = 0;
    }

    const ImTextureID id = entry.id;
    m_entries.emplace(key, std::move(entry));
    return id;
}

} // namespace Supersonic
