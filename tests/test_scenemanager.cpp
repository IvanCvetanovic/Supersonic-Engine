// Tests for which scene is open.
//
// Nothing owned that. `kScenePath` was a constexpr in EditorLayer.cpp and the
// only argument Save, Open and both shortcuts ever passed, so the engine had
// exactly one scene by construction - and GamePackager wrote the same literal
// into every manifest, meaning "package the current scene" shipped whatever was
// last written to MainScene.scene regardless of what was actually open.
//
// The property pinned hardest here is that loading is DEFERRED. A load clears
// and refills the registry, and the editor asks for one from inside its own UI
// build, while panels are iterating views over that registry.

#include "TestHarness.hpp"
#include "core/SceneManager.hpp"
#include "core/Components.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace Supersonic;

namespace {

size_t countEntities(entt::registry& registry) {
    return static_cast<size_t>(registry.storage<entt::entity>().free_list());
}

entt::entity makeTagged(entt::registry& registry, const char* name) {
    const auto e = registry.create();
    registry.emplace<TagComponent>(e, name);
    registry.emplace<TransformComponent>(e);
    return e;
}

} // namespace

static void testStartsOnTheDefaultAndCleanly() {
    SceneManager scenes;
    CHECK(scenes.CurrentPath() == SceneManager::kDefaultScene);
    CHECK_MSG(!scenes.IsDirty(), "a freshly opened scene has no unsaved changes");
    CHECK_MSG(!scenes.HasPending(), "nothing is queued until something asks");
}

static void testRequestLoadDoesNotTouchTheRegistryUntilApplied() {
    // The whole reason the request is deferred. If RequestLoad loaded, calling
    // it from inside BuildUI would clear the registry the panels are walking.
    const std::string path = "test_sm_deferred_tmp.scene";
    {
        entt::registry source;
        makeTagged(source, "OnDisk");
        CHECK(SceneSerializer::Serialize(source, path).ok);
    }

    entt::registry registry;
    makeTagged(registry, "AlreadyOpen");
    makeTagged(registry, "AlsoOpen");

    SceneManager scenes;
    scenes.RequestLoad(path);

    CHECK_MSG(scenes.HasPending(), "the request should be recorded");
    CHECK_MSG(countEntities(registry) == 2,
              "RequestLoad must not touch the registry");
    CHECK_MSG(scenes.CurrentPath() == SceneManager::kDefaultScene,
              "and must not adopt the path before the load has happened");

    SerializationResult result{};
    CHECK(scenes.ApplyPending(registry, result));
    std::remove(path.c_str());

    CHECK_MSG(result.ok, result.message);
    CHECK_EQ(countEntities(registry), size_t{1});
    CHECK(scenes.CurrentPath() == path);
    CHECK_MSG(!scenes.HasPending(), "applying consumes the request");
}

static void testAFailedLoadKeepsTheOpenSceneAndItsName() {
    // Deserialize is deliberately non-destructive - it parses before it touches
    // the registry - so the name has to agree with that rather than pointing at
    // a file that was never loaded.
    entt::registry registry;
    makeTagged(registry, "StillHere");

    SceneManager scenes;
    scenes.RequestLoad("test_sm_definitely_missing_4417.scene");

    SerializationResult result{};
    CHECK(scenes.ApplyPending(registry, result));

    CHECK_MSG(!result.ok, "a missing scene must fail");
    CHECK_EQ(countEntities(registry), size_t{1});
    CHECK_MSG(scenes.CurrentPath() == SceneManager::kDefaultScene,
              "a failed load must not become the current scene");
}

static void testSaveAsMovesTheCurrentPath() {
    const std::string path = "test_sm_saveas_tmp.scene";
    entt::registry registry;
    makeTagged(registry, "Cube");

    SceneManager scenes;
    scenes.MarkDirty();
    CHECK(scenes.IsDirty());

    const auto result = scenes.SaveAs(registry, path);
    CHECK_MSG(result.ok, result.message);
    CHECK(scenes.CurrentPath() == path);
    CHECK_MSG(!scenes.IsDirty(), "a successful save clears the dirty flag");

    // And a plain Save now writes THERE, which is the point of Save As.
    scenes.MarkDirty();
    CHECK(scenes.Save(registry).ok);
    CHECK(scenes.CurrentPath() == path);

    std::ifstream check(path);
    CHECK_MSG(check.good(), "Save must have written to the new path");
    check.close();
    std::remove(path.c_str());
}

static void testAFailedSaveLeavesTheSceneDirty() {
    // Reporting a scene clean after a failed write is how the work is lost at
    // the next Open, with nothing having said so.
    entt::registry registry;
    makeTagged(registry, "Cube");

    SceneManager scenes;
    scenes.MarkDirty();

    // NOT a missing directory: SceneSerializer creates the parent on purpose,
    // so saving into a new folder is meant to work. This uses a path whose
    // "directory" component is an existing FILE, which no filesystem will
    // accept as one.
    const std::string blocker = "test_sm_blocker_tmp";
    { std::ofstream f(blocker); f << "not a directory"; }

    const auto result = scenes.SaveAs(registry, blocker + "/x.scene");
    std::remove(blocker.c_str());
    CHECK_MSG(!result.ok, "writing under a path that is a file must fail");
    CHECK_MSG(scenes.IsDirty(), "a failed save must leave the scene dirty");
    CHECK_MSG(scenes.CurrentPath() == SceneManager::kDefaultScene,
              "and must not adopt the path it could not write");
}

static void testNewSceneEmptiesAndResets() {
    entt::registry registry;
    makeTagged(registry, "A");
    makeTagged(registry, "B");

    SceneManager scenes;
    scenes.MarkDirty();
    scenes.RequestNew();
    CHECK(scenes.HasPending());
    CHECK_MSG(countEntities(registry) == 2, "RequestNew is deferred too");

    SerializationResult result{};
    CHECK(scenes.ApplyPending(registry, result));
    CHECK(result.ok);
    CHECK_EQ(countEntities(registry), size_t{0});
    CHECK(!scenes.IsDirty());
}

static void testApplyPendingIsANoOpWhenNothingIsQueued() {
    entt::registry registry;
    makeTagged(registry, "Untouched");

    SceneManager scenes;
    SerializationResult result{};
    CHECK_MSG(!scenes.ApplyPending(registry, result),
              "with nothing queued, ApplyPending must report that and do nothing");
    CHECK_EQ(countEntities(registry), size_t{1});
}

static void runTests() {
    testStartsOnTheDefaultAndCleanly();
    testRequestLoadDoesNotTouchTheRegistryUntilApplied();
    testAFailedLoadKeepsTheOpenSceneAndItsName();
    testSaveAsMovesTheCurrentPath();
    testAFailedSaveLeavesTheSceneDirty();
    testNewSceneEmptiesAndResets();
    testApplyPendingIsANoOpWhenNothingIsQueued();
}

TEST_MAIN("test_scenemanager", 25)
