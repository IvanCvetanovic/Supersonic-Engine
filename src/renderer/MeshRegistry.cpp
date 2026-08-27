#include "renderer/MeshRegistry.hpp"

#include <unordered_map>
#include "core/Log.hpp"

#include "core/ModelLoader.hpp"
#include "core/TerrainGenerator.hpp"
#include "core/GltfLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace Supersonic {

MeshRegistry::MeshRegistry(VulkanDevice& device, vk::CommandPool commandPool)
    : m_deviceRef(device), m_commandPool(commandPool) {

    // The cube is the fallback for everything, so it must exist up front.
    MeshData cube;
    if (!ModelLoader::GenerateCube(1.0f, cube)) {
        throw std::runtime_error("MeshRegistry could not generate the fallback cube mesh!");
    }
    m_cubeMesh = Upload("primitive:Cube", cube);

    SUPERSONIC_LOG_INFO("MeshRegistry") << "Initialised with fallback cube mesh." << std::endl;
}

MeshRegistry::~MeshRegistry() {
    m_meshes.clear();
}

void MeshRegistry::BuildSections(const std::vector<GltfLoader::Submesh>& submeshes,
                                 int32_t mergeSkin,
                                 const std::string& sourcePath,
                                 MeshData& outData,
                                 std::vector<MeshSection>& outSections) {
    // GROUPED BY MATERIAL, not appended in file order.
    //
    // Every primitive still lands in one vertex buffer and one
    // index buffer - the merge is what makes a whole model one bind
    // - but primitives sharing a material are made contiguous, and
    // each run becomes a section the draw loop can issue on its
    // own. A soldier authored as seventeen primitives over three
    // materials is three draws, not seventeen.
    //
    // Grouped by NAME where there is one, because that is what the
    // file means by "the same surface" and what a game addresses it
    // by. A material with no name falls back to a key of its own,
    // so two anonymous surfaces are not silently welded together.
    std::vector<std::string> groupKeys;      // first-seen order
    std::unordered_map<std::string, std::vector<const GltfLoader::Submesh*>> groups;

    size_t kept = 0;
    for (const auto& submesh : submeshes) {
        if (submesh.skinIndex != mergeSkin) {
            SUPERSONIC_LOG_ERROR("MeshRegistry") << "Skipping '" << submesh.name << "' in " << sourcePath
                      << ": it belongs to skin " << submesh.skinIndex
                      << " while the mesh is being built from skin " << mergeSkin << "."
                      << std::endl;
            continue;
        }
        ++kept;

        // The prefix is a control character, which a glTF material
        // name cannot usefully contain - so a file cannot collide
        // with these by naming a material "none".
        std::string groupKey;
        if (!submesh.material.present) {
            groupKey = "\x01none";
        } else if (submesh.material.name.empty()) {
            groupKey = "\x01anon:" + std::to_string(groupKeys.size());
        } else {
            groupKey = submesh.material.name;
        }

        if (groups.find(groupKey) == groups.end()) groupKeys.push_back(groupKey);
        groups[groupKey].push_back(&submesh);
    }

    for (const std::string& groupKey : groupKeys) {
        MeshSection section;
        section.firstIndex = static_cast<uint32_t>(outData.indices.size());

        for (const GltfLoader::Submesh* submesh : groups[groupKey]) {
            if (submesh->material.present && !section.material.present) {
                section.material = submesh->material;
            }
            const auto vertexOffset = static_cast<uint32_t>(outData.vertices.size());
            outData.vertices.insert(outData.vertices.end(),
                                 submesh->mesh.vertices.begin(),
                                 submesh->mesh.vertices.end());
            for (const uint32_t index : submesh->mesh.indices) {
                outData.indices.push_back(index + vertexOffset);
            }
        }

        section.indexCount =
            static_cast<uint32_t>(outData.indices.size()) - section.firstIndex;
        if (section.indexCount > 0) outSections.push_back(std::move(section));
    }
}

uint32_t MeshRegistry::Upload(const std::string& key, const MeshData& data) {
    if (auto it = m_lookup.find(key); it != m_lookup.end()) {
        return it->second;
    }

    if (data.empty()) {
        SUPERSONIC_LOG_ERROR("MeshRegistry") << "Refusing to upload empty mesh '" << key << "'." << std::endl;
        return m_cubeMesh;
    }

    GpuMesh mesh;
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    mesh.boundsMin = data.boundsMin;
    mesh.boundsMax = data.boundsMax;

    // EVERY mesh gets a section, here, at the one place geometry becomes a
    // GpuMesh. A procedural cube, an OBJ and a single-material glTF all end up
    // with exactly one covering every index, so the draw loop never has to ask
    // whether a mesh has sections - the empty case does not exist. Acquire
    // replaces this when a file actually described more than one surface.
    mesh.sections.push_back(MeshSection{0u, mesh.indexCount, MeshMaterial{}});

    // Vertices: staged through a host-visible buffer into device-local memory.
    {
        const vk::DeviceSize size = sizeof(Vertex) * data.vertices.size();
        VulkanBuffer staging(m_deviceRef.GetAllocator(), size,
                             vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
        staging.UploadData(data.vertices.data(), size);

        mesh.vertexBuffer = std::make_unique<VulkanBuffer>(
            m_deviceRef.GetAllocator(), size,
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
            VMA_MEMORY_USAGE_GPU_ONLY);

        VulkanBuffer::CopyBuffer(m_deviceRef, m_commandPool,
                                 staging.GetBuffer(), mesh.vertexBuffer->GetBuffer(), size);
    }

    // Indices: uint32_t throughout, so meshes past 65k vertices are correct.
    {
        const vk::DeviceSize size = sizeof(uint32_t) * data.indices.size();
        VulkanBuffer staging(m_deviceRef.GetAllocator(), size,
                             vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
        staging.UploadData(data.indices.data(), size);

        mesh.indexBuffer = std::make_unique<VulkanBuffer>(
            m_deviceRef.GetAllocator(), size,
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
            VMA_MEMORY_USAGE_GPU_ONLY);

        VulkanBuffer::CopyBuffer(m_deviceRef, m_commandPool,
                                 staging.GetBuffer(), mesh.indexBuffer->GetBuffer(), size);
    }

    const auto id = static_cast<uint32_t>(m_meshes.size());
    m_meshes.push_back(std::move(mesh));
    m_lookup.emplace(key, id);

    SUPERSONIC_LOG_INFO("MeshRegistry") << "Uploaded '" << key << "' (" << data.vertices.size()
              << " vertices, " << data.indices.size() / 3 << " triangles)." << std::endl;
    return id;
}

uint32_t MeshRegistry::Acquire(const std::string& primitiveType, const std::string& filePath) {
    const std::string key = filePath.empty() ? ("primitive:" + primitiveType)
                                             : ("file:" + filePath);

    if (auto it = m_lookup.find(key); it != m_lookup.end()) {
        return it->second;
    }

    MeshData data;
    MeshMaterial material;
    std::vector<MeshSection> sections;
    bool ok = false;

    if (!filePath.empty()) {
        std::string extension = std::filesystem::path(filePath).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (extension == ".gltf" || extension == ".glb") {
            // Merge every primitive in the file into one mesh. The importer has
            // already baked each node's transform into its vertices, so they
            // share a coordinate system.
            const GltfLoader::Scene scene = GltfLoader::Load(filePath);
            if (scene.ok) {
                // Only primitives that share ONE skin are merged.
                //
                // Two skeletons welded into one mesh would have their joint
                // indices addressing the wrong palette slice, and an unskinned
                // primitive merged into a skinned draw carries default weights -
                // so it would either collapse onto the origin or rigidly follow
                // joint 0. Neither reports anything.
                int32_t mergeSkin = -1;
                for (const auto& submesh : scene.submeshes) {
                    if (submesh.skinIndex >= 0) { mergeSkin = submesh.skinIndex; break; }
                }

                MeshRegistry::BuildSections(scene.submeshes, mergeSkin, filePath,
                                            data, sections);

                // The first surviving material is still what the ENTITY is
                // offered on import, because a MaterialComponent is one
                // material. The difference is that the others now draw from
                // their own sections instead of being dropped.
                if (!sections.empty()) material = sections.front().material;

                data.computeBounds();
                ok = !data.empty();
            } else {
                SUPERSONIC_LOG_ERROR("MeshRegistry") << scene.error << std::endl;
            }
        } else {
            ok = ModelLoader::LoadOBJ(filePath, data);
        }
    } else if (primitiveType == "Cube") {
        ok = ModelLoader::GenerateCube(ModelLoader::kCubeSize, data);
    } else if (primitiveType == "Sphere") {
        ok = ModelLoader::GenerateSphere(ModelLoader::kSphereRadius, ModelLoader::kSphereRings,
                                         ModelLoader::kSphereSectors, data);
    } else if (primitiveType == "Quad") {
        ok = ModelLoader::GenerateQuad(ModelLoader::kQuadWidth, ModelLoader::kQuadHeight, data);
    } else if (primitiveType == "Plane") {
        ok = ModelLoader::GeneratePlane(ModelLoader::kPlaneWidth, ModelLoader::kPlaneHeight, data);
    } else if (primitiveType == "Terrain") {
        ok = TerrainGenerator::GenerateTerrainMesh(TerrainGenerator::kPrimitiveWidth,
                                                   TerrainGenerator::kPrimitiveDepth,
                                                   TerrainGenerator::kPrimitiveHeightScale,
                                                   data);
    } else {
        SUPERSONIC_LOG_ERROR("MeshRegistry") << "Unknown primitive '" << primitiveType
                  << "', falling back to cube." << std::endl;
    }

    if (!ok || data.empty()) {
        // Cache the failure against the same key so a broken asset does not
        // retry generation on every single frame.
        m_lookup.emplace(key, m_cubeMesh);
        return m_cubeMesh;
    }

    const uint32_t id = Upload(key, data);

    // After Upload, because Upload is also the entry point for procedural
    // geometry, which has no source file and therefore no material to carry.
    if (id < m_meshes.size()) {
        m_meshes[id].material = material;

        // ONE SECTION COVERING EVERYTHING is the answer for a procedural
        // primitive, for an OBJ, and for a glTF whose surfaces all share a
        // material. Upload already writes that, and it is what lets the draw
        // loop treat every mesh alike rather than asking whether this one has
        // sections. Only a file that really described several replaces it.
        if (!sections.empty()) m_meshes[id].sections = std::move(sections);
    }
    return id;
}

const GpuMesh* MeshRegistry::Get(uint32_t id) const {
    if (id >= m_meshes.size()) return nullptr;
    return &m_meshes[id];
}

GpuMesh* MeshRegistry::GetMutable(uint32_t id) {
    if (id >= m_meshes.size()) return nullptr;
    return &m_meshes[id];
}

const MeshMaterial* MeshRegistry::GetMaterial(uint32_t id) const {
    if (id >= m_meshes.size()) return nullptr;
    return &m_meshes[id].material;
}


bool MeshRegistry::Invalidate(const std::string& key) {
    const auto it = m_lookup.find(key);
    if (it == m_lookup.end()) return false;

    const uint32_t id = it->second;

    // Never free the built-in cube. Acquire hands it out when a model fails to
    // load and caches the FILE key onto it, so several dead paths share one id
    // with "primitive:Cube" - and freeing it through any of them would take the
    // fallback away from everything still using it, for the rest of the
    // session, with the lookup still cheerfully returning the gutted id.
    //
    // TextureRegistry::Invalidate has had this guard since it was written; this
    // one did not. The key is still dropped, so the next request re-uploads
    // under it rather than resolving to the cube for ever.
    if (id == m_cubeMesh) {
        m_lookup.erase(it);
        ++m_generation;
        SUPERSONIC_LOG_INFO("MeshRegistry") << "Dropped '" << key
            << "', which was resolving to the built-in cube; the buffers stay."
            << std::endl;
        return true;
    }

    m_lookup.erase(it);

    if (id >= m_meshes.size()) return false;

    // Ownership moves into the deleter, so the buffers outlive this call by
    // however long the device says a submitted command buffer might still name
    // them. Releasing here instead would free memory the GPU is reading.
    auto vertexBuffer = std::move(m_meshes[id].vertexBuffer);
    auto indexBuffer = std::move(m_meshes[id].indexBuffer);
    m_meshes[id].indexCount = 0;

    m_deviceRef.DeferDestroy(
        [vb = std::shared_ptr<VulkanBuffer>(std::move(vertexBuffer)),
         ib = std::shared_ptr<VulkanBuffer>(std::move(indexBuffer))]() mutable {
            vb.reset();
            ib.reset();
        });

    // Every cached path-to-id answer is now wrong: the next request for this
    // key builds a new id, and anything still holding the old one is pointing
    // at a slot with no buffers in it.
    ++m_generation;

    SUPERSONIC_LOG_INFO("MeshRegistry") << "Invalidated '" << key
        << "'; the next request will re-upload it." << std::endl;
    return true;
}

bool MeshRegistry::Replace(uint32_t id, const MeshData& data) {
    if (id >= m_meshes.size()) return false;
    if (data.vertices.empty() || data.indices.empty()) return false;

    // Upload into a scratch key first, then move the new buffers over the old
    // ones. Building in place would leave the id pointing at half a mesh if the
    // upload threw partway through.
    const std::string scratchKey = "__replace_scratch";
    m_lookup.erase(scratchKey);
    const uint32_t scratchId = Upload(scratchKey, data);
    m_lookup.erase(scratchKey);
    if (scratchId >= m_meshes.size()) return false;

    auto oldVertex = std::move(m_meshes[id].vertexBuffer);
    auto oldIndex = std::move(m_meshes[id].indexBuffer);

    m_meshes[id].vertexBuffer = std::move(m_meshes[scratchId].vertexBuffer);
    m_meshes[id].indexBuffer = std::move(m_meshes[scratchId].indexBuffer);
    m_meshes[id].indexCount = m_meshes[scratchId].indexCount;
    m_meshes[id].boundsMin = m_meshes[scratchId].boundsMin;
    m_meshes[id].boundsMax = m_meshes[scratchId].boundsMax;
    m_meshes[scratchId].indexCount = 0;

    m_deviceRef.DeferDestroy(
        [vb = std::shared_ptr<VulkanBuffer>(std::move(oldVertex)),
         ib = std::shared_ptr<VulkanBuffer>(std::move(oldIndex))]() mutable {
            vb.reset();
            ib.reset();
        });

    // The id is unchanged and the geometry behind it is not, which is the case
    // a cache keyed on the id alone cannot see. Its BOUNDS may have changed
    // too, and those are copied onto the renderable by SyncResources.
    ++m_generation;
    return true;
}

} // namespace Supersonic
