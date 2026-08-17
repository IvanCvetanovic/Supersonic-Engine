// Regression tests for editor undo/redo.
//
// The failure modes here are quiet ones: a redo stack that survives a new edit
// replays a scene that no longer exists, a commit that fires mid-drag turns one
// gizmo movement into a hundred steps, and an unbounded stack grows forever in
// a long session. None of those show up as a crash.

#include "TestHarness.hpp"
#include "editor/EditHistory.hpp"
#include "core/Components.hpp"

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

static void runTests() {
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

TEST_MAIN("test_undo")
