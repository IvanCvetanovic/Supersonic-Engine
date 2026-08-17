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
    cleanup();
}

TEST_MAIN("test_materials")
