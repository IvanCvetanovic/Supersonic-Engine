#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/GltfLoader.hpp"   // Submesh, for BuildSections
#include "core/MeshData.hpp"
#include "renderer/VulkanBuffer.hpp"
#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

// GPU-resident mesh: device-local vertex and index buffers plus the local-space
// bounds the picker uses.
// One contiguous run of indices sharing a material.
//
// A model is authored as several named surfaces and arrives as several
// primitives; they were merged into one mesh with one material and the first
// won, so a monument of stone and gold drew entirely in stone. Sections keep
// the merge - one vertex buffer, one index buffer, one bind - and let the draw
// loop issue a range per surface.
//
// The runs are built by GROUPING the file's primitives by material rather than
// by keeping file order, so a model with seventeen primitives over three
// materials is three draws and not seventeen.
struct MeshSection {
    uint32_t firstIndex{0};
    uint32_t indexCount{0};
    MeshMaterial material;

    // Resolved from the paths above, ONCE PER MESH rather than once per entity
    // per frame. Resolving a path means building a map key by concatenating
    // strings, and a scene of eighty units sharing one model would otherwise
    // pay for the same eight lookups eighty times a frame.
    //
    // Cached here and not on the entity because a section belongs to the MESH -
    // every entity drawing this model wants the same answer.
    uint32_t albedoTextureID{0};
    uint32_t normalTextureID{0};
    uint32_t ormTextureID{0};
};

struct GpuMesh {
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    uint32_t indexCount{0};

    // One entry per surface, in the order their indices appear. ALWAYS at least
    // one for a mesh that drew at all: a procedural primitive and a
    // single-material file both produce exactly one section covering every
    // index, so the draw loop has no special case and "sections" is not a
    // second way of describing a mesh that only some meshes have.
    std::vector<MeshSection> sections;

    // Which texture generation the ids above were resolved against. Zero means
    // never, which is what a freshly uploaded mesh is - and a reload bumps the
    // generation, so a re-imported texture is picked up rather than leaving
    // every section pointing at the image it replaced.
    uint64_t sectionTextureGeneration{0};

    // And which colour space their albedo was acquired in
    // (RenderSettings::decodesColourTextures). A scene switching its encoding
    // moves no generation, so this is compared beside it.
    bool sectionDecodesColour{true};
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

    // The id a key resolves to, or kInvalidMesh when nothing is cached under
    // it. Upload answers a known key with its id too, but SILENTLY - it keeps
    // the old geometry and drops the new - so a caller that means "replace if
    // present, upload if not" has to be able to ask first.
    uint32_t Find(const std::string& key) const;

    const GpuMesh* Get(uint32_t id) const;

    // What the file behind this id said its surface is. Null for an unknown id,
    // and `present == false` when the geometry came from a generator or the
    // file named no material.
    const MeshMaterial* GetMaterial(uint32_t id) const;

    // Groups a loaded file's primitives by material and merges them into one
    // mesh, recording the index range each surface ends up occupying.
    //
    // A free function rather than inline in Acquire because Acquire needs a
    // Vulkan device to reach and the suites deliberately touch no Vulkan entry
    // point - the same reason MaterialSystem::ApplyImportedMaterial is one.
    // The arithmetic here is an index offset per surface, which is exactly the
    // kind of thing that is off by one and looks almost right.
    //
    // `mergeSkin` is the skin every kept primitive must belong to; the rest are
    // reported and skipped, because two skeletons welded into one mesh would
    // have their joint indices addressing the wrong palette slice.
    static void BuildSections(const std::vector<GltfLoader::Submesh>& submeshes,
                              int32_t mergeSkin,
                              const std::string& sourcePath,
                              MeshData& outData,
                              std::vector<MeshSection>& outSections);

    // Mutable access for the one caller that resolves section textures. Not a
    // general door into the cache: RenderSystem::SyncResources owns both
    // registries and is the only place that can turn a path into a texture id.
    GpuMesh* GetMutable(uint32_t id);

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
    // Stages the vertices and indices into device-local buffers and fills in
    // everything a GpuMesh derives from the data: the count, the bounds and
    // the one section covering every index. The registry's bookkeeping - the
    // slot, the key, the material - is the caller's.
    //
    // Shared by Upload and Replace. Replace used to get its buffers by
    // uploading under a scratch key and stealing them back, which left one
    // dead slot in the vector per call and one section describing the OLD
    // index count on the mesh it had just replaced.
    GpuMesh createGpuMesh(const MeshData& data);

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool;

    std::vector<GpuMesh> m_meshes;
    std::unordered_map<std::string, uint32_t> m_lookup;
    uint64_t m_generation{1};
    uint32_t m_cubeMesh{kInvalidMesh};
};

} // namespace Supersonic
