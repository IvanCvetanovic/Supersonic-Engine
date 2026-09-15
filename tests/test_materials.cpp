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
#include "core/AssetWatcher.hpp"
#include "core/Components.hpp"
#include "core/Light2D.hpp"
#include "core/MaterialLibrary.hpp"
#include "core/MaterialSystem.hpp"
#include "core/RenderSystem.hpp"
#include "core/SceneSerializer.hpp"
#include "renderer/MaterialSetLedger.hpp"
#include "renderer/VulkanPipeline.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
    imported.ormTexturePath = "assets/textures/rust_orm.png";
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

    CHECK_MSG(component.ormTexturePath == "assets/textures/rust_orm.png",
              "and the packed occlusion/roughness/metallic map must arrive too - "
              "a glTF that says a surface is worn in places said so in a texture");
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

// Nothing about a material asset may be lost by a save and a reload, and the
// packed map is the newest field - the one a writer is most likely to have
// been forgotten in while the reader was updated, which round-trips as a
// silent reset to no map.
static void testAPackedMapSurvivesTheAssetRoundTrip() {
    MaterialAsset asset;
    asset.name = "Worn Steel";
    asset.albedoTexturePath = "assets/textures/steel.png";
    asset.normalTexturePath = "assets/textures/steel_normal.png";
    asset.ormTexturePath = "assets/textures/steel_orm.png";
    asset.occlusionStrength = 0.0f;

    MaterialAsset restored;
    std::string error;
    CHECK_MSG(MaterialLibrary::Deserialize(MaterialLibrary::Serialize(asset), restored, error),
              error);
    CHECK_MSG(restored.ormTexturePath == "assets/textures/steel_orm.png",
              restored.ormTexturePath);
    CHECK_MSG(restored.occlusionStrength == 0.0f,
              "the strength decides whether red is occlusion, and zero is the "
              "value a default-on-absent bug hands back as one");

    // And a file written before the field existed reads as no map, which is
    // the surface it always was rather than a checkerboard.
    MaterialAsset old;
    CHECK_MSG(MaterialLibrary::Deserialize(
                  "{\"Material\": \"Old\", \"Roughness\": 0.3}", old, error), error);
    CHECK_MSG(old.ormTexturePath.empty(), "an absent packed map is no map");
    CHECK_NEAR(old.occlusionStrength, 1.0f);
    CHECK_NEAR(old.roughness, 0.3f);
}

// Writes raw text, so a test can put something on disk that the library did
// not produce - including something that does not parse.
static void writeRaw(const std::string& path, const std::string& text) {
    std::filesystem::create_directories(kDir);
    std::ofstream file(path, std::ios::trunc);
    file << text;
}

// Filesystem write times are coarse, so a test that depends on a file looking
// CHANGED has to outlive one tick. See test_assetwatcher for the same helper.
static void letTheClockMove() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
}

static void testReloadKeepsTheIdAndPicksUpTheNewValues() {
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Original", 0.3f));

    const uint32_t id = library.Acquire(kPathA);
    CHECK_MSG(id != MaterialLibrary::kInvalidMaterial, "the material must load to begin with");

    // Edited by something outside the engine - a text editor, or another tool.
    writeRaw(kPathA, MaterialLibrary::Serialize(makeAsset("Edited", 0.87f)));
    CHECK_MSG(library.Reload(kPathA), "a valid material must reload");

    // The id is the load-bearing part. Every MaterialComponent in the scene is
    // holding this number; if a reload minted a new one they would all keep
    // rendering the values from before the edit.
    CHECK_EQ(library.Acquire(kPathA), id);

    const MaterialAsset* asset = library.Get(id);
    CHECK_MSG(asset != nullptr && asset->name == "Edited", "the reload must replace the name");
    CHECK_NEAR(asset ? asset->roughness : 0.0f, 0.87f);
    cleanup();
}

static void testReloadingAPathNobodyAcquiredDoesNothing() {
    cleanup();
    MaterialLibrary library;
    writeRaw(kPathB, MaterialLibrary::Serialize(makeAsset("Unreferenced", 0.5f)));

    // Reload is driven by a file watcher that also watches textures and meshes,
    // so it is handed paths of every kind. Minting an entry for one nothing
    // asked for would grow the library for the life of the session.
    CHECK_MSG(!library.Reload(kPathB), "reloading an unknown path must be a no-op");
    CHECK_EQ(library.Size(), size_t{0});
    cleanup();
}

static void testABrokenFileKeepsTheValuesAlreadyLoaded() {
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Good", 0.42f));
    const uint32_t id = library.Acquire(kPathA);

    // A save is not atomic. A poll landing between the truncate and the write
    // reads an empty file, and blanking the asset there would turn every object
    // using it white - and then let the editor write that blank back.
    writeRaw(kPathA, "{ this is not json");
    CHECK_MSG(!library.Reload(kPathA), "an unparseable file must report failure");

    const MaterialAsset* asset = library.Get(id);
    CHECK_MSG(asset != nullptr, "a failed reload must not invalidate a loaded material");
    CHECK_NEAR(asset ? asset->roughness : 0.0f, 0.42f);
    cleanup();
}

static void testFixingABrokenMaterialClearsTheCachedMiss() {
    cleanup();
    MaterialLibrary library;
    writeRaw(kPathA, "not a material at all");

    // Cached as a miss, so the broken reference is not re-read every frame.
    CHECK_EQ(library.Acquire(kPathA), MaterialLibrary::kInvalidMaterial);

    // Fixing the file on disk is exactly how someone expects to clear that, so
    // the reload has to promote the miss rather than only refresh a hit.
    writeRaw(kPathA, MaterialLibrary::Serialize(makeAsset("Fixed", 0.61f)));
    CHECK_MSG(library.Reload(kPathA), "fixing the file must clear the cached miss");

    const uint32_t id = library.Acquire(kPathA);
    CHECK_MSG(id != MaterialLibrary::kInvalidMaterial, "the material must now resolve");
    CHECK_NEAR(library.Get(id) ? library.Get(id)->roughness : 0.0f, 0.61f);
    cleanup();
}

static void testSavingFromTheEditorDoesNotFireTheWatcher() {
    cleanup();
    AssetWatcher watcher;
    int fired = 0;
    watcher.SetCallback([&](const std::string&) { ++fired; });

    MaterialLibrary library;
    library.SetWatcher(&watcher);
    library.Create(kPathA, makeAsset("Tuned", 0.3f));

    const uint32_t id = library.Acquire(kPathA);
    watcher.Watch(kPathA);
    CHECK_EQ(watcher.Poll(), size_t{0});

    // The inspector edits the shared asset in place and presses Save. Without
    // the acknowledgement the next poll reads the file back over the values
    // still being dragged: harmless while they match, wrong on the first frame
    // where the slider has moved on.
    letTheClockMove();
    if (MaterialAsset* asset = library.Get(id)) asset->roughness = 0.9f;
    library.Save(id);

    CHECK_MSG(watcher.Poll() == size_t{0}, "the engine must not read its own write back");
    CHECK_EQ(fired, 0);

    // And it is still watching: an edit made ELSEWHERE still fires.
    letTheClockMove();
    writeRaw(kPathA, MaterialLibrary::Serialize(makeAsset("External", 0.11f)));
    CHECK_MSG(watcher.Poll() == size_t{1}, "an edit from outside must still fire");

    cleanup();
}

static void testRenamingTheMaterialItselfKeepsItsIdAndItsEdits() {
    // A .material is an asset with an identity of its own, so it can be the
    // thing that gets renamed. The component naming it is re-pointed in the
    // same pass, so without re-keying the cache it misses, pushes a SECOND
    // entry for the same file, and reads it back from disk - silently throwing
    // away an edit made in the inspector and not yet saved.
    cleanup();
    MaterialLibrary library;
    library.Create(kPathA, makeAsset("Shared", 0.33f));
    const uint32_t id = library.Acquire(kPathA);

    if (MaterialAsset* asset = library.Get(id)) asset->roughness = 0.91f;

    CHECK_EQ(library.Repoint(kPathA, kPathB), size_t{1});
    CHECK_EQ(library.Acquire(kPathB), id);
    CHECK_MSG(library.PathOf(id) == kPathB, "the entry must know where it lives now");
    CHECK_NEAR(library.Get(id) ? library.Get(id)->roughness : 0.0f, 0.91f);
    CHECK_MSG(library.Size() == size_t{1}, "and there must not be a second copy of it");
    cleanup();
}

// --- The texture coordinate transform -------------------------------------
//
// Scrolling rain and a flipbook flame are one feature seen twice: the mesh
// stands still and the texture coordinates move. The transform is per DRAW -
// twenty flames share a material and each is on its own frame - so it travels
// as an index into a per-frame buffer, and every failure mode of that is quiet.
//
// A gather that skips the identity leaves the shader reading a buffer nobody
// wrote. One that forgets to reset a material that stopped scrolling leaves it
// pointing at somebody else's transform. And a slot packed into the wrong bits
// takes the unlit switch with it - which is the switch both of the materials
// this feature was built for happen to set.

// A TRANSLITERATION of transformedUV() in assets/shaders/shader.frag, and it is
// written that way on purpose: that expression is the only one that matters and
// the only one no test can reach. Two descriptions of the same multiply, and
// changing one without the other is the whole risk.
static glm::vec2 applyUv(const UvTransform& t, const glm::vec2& uv) {
    // mat2(x, y, z, w) takes COLUMNS, so axes.xy is column 0 and axes.zw is
    // column 1 - which is what the shader's mat2 constructor does with the
    // same four floats.
    return glm::vec2(t.axes.x * uv.x + t.axes.z * uv.y + t.offset.x,
                     t.axes.y * uv.x + t.axes.w * uv.y + t.offset.y);
}

static void testTheIdentityAlwaysOccupiesSlotZero() {
    entt::registry registry;
    std::vector<UvTransform> out;

    // Nothing in the scene at all. The buffer still has to be written: the
    // shader indexes it on EVERY draw, and reading a storage buffer nobody
    // wrote is undefined even where the answer is thrown away.
    CHECK_EQ(MaterialSystem::GatherUvTransforms(registry, out, 64), 1u);
    CHECK_EQ(out.size(), size_t(1));

    // Return rather than index an empty vector. MSVC's debug subscript check
    // opens a MODAL DIALOG, so a suite that indexes past the end does not fail,
    // it hangs - and a hung suite in a mutation run reads as a mutation that
    // was never tried. The CHECK above has already recorded the failure.
    if (out.empty()) return;

    CHECK_NEAR(out[0].axes.x, 1.0f);
    CHECK_NEAR(out[0].axes.y, 0.0f);
    CHECK_NEAR(out[0].axes.z, 0.0f);
    CHECK_NEAR(out[0].axes.w, 1.0f);
    CHECK_NEAR(out[0].offset.x, 0.0f);
    CHECK_NEAR(out[0].offset.y, 0.0f);
}

static void testOnlyMaterialsThatActuallyScrollTakeASlot() {
    entt::registry registry;
    std::vector<UvTransform> out;

    const entt::entity plain = registry.create();
    registry.emplace<MaterialComponent>(plain);

    const entt::entity scrolling = registry.create();
    auto& rain = registry.emplace<MaterialComponent>(scrolling);
    rain.uvOffset = glm::vec2(0.0f, -0.25f);

    CHECK_EQ(MaterialSystem::GatherUvTransforms(registry, out, 64), 2u);

    // The identity one points at slot 0 rather than taking a slot of its own.
    // A scene where nothing scrolls uploads thirty-two bytes.
    CHECK_EQ(registry.get<MaterialComponent>(plain).uvSlot, 0);
    CHECK_MSG(registry.get<MaterialComponent>(scrolling).uvSlot != 0,
              "a material that scrolls got a slot of its own");

    const size_t slot = static_cast<size_t>(registry.get<MaterialComponent>(scrolling).uvSlot);
    CHECK_MSG(slot < out.size(), "and the slot points inside the buffer that was written");
    if (slot >= out.size()) return;
    CHECK_NEAR(out[slot].offset.y, -0.25f);
}

static void testAFlipbookFrameComposesTheWayItsSourceDid() {
    // HUSK's fire is a sixteen-frame strip: scale (1/16, 1), offset frame/16.
    // The numbers are checked against what the Bevy original computes, because
    // "nearly the same" here is a flame showing two half frames at once.
    const UvTransform t = MakeUvTransform(glm::vec2(1.0f / 16.0f, 1.0f), 0.0f,
                                          glm::vec2(3.0f / 16.0f, 0.0f));

    CHECK_NEAR(t.axes.x, 1.0f / 16.0f);   // U axis, scaled
    CHECK_NEAR(t.axes.y, 0.0f);
    CHECK_NEAR(t.axes.z, 0.0f);           // V axis, untouched
    CHECK_NEAR(t.axes.w, 1.0f);
    CHECK_NEAR(t.offset.x, 0.1875f);
    CHECK_NEAR(t.offset.y, 0.0f);

    // And what the shader will actually compute, since those four floats are
    // only right if they multiply the way mat2 does. Frame 3 of 16 maps the
    // quad's full 0..1 U range onto 0.1875..0.25 of the strip.
    const glm::vec2 left = applyUv(t, glm::vec2(0.0f, 0.5f));
    const glm::vec2 right = applyUv(t, glm::vec2(1.0f, 0.5f));
    CHECK_NEAR(left.x, 0.1875f);
    CHECK_NEAR(right.x, 0.25f);
    CHECK_NEAR(left.y, 0.5f);
    CHECK_NEAR(right.y, 0.5f);
}

static void testScaleHappensBeforeRotationAndTranslationAfterBoth() {
    // The one composition-order question, and the only way to get it wrong
    // quietly: every order agrees when the rotation is zero, which is what both
    // of the materials that motivated this feature use.
    //
    // A quarter turn, scale (2, 3), then slid one to the right. Under
    // scale-then-rotate the U axis is (0, 2); under rotate-then-scale it would
    // be (0, 3), and nothing but a number says which one ran.
    const UvTransform t = MakeUvTransform(glm::vec2(2.0f, 3.0f),
                                          glm::radians(90.0f),
                                          glm::vec2(1.0f, 0.0f));

    CHECK_NEAR(t.axes.x, 0.0f);
    CHECK_NEAR(t.axes.y, 2.0f);
    CHECK_NEAR(t.axes.z, -3.0f);
    CHECK_NEAR(t.axes.w, 0.0f);

    // The origin lands on the translation, which is what "translate last"
    // means and what a rotation applied after it would break.
    const glm::vec2 origin = applyUv(t, glm::vec2(0.0f, 0.0f));
    CHECK_NEAR(origin.x, 1.0f);
    CHECK_NEAR(origin.y, 0.0f);

    const glm::vec2 alongU = applyUv(t, glm::vec2(1.0f, 0.0f));
    CHECK_NEAR(alongU.x, 1.0f);
    CHECK_NEAR(alongU.y, 2.0f);

    const glm::vec2 alongV = applyUv(t, glm::vec2(0.0f, 1.0f));
    CHECK_NEAR(alongV.x, -2.0f);
    CHECK_NEAR(alongV.y, 0.0f);
}

static void testAMaterialThatStopsScrollingGoesBackToTheIdentity() {
    // Slots are assigned fresh every frame, so a material that scrolled and
    // then stopped is left pointing at a slot that now holds SOMEBODY ELSE'S
    // transform. Leaving the stale value alone looks like a rounding error and
    // is a texture belonging to another object.
    entt::registry registry;
    std::vector<UvTransform> out;

    const entt::entity stops = registry.create();
    auto& material = registry.emplace<MaterialComponent>(stops);
    material.uvOffset = glm::vec2(0.5f, 0.0f);

    CHECK_EQ(MaterialSystem::GatherUvTransforms(registry, out, 64), 2u);
    CHECK_MSG(registry.get<MaterialComponent>(stops).uvSlot != 0, "it took a slot");

    registry.get<MaterialComponent>(stops).uvOffset = glm::vec2(0.0f, 0.0f);

    CHECK_EQ(MaterialSystem::GatherUvTransforms(registry, out, 64), 1u);
    CHECK_EQ(registry.get<MaterialComponent>(stops).uvSlot, 0);
}

static void testRunningOutOfSlotsDrawsUntransformedRatherThanOutOfBounds() {
    // robustBufferAccess is not enabled on this device, so a slot past the end
    // of the buffer is a device loss and not a zeroed read. Four materials into
    // room for two.
    entt::registry registry;
    std::vector<UvTransform> out;

    std::vector<entt::entity> entities;
    for (int i = 0; i < 4; ++i) {
        const entt::entity e = registry.create();
        auto& material = registry.emplace<MaterialComponent>(e);
        material.uvOffset = glm::vec2(0.1f * static_cast<float>(i + 1), 0.0f);
        entities.push_back(e);
    }

    const uint32_t written = MaterialSystem::GatherUvTransforms(registry, out, 3);
    CHECK_EQ(written, 3u);
    CHECK_EQ(out.size(), size_t(3));

    int dropped = 0;
    for (entt::entity e : entities) {
        const int slot = registry.get<MaterialComponent>(e).uvSlot;
        CHECK_MSG(slot >= 0 && static_cast<size_t>(slot) < out.size(),
                  "every slot handed out is inside the buffer that was written");
        if (slot == 0) ++dropped;
    }
    CHECK_MSG(dropped == 2, "the two that did not fit draw untransformed");
}

static void testRunningOutOfSlotsIsReportedRatherThanSwallowed() {
    // WHAT DID NOT EXIST WAS THE NUMBER, not the behaviour above. Overflow has
    // always handed the material slot 0 and carried on, and slot 0 is the
    // IDENTITY - so a sprite past the limit draws its whole atlas rather than
    // one cell of it. A large, obvious wrongness whose cause is invisible,
    // while the instance buffer sixteen times larger has always logged when it
    // filled. The caller cannot warn about a count it is never given.
    entt::registry registry;
    std::vector<UvTransform> out;

    for (int i = 0; i < 7; ++i) {
        const entt::entity e = registry.create();
        auto& material = registry.emplace<MaterialComponent>(e);
        material.uvOffset = glm::vec2(0.05f * static_cast<float>(i + 1), 0.0f);
    }

    // Room for the identity plus two, so five of the seven must be turned away.
    uint32_t droppedCount = 99;
    const uint32_t written = MaterialSystem::GatherUvTransforms(registry, out, 3, &droppedCount);

    CHECK_EQ(written, 3u);
    CHECK_MSG(droppedCount == 5u, "every material that asked and was refused is counted");
}

static void testAFrameThatFitsReportsNothingDropped() {
    // The control, and it is the half that would otherwise pass by accident: a
    // counter wired to the wrong place, or one never cleared between frames,
    // reports a shortage in a scene that has none - and a warning that cries
    // wolf on every frame is worse than the silence it replaced.
    entt::registry registry;
    std::vector<UvTransform> out;

    for (int i = 0; i < 3; ++i) {
        const entt::entity e = registry.create();
        auto& material = registry.emplace<MaterialComponent>(e);
        material.uvOffset = glm::vec2(0.05f * static_cast<float>(i + 1), 0.0f);
    }

    uint32_t droppedCount = 99;
    MaterialSystem::GatherUvTransforms(registry, out, 64, &droppedCount);
    CHECK_EQ(droppedCount, 0u);

    // And a material that never wanted a transform is not a material that was
    // refused one. Those are different facts and only one of them is a problem.
    registry.emplace<MaterialComponent>(registry.create());
    droppedCount = 99;
    MaterialSystem::GatherUvTransforms(registry, out, 64, &droppedCount);
    CHECK_MSG(droppedCount == 0u,
              "a material with an identity transform takes no slot and is not a drop");
}

static void testASlotAndTheUnlitSwitchShareAWordWithoutTouching() {
    // The slot rides in the twelve bits above the switches. Both of the
    // materials this feature was built for are unlit, so a slot packed one bit
    // too low would turn every scrolling surface into a lit one - and lighting
    // that happens to look plausible is not a bug anybody reports.
    const int32_t kUnlitBit = 1;

    int32_t flags = kUnlitBit;
    flags = PackUvSlot(flags, 4095);

    CHECK_EQ(UnpackUvSlot(flags), 4095);
    CHECK_MSG((flags & kUnlitBit) != 0, "the unlit switch survived the packing");
    CHECK_MSG(flags > 0, "and the sign bit was never reached");

    // Repacking REPLACES rather than accumulates: a draw whose slot changes
    // between frames must not end up with the two ORed together.
    flags = PackUvSlot(flags, 7);
    CHECK_EQ(UnpackUvSlot(flags), 7);
    CHECK_MSG((flags & kUnlitBit) != 0, "and it is still unlit");

    // The identity slot is what an untouched flags word already says, which is
    // what lets a hand-built push constant - the particle path builds one -
    // draw correctly having never heard of any of this.
    CHECK_EQ(UnpackUvSlot(0), 0);
}

static void testAUvTransformSurvivesASaveAndLoad() {
    // The quiet failure: a scene that writes the transform but does not read it
    // back loads every scrolling surface as a still one, and the only symptom
    // is that the rain has stopped. Both halves are asserted, and the write is
    // asserted by NAME so a field renamed on one side of the codec is caught
    // where it happens rather than by a picture.
    cleanup();
    entt::registry registry;
    const auto entity = makeEntity(registry, "Rain");
    auto& material = registry.get<MaterialComponent>(entity);
    material.uvScale = glm::vec2(6.0f, 3.0f);
    material.uvRotation = 0.75f;
    material.uvOffset = glm::vec2(0.0f, -0.4f);

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find("UvScale") != std::string::npos, "the scale was written");
    CHECK_MSG(text.find("UvRotation") != std::string::npos, "and the rotation");
    CHECK_MSG(text.find("UvOffset") != std::string::npos, "and the offset");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);

    bool found = false;
    for (auto e : loaded.view<MaterialComponent>()) {
        const auto& m = loaded.get<MaterialComponent>(e);
        found = true;
        CHECK_NEAR(m.uvScale.x, 6.0f);
        CHECK_NEAR(m.uvScale.y, 3.0f);
        CHECK_NEAR(m.uvRotation, 0.75f);
        CHECK_NEAR(m.uvOffset.y, -0.4f);
    }
    CHECK_MSG(found, "the entity came back");
    cleanup();
}

static void testAMaterialWrittenBeforeThisExistedLoadsUntransformed() {
    // Every scene on disk predates this feature. The fallbacks have to be the
    // IDENTITY and not zero: a scale of zero collapses every texture
    // coordinate onto one texel, so an absent field would repaint every old
    // scene in a single flat colour.
    cleanup();
    entt::registry registry;
    makeEntity(registry, "Old");   // makeEntity already gives it a material

    std::string text = SceneSerializer::SerializeToString(registry);

    // Strip the three keys back out, which is exactly what an older file is.
    for (const char* key : {"UvScale", "UvRotation", "UvOffset"}) {
        const size_t at = text.find(key);
        if (at == std::string::npos) continue;
        const size_t lineStart = text.rfind('\n', at);
        const size_t lineEnd = text.find('\n', at);
        if (lineStart == std::string::npos || lineEnd == std::string::npos) continue;
        text.erase(lineStart, lineEnd - lineStart);
    }
    CHECK_MSG(text.find("UvScale") == std::string::npos, "the field really is gone");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);
    for (auto e : loaded.view<MaterialComponent>()) {
        const auto& m = loaded.get<MaterialComponent>(e);
        CHECK_NEAR(m.uvScale.x, 1.0f);
        CHECK_NEAR(m.uvScale.y, 1.0f);
        CHECK_MSG(!m.HasUvTransform(), "and it costs no transform slot");
    }
    cleanup();
}

static void testTheBlendSurvivesASaveAndLoad() {
    // A glow saved and loaded as a pane is a black square where a portal was,
    // so the write is asserted by name, as the UV transform's is.
    cleanup();
    entt::registry registry;
    const auto entity = makeEntity(registry, "Halo");
    auto& material = registry.get<MaterialComponent>(entity);
    material.transparent = true;
    material.blend = MaterialComponent::BlendMode::Additive;

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find("\"Blend\": \"Additive\"") != std::string::npos, "the blend was written, by name");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);

    bool found = false;
    for (auto e : loaded.view<MaterialComponent>()) {
        const auto& m = loaded.get<MaterialComponent>(e);
        found = true;
        CHECK(m.transparent);
        CHECK_MSG(m.blend == MaterialComponent::BlendMode::Additive, "and read back");
    }
    CHECK_MSG(found, "the entity came back");
    cleanup();
}

static void testAMaterialWrittenBeforeTheBlendMixes() {
    cleanup();
    entt::registry registry;
    const auto entity = makeEntity(registry, "Pane");
    registry.get<MaterialComponent>(entity).transparent = true;

    std::string text = SceneSerializer::SerializeToString(registry);
    const size_t at = text.find("\"Blend\"");
    CHECK_MSG(at != std::string::npos, "a mixing material writes its blend too");
    if (at != std::string::npos) {
        const size_t lineStart = text.rfind('\n', at);
        const size_t lineEnd = text.find('\n', at);
        if (lineStart != std::string::npos && lineEnd != std::string::npos) {
            text.erase(lineStart, lineEnd - lineStart);
        }
    }
    CHECK_MSG(text.find("\"Blend\"") == std::string::npos, "the field really is gone");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);
    for (auto e : loaded.view<MaterialComponent>()) {
        CHECK_MSG(loaded.get<MaterialComponent>(e).blend == MaterialComponent::BlendMode::Alpha,
                  "a scene from before the blend loads mixing, as it drew");
    }
    cleanup();
}

static void testTheShaderAgreesAboutWhereTheSlotLives() {
    // TWO DESCRIPTIONS OF ONE PACKING: kUvSlotShift and kUvSlotMask here,
    // UV_SLOT_SHIFT and UV_SLOT_MASK in shader.frag. Nothing links them.
    //
    // And a disagreement is invisible in every other way. It is not a compile
    // error, it is not a validation error, and the round trip inside C++ still
    // works perfectly - PackUvSlot and UnpackUvSlot both move, so every check
    // that only asks C++ about C++ still passes. What actually happens is that
    // the shader reads the slot out of different bits than the draw loop put it
    // in, so every scrolling surface samples somebody else's transform.
    //
    // Read out of the shader SOURCE rather than copied into a literal here,
    // because a copy in the test is a third description with the same problem.
    std::ifstream file("assets/shaders/shader.frag");
    CHECK_MSG(file.good(), "shader.frag must be readable from the working directory");
    if (!file.good()) return;

    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    CHECK_MSG(!source.empty(), "and it must not be empty");

    const std::string shiftDecl =
        "const int UV_SLOT_SHIFT = " + std::to_string(kUvSlotShift) + ";";
    CHECK_MSG(source.find(shiftDecl) != std::string::npos,
              "shader.frag must unpack the slot from the bit the packing puts it in: "
              + shiftDecl);

    std::ostringstream mask;
    mask << "const int UV_SLOT_MASK = 0x" << std::uppercase << std::hex << kUvSlotMask << ";";
    CHECK_MSG(source.find(mask.str()) != std::string::npos,
              "and it must keep the same number of bits: " + mask.str());

    // The stride the other half of this contract depends on. std430 gives an
    // array of two vec4 a 32-byte stride, which is what sizeof(UvTransform) is
    // asserted against - so a third vec4 added on one side only would put every
    // transform after the first half inside its neighbour.
    CHECK_MSG(source.find("vec4 axes;") != std::string::npos &&
              source.find("vec4 offset;") != std::string::npos,
              "the shader's transform must still be exactly the two vec4 C++ writes");
    CHECK_EQ(sizeof(UvTransform), size_t(32));
}

// --- re-materialising one named surface -----------------------------------
//
// A model is authored as several named surfaces and a game addresses them by
// name. HUSK tells its two teams apart exactly this way: one chassis, with
// BODY and DARK swapped for a teal set or a rust set depending on who owns the
// unit. Without it the only options are shipping the model twice or tinting the
// whole thing - and tinting the whole thing takes the visor and the exhaust
// glow with it.

static MeshMaterial surfaceNamed(const char* name, const glm::vec4& colour) {
    MeshMaterial m;
    m.present = true;
    m.name = name;
    m.baseColor = colour;
    m.roughness = 0.9f;
    m.albedoTexturePath = "assets/textures/uv_grid.png";
    return m;
}

static void testAnOverrideReplacesOnlyTheSurfaceItNames() {
    SurfaceOverridesComponent overrides;
    SurfaceOverride teal;
    teal.surface = "BODY";
    teal.albedoColor = glm::vec4(0.20f, 0.40f, 0.55f, 1.0f);
    teal.roughness = 0.6f;
    overrides.overrides.push_back(teal);

    const MeshMaterial body = surfaceNamed("BODY", glm::vec4(0.16f, 0.155f, 0.165f, 1.0f));
    const MeshMaterial dark = surfaceNamed("DARK", glm::vec4(0.10f, 0.10f, 0.11f, 1.0f));

    const MeshMaterial resolvedBody = RenderSystem::ResolveSurface(body, &overrides);
    CHECK_NEAR(resolvedBody.baseColor.x, 0.20f);
    CHECK_NEAR(resolvedBody.baseColor.y, 0.40f);
    CHECK_NEAR(resolvedBody.baseColor.z, 0.55f);
    CHECK_NEAR(resolvedBody.roughness, 0.6f);

    // THE NEIGHBOUR IS UNTOUCHED. An override that leaked onto every surface
    // would look like a team colour working, right up until somebody noticed
    // the whole unit was one flat colour again.
    const MeshMaterial resolvedDark = RenderSystem::ResolveSurface(dark, &overrides);
    CHECK_NEAR(resolvedDark.baseColor.x, 0.10f);
    CHECK_NEAR(resolvedDark.baseColor.z, 0.11f);
    CHECK_NEAR(resolvedDark.roughness, 0.9f);
}

static void testTheMapsSurviveTheOverride() {
    // An override says what colour a surface is, not what shape it is. A model
    // whose BODY carries a normal map keeps it when the team colour lands.
    SurfaceOverridesComponent overrides;
    SurfaceOverride entry;
    entry.surface = "BODY";
    entry.albedoColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    overrides.overrides.push_back(entry);

    const MeshMaterial resolved =
        RenderSystem::ResolveSurface(surfaceNamed("BODY", glm::vec4(1.0f)), &overrides);
    CHECK_MSG(resolved.albedoTexturePath == "assets/textures/uv_grid.png",
              "the surface keeps the maps the file gave it");
    CHECK_MSG(resolved.name == "BODY", "and keeps its name, so it can be overridden again");
}

static void testAnOverrideThatNamesNothingIsInert() {
    // A model is allowed not to have the part being described - a rig swapped
    // for one with fewer pieces, or a shared override list applied to a
    // building as well as a unit. It must be silent, not an error.
    SurfaceOverridesComponent overrides;
    SurfaceOverride entry;
    entry.surface = "TURRET";
    entry.albedoColor = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    overrides.overrides.push_back(entry);

    const MeshMaterial body = surfaceNamed("BODY", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
    const MeshMaterial resolved = RenderSystem::ResolveSurface(body, &overrides);
    CHECK_NEAR(resolved.baseColor.x, 0.5f);

    // And no overrides at all is the ordinary case for every model in a scene.
    const MeshMaterial untouched = RenderSystem::ResolveSurface(body, nullptr);
    CHECK_NEAR(untouched.baseColor.x, 0.5f);

    // A surface the file left unnamed cannot be addressed, and must not match
    // an override that happens to have an empty name either.
    SurfaceOverridesComponent empty;
    empty.overrides.push_back(SurfaceOverride{});
    MeshMaterial anonymous = body;
    anonymous.name.clear();
    CHECK_NEAR(RenderSystem::ResolveSurface(anonymous, &empty).baseColor.x, 0.5f);
}

static void testTwoTeamsShareOneModel() {
    // The whole point, in the shape HUSK uses it: one mesh material, two
    // entities, two different answers - and neither writes into the other.
    const MeshMaterial body = surfaceNamed("BODY", glm::vec4(0.16f, 0.155f, 0.165f, 1.0f));

    SurfaceOverridesComponent player;
    SurfaceOverride teal;
    teal.surface = "BODY";
    teal.albedoColor = glm::vec4(0.20f, 0.40f, 0.55f, 1.0f);
    player.overrides.push_back(teal);

    SurfaceOverridesComponent enemy;
    SurfaceOverride rust;
    rust.surface = "BODY";
    rust.albedoColor = glm::vec4(0.50f, 0.19f, 0.13f, 1.0f);
    enemy.overrides.push_back(rust);

    CHECK_NEAR(RenderSystem::ResolveSurface(body, &player).baseColor.z, 0.55f);
    CHECK_NEAR(RenderSystem::ResolveSurface(body, &enemy).baseColor.x, 0.50f);

    // The FILE's material is unchanged by either, which is what makes it safe
    // for the mesh to be shared: it belongs to the mesh, not to an entity, and
    // an in-place override would colour every other unit drawing the same model.
    CHECK_NEAR(body.baseColor.x, 0.16f);
}

static void testOverridesSurviveASaveAndLoad() {
    cleanup();
    entt::registry registry;
    const auto entity = makeEntity(registry, "Unit");
    auto& surfaces = registry.emplace<SurfaceOverridesComponent>(entity);
    SurfaceOverride entry;
    entry.surface = "BODY";
    entry.albedoColor = glm::vec4(0.20f, 0.40f, 0.55f, 1.0f);
    entry.roughness = 0.6f;
    entry.emissiveColor = glm::vec3(0.1f, 0.2f, 0.3f);
    entry.emissiveStrength = 2.0f;
    surfaces.overrides.push_back(entry);

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find("SurfaceOverrides") != std::string::npos, "the list was written");
    CHECK_MSG(text.find("BODY") != std::string::npos, "and the surface it names");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);

    bool found = false;
    for (auto e : loaded.view<SurfaceOverridesComponent>()) {
        const auto& back = loaded.get<SurfaceOverridesComponent>(e);
        found = true;
        CHECK_MSG(back.overrides.size() == size_t{1}, "one override came back");
        if (back.overrides.empty()) break;
        CHECK_MSG(back.overrides[0].surface == "BODY", back.overrides[0].surface);
        CHECK_NEAR(back.overrides[0].albedoColor.z, 0.55f);
        CHECK_NEAR(back.overrides[0].roughness, 0.6f);
        CHECK_NEAR(back.overrides[0].emissiveStrength, 2.0f);

        // And the lookup works on the loaded copy, which is what the draw loop
        // will do with it.
        CHECK_MSG(back.Find("BODY") != nullptr, "and it can still be found by name");
        CHECK_MSG(back.Find("DARK") == nullptr, "without matching a surface it does not name");
    }
    CHECK_MSG(found, "the entity came back with its overrides");
    cleanup();
}

// --- material descriptor sets going back to the pool -----------------------
//
// TextureRegistry cannot be built here: it needs a device. What it DECIDES can,
// and that is where both of its failures were. Neither was visible: a pool
// that never took a set back threw ErrorOutOfPoolMemory hundreds of drops in,
// while the cache it checked held a few dozen sets. Walking Magic Portals'
// lightmapped levels in one process did exactly that, at the 44th level.
// The Vulkan calls themselves - the free, and its deferral past the frames in
// flight - are proved only by that walk (MagicPortals --visit-levels).

// The registry's own key shape: one id per binding of the material layout.
using SetKey = std::array<uint32_t, VulkanPipeline::kMaterialBindingCount>;

static void testASetDroppedFromTheCacheStillFillsThePool() {
    // The undercount itself. Dropped from the cache, a set is still allocated
    // until its deferred free runs, so the room is not there yet.
    MaterialSets::Ledger ledger(3);
    std::map<SetKey, int> cache;
    for (int i = 0; i < 3; ++i) {
        cache.emplace(SetKey{uint32_t(i), 100, 200, 300}, 1000 + i);
        ledger.Taken();
    }
    CHECK_MSG(!ledger.HasRoom(), "three sets taken fill a pool of three");

    const std::vector<int> dropped = MaterialSets::TakeNaming(cache, {1});
    CHECK_EQ(dropped.size(), size_t{1});
    CHECK_EQ(cache.size(), size_t{2});
    CHECK_MSG(!ledger.HasRoom(),
              "a set out of the cache and not yet freed still occupies the pool; the cache's size "
              "said there was room, and the allocation threw");
    CHECK_EQ(ledger.Live(), uint32_t{3});

    CHECK_MSG(ledger.GivenBack(), "the deferred free gives it back");
    CHECK_MSG(ledger.HasRoom(), "and only then is there room");
    CHECK_EQ(ledger.Live(), uint32_t{2});
    CHECK_EQ(ledger.Live(), static_cast<uint32_t>(cache.size()));
}

static void testGivingBackWhatWasNeverTakenChangesNothing() {
    // A double free is a bug to report. Wrapped instead, the count would read
    // four billion and every material after it would draw the fallback.
    MaterialSets::Ledger ledger(2);
    ledger.Taken();
    CHECK(ledger.GivenBack());
    CHECK_MSG(!ledger.GivenBack(), "nothing is live, so nothing can be given back");
    CHECK_EQ(ledger.Live(), uint32_t{0});
    CHECK_MSG(ledger.HasRoom(), "and the pool is still usable");
}

static void testEveryBindingIsSearchedForADeadTexture() {
    // A dropped texture takes every set naming it, in whichever binding: a
    // set left naming a destroyed image is the null-sampler bug. The key is the
    // whole quadruple; the search must be too, and it must hand back what it
    // took, which the two hand-written loops it replaced simply erased.
    //
    // The overlay binding is the one the port drops most: a level's lightmaps
    // are invalidated every time it unloads.
    std::map<SetKey, int> cache{
        {SetKey{1, 2, 3, 15}, 10},    // albedo
        {SetKey{4, 5, 6, 15}, 20},
        {SetKey{7, 1, 9, 15}, 30},    // normal
        {SetKey{10, 11, 1, 15}, 40},  // ORM
        {SetKey{12, 13, 14, 15}, 50},
        {SetKey{16, 17, 18, 1}, 60},  // overlay
    };

    CHECK_MSG(MaterialSets::TakeNaming(cache, {}).empty(), "no dead ids take nothing");
    CHECK_MSG(MaterialSets::TakeNaming(cache, {99}).empty(), "an id no set names takes nothing");
    CHECK_EQ(cache.size(), size_t{6});

    const std::vector<int> taken = MaterialSets::TakeNaming(cache, {1});
    CHECK_EQ(taken.size(), size_t{4});
    CHECK_MSG(taken == std::vector<int>({10, 30, 40, 60}), "every binding that names it, the overlay's too, in key order");
    CHECK_EQ(cache.size(), size_t{2});
    CHECK_MSG(cache.count(SetKey{4, 5, 6, 15}) == 1 && cache.count(SetKey{12, 13, 14, 15}) == 1,
              "and nothing that does not");

    const std::vector<int> two = MaterialSets::TakeNaming(cache, {13, 5});
    CHECK_MSG(two == std::vector<int>({20, 50}), "several dead ids at once, as one Invalidate drops both colour spaces");
    CHECK_MSG(cache.empty(), "leaving nothing");
}

static void testThePoolHoldsTheWalkWithRoomToSpare() {
    // The cap the lighting design asks for: twice the old 512. At most 21
    // lightmaps in one Magic Portals level and 730 in the game; with sets given
    // back, the walk through all of them twice peaked at 96 live.
    CHECK_EQ(MaterialSets::kMaxSets, uint32_t{1024});
    MaterialSets::Ledger ledger(MaterialSets::kMaxSets);
    CHECK_EQ(ledger.Capacity(), uint32_t{1024});
    CHECK(ledger.HasRoom());
}

// --- the fourth map, the 2D record and the premultiplied blend -------------
//
// The overlay is a fourth binding in every material set, and the choices that
// can be wrong without a picture showing it are all here: which neutral a slot
// with no texture gets, where the switches and the light mask sit in the flags
// word and whether the shader reads them from the same bits, what a 2D sprite
// writes over the fields the unlit path never reads, and the factors the
// premultiplied pipeline composites with. None of it needs a device.

static void testAMaterialSetHasFourBindingsAndTheOverlayIsThird() {
    CHECK_EQ(VulkanPipeline::kMaterialBindingCount, uint32_t{4});
    CHECK_EQ(VulkanPipeline::kOverlayBinding, uint32_t{3});
    CHECK_EQ(sizeof(SetKey) / sizeof(uint32_t), size_t{4});
}

static void testASlotWithNoTextureFallsBackToItsOwnNeutral() {
    // Five textures exist (ids 0 to 4); an id past them names nothing. The
    // neutrals are distinct numbers so a slot handed another slot's neutral is
    // caught.
    const SetKey fallbacks{103, 101, 102, 104};  // checker, flat normal, neutral ORM, black
    const uint32_t count = 5;

    const SetKey named = MaterialSets::ResolveKey(SetKey{0, 1, 2, 4}, count, fallbacks);
    CHECK_MSG(named == SetKey({0, 1, 2, 4}), "ids that name textures are kept, every slot");

    const SetKey none = MaterialSets::ResolveKey(SetKey{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
                                                 count, fallbacks);
    CHECK_MSG(none == fallbacks, "each slot naming nothing gets its own neutral, in binding order");

    const SetKey onlyOverlay = MaterialSets::ResolveKey(SetKey{3, 1, 2, 5}, count, fallbacks);
    CHECK_MSG(onlyOverlay[VulkanPipeline::kOverlayBinding] == 104,
              "an overlay past the end is black - white would add a full-bright copy of nothing");
    CHECK_MSG(onlyOverlay[0] == 3 && onlyOverlay[1] == 1 && onlyOverlay[2] == 2,
              "and the other three are left alone");

    const SetKey edge = MaterialSets::ResolveKey(SetKey{5, 4, 4, 4}, count, fallbacks);
    CHECK_MSG(edge == SetKey({103, 4, 4, 4}), "the last id is a texture; one past it is not");
}

static void testAPremultipliedColourIsTakenWhole() {
    VulkanPipelineOptions options;
    options.blendEnable = true;
    options.blendEquation = BlendEquation::Premultiplied;
    const vk::PipelineColorBlendAttachmentState blend = ColorBlendFor(options);
    CHECK(blend.blendEnable == VK_TRUE);
    CHECK_MSG(blend.srcColorBlendFactor == vk::BlendFactor::eOne,
              "the colour already carries its alpha, so it is not weighted again");
    CHECK(blend.dstColorBlendFactor == vk::BlendFactor::eOneMinusSrcAlpha);
    CHECK(blend.colorBlendOp == vk::BlendOp::eAdd);
    CHECK_MSG(blend.srcAlphaBlendFactor == vk::BlendFactor::eOne &&
                  blend.dstAlphaBlendFactor == vk::BlendFactor::eOneMinusSrcAlpha &&
                  blend.alphaBlendOp == vk::BlendOp::eAdd,
              "and alpha composites as a mixed draw's does");

    // The two it joins are what they were: a new equation must not move them.
    VulkanPipelineOptions mix;
    mix.blendEnable = true;
    CHECK(ColorBlendFor(mix).srcColorBlendFactor == vk::BlendFactor::eSrcAlpha);
    CHECK(ColorBlendFor(mix).dstColorBlendFactor == vk::BlendFactor::eOneMinusSrcAlpha);
    CHECK(ColorBlendFor(mix).srcAlphaBlendFactor == vk::BlendFactor::eOne);
    VulkanPipelineOptions add = mix;
    add.blendEquation = BlendEquation::Add;
    CHECK(ColorBlendFor(add).srcColorBlendFactor == vk::BlendFactor::eSrcAlpha);
    CHECK(ColorBlendFor(add).dstColorBlendFactor == vk::BlendFactor::eOne);
    CHECK(ColorBlendFor(add).srcAlphaBlendFactor == vk::BlendFactor::eZero);
}

static void testTheSwitchesTheSlotAndTheMaskShareAWordWithoutTouching() {
    // Unlit, 2D, normal-down and premultiplied in the low byte, the slot in the
    // twelve bits above, the light mask in the eight above that. Packing any
    // of them must leave the others exactly as they were.
    CHECK_EQ(PushConstantData::kUnlit, 1);
    CHECK_EQ(PushConstantData::kSprite2D, 2);
    CHECK_EQ(PushConstantData::kNormalYDown, 4);
    CHECK_EQ(PushConstantData::kPremultiplied, 8);
    CHECK_EQ(kLightMaskShift, kUvSlotShift + 12);

    const int32_t switches = PushConstantData::kUnlit | PushConstantData::kSprite2D |
                             PushConstantData::kNormalYDown | PushConstantData::kPremultiplied;
    int32_t flags = PackUvSlot(switches, 4095);
    flags = PackLightMask(flags, 0xFF);
    CHECK_EQ(UnpackUvSlot(flags), 4095);
    CHECK_EQ(int(UnpackLightMask(flags)), 0xFF);
    CHECK_MSG((flags & 0xFF) == switches, "the switches survived both packings");
    CHECK_MSG(flags > 0, "and the sign bit was never reached");

    // Repacking replaces rather than accumulates, in both directions.
    flags = PackLightMask(flags, 0x02);
    CHECK_EQ(int(UnpackLightMask(flags)), 0x02);
    CHECK_EQ(UnpackUvSlot(flags), 4095);
    flags = PackUvSlot(flags, 7);
    CHECK_EQ(int(UnpackLightMask(flags)), 0x02);
    CHECK_EQ(UnpackUvSlot(flags), 7);
    CHECK_MSG((flags & 0xFF) == switches, "and the switches are still the switches");
    CHECK_EQ(int(UnpackLightMask(0)), 0);
}

static void testTheShaderReadsTheSwitchesFromTheSameBits() {
    // The same two-descriptions problem as the UV slot, for the new switches:
    // shader.frag spells each bit itself, and a disagreement draws a sprite
    // through the wrong exit with nothing else to say so.
    std::ifstream file("assets/shaders/shader.frag");
    CHECK_MSG(file.good(), "shader.frag must be readable from the working directory");
    if (!file.good()) return;
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const auto declares = [&source](const std::string& name, int32_t bit) {
        const std::string line = "const int " + name + " = 1 << " + std::to_string(bit) + ";";
        const size_t at = source.find("const int " + name + " ");
        if (at == std::string::npos) return false;
        // Compared with the spacing taken out, so aligning the declarations
        // is not a failure.
        std::string found = source.substr(at, source.find(';', at) - at + 1);
        found.erase(std::remove(found.begin(), found.end(), ' '), found.end());
        std::string wanted = line;
        wanted.erase(std::remove(wanted.begin(), wanted.end(), ' '), wanted.end());
        return found == wanted;
    };
    const auto bitOf = [](int32_t flag) {
        int32_t bit = 0;
        while ((1 << bit) != flag) ++bit;
        return bit;
    };

    CHECK_MSG(declares("FLAG_SPRITE2D", bitOf(PushConstantData::kSprite2D)),
              "shader.frag's FLAG_SPRITE2D is PushConstantData::kSprite2D's bit");
    CHECK_MSG(declares("FLAG_NORMAL_Y_DOWN", bitOf(PushConstantData::kNormalYDown)),
              "FLAG_NORMAL_Y_DOWN is kNormalYDown's bit");
    CHECK_MSG(declares("FLAG_PREMULTIPLIED", bitOf(PushConstantData::kPremultiplied)),
              "FLAG_PREMULTIPLIED is kPremultiplied's bit");

    const size_t maskAt = source.find("const int LIGHT_MASK_SHIFT");
    CHECK_MSG(maskAt != std::string::npos, "shader.frag declares LIGHT_MASK_SHIFT");
    if (maskAt == std::string::npos) return;
    std::string mask = source.substr(maskAt, source.find(';', maskAt) - maskAt + 1);
    mask.erase(std::remove(mask.begin(), mask.end(), ' '), mask.end());
    CHECK_MSG(mask == "constintLIGHT_MASK_SHIFT=" + std::to_string(kLightMaskShift) + ";",
              "LIGHT_MASK_SHIFT is kLightMaskShift: " + mask);

    CHECK_MSG(source.find("layout(set = 1, binding = " + std::to_string(VulkanPipeline::kOverlayBinding) +
                          ") uniform sampler2D overlayMap;") != std::string::npos,
              "the overlay is sampled from the binding the registry writes it to");
}

static void testTheShaderReadsThe2DLightsAsTheRendererWritesThem() {
    // Scene binding 12. Three more descriptions of one layout: GpuLight2D and its
    // header in core/Light2D.hpp, the buffer VulkanRenderer writes, and the block
    // shader.frag declares. A disagreement is not a validation error; it is every
    // sprite lit from somebody else's numbers, or a count read out of a light.
    std::ifstream file("assets/shaders/shader.frag");
    CHECK_MSG(file.good(), "shader.frag must be readable from the working directory");
    if (!file.good()) return;
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Compared with the spacing taken out, so aligning declarations is not a
    // failure and a changed word is.
    const auto squeezed = [](std::string text) {
        text.erase(std::remove_if(text.begin(), text.end(),
                                  [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }),
                   text.end());
        return text;
    };
    // The text from `start` to the first `end` after it, or empty.
    const auto between = [&source](const std::string& start, const std::string& end) {
        const size_t at = source.find(start);
        if (at == std::string::npos) return std::string();
        const size_t stop = source.find(end, at);
        if (stop == std::string::npos) return std::string();
        return source.substr(at, stop - at + end.size());
    };
    // Comments taken out, so the check reads the declaration and not its notes.
    const auto withoutComments = [](const std::string& text) {
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            if (text.compare(i, 2, "//") == 0) {
                while (i < text.size() && text[i] != '\n') ++i;
            } else {
                out += text[i++];
            }
        }
        return out;
    };

    CHECK_MSG(squeezed(withoutComments(between("struct Light2D {", "};"))) ==
                  "structLight2D{vec3position;floatrange;vec3color;uintlayers;};",
              "the shader's Light2D is GpuLight2D's four members, in its order");
    CHECK_EQ(offsetof(GpuLight2D, range), size_t(12));
    CHECK_EQ(offsetof(GpuLight2D, layers), size_t(28));

    CHECK_MSG(squeezed(withoutComments(between("layout(std430, set = 0, binding = 12)", "} light2D;"))) ==
                  "layout(std430,set=0,binding=12)readonlybufferLight2DBuffer{uintcount;uint_pad0;"
                  "uint_pad1;uint_pad2;Light2Dlights[];}light2D;",
              "binding 12 is a count, three words of padding and the lights: GpuLight2DHeader, 16 bytes");
    CHECK_EQ(sizeof(GpuLight2DHeader), size_t(16));

    CHECK_MSG(squeezed(between("const uint MAX_LIGHTS_2D", ";")) ==
                  "constuintMAX_LIGHTS_2D=" + std::to_string(kMaxLights2D) + "u;",
              "MAX_LIGHTS_2D is kMaxLights2D, the number the buffer is sized for");

    std::ostringstream bits;
    bits << "constuintLIGHT_MASK_BITS=0x" << std::uppercase << std::hex << kLightMaskBits << "u;";
    CHECK_MSG(squeezed(between("const uint LIGHT_MASK_BITS", ";")) == bits.str(),
              "and the mask it unpacks is as wide as PackLightMask packs: " + bits.str());

    // The loop reads what ApplySprite2D writes: the height from emissive.w and
    // the tint without ambient from emissive.rgb.
    CHECK_MSG(source.find("vec3 p = vec3(fragWorldPos.xy, instances[fragInstance].emissive.w);") !=
                  std::string::npos,
              "the light loop takes the surface's height from emissive.w");
    CHECK_MSG(source.find("vec3 tint = texel * instances[fragInstance].emissive.rgb;") != std::string::npos,
              "and the tint without the ambient from emissive.rgb");
}

static void testA2DSpriteWritesItsRecordAndNothingElseDoes() {
    // A material that did not ask writes exactly what it wrote before: the
    // fields a 2D sprite repurposes are the PBR path's, and a lit surface
    // reading its roughness out of an overlay strength would look plausible.
    PushConstantData untouched{};
    untouched.albedoColor = glm::vec4(0.2f, 0.4f, 0.6f, 0.8f);
    untouched.material = glm::vec4(0.4f, 0.1f, 1.0f, 0.25f);
    untouched.emissive = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    untouched.flags = PackUvSlot(PushConstantData::kUnlit, 9);

    MaterialComponent plain;
    plain.unlit = true;
    PushConstantData record = untouched;
    RenderSystem::ApplySprite2D(plain, record);
    CHECK_MSG(std::memcmp(&record, &untouched, sizeof(record)) == 0,
              "an unlit material without sprite2D leaves every byte of its record");

    MaterialComponent litSprite = plain;
    litSprite.unlit = false;
    litSprite.sprite2D.enabled = true;
    record = untouched;
    RenderSystem::ApplySprite2D(litSprite, record);
    CHECK_MSG(std::memcmp(&record, &untouched, sizeof(record)) == 0,
              "and so does a lit one that asks: the 2D record is the unlit path's");

    MaterialComponent sprite;
    sprite.unlit = true;
    sprite.albedoColor = glm::vec4(0.5f, 1.0f, 0.25f, 0.75f);
    sprite.alphaCutoff = 0.125f;
    sprite.sprite2D.enabled = true;
    sprite.sprite2D.ambient = glm::vec3(0.35f, 0.30f, 0.35f);
    sprite.sprite2D.height = -0.36f;
    sprite.sprite2D.lightMask = 0x03;
    sprite.sprite2D.normalYDown = true;
    sprite.sprite2D.overlayStrength = 0.5f;
    record = untouched;
    RenderSystem::ApplySprite2D(sprite, record);

    CHECK((record.flags & PushConstantData::kUnlit) != 0);
    CHECK((record.flags & PushConstantData::kSprite2D) != 0);
    CHECK((record.flags & PushConstantData::kNormalYDown) != 0);
    CHECK_MSG((record.flags & PushConstantData::kPremultiplied) == 0, "not blended, so not premultiplied");
    CHECK_EQ(int(UnpackLightMask(record.flags)), 0x03);
    CHECK_MSG(UnpackUvSlot(record.flags) == 9, "the UV slot the gather wrote is kept");
    CHECK_NEAR(record.albedoColor.r, 0.5f * 0.35f);
    CHECK_NEAR(record.albedoColor.g, 1.0f * 0.30f);
    CHECK_NEAR(record.albedoColor.b, 0.25f * 0.35f);
    CHECK_MSG(record.albedoColor.a == 0.75f, "alpha is the tint's own, never the ambient's");
    CHECK_MSG(record.emissive.r == 0.5f && record.emissive.g == 1.0f && record.emissive.b == 0.25f,
              "emissive.rgb is the tint without the ambient");
    CHECK_NEAR(record.emissive.w, -0.36f);
    CHECK_MSG(record.material.x == 0.5f && record.material.y == 0.0f && record.material.z == 0.0f,
              "material.x is the overlay's strength and nothing else rides there");
    CHECK_MSG(record.material.w == 0.125f, "and w is still the cutoff the discard reads");
    CHECK_MSG(record.model == untouched.model && record.skinPaletteBase == untouched.skinPaletteBase &&
                  record.probeIndex == untouched.probeIndex,
              "the fields it does not own are left");

    MaterialComponent noDown = sprite;
    noDown.sprite2D.normalYDown = false;
    noDown.sprite2D.lightMask = 0;
    record = untouched;
    RenderSystem::ApplySprite2D(noDown, record);
    CHECK((record.flags & PushConstantData::kNormalYDown) == 0);
    CHECK_EQ(int(UnpackLightMask(record.flags)), 0);
}

static void testOnlyABlendedPremultipliedMaterialSaysSo() {
    MaterialComponent material;
    material.blend = MaterialComponent::BlendMode::Premultiplied;

    PushConstantData record{};
    RenderSystem::ApplySprite2D(material, record);
    CHECK_MSG((record.flags & PushConstantData::kPremultiplied) == 0,
              "an opaque material ignores its blend, as the gather does");

    material.transparent = true;
    record = PushConstantData{};
    RenderSystem::ApplySprite2D(material, record);
    CHECK_MSG((record.flags & PushConstantData::kPremultiplied) != 0, "a blended one premultiplies, lit or not");
    CHECK_MSG((record.flags & PushConstantData::kSprite2D) == 0, "without becoming a sprite");

    for (const MaterialComponent::BlendMode other :
         {MaterialComponent::BlendMode::Alpha, MaterialComponent::BlendMode::Additive}) {
        material.blend = other;
        record = PushConstantData{};
        RenderSystem::ApplySprite2D(material, record);
        CHECK_MSG(record.flags == 0, "the other two blends set no switch at all");
    }
}

static void testAnOverlayAndA2DSpriteSurviveASaveAndLoad() {
    cleanup();
    entt::registry registry;
    const auto entity = makeEntity(registry, "Lit Wall");
    auto& material = registry.get<MaterialComponent>(entity);
    material.unlit = true;
    material.transparent = true;
    material.blend = MaterialComponent::BlendMode::Premultiplied;
    material.overlayTexturePath = "assets/textures/uv_grid.png";
    material.sprite2D.enabled = true;
    material.sprite2D.ambient = glm::vec3(0.35f, 0.3f, 0.35f);
    material.sprite2D.height = -0.36f;
    material.sprite2D.lightMask = 2;
    material.sprite2D.normalYDown = true;
    material.sprite2D.overlayStrength = 0.75f;

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find("\"Blend\": \"Premultiplied\"") != std::string::npos, "the blend was written, by name");
    CHECK_MSG(text.find("\"OverlayTexture\"") != std::string::npos, "the overlay was written");
    CHECK_MSG(text.find("\"Sprite2D\"") != std::string::npos, "and the 2D block");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);
    bool found = false;
    for (auto e : loaded.view<MaterialComponent>()) {
        const auto& m = loaded.get<MaterialComponent>(e);
        found = true;
        CHECK(m.blend == MaterialComponent::BlendMode::Premultiplied);
        CHECK(m.overlayTexturePath == "assets/textures/uv_grid.png");
        CHECK(m.sprite2D.enabled);
        CHECK_NEAR(m.sprite2D.ambient.r, 0.35f);
        CHECK_NEAR(m.sprite2D.ambient.g, 0.3f);
        CHECK_NEAR(m.sprite2D.height, -0.36f);
        CHECK_EQ(int(m.sprite2D.lightMask), 2);
        CHECK(m.sprite2D.normalYDown);
        CHECK_NEAR(m.sprite2D.overlayStrength, 0.75f);
    }
    CHECK_MSG(found, "the entity came back");
    cleanup();
}

static void testAMaterialThatNeverUsedThemWritesNeither() {
    // Omitted at the default, so every scene saved before this saves to the
    // same text - and one saved since, that uses neither, reads as before.
    cleanup();
    entt::registry registry;
    makeEntity(registry, "Plain");
    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find("OverlayTexture") == std::string::npos, "no overlay key");
    CHECK_MSG(text.find("Sprite2D") == std::string::npos, "no 2D block");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);
    for (auto e : loaded.view<MaterialComponent>()) {
        const auto& m = loaded.get<MaterialComponent>(e);
        CHECK(m.overlayTexturePath.empty());
        CHECK_MSG(m.sprite2D == MaterialComponent::Sprite2DLight{}, "and the default sprite settings");
    }

    // A block that says only that it is enabled reads every other field as
    // its default, not as zero: an ambient of zero would draw the sprite black.
    std::string partial = text;
    const size_t at = partial.find("\"Transparent\"");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        partial.insert(at, "\"Sprite2D\": { \"Enabled\": true },\n    ");
    }
    entt::registry partialLoaded;
    CHECK(SceneSerializer::DeserializeFromString(partialLoaded, partial).ok);
    for (auto e : partialLoaded.view<MaterialComponent>()) {
        const auto& m = partialLoaded.get<MaterialComponent>(e);
        CHECK(m.sprite2D.enabled);
        CHECK_MSG(m.sprite2D.ambient == glm::vec3(1.0f) && m.sprite2D.overlayStrength == 1.0f,
                  "the ambient and the strength default to one");
    }
    cleanup();
}

static void testABlendThisBuildDoesNotKnowMixes() {
    cleanup();
    entt::registry registry;
    const auto entity = makeEntity(registry, "Future");
    registry.get<MaterialComponent>(entity).transparent = true;
    registry.get<MaterialComponent>(entity).blend = MaterialComponent::BlendMode::Additive;
    std::string text = SceneSerializer::SerializeToString(registry);
    const size_t at = text.find("\"Additive\"");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, 10, "\"Subtractive\"");

    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, text).ok);
    for (auto e : loaded.view<MaterialComponent>()) {
        CHECK_MSG(loaded.get<MaterialComponent>(e).blend == MaterialComponent::BlendMode::Alpha,
                  "an unknown word loads mixing, as an unknown background loads the sky");
    }
    cleanup();
}

static void runTests() {
    testAMaterialSetHasFourBindingsAndTheOverlayIsThird();
    testASlotWithNoTextureFallsBackToItsOwnNeutral();
    testAPremultipliedColourIsTakenWhole();
    testTheSwitchesTheSlotAndTheMaskShareAWordWithoutTouching();
    testTheShaderReadsTheSwitchesFromTheSameBits();
    testTheShaderReadsThe2DLightsAsTheRendererWritesThem();
    testA2DSpriteWritesItsRecordAndNothingElseDoes();
    testOnlyABlendedPremultipliedMaterialSaysSo();
    testAnOverlayAndA2DSpriteSurviveASaveAndLoad();
    testAMaterialThatNeverUsedThemWritesNeither();
    testABlendThisBuildDoesNotKnowMixes();
    testASetDroppedFromTheCacheStillFillsThePool();
    testGivingBackWhatWasNeverTakenChangesNothing();
    testEveryBindingIsSearchedForADeadTexture();
    testThePoolHoldsTheWalkWithRoomToSpare();
    testAnOverrideReplacesOnlyTheSurfaceItNames();
    testTheMapsSurviveTheOverride();
    testAnOverrideThatNamesNothingIsInert();
    testTwoTeamsShareOneModel();
    testOverridesSurviveASaveAndLoad();
    testTheShaderAgreesAboutWhereTheSlotLives();
    testAUvTransformSurvivesASaveAndLoad();
    testAMaterialWrittenBeforeThisExistedLoadsUntransformed();
    testTheBlendSurvivesASaveAndLoad();
    testAMaterialWrittenBeforeTheBlendMixes();
    testTheIdentityAlwaysOccupiesSlotZero();
    testOnlyMaterialsThatActuallyScrollTakeASlot();
    testAFlipbookFrameComposesTheWayItsSourceDid();
    testScaleHappensBeforeRotationAndTranslationAfterBoth();
    testAMaterialThatStopsScrollingGoesBackToTheIdentity();
    testRunningOutOfSlotsDrawsUntransformedRatherThanOutOfBounds();
    testRunningOutOfSlotsIsReportedRatherThanSwallowed();
    testAFrameThatFitsReportsNothingDropped();
    testASlotAndTheUnlitSwitchShareAWordWithoutTouching();
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
    testAPackedMapSurvivesTheAssetRoundTrip();
    testReloadKeepsTheIdAndPicksUpTheNewValues();
    testReloadingAPathNobodyAcquiredDoesNothing();
    testABrokenFileKeepsTheValuesAlreadyLoaded();
    testFixingABrokenMaterialClearsTheCachedMiss();
    testSavingFromTheEditorDoesNotFireTheWatcher();
    testRenamingTheMaterialItselfKeepsItsIdAndItsEdits();
    cleanup();
}

TEST_MAIN("test_materials", 330)
