#include "renderer/PipelineCache.hpp"
#include "core/Log.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace Supersonic {

namespace {

// VkPipelineCacheHeaderVersionOne, laid out by the spec:
//   0  uint32  header length (32)
//   4  uint32  header version (1)
//   8  uint32  vendor ID
//  12  uint32  device ID
//  16  uint8[16] pipeline cache UUID
constexpr size_t kHeaderSize = 32;

uint32_t readU32(const std::vector<uint8_t>& blob, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, blob.data() + offset, sizeof(value));
    return value;
}

} // namespace

PipelineCache::PipelineCache(VulkanDevice& device, std::string path)
    : m_deviceRef(device), m_path(std::move(path)) {

    std::vector<uint8_t> blob;

    std::error_code ec;
    if (std::filesystem::exists(m_path, ec)) {
        std::ifstream file(m_path, std::ios::binary | std::ios::ate);
        if (file) {
            const auto size = static_cast<std::streamsize>(file.tellg());
            if (size > 0) {
                blob.resize(static_cast<size_t>(size));
                file.seekg(0);
                file.read(reinterpret_cast<char*>(blob.data()), size);
                if (!file) blob.clear();
            }
        }
    }

    if (!blob.empty() && !headerMatchesThisDevice(blob)) {
        SUPERSONIC_LOG_INFO("PipelineCache") << "Discarding " << m_path
                  << ": it was written by a different driver or device." << std::endl;
        blob.clear();
    }

    vk::PipelineCacheCreateInfo info{};
    info.initialDataSize = blob.size();
    info.pInitialData = blob.empty() ? nullptr : blob.data();

    m_cache = m_deviceRef.GetDevice().createPipelineCache(info);
    m_loadedBytes = blob.size();

    if (m_loadedBytes > 0) {
        SUPERSONIC_LOG_INFO("PipelineCache") << "Reusing " << m_loadedBytes << " bytes from " << m_path << "." << std::endl;
    } else {
        SUPERSONIC_LOG_INFO("PipelineCache") << "Starting empty; pipelines will compile from SPIR-V this run." << std::endl;
    }
}

PipelineCache::~PipelineCache() {
    Save();
    if (m_cache) {
        m_deviceRef.GetDevice().destroyPipelineCache(m_cache);
        m_cache = nullptr;
    }
}

bool PipelineCache::headerMatchesThisDevice(const std::vector<uint8_t>& blob) const {
    if (blob.size() < kHeaderSize) return false;
    if (readU32(blob, 0) != kHeaderSize) return false;
    if (readU32(blob, 4) != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) return false;

    const vk::PhysicalDeviceProperties props = m_deviceRef.GetPhysicalDevice().getProperties();
    if (readU32(blob, 8) != props.vendorID) return false;
    if (readU32(blob, 12) != props.deviceID) return false;

    // The UUID changes with the driver version, which is what makes a cache
    // written before a driver update unusable rather than merely stale.
    return std::memcmp(blob.data() + 16, props.pipelineCacheUUID.data(), VK_UUID_SIZE) == 0;
}

void PipelineCache::Save() const {
    if (!m_cache) return;

    std::vector<uint8_t> data;
    try {
        data = m_deviceRef.GetDevice().getPipelineCacheData(m_cache);
    } catch (const std::exception& e) {
        SUPERSONIC_LOG_ERROR("PipelineCache") << "Could not read cache data back: " << e.what() << std::endl;
        return;
    }

    if (data.empty()) return;
    // Nothing new compiled, so rewriting the file would only cost I/O.
    if (data.size() == m_loadedBytes) return;

    std::error_code ec;
    const std::filesystem::path filePath(m_path);
    if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }

    // Written to a temporary first: a half-written cache killed mid-save would
    // otherwise be loaded on the next run, and the header check cannot detect
    // truncation past the first 32 bytes.
    const std::filesystem::path temp = filePath.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            SUPERSONIC_LOG_ERROR("PipelineCache") << "Could not open " << temp.string() << " for writing." << std::endl;
            return;
        }
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            SUPERSONIC_LOG_ERROR("PipelineCache") << "Write to " << temp.string() << " failed." << std::endl;
            return;
        }
    }

    std::filesystem::rename(temp, filePath, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        SUPERSONIC_LOG_ERROR("PipelineCache") << "Could not replace " << m_path << ": " << ec.message() << std::endl;
        return;
    }

    SUPERSONIC_LOG_INFO("PipelineCache") << "Saved " << data.size() << " bytes to " << m_path << "." << std::endl;
}

} // namespace Supersonic
