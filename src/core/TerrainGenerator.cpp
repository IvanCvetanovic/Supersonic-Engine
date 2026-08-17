#include "core/TerrainGenerator.hpp"

#include <cmath>
#include <iostream>

namespace Supersonic {

bool TerrainGenerator::GenerateTerrainMesh(uint32_t width, uint32_t height, float heightScale, MeshData& out) {
    out.clear();

    // The index loops run to width - 1 / height - 1. On an unsigned type a
    // value of 0 wraps to ~4.29 billion and the generator allocates until it
    // throws bad_alloc.
    if (width < 2 || height < 2) {
        std::cerr << "[TerrainGenerator] Requires width and height >= 2 (got "
                  << width << "x" << height << ")." << std::endl;
        return false;
    }

    const float halfW = static_cast<float>(width) * 0.5f;
    const float halfH = static_cast<float>(height) * 0.5f;

    out.vertices.reserve(static_cast<size_t>(width) * height);
    for (uint32_t z = 0; z < height; ++z) {
        for (uint32_t x = 0; x < width; ++x) {
            const float fx = static_cast<float>(x) - halfW;
            const float fz = static_cast<float>(z) - halfH;
            const float fy = (std::sin(fx * 0.2f) + std::cos(fz * 0.2f)) * heightScale;

            Vertex vertex{};
            vertex.pos = glm::vec3(fx, fy, fz);
            // Analytic normal: the surface is y = (sin(0.2x) + cos(0.2z)) * s,
            // so dy/dx = 0.2s*cos(0.2x) and dy/dz = -0.2s*sin(0.2z).
            vertex.normal = glm::normalize(glm::vec3(
                -0.2f * heightScale * std::cos(fx * 0.2f),
                1.0f,
                 0.2f * heightScale * std::sin(fz * 0.2f)));
            vertex.color = glm::vec3(0.25f, 0.65f, 0.35f);
            vertex.texCoord = glm::vec2(static_cast<float>(x) / static_cast<float>(width),
                                        static_cast<float>(z) / static_cast<float>(height));
            out.vertices.push_back(vertex);
        }
    }

    out.indices.reserve(static_cast<size_t>(width - 1) * (height - 1) * 6);
    for (uint32_t z = 0; z + 1 < height; ++z) {
        for (uint32_t x = 0; x + 1 < width; ++x) {
            const uint32_t topLeft = z * width + x;
            const uint32_t topRight = z * width + (x + 1);
            const uint32_t bottomLeft = (z + 1) * width + x;
            const uint32_t bottomRight = (z + 1) * width + (x + 1);

            out.indices.insert(out.indices.end(),
                { topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight });
        }
    }

    out.computeTangents();
    out.computeBounds();
    std::cout << "[TerrainGenerator] Generated terrain mesh (" << out.vertices.size()
              << " vertices, " << out.indices.size() / 3 << " triangles)." << std::endl;
    return true;
}

} // namespace Supersonic
