#pragma once

#include <entt/entt.hpp>
#include "imgui.h"
#include "ImGuizmo.h"
#include "core/Components.hpp"

namespace Supersonic {

class AnimationLibrary;
class MaterialLibrary;

class InspectorPanel {
public:
    // Non-owning; null simply means the material section edits the entity's own
    // values, which is what it did before assets existed.
    void SetMaterialLibrary(MaterialLibrary* library) { m_materialLibrary = library; }

    // So the animator can offer the clips a rig actually has, instead of asking
    // someone to type a name and spell it the same way the exporter did.
    void SetAnimationLibrary(AnimationLibrary* library) { m_animationLibrary = library; }

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

    // The tilemap brush. Editor state rather than a component's, because
    // which tile the next click paints is a fact about the person editing
    // and not about the map - it is not saved with the scene, it survives
    // selecting another map, and two maps are painted with one brush.
    struct TileBrush {
        // While set, a left click in the viewport paints the selected map
        // instead of picking. Off by default so a scene with a map in it
        // still selects like any other scene.
        bool painting{false};

        int atlasIndex{0};
        bool flipH{false};
        bool flipV{false};
        bool erase{false};

        // The cell value a stroke writes.
        int32_t Cell() const {
            if (erase) return TilemapComponent::kEmpty;
            return TilemapComponent::MakeCell(static_cast<uint32_t>(atlasIndex < 0 ? 0 : atlasIndex),
                                              flipH, flipV);
        }
    };

    const TileBrush& GetTileBrush() const { return m_tileBrush; }

private:
    TileBrush m_tileBrush;
    MaterialLibrary* m_materialLibrary{nullptr};
    AnimationLibrary* m_animationLibrary{nullptr};

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
