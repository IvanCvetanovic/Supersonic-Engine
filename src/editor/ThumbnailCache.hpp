#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"

namespace Supersonic {

// Image previews for the content browser.
//
// The browser drew every file as an identical tile, so a folder of textures was
// a grid of identical labels - you had to open a file to find out what it was.
//
// Owns its own transient command pool rather than borrowing the renderer's:
// uploads happen while the editor is being built, which is a different point in
// the frame from where the renderer records, and a separate pool means the two
// cannot interfere.
class ThumbnailCache {
public:
    explicit ThumbnailCache(VulkanDevice& device);
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    // The ImGui texture for a file, or 0 when it is not an image, cannot be
    // decoded, or the budget is spent. A failure is cached, so a file that is
    // not really an image is not re-read from disk every frame.
    ImTextureID Get(const std::filesystem::path& path);

    // True for extensions worth attempting at all.
    static bool IsImage(const std::filesystem::path& path);

    size_t Count() const { return m_entries.size(); }

    // Thumbnails are decorative; a directory of a thousand textures must not
    // consume a gigabyte of VRAM to show them.
    static constexpr size_t kMaxThumbnails = 64;
    static constexpr uint32_t kThumbnailSize = 128;

private:
    struct Entry {
        std::unique_ptr<VulkanImage> image;
        ImTextureID id{0};
    };

    ImTextureID upload(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
                       Entry& entry);

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool{nullptr};
    std::unordered_map<std::string, Entry> m_entries;
};

} // namespace Supersonic
