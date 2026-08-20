#pragma once

#include <entt/entt.hpp>
#include "imgui.h"
#include "ImGuizmo.h"
#include "core/Components.hpp"

namespace Supersonic {

class MaterialLibrary;
class BloomPass;

class InspectorPanel {
public:
    // Non-owning; null simply means the material section edits the entity's own
    // values, which is what it did before assets existed.
    void SetMaterialLibrary(MaterialLibrary* library) { m_materialLibrary = library; }

    // Non-owning; null simply hides the bloom controls.
    void SetBloom(BloomPass* bloom) { m_bloom = bloom; }

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
    MaterialLibrary* m_materialLibrary{nullptr};

    // The post chain, so the world settings can include how it looks. Non-owning
    // and null until the editor hands it over.
    BloomPass* m_bloom{nullptr};

    void drawComponents(entt::registry& registry, entt::entity entity);

    // Scene-level physics, drawn where the inspector would otherwise be
    // empty: it belongs to no entity, so there is nothing to select to
    // reach it.
    void drawWorldSettings(entt::registry& registry);

    // Decomposes using TransformComponent::getModelMatrix's own Euler order,
    // rather than ImGuizmo's, which is a different convention.
    static void decomposeToTransform(const glm::mat4& model, TransformComponent& transform);

    ImGuizmo::OPERATION m_gizmoOperation{ImGuizmo::TRANSLATE};
};

} // namespace Supersonic
