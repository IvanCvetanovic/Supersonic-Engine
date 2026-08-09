#include "editor/InspectorPanel.hpp"
#include "editor/Theme.hpp"

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
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);
        ImGui::TextDisabled("  Select an entity from the Scene Hierarchy to inspect.");
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

        ImGui::TextDisabled("ENTITY TAG");
        if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
            tag.tag = std::string(buffer);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. TransformComponent
    if (registry.all_of<TransformComponent>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& transform = registry.get<TransformComponent>(entity);

            Theme::DrawVec3Control("Position", transform.position, 0.0f);

            glm::vec3 rotDegrees = glm::degrees(transform.rotation);
            Theme::DrawVec3Control("Rotation", rotDegrees, 0.0f);
            transform.rotation = glm::radians(rotDegrees);

            Theme::DrawVec3Control("Scale", transform.scale, 1.0f);
        }
    }

    ImGui::Spacing();

    // 3. RigidBodyComponent
    if (registry.all_of<RigidBodyComponent>(entity)) {
        if (ImGui::CollapsingHeader("RigidBody Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& rb = registry.get<RigidBodyComponent>(entity);

            ImGui::Checkbox("Use Gravity", &rb.useGravity);
            ImGui::Checkbox("Is Kinematic", &rb.isKinematic);
            ImGui::DragFloat("Mass", &rb.mass, 0.1f, 0.01f, 1000.0f);
            Theme::DrawVec3Control("Velocity", rb.velocity, 0.0f);
        }
    }

    ImGui::Spacing();

    // 4. BoxColliderComponent
    if (registry.all_of<BoxColliderComponent>(entity)) {
        if (ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& box = registry.get<BoxColliderComponent>(entity);

            Theme::DrawVec3Control("Size", box.size, 1.0f);
            ImGui::Checkbox("Is Trigger", &box.isTrigger);
        }
    }

    ImGui::Spacing();

    // 5. AudioSourceComponent
    if (registry.all_of<AudioSourceComponent>(entity)) {
        if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& audio = registry.get<AudioSourceComponent>(entity);

            ImGui::Checkbox("Play On Start", &audio.isPlaying);
            ImGui::Checkbox("Looping", &audio.loop);
            ImGui::SliderFloat("Volume", &audio.volume, 0.0f, 1.0f);
            ImGui::SliderFloat("Pitch", &audio.pitch, 0.5f, 2.0f);
        }
    }

    ImGui::Spacing();

    // 6. ScriptComponent
    if (registry.all_of<ScriptComponent>(entity)) {
        if (ImGui::CollapsingHeader("Script Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& script = registry.get<ScriptComponent>(entity);

            ImGui::Checkbox("Enabled", &script.isEnabled);
            ImGui::Text("Script Name: %s", script.scriptName.c_str());
        }
    }

    ImGui::Spacing();

    // 7. ParticleEmitterComponent
    if (registry.all_of<ParticleEmitterComponent>(entity)) {
        if (ImGui::CollapsingHeader("Particle Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& emitter = registry.get<ParticleEmitterComponent>(entity);

            ImGui::DragFloat("Emit Rate", &emitter.emitRate, 1.0f, 1.0f, 100.0f);
            ImGui::DragFloat("Lifetime", &emitter.particleLifetime, 0.1f, 0.1f, 10.0f);
            ImGui::ColorEdit4("Start Color", glm::value_ptr(emitter.startColor));
            ImGui::ColorEdit4("End Color", glm::value_ptr(emitter.endColor));
        }
    }

    ImGui::Spacing();

    // 8. LightComponent
    if (registry.all_of<LightComponent>(entity)) {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = registry.get<LightComponent>(entity);

            Theme::DrawVec3Control("Direction", light.direction, 0.0f);
            ImGui::ColorEdit3("Light Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 10.0f);
            ImGui::ColorEdit3("Ambient Color", glm::value_ptr(light.ambient));
        }
    }

    ImGui::Spacing();

    // 9. CameraComponent
    if (registry.all_of<CameraComponent>(entity)) {
        if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& camera = registry.get<CameraComponent>(entity);

            ImGui::DragFloat("Field of View", &camera.fov, 0.5f, 10.0f, 120.0f);
            ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.001f, 10.0f);
            ImGui::DragFloat("Far Plane", &camera.farPlane, 1.0f, 10.0f, 1000.0f);
            Theme::DrawVec3Control("Cam Position", camera.position, 0.0f);
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
