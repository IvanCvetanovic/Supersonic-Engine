#pragma once

#include "core/MeshData.hpp"

namespace Supersonic {

class TerrainGenerator {
public:
    // width/height are vertex counts per axis, so both must be at least 2 to
    // produce a single quad. Returns false rather than looping on underflow.
    static bool GenerateTerrainMesh(uint32_t width, uint32_t height, float heightScale, MeshData& out);
};

} // namespace Supersonic
