#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <array>
#include <map>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"
#include "renderer/VulkanPipeline.hpp"

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

    // Loads from disk on first request; afterwards returns the cached id, and
    // caches a failure too so a broken path is not reopened every frame.
    //
    // srgb=false is required for data textures: a normal map stores directions
    // and an ORM map stores three numbers, and a transfer function bends both.
    //
    // `fallback` is what a missing or undecodable file resolves to, and it is a
    // parameter rather than something derived from `srgb` because that bool now
    // separates three kinds of texture and can only answer two. Deriving it
    // handed a broken ORM path the FLAT NORMAL - (128,128,255), which as packed
    // occlusion/roughness/metallic reads as half occlusion, half roughness and
    // fully metallic - a darker, shinier, solid-metal surface arriving from a
    // typo, under a log line that said "flat-normal fallback" about an ORM map.
    uint32_t Acquire(const std::string& path, bool srgb, uint32_t fallback);

    // Uploads raw RGBA8 pixels under an explicit cache key.
    //
    // `filter` is PASSED IN rather than looked up from the key, and that is not
    // a style choice: the key is not a path. Acquire prefixes it with "srgb:"
    // or "data:" so one file uploaded in two colour spaces gets two entries, so
    // a .meta lookup made here would be for a file that cannot exist - which is
    // exactly the bug this parameter replaced, and one every unit test passed
    // over because none of them composed the two.
    //
    // Linear by default, which is right for every generated texture: they have
    // no asset on disk to carry an import setting.
    uint32_t UploadRGBA(const std::string& key, const uint8_t* pixels,
                        uint32_t width, uint32_t height, bool srgb = true,
                        vk::Filter filter = vk::Filter::eLinear);

    // Descriptor set binding both maps for one material, cached per pair so a
    // scene sharing materials does not allocate a set per entity.
    vk::DescriptorSet AcquireMaterialSet(uint32_t albedoId, uint32_t normalId, uint32_t ormId);

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

    // 1x1 white uploaded as DATA: the neutral occlusion/roughness/metallic map.
    // The shader multiplies the material's constants by it, so a material with
    // no map shades exactly as it did before the binding existed.
    uint32_t GetNeutralOrmTexture() const { return m_neutralOrmTexture; }

    size_t Size() const { return m_textures.size(); }
    size_t MaterialSetCount() const { return m_materialSets.size(); }

private:
    struct Texture {
        std::unique_ptr<VulkanImage> image;
        uint32_t width{0};
        uint32_t height{0};
    };

    void createDescriptorPool();
    // One of the shared fallbacks. Never freed and never rewritten: Acquire
    // hands them out for missing files, so many paths resolve to one id.
    bool isBuiltIn(uint32_t id) const;

    const Texture* get(uint32_t id) const;

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;
    vk::DescriptorSetLayout m_materialLayout;
    vk::DescriptorPool m_descriptorPool{nullptr};

    std::vector<Texture> m_textures;
    std::unordered_map<std::string, uint32_t> m_lookup;

    // Keyed by the ids of every binding, in binding order. Ordered rather than
    // hashed: three 32-bit ids do not pack into a 64-bit key, and a hash
    // collision here would render one material with another's maps and say
    // nothing about it.
    std::map<std::array<uint32_t, VulkanPipeline::kMaterialBindingCount>,
             vk::DescriptorSet> m_materialSets;

    uint32_t m_whiteTexture{kInvalidTexture};
    uint64_t m_generation{1};
    uint32_t m_checkerTexture{kInvalidTexture};
    uint32_t m_flatNormalTexture{kInvalidTexture};
    uint32_t m_neutralOrmTexture{kInvalidTexture};
};

} // namespace Supersonic
