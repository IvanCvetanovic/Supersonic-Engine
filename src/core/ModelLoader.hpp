#pragma once

#include <string>

#include "core/MeshData.hpp"

namespace Supersonic {

class ModelLoader {
public:
    // All generators validate their parameters and return false rather than
    // producing a degenerate mesh or looping on unsigned underflow.
    static bool GenerateSphere(float radius, uint32_t rings, uint32_t sectors, MeshData& out);
    static bool GenerateCube(float size, MeshData& out);
    static bool GeneratePlane(float width, float height, MeshData& out);

    // Parses positions, normals, UVs and faces. Triangulates n-gons as a fan.
    static bool LoadOBJ(const std::string& filepath, MeshData& out);
};

} // namespace Supersonic
