#pragma once

#include <string>
#include <vector>

#include "core/MeshData.hpp"

namespace Engine {

// glTF 2.0 importer.
//
// tinygltf has been vendored since the "tinygltf Asset Subsystem" commit, but a
// repo-wide grep for it returned nothing: no include, no entry point, no way to
// load a .gltf or .glb. Only OBJ was supported, and only positions at that.
class GltfLoader {
public:
    // One primitive from the file, already flattened into world-relative space
    // by its node chain.
    struct Submesh {
        MeshData mesh;
        std::string name;
        std::string albedoTexturePath;   // resolved next to the source file
        glm::vec4 baseColorFactor{1.0f};
        float roughness{0.5f};
        float metallic{0.0f};
    };

    struct Scene {
        std::vector<Submesh> submeshes;
        std::string error;
        bool ok{false};
    };

    // Handles both .gltf (with external or embedded buffers) and binary .glb.
    static Scene Load(const std::string& path);
};

} // namespace Engine
