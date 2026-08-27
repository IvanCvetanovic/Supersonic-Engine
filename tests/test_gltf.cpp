// Tests for the glTF 2.0 importer.
//
// tinygltf had been vendored since the "tinygltf Asset Subsystem" commit but a
// repo-wide grep found no include and no entry point, so nothing could load a
// .gltf at all. These run against assets/models/monument.gltf, which is built
// specifically to exercise node hierarchies, TRS transforms, multiple materials
// and an external texture reference.

#include "TestHarness.hpp"
#include "core/GltfLoader.hpp"
#include "core/AnimationSystem.hpp"
#include "core/Skeleton.hpp"

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
//
// The write time is what makes this a test of REUSE. Comparing only the two
// returned paths would pass just as well against a loader that rewrote the file
// on every single load, which is the soft pass this suite has been caught by
// before.
static void testExtractionIsReusedNotRewritten() {
    const std::string glb = "assets/models/embedded_textures.glb";

    const auto first = GltfLoader::Load(glb);
    CHECK_MSG(first.ok, first.error);
    if (!first.ok || first.submeshes.empty()) return;

    const std::string extracted = first.submeshes[0].material.albedoTexturePath;
    CHECK(!extracted.empty());
    if (extracted.empty()) return;

    std::error_code ec;
    const auto writtenAt = std::filesystem::last_write_time(extracted, ec);
    CHECK(!ec);

    const auto second = GltfLoader::Load(glb);
    CHECK_MSG(second.ok, second.error);
    if (!second.ok || second.submeshes.empty()) return;

    CHECK_MSG(second.submeshes[0].material.albedoTexturePath == extracted,
              "the extracted path must be stable across loads");

    const auto stillWrittenAt = std::filesystem::last_write_time(extracted, ec);
    CHECK(!ec);
    CHECK_MSG(stillWrittenAt == writtenAt,
              "a second load must reuse the extraction, not redo it");
}

// Two models with the same FILENAME in different folders must not extract over
// each other. The stem alone is not a key, and combined with the freshness
// check the failure is worse than last-write-wins: load the older model second,
// find a cache file newer than its own source, reuse it, and render one model
// with the other's texture with nothing logged.
static void testTwoModelsNamedTheSameDoNotCollide() {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path root = fs::temp_directory_path() / "supersonic_gltf_collision";
    fs::remove_all(root, ec);
    const fs::path a = root / "enemies";
    const fs::path b = root / "npcs";
    fs::create_directories(a, ec);
    fs::create_directories(b, ec);

    // The same bytes under the same filename, in two directories.
    fs::copy_file("assets/models/embedded_textures.glb", a / "character.glb",
                  fs::copy_options::overwrite_existing, ec);
    CHECK(!ec);
    fs::copy_file("assets/models/embedded_textures.glb", b / "character.glb",
                  fs::copy_options::overwrite_existing, ec);
    CHECK(!ec);

    const auto first = GltfLoader::Load((a / "character.glb").string());
    const auto second = GltfLoader::Load((b / "character.glb").string());
    CHECK_MSG(first.ok, first.error);
    CHECK_MSG(second.ok, second.error);
    if (first.submeshes.empty() || second.submeshes.empty()) { fs::remove_all(root, ec); return; }

    const std::string pathA = first.submeshes[0].material.albedoTexturePath;
    const std::string pathB = second.submeshes[0].material.albedoTexturePath;
    CHECK(!pathA.empty() && !pathB.empty());
    CHECK_MSG(pathA != pathB,
              "two models sharing a filename must extract to different files: " + pathA);
    CHECK(fs::exists(pathA) && fs::exists(pathB));

    fs::remove(pathA, ec);
    fs::remove(pathB, ec);
    fs::remove(first.submeshes[0].material.normalTexturePath, ec);
    fs::remove(second.submeshes[0].material.normalTexturePath, ec);
    fs::remove_all(root, ec);
}

// The extracted name must be a function of the path AS ADDRESSED, not of where
// the project happens to sit on this machine.
//
// A material serialises the extracted texture's path, so the name has to come
// out identical on the machine that authored the scene and in the folder the
// game ships to. An earlier draft hashed the canonical ABSOLUTE path: the
// editor wrote one name into the scene, the packaged copy of the same model
// extracted itself under another, and the game rendered the missing-texture
// checkerboard. It passed every test in this file, and was only visible by
// packaging the thing and running it.
//
// A literal, deliberately. Any machine-dependent input to the name makes this
// fail on the next machine that runs it, which is exactly the alarm wanted.
static void testTheExtractedNameIsPortable() {
    const auto scene = GltfLoader::Load("assets/models/embedded_textures.glb");
    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok || scene.submeshes.empty()) return;

    const std::string expected = "cache/gltf/embedded_textures-01c42191-image0.png";
    const std::string actual =
        std::filesystem::path(scene.submeshes[0].material.albedoTexturePath)
            .lexically_normal().generic_string();

    CHECK_MSG(actual == expected,
              "extracted name must not depend on this machine's directory layout; "
              "got " + actual);
}

// The packed-map decision, tested without a file on disk.
//
// This is the one place in the importer where believing the format costs you a
// black surface. glTF says of a metallic-roughness texture that "the red and
// alpha channels are not specified and their values are ignored" - so an
// exporter may write zero there, and an engine that reads red as occlusion
// renders a perfectly valid file pitch black wherever no light directly
// reaches it, with nothing anywhere to say why.
static void testWhatTheRedChannelIsAllowedToMean() {
    using Choice = GltfLoader::PackedMap;

    // Both slots, one image: the arrangement almost every exporter writes, and
    // the only one where red really is occlusion.
    const Choice both = GltfLoader::ChoosePackedMap("orm.png", "orm.png", 1.0f);
    CHECK_MSG(both.path == "orm.png", both.path);
    CHECK_NEAR(both.occlusionStrength, 1.0f);

    // And the file's own strength is honoured rather than assumed.
    CHECK_NEAR(GltfLoader::ChoosePackedMap("orm.png", "orm.png", 0.4f).occlusionStrength, 0.4f);

    // Metallic-roughness alone. The map is taken for its green and blue, and
    // the red channel is refused - this is the case that renders black.
    const Choice mrOnly = GltfLoader::ChoosePackedMap("mr.png", "", 1.0f);
    CHECK_MSG(mrOnly.path == "mr.png", "the roughness and metallic channels are still wanted");
    CHECK_MSG(mrOnly.occlusionStrength == 0.0f,
              "red is undefined in a metallic-roughness image and must not be believed");

    // Two different images. One packed slot cannot hold both, so occlusion is
    // dropped rather than read out of a channel that does not carry it.
    const Choice split = GltfLoader::ChoosePackedMap("mr.png", "ao.png", 1.0f);
    CHECK_MSG(split.path == "mr.png", split.path);
    CHECK_MSG(split.occlusionStrength == 0.0f, "a strength of zero is how red is ignored");

    // Occlusion alone is not imported at all. An AO bake is greyscale, so its
    // green and blue would drive roughness and metallic too, and every crevice
    // would come out smoother and less dielectric than the surface around it.
    const Choice aoOnly = GltfLoader::ChoosePackedMap("", "ao.png", 1.0f);
    CHECK_MSG(aoOnly.path.empty(),
              "a greyscale occlusion bake would drive roughness and metallic as well");

    const Choice neither = GltfLoader::ChoosePackedMap("", "", 1.0f);
    CHECK_MSG(neither.path.empty(), "no map is no map");
    CHECK_MSG(neither.occlusionStrength == 0.0f, "and nothing to believe in it");
}

// --- Baked vertex colour -------------------------------------------------
//
// Every vertex used to come out white, unconditionally: the loader read
// positions, normals, UVs, tangents and skinning influences, and then assigned
// glm::vec3(1.0f) over the colour. COLOR_0 appeared nowhere in the engine.
//
// For a model library that bakes its shading into the mesh rather than into a
// texture - which is what a Blender-authored, AO-baked pipeline produces - that
// is the entire look discarded with nothing to point at. The geometry is right,
// the materials are right, and everything is flat.
//
// Written to a temp file rather than added to assets/, so this suite keeps
// working on a machine with no art checked out.

// Three vertices, one triangle, POSITION plus COLOR_0 as NORMALISED UNSIGNED
// BYTE - which is how exporters actually write vertex colour, and the case a
// float-only reader would silently drop.
//
// Red, green, blue at the three corners: 255/0/0, 0/255/0, 0/0/255.
const char* kColoredTriangleGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "COLOR_0": 1 },
      "indices": 2
  } ] } ],
  "buffers": [ {
      "byteLength": 56,
      "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA/wAA/wD/AP8AAP//AAABAAIAAAA="
  } ],
  "bufferViews": [
      { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
      { "buffer": 0, "byteOffset": 36, "byteLength": 12 },
      { "buffer": 0, "byteOffset": 48, "byteLength": 6 }
  ],
  "accessors": [
      { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
        "min": [0,0,0], "max": [1,1,0] },
      { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4",
        "normalized": true },
      { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})";

static std::string writeTempGltf(const std::string& name, const char* json) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << json;
    out.close();
    return path.string();
}

static void testBakedVertexColourSurvivesTheImport() {
    const std::string path = writeTempGltf("supersonic_color0.gltf", kColoredTriangleGltf);

    const auto scene = GltfLoader::Load(path);
    std::filesystem::remove(path);

    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok || scene.submeshes.empty()) return;

    const auto& mesh = scene.submeshes[0].mesh;
    CHECK_EQ(static_cast<int>(mesh.vertices.size()), 3);
    if (mesh.vertices.size() != 3) return;

    // Normalised unsigned byte: 255 becomes 1.0 and 0 becomes 0.0. A reader
    // that took the raw integer would give 255.0 here and a reader that
    // ignored the attribute would give white.
    CHECK_NEAR(mesh.vertices[0].color.r, 1.0f);
    CHECK_NEAR(mesh.vertices[0].color.g, 0.0f);
    CHECK_NEAR(mesh.vertices[0].color.b, 0.0f);

    CHECK_NEAR(mesh.vertices[1].color.g, 1.0f);
    CHECK_NEAR(mesh.vertices[1].color.r, 0.0f);

    CHECK_NEAR(mesh.vertices[2].color.b, 1.0f);
    CHECK_NEAR(mesh.vertices[2].color.g, 0.0f);

    CHECK_MSG(!(mesh.vertices[0].color == mesh.vertices[1].color),
              "the three corners are genuinely different, not all one value");
}

static void testAMeshWithNoVertexColourIsStillWhite() {
    // The identity for a multiply, so every model that predates this reads
    // exactly as it did. Asserted rather than assumed, because the natural
    // mistake when adding an attribute is to leave it at zero - which is black,
    // and would turn every existing model in the library off.
    const char* plain = R"({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [ 0 ] } ],
      "nodes": [ { "mesh": 0 } ],
      "meshes": [ { "primitives": [ {
          "attributes": { "POSITION": 0 }, "indices": 1
      } ] } ],
      "buffers": [ {
          "byteLength": 56,
          "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA/wAA/wD/AP8AAP//AAABAAIAAAA="
      } ],
      "bufferViews": [
          { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
          { "buffer": 0, "byteOffset": 48, "byteLength": 6 }
      ],
      "accessors": [
          { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
            "min": [0,0,0], "max": [1,1,0] },
          { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
      ]
    })";

    const std::string path = writeTempGltf("supersonic_nocolor.gltf", plain);

    const auto scene = GltfLoader::Load(path);
    std::filesystem::remove(path);

    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok || scene.submeshes.empty()) return;

    const auto& mesh = scene.submeshes[0].mesh;
    if (mesh.vertices.empty()) return;

    CHECK_NEAR(mesh.vertices[0].color.r, 1.0f);
    CHECK_NEAR(mesh.vertices[0].color.g, 1.0f);
    CHECK_NEAR(mesh.vertices[0].color.b, 1.0f);
}


// A file that animates its NODES and has no skin - the shape almost all
// hand-built content takes, and the shape the importer used to drop on the
// floor. Buffer holds 3 positions then 3 indices, then the animation's
// times (2 floats) and translations (2 vec3).
const char* kNodeRigGltf = R"({
  "asset": { "version": "2.0" },
  "scenes": [ { "nodes": [0] } ],
  "scene": 0,
  "nodes": [
    { "name": "arm",   "translation": [5, 0, 0], "children": [1] },
    { "name": "plate", "translation": [2, 0, 0], "mesh": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1 } ] } ],
  "animations": [ {
    "name": "swing",
    "channels": [ { "sampler": 0, "target": { "node": 0, "path": "translation" } } ],
    "samplers": [ { "input": 2, "output": 3, "interpolation": "LINEAR" } ]
  } ],
  "buffers": [ { "byteLength": 76, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAAAAAAAAACAPwAAoEAAAAAAAAAAAAAAoEAAACBBAAAAAA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6  },
    { "buffer": 0, "byteOffset": 44, "byteLength": 8  },
    { "buffer": 0, "byteOffset": 52, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0], "max": [1] },
    { "bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3" }
  ]
})";

static void testAFileThatAnimatesNodesAndHasNoSkinStillAnimates() {
    // The measured motivation: of HUSK's 58 models, 19 are animated and NOT ONE
    // has a skin, so the importer produced no clips whatsoever for that game.
    // A file with no skin is not an error, so nothing said a word.
    const std::string path = writeTempGltf("supersonic_noderig.gltf", kNodeRigGltf);
    const auto scene = GltfLoader::Load(path);
    std::filesystem::remove(path);

    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok) return;

    CHECK_MSG(scene.clips.size() == 1, "the node animation became a clip");
    CHECK_MSG(scene.skeletons.size() == 1, "and a skeleton was built out of the node graph");
    if (scene.clips.empty() || scene.skeletons.empty()) return;

    const Skeleton& skeleton = scene.skeletons[0];
    // One animated node, plus the stationary joint everything unrigged binds to.
    CHECK_MSG(skeleton.joints.size() == 2, "one animated node and one stationary joint");
    if (skeleton.joints.size() != 2) return;

    // THE CANCELLATION THE WHOLE DESIGN RESTS ON. The vertices are baked into
    // world space, so the joint's inverse bind must be the inverse of that same
    // world matrix - here a translation of -5 against a rest of +5.
    const Joint& arm = skeleton.joints[0];
    CHECK_NEAR(arm.restTranslation.x, 5.0f);
    CHECK_NEAR(arm.inverseBind[3][0], -5.0f);

    // The geometry really was baked: the triangle's first vertex is authored at
    // the origin and its node chain is 5 + 2.
    bool foundPlate = false;
    for (const auto& submesh : scene.submeshes) {
        if (submesh.name != "plate") continue;
        foundPlate = true;
        CHECK_MSG(submesh.skinIndex == 0, "the primitive belongs to the node rig");
        CHECK_MSG(!submesh.mesh.vertices.empty(), "and it has vertices");
        if (submesh.mesh.vertices.empty()) break;

        CHECK_NEAR(submesh.mesh.vertices[0].pos.x, 7.0f);

        // Bound to the arm, at full weight, with nothing left for the others -
        // which is what makes a rigid piece follow one node exactly.
        CHECK_MSG(submesh.mesh.vertices[0].jointIndices[0] == 0,
                  "every vertex follows the node above it");
        CHECK_NEAR(submesh.mesh.vertices[0].jointWeights.x, 1.0f);
        CHECK_NEAR(submesh.mesh.vertices[0].jointWeights.y, 0.0f);
    }
    CHECK_MSG(foundPlate, "the mesh node survived the walk");

    // And now the part a "clips.size() > 0" check cannot tell you: whether the
    // maths is right. Drive the real pose pipeline and read the joint matrix.
    std::vector<JointPose> pose;
    std::vector<glm::mat4> locals;
    std::vector<glm::mat4> matrices;

    AnimationSystem::SamplePose(skeleton, scene.clips[0], 0.0f, pose);
    AnimationSystem::PoseToLocals(pose, locals);
    AnimationSystem::ComposePose(skeleton, locals, matrices);
    CHECK_MSG(matrices.size() == 2, "a matrix per joint");
    if (matrices.size() != 2) return;

    // AT REST THE JOINT MATRIX IS THE IDENTITY. If it is not, the bake and the
    // inverse bind disagree and every animated prop starts life in the wrong
    // place - which looks like a broken export, not a broken importer.
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_NEAR(matrices[0][c][r], c == r ? 1.0f : 0.0f);
        }
    }

    // At the far keyframe it is a pure ten along Y - the authored motion, and
    // nothing added by the node's own five along X.
    AnimationSystem::SamplePose(skeleton, scene.clips[0], 1.0f, pose);
    AnimationSystem::PoseToLocals(pose, locals);
    AnimationSystem::ComposePose(skeleton, locals, matrices);
    CHECK_NEAR(matrices[0][3][0], 0.0f);
    CHECK_NEAR(matrices[0][3][1], 10.0f);
    CHECK_NEAR(matrices[0][3][2], 0.0f);

    // The stationary joint stays the identity at every time, which is what lets
    // unrigged geometry in a rigged file be merged rather than skipped.
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_NEAR(matrices[1][c][r], c == r ? 1.0f : 0.0f);
        }
    }
}

static void testAFileWithNoAnimationIsUntouched() {
    // The regression this could most easily cause: every rigid model in the
    // project acquiring a skeleton it does not need, a joint index it did not
    // have, and a skin index that changes how MeshRegistry merges it.
    const auto scene = GltfLoader::Load(kModel);
    CHECK_MSG(scene.ok, scene.error);
    if (!scene.ok) return;

    CHECK_MSG(scene.skeletons.empty(), "an unanimated file gets no skeleton");
    CHECK_MSG(scene.clips.empty(), "and no clips");
    for (const auto& submesh : scene.submeshes) {
        CHECK_MSG(submesh.skinIndex == -1, "and its primitives stay unskinned");
        break;
    }
}

static void runTests() {
    testAFileThatAnimatesNodesAndHasNoSkinStillAnimates();
    testAFileWithNoAnimationIsUntouched();
    testBakedVertexColourSurvivesTheImport();
    testAMeshWithNoVertexColourIsStillWhite();
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
    testExtractionIsReusedNotRewritten();
    testTwoModelsNamedTheSameDoNotCollide();
    testTheExtractedNameIsPortable();
    testMissingFileFails();
    testGarbageFileFails();
    testWhatTheRedChannelIsAllowedToMean();
}

TEST_MAIN("test_gltf", 250)
