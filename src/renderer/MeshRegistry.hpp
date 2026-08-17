#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/MeshData.hpp"
#include "renderer/VulkanBuffer.hpp"
#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

// GPU-resident mesh: device-local vertex and index buffers plus the local-space
// bounds the picker uses.
struct GpuMesh {
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    uint32_t indexCount{0};
    glm::vec3 boundsMin{-0.5f};
    glm::vec3 boundsMax{0.5f};
};

// Owns every mesh the scene can draw, keyed by the MeshComponent description.
//
// RenderSystem previously bound one hardcoded cube and issued the same draw for
// every entity, so MeshComponent was data nothing consumed and "Create Sphere"
// produced a cube. Meshes are uploaded once on first use and cached.
class MeshRegistry {
public:
    static constexpr uint32_t kInvalidMesh = 0xFFFFFFFFu;

    MeshRegistry(VulkanDevice& device, vk::CommandPool commandPool);
    ~MeshRegistry();

    MeshRegistry(const MeshRegistry&) = delete;
    MeshRegistry& operator=(const MeshRegistry&) = delete;

    // Resolves a MeshComponent to a GPU mesh, uploading it if this is the first
    // request. Falls back to the unit cube when generation or loading fails, so
    // a bad asset degrades to visible geometry instead of a missing draw.
    uint32_t Acquire(const std::string& primitiveType, const std::string& filePath);

    // Uploads caller-supplied geometry under an explicit cache key.
    uint32_t Upload(const std::string& key, const MeshData& data);

    const GpuMesh* Get(uint32_t id) const;
    uint32_t GetCubeMesh() const { return m_cubeMesh; }
    size_t Size() const { return m_meshes.size(); }

private:
    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;

    std::vector<GpuMesh> m_meshes;
    std::unordered_map<std::string, uint32_t> m_lookup;
    uint32_t m_cubeMesh{kInvalidMesh};
};

} // namespace Supersonic
