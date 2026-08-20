#pragma once

#include <entt/entt.hpp>
#include "imgui.h"

#include <unordered_map>
#include <vector>

namespace Supersonic {

class SceneHierarchyPanel {
public:
    SceneHierarchyPanel() = default;
    explicit SceneHierarchyPanel(entt::registry& registry);

    void SetRegistry(entt::registry& registry) { m_registry = &registry; }
    void OnImGuiRender();

    entt::entity GetSelectedEntity() const { return m_selectedEntity; }

    // Who is whose child, and who has no parent, in one pass.
    //
    // Public and static because it is the only part of drawing a tree that can
    // be WRONG rather than merely ugly, and the rest of the panel needs an
    // ImGui context to run at all. An entity misfiled here disappears from the
    // hierarchy - it is not drawn as a root and its parent never draws it -
    // which is indistinguishable from having been deleted.
    //
    // Both outputs are cleared and refilled, so a caller can keep them between
    // frames and stop allocating after the first.
    static void BuildIndex(const entt::registry& registry,
                           std::unordered_map<entt::entity, std::vector<entt::entity>>& outChildren,
                           std::vector<entt::entity>& outRoots);

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

    // Who is whose child, rebuilt once per frame.
    //
    // Each row used to answer that by scanning the whole HierarchyComponent
    // pool looking for entities pointing at it - so drawing the tree was
    // quadratic in the number of parented entities, and a scene of a thousand
    // of them did a million comparisons to lay out one panel. Built in the same
    // pass that already had to visit every entity to find the roots.
    //
    // A member rather than a local so the buckets and the child vectors keep
    // their capacity between frames.
    std::unordered_map<entt::entity, std::vector<entt::entity>> m_children;
    std::vector<entt::entity> m_roots;

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
