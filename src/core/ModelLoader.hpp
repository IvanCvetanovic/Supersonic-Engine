#pragma once

#include <string>
#include <vector>
#include "core/Components.hpp"

namespace Engine {

struct LoadedMeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

class ModelLoader {
public:
    static bool GenerateSphere(float radius, uint32_t rings, uint32_t sectors, LoadedMeshData& outMeshData);
    static bool GenerateCube(float size, LoadedMeshData& outMeshData);
    static bool GeneratePlane(float width, float height, LoadedMeshData& outMeshData);
    static bool LoadOBJ(const std::string& filepath, LoadedMeshData& outMeshData);
};

} // namespace Engine
