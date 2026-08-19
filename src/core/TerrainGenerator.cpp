#include "core/TerrainGenerator.hpp"
#include "core/Log.hpp"
#include "core/JobSystem.hpp"

#include <cmath>
#include <iostream>

namespace Supersonic {

bool TerrainGenerator::GenerateTerrainMesh(uint32_t width, uint32_t height, float heightScale, MeshData& out) {
    out.clear();

    // The index loops run to width - 1 / height - 1. On an unsigned type a
    // value of 0 wraps to ~4.29 billion and the generator allocates until it
    // throws bad_alloc.
    if (width < 2 || height < 2) {
        SUPERSONIC_LOG_ERROR("TerrainGenerator") << "Requires width and height >= 2 (got "
                  << width << "x" << height << ")." << std::endl;
        return false;
    }

    const float halfW = static_cast<float>(width) * 0.5f;
    const float halfH = static_cast<float>(height) * 0.5f;

    // Resized up front and written by index rather than appended, so the work
    // can be split: every vertex is a pure function of its own grid position,
    // with nothing shared to contend over. A 512x512 grid is 262,144 vertices,
    // each costing a sin, a cos and a normalize.
    const uint32_t vertexCount = width * height;
    out.vertices.resize(vertexCount);

    JobSystem::Dispatch(vertexCount, 4096u, [&out, width, height, halfW, halfH, heightScale](JobSystem::JobArgs args) {
        const uint32_t index = args.jobIndex;
        const uint32_t x = index % width;
        const uint32_t z = index / width;

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
        out.vertices[index] = vertex;
    });
    JobSystem::Wait();

    const uint32_t quadsX = width - 1;
    const uint32_t quadCount = quadsX * (height - 1);
    out.indices.resize(static_cast<size_t>(quadCount) * 6);

    JobSystem::Dispatch(quadCount, 4096u, [&out, width, quadsX](JobSystem::JobArgs args) {
        const uint32_t quad = args.jobIndex;
        const uint32_t x = quad % quadsX;
        const uint32_t z = quad / quadsX;

        const uint32_t topLeft = z * width + x;
        const uint32_t topRight = z * width + (x + 1);
        const uint32_t bottomLeft = (z + 1) * width + x;
        const uint32_t bottomRight = (z + 1) * width + (x + 1);

        // Each quad owns exactly six slots, so the winding order is identical to
        // the serial version rather than depending on completion order.
        const size_t base = static_cast<size_t>(quad) * 6;
        out.indices[base + 0] = topLeft;
        out.indices[base + 1] = bottomLeft;
        out.indices[base + 2] = topRight;
        out.indices[base + 3] = topRight;
        out.indices[base + 4] = bottomLeft;
        out.indices[base + 5] = bottomRight;
    });
    JobSystem::Wait();

    out.computeTangents();
    out.computeBounds();
    SUPERSONIC_LOG_INFO("TerrainGenerator") << "Generated terrain mesh (" << out.vertices.size()
              << " vertices, " << out.indices.size() / 3 << " triangles)." << std::endl;
    return true;
}

} // namespace Supersonic
