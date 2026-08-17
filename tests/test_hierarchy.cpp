// Tests for the transform hierarchy and Play/Stop.
//
// Transforms used to be flat - TransformComponent had no parent field at all -
// so nothing could be grouped or attached. The editor also simulated
// permanently, with no way to author a scene, try it, and get the authored
// state back.

#include "TestHarness.hpp"
#include "core/TransformSystem.hpp"
#include "core/PlayMode.hpp"
#include "core/SceneSerializer.hpp"
#include "core/Components.hpp"

#include <glm/gtc/matrix_transform.hpp>

using namespace Engine;

namespace {

entt::entity makeEntity(entt::registry& registry, const std::string& name, const glm::vec3& position) {
    const auto e = registry.create();
    registry.emplace<TagComponent>(e, name);
    registry.emplace<TransformComponent>(e, position);
    return e;
}

glm::vec3 worldPositionOf(entt::registry& registry, entt::entity e) {
    return glm::vec3(TransformSystem::GetWorldMatrix(registry, e)[3]);
}

} // namespace

static void testChildInheritsParentTranslation() {
    entt::registry registry;
    const auto parent = makeEntity(registry, "Parent", glm::vec3(10.0f, 0.0f, 0.0f));
    const auto child = makeEntity(registry, "Child", glm::vec3(0.0f, 2.0f, 0.0f));

    registry.emplace<HierarchyComponent>(child, parent);
    TransformSystem::UpdateWorldTransforms(registry);

    const glm::vec3 world = worldPositionOf(registry, child);
    CHECK_NEAR(world.x, 10.0f);
    CHECK_NEAR(world.y, 2.0f);
}

static void testChildInheritsParentScaleAndRotation() {
    entt::registry registry;
    const auto parent = makeEntity(registry, "Parent", glm::vec3(0.0f));
    registry.get<TransformComponent>(parent).scale = glm::vec3(2.0f);
    registry.get<TransformComponent>(parent).rotation = glm::vec3(0.0f, glm::radians(90.0f), 0.0f);

    const auto child = makeEntity(registry, "Child", glm::vec3(1.0f, 0.0f, 0.0f));
    registry.emplace<HierarchyComponent>(child, parent);
    TransformSystem::UpdateWorldTransforms(registry);

    // Local +X, scaled by 2 and rotated 90 degrees about Y, lands on -Z.
    const glm::vec3 world = worldPositionOf(registry, child);
    CHECK_NEAR(world.x, 0.0f);
    CHECK_NEAR(world.z, -2.0f);
}

static void testGrandchildChains() {
    entt::registry registry;
    const auto a = makeEntity(registry, "A", glm::vec3(1.0f, 0.0f, 0.0f));
    const auto b = makeEntity(registry, "B", glm::vec3(2.0f, 0.0f, 0.0f));
    const auto c = makeEntity(registry, "C", glm::vec3(4.0f, 0.0f, 0.0f));

    registry.emplace<HierarchyComponent>(b, a);
    registry.emplace<HierarchyComponent>(c, b);
    TransformSystem::UpdateWorldTransforms(registry);

    CHECK_NEAR(worldPositionOf(registry, c).x, 7.0f);
}

static void testMovingParentMovesSubtree() {
    entt::registry registry;
    const auto parent = makeEntity(registry, "Parent", glm::vec3(0.0f));
    const auto child = makeEntity(registry, "Child", glm::vec3(0.0f, 3.0f, 0.0f));
    registry.emplace<HierarchyComponent>(child, parent);

    TransformSystem::UpdateWorldTransforms(registry);
    CHECK_NEAR(worldPositionOf(registry, child).y, 3.0f);

    registry.get<TransformComponent>(parent).position = glm::vec3(0.0f, 10.0f, 0.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    CHECK_MSG(std::abs(worldPositionOf(registry, child).y - 13.0f) < 1e-3f,
              "moving a parent must move its whole subtree");
}

static void testSetParentPreservesWorldPosition() {
    // Attaching in the hierarchy must not teleport the object.
    entt::registry registry;
    const auto parent = makeEntity(registry, "Parent", glm::vec3(5.0f, 0.0f, 0.0f));
    const auto child = makeEntity(registry, "Child", glm::vec3(1.0f, 1.0f, 0.0f));
    TransformSystem::UpdateWorldTransforms(registry);

    const glm::vec3 before = worldPositionOf(registry, child);
    CHECK(TransformSystem::SetParent(registry, child, parent));
    TransformSystem::UpdateWorldTransforms(registry);
    const glm::vec3 after = worldPositionOf(registry, child);

    CHECK_NEAR(after.x, before.x);
    CHECK_NEAR(after.y, before.y);
    CHECK_NEAR(after.z, before.z);
    // Its LOCAL position must have changed to compensate.
    CHECK_NEAR(registry.get<TransformComponent>(child).position.x, -4.0f);
}

static void testDetachPreservesWorldPosition() {
    entt::registry registry;
    const auto parent = makeEntity(registry, "Parent", glm::vec3(5.0f, 2.0f, 0.0f));
    const auto child = makeEntity(registry, "Child", glm::vec3(1.0f, 0.0f, 0.0f));
    registry.emplace<HierarchyComponent>(child, parent);
    TransformSystem::UpdateWorldTransforms(registry);

    const glm::vec3 before = worldPositionOf(registry, child);
    CHECK(TransformSystem::SetParent(registry, child, entt::null));
    TransformSystem::UpdateWorldTransforms(registry);

    const glm::vec3 after = worldPositionOf(registry, child);
    CHECK_NEAR(after.x, before.x);
    CHECK_NEAR(after.y, before.y);
}

static void testCyclesAreRejected() {
    entt::registry registry;
    const auto a = makeEntity(registry, "A", glm::vec3(0.0f));
    const auto b = makeEntity(registry, "B", glm::vec3(0.0f));
    const auto c = makeEntity(registry, "C", glm::vec3(0.0f));

    CHECK(TransformSystem::SetParent(registry, b, a));
    CHECK(TransformSystem::SetParent(registry, c, b));

    // a -> b -> c, so parenting a under c would close a loop the resolver would
    // otherwise have to break every frame.
    CHECK_MSG(!TransformSystem::SetParent(registry, a, c), "a cycle must be rejected");
    CHECK_MSG(!TransformSystem::SetParent(registry, a, a), "self-parenting must be rejected");

    // And the resolve must still terminate.
    TransformSystem::UpdateWorldTransforms(registry);
    CHECK(true);
}

static void testDestroyingParentPromotesChildren() {
    entt::registry registry;
    const auto parent = makeEntity(registry, "Parent", glm::vec3(4.0f, 1.0f, 0.0f));
    const auto child = makeEntity(registry, "Child", glm::vec3(1.0f, 0.0f, 0.0f));
    registry.emplace<HierarchyComponent>(child, parent);
    TransformSystem::UpdateWorldTransforms(registry);

    const glm::vec3 before = worldPositionOf(registry, child);

    TransformSystem::OnParentDestroyed(registry, parent);
    registry.destroy(parent);
    TransformSystem::UpdateWorldTransforms(registry);

    // The child must survive at the same place, not point at a released handle
    // that entity recycling could later turn into a different parent.
    CHECK_NEAR(worldPositionOf(registry, child).x, before.x);
    const auto* hierarchy = registry.try_get<HierarchyComponent>(child);
    CHECK_MSG(!hierarchy || hierarchy->parent == entt::null,
              "an orphaned child must be promoted to the root");
}

static void testHierarchySurvivesSerialization() {
    entt::registry source;
    const auto parent = makeEntity(source, "Parent", glm::vec3(7.0f, 0.0f, 0.0f));
    const auto child = makeEntity(source, "Child", glm::vec3(0.0f, 2.0f, 0.0f));
    source.emplace<HierarchyComponent>(child, parent);

    const std::string text = SceneSerializer::SerializeToString(source);

    entt::registry loaded;
    const auto result = SceneSerializer::DeserializeFromString(loaded, text);
    CHECK_MSG(result.ok, result.message);

    TransformSystem::UpdateWorldTransforms(loaded);

    entt::entity loadedChild = entt::null;
    for (auto e : loaded.view<TagComponent>()) {
        if (loaded.get<TagComponent>(e).tag == "Child") loadedChild = e;
    }
    CHECK(loadedChild != entt::null);
    if (loadedChild != entt::null) {
        // Parent links are stored as an array index, not a raw handle, because
        // handles are recycled and carry a version.
        CHECK_MSG(std::abs(worldPositionOf(loaded, loadedChild).x - 7.0f) < 1e-3f,
                  "the parent link must survive a round trip");
    }
}

// ---------------------------------------------------------------------------
// Play / Stop
// ---------------------------------------------------------------------------

static void testPlayStopRestoresScene() {
    entt::registry registry;
    const auto cube = makeEntity(registry, "Cube", glm::vec3(0.0f, 5.0f, 0.0f));
    registry.emplace<RigidBodyComponent>(cube);

    PlayMode play;
    CHECK(play.IsEditing());

    const auto started = play.Play(registry);
    CHECK_MSG(started.ok, started.message);
    CHECK(play.IsPlaying());
    CHECK(play.ShouldSimulate());

    // Simulate gameplay mutating the scene.
    registry.get<TransformComponent>(cube).position = glm::vec3(0.0f, 0.35f, 0.0f);
    registry.destroy(makeEntity(registry, "Temporary", glm::vec3(0.0f)));
    makeEntity(registry, "SpawnedDuringPlay", glm::vec3(1.0f));

    const auto stopped = play.Stop(registry);
    CHECK_MSG(stopped.ok, stopped.message);
    CHECK(play.IsEditing());
    CHECK_MSG(!play.ShouldSimulate(), "gameplay must not run in edit mode");

    // The authored state comes back, and anything spawned during play is gone.
    size_t count = 0;
    bool foundCube = false;
    bool foundSpawned = false;
    for (auto e : registry.view<TagComponent>()) {
        ++count;
        const auto& tag = registry.get<TagComponent>(e).tag;
        if (tag == "Cube") {
            foundCube = true;
            CHECK_NEAR(registry.get<TransformComponent>(e).position.y, 5.0f);
        }
        if (tag == "SpawnedDuringPlay") foundSpawned = true;
    }
    CHECK_MSG(foundCube, "the authored entity must come back");
    CHECK_MSG(!foundSpawned, "entities spawned during play must not survive Stop");
    CHECK_EQ(count, size_t{1});
}

static void testPauseAndStep() {
    entt::registry registry;
    makeEntity(registry, "A", glm::vec3(0.0f));

    PlayMode play;
    play.Play(registry);
    CHECK(play.ShouldSimulate());

    play.Pause();
    CHECK(play.IsPaused());
    CHECK_MSG(!play.ShouldSimulate(), "a paused scene must not simulate continuously");

    CHECK_MSG(!play.ConsumeSingleStep(), "no step is pending until one is requested");
    play.RequestSingleStep();
    CHECK_MSG(play.ConsumeSingleStep(), "a requested step must fire exactly once");
    CHECK_MSG(!play.ConsumeSingleStep(), "and only once");

    play.Resume();
    CHECK(play.IsPlaying());
}

static void testInvalidSnapshotLeavesSceneIntact() {
    // applyScene must clear before it can populate, so a document that parses
    // but is not a usable scene has to be rejected while the live scene is
    // still intact - the same principle that made a failed file load
    // non-destructive.
    entt::registry registry;
    makeEntity(registry, "Precious", glm::vec3(1.0f));

    const std::string bad =
        R"({"Scene":"x","Entities":[{"Tag":"A","Parent":99}]})";

    const auto result = SceneSerializer::DeserializeFromString(registry, bad);
    CHECK_MSG(!result.ok, "an out-of-range Parent index must be rejected");

    size_t count = 0;
    bool foundPrecious = false;
    for (auto e : registry.view<TagComponent>()) {
        ++count;
        if (registry.get<TagComponent>(e).tag == "Precious") foundPrecious = true;
    }
    CHECK_MSG(count == 1 && foundPrecious, "a rejected snapshot must not destroy the scene");
}

static void testStopWithoutPlayIsHarmless() {
    entt::registry registry;
    makeEntity(registry, "A", glm::vec3(0.0f));

    PlayMode play;
    const auto result = play.Stop(registry);
    CHECK_MSG(result.ok, "stopping while already in edit mode must be a no-op");

    size_t count = 0;
    for (auto e : registry.view<TagComponent>()) { (void)e; ++count; }
    CHECK_MSG(count == 1, "a spurious Stop must not clear the scene");
}

static void runTests() {
    testChildInheritsParentTranslation();
    testChildInheritsParentScaleAndRotation();
    testGrandchildChains();
    testMovingParentMovesSubtree();
    testSetParentPreservesWorldPosition();
    testDetachPreservesWorldPosition();
    testCyclesAreRejected();
    testDestroyingParentPromotesChildren();
    testHierarchySurvivesSerialization();
    testPlayStopRestoresScene();
    testPauseAndStep();
    testInvalidSnapshotLeavesSceneIntact();
    testStopWithoutPlayIsHarmless();
}

TEST_MAIN("test_hierarchy")
