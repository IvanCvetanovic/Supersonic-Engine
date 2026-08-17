#include "renderer/MeshRegistry.hpp"

#include "core/ModelLoader.hpp"
#include "core/TerrainGenerator.hpp"

#include <iostream>

namespace Engine {

MeshRegistry::MeshRegistry(VulkanDevice& device, vk::CommandPool commandPool)
    : m_deviceRef(device), m_commandPool(commandPool) {

    // The cube is the fallback for everything, so it must exist up front.
    MeshData cube;
    if (!ModelLoader::GenerateCube(1.0f, cube)) {
        throw std::runtime_error("MeshRegistry could not generate the fallback cube mesh!");
    }
    m_cubeMesh = Upload("primitive:Cube", cube);

    std::cout << "[MeshRegistry] Initialised with fallback cube mesh." << std::endl;
}

MeshRegistry::~MeshRegistry() {
    m_meshes.clear();
}

uint32_t MeshRegistry::Upload(const std::string& key, const MeshData& data) {
    if (auto it = m_lookup.find(key); it != m_lookup.end()) {
        return it->second;
    }

    if (data.empty()) {
        std::cerr << "[MeshRegistry] Refusing to upload empty mesh '" << key << "'." << std::endl;
        return m_cubeMesh;
    }

    GpuMesh mesh;
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    mesh.boundsMin = data.boundsMin;
    mesh.boundsMax = data.boundsMax;

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

    std::cout << "[MeshRegistry] Uploaded '" << key << "' (" << data.vertices.size()
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
    bool ok = false;

    if (!filePath.empty()) {
        ok = ModelLoader::LoadOBJ(filePath, data);
    } else if (primitiveType == "Cube") {
        ok = ModelLoader::GenerateCube(1.0f, data);
    } else if (primitiveType == "Sphere") {
        ok = ModelLoader::GenerateSphere(0.5f, 32, 32, data);
    } else if (primitiveType == "Plane") {
        ok = ModelLoader::GeneratePlane(1.0f, 1.0f, data);
    } else if (primitiveType == "Terrain") {
        ok = TerrainGenerator::GenerateTerrainMesh(64, 64, 0.6f, data);
    } else {
        std::cerr << "[MeshRegistry] Unknown primitive '" << primitiveType
                  << "', falling back to cube." << std::endl;
    }

    if (!ok || data.empty()) {
        // Cache the failure against the same key so a broken asset does not
        // retry generation on every single frame.
        m_lookup.emplace(key, m_cubeMesh);
        return m_cubeMesh;
    }

    return Upload(key, data);
}

const GpuMesh* MeshRegistry::Get(uint32_t id) const {
    if (id >= m_meshes.size()) return nullptr;
    return &m_meshes[id];
}

} // namespace Engine
