#pragma once

#include <string>
#include <vector>

#include "core/MeshData.hpp"
#include "core/Skeleton.hpp"

namespace Supersonic {

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
        // What the file said this primitive's surface is. Carried through to
        // the mesh registry, which used to copy the geometry out of here and
        // drop everything else on the floor.
        MeshMaterial material;

        // Index into Scene::skeletons, or -1 for a rigid primitive. A skinned
        // primitive is NOT baked into its node's world space: the inverse bind
        // matrices are authored in the skin's own space, and the glTF spec says
        // the skinned mesh node's transform must be ignored outright.
        int32_t skinIndex{-1};
    };

    struct Scene {
        std::vector<Submesh> submeshes;

        // One per glTF skin, joints already reordered parent-before-child.
        std::vector<Skeleton> skeletons;
        std::vector<AnimationClip> clips;

        std::string error;
        bool ok{false};
    };

    // Handles both .gltf (with external or embedded buffers) and binary .glb.
    static Scene Load(const std::string& path);
};

} // namespace Supersonic
