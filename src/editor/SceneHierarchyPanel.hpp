#pragma once

#include <entt/entt.hpp>
#include "imgui.h"

namespace Supersonic {

class SceneHierarchyPanel {
public:
    SceneHierarchyPanel() = default;
    explicit SceneHierarchyPanel(entt::registry& registry);

    void SetRegistry(entt::registry& registry) { m_registry = &registry; }
    void OnImGuiRender();

    entt::entity GetSelectedEntity() const { return m_selectedEntity; }

    // Set when the user picks "Save as Prefab" on a row. The panel does not
    // write the file: choosing a path and reporting the result belongs to the
    // editor, which already owns the status line.
    entt::entity ConsumePrefabSaveRequest();
    void SetSelectedEntity(entt::entity entity) { m_selectedEntity = entity; }

private:
    // Draws an entity and, recursively, its children. Returns true if the
    // entity was deleted, so the caller can stop touching it.
    bool drawEntityNode(entt::entity entity);

    void drawCreateMenu();

    entt::registry* m_registry{nullptr};
    entt::entity m_selectedEntity{entt::null};

    // Deferred so the tree is not mutated midway through being walked.
    entt::entity m_pendingDelete{entt::null};
    entt::entity m_pendingPrefabSave{entt::null};
    entt::entity m_pendingReparentChild{entt::null};
    entt::entity m_pendingReparentParent{entt::null};
    bool m_hasPendingReparent{false};
};

} // namespace Supersonic
