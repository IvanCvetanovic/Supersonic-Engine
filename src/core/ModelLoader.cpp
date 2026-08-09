#include "core/ModelLoader.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Engine {

constexpr float PI = 3.14159265359f;

bool ModelLoader::GenerateSphere(float radius, uint32_t rings, uint32_t sectors, LoadedMeshData& outMeshData) {
    outMeshData.vertices.clear();
    outMeshData.indices.clear();

    float const R = 1.0f / static_cast<float>(rings - 1);
    float const S = 1.0f / static_cast<float>(sectors - 1);

    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            float y = sin(-PI / 2.0f + PI * r * R);
            float x = cos(2.0f * PI * s * S) * sin(PI * r * R);
            float z = sin(2.0f * PI * s * S) * sin(PI * r * R);

            Vertex vertex{};
            vertex.pos = glm::vec3(x * radius, y * radius, z * radius);
            vertex.normal = glm::normalize(vertex.pos);
            vertex.color = glm::vec3(0.9f, 0.9f, 0.95f);
            vertex.texCoord = glm::vec2(s * S, r * R);

            outMeshData.vertices.push_back(vertex);
        }
    }

    for (uint32_t r = 0; r < rings - 1; ++r) {
        for (uint32_t s = 0; s < sectors - 1; ++s) {
            uint16_t idx0 = static_cast<uint16_t>(r * sectors + s);
            uint16_t idx1 = static_cast<uint16_t>(r * sectors + (s + 1));
            uint16_t idx2 = static_cast<uint16_t>((r + 1) * sectors + (s + 1));
            uint16_t idx3 = static_cast<uint16_t>((r + 1) * sectors + s);

            outMeshData.indices.push_back(idx0);
            outMeshData.indices.push_back(idx1);
            outMeshData.indices.push_back(idx2);

            outMeshData.indices.push_back(idx0);
            outMeshData.indices.push_back(idx2);
            outMeshData.indices.push_back(idx3);
        }
    }

    std::cout << "[ModelLoader] Generated Sphere mesh (" << outMeshData.vertices.size() << " vertices)." << std::endl;
    return true;
}

bool ModelLoader::GenerateCube(float size, LoadedMeshData& outMeshData) {
    outMeshData.vertices.clear();
    outMeshData.indices.clear();

    float halfSize = size * 0.5f;

    // 24 Vertices for 6 Faces with distinct Normals
    static const std::vector<Vertex> vertices = {
        // Front face (Z = +halfSize, Normal = {0, 0, 1})
        {{-halfSize, -halfSize,  halfSize}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f}},
        {{ halfSize, -halfSize,  halfSize}, {0.0f, 0.0f, 1.0f}, {0.2f, 1.0f, 0.2f}, {1.0f, 0.0f}},
        {{ halfSize,  halfSize,  halfSize}, {0.0f, 0.0f, 1.0f}, {0.2f, 0.2f, 1.0f}, {1.0f, 1.0f}},
        {{-halfSize,  halfSize,  halfSize}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.2f}, {0.0f, 1.0f}},
        // Back face (Z = -halfSize, Normal = {0, 0, -1})
        {{ halfSize, -halfSize, -halfSize}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.2f, 1.0f}, {0.0f, 0.0f}},
        {{-halfSize, -halfSize, -halfSize}, {0.0f, 0.0f, -1.0f}, {0.2f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-halfSize,  halfSize, -halfSize}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{ halfSize,  halfSize, -halfSize}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}},
        // Top face (Y = -halfSize, Normal = {0, -1, 0})
        {{-halfSize, -halfSize, -halfSize}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.4f, 0.4f}, {0.0f, 0.0f}},
        {{ halfSize, -halfSize, -halfSize}, {0.0f, -1.0f, 0.0f}, {0.4f, 1.0f, 0.4f}, {1.0f, 0.0f}},
        {{ halfSize, -halfSize,  halfSize}, {0.0f, -1.0f, 0.0f}, {0.4f, 0.4f, 1.0f}, {1.0f, 1.0f}},
        {{-halfSize, -halfSize,  halfSize}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.4f}, {0.0f, 1.0f}},
        // Bottom face (Y = +halfSize, Normal = {0, 1, 0})
        {{-halfSize,  halfSize,  halfSize}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.3f, 0.3f}, {0.0f, 0.0f}},
        {{ halfSize,  halfSize,  halfSize}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.8f, 0.3f}, {1.0f, 0.0f}},
        {{ halfSize,  halfSize, -halfSize}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.3f, 0.8f}, {1.0f, 1.0f}},
        {{-halfSize,  halfSize, -halfSize}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.3f}, {0.0f, 1.0f}},
        // Right face (X = +halfSize, Normal = {1, 0, 0})
        {{ halfSize, -halfSize,  halfSize}, {1.0f, 0.0f, 0.0f}, {0.9f, 0.5f, 0.2f}, {0.0f, 0.0f}},
        {{ halfSize, -halfSize, -halfSize}, {1.0f, 0.0f, 0.0f}, {0.2f, 0.9f, 0.5f}, {1.0f, 0.0f}},
        {{ halfSize,  halfSize, -halfSize}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.2f, 0.9f}, {1.0f, 1.0f}},
        {{ halfSize,  halfSize,  halfSize}, {1.0f, 0.0f, 0.0f}, {0.9f, 0.9f, 0.2f}, {0.0f, 1.0f}},
        // Left face (X = -halfSize, Normal = {-1, 0, 0})
        {{-halfSize, -halfSize, -halfSize}, {-1.0f, 0.0f, 0.0f}, {0.2f, 0.6f, 0.9f}, {0.0f, 0.0f}},
        {{-halfSize, -halfSize,  halfSize}, {-1.0f, 0.0f, 0.0f}, {0.9f, 0.2f, 0.6f}, {1.0f, 0.0f}},
        {{-halfSize,  halfSize,  halfSize}, {-1.0f, 0.0f, 0.0f}, {0.6f, 0.9f, 0.2f}, {1.0f, 1.0f}},
        {{-halfSize,  halfSize, -halfSize}, {-1.0f, 0.0f, 0.0f}, {0.2f, 0.9f, 0.6f}, {0.0f, 1.0f}}
    };

    static const std::vector<uint16_t> indices = {
         0,  1,  2,  2,  3,  0, // Front
         4,  5,  6,  6,  7,  4, // Back
         8,  9, 10, 10, 11,  8, // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        20, 21, 22, 22, 23, 20  // Left
    };

    outMeshData.vertices = vertices;
    outMeshData.indices = indices;

    std::cout << "[ModelLoader] Generated Cube mesh." << std::endl;
    return true;
}

bool ModelLoader::GeneratePlane(float width, float height, LoadedMeshData& outMeshData) {
    outMeshData.vertices.clear();
    outMeshData.indices.clear();

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    outMeshData.vertices = {
        {{-halfW, 0.0f, -halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {0.0f, 0.0f}},
        {{ halfW, 0.0f, -halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {1.0f, 0.0f}},
        {{ halfW, 0.0f,  halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {1.0f, 1.0f}},
        {{-halfW, 0.0f,  halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, {0.0f, 1.0f}}
    };

    outMeshData.indices = { 0, 1, 2, 2, 3, 0 };

    std::cout << "[ModelLoader] Generated Plane mesh." << std::endl;
    return true;
}

bool ModelLoader::LoadOBJ(const std::string& filepath, LoadedMeshData& outMeshData) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[ModelLoader] Failed to open OBJ file: " << filepath << std::endl;
        return false;
    }

    std::vector<glm::vec3> tempPositions;
    std::vector<glm::vec3> tempNormals;
    std::vector<glm::vec2> tempUVs;

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        } else if (prefix == "vn") {
            glm::vec3 norm;
            ss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);
        } else if (prefix == "vt") {
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            tempUVs.push_back(uv);
        }
    }

    // Default fallback cube if obj is empty
    if (tempPositions.empty()) {
        return GenerateCube(1.0f, outMeshData);
    }

    for (size_t i = 0; i < tempPositions.size(); i++) {
        Vertex vertex{};
        vertex.pos = tempPositions[i];
        vertex.normal = i < tempNormals.size() ? tempNormals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
        vertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
        vertex.texCoord = i < tempUVs.size() ? tempUVs[i] : glm::vec2(0.0f, 0.0f);

        outMeshData.vertices.push_back(vertex);
        outMeshData.indices.push_back(static_cast<uint16_t>(i));
    }

    std::cout << "[ModelLoader] Parsed OBJ file " << filepath << " (" << outMeshData.vertices.size() << " vertices)." << std::endl;
    return true;
}

} // namespace Engine
