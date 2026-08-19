#include "editor/InspectorPanel.hpp"
#include <string>

#include "renderer/PointShadow.hpp"
#include "renderer/SpotLight.hpp"

#include <cstdio>
#include "editor/EditorIcons.hpp"
#include "core/MaterialSystem.hpp"
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

namespace Supersonic {

namespace {

// Accepts a dragged asset path on the widget just submitted.
//
// Returns true and fills `out` on a drop. The payload is a NUL-terminated path
// that ImGui copied at drag time, so it stays valid here even though the string
// it came from was rebuilt several frames ago.
bool acceptAssetDrop(const char* payloadType, std::string& out) {
    if (!ImGui::BeginDragDropTarget()) return false;

    bool dropped = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType)) {
        out.assign(static_cast<const char*>(payload->Data));
        dropped = true;
    }
    ImGui::EndDragDropTarget();
    return dropped;
}

} // namespace

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

namespace {

// The nine anchors, in the enum's order. A HUD element's anchor is the single
// setting that decides whether it survives a change of resolution, so it is
// worth a named dropdown rather than a raw integer.
bool drawAnchorCombo(const char* label, UIAnchor& anchor) {
    static const char* kAnchors[] = {
        "Top Left", "Top Center", "Top Right",
        "Middle Left", "Center", "Middle Right",
        "Bottom Left", "Bottom Center", "Bottom Right",
    };
    int current = static_cast<int>(anchor);
    if (ImGui::Combo(label, &current, kAnchors, IM_ARRAYSIZE(kAnchors))) {
        anchor = static_cast<UIAnchor>(current);
        return true;
    }
    return false;
}

} // namespace

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

            // The drop target is the whole row, not a button, so the gesture is
            // "drag the model onto the mesh" rather than "find the small widget".
            if (mesh.filePath.empty()) {
                ImGui::TextDisabled("Source: <primitive>   (drop a model here)");
            } else {
                ImGui::TextDisabled("Source: %s", mesh.filePath.c_str());
            }

            std::string droppedMesh;
            if (acceptAssetDrop("SUPERSONIC_MESH", droppedMesh)) {
                // Clearing the primitive is what makes the file win: MeshRegistry
                // keys on "file:" + path when there is one and "primitive:" + name
                // otherwise, so leaving both set would keep drawing the cube.
                mesh.filePath = droppedMesh;
                mesh.primitiveType.clear();
            }

            if (!mesh.filePath.empty() && ImGui::SmallButton("Revert to primitive")) {
                mesh.filePath.clear();
                mesh.primitiveType = "Cube";
            }
        }
    }

    ImGui::Spacing();

    // 2c. MaterialComponent - reaches the GPU via push constants.
    if (registry.all_of<MaterialComponent>(entity)) {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& material = registry.get<MaterialComponent>(entity);

            // A linked entity edits the SHARED asset, so the change lands on
            // every entity using it. An unlinked one edits only itself. Saying
            // which is which up front is the whole point - an inspector that
            // looked identical either way would make shared edits a surprise.
            const bool linked = !material.materialPath.empty() && m_materialLibrary != nullptr;
            MaterialAsset* asset = nullptr;
            if (linked) {
                asset = m_materialLibrary->Get(m_materialLibrary->Acquire(material.materialPath));
            }

            if (asset) {
                ImGui::PushStyleColor(ImGuiCol_Text, Brand::Cyan);
                ImGui::Text(ICON_FA_LAYER_GROUP "  %s", material.materialPath.c_str());
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Shared asset - edits apply to every entity using it.");

                ImGui::ColorEdit4("Albedo Tint", glm::value_ptr(asset->albedoColor));
                ImGui::SliderFloat("Roughness", &asset->roughness, 0.02f, 1.0f);
                ImGui::SliderFloat("Metallic", &asset->metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("Ambient Occlusion", &asset->ao, 0.0f, 1.0f);

                char assetAlbedo[512] = {};
                const size_t albedoLen = std::min(asset->albedoTexturePath.size(), sizeof(assetAlbedo) - 1);
                std::memcpy(assetAlbedo, asset->albedoTexturePath.data(), albedoLen);
                if (ImGui::InputText("Albedo Texture", assetAlbedo, sizeof(assetAlbedo))) {
                    asset->albedoTexturePath = assetAlbedo;
                }
                std::string droppedAlbedo;
                if (acceptAssetDrop("SUPERSONIC_TEXTURE", droppedAlbedo)) {
                    asset->albedoTexturePath = droppedAlbedo;
                }

                char assetNormal[512] = {};
                const size_t normalLen = std::min(asset->normalTexturePath.size(), sizeof(assetNormal) - 1);
                std::memcpy(assetNormal, asset->normalTexturePath.data(), normalLen);
                if (ImGui::InputText("Normal Map", assetNormal, sizeof(assetNormal))) {
                    asset->normalTexturePath = assetNormal;
                }
                std::string droppedNormal;
                if (acceptAssetDrop("SUPERSONIC_TEXTURE", droppedNormal)) {
                    asset->normalTexturePath = droppedNormal;
                }

                if (ImGui::Button(ICON_FA_FLOPPY "  Save Asset")) {
                    m_materialLibrary->Save(m_materialLibrary->Acquire(material.materialPath));
                }
                ImGui::SameLine();
                if (ImGui::Button("Make Unique")) {
                    // Keeps the look, drops the link - so an entity can diverge
                    // from the shared asset without first losing its appearance.
                    MaterialSystem::MakeUnique(registry, entity, *m_materialLibrary);
                }
            } else {

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

            char normalBuffer[512] = {};
            const size_t normalLen = std::min(material.normalTexturePath.size(), sizeof(normalBuffer) - 1);
            std::memcpy(normalBuffer, material.normalTexturePath.data(), normalLen);
            if (ImGui::InputText("Normal Map", normalBuffer, sizeof(normalBuffer))) {
                material.normalTexturePath = normalBuffer;
            }
            if (material.normalTexturePath.empty()) {
                ImGui::TextDisabled("Empty = flat normal; lighting uses the mesh normals.");
            }
            }
        }
    }

    ImGui::Spacing();

    // 3. RigidBodyComponent
    if (registry.all_of<RigidBodyComponent>(entity)) {
        if (ImGui::CollapsingHeader("RigidBody Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& rb = registry.get<RigidBodyComponent>(entity);

            ImGui::Checkbox("Use Gravity", &rb.useGravity);

            ImGui::SliderFloat("Restitution", &rb.restitution, 0.0f, 0.99f);
            ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 2.0f);
            ImGui::TextDisabled("Bounce takes the livelier surface; grip is the "
                                "geometric mean, so ice stays slippery.");

            ImGui::DragFloat("Linear Damping", &rb.linearDamping, 0.01f, 0.0f, 1.0f);
            Theme::DrawVec3Control("Angular Velocity", rb.angularVelocity, 0.0f);
            ImGui::DragFloat("Angular Damping", &rb.angularDamping, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Freeze Rotation", &rb.freezeRotation);
            ImGui::TextDisabled("Frozen bodies are pushed around but never tip over.");
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

    // 4b. SphereColliderComponent
    //
    // The component, its narrowphase and its serialization all existed; there
    // was simply no way to see or add one from the editor.
    if (registry.all_of<SphereColliderComponent>(entity)) {
        if (ImGui::CollapsingHeader("Sphere Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sphere = registry.get<SphereColliderComponent>(entity);
            ImGui::DragFloat("Radius", &sphere.radius, 0.01f, 0.01f, 100.0f);
            ImGui::Checkbox("Is Trigger##sphere", &sphere.isTrigger);
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
            if (audio.soundFile.empty()) {
                ImGui::TextDisabled("Clip: <none>   (drop a .wav here)");
            } else {
                ImGui::TextDisabled("Clip: %s", audio.soundFile.c_str());
            }
            std::string droppedClip;
            if (acceptAssetDrop("SUPERSONIC_AUDIO", droppedClip)) {
                audio.soundFile = droppedClip;
            }
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

            static const char* kTypes[] = { "Directional", "Point", "Spot" };
            ImGui::Combo("Type", &light.type, kTypes, IM_ARRAYSIZE(kTypes));

            if (light.type == static_cast<int>(LightType::Directional)) {
                Theme::DrawVec3Control("Direction", light.direction, 0.0f);
                ImGui::TextDisabled("Points toward the light.");
                ImGui::Checkbox("Casts Shadow", &light.castsShadow);
                ImGui::TextDisabled("Only the first shadow-casting directional light casts.");
            } else if (light.type == static_cast<int>(LightType::Spot)) {
                ImGui::TextDisabled("Position comes from the Transform; the cone "
                                    "points along Direction.");
                Theme::DrawVec3Control("Direction", light.direction, 0.0f);
                ImGui::DragFloat("Range", &light.range, 0.5f, 0.5f, 200.0f);

                float innerDegrees = glm::degrees(light.innerAngle);
                float outerDegrees = glm::degrees(light.outerAngle);
                if (ImGui::SliderFloat("Inner Angle", &innerDegrees, 0.5f, 89.0f, "%.1f deg")) {
                    light.innerAngle = glm::radians(innerDegrees);
                }
                if (ImGui::SliderFloat("Outer Angle", &outerDegrees, 0.5f, 89.0f, "%.1f deg")) {
                    light.outerAngle = glm::radians(outerDegrees);
                }
                ImGui::TextDisabled("Full brightness inside the inner angle, fading "
                                    "to nothing at the outer one.");

                ImGui::Checkbox("Casts Shadow", &light.castsShadow);
                ImGui::TextDisabled("The first %d spot lights that ask for a shadow get one.",
                                    static_cast<int>(SpotLight::kMaxShadowCasters));
            } else {
                ImGui::TextDisabled("Position comes from the Transform.");
                ImGui::DragFloat("Range", &light.range, 0.5f, 0.5f, 200.0f);
                ImGui::Checkbox("Casts Shadow", &light.castsShadow);
                ImGui::TextDisabled("Cube shadow map. The first %d point lights that ask "
                                    "for one get it; the rest shine through walls.",
                                    static_cast<int>(PointShadow::kMaxShadowCasters));
            }

            ImGui::ColorEdit3("Light Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 50.0f);
            ImGui::ColorEdit3("Ambient Sky", glm::value_ptr(light.ambient));
            ImGui::ColorEdit3("Ambient Ground", glm::value_ptr(light.ambientGround));
            ImGui::TextDisabled("Ambient is hemispheric: sky above, bounce below.");
            ImGui::TextDisabled("Ambient is scene-wide; taken from the first light.");
        }
    }

    ImGui::Spacing();

    // 8b. AnimatorComponent
    if (registry.all_of<AnimatorComponent>(entity)) {
        if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& animator = registry.get<AnimatorComponent>(entity);

            char clipBuffer[64];
            std::snprintf(clipBuffer, sizeof(clipBuffer), "%s", animator.clipName.c_str());
            if (ImGui::InputText("Clip", clipBuffer, sizeof(clipBuffer))) {
                animator.clipName = clipBuffer;
                animator.warnedMissing = false;
            }
            ImGui::TextDisabled("Empty plays the file's first clip.");

            ImGui::Checkbox("Playing", &animator.playing);
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &animator.loop);
            ImGui::DragFloat("Speed", &animator.speed, 0.05f, -4.0f, 4.0f);
            ImGui::DragFloat("Blend", &animator.blendDuration, 0.01f, 0.0f, 2.0f, "%.2f s");
            ImGui::TextDisabled("Cross-fade when the clip changes. 0 snaps.");
            if (animator.blendRemaining > 0.0f) {
                ImGui::TextDisabled("Blending from '%s' (%.2fs left)",
                                    animator.blendFromClip.c_str(),
                                    static_cast<double>(animator.blendRemaining));
            }

            // Scrubbable in edit mode. Poses are evaluated every frame whatever
            // the play state, so dragging this shows the pose immediately rather
            // than only once the scene is running.
            ImGui::DragFloat("Time", &animator.time, 0.01f, 0.0f, 120.0f);

            if (const auto* skin = registry.try_get<SkinnedMeshComponent>(entity)) {
                ImGui::TextDisabled("Rig: %zu joints", skin->jointMatrices.size());
            } else {
                ImGui::TextDisabled("No rig: this mesh has no glTF skin.");
            }
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
            ImGui::DragFloat("Move Speed", &camera.movementSpeed, 0.1f, 0.1f, 100.0f);

            // Nothing wrote isPrimary before this, so which camera play mode
            // rendered through was decided by EnTT pool order - and therefore
            // changed on Play, Stop and undo. Exclusive by construction: there is
            // no useful meaning to two primaries.
            bool primary = camera.isPrimary;
            if (ImGui::Checkbox("Primary Camera", &primary)) {
                if (primary) {
                    for (auto other : registry.view<CameraComponent>()) {
                        registry.get<CameraComponent>(other).isPrimary = (other == entity);
                    }
                } else {
                    camera.isPrimary = false;
                }
            }
            if (!camera.isPrimary) {
                ImGui::TextDisabled("Play mode renders through the primary camera.");
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // UITextComponent
    if (registry.all_of<UITextComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Text", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& text = registry.get<UITextComponent>(entity);

            // Fixed buffer with an explicit copy back, matching how the tag
            // field works: ImGui writes into the buffer, not the std::string.
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", text.text.c_str());
            if (ImGui::InputText("Text", buffer, sizeof(buffer))) {
                text.text = buffer;
            }

            drawAnchorCombo("Anchor", text.anchor);
            ImGui::DragFloat2("Offset", glm::value_ptr(text.offset), 1.0f, -4000.0f, 4000.0f);
            ImGui::DragFloat("Font Size", &text.fontSize, 0.5f, 4.0f, 300.0f);
            ImGui::ColorEdit4("Text Color", glm::value_ptr(text.color));
            ImGui::Checkbox("Drop Shadow", &text.shadow);
            ImGui::SameLine();
            ImGui::Checkbox("Visible##text", &text.visible);
            ImGui::TextDisabled("Sizes are authored at 1080p and scale with the window.");
        }
    }

    ImGui::Spacing();

    // UIPanelComponent
    if (registry.all_of<UIPanelComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Panel", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& panel = registry.get<UIPanelComponent>(entity);

            drawAnchorCombo("Anchor##panel", panel.anchor);
            ImGui::DragFloat2("Offset##panel", glm::value_ptr(panel.offset), 1.0f, -4000.0f, 4000.0f);
            ImGui::DragFloat2("Size", glm::value_ptr(panel.size), 1.0f, 1.0f, 4000.0f);
            ImGui::ColorEdit4("Fill Color", glm::value_ptr(panel.color));
            ImGui::DragFloat("Corner Radius", &panel.cornerRadius, 0.5f, 0.0f, 64.0f);
            ImGui::SliderFloat("Fill", &panel.fill, 0.0f, 1.0f);
            ImGui::TextDisabled("Fill turns the panel into a bar; 1 is a plain rectangle.");
            ImGui::Checkbox("Draw Track", &panel.drawTrack);
            if (panel.drawTrack) {
                ImGui::ColorEdit4("Track Color", glm::value_ptr(panel.trackColor));
            }
            ImGui::Checkbox("Visible##panel", &panel.visible);
        }
    }

    ImGui::Spacing();

    // UIButtonComponent
    if (registry.all_of<UIButtonComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Button", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& button = registry.get<UIButtonComponent>(entity);

            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%s", button.label.c_str());
            if (ImGui::InputText("Label", buffer, sizeof(buffer))) {
                button.label = buffer;
            }

            drawAnchorCombo("Anchor##button", button.anchor);
            ImGui::DragFloat2("Offset##button", glm::value_ptr(button.offset), 1.0f, -4000.0f, 4000.0f);
            ImGui::DragFloat2("Size##button", glm::value_ptr(button.size), 1.0f, 8.0f, 4000.0f);
            ImGui::DragFloat("Font Size##button", &button.fontSize, 0.5f, 4.0f, 300.0f);
            ImGui::DragFloat("Corner Radius##button", &button.cornerRadius, 0.5f, 0.0f, 64.0f);

            ImGui::ColorEdit4("Normal", glm::value_ptr(button.color));
            ImGui::ColorEdit4("Hovered", glm::value_ptr(button.hoverColor));
            ImGui::ColorEdit4("Pressed", glm::value_ptr(button.pressColor));
            ImGui::ColorEdit4("Label Color", glm::value_ptr(button.textColor));

            ImGui::Checkbox("Enabled", &button.enabled);
            ImGui::SameLine();
            ImGui::Checkbox("Visible##button", &button.visible);

            // Live state, so it is obvious the button is actually reacting
            // rather than merely drawn.
            ImGui::TextDisabled("%s%s%s", button.hovered ? "hovered " : "",
                                button.pressed ? "pressed " : "",
                                (!button.hovered && !button.pressed) ? "idle" : "");
            ImGui::TextDisabled("Scripts read clicks through ctx->ui->wasClicked.");
        }
    }

    ImGui::Spacing();

    // Dynamic "Add Component" Dropdown Button
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 160.0f) * 0.5f);
    if (ImGui::Button("+ Add Component", ImVec2(160.0f, 28.0f))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        // Mesh, Material and Renderable were absent from this menu, so an
        // entity made with Create Empty could never be made to draw - the three
        // components that decide whether anything appears were the three the
        // menu did not offer. "Renderable" is the one that actually gates the
        // draw, so it says so rather than being named after its type.
        if (!registry.all_of<MeshComponent>(entity) && ImGui::MenuItem("Mesh")) {
            registry.emplace<MeshComponent>(entity).primitiveType = "Cube";
        }

        if (!registry.all_of<MaterialComponent>(entity) && ImGui::MenuItem("Material")) {
            registry.emplace<MaterialComponent>(entity);
        }

        if (!registry.all_of<RenderableComponent>(entity) &&
            ImGui::MenuItem("Renderable (draw this entity)")) {
            registry.emplace<RenderableComponent>(entity);
        }

        ImGui::Separator();

        if (!registry.all_of<RigidBodyComponent>(entity) && ImGui::MenuItem("RigidBody Physics")) {
            registry.emplace<RigidBodyComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<BoxColliderComponent>(entity) && ImGui::MenuItem("Box Collider")) {
            registry.emplace<BoxColliderComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<SphereColliderComponent>(entity) && ImGui::MenuItem("Sphere Collider")) {
            registry.emplace<SphereColliderComponent>(entity);
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
        if (!registry.all_of<AnimatorComponent>(entity) && ImGui::MenuItem("Animator")) {
            registry.emplace<AnimatorComponent>(entity);
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
            // A second camera must not silently steal the primary flag from the
            // one the scene is already using, so it only claims it if nothing
            // else holds it.
            bool anotherIsPrimary = false;
            for (auto other : registry.view<CameraComponent>()) {
                if (registry.get<CameraComponent>(other).isPrimary) {
                    anotherIsPrimary = true;
                    break;
                }
            }
            auto& camera = registry.emplace<CameraComponent>(entity);
            camera.isPrimary = !anotherIsPrimary;
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<UITextComponent>(entity) && ImGui::MenuItem("HUD Text")) {
            registry.emplace<UITextComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<UIPanelComponent>(entity) && ImGui::MenuItem("HUD Panel")) {
            registry.emplace<UIPanelComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<UIButtonComponent>(entity) && ImGui::MenuItem("HUD Button")) {
            registry.emplace<UIButtonComponent>(entity);
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

} // namespace Supersonic
