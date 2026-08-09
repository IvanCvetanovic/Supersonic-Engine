#include "editor/InspectorPanel.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <iostream>

namespace Engine {

void InspectorPanel::OnImGuiRender(entt::registry& registry, entt::entity selectedEntity) {
    ImGui::Begin("Inspector");

    if (selectedEntity != entt::null && registry.valid(selectedEntity)) {
        drawComponents(registry, selectedEntity);
    } else {
        ImGui::TextDisabled("No Entity Selected");
    }

    ImGui::End();
}

void InspectorPanel::drawComponents(entt::registry& registry, entt::entity entity) {
    // 1. TagComponent
    if (registry.all_of<TagComponent>(entity)) {
        auto& tag = registry.get<TagComponent>(entity);
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, tag.tag.c_str(), sizeof(buffer) - 1);

        if (ImGui::InputText("Tag", buffer, sizeof(buffer))) {
            tag.tag = std::string(buffer);
        }
    }

    ImGui::Separator();

    // 2. TransformComponent
    if (registry.all_of<TransformComponent>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& transform = registry.get<TransformComponent>(entity);

            ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.05f);

            glm::vec3 rotDegrees = glm::degrees(transform.rotation);
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotDegrees), 0.5f)) {
                transform.rotation = glm::radians(rotDegrees);
            }

            ImGui::DragFloat3("Scale", glm::value_ptr(transform.scale), 0.05f, 0.001f, 100.0f);
        }
    }

    // 3. LightComponent
    if (registry.all_of<LightComponent>(entity)) {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = registry.get<LightComponent>(entity);

            ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.05f);
            ImGui::ColorEdit3("Light Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 10.0f);
            ImGui::ColorEdit3("Ambient Color", glm::value_ptr(light.ambient));
        }
    }

    // 4. CameraComponent
    if (registry.all_of<CameraComponent>(entity)) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& camera = registry.get<CameraComponent>(entity);

            ImGui::DragFloat("Field of View", &camera.fov, 0.5f, 10.0f, 120.0f);
            ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.001f, 10.0f);
            ImGui::DragFloat("Far Plane", &camera.farPlane, 1.0f, 10.0f, 1000.0f);
            ImGui::DragFloat3("Camera Position", glm::value_ptr(camera.position), 0.05f);
        }
    }

    // 5. RenderableComponent
    if (registry.all_of<RenderableComponent>(entity)) {
        if (ImGui::CollapsingHeader("Renderable", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& renderable = registry.get<RenderableComponent>(entity);

            ImGui::Checkbox("Visible", &renderable.isVisible);
            ImGui::Text("Mesh ID: %u", renderable.meshID);
            ImGui::Text("Material ID: %u", renderable.materialID);
        }
    }
}

void InspectorPanel::RenderGizmo(
    entt::registry& registry,
    entt::entity selectedEntity,
    const CameraComponent& camera,
    const ImVec2& viewportPos,
    const ImVec2& viewportSize) {

    if (selectedEntity == entt::null || !registry.valid(selectedEntity)) return;
    if (!registry.all_of<TransformComponent>(selectedEntity)) return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix();
    proj[1][1] *= -1.0f; // Unflip Y projection matrix for ImGuizmo screen-space picking

    auto& transform = registry.get<TransformComponent>(selectedEntity);
    glm::mat4 model = transform.getModelMatrix();

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        m_gizmoOperation,
        ImGuizmo::LOCAL,
        glm::value_ptr(model)
    );

    if (ImGuizmo::IsUsing()) {
        float matrixTranslation[3], matrixRotation[3], matrixScale[3];
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(model),
            matrixTranslation,
            matrixRotation,
            matrixScale
        );

        transform.position = glm::vec3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]);
        transform.rotation = glm::vec3(glm::radians(matrixRotation[0]), glm::radians(matrixRotation[1]), glm::radians(matrixRotation[2]));
        transform.scale = glm::vec3(matrixScale[0], matrixScale[1], matrixScale[2]);
    }
}

} // namespace Engine
