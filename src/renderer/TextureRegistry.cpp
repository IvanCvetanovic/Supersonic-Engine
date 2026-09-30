#include "renderer/TextureRegistry.hpp"
#include <algorithm>
#include "core/AssetDatabase.hpp"
#include "core/ImagePixels.hpp"
#include "core/Log.hpp"
#include "renderer/VulkanBuffer.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

// stb_image was vendored in third_party/ but never included by anything.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

namespace Supersonic {

namespace {
// The ids a material set is built from, in binding order.
//
// This used to be two ids packed into one uint64_t, which stopped working the
// moment there were three. Squeezing three into 64 bits would mean 21 bits each
// and a silent wrong answer the day an id passed two million; hashing them
// would mean a collision rendering one material with another's maps, with
// nothing to say so. An ordered key over at most MaterialSets::kMaxSets entries
// costs a handful of comparisons and cannot be wrong.
using MaterialKey = std::array<uint32_t, VulkanPipeline::kMaterialBindingCount>;
} // namespace

TextureRegistry::TextureRegistry(VulkanDevice& device, vk::CommandPool commandPool,
                                 vk::DescriptorSetLayout materialLayout)
    : m_deviceRef(device), m_commandPool(commandPool), m_materialLayout(materialLayout) {

    createDescriptorPool();

    // 1x1 white: the neutral albedo for materials with no texture, so the
    // shader needs no branch and untextured geometry shows its vertex colour.
    const std::array<uint8_t, 4> white = { 255, 255, 255, 255 };
    m_whiteTexture = UploadRGBA("builtin:white", white.data(), 1, 1, true);

    // 1x1 flat normal: (0.5, 0.5, 1.0) decodes to (0, 0, 1) in tangent space,
    // i.e. "use the interpolated vertex normal unchanged". Uploaded as UNORM,
    // because a normal map holds directions and must not be gamma-decoded.
    const std::array<uint8_t, 4> flatNormal = { 128, 128, 255, 255 };
    m_flatNormalTexture = UploadRGBA("builtin:flatnormal", flatNormal.data(), 1, 1, false);

    // 1x1 white, as data rather than as colour: the neutral occlusion /
    // roughness / metallic map. Every channel is 1, and the shader MULTIPLIES
    // the material's constants by it, so a material with no map shades exactly
    // as it did before this binding existed.
    //
    // Its own texture rather than reusing the white albedo, and the reason is
    // one character of the key: that one is uploaded srgb, so the hardware
    // applies a transfer function on read. White survives it - 255 decodes to
    // 1.0 either way - so this would have worked, right up until somebody
    // changed the neutral to anything other than white and spent an afternoon
    // on why a roughness of 0.5 was arriving as 0.21.
    const std::array<uint8_t, 4> neutralOrm = { 255, 255, 255, 255 };
    m_neutralOrmTexture = UploadRGBA("builtin:neutralorm", neutralOrm.data(), 1, 1, false);

    // The old hardcoded checkerboard, kept as the missing-texture marker.
    constexpr uint32_t dim = 64;
    std::vector<uint8_t> checker(static_cast<size_t>(dim) * dim * 4);
    for (uint32_t y = 0; y < dim; ++y) {
        for (uint32_t x = 0; x < dim; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const size_t i = (static_cast<size_t>(y) * dim + x) * 4;
            checker[i + 0] = light ? 255 : 40;
            checker[i + 1] = light ? 255 : 120;
            checker[i + 2] = light ? 255 : 220;
            checker[i + 3] = 255;
        }
    }
    m_checkerTexture = UploadRGBA("builtin:checker", checker.data(), dim, dim, true);

    // 1x1 black, as data: the neutral additive overlay, which adds nothing.
    // LAST, after the checker, so the ids RenderableComponent's literals name
    // for the other built-ins (0, 1, 2) do not move.
    const std::array<uint8_t, 4> black = { 0, 0, 0, 255 };
    m_blackTexture = UploadRGBA("builtin:black", black.data(), 1, 1, false);

    SUPERSONIC_LOG_INFO("TextureRegistry") << "Initialised with built-in white, flat-normal and checker textures." << std::endl;
}

TextureRegistry::~TextureRegistry() {
    // Sets are freed with the pool; the images own their own handles.
    m_materialSets.clear();
    m_textures.clear();
    if (m_setPool && m_setPool->handle) {
        m_deviceRef.GetDevice().destroyDescriptorPool(m_setPool->handle);
        m_setPool->handle = nullptr;
    }
    // Last, and it is what makes a free still queued on the device harmless:
    // its weak reference no longer locks, so it does nothing. The renderer
    // flushes that queue AFTER destroying this registry.
    m_setPool.reset();
}

void TextureRegistry::createDescriptorPool() {
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    // Read from the layout's own count rather than repeated here. A pool sized
    // for two bindings while the layout declares three does not fail: it simply
    // runs out of sets a third early, hundreds of materials into a scene.
    poolSize.descriptorCount = MaterialSets::kMaxSets * VulkanPipeline::kMaterialBindingCount;

    vk::DescriptorPoolCreateInfo poolInfo{};
    // FREEABLE. Without this flag a set goes back only with the whole pool, and
    // this registry drops sets for as long as the process runs: every texture
    // invalidated, every texture's pixels replaced. A pool that never took one
    // back ran out a few hundred drops in - ErrorOutOfPoolMemory, thrown
    // mid-frame, with the cache holding a few dozen sets - which is what walking
    // Magic Portals' lightmapped levels in one process did, at 3-10.
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = MaterialSets::kMaxSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    m_setPool = std::make_shared<SetPool>();
    m_setPool->device = m_deviceRef.GetDevice();
    m_setPool->handle = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);
}

void TextureRegistry::giveBack(std::vector<vk::DescriptorSet> sets) {
    if (sets.empty() || !m_setPool) return;
    // Deferred rather than freed here, for the reason the images are: a set
    // bound by a command buffer submitted a frame or two ago is still being
    // read, and freeing it now is a use-after-free that was legal on the frame
    // it was recorded. Counted as live until the free actually runs.
    m_deviceRef.DeferDestroy(
        [pool = std::weak_ptr<SetPool>(m_setPool), sets = std::move(sets)]() {
            const std::shared_ptr<SetPool> owner = pool.lock();
            if (!owner || !owner->handle) return; // freed with the pool already
            owner->device.freeDescriptorSets(owner->handle, sets);
            for (std::size_t i = 0; i < sets.size(); ++i) {
                if (!owner->ledger.GivenBack()) {
                    SUPERSONIC_LOG_ERROR("TextureRegistry")
                        << "Gave back a material set the pool did not count as live." << std::endl;
                    break;
                }
            }
        });
}

vk::DescriptorSet TextureRegistry::fallbackSet() const {
    // Every slot named. A brace-initialised array zero-fills what it is not
    // given, and id 0 is the white albedo: an overlay of white. The gloss IS
    // white, named rather than left to the zero-fill, so the key says so.
    // Read with its own samplers: every one of these is a 1x1, which no wrap
    // can change.
    const MaterialSets::Key<VulkanPipeline::kMaterialBindingCount> fallbackKey{
        MaterialKey{m_whiteTexture, m_flatNormalTexture, m_neutralOrmTexture, m_blackTexture,
                    m_whiteTexture},
        false};
    if (auto it = m_materialSets.find(fallbackKey); it != m_materialSets.end()) return it->second;
    return nullptr;
}

bool TextureRegistry::isBuiltIn(uint32_t id) const {
    // Black too, and the port makes it urgent: a lightmap that failed to load
    // caches black under its own path, and that path is invalidated every time
    // its level unloads.
    return id == m_checkerTexture || id == m_whiteTexture ||
           id == m_flatNormalTexture || id == m_neutralOrmTexture ||
           id == m_blackTexture;
}

const TextureRegistry::Texture* TextureRegistry::get(uint32_t id) const {
    if (id >= m_textures.size()) return nullptr;
    return &m_textures[id];
}

uint32_t TextureRegistry::UploadRGBA(const std::string& key, const uint8_t* pixels,
                                     uint32_t width, uint32_t height, bool srgb,
                                     vk::Filter filter, vk::SamplerAddressMode addressMode,
                                     bool mipmaps) {
    if (auto it = m_lookup.find(key); it != m_lookup.end()) {
        return it->second;
    }

    if (!pixels || width == 0 || height == 0) {
        SUPERSONIC_LOG_ERROR("TextureRegistry") << "Refusing to upload empty texture '" << key << "'." << std::endl;
        return m_checkerTexture;
    }

    const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(width) * height * 4;

    VulkanBuffer staging(m_deviceRef.GetAllocator(), imageSize,
                         vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(pixels, imageSize);

    Texture texture;
    texture.width = width;
    texture.height = height;

    // SRGB for colour data so the hardware decodes to linear on read, which is
    // what the PBR maths expects. Normal maps and other data textures must pass
    // srgb=false or they get an unwanted transfer function applied.
    const vk::Format format = srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;

    // Mip chain requested here, where the size is known. Without it a floor at
    // a glancing angle sampled full-resolution texels smaller than a pixel and
    // shimmered as the camera moved - which 4x MSAA does nothing about,
    // because it anti-aliases geometry edges rather than texture minification.
    // Unless the caller said otherwise (`mipmaps`), for art drawn without one.
    texture.image = std::make_unique<VulkanImage>(
        m_deviceRef, width, height, format,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::ImageAspectFlagBits::eColor,
        /*arrayLayers*/ 1, vk::SampleCountFlagBits::e1, /*cubeCompatible*/ false,
        /*generateMipmaps*/ mipmaps);

    // After the image, because the sampler's maxLod comes from its level count.
    //
    // The filter arrives from the caller, which is the only one that knows the
    // PATH - see the header. So does the wrap: only the caller knows whether the
    // image tiles or is drawn once, edge to edge.
    texture.image->CreateSampler(filter, addressMode);

    VulkanImage::TransitionLayout(m_deviceRef, m_commandPool, texture.image->GetImage(),
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(m_deviceRef, m_commandPool, staging.GetBuffer(),
                                   texture.image->GetImage(), width, height);

    // GenerateMipmaps leaves every level in eShaderReadOnlyOptimal, so it
    // REPLACES the transition that used to follow the copy rather than adding
    // to it - transitioning again from eTransferDstOptimal would be a lie about
    // the layout the levels are actually in. With one level there is nothing
    // to build, and the transition is the one that always followed a copy.
    if (mipmaps) {
        const bool filtered = VulkanImage::GenerateMipmaps(
            m_deviceRef, m_commandPool, texture.image->GetImage(), format, width, height,
            texture.image->GetMipLevels());
        if (!filtered) {
            SUPERSONIC_LOG_WARN("TextureRegistry")
                << key << ": this format cannot be linearly filtered on this device, "
                << "so only the base level is populated";
        }
    } else {
        VulkanImage::TransitionLayout(m_deviceRef, m_commandPool, texture.image->GetImage(),
                                      vk::ImageLayout::eTransferDstOptimal,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    const auto id = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(texture));
    m_lookup.emplace(key, id);
    return id;
}

uint32_t TextureRegistry::Acquire(const std::string& path, bool srgb, uint32_t fallback) {
    if (path.empty()) return fallback;

    const std::string key = (srgb ? "srgb:" : "data:") + path;
    if (auto it = m_lookup.find(key); it != m_lookup.end()) {
        return it->second;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    // Forced to 4 channels so the upload path only ever deals with RGBA8.
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels || width <= 0 || height <= 0) {
        SUPERSONIC_LOG_ERROR("TextureRegistry") << "Could not load '" << path << "': "
                  << (stbi_failure_reason() ? stbi_failure_reason() : "unknown")
                  << " - using the caller's fallback texture." << std::endl;
        if (pixels) stbi_image_free(pixels);
        // Cache the failure against this key so it is not retried every frame.
        //
        // Which is why every built-in has to be protected from Invalidate: this
        // line puts a BUILT-IN id under a real file's key, so invalidating that
        // file would otherwise reach in and destroy a texture every material in
        // the scene is sharing.
        m_lookup.emplace(key, fallback);
        return fallback;
    }

    // THE FILTER COMES FROM THE ASSET, not from here and not from the material
    // that asked. It is a property of the image: two materials naming one file
    // cannot disagree about it, because this cache is keyed by path and the
    // first one to ask would decide for both.
    //
    // Resolved HERE, from `path`, because `key` is not one - it carries a
    // colour-space prefix, and asking the filesystem about it looks for a
    // .meta that can never exist.
    //
    // The wrap, the mip chain and the alpha border come from the same file for
    // the same reason. A hot reload (Invalidate, then this) reads them again,
    // so an edited PNG comes back as its .meta asks.
    const UploadChoice choice = ChooseUpload(AssetDatabase::TextureSettingsForAsset(path));
    if (choice.fixAlphaBorder) {
        ImagePixels::FixAlphaBorder(pixels, static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height));
    }
    if (choice.filter != vk::Filter::eLinear || choice.addressMode != vk::SamplerAddressMode::eRepeat ||
        !choice.mipmaps || choice.fixAlphaBorder) {
        SUPERSONIC_LOG_INFO("TextureRegistry")
            << path << ": " << (choice.filter == vk::Filter::eNearest ? "nearest" : "linear")
            << " filtering, " << (choice.addressMode == vk::SamplerAddressMode::eClampToEdge ? "clamped" : "repeated")
            << ", " << (choice.mipmaps ? "a mip chain" : "one level")
            << (choice.fixAlphaBorder ? ", alpha border fixed" : "") << ", as its .meta asks." << std::endl;
    }

    const uint32_t id = UploadRGBA(key, pixels,
                                   static_cast<uint32_t>(width), static_cast<uint32_t>(height), srgb,
                                   choice.filter, choice.addressMode, choice.mipmaps);
    stbi_image_free(pixels);

    const auto* uploaded = get(id);
    const uint32_t mips = uploaded && uploaded->image ? uploaded->image->GetMipLevels() : 1;
    SUPERSONIC_LOG_INFO("TextureRegistry") << "Loaded " << path << " (" << width << "x" << height
              << ", " << channels << " source channels, " << (srgb ? "sRGB" : "linear")
              << ", " << mips << " mip level(s))." << std::endl;
    return id;
}

vk::DescriptorSet TextureRegistry::AcquireMaterialSet(uint32_t albedoId, uint32_t normalId,
                                                      uint32_t ormId, uint32_t overlayId,
                                                      uint32_t glossId, bool clampToEdge) {
    // Each slot falls back to its OWN neutral, not to a shared one. A missing
    // albedo is a mistake worth seeing, so it gets the checkerboard; a missing
    // normal, ORM or overlay map is the ordinary case - most materials have
    // none of them - so they get values that change nothing at all. The gloss
    // is white, what an unnamed one resolves to: a sprite asking for a
    // highlight with no map gets it uniform.
    const MaterialSets::Key<VulkanPipeline::kMaterialBindingCount> key{
        MaterialSets::ResolveKey(
            MaterialKey{albedoId, normalId, ormId, overlayId, glossId},
            static_cast<uint32_t>(m_textures.size()),
            MaterialKey{m_checkerTexture, m_flatNormalTexture, m_neutralOrmTexture, m_blackTexture,
                        m_whiteTexture}),
        clampToEdge};
    if (auto it = m_materialSets.find(key); it != m_materialSets.end()) {
        return it->second;
    }

    // The POOL's count, not the cache's. A set dropped from the cache is still
    // in the pool until its deferred free runs, and checking the cache's size
    // here is what let the allocation below throw with room apparently left.
    if (!m_setPool->ledger.HasRoom()) {
        SUPERSONIC_LOG_ERROR("TextureRegistry") << "Material descriptor set pool exhausted ("
            << m_setPool->ledger.Live() << " live); reusing the default." << std::endl;
        return fallbackSet();
    }

    // Written binding by binding from the key itself, so adding a map means
    // adding it to the key and to the layout and nowhere else - which is how
    // the overlay, the fourth, arrived, and the gloss, the fifth. The previous
    // shape named each texture in a local and would have needed a third of
    // everything, in three places, all of them easy to half-do.
    //
    // Resolved BEFORE the set is allocated, because each of these can throw,
    // and a throw after the allocation would leave a set the pool counts and
    // nothing will ever give back.
    std::array<vk::DescriptorImageInfo, VulkanPipeline::kMaterialBindingCount> images{};
    for (uint32_t i = 0; i < VulkanPipeline::kMaterialBindingCount; ++i) {
        const Texture* texture = get(key.ids[i]);
        if (!texture || !texture->image) {
            throw std::runtime_error("Material references a texture that does not exist!");
        }
        images[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        images[i].imageView = texture->image->GetImageView();
        // The texture's own sampler unless the material clamps; a clamped read
        // of an image uploaded with repeat is a second sampler over the same
        // image, made once and kept with it.
        images[i].sampler = texture->image->GetSampler(
            MaterialSets::WrapFor(texture->image->GetAddressMode(), clampToEdge));

        if (!images[i].sampler || !images[i].imageView) {
            throw std::runtime_error("Texture is missing a sampler or image view!");
        }
    }

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_setPool->handle;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialLayout;

    // The two ways a pool refuses, caught and degraded exactly like the cap
    // above. vulkan-hpp THROWS them - the empty-vector check that stood here
    // could never run - and a freeable pool can also be fragmented, with room
    // in total and none in one piece. Anything else (a lost device, no memory
    // at all) is not a full pool and is left to propagate.
    std::vector<vk::DescriptorSet> sets;
    try {
        sets = m_deviceRef.GetDevice().allocateDescriptorSets(allocInfo);
    } catch (const vk::OutOfPoolMemoryError&) {
        SUPERSONIC_LOG_ERROR("TextureRegistry") << "Material descriptor pool out of memory ("
            << m_setPool->ledger.Live() << " live); reusing the default." << std::endl;
        return fallbackSet();
    } catch (const vk::FragmentedPoolError&) {
        SUPERSONIC_LOG_ERROR("TextureRegistry") << "Material descriptor pool fragmented ("
            << m_setPool->ledger.Live() << " live); reusing the default." << std::endl;
        return fallbackSet();
    }
    if (sets.empty()) return fallbackSet();
    m_setPool->ledger.Taken();

    std::array<vk::WriteDescriptorSet, VulkanPipeline::kMaterialBindingCount> writes{};
    for (uint32_t i = 0; i < VulkanPipeline::kMaterialBindingCount; ++i) {
        writes[i].dstSet = sets[0];
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &images[i];
    }

    m_deviceRef.GetDevice().updateDescriptorSets(
        static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    m_materialSets.emplace(key, sets[0]);
    return sets[0];
}


bool TextureRegistry::Invalidate(const std::string& path) {
    // The cache key carries the colour space, because the same file loaded as
    // sRGB and as linear data are two different images. Both are dropped: the
    // caller asked for that path to be re-read, not for one interpretation.
    bool dropped = false;
    std::vector<uint32_t> deadIds;

    for (const bool srgb : { true, false }) {
        const std::string key = (srgb ? "srgb:" : "data:") + path;
        const auto it = m_lookup.find(key);
        if (it == m_lookup.end()) continue;

        const uint32_t id = it->second;
        m_lookup.erase(it);
        dropped = true;

        // Never free a built-in. Acquire hands these out when a file is
        // missing and caches the failure under the real path's key, so a
        // dead path resolves to a shared id - freeing it would take the
        // checkerboard, or the neutral ORM map, away from everything else
        // still using it.
        //
        // Every built-in, listed once. Adding one and forgetting this is a
        // destroyed image still bound in live descriptor sets, which is the
        // null-sampler class of bug this file's comments already carry the
        // scars of.
        if (isBuiltIn(id)) continue;
        if (id >= m_textures.size()) continue;

        deadIds.push_back(id);
        auto image = std::move(m_textures[id].image);
        m_textures[id].width = 0;
        m_textures[id].height = 0;
        m_deviceRef.DeferDestroy(
            [img = std::shared_ptr<VulkanImage>(std::move(image))]() mutable { img.reset(); });
    }

    // A descriptor set naming a destroyed image is the null-sampler class of
    // bug that cost this project six commits, so every set mentioning a dead id
    // goes too - out of the cache now, back to the pool once no frame can bind
    // it. They are rebuilt on demand by AcquireMaterialSet.
    //
    // Every binding, not two named ones: TakeNaming searches the whole key.
    // The generation below is bumped in the same call, which is what lets a
    // cache keyed on a set handle tell a recycled handle from the old one.
    giveBack(MaterialSets::TakeNaming(m_materialSets, deadIds));

    if (dropped) {
        // Every cached path-to-id answer is now wrong.
        ++m_generation;
        SUPERSONIC_LOG_INFO("TextureRegistry") << "Invalidated '" << path
            << "'; the next request will re-read it from disk." << std::endl;
    }
    return dropped;
}

bool TextureRegistry::ReplaceRGBA(uint32_t id, const uint8_t* pixels,
                                  uint32_t width, uint32_t height, bool srgb) {
    if (id >= m_textures.size() || !pixels || width == 0 || height == 0) return false;
    if (isBuiltIn(id)) {
        return false;  // shared fallbacks; replacing one changes every user
    }

    const std::string scratchKey = "__replace_scratch";
    m_lookup.erase(scratchKey);
    const uint32_t scratchId = UploadRGBA(scratchKey, pixels, width, height, srgb);
    m_lookup.erase(scratchKey);
    if (scratchId >= m_textures.size()) return false;

    auto oldImage = std::move(m_textures[id].image);
    m_textures[id].image = std::move(m_textures[scratchId].image);
    m_textures[id].width = width;
    m_textures[id].height = height;

    // Descriptor sets naming this id keep working only if they are rewritten to
    // the new image, so they are dropped and rebuilt rather than left pointing
    // at the image about to be destroyed - and given back, or a texture
    // replaced once a frame would take a set from the pool once a frame.
    giveBack(MaterialSets::TakeNaming(m_materialSets, std::vector<uint32_t>{id}));

    m_deviceRef.DeferDestroy(
        [img = std::shared_ptr<VulkanImage>(std::move(oldImage))]() mutable { img.reset(); });

    // Same id, different image.
    ++m_generation;
    return true;
}

} // namespace Supersonic
