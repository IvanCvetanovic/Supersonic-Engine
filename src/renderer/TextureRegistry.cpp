#include "renderer/TextureRegistry.hpp"
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
// Generous, but each set is tiny and the pool is allocated once.
constexpr uint32_t kMaxMaterialSets = 512;

uint64_t materialKey(uint32_t albedo, uint32_t normal) {
    return (static_cast<uint64_t>(albedo) << 32) | static_cast<uint64_t>(normal);
}
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

    std::cout << "[TextureRegistry] Initialised with built-in white, flat-normal and checker textures." << std::endl;
}

TextureRegistry::~TextureRegistry() {
    // Sets are freed with the pool; the images own their own handles.
    m_materialSets.clear();
    m_textures.clear();
    if (m_descriptorPool) {
        m_deviceRef.GetDevice().destroyDescriptorPool(m_descriptorPool);
        m_descriptorPool = nullptr;
    }
}

void TextureRegistry::createDescriptorPool() {
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    // Two bindings per material set: albedo and normal.
    poolSize.descriptorCount = kMaxMaterialSets * 2;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = kMaxMaterialSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    m_descriptorPool = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);
}

const TextureRegistry::Texture* TextureRegistry::get(uint32_t id) const {
    if (id >= m_textures.size()) return nullptr;
    return &m_textures[id];
}

uint32_t TextureRegistry::UploadRGBA(const std::string& key, const uint8_t* pixels,
                                     uint32_t width, uint32_t height, bool srgb) {
    if (auto it = m_lookup.find(key); it != m_lookup.end()) {
        return it->second;
    }

    if (!pixels || width == 0 || height == 0) {
        std::cerr << "[TextureRegistry] Refusing to upload empty texture '" << key << "'." << std::endl;
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
    texture.image = std::make_unique<VulkanImage>(
        m_deviceRef, width, height,
        srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm);
    texture.image->CreateSampler();

    VulkanImage::TransitionLayout(m_deviceRef, m_commandPool, texture.image->GetImage(),
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(m_deviceRef, m_commandPool, staging.GetBuffer(),
                                   texture.image->GetImage(), width, height);
    VulkanImage::TransitionLayout(m_deviceRef, m_commandPool, texture.image->GetImage(),
                                  vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);

    const auto id = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(texture));
    m_lookup.emplace(key, id);
    return id;
}

uint32_t TextureRegistry::Acquire(const std::string& path, bool srgb) {
    if (path.empty()) return srgb ? m_whiteTexture : m_flatNormalTexture;

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
        std::cerr << "[TextureRegistry] Could not load '" << path << "': "
                  << (stbi_failure_reason() ? stbi_failure_reason() : "unknown")
                  << " - using the " << (srgb ? "checker" : "flat-normal") << " fallback." << std::endl;
        if (pixels) stbi_image_free(pixels);
        // Cache the failure against this key so it is not retried every frame.
        const uint32_t fallback = srgb ? m_checkerTexture : m_flatNormalTexture;
        m_lookup.emplace(key, fallback);
        return fallback;
    }

    const uint32_t id = UploadRGBA(key, pixels,
                                   static_cast<uint32_t>(width), static_cast<uint32_t>(height), srgb);
    stbi_image_free(pixels);

    std::cout << "[TextureRegistry] Loaded " << path << " (" << width << "x" << height
              << ", " << channels << " source channels, " << (srgb ? "sRGB" : "linear") << ")." << std::endl;
    return id;
}

vk::DescriptorSet TextureRegistry::AcquireMaterialSet(uint32_t albedoId, uint32_t normalId) {
    if (albedoId >= m_textures.size()) albedoId = m_checkerTexture;
    if (normalId >= m_textures.size()) normalId = m_flatNormalTexture;

    const uint64_t key = materialKey(albedoId, normalId);
    if (auto it = m_materialSets.find(key); it != m_materialSets.end()) {
        return it->second;
    }

    if (m_materialSets.size() >= kMaxMaterialSets) {
        std::cerr << "[TextureRegistry] Material descriptor set pool exhausted; reusing the default." << std::endl;
        const uint64_t fallbackKey = materialKey(m_whiteTexture, m_flatNormalTexture);
        if (auto it = m_materialSets.find(fallbackKey); it != m_materialSets.end()) return it->second;
        return nullptr;
    }

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialLayout;

    const auto sets = m_deviceRef.GetDevice().allocateDescriptorSets(allocInfo);
    if (sets.empty()) {
        throw std::runtime_error("TextureRegistry ran out of descriptor sets!");
    }

    const Texture* albedo = get(albedoId);
    const Texture* normal = get(normalId);
    if (!albedo || !normal || !albedo->image || !normal->image) {
        throw std::runtime_error("Material references a texture that does not exist!");
    }

    std::array<vk::DescriptorImageInfo, 2> images{};
    images[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    images[0].imageView = albedo->image->GetImageView();
    images[0].sampler = albedo->image->GetSampler();

    images[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    images[1].imageView = normal->image->GetImageView();
    images[1].sampler = normal->image->GetSampler();

    for (const auto& info : images) {
        if (!info.sampler || !info.imageView) {
            throw std::runtime_error("Texture is missing a sampler or image view!");
        }
    }

    std::array<vk::WriteDescriptorSet, 2> writes{};
    for (uint32_t i = 0; i < 2; ++i) {
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

} // namespace Supersonic
