#include "core/TerrainGenerator.hpp"
#include <cmath>
#include <iostream>

namespace Engine {

bool TerrainGenerator::GenerateTerrainMesh(
    uint32_t width,
    uint32_t height,
    float heightScale,
    std::vector<Vertex>& outVertices,
    std::vector<uint16_t>& outIndices) {

    outVertices.clear();
    outIndices.clear();

    float halfW = static_cast<float>(width) * 0.5f;
    float halfH = static_cast<float>(height) * 0.5f;

    for (uint32_t z = 0; z < height; z++) {
        for (uint32_t x = 0; x < width; x++) {
            float fx = static_cast<float>(x) - halfW;
            float fz = static_cast<float>(z) - halfH;
            float fy = (std::sin(fx * 0.2f) + std::cos(fz * 0.2f)) * heightScale;

            Vertex vertex{};
            vertex.pos = glm::vec3(fx, fy, fz);
            vertex.normal = glm::normalize(glm::vec3(-std::cos(fx * 0.2f) * 0.2f * heightScale, 1.0f, std::sin(fz * 0.2f) * 0.2f * heightScale));
            vertex.color = glm::vec3(0.25f, 0.65f, 0.35f); // Terrain grass green
            vertex.texCoord = glm::vec2(static_cast<float>(x) / width, static_cast<float>(z) / height);

            outVertices.push_back(vertex);
        }
    }

    for (uint32_t z = 0; z < height - 1; z++) {
        for (uint32_t x = 0; x < width - 1; x++) {
            uint16_t topLeft = static_cast<uint16_t>(z * width + x);
            uint16_t topRight = static_cast<uint16_t>(z * width + (x + 1));
            uint16_t bottomLeft = static_cast<uint16_t>((z + 1) * width + x);
            uint16_t bottomRight = static_cast<uint16_t>((z + 1) * width + (x + 1));

            outIndices.push_back(topLeft);
            outIndices.push_back(bottomLeft);
            outIndices.push_back(topRight);

            outIndices.push_back(topRight);
            outIndices.push_back(bottomLeft);
            outIndices.push_back(bottomRight);
        }
    }

    std::cout << "[TerrainGenerator] Generated 3D Terrain mesh (" << outVertices.size() << " vertices)." << std::endl;
    return true;
}

} // namespace Engine
