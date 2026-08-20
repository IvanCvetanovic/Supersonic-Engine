#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"

namespace Supersonic {

// Loads, uploads and caches textures, and hands out per-material descriptor
// sets combining them.
//
// The engine previously had exactly one texture: a 64x64 checkerboard generated
// in C++ inside the renderer. MaterialComponent carried albedoTexturePath and
// normalTexturePath that nothing read, and stb_image sat vendored and unused.
class TextureRegistry {
public:
    static constexpr uint32_t kInvalidTexture = 0xFFFFFFFFu;

    TextureRegistry(VulkanDevice& device, vk::CommandPool commandPool, vk::DescriptorSetLayout materialLayout);
    ~TextureRegistry();

    TextureRegistry(const TextureRegistry&) = delete;
    TextureRegistry& operator=(const TextureRegistry&) = delete;

    // Loads from disk on first request; afterwards returns the cached id.
    // Falls back to the checkerboard when a file is missing or undecodable, and
    // caches that failure so a broken path is not reopened every frame.
    //
    // srgb=false is required for data textures: a normal map stores directions,
    // not colour, and applying a transfer function to it corrupts the vectors.
    uint32_t Acquire(const std::string& path, bool srgb = true);

    // Uploads raw RGBA8 pixels under an explicit cache key.
    uint32_t UploadRGBA(const std::string& key, const uint8_t* pixels,
                        uint32_t width, uint32_t height, bool srgb = true);

    // Descriptor set binding both maps for one material, cached per pair so a
    // scene sharing materials does not allocate a set per entity.
    vk::DescriptorSet AcquireMaterialSet(uint32_t albedoId, uint32_t normalId);

    // Drops a cached path so the next Acquire re-reads it from disk, and
    // queues the old image for deferred destruction. This is what makes editing
    // a texture and seeing the result possible without restarting - the
    // registries cached failures deliberately, and had no way to un-cache a
    // success or a failure.
    //
    // Any material descriptor set naming the old id is dropped with it, because
    // a set still pointing at a destroyed image is exactly the null-sampler
    // class of bug that cost this project six commits.
    bool Invalidate(const std::string& path);

    // Rewrites the pixels behind an existing id, keeping the id and every
    // descriptor set that names it valid. Required for anything regenerated per
    // frame; Invalidate-then-Acquire would leak an image per frame.
    bool ReplaceRGBA(uint32_t id, const uint8_t* pixels,
                     uint32_t width, uint32_t height, bool srgb = true);


    // Bumped whenever an id stops meaning what it meant.
    //
    // Anything that caches "this path resolves to this id" has to be told when
    // that stops being true, and there is no other signal: a hot reload leaves
    // the component holding the path it always held. Mixing this counter into
    // such a cache invalidates every entry at once, which is exactly the right
    // blast radius for something that happens when a file is saved.
    uint64_t Generation() const { return m_generation; }

    uint32_t GetCheckerTexture() const { return m_checkerTexture; }
    uint32_t GetWhiteTexture() const { return m_whiteTexture; }

    // 1x1 (0.5, 0.5, 1.0): a flat tangent-space normal. Materials without a
    // normal map sample this, so the shader needs no branch.
    uint32_t GetFlatNormalTexture() const { return m_flatNormalTexture; }

    size_t Size() const { return m_textures.size(); }
    size_t MaterialSetCount() const { return m_materialSets.size(); }

private:
    struct Texture {
        std::unique_ptr<VulkanImage> image;
        uint32_t width{0};
        uint32_t height{0};
    };

    void createDescriptorPool();
    const Texture* get(uint32_t id) const;

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;
    vk::DescriptorSetLayout m_materialLayout;
    vk::DescriptorPool m_descriptorPool{nullptr};

    std::vector<Texture> m_textures;
    std::unordered_map<std::string, uint32_t> m_lookup;

    // Keyed by (albedo << 32) | normal.
    std::unordered_map<uint64_t, vk::DescriptorSet> m_materialSets;

    uint32_t m_whiteTexture{kInvalidTexture};
    uint64_t m_generation{1};
    uint32_t m_checkerTexture{kInvalidTexture};
    uint32_t m_flatNormalTexture{kInvalidTexture};
};

} // namespace Supersonic
