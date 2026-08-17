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
// Generous, but each set is tiny and this is allocated once.
constexpr uint32_t kMaxTextures = 512;
} // namespace

TextureRegistry::TextureRegistry(VulkanDevice& device, vk::CommandPool commandPool,
                                 vk::DescriptorSetLayout materialLayout)
    : m_deviceRef(device), m_commandPool(commandPool), m_materialLayout(materialLayout) {

    createDescriptorPool();

    // 1x1 white: the neutral albedo for materials with no texture, so the
    // shader needs no branch and untextured geometry shows its vertex colour.
    const std::array<uint8_t, 4> white = { 255, 255, 255, 255 };
    m_whiteTexture = UploadRGBA("builtin:white", white.data(), 1, 1, true);

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

    std::cout << "[TextureRegistry] Initialised with built-in white and checker textures." << std::endl;
}

TextureRegistry::~TextureRegistry() {
    // Sets are freed with the pool; the images own their own handles.
    m_textures.clear();
    if (m_descriptorPool) {
        m_deviceRef.GetDevice().destroyDescriptorPool(m_descriptorPool);
        m_descriptorPool = nullptr;
    }
}

void TextureRegistry::createDescriptorPool() {
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    poolSize.descriptorCount = kMaxTextures;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = kMaxTextures;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    m_descriptorPool = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);
}

vk::DescriptorSet TextureRegistry::allocateAndWriteSet(const VulkanImage& image) {
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialLayout;

    const auto sets = m_deviceRef.GetDevice().allocateDescriptorSets(allocInfo);
    if (sets.empty()) {
        throw std::runtime_error("TextureRegistry ran out of descriptor sets!");
    }

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = image.GetImageView();
    imageInfo.sampler = image.GetSampler();

    if (!imageInfo.sampler || !imageInfo.imageView) {
        throw std::runtime_error("Texture is missing a sampler or image view!");
    }

    vk::WriteDescriptorSet write{};
    write.dstSet = sets[0];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    m_deviceRef.GetDevice().updateDescriptorSets(1, &write, 0, nullptr);
    return sets[0];
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

    texture.descriptorSet = allocateAndWriteSet(*texture.image);

    const auto id = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(texture));
    m_lookup.emplace(key, id);
    return id;
}

uint32_t TextureRegistry::Acquire(const std::string& path, bool srgb) {
    if (path.empty()) return m_whiteTexture;

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
                  << " - using the checker fallback." << std::endl;
        if (pixels) stbi_image_free(pixels);
        // Cache the failure against this key so it is not retried every frame.
        m_lookup.emplace(key, m_checkerTexture);
        return m_checkerTexture;
    }

    const uint32_t id = UploadRGBA(key, pixels,
                                   static_cast<uint32_t>(width), static_cast<uint32_t>(height), srgb);
    stbi_image_free(pixels);

    std::cout << "[TextureRegistry] Loaded " << path << " (" << width << "x" << height
              << ", " << channels << " source channels)." << std::endl;
    return id;
}

vk::DescriptorSet TextureRegistry::GetDescriptorSet(uint32_t id) const {
    if (id >= m_textures.size()) {
        return id == kInvalidTexture && m_checkerTexture < m_textures.size()
             ? m_textures[m_checkerTexture].descriptorSet
             : nullptr;
    }
    return m_textures[id].descriptorSet;
}

} // namespace Supersonic
