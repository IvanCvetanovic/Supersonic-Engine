#pragma once

#include <deque>
#include <string>

#include <entt/entt.hpp>

namespace Supersonic {

// Undo / redo for the editor.
//
// Nothing was undoable before this: a mis-drag of the gizmo, a deleted entity
// or a mistyped field was permanent unless the scene happened to have been
// saved. That makes an editor unusable for real work long before any missing
// renderer feature does.
//
// Snapshot-based, over the scene serializer that Play/Stop already uses, rather
// than one inverse-operation class per action. Two reasons: a snapshot cannot
// drift out of sync with the operation it is meant to invert, and every future
// editor action becomes undoable without writing anything. The cost is memory
// per step, which for a text scene description is a few kilobytes.
class EditHistory {
public:
    // Records the starting state. Call once the scene is populated, and again
    // after loading a scene, since undoing past a load makes no sense.
    void Reset(entt::registry& registry);

    // Serialises the scene and, if it differs from the last recorded state,
    // pushes the previous state onto the undo stack. Returns true if a step was
    // recorded. Call after a mutation, when the edit has settled - not
    // mid-drag, or every frame of a gizmo drag becomes its own step.
    bool CommitIfChanged(entt::registry& registry);

    bool Undo(entt::registry& registry);
    bool Redo(entt::registry& registry);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }

    // Human-readable description of what the next undo/redo would do, for the
    // Edit menu. Derived from how the entity count changed, which distinguishes
    // the three cases that matter without needing every call site to pass a
    // label it would eventually get wrong.
    std::string UndoLabel() const;
    std::string RedoLabel() const;

    size_t UndoDepth() const { return m_undo.size(); }

    // Bounded so a long session cannot grow without limit.
    static constexpr size_t kMaxDepth = 64;

private:
    struct Snapshot {
        std::string text;
        size_t entityCount{0};
    };

    static Snapshot capture(entt::registry& registry);
    static bool restore(entt::registry& registry, const Snapshot& snapshot);
    static std::string describe(const Snapshot& from, const Snapshot& to);

    Snapshot m_current;
    std::deque<Snapshot> m_undo;
    std::deque<Snapshot> m_redo;
    bool m_initialised{false};
};

} // namespace Supersonic
