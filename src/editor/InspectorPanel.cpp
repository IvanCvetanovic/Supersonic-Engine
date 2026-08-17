#include "editor/InspectorPanel.hpp"
#include "editor/Theme.hpp"
#include "core/ScriptRegistry.hpp"
#include "core/TransformSystem.hpp"
#include "renderer/VulkanPipeline.hpp"   // LightType

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

            // Make it obvious these numbers are parent-relative once an entity
            // is attached to something.
            if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
                hierarchy && hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
                const char* parentName = "unnamed";
                if (const auto* tag = registry.try_get<TagComponent>(hierarchy->parent)) {
                    parentName = tag->tag.c_str();
                }
                ImGui::TextDisabled("Local to parent: %s", parentName);

                const glm::vec3 worldPos = glm::vec3(TransformSystem::GetWorldMatrix(registry, entity)[3]);
                ImGui::TextDisabled("World position: %.2f, %.2f, %.2f",
                                    static_cast<double>(worldPos.x),
                                    static_cast<double>(worldPos.y),
                                    static_cast<double>(worldPos.z));

                if (ImGui::Button("Detach from Parent")) {
                    TransformSystem::SetParent(registry, entity, entt::null);
                }
            }
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

            ImGui::ColorEdit4("Albedo Tint", glm::value_ptr(material.albedoColor));
            ImGui::SliderFloat("Roughness", &material.roughness, 0.02f, 1.0f);
            ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("Ambient Occlusion", &material.ao, 0.0f, 1.0f);

            // albedoTexturePath used to be a field nothing read.
            char texBuffer[512] = {};
            const size_t texLen = std::min(material.albedoTexturePath.size(), sizeof(texBuffer) - 1);
            std::memcpy(texBuffer, material.albedoTexturePath.data(), texLen);
            if (ImGui::InputText("Albedo Texture", texBuffer, sizeof(texBuffer))) {
                material.albedoTexturePath = texBuffer;
            }
            if (material.albedoTexturePath.empty()) {
                ImGui::TextDisabled("Empty = flat white; the tint and vertex colour still apply.");
            }
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

            // Populated from ScriptRegistry, so hot-reloaded plugin scripts show
            // up here without any editor change.
            const std::vector<std::string> names = ScriptRegistry::Get().Names();
            int current = -1;
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i] == script.scriptName) { current = static_cast<int>(i); break; }
            }

            if (ImGui::BeginCombo("Script", script.scriptName.c_str())) {
                for (size_t i = 0; i < names.size(); ++i) {
                    const bool selected = (current == static_cast<int>(i));
                    if (ImGui::Selectable(names[i].c_str(), selected)) {
                        script.scriptName = names[i];
                        script.elapsed = 0.0f;
                        script.baselineCaptured = false;
                        script.warnedMissing = false;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                // LightFlicker is native because it needs LightComponent, which
                // the flat script ABI does not carry.
                if (ImGui::Selectable("LightFlickerScript", script.scriptName == "LightFlickerScript")) {
                    script.scriptName = "LightFlickerScript";
                    script.elapsed = 0.0f;
                    script.baselineCaptured = false;
                }
                ImGui::EndCombo();
            }

            if (current < 0 && script.scriptName != "LightFlickerScript") {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "Not registered.");
            }
            ImGui::TextDisabled("Elapsed: %.1fs", static_cast<double>(script.elapsed));
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
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = registry.get<LightComponent>(entity);

            static const char* kTypes[] = { "Directional", "Point" };
            ImGui::Combo("Type", &light.type, kTypes, IM_ARRAYSIZE(kTypes));

            if (light.type == static_cast<int>(LightType::Directional)) {
                Theme::DrawVec3Control("Direction", light.direction, 0.0f);
                ImGui::TextDisabled("Points toward the light.");
                ImGui::Checkbox("Casts Shadow", &light.castsShadow);
                ImGui::TextDisabled("Only the first shadow-casting directional light casts.");
            } else {
                ImGui::TextDisabled("Position comes from the Transform.");
                ImGui::DragFloat("Range", &light.range, 0.5f, 0.5f, 200.0f);
            }

            ImGui::ColorEdit3("Light Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 50.0f);
            ImGui::ColorEdit3("Ambient Color", glm::value_ptr(light.ambient));
            ImGui::TextDisabled("Ambient is scene-wide; taken from the first light.");
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

    // The gizmo manipulates in WORLD space, so a child of a moved parent shows
    // its handles where it actually appears rather than at its local offset.
    glm::mat4 model = TransformSystem::GetWorldMatrix(registry, selectedEntity);
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
        // Converts the new world matrix back into a local transform under the
        // entity's parent, using the engine's own Euler convention.
        TransformSystem::SetWorldMatrix(registry, selectedEntity, model);
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
