#include "editor/EditHistory.hpp"

#include "core/SceneSerializer.hpp"

namespace Supersonic {

EditHistory::Snapshot EditHistory::capture(entt::registry& registry) {
    Snapshot snapshot;
    snapshot.text = SceneSerializer::SerializeToString(registry);
    // Counted by walking the entity view rather than asking the registry for a
    // size: registry.size() includes destroyed handles still in the free list,
    // and storage<entt::entity>().in_use() is deprecated in this EnTT.
    size_t live = 0;
    for ([[maybe_unused]] auto entity : registry.view<entt::entity>()) ++live;
    snapshot.entityCount = live;
    return snapshot;
}

bool EditHistory::restore(entt::registry& registry, const Snapshot& snapshot) {
    return SceneSerializer::DeserializeFromString(registry, snapshot.text).ok;
}

void EditHistory::Reset(entt::registry& registry) {
    m_current = capture(registry);
    m_undo.clear();
    m_redo.clear();
    m_initialised = true;
}

bool EditHistory::CommitIfChanged(entt::registry& registry) {
    if (!m_initialised) {
        Reset(registry);
        return false;
    }

    Snapshot latest = capture(registry);
    if (latest.text == m_current.text) return false;

    m_undo.push_back(std::move(m_current));
    if (m_undo.size() > kMaxDepth) m_undo.pop_front();

    m_current = std::move(latest);

    // A new edit invalidates the redo branch, which is what every editor does
    // and what stops redo from replaying a scene that no longer exists.
    m_redo.clear();
    return true;
}

bool EditHistory::Undo(entt::registry& registry) {
    if (m_undo.empty()) return false;

    Snapshot target = std::move(m_undo.back());
    m_undo.pop_back();

    if (!restore(registry, target)) return false;

    m_redo.push_back(std::move(m_current));
    if (m_redo.size() > kMaxDepth) m_redo.pop_front();

    m_current = std::move(target);
    return true;
}

bool EditHistory::Redo(entt::registry& registry) {
    if (m_redo.empty()) return false;

    Snapshot target = std::move(m_redo.back());
    m_redo.pop_back();

    if (!restore(registry, target)) return false;

    m_undo.push_back(std::move(m_current));
    if (m_undo.size() > kMaxDepth) m_undo.pop_front();

    m_current = std::move(target);
    return true;
}

std::string EditHistory::describe(const Snapshot& from, const Snapshot& to) {
    if (to.entityCount > from.entityCount) return "Undo Create Entity";
    if (to.entityCount < from.entityCount) return "Undo Delete Entity";
    return "Undo Edit";
}

std::string EditHistory::UndoLabel() const {
    if (m_undo.empty()) return "Undo";
    return describe(m_undo.back(), m_current);
}

std::string EditHistory::RedoLabel() const {
    if (m_redo.empty()) return "Redo";
    std::string label = describe(m_current, m_redo.back());
    label.replace(0, 4, "Redo"); // "Undo X" -> "Redo X"
    return label;
}

} // namespace Supersonic
