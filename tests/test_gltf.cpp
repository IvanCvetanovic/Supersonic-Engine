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
    CHECK_MSG(scene.ok, scene.error);
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
    CHECK_MSG(scene.ok, scene.error);
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
    CHECK_MSG(scene.ok, scene.error);
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
    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok) return;

    bool foundGold = false;
    bool foundTextured = false;

    for (const auto& sub : scene.submeshes) {
        CHECK_MSG(sub.material.present,
                  "every primitive in this file names a material");
        if (sub.material.metallic > 0.9f && sub.material.roughness < 0.3f) foundGold = true;
        if (!sub.material.albedoTexturePath.empty()) {
            foundTextured = true;
            // The URI is relative to the .gltf, and must be resolved against it.
            CHECK_MSG(sub.material.albedoTexturePath.find("uv_grid") != std::string::npos,
                      "base colour texture URI should resolve to the referenced file");
        }
    }

    CHECK_MSG(foundGold, "the metallic material's factors must be read");
    CHECK_MSG(foundTextured, "the base colour texture reference must be resolved");
}

// The base colour was read for years and then discarded one caller up. These
// are the fields that were never read at all, because nothing downstream could
// have received them: the renderer has had a normal-map descriptor slot since
// normal mapping shipped, and the importer never filled it.
static void testTheRestOfTheMaterialIsReadToo() {
    const auto scene = GltfLoader::Load(kModel);
    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok) return;

    bool foundStone = false;
    for (const auto& sub : scene.submeshes) {
        // The stone material carries a base colour factor that is not white,
        // which is exactly the value that used to be parsed and thrown away.
        if (sub.material.albedoTexturePath.empty()) continue;
        foundStone = true;

        CHECK_MSG(sub.material.baseColor.r < 0.99f && sub.material.baseColor.a > 0.99f,
                  "the stone base colour factor is off-white and opaque");
        CHECK_MSG(sub.material.roughness > 0.8f, "stone is rough");
        CHECK_MSG(!sub.material.transparent,
                  "no alphaMode means OPAQUE, which must not reach the blend pass");
        CHECK_MSG(sub.material.emissiveStrength == 0.0f,
                  "a material with no emissive factor does not glow");
    }
    CHECK(foundStone);
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

// A .glb keeps its images as bytes in the binary chunk, and every texture in
// this engine is a path: the registry opens files, hot reload watches files,
// and MaterialComponent serialises a path. So the single-file export that most
// exporters produce by default arrived with no textures at all.
//
// Extracted to cache/ now, verbatim - they are already encoded PNGs, and
// tinygltf is built with TINYGLTF_NO_STB_IMAGE so it never decoded them.
static void testEmbeddedImagesAreExtracted() {
    const std::string glb = "assets/models/embedded_textures.glb";
    const auto scene = GltfLoader::Load(glb);
    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok) return;
    CHECK_EQ(scene.submeshes.size(), size_t{1});
    if (scene.submeshes.empty()) return;

    const auto& material = scene.submeshes[0].material;
    CHECK(material.present);

    CHECK_MSG(!material.albedoTexturePath.empty(),
              "an embedded base colour image must still produce a usable path");
    CHECK_MSG(!material.normalTexturePath.empty(),
              "and so must an embedded normal map");

    CHECK_MSG(material.albedoTexturePath != material.normalTexturePath,
              "two distinct images must not extract over each other, even when "
              "they share one bufferView");

    // The point of the path is that something can open it.
    CHECK_MSG(std::filesystem::exists(material.albedoTexturePath),
              "extracted: " + material.albedoTexturePath);
    CHECK_MSG(std::filesystem::exists(material.normalTexturePath),
              "extracted: " + material.normalTexturePath);

    // Verbatim: the bytes on disk must be a PNG, not a re-encode of one.
    std::ifstream file(material.albedoTexturePath, std::ios::binary);
    CHECK(file.is_open());
    unsigned char signature[8] = {0};
    file.read(reinterpret_cast<char*>(signature), 8);
    CHECK_MSG(signature[0] == 0x89 && signature[1] == 'P' &&
              signature[2] == 'N' && signature[3] == 'G',
              "the extracted file should be the PNG the .glb was carrying");

    // The rest of the material must survive the same trip.
    CHECK(material.metallic > 0.2f && material.metallic < 0.3f);
    CHECK(material.roughness > 0.55f && material.roughness < 0.65f);
    CHECK_MSG(material.emissiveStrength == 1.0f,
              "an emissiveFactor with no strength extension means unit strength");
    CHECK(material.emissiveColor.b > 0.7f);
}

// Extracting twice must reuse rather than rewrite, and must land on the same
// path both times - a cache that moved would break every scene referencing it.
static void testExtractionIsStable() {
    const std::string glb = "assets/models/embedded_textures.glb";
    const auto first = GltfLoader::Load(glb);
    const auto second = GltfLoader::Load(glb);
    CHECK(first.ok && second.ok);
    if (!first.ok || !second.ok) return;
    if (first.submeshes.empty() || second.submeshes.empty()) return;

    CHECK_MSG(first.submeshes[0].material.albedoTexturePath ==
                  second.submeshes[0].material.albedoTexturePath,
              "the extracted path must be stable across loads");
}

static void runTests() {
    // The fixture is committed to the tree and CTest runs this suite from the
    // project root, so it is always reachable. It used to be optional: a miss
    // printed a note, ran two of seven cases and exited 0, which meant a broken
    // glTF importer looked identical to a green run. If the file is gone, that
    // is the failure - say so.
    CHECK_MSG(modelAvailable(), kModel + " is missing; run this suite from the project root");
    if (!modelAvailable()) return;

    testLoadsModel();
    testIndicesAreInRange();
    testNodeTransformsAreApplied();
    testNormalsSurviveNonUniformScale();
    testMaterialsAreRead();
    testTheRestOfTheMaterialIsReadToo();
    testEmbeddedImagesAreExtracted();
    testExtractionIsStable();
    testMissingFileFails();
    testGarbageFileFails();
}

TEST_MAIN("test_gltf", 100)
