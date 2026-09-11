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
#include "core/MaterialLibrary.hpp"
#include "core/MaterialSystem.hpp"
#include "core/RenderSystem.hpp"
#include "core/SceneSerializer.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>

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

static void runTests() {
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

TEST_MAIN("test_materials", 210)
