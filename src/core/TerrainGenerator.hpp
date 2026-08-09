#pragma once

#include <vector>
#include "core/Components.hpp"

namespace Engine {

class TerrainGenerator {
public:
    static bool GenerateTerrainMesh(
        uint32_t width,
        uint32_t height,
        float heightScale,
        std::vector<Vertex>& outVertices,
        std::vector<uint16_t>& outIndices
    );
};

} // namespace Engine
