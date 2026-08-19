// Regression tests for editor undo/redo.
//
// The failure modes here are quiet ones: a redo stack that survives a new edit
// replays a scene that no longer exists, a commit that fires mid-drag turns one
// gizmo movement into a hundred steps, and an unbounded stack grows forever in
// a long session. None of those show up as a crash.

#include "TestHarness.hpp"
#include "editor/EditHistory.hpp"
#include "core/Components.hpp"
#include "core/SceneSerializer.hpp"

#include <string>

using namespace Supersonic;

static size_t entityCount(entt::registry& registry) {
    size_t count = 0;
    for ([[maybe_unused]] auto entity : registry.view<entt::entity>()) ++count;
    return count;
}

static entt::entity addEntity(entt::registry& registry, const std::string& name,
                              const glm::vec3& position = glm::vec3(0.0f)) {
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, name);
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    return entity;
}

static void testFreshHistoryHasNothingToUndo() {
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    history.Reset(registry);

    CHECK_MSG(!history.CanUndo(), "a freshly reset history must have no steps");
    CHECK_MSG(!history.CanRedo(), "and nothing to redo");
    CHECK_MSG(!history.Undo(registry), "undoing an empty history must be a no-op");
    CHECK_MSG(!history.Redo(registry), "and so must redoing one");
}

static void testCommitWithoutChangeRecordsNothing() {
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    history.Reset(registry);

    CHECK_MSG(!history.CommitIfChanged(registry), "an unchanged scene must not become a step");
    CHECK_MSG(!history.CommitIfChanged(registry), "however many times it is asked");
    CHECK_EQ(history.UndoDepth(), size_t{0});
}

static void testUndoRestoresADeletedEntity() {
    entt::registry registry;
    addEntity(registry, "Cube");
    const auto doomed = addEntity(registry, "Sphere");

    EditHistory history;
    history.Reset(registry);

    registry.destroy(doomed);
    CHECK_EQ(entityCount(registry), size_t{1});
    CHECK_MSG(history.CommitIfChanged(registry), "a deletion must be recorded");

    CHECK_MSG(history.Undo(registry), "undo must succeed");
    CHECK_EQ(entityCount(registry), size_t{2});
}

static void testUndoRemovesACreatedEntity() {
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    history.Reset(registry);

    addEntity(registry, "Sphere");
    CHECK(history.CommitIfChanged(registry));

    CHECK(history.Undo(registry));
    CHECK_EQ(entityCount(registry), size_t{1});
}

static void testUndoRestoresAFieldEdit() {
    entt::registry registry;
    const auto entity = addEntity(registry, "Cube", glm::vec3(1.0f, 2.0f, 3.0f));

    EditHistory history;
    history.Reset(registry);

    registry.get<TransformComponent>(entity).position = glm::vec3(9.0f, 9.0f, 9.0f);
    CHECK(history.CommitIfChanged(registry));
    CHECK(history.Undo(registry));

    // The handle is stale after a restore, so the value is checked by name.
    bool found = false;
    for (auto e : registry.view<TagComponent, TransformComponent>()) {
        if (registry.get<TagComponent>(e).tag != "Cube") continue;
        found = true;
        const auto& t = registry.get<TransformComponent>(e);
        CHECK_NEAR(t.position.x, 1.0f);
        CHECK_NEAR(t.position.y, 2.0f);
        CHECK_NEAR(t.position.z, 3.0f);
    }
    CHECK_MSG(found, "the entity must still exist after the undo");
}

static void testRedoReappliesTheChange() {
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    history.Reset(registry);

    addEntity(registry, "Sphere");
    CHECK(history.CommitIfChanged(registry));

    CHECK(history.Undo(registry));
    CHECK_EQ(entityCount(registry), size_t{1});
    CHECK_MSG(history.CanRedo(), "an undo must leave something to redo");

    CHECK(history.Redo(registry));
    CHECK_EQ(entityCount(registry), size_t{2});
    CHECK_MSG(!history.CanRedo(), "and the redo stack must then be empty");
}

static void testNewEditDiscardsTheRedoBranch() {
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    history.Reset(registry);

    addEntity(registry, "Sphere");
    CHECK(history.CommitIfChanged(registry));
    CHECK(history.Undo(registry));
    CHECK(history.CanRedo());

    // Diverging from the undone state must drop the branch that was undone.
    addEntity(registry, "Cone");
    CHECK(history.CommitIfChanged(registry));
    CHECK_MSG(!history.CanRedo(), "a new edit must invalidate the redo branch");
}

static void testSeveralStepsUnwindInOrder() {
    entt::registry registry;
    EditHistory history;
    history.Reset(registry);

    for (int i = 0; i < 5; ++i) {
        addEntity(registry, "Entity" + std::to_string(i));
        CHECK(history.CommitIfChanged(registry));
    }
    CHECK_EQ(entityCount(registry), size_t{5});
    CHECK_EQ(history.UndoDepth(), size_t{5});

    for (int i = 4; i >= 0; --i) {
        CHECK(history.Undo(registry));
        CHECK_EQ(entityCount(registry), static_cast<size_t>(i));
    }
    CHECK_MSG(!history.CanUndo(), "the history must bottom out at the reset state");
}

static void testHistoryDepthIsBounded() {
    entt::registry registry;
    EditHistory history;
    history.Reset(registry);

    // Deliberately past the cap: an editor left open all day must not grow a
    // snapshot per edit forever.
    for (size_t i = 0; i < EditHistory::kMaxDepth + 20; ++i) {
        addEntity(registry, "Entity" + std::to_string(i));
        history.CommitIfChanged(registry);
    }
    CHECK_EQ(history.UndoDepth(), EditHistory::kMaxDepth);
}

static void testResetClearsBothStacks() {
    entt::registry registry;
    EditHistory history;
    history.Reset(registry);

    addEntity(registry, "Cube");
    CHECK(history.CommitIfChanged(registry));
    CHECK(history.Undo(registry));

    history.Reset(registry);
    CHECK_MSG(!history.CanUndo(), "Reset must drop the undo stack");
    CHECK_MSG(!history.CanRedo(), "and the redo stack with it");
}

static void testLabelsDescribeTheStep() {
    entt::registry registry;
    const auto entity = addEntity(registry, "Cube");
    EditHistory history;
    history.Reset(registry);

    registry.get<TransformComponent>(entity).position.x = 5.0f;
    CHECK(history.CommitIfChanged(registry));
    CHECK_MSG(history.UndoLabel() == "Undo Edit", history.UndoLabel());

    addEntity(registry, "Sphere");
    CHECK(history.CommitIfChanged(registry));
    CHECK_MSG(history.UndoLabel() == "Undo Create Entity", history.UndoLabel());

    CHECK(history.Undo(registry));
    CHECK_MSG(history.RedoLabel() == "Redo Create Entity", history.RedoLabel());
}

static void testCommitBeforeResetSelfInitialises() {
    // The editor never calls Reset explicitly - the first commit of the session
    // has to establish the baseline rather than record a step against an empty
    // scene the user never had.
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    CHECK_MSG(!history.CommitIfChanged(registry), "the first commit is the baseline, not a step");
    CHECK_MSG(!history.CanUndo(), "so there is nothing to undo yet");

    addEntity(registry, "Sphere");
    CHECK_MSG(history.CommitIfChanged(registry), "and the next change is a real step");
}

static void testCommitAfterUndoRecordsNothing() {
    // The editor runs CommitIfChanged at the end of every frame, including the
    // frame Ctrl+Z fired in. Undo used to assume a restore reproduced the exact
    // text it was restored from; it did not, because the serializer rebuilt the
    // scene in the opposite order, so that commit saw a phantom change, pushed a
    // step and cleared the redo stack. Redo could therefore never do anything
    // and undo was stuck at a single step.
    entt::registry registry;
    addEntity(registry, "Cube");

    EditHistory history;
    history.Reset(registry);

    addEntity(registry, "Sphere");
    CHECK(history.CommitIfChanged(registry));
    CHECK(history.Undo(registry));
    CHECK_MSG(history.CanRedo(), "the undo must leave a redo available");

    CHECK_MSG(!history.CommitIfChanged(registry),
              "the frame's commit right after an undo must record nothing");
    CHECK_MSG(history.CanRedo(), "and must not wipe the redo stack");
    CHECK_EQ(entityCount(registry), size_t{1});

    CHECK_MSG(history.Redo(registry), "redo must still work");
    CHECK_EQ(entityCount(registry), size_t{2});
}

static void testUndoWalksBackSeveralStepsThroughFrameCommits() {
    // The same failure, seen from the user's side: undo, undo, undo with a
    // frame commit in between each, as the editor actually runs it.
    entt::registry registry;
    EditHistory history;
    history.Reset(registry);

    for (int i = 0; i < 4; ++i) {
        addEntity(registry, "Entity" + std::to_string(i));
        CHECK(history.CommitIfChanged(registry));
    }
    CHECK_EQ(entityCount(registry), size_t{4});

    for (int expected = 3; expected >= 0; --expected) {
        CHECK(history.Undo(registry));
        history.CommitIfChanged(registry); // the end-of-frame commit
        CHECK_EQ(entityCount(registry), static_cast<size_t>(expected));
    }
}

static void testSnapshotTextIsStableAcrossARoundTrip() {
    // The root cause, pinned directly: capture(restore(T)) must equal T, or
    // every consumer of "has the scene changed?" gets a false positive.
    entt::registry registry;
    const auto a = addEntity(registry, "A", glm::vec3(1.0f, 0.0f, 0.0f));
    addEntity(registry, "B", glm::vec3(2.0f, 0.0f, 0.0f));
    const auto c = addEntity(registry, "C", glm::vec3(3.0f, 0.0f, 0.0f));
    registry.emplace<HierarchyComponent>(c, a);

    // Deliberately awkward floats, and every component block that carries them:
    // a value that does not survive the text round trip makes the whole snapshot
    // unstable, and every consumer of "has the scene changed?" then sees a
    // change that is not there.
    auto& animator = registry.emplace<AnimatorComponent>(c);
    animator.clipName = "Bend";
    animator.time = 0.8333333f;
    animator.speed = 1.4285714f;
    auto& emitter = registry.emplace<ParticleEmitterComponent>(a);
    emitter.emitRate = 33.333333f;
    emitter.particleSize = 0.1234567f;

    const std::string first = SceneSerializer::SerializeToString(registry);
    CHECK(SceneSerializer::DeserializeFromString(registry, first).ok);
    const std::string second = SceneSerializer::SerializeToString(registry);

    CHECK_MSG(first == second, "a scene written, loaded and written again must be identical");

    CHECK(SceneSerializer::DeserializeFromString(registry, second).ok);
    CHECK_MSG(SceneSerializer::SerializeToString(registry) == first,
              "and stable over any number of round trips, not merely period-2");
}

static void testEmitterSettingsSurviveAnUndo() {
    // The emitter was written as a bare `true`, so every authored setting was
    // reset by the next undo - and, because the text never changed when those
    // fields were edited, the edit was not undoable in the first place.
    entt::registry registry;
    const auto emitterEntity = addEntity(registry, "Emitter");
    auto& emitter = registry.emplace<ParticleEmitterComponent>(emitterEntity);
    emitter.emitRate = 60.0f;
    emitter.particleLifetime = 7.5f;
    emitter.maxParticles = 512;
    emitter.startColor = glm::vec4(0.1f, 0.2f, 0.9f, 1.0f);
    emitter.particleSize = 0.25f;

    EditHistory history;
    history.Reset(registry);

    // An unrelated edit elsewhere in the scene.
    const auto other = addEntity(registry, "Cube");
    registry.get<TransformComponent>(other).position.x = 5.0f;
    CHECK(history.CommitIfChanged(registry));
    CHECK(history.Undo(registry));

    bool found = false;
    for (auto e : registry.view<ParticleEmitterComponent>()) {
        found = true;
        const auto& restored = registry.get<ParticleEmitterComponent>(e);
        CHECK_NEAR(restored.emitRate, 60.0f);
        CHECK_NEAR(restored.particleLifetime, 7.5f);
        CHECK_EQ(restored.maxParticles, uint32_t{512});
        CHECK_NEAR(restored.startColor.z, 0.9f);
        CHECK_NEAR(restored.particleSize, 0.25f);
    }
    CHECK_MSG(found, "the emitter must still exist");
}

static void testEmitterEditIsItselfUndoable() {
    entt::registry registry;
    const auto emitterEntity = addEntity(registry, "Emitter");
    registry.emplace<ParticleEmitterComponent>(emitterEntity);

    EditHistory history;
    history.Reset(registry);

    registry.get<ParticleEmitterComponent>(emitterEntity).emitRate = 99.0f;
    CHECK_MSG(history.CommitIfChanged(registry), "changing an emitter field must be a recordable edit");
    CHECK(history.Undo(registry));

    for (auto e : registry.view<ParticleEmitterComponent>()) {
        CHECK_NEAR(registry.get<ParticleEmitterComponent>(e).emitRate, 10.0f);
    }
}

static void testColliderAndCameraFlagsSurviveAnUndo() {
    // SphereColliderComponent and AudioListenerComponent were not serialised at
    // all, and BoxCollider::isTrigger and Camera::isPrimary were dropped - so an
    // undo silently deleted colliders and moved the primary camera.
    entt::registry registry;

    const auto ball = addEntity(registry, "Ball");
    auto& sphere = registry.emplace<SphereColliderComponent>(ball);
    sphere.radius = 1.75f;
    sphere.isTrigger = true;

    const auto crate = addEntity(registry, "Crate");
    registry.emplace<BoxColliderComponent>(crate).isTrigger = true;

    const auto camA = addEntity(registry, "Camera A");
    registry.emplace<CameraComponent>(camA).isPrimary = false;
    const auto camB = addEntity(registry, "Camera B");
    registry.emplace<CameraComponent>(camB).isPrimary = true;

    const auto ears = addEntity(registry, "Listener");
    registry.emplace<AudioListenerComponent>(ears);

    EditHistory history;
    history.Reset(registry);

    addEntity(registry, "Something Else");
    CHECK(history.CommitIfChanged(registry));
    CHECK(history.Undo(registry));

    bool sawSphere = false, sawTriggerBox = false, sawListener = false;
    for (auto e : registry.view<TagComponent>()) {
        const std::string& tag = registry.get<TagComponent>(e).tag;
        if (tag == "Ball") {
            const auto* restored = registry.try_get<SphereColliderComponent>(e);
            CHECK_MSG(restored != nullptr, "the sphere collider must survive an undo");
            if (restored) {
                sawSphere = true;
                CHECK_NEAR(restored->radius, 1.75f);
                CHECK_MSG(restored->isTrigger, "and keep its trigger flag");
            }
        }
        if (tag == "Crate") {
            const auto* restored = registry.try_get<BoxColliderComponent>(e);
            if (restored) sawTriggerBox = restored->isTrigger;
        }
        if (tag == "Listener") sawListener = registry.all_of<AudioListenerComponent>(e);
        if (tag == "Camera B") {
            CHECK_MSG(registry.get<CameraComponent>(e).isPrimary,
                      "the primary camera must stay the primary camera");
        }
        if (tag == "Camera A") {
            CHECK_MSG(!registry.get<CameraComponent>(e).isPrimary,
                      "and a non-primary camera must not become one");
        }
    }
    CHECK_MSG(sawSphere, "the ball entity must be found");
    CHECK_MSG(sawTriggerBox, "the box collider's trigger flag must survive");
    CHECK_MSG(sawListener, "the audio listener must survive");
}

static void runTests() {
    testCommitAfterUndoRecordsNothing();
    testUndoWalksBackSeveralStepsThroughFrameCommits();
    testSnapshotTextIsStableAcrossARoundTrip();
    testEmitterSettingsSurviveAnUndo();
    testEmitterEditIsItselfUndoable();
    testColliderAndCameraFlagsSurviveAnUndo();
    testFreshHistoryHasNothingToUndo();
    testCommitWithoutChangeRecordsNothing();
    testUndoRestoresADeletedEntity();
    testUndoRemovesACreatedEntity();
    testUndoRestoresAFieldEdit();
    testRedoReappliesTheChange();
    testNewEditDiscardsTheRedoBranch();
    testSeveralStepsUnwindInOrder();
    testHistoryDepthIsBounded();
    testResetClearsBothStacks();
    testLabelsDescribeTheStep();
    testCommitBeforeResetSelfInitialises();
}

TEST_MAIN("test_undo", 75)
