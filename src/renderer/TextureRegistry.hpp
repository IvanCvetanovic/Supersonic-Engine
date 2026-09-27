#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <array>
#include <map>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/MaterialSetLedger.hpp"
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

    // Descriptor set binding every map of one material, cached per five ids
    // so a scene sharing materials does not allocate a set per entity.
    //
    // Every slot falls back to its own neutral (MaterialSets::ResolveKey): an
    // id that names no texture - kInvalidTexture, say - is the checkerboard as
    // an albedo, the flat normal, the neutral ORM, black as an overlay and
    // white as a gloss map. Not defaulted: a fifth map a caller forgot is a
    // set the caller did not mean to ask for.
    //
    // A full pool - MaterialSets::kMaxSets live, which counts sets dropped from
    // the cache and not yet freed - hands out the white fallback's set and logs
    // it, rather than throwing out of the middle of a frame. So does a driver
    // that reports the pool out of memory or fragmented before that.
    vk::DescriptorSet AcquireMaterialSet(uint32_t albedoId, uint32_t normalId, uint32_t ormId,
                                         uint32_t overlayId, uint32_t glossId);

    // Drops a cached path so the next Acquire re-reads it from disk, and
    // queues the old image for deferred destruction. This is what makes editing
    // a texture and seeing the result possible without restarting - the
    // registries cached failures deliberately, and had no way to un-cache a
    // success or a failure.
    //
    // Any material descriptor set naming the old id is dropped with it, because
    // a set still pointing at a destroyed image is exactly the null-sampler
    // class of bug that cost this project six commits - and given back to the
    // pool, through the device's deferred queue, because a command buffer
    // recorded in the last two frames may still bind it.
    //
    // So a set's HANDLE can come back later naming other textures. Every path
    // that frees one bumps Generation() as it drops it, so anything caching a
    // handle across frames has to mix the generation in beside it, as the
    // shadow pass signature does.
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

    // 1x1 black, as data: the neutral additive overlay. A material with no
    // overlay binds this and the 2D sprite path adds exactly zero, so the add
    // is the same arithmetic rather than a branch. Uploaded after the checker,
    // so the three built-ins RenderableComponent's literals name kept their ids.
    uint32_t GetBlackTexture() const { return m_blackTexture; }

    size_t Size() const { return m_textures.size(); }

    // The sets the cache hands out.
    size_t MaterialSetCount() const { return m_materialSets.size(); }

    // The sets the POOL holds: the cached ones, plus any dropped from the cache
    // whose free has not run yet. Equal to MaterialSetCount() once the renderer
    // has collected - two frames after the last Invalidate or ReplaceRGBA - and
    // larger by exactly what is waiting before that. It is the number the cap
    // is checked against, and the one a check of reclamation has to read: the
    // cache's size went down on every Invalidate even when nothing was freed.
    size_t MaterialSetsInPool() const { return m_setPool ? m_setPool->ledger.Live() : 0; }

private:
    struct Texture {
        std::unique_ptr<VulkanImage> image;
        uint32_t width{0};
        uint32_t height{0};
    };

    // The pool and what it has handed out, SHARED so that a free queued on the
    // device can outlive this registry. The renderer destroys the registry -
    // and with it the pool, which frees every set at once - before it flushes
    // the deferred queue at shutdown, so a queued free holding the pool's handle
    // would free into a destroyed pool. A queued free holds a weak reference
    // instead, and does nothing once the pool is gone.
    //
    // The registry's m_setPool is the ONLY strong owner, and that is what the
    // guard rests on: a queued free locks the reference only for the duration
    // of its own call, never across the destructor, so once m_setPool is reset
    // no lock can succeed. Hand a second shared_ptr to anything and a free could
    // find the pool alive with a null handle, skip, and leave the ledger counting
    // a set that no longer exists.
    //
    // Measured, not assumed: with the free taking the raw handle instead, the
    // Magic Portals level walk (MagicPortals --visit-levels) quit with 5 frees
    // queued and failed on 20 validation errors, "vkFreeDescriptorSets():
    // descriptorPool Invalid VkDescriptorPool Object".
    struct SetPool {
        vk::Device device;
        vk::DescriptorPool handle{nullptr};
        MaterialSets::Ledger ledger{MaterialSets::kMaxSets};
    };

    void createDescriptorPool();
    // Queues `sets`, already out of the cache, to be given back to the pool
    // once no frame in flight can still bind them.
    void giveBack(std::vector<vk::DescriptorSet> sets);
    // The white fallback's set, for a pool that cannot hand out another. Null
    // when even that one was never allocated.
    vk::DescriptorSet fallbackSet() const;
    // One of the shared fallbacks. Never freed and never rewritten: Acquire
    // hands them out for missing files, so many paths resolve to one id.
    bool isBuiltIn(uint32_t id) const;

    const Texture* get(uint32_t id) const;

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;
    vk::DescriptorSetLayout m_materialLayout;
    std::shared_ptr<SetPool> m_setPool;

    std::vector<Texture> m_textures;
    std::unordered_map<std::string, uint32_t> m_lookup;

    // Keyed by the ids of every binding, in binding order. Ordered rather than
    // hashed: five 32-bit ids do not pack into a 64-bit key, and a hash
    // collision here would render one material with another's maps and say
    // nothing about it.
    std::map<std::array<uint32_t, VulkanPipeline::kMaterialBindingCount>,
             vk::DescriptorSet> m_materialSets;

    uint32_t m_whiteTexture{kInvalidTexture};
    uint64_t m_generation{1};
    uint32_t m_checkerTexture{kInvalidTexture};
    uint32_t m_flatNormalTexture{kInvalidTexture};
    uint32_t m_neutralOrmTexture{kInvalidTexture};
    uint32_t m_blackTexture{kInvalidTexture};
};

} // namespace Supersonic
