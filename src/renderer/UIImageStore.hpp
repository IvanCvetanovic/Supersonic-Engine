#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "renderer/VulkanImage.hpp"

namespace Supersonic {

class VulkanDevice;

// Pixels a game owns, as something the UI can draw.
//
// The engine could already put an image on screen in exactly one place - the
// editor's thumbnail cache - and a game could not reach it. So a minimap, a fog
// overlay, a portrait and an item icon were all missing for one reason: there
// was no way to hand the UI a texture that did not come out of the asset
// pipeline as a material.
//
// The handle is an ImTextureID widened to a uint64_t, so UIImageComponent can
// carry it without Components.hpp needing ImGui or Vulkan in its translation
// unit. Zero is never a valid handle and means "nothing yet".
//
// EVERY IMAGE HERE IS RGBA8 AND UNORM, not sRGB, and that is not a detail. The
// UI is composited by ImGui without a colour conversion, so a texture the
// hardware decoded to linear would arrive washed out - which is the same
// decision, for the same reason, that the thumbnail cache records.
class UIImageStore {
public:
    UIImageStore(VulkanDevice& device, vk::CommandPool commandPool);
    ~UIImageStore();

    UIImageStore(const UIImageStore&) = delete;
    UIImageStore& operator=(const UIImageStore&) = delete;

    // Uploads RGBA8 pixels and returns a handle, or 0 on failure.
    //
    // `pixels` must hold width * height * 4 bytes and is copied, so the caller
    // may free it immediately.
    uint64_t Create(const uint8_t* pixels, uint32_t width, uint32_t height);

    // Replaces the pixels of an existing image, keeping the handle valid.
    //
    // This is what fog of war needs and what makes it worth having at all: a
    // visibility overlay changes every few frames, and creating a texture per
    // update would allocate and leak device memory at frame rate. Dimensions
    // must match the original - a resize is a new image, because the descriptor
    // ImGui is holding describes the old extent.
    //
    // BLOCKS until the copy is done. The alternative is tracking which frames
    // in flight still read the image, and for an overlay updated a few times a
    // second the stall is smaller than the bookkeeping. Said plainly because it
    // is the wrong trade for a texture updated every frame.
    bool Update(uint64_t handle, const uint8_t* pixels, uint32_t width, uint32_t height);

    // Frees an image. The handle is invalid afterwards and must not be drawn.
    //
    // NOT safe while a frame that references it is in flight. The caller clears
    // the component first; there is no deferred queue here, deliberately, and
    // the reason is that a store which quietly outlived its users would hide
    // exactly the leak it exists to prevent.
    void Destroy(uint64_t handle);

    bool Has(uint64_t handle) const { return m_images.find(handle) != m_images.end(); }
    size_t Count() const { return m_images.size(); }

private:
    struct Entry {
        std::unique_ptr<VulkanImage> image;
        uint32_t width{0};
        uint32_t height{0};
        uint64_t handle{0};
    };

    VulkanDevice& m_device;
    vk::CommandPool m_commandPool;
    std::unordered_map<uint64_t, Entry> m_images;
};

} // namespace Supersonic
