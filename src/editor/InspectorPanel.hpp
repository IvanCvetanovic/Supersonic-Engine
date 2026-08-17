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

    // Decomposes using TransformComponent::getModelMatrix's own Euler order,
    // rather than ImGuizmo's, which is a different convention.
    static void decomposeToTransform(const glm::mat4& model, TransformComponent& transform);

    ImGuizmo::OPERATION m_gizmoOperation{ImGuizmo::TRANSLATE};
};

} // namespace Engine
