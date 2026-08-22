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

    // What the source file said this surface looks like, or `present == false`
    // for procedural geometry and for a file that named no material. Kept here
    // because this is where the file was parsed: re-reading a 40 MB .glb to ask
    // it what colour it is would be absurd, and the alternative - handing it to
    // the entity during the load - would overwrite whatever the user had since
    // authored, every time the asset hot-reloaded.
    MeshMaterial material;
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

    // What the file behind this id said its surface is. Null for an unknown id,
    // and `present == false` when the geometry came from a generator or the
    // file named no material.
    const MeshMaterial* GetMaterial(uint32_t id) const;

    // Drops the cache entry for a key and hands its buffers to the device's
    // deferred-destroy queue, so the next Acquire re-reads the file.
    //
    // The id is NOT recycled. Reusing it would resolve a stale id held by some
    // component to a completely different mesh, which is the same failure the
    // scene serializer already avoids by writing parent links as array indices
    // rather than raw entity handles. A dead slot costs a few dozen bytes;
    // silently drawing the wrong geometry costs an afternoon.
    //
    // Returns false when the key was not cached.
    bool Invalidate(const std::string& key);

    // Replaces the geometry behind an EXISTING id, keeping the id valid.
    //
    // This is the one that matters for a mesh rebuilt every frame - fog, a
    // dynamic terrain patch, a debug overlay. Invalidate-then-Acquire would
    // allocate a new id per frame and grow the vector without bound.
    bool Replace(uint32_t id, const MeshData& data);

    // Bumped whenever an id stops meaning what it meant.
    //
    // Anything that caches "this path resolves to this id" has to be told when
    // that stops being true, and there is no other signal: a hot reload leaves
    // the component holding the path it always held. Mixing this counter into
    // such a cache invalidates every entry at once, which is exactly the right
    // blast radius for something that happens when a file is saved.
    uint64_t Generation() const { return m_generation; }

    uint32_t GetCubeMesh() const { return m_cubeMesh; }
    size_t Size() const { return m_meshes.size(); }

private:
    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;

    std::vector<GpuMesh> m_meshes;
    std::unordered_map<std::string, uint32_t> m_lookup;
    uint64_t m_generation{1};
    uint32_t m_cubeMesh{kInvalidMesh};
};

} // namespace Supersonic
