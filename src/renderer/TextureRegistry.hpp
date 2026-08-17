#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanImage.hpp"

namespace Supersonic {

// Loads, uploads and caches textures, and owns one descriptor set per texture.
//
// The engine previously had exactly one texture: a 64x64 checkerboard generated
// in C++ inside the renderer. MaterialComponent carried albedoTexturePath and
// normalTexturePath that nothing read, and stb_image sat vendored and unused,
// so there was no way to get an actual image into the engine.
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
    uint32_t Acquire(const std::string& path, bool srgb = true);

    // Uploads raw RGBA8 pixels under an explicit cache key.
    uint32_t UploadRGBA(const std::string& key, const uint8_t* pixels,
                        uint32_t width, uint32_t height, bool srgb = true);

    vk::DescriptorSet GetDescriptorSet(uint32_t id) const;

    uint32_t GetCheckerTexture() const { return m_checkerTexture; }
    uint32_t GetWhiteTexture() const { return m_whiteTexture; }
    size_t Size() const { return m_textures.size(); }

private:
    struct Texture {
        std::unique_ptr<VulkanImage> image;
        vk::DescriptorSet descriptorSet{nullptr};
        uint32_t width{0};
        uint32_t height{0};
    };

    void createDescriptorPool();
    vk::DescriptorSet allocateAndWriteSet(const VulkanImage& image);

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;
    vk::DescriptorSetLayout m_materialLayout;
    vk::DescriptorPool m_descriptorPool{nullptr};

    std::vector<Texture> m_textures;
    std::unordered_map<std::string, uint32_t> m_lookup;

    uint32_t m_whiteTexture{kInvalidTexture};
    uint32_t m_checkerTexture{kInvalidTexture};
};

} // namespace Supersonic
