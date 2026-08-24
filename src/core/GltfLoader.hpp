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

    // Which image becomes the packed occlusion/roughness/metallic map, and how
    // much of its red channel is really occlusion.
    //
    // Split out of the import so the decision can be tested without a file on
    // disk, because the decision is where the damage is. glTF says of a
    // metallic-roughness texture that "the red and alpha channels are not
    // specified and their values are ignored" - exporters write zero there, and
    // an engine that reads it as occlusion renders a valid file pitch black
    // wherever no light directly reaches it.
    struct PackedMap {
        std::string path;              // empty means the material keeps its constants
        float occlusionStrength{0.0f}; // zero means the red channel is ignored
    };

    // Both paths are already resolved, so two texture entries naming one image
    // through different samplers compare equal - which is the question that
    // matters: do the same PIXELS carry both.
    static PackedMap ChoosePackedMap(const std::string& metallicRoughnessPath,
                                     const std::string& occlusionPath,
                                     float gltfOcclusionStrength);
};

} // namespace Supersonic
