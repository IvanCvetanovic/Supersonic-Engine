// Regression tests for shared material assets.
//
// Materials used to be authored per entity: every object carried its own copy
// of the same numbers, and retuning a look meant editing each one by hand with
// no way to tell which were meant to match.
//
// The failure modes of the fix are all quiet. A scene that saves the resolved
// numbers but not the asset link silently detaches every entity the first time
// it is saved. A "make unique" that drops the link without copying the values
// first visibly changes the object it was supposed to leave alone. And a broken
// path that is retried every frame turns one typo into a per-frame disk hit.

#include "TestHarness.hpp"
#include "core/Components.hpp"
#include "core/MaterialLibrary.hpp"
#include "core/MaterialSystem.hpp"
#include "core/SceneSerializer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Supersonic;

static const std::string kDir = "test_materials_tmp";
static const std::string kPathA = kDir + "/A.material";
static const std::string kPathB = kDir + "/B.material";

static void cleanup() {
    std::error_code ec;
    std::filesystem::remove_all(kDir, ec);
}

static MaterialAsset makeAsset(const std::string& name, float roughness) {
    MaterialAsset asset;
    asset.name = name;
    asset.roughness = roughness;
    asset.metallic = 0.25f;
    asset.ao = 0.75f;
    asset.albedoColor = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    asset.albedoTexturePath = "assets/textures/uv_grid.png";
    return asset;
}

static entt::entity makeEntity(entt::registry& registry, const std::string& tag) {
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, tag);
    registry.emplace<TransformComponent>(entity);
    registry.emplace<MaterialComponent>(entity);
    return entity;
}

static void testTextRoundTrip() {
    const MaterialAsset original = makeAsset("Brass", 0.31f);
    const std::string text = MaterialLibrary::Serialize(original);

    MaterialAsset restored;
    std::string error;
    CHECK_MSG(MaterialLibrary::Deserialize(text, restored, error), error);

    CHECK_MSG(restored.name == "Brass", restored.name);
    CHECK_NEAR(restored.roughness, 0.31f);
    CHECK_NEAR(restored.metallic, 0.25f);
    CHECK_NEAR(restored.ao, 0.75f);
    CHECK_NEAR(restored.albedoColor.z, 0.6f);
    CHECK_MSG(restored.albedoTexturePath == "assets/textures/uv_grid.png", restored.albedoTexturePath);
}

static void testGarbageIsRejected() {
    MaterialAsset out;
    std::string error;
    CHECK_MSG(!MaterialLibrary::Deserialize("not json", out, error), "garbage must not parse");
    CHECK_MSG(!MaterialLibrary::Deserialize("[1, 2, 3]", out, error), "an array is not a material");
    CHECK_MSG(!error.empty(), "and the failure must say why");
}

static void testCreateAndReload() {
    cleanup();
    MaterialLibrary library;

    const uint32_t id = library.Create(kPathA, makeAsset("Copper", 0.6f));
    CHECK_MSG(id != MaterialLibrary::kInvalidMaterial, "Create must write the file and cache it");
    CHECK_MSG(std::filesystem::exists(kPathA), "and the file must actually be on disk");

    // A second library, as a fresh session would see it.
    MaterialLibrary reopened;
    const uint32_t reloaded = reopened.Acquire(kPathA);
    CHECK_MSG(reloaded != MaterialLibrary::kInvalidMaterial, "the written asset must load back");

    const MaterialAsset* asset = reopened.Get(reloaded);
    CHECK_MSG(asset != nullptr, "and resolve to an asset");
    if (asset) {
        CHECK_MSG(asset->name == "Copper", asset->name);
        CHECK_NEAR(asset->roughness, 0.6f);
    }
    cleanup();
}

static void testAcquireIsCachedAndMissesAreToo() {
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Copper", 0.6f));

    const uint32_t first = library.Acquire(kPathA);
    CHECK_EQ(library.Acquire(kPathA), first);
    CHECK_EQ(library.Size(), size_t{1});

    // A path that does not exist must be remembered as a miss, or a typo in a
    // scene file becomes a disk read every frame for as long as it is loaded.
    CHECK_EQ(library.Acquire(kDir + "/nope.material"), MaterialLibrary::kInvalidMaterial);
    CHECK_EQ(library.Acquire(kDir + "/nope.material"), MaterialLibrary::kInvalidMaterial);
    CHECK_EQ(library.Size(), size_t{2});
    CHECK_MSG(library.Get(MaterialLibrary::kInvalidMaterial) == nullptr,
              "an invalid id must not index the entry list");
    cleanup();
}

static void testOneEditReachesEveryUser() {
    // The entire point of the feature.
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Shared", 0.5f));

    entt::registry registry;
    const auto a = makeEntity(registry, "A");
    const auto b = makeEntity(registry, "B");

    CHECK(MaterialSystem::Assign(registry, a, library, kPathA));
    CHECK(MaterialSystem::Assign(registry, b, library, kPathA));

    MaterialAsset* asset = library.Get(library.Acquire(kPathA));
    CHECK_MSG(asset != nullptr, "the shared asset must resolve");
    if (!asset) { cleanup(); return; }

    asset->roughness = 0.9f;
    asset->albedoColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    MaterialSystem::Sync(registry, library);

    CHECK_NEAR(registry.get<MaterialComponent>(a).roughness, 0.9f);
    CHECK_NEAR(registry.get<MaterialComponent>(b).roughness, 0.9f);
    CHECK_NEAR(registry.get<MaterialComponent>(b).albedoColor.x, 1.0f);
    cleanup();
}

static void testUnlinkedEntitiesAreNotTouched() {
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Shared", 0.5f));

    entt::registry registry;
    const auto linked = makeEntity(registry, "Linked");
    const auto ownIt = makeEntity(registry, "Own");
    registry.get<MaterialComponent>(ownIt).roughness = 0.11f;

    CHECK(MaterialSystem::Assign(registry, linked, library, kPathA));
    MaterialSystem::Sync(registry, library);

    CHECK_NEAR(registry.get<MaterialComponent>(ownIt).roughness, 0.11f);
    CHECK_NEAR(registry.get<MaterialComponent>(linked).roughness, 0.5f);
    cleanup();
}

static void testMakeUniqueKeepsTheLook() {
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Shared", 0.42f));

    entt::registry registry;
    const auto entity = makeEntity(registry, "Thing");
    CHECK(MaterialSystem::Assign(registry, entity, library, kPathA));
    MaterialSystem::Sync(registry, library);

    MaterialSystem::MakeUnique(registry, entity, library);

    const auto& material = registry.get<MaterialComponent>(entity);
    CHECK_MSG(material.materialPath.empty(), "the link must be gone");
    CHECK_MSG(std::fabs(material.roughness - 0.42f) < 1e-4f,
              "but the appearance must not change - that is what makes it safe to press");

    // And it must now be immune to further edits of the asset.
    MaterialAsset* asset = library.Get(library.Acquire(kPathA));
    if (asset) asset->roughness = 0.99f;
    MaterialSystem::Sync(registry, library);
    CHECK_NEAR(registry.get<MaterialComponent>(entity).roughness, 0.42f);
    cleanup();
}

static void testMissingAssetLeavesTheEntityAlone() {
    cleanup();
    MaterialLibrary library;

    entt::registry registry;
    const auto entity = makeEntity(registry, "Thing");
    registry.get<MaterialComponent>(entity).roughness = 0.23f;
    registry.get<MaterialComponent>(entity).materialPath = kDir + "/gone.material";

    MaterialSystem::Sync(registry, library);

    CHECK_NEAR(registry.get<MaterialComponent>(entity).roughness, 0.23f);
    CHECK_MSG(!MaterialSystem::Assign(registry, entity, library, kDir + "/gone.material"),
              "assigning a missing asset must fail rather than silently linking to nothing");
    cleanup();
}

static void testSceneKeepsTheLinkNotJustTheValues() {
    // A scene that stored only the resolved numbers would detach every entity
    // from its material the first time it was saved - and nothing would look
    // wrong until someone edited the asset and nothing moved.
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Shared", 0.5f));

    entt::registry registry;
    const auto entity = makeEntity(registry, "Thing");
    CHECK(MaterialSystem::Assign(registry, entity, library, kPathA));

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find(kPathA) != std::string::npos,
              "the asset path must appear in the saved scene");

    CHECK(SceneSerializer::DeserializeFromString(registry, text).ok);

    bool found = false;
    for (auto e : registry.view<MaterialComponent>()) {
        found = true;
        CHECK_MSG(registry.get<MaterialComponent>(e).materialPath == kPathA,
                  "the link must survive a save and load");
    }
    CHECK_MSG(found, "the entity must survive too");
    cleanup();
}

static void testAssignSwitchesBetweenAssets() {
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("A", 0.2f));
    library.Create(kPathB, makeAsset("B", 0.8f));

    entt::registry registry;
    const auto entity = makeEntity(registry, "Thing");

    CHECK(MaterialSystem::Assign(registry, entity, library, kPathA));
    MaterialSystem::Sync(registry, library);
    CHECK_NEAR(registry.get<MaterialComponent>(entity).roughness, 0.2f);

    CHECK(MaterialSystem::Assign(registry, entity, library, kPathB));
    MaterialSystem::Sync(registry, library);
    CHECK_NEAR(registry.get<MaterialComponent>(entity).roughness, 0.8f);
    cleanup();
}

static void testSaveWritesEdits() {
    cleanup();
    MaterialLibrary library;
    const uint32_t id = library.Create(kPathA, makeAsset("Original", 0.3f));

    MaterialAsset* asset = library.Get(id);
    CHECK_MSG(asset != nullptr, "the created asset must resolve");
    if (asset) {
        asset->roughness = 0.77f;
        asset->name = "Edited";
    }
    CHECK(library.Save(id));

    MaterialLibrary reopened;
    const MaterialAsset* reloaded = reopened.Get(reopened.Acquire(kPathA));
    CHECK_MSG(reloaded != nullptr, "the saved asset must load back");
    if (reloaded) {
        CHECK_NEAR(reloaded->roughness, 0.77f);
        CHECK_MSG(reloaded->name == "Edited", reloaded->name);
    }
    cleanup();
}

// A glTF says what its surface looks like and the engine threw all of it away:
// GltfLoader resolved the base colour, the factors and the texture path, and
// MeshRegistry::Acquire copied the vertices and indices out of the submesh and
// dropped the wrapper holding the rest. A model describing rough gold with a
// texture on it arrived as untextured white plastic.
static void testAnImportedMaterialReachesTheComponent() {
    MeshMaterial imported;
    imported.present = true;
    imported.baseColor = glm::vec4(1.0f, 0.78f, 0.34f, 1.0f);
    imported.roughness = 0.22f;
    imported.metallic = 0.95f;
    imported.albedoTexturePath = "assets/textures/uv_grid.png";
    imported.normalTexturePath = "assets/textures/tiles_normal.png";
    imported.emissiveColor = glm::vec3(0.0f, 1.0f, 0.5f);
    imported.emissiveStrength = 4.0f;
    imported.transparent = true;
    imported.alphaCutoff = 0.5f;

    MaterialComponent component;
    component.ao = 0.35f;   // authored, and not something glTF carries as a factor
    MaterialSystem::ApplyImportedMaterial(imported, component);

    CHECK(component.albedoColor.r > 0.99f && component.albedoColor.g > 0.77f);
    CHECK(component.roughness == 0.22f);
    CHECK(component.metallic == 0.95f);
    CHECK(component.albedoTexturePath == "assets/textures/uv_grid.png");
    CHECK_MSG(component.normalTexturePath == "assets/textures/tiles_normal.png",
              "the renderer has had a normal-map slot since normal mapping shipped, "
              "and the importer never filled it");
    CHECK(component.emissiveStrength == 4.0f);
    CHECK(component.transparent);
    CHECK(component.alphaCutoff == 0.5f);
    CHECK_MSG(component.ao == 0.35f,
              "glTF carries occlusion as a texture, not a factor, so an authored "
              "ao must survive an import rather than being reset to a default");
}

// A file that names no material must not blank a material somebody authored.
// This is the difference between "the file said white" and "the file said
// nothing", and it is why MeshMaterial carries `present` at all - every
// procedural primitive in the engine goes through the same path.
static void testAFileThatSaysNothingChangesNothing() {
    MaterialComponent component;
    component.albedoColor = glm::vec4(0.2f, 0.4f, 0.9f, 1.0f);
    component.roughness = 0.11f;
    component.albedoTexturePath = "assets/textures/turret.png";
    component.materialPath = "assets/materials/Turret.material";

    MaterialSystem::ApplyImportedMaterial(MeshMaterial{}, component);

    CHECK(component.albedoColor.b > 0.89f);
    CHECK(component.roughness == 0.11f);
    CHECK(component.albedoTexturePath == "assets/textures/turret.png");
    CHECK_MSG(component.materialPath == "assets/materials/Turret.material",
              "a cube must not detach an entity from its shared material");
}

// Importing describes THIS entity's surface, so a linked entity is detached
// rather than having the shared asset silently rewritten underneath every other
// entity using it.
static void testImportingDetachesFromASharedAsset() {
    MeshMaterial imported;
    imported.present = true;
    imported.baseColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);

    MaterialComponent component;
    component.materialPath = "assets/materials/Shared.material";
    component.warnedMissingAsset = true;

    MaterialSystem::ApplyImportedMaterial(imported, component);

    CHECK_MSG(component.materialPath.empty(),
              "an imported material is the entity's own, not a shared asset");
    CHECK(!component.warnedMissingAsset);
}

// glTF alphaMode MASK is a request for a hard edge, not for a place in the
// sorted blend. Mapping it onto `transparent` is what makes foliage sort
// against itself: one leaf card in front of another composites in whichever
// order the distance sort picked, and it flickers as the camera moves.
static void testMaskBecomesACutoffAndNotTransparency() {
    MeshMaterial masked;
    masked.present = true;
    masked.alphaCutoff = 0.5f;
    masked.transparent = false;

    MaterialComponent component;
    MaterialSystem::ApplyImportedMaterial(masked, component);

    CHECK_MSG(component.alphaCutoff == 0.5f, "MASK must arrive as a cutoff");
    CHECK_MSG(!component.transparent,
              "a cutout surface stays opaque: it writes depth and needs no sorting");
}

// BLEND is the other request, and must not turn into a cutoff.
static void testBlendStaysTransparentWithNoCutoff() {
    MeshMaterial blended;
    blended.present = true;
    blended.transparent = true;
    blended.alphaCutoff = 0.0f;

    MaterialComponent component;
    component.alphaCutoff = 0.9f;   // whatever was there before
    MaterialSystem::ApplyImportedMaterial(blended, component);

    CHECK(component.transparent);
    CHECK_MSG(component.alphaCutoff == 0.0f,
              "importing BLEND must clear a cutoff, not leave the old one behind");
}

static void runTests() {
    testTextRoundTrip();
    testGarbageIsRejected();
    testCreateAndReload();
    testAcquireIsCachedAndMissesAreToo();
    testOneEditReachesEveryUser();
    testUnlinkedEntitiesAreNotTouched();
    testMakeUniqueKeepsTheLook();
    testMissingAssetLeavesTheEntityAlone();
    testSceneKeepsTheLinkNotJustTheValues();
    testAssignSwitchesBetweenAssets();
    testSaveWritesEdits();
    testAnImportedMaterialReachesTheComponent();
    testAFileThatSaysNothingChangesNothing();
    testImportingDetachesFromASharedAsset();
    testMaskBecomesACutoffAndNotTransparency();
    testBlendStaysTransparentWithNoCutoff();
    cleanup();
}

TEST_MAIN("test_materials", 55)
