// Tests for the glTF 2.0 importer.
//
// tinygltf had been vendored since the "tinygltf Asset Subsystem" commit but a
// repo-wide grep found no include and no entry point, so nothing could load a
// .gltf at all. These run against assets/models/monument.gltf, which is built
// specifically to exercise node hierarchies, TRS transforms, multiple materials
// and an external texture reference.

#include "TestHarness.hpp"
#include "core/GltfLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Supersonic;

namespace {
const std::string kModel = "assets/models/monument.gltf";

bool modelAvailable() {
    return std::filesystem::exists(kModel);
}
} // namespace

static void testLoadsModel() {
    const auto scene = GltfLoader::Load(kModel);
    CHECK_MSG(scene.ok, scene.error);
    CHECK_MSG(scene.submeshes.size() == 2, "the fixture has two primitives on two nodes");
}

static void testIndicesAreInRange() {
    const auto scene = GltfLoader::Load(kModel);
    if (!scene.ok) return;

    for (const auto& sub : scene.submeshes) {
        CHECK_MSG(!sub.mesh.indices.empty(), "every primitive must produce indices");
        CHECK_MSG(sub.mesh.indices.size() % 3 == 0, "indices must form whole triangles");
        for (const uint32_t index : sub.mesh.indices) {
            CHECK_MSG(index < sub.mesh.vertices.size(), "index out of range for its primitive");
        }
    }
}

static void testNodeTransformsAreApplied() {
    // The fixture's parent is scaled 1.6 x 0.5 x 1.6 and raised to y=0.25; the
    // child sits at y=1.6 in parent space and is scaled 0.45 x 1.2 x 0.45.
    // Baking the chain is the whole point - a loader that ignored node
    // transforms would return two identical unit cubes at the origin.
    const auto scene = GltfLoader::Load(kModel);
    if (!scene.ok) return;

    const auto pedestal = std::find_if(scene.submeshes.begin(), scene.submeshes.end(),
        [](const auto& s) { return s.name == "Pedestal"; });
    const auto topper = std::find_if(scene.submeshes.begin(), scene.submeshes.end(),
        [](const auto& s) { return s.name == "Topper"; });

    CHECK(pedestal != scene.submeshes.end());
    CHECK(topper != scene.submeshes.end());
    if (pedestal == scene.submeshes.end() || topper == scene.submeshes.end()) return;

    // Parent scale 1.6 on X from a unit cube gives half-extent 0.8.
    CHECK_NEAR(pedestal->mesh.boundsMax.x, 0.8f);
    CHECK_NEAR(pedestal->mesh.boundsMin.x, -0.8f);
    // Parent scale 0.5 on Y, translated +0.25, gives [0, 0.5].
    CHECK_NEAR(pedestal->mesh.boundsMin.y, 0.0f);
    CHECK_NEAR(pedestal->mesh.boundsMax.y, 0.5f);

    // The child inherits the parent's scale, so it must sit well above it.
    CHECK_MSG(topper->mesh.boundsMin.y > pedestal->mesh.boundsMax.y,
              "the child node must inherit the parent's transform and sit above it");

    // Its own rotation about Y widens the axis-aligned bounds beyond its
    // unrotated half-extent (0.45 * 1.6 / 2 = 0.36).
    CHECK_MSG(topper->mesh.boundsMax.x > 0.37f,
              "a 45-degree Y rotation must widen the AABB");
}

static void testNormalsSurviveNonUniformScale() {
    const auto scene = GltfLoader::Load(kModel);
    if (!scene.ok) return;

    // Normals go through the inverse-transpose; using the matrix directly would
    // shear them off the surface under the fixture's non-uniform scale.
    for (const auto& sub : scene.submeshes) {
        for (const auto& v : sub.mesh.vertices) {
            CHECK_NEAR(glm::length(v.normal), 1.0f);
        }
    }
}

static void testMaterialsAreRead() {
    const auto scene = GltfLoader::Load(kModel);
    if (!scene.ok) return;

    bool foundGold = false;
    bool foundTextured = false;

    for (const auto& sub : scene.submeshes) {
        if (sub.metallic > 0.9f && sub.roughness < 0.3f) foundGold = true;
        if (!sub.albedoTexturePath.empty()) {
            foundTextured = true;
            // The URI is relative to the .gltf, and must be resolved against it.
            CHECK_MSG(sub.albedoTexturePath.find("uv_grid") != std::string::npos,
                      "base colour texture URI should resolve to the referenced file");
        }
    }

    CHECK_MSG(foundGold, "the metallic material's factors must be read");
    CHECK_MSG(foundTextured, "the base colour texture reference must be resolved");
}

static void testMissingFileFails() {
    const auto scene = GltfLoader::Load("assets/models/definitely_missing_9182.gltf");
    CHECK_MSG(!scene.ok, "a missing file must fail");
    CHECK_MSG(!scene.error.empty(), "failures must explain themselves");
}

static void testGarbageFileFails() {
    const std::string path = "test_bad_tmp.gltf";
    {
        std::ofstream f(path);
        f << "{ this is not glTF at all";
    }
    const auto scene = GltfLoader::Load(path);
    std::remove(path.c_str());

    CHECK_MSG(!scene.ok, "malformed JSON must be rejected, not partially imported");
}

static void runTests() {
    if (!modelAvailable()) {
        std::printf("  note: %s not reachable from the test cwd; skipping model cases\n", kModel.c_str());
        testMissingFileFails();
        testGarbageFileFails();
        return;
    }
    testLoadsModel();
    testIndicesAreInRange();
    testNodeTransformsAreApplied();
    testNormalsSurviveNonUniformScale();
    testMaterialsAreRead();
    testMissingFileFails();
    testGarbageFileFails();
}

TEST_MAIN("test_gltf")
