#include "editor/InspectorPanel.hpp"
#include "editor/Theme.hpp"

// GLM_ENABLE_EXPERIMENTAL is set on the target in CMakeLists.txt.
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Engine {

void InspectorPanel::OnImGuiRender(entt::registry& registry, entt::entity selectedEntity) {
    ImGui::Begin("Inspector");

    if (selectedEntity != entt::null && registry.valid(selectedEntity)) {
        // Scope every widget ID to the entity. Without this, the tag field has
        // the same ImGui ID for all entities, and switching selection while an
        // edit is uncommitted makes ImGui reapply the old text to the newly
        // selected entity - silently renaming it.
        ImGui::PushID(static_cast<int>(entt::to_integral(selectedEntity)));
        drawComponents(registry, selectedEntity);
        ImGui::PopID();
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
        char buffer[256] = {};
        const size_t copyLength = std::min(tag.tag.size(), sizeof(buffer) - 1);
        std::memcpy(buffer, tag.tag.data(), copyLength);

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

    // 2b. MeshComponent - now actually drives which geometry is drawn.
    if (registry.all_of<MeshComponent>(entity)) {
        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& mesh = registry.get<MeshComponent>(entity);

            static const char* kPrimitives[] = { "Cube", "Sphere", "Plane", "Terrain" };
            int current = 0;
            for (int i = 0; i < IM_ARRAYSIZE(kPrimitives); ++i) {
                if (mesh.primitiveType == kPrimitives[i]) { current = i; break; }
            }
            if (ImGui::Combo("Primitive", &current, kPrimitives, IM_ARRAYSIZE(kPrimitives))) {
                mesh.primitiveType = kPrimitives[current];
                mesh.filePath.clear();
            }

            if (!mesh.filePath.empty()) {
                ImGui::TextDisabled("Source: %s", mesh.filePath.c_str());
            }
        }
    }

    ImGui::Spacing();

    // 2c. MaterialComponent - reaches the GPU via push constants.
    if (registry.all_of<MaterialComponent>(entity)) {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& material = registry.get<MaterialComponent>(entity);

            ImGui::ColorEdit4("Albedo", glm::value_ptr(material.albedoColor));
            ImGui::SliderFloat("Roughness", &material.roughness, 0.02f, 1.0f);
            ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("Ambient Occlusion", &material.ao, 0.0f, 1.0f);
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

            ImGui::Checkbox("Playing", &audio.isPlaying);
            ImGui::Checkbox("Looping", &audio.loop);
            ImGui::SliderFloat("Volume", &audio.volume, 0.0f, 1.0f);
            ImGui::SliderFloat("Pitch", &audio.pitch, 0.5f, 2.0f);
            ImGui::DragFloat("Reference Distance", &audio.referenceDistance, 0.1f, 0.1f, 100.0f);
            ImGui::DragFloat("Max Distance", &audio.maxDistance, 0.5f, 1.0f, 500.0f);
            ImGui::TextDisabled("Clip: %s", audio.soundFile.c_str());
            if (audio.failedToLoad) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "Clip failed to load.");
                if (ImGui::Button("Retry Load")) { audio.failedToLoad = false; }
            }
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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Dynamic "Add Component" Dropdown Button
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 160.0f) * 0.5f);
    if (ImGui::Button("+ Add Component", ImVec2(160.0f, 28.0f))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!registry.all_of<RigidBodyComponent>(entity) && ImGui::MenuItem("RigidBody Physics")) {
            registry.emplace<RigidBodyComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<BoxColliderComponent>(entity) && ImGui::MenuItem("Box Collider")) {
            registry.emplace<BoxColliderComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<AudioSourceComponent>(entity) && ImGui::MenuItem("Audio Source")) {
            registry.emplace<AudioSourceComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<ScriptComponent>(entity) && ImGui::MenuItem("Script Component")) {
            registry.emplace<ScriptComponent>(entity, "RotatorScript");
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<ParticleEmitterComponent>(entity) && ImGui::MenuItem("Particle Emitter")) {
            registry.emplace<ParticleEmitterComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<LightComponent>(entity) && ImGui::MenuItem("Directional Light")) {
            registry.emplace<LightComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<CameraComponent>(entity) && ImGui::MenuItem("Camera")) {
            registry.emplace<CameraComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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
    const glm::mat4 before = model;

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        m_gizmoOperation,
        ImGuizmo::LOCAL,
        glm::value_ptr(model)
    );

    // Only write back when the gizmo actually changed the matrix. IsUsing() is
    // already true on the click frame with a zero drag delta.
    if (ImGuizmo::IsUsing() && model != before) {
        decomposeToTransform(model, transform);
    }
}

void InspectorPanel::decomposeToTransform(const glm::mat4& model, TransformComponent& transform) {
    // Decomposed with the SAME convention getModelMatrix() composes with
    // (T * Rx * Ry * Rz * S). ImGuizmo::DecomposeMatrixToComponents returns
    // angles in its own Rz*Ry*Rx order, so feeding those straight back
    // reinterpreted them and made objects snap the instant a handle was pressed.
    transform.position = glm::vec3(model[3]);

    glm::vec3 scale(glm::length(glm::vec3(model[0])),
                    glm::length(glm::vec3(model[1])),
                    glm::length(glm::vec3(model[2])));

    // A mirrored matrix has a negative determinant; fold that into X.
    if (glm::determinant(glm::mat3(model)) < 0.0f) {
        scale.x = -scale.x;
    }

    if (scale.x != 0.0f && scale.y != 0.0f && scale.z != 0.0f) {
        transform.scale = scale;

        glm::mat3 rot(glm::vec3(model[0]) / scale.x,
                      glm::vec3(model[1]) / scale.y,
                      glm::vec3(model[2]) / scale.z);

        // For R = Rx(a)Ry(b)Rz(c), in glm's column-major storage rot[col][row]:
        //   b = asin(rot[2][0])
        //   a = atan2(-rot[2][1], rot[2][2])
        //   c = atan2(-rot[1][0], rot[0][0])
        const float sy = glm::clamp(rot[2][0], -1.0f, 1.0f);
        const float b = std::asin(sy);

        float a = 0.0f;
        float c = 0.0f;
        if (std::fabs(sy) < 0.99999f) {
            a = std::atan2(-rot[2][1], rot[2][2]);
            c = std::atan2(-rot[1][0], rot[0][0]);
        } else {
            // Gimbal lock: X and Z are degenerate, so fold everything into X.
            a = std::atan2(rot[1][2], rot[1][1]);
            c = 0.0f;
        }

        transform.rotation = glm::vec3(a, b, c);
    }
}

} // namespace Engine
