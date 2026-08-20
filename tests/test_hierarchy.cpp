// Tests for the transform hierarchy and Play/Stop.
//
// Transforms used to be flat - TransformComponent had no parent field at all -
// so nothing could be grouped or attached. The editor also simulated
// permanently, with no way to author a scene, try it, and get the authored
// state back.

#include "TestHarness.hpp"
#include "core/TransformSystem.hpp"
#include "core/PlayMode.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "core/SceneSerializer.hpp"
#include "core/Components.hpp"
#include "editor/SceneHierarchyPanel.hpp"

#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

using namespace Supersonic;

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

static void testStopClearsTheRewindFlag() {
    // Clear() reset the history but left s_isRewinding set. The only widget that
    // can clear the flag lives behind an early return taken when the history is
    // empty, and SupersonicApp gates physics, audio, scripts, particles AND the
    // frame recorder on !IsRewinding() - so pressing Stop while scrubbing left
    // Play and Step dead for the rest of the session, with no way back.
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Cube");
    registry.emplace<TransformComponent>(entity);

    PlayMode playMode;
    CHECK(playMode.Play(registry).ok);

    TimeTravelDebugger::SetRewinding(true);
    CHECK(TimeTravelDebugger::IsRewinding());

    CHECK(playMode.Stop(registry).ok);
    CHECK_MSG(!TimeTravelDebugger::IsRewinding(),
              "Stop must clear the rewind flag, or nothing simulates ever again");

    // And Play again must leave it clear, so the recorder can refill the history.
    TimeTravelDebugger::SetRewinding(true);
    CHECK(playMode.Play(registry).ok);
    CHECK_MSG(!TimeTravelDebugger::IsRewinding(), "Play must start un-paused too");

    TimeTravelDebugger::SetRewinding(false);
}

static void testRewindRestoresSpinAsWellAsFall() {
    // EntityStateSnapshot carried position, rotation, scale and linear
    // velocity. angularVelocity was added to RigidBodyComponent afterwards and
    // nothing brought it along, so rewinding stopped a body's fall and left it
    // spinning at whatever rate the scrub happened to end on - while the
    // comment beside the linear capture promised that rewinding "actually
    // rewinds its motion".
    TimeTravelDebugger::Clear();

    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Spinner");
    registry.emplace<TransformComponent>(entity);

    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.velocity = glm::vec3(0.0f, -4.0f, 0.0f);
    body.angularVelocity = glm::vec3(0.0f, 7.0f, 0.0f);

    TimeTravelDebugger::RecordFrame(registry, 0.0f);
    CHECK_EQ(TimeTravelDebugger::GetRecordedFrameCount(), size_t{1});

    // Whatever the simulation would have done between then and now.
    body.velocity = glm::vec3(3.0f, -19.0f, 1.0f);
    body.angularVelocity = glm::vec3(2.0f, -11.0f, 5.0f);

    CHECK_EQ(TimeTravelDebugger::RestoreFrame(registry, 0), size_t{1});

    const auto& restored = registry.get<RigidBodyComponent>(entity);
    CHECK_NEAR(restored.velocity.y, -4.0f);
    CHECK_MSG(::test::nearly(restored.angularVelocity.y, 7.0f),
              "rewinding must restore spin, not only fall");
    CHECK_NEAR(restored.angularVelocity.x, 0.0f);
    CHECK_NEAR(restored.angularVelocity.z, 0.0f);

    TimeTravelDebugger::Clear();
}


// --- the memoised resolve ---------------------------------------------------
//
// The resolve stops climbing the moment it reaches an ancestor already composed
// during this call, and marks every node it passes on the way back down. That
// is what stops ten siblings building their shared parent's matrix ten times.
// It also introduces the two ways a cache can be wrong: a stale entry that is
// believed, and a fresh entry that is skipped.

static void testASharedAncestorResolvesTheSameForEveryDescendant() {
    entt::registry registry;

    const auto root = registry.create();
    registry.emplace<TransformComponent>(root, glm::vec3(10.0f, 0.0f, 0.0f));

    // A deep chain, so the walk has something to climb and something to mark.
    entt::entity deepest = root;
    for (int i = 0; i < 5; ++i) {
        const auto link = registry.create();
        registry.emplace<TransformComponent>(link, glm::vec3(0.0f, 1.0f, 0.0f));
        registry.emplace<HierarchyComponent>(link, deepest);
        deepest = link;
    }

    // And a fan of siblings under the deepest link, which is the arrangement
    // the memoisation exists for: the first one resolves the whole chain and
    // the rest must find it done rather than redo it - and must get the same
    // answer either way.
    std::vector<entt::entity> siblings;
    for (int i = 0; i < 8; ++i) {
        const auto leaf = registry.create();
        registry.emplace<TransformComponent>(leaf, glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        registry.emplace<HierarchyComponent>(leaf, deepest);
        siblings.push_back(leaf);
    }

    TransformSystem::UpdateWorldTransforms(registry);

    // Root at x = 10, five links of +1 y, then the leaf's own x offset.
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 world =
            glm::vec3(registry.get<WorldTransformComponent>(siblings[i]).matrix[3]);
        CHECK_MSG(std::fabs(world.x - (10.0f + static_cast<float>(i))) < 1e-4f,
                  "sibling " + std::to_string(i) + " x = " + std::to_string(world.x));
        CHECK_MSG(std::fabs(world.y - 5.0f) < 1e-4f,
                  "sibling " + std::to_string(i) + " y = " + std::to_string(world.y));
    }

    // The chain itself has to be right too, not merely the leaves: every node
    // on the way down is written by the same loop that composes them.
    const glm::vec3 deepestWorld =
        glm::vec3(registry.get<WorldTransformComponent>(deepest).matrix[3]);
    CHECK_MSG(std::fabs(deepestWorld.y - 5.0f) < 1e-4f,
              "the marked ancestors must be correct, not only the leaves: y = " +
                  std::to_string(deepestWorld.y));
}

static void testASecondResolveInTheSameFrameSeesTheEdit() {
    // The application calls UpdateWorldTransforms TWICE per frame - once before
    // the editor builds its UI and once after, because the editor may have
    // moved something in between. A cache keyed on the frame rather than on the
    // call would make the second one a no-op, and a gizmo drag would show up
    // one frame late in everything that reads world matrices.
    entt::registry registry;

    const auto parent = registry.create();
    registry.emplace<TransformComponent>(parent, glm::vec3(0.0f, 0.0f, 0.0f));

    const auto child = registry.create();
    registry.emplace<TransformComponent>(child, glm::vec3(2.0f, 0.0f, 0.0f));
    registry.emplace<HierarchyComponent>(child, parent);

    TransformSystem::UpdateWorldTransforms(registry);
    CHECK_NEAR(glm::vec3(registry.get<WorldTransformComponent>(child).matrix[3]).x, 2.0f);

    registry.get<TransformComponent>(parent).position = glm::vec3(0.0f, 7.0f, 0.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    const glm::vec3 moved = glm::vec3(registry.get<WorldTransformComponent>(child).matrix[3]);
    CHECK_MSG(std::fabs(moved.y - 7.0f) < 1e-4f,
              "a second resolve must see the move: y = " + std::to_string(moved.y));

    // And the rootless path has to advance too, not only the parented one.
    const glm::vec3 parentWorld =
        glm::vec3(registry.get<WorldTransformComponent>(parent).matrix[3]);
    CHECK_MSG(std::fabs(parentWorld.y - 7.0f) < 1e-4f,
              "the parent itself must have moved: y = " + std::to_string(parentWorld.y));
}


// --- the hierarchy panel's index --------------------------------------------
//
// Drawing the tree used to answer "what are this row's children" by scanning
// the whole HierarchyComponent pool, once per row - quadratic, and on two
// thousand entities it was 2.9 ms of a 1.5 ms release frame. One pass builds
// the answer for every row instead.
//
// Tested here rather than through the panel because the panel needs an ImGui
// context, and because this is the part that can be WRONG rather than merely
// slow: an entity misfiled by the index is not drawn as a root and is not
// drawn by its parent either, which looks exactly like having been deleted.

static void testEveryEntityIsEitherARootOrSomebodysChild() {
    entt::registry registry;
    std::unordered_map<entt::entity, std::vector<entt::entity>> children;
    std::vector<entt::entity> roots;

    const auto parent = registry.create();
    registry.emplace<TransformComponent>(parent);

    std::vector<entt::entity> kids;
    for (int i = 0; i < 4; ++i) {
        const auto child = registry.create();
        registry.emplace<TransformComponent>(child);
        registry.emplace<HierarchyComponent>(child, parent);
        kids.push_back(child);
    }
    const auto loner = registry.create();
    registry.emplace<TransformComponent>(loner);

    SceneHierarchyPanel::BuildIndex(registry, children, roots);

    CHECK_MSG(roots.size() == 2, "the parent and the loner are roots: got " +
                                     std::to_string(roots.size()));
    CHECK_MSG(children[parent].size() == 4,
              "all four children must be filed under the parent: got " +
                  std::to_string(children[parent].size()));

    // Nothing may be lost and nothing may be counted twice - the two failures
    // that look like a deleted entity and a duplicated one.
    size_t seen = roots.size();
    for (const auto& bucket : children) seen += bucket.second.size();
    CHECK_MSG(seen == 6, "every entity appears exactly once: got " + std::to_string(seen));
}

static void testAChildOfADestroyedParentBecomesARoot() {
    // Entity handles are recycled, so a child left pointing at a released one
    // would later be filed under whatever took that slot - drawn inside a
    // stranger's subtree. It has to come back as a root instead.
    entt::registry registry;
    std::unordered_map<entt::entity, std::vector<entt::entity>> children;
    std::vector<entt::entity> roots;

    const auto parent = registry.create();
    registry.emplace<TransformComponent>(parent);
    const auto child = registry.create();
    registry.emplace<TransformComponent>(child);
    registry.emplace<HierarchyComponent>(child, parent);

    registry.destroy(parent);

    SceneHierarchyPanel::BuildIndex(registry, children, roots);

    CHECK_MSG(roots.size() == 1, "only the orphan is left: got " + std::to_string(roots.size()));
    CHECK_MSG(!roots.empty() && roots[0] == child,
              "an orphan must be drawn as a root, not hidden under a dead handle");

    // A HierarchyComponent whose parent is explicitly null is the same case.
    entt::registry detached;
    const auto lone = detached.create();
    detached.emplace<TransformComponent>(lone);
    detached.emplace<HierarchyComponent>(lone, entt::null);

    children.clear();
    SceneHierarchyPanel::BuildIndex(detached, children, roots);
    CHECK_MSG(roots.size() == 1 && roots[0] == lone,
              "a null parent is a root, not a child of nothing");
}

static void testTheIndexIsRebuiltNotAppended() {
    // The map is a member kept between frames so it stops allocating, which
    // means a stale bucket is a child drawn twice - or a child still drawn
    // under a parent it has since left.
    entt::registry registry;
    std::unordered_map<entt::entity, std::vector<entt::entity>> children;
    std::vector<entt::entity> roots;

    const auto first = registry.create();
    registry.emplace<TransformComponent>(first);
    const auto second = registry.create();
    registry.emplace<TransformComponent>(second);
    const auto child = registry.create();
    registry.emplace<TransformComponent>(child);
    registry.emplace<HierarchyComponent>(child, first);

    SceneHierarchyPanel::BuildIndex(registry, children, roots);
    CHECK(children[first].size() == 1);

    // Reparented, then rebuilt into the SAME containers.
    registry.get<HierarchyComponent>(child).parent = second;
    SceneHierarchyPanel::BuildIndex(registry, children, roots);

    CHECK_MSG(children[first].empty(),
              "the old parent must not keep the child it lost: got " +
                  std::to_string(children[first].size()));
    CHECK_MSG(children[second].size() == 1, "the new parent must have it");
    CHECK_MSG(roots.size() == 2, "and the roots must not accumulate: got " +
                                     std::to_string(roots.size()));
}

static void runTests() {
    testEveryEntityIsEitherARootOrSomebodysChild();
    testAChildOfADestroyedParentBecomesARoot();
    testTheIndexIsRebuiltNotAppended();
    testASharedAncestorResolvesTheSameForEveryDescendant();
    testASecondResolveInTheSameFrameSeesTheEdit();
    testStopClearsTheRewindFlag();
    testRewindRestoresSpinAsWellAsFall();
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

TEST_MAIN("test_hierarchy", 84)
