#pragma once

#include <entt/entt.hpp>
#include "imgui.h"

namespace Engine {

class SceneHierarchyPanel {
public:
    SceneHierarchyPanel() = default;
    explicit SceneHierarchyPanel(entt::registry& registry);

    void SetRegistry(entt::registry& registry) { m_registry = &registry; }
    void OnImGuiRender();

    entt::entity GetSelectedEntity() const { return m_selectedEntity; }
    void SetSelectedEntity(entt::entity entity) { m_selectedEntity = entity; }

private:
    void drawEntityNode(entt::entity entity);

    entt::registry* m_registry{nullptr};
    entt::entity m_selectedEntity{entt::null};
};

} // namespace Engine
