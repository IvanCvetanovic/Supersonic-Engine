#pragma once

#include <entt/entt.hpp>
#include "imgui.h"
#include "ImGuizmo.h"
#include "core/Components.hpp"

namespace Engine {

class InspectorPanel {
public:
    InspectorPanel() = default;

    void OnImGuiRender(entt::registry& registry, entt::entity selectedEntity);
    void RenderGizmo(
        entt::registry& registry,
        entt::entity selectedEntity,
        const CameraComponent& camera,
        const ImVec2& viewportPos,
        const ImVec2& viewportSize
    );

    ImGuizmo::OPERATION GetGizmoOperation() const { return m_gizmoOperation; }
    void SetGizmoOperation(ImGuizmo::OPERATION op) { m_gizmoOperation = op; }

private:
    void drawComponents(entt::registry& registry, entt::entity entity);

    ImGuizmo::OPERATION m_gizmoOperation{ImGuizmo::TRANSLATE};
};

} // namespace Engine
