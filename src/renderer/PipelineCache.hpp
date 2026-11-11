#pragma once

#include <string>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

// Persistent VkPipelineCache.
//
// Every pipeline was previously created with a null cache, so the driver
// recompiled all of them from SPIR-V on every launch - and again after every
// swapchain recreation that rebuilt them. A cache lets the driver reuse the
// compiled form both within a run and across runs.
//
// The blob is driver- and device-specific. Vulkan tolerates a stale or foreign
// blob, but the header is validated here anyway rather than handing a driver
// bytes from a different GPU and trusting it to notice.
class PipelineCache {
public:
    PipelineCache(VulkanDevice& device, std::string path);
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    vk::PipelineCache Get() const { return m_cache; }

    // Writes the cache back to disk. Called from the destructor; exposed so a
    // long session can checkpoint without exiting.
    void Save() const;

    // How many bytes of a saved cache were accepted at construction: zero for a
    // cache that started empty, or whose file was missing, unreadable or from
    // another device. Lets a test tell "loaded the file" from "made an empty cache",
    // which Get() cannot: both give a valid handle.
    size_t LoadedBytes() const { return m_loadedBytes; }

private:
    // The 32-byte header every VkPipelineCache blob starts with.
    bool headerMatchesThisDevice(const std::vector<uint8_t>& blob) const;

    VulkanDevice& m_deviceRef;
    std::string m_path;
    vk::PipelineCache m_cache{nullptr};
    size_t m_loadedBytes{0};
};

} // namespace Supersonic
