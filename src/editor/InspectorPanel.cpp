#include "editor/InspectorPanel.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/RenderSettings.hpp"
#include <string>

#include "renderer/PointShadow.hpp"
#include "renderer/SpotLight.hpp"

#include <cstdio>
#include "editor/EditorIcons.hpp"
#include "core/AnimationLibrary.hpp"
#include "core/MaterialSystem.hpp"
#include "editor/Theme.hpp"
#include "core/ScriptRegistry.hpp"
#include "core/TransformSystem.hpp"
#include "renderer/VulkanPipeline.hpp"   // LightType

// GLM_ENABLE_EXPERIMENTAL is set on the target in CMakeLists.txt.
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "core/ConvexHullCache.hpp"
#include "core/Joints.hpp"
#include "core/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Supersonic {

namespace {

// Eight named checkboxes rather than a hex field.
//
// A mask edited as a number is a mask nobody edits: working out that a bullet
// which ignores its shooter needs 0xFFFFFFFD is the kind of arithmetic that
// gets done once, wrongly, and then copied. Eight layers is enough for the
// distinctions games actually make - player, enemy, terrain, trigger - and the
// rest of the 32 stay reachable from code for anyone who needs them.
// The hinge angle an author needs to see, worked out the way the SOLVER works
// it out.
//
// Building the same two reference directions and asking Joints::HingeAngle,
// rather than deriving the angle here, because two answers to one question is
// how the number in the inspector ends up disagreeing with the stop the door
// actually hits.
float hingeAngleOf(entt::registry& registry, entt::entity entity, const JointComponent& joint) {
    const auto worldOf = [&](entt::entity target) {
        if (const auto* world = registry.try_get<WorldTransformComponent>(target)) {
            return glm::mat3(world->matrix);
        }
        if (const auto* local = registry.try_get<TransformComponent>(target)) {
            return glm::mat3(local->getModelMatrix());
        }
        return glm::mat3(1.0f);
    };

    Joints::Constraint probe;

    const glm::mat3 basisA = worldOf(entity);
    const glm::vec3 axis = basisA * joint.axis;
    const float length = glm::length(axis);
    probe.axisA = length > 1e-6f ? axis / length : glm::vec3(0.0f, 1.0f, 0.0f);
    probe.referenceA = glm::normalize(basisA * Joints::PerpendicularTo(joint.axis));

    const glm::vec3 localReferenceB = Joints::PerpendicularTo(joint.connectedAxis);
    const bool toWorld = joint.connectedBody == entt::null ||
                         !registry.valid(joint.connectedBody);
    probe.referenceB = toWorld ? localReferenceB
                               : glm::normalize(worldOf(joint.connectedBody) * localReferenceB);

    return Joints::HingeAngle(probe);
}

void drawCollisionLayers(uint32_t& layer, uint32_t& collidesWith) {
    if (!ImGui::TreeNode("Collision Layers")) return;

    ImGui::TextDisabled("This collider is:");
    for (int bit = 0; bit < 8; ++bit) {
        const uint32_t flag = 1u << bit;
        bool on = (layer & flag) != 0;
        ImGui::PushID(bit);
        if (ImGui::Checkbox("##layer", &on)) {
            layer = on ? (layer | flag) : (layer & ~flag);
        }
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Text("%d", bit);
        if (bit < 7) ImGui::SameLine();
    }

    ImGui::TextDisabled("and collides with:");
    for (int bit = 0; bit < 8; ++bit) {
        const uint32_t flag = 1u << bit;
        bool on = (collidesWith & flag) != 0;
        ImGui::PushID(100 + bit);
        if (ImGui::Checkbox("##mask", &on)) {
            collidesWith = on ? (collidesWith | flag) : (collidesWith & ~flag);
        }
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Text("%d", bit);
        if (bit < 7) ImGui::SameLine();
    }

    // Both sides have to agree for a pair to be tested, so a collider that
    // talks to nothing is almost always a mistake rather than an intention.
    if (collidesWith == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f),
                           "Collides with nothing - this collider is inert.");
    }
    ImGui::TreePop();
}

} // namespace


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

// Scene-level physics, shown where the inspector would otherwise be empty.
//
// It belongs to the scene rather than to any entity, so there is nothing to
// select in order to reach it - which is exactly how the world ground plane
// managed to be an unconditional invisible floor that nobody could find or turn
// off. Anything without an entity to hold it needs somewhere to be seen.
void InspectorPanel::drawWorldSettings(entt::registry& registry) {
    // Materialised on first sight rather than on first edit, so what is shown
    // is what will be saved.
    auto& physics = registry.ctx().contains<PhysicsSettings>()
                        ? registry.ctx().get<PhysicsSettings>()
                        : registry.ctx().emplace<PhysicsSettings>();

    if (!ImGui::CollapsingHeader("World Physics", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Theme::DrawVec3Control("Gravity", physics.gravity, -9.81f);
    ImGui::TextDisabled("Metres per second squared. Per scene, not per body - a "
                        "body opts out with Use Gravity.");

    ImGui::Spacing();
    ImGui::Checkbox("Ground Plane", &physics.hasGroundPlane);
    if (physics.hasGroundPlane) {
        ImGui::DragFloat("Height", &physics.groundPlaneY, 0.05f);
    }
    ImGui::TextDisabled("An invisible solid floor across the whole world. Off by "
                        "default: it has no friction, nothing can fall below it, "
                        "and there is nothing to select when it catches something "
                        "unexpectedly.");

    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) return;

    // Edited in the SCENE, not on the renderer. That is what makes a tuned look
    // survive a save, and what puts it inside undo, redo and the snapshot Play
    // restores on Stop - all of which work by serialising the registry.
    auto& rendering = registry.ctx().contains<RenderSettings>()
                          ? registry.ctx().get<RenderSettings>()
                          : registry.ctx().emplace<RenderSettings>();

    ImGui::DragFloat("Threshold", &rendering.bloomThreshold, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Soft Knee", &rendering.bloomSoftKnee, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Intensity", &rendering.bloomIntensity, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("Exposure", &rendering.exposure, 0.01f, 0.01f, 8.0f);
    ImGui::TextDisabled("The scene is HDR here, so a threshold of 1 means "
                        "brighter than white. These were compile-time constants; "
                        "a night level and a bright exterior do not share them.");

    ImGui::Spacing();
    ImGui::SeparatorText("Fog");
    ImGui::DragFloat("Density", &rendering.fogDensity, 0.001f, 0.0f, 0.5f, "%.4f");
    ImGui::ColorEdit3("Fog Colour", rendering.fogColor);
    ImGui::TextDisabled("Zero density is no fog. Around 0.02 puts the horizon "
                        "at roughly fifty units.");
}

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
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawWorldSettings(registry);
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

                // Take the file's own material, once. The file is not parsed
                // until the next resolve, so this is a request rather than a
                // copy - see the comment on the flag. Without it, dropping in a
                // model that describes rough gold with a texture on it produced
                // untextured white plastic and left the user to retype, by
                // hand, values the file had already stated.
                //
                // Only asked for when something will answer. SyncResources
                // walks RenderableComponent, so an entity without one is never
                // resolved - "Add Component -> Mesh" adds a MeshComponent on
                // its own, and on that entity the request would sit set for the
                // life of the scene and quietly never happen.
                if (registry.all_of<RenderableComponent>(entity)) {
                    mesh.importMaterialOnResolve = true;
                }
            }

            if (!mesh.filePath.empty() && ImGui::SmallButton("Revert to primitive")) {
                mesh.filePath.clear();
                mesh.primitiveType = "Cube";
            }
        }
    }

    // RenderableComponent
    //
    // Both flags have been serialized and read since the day they existed, and
    // neither had a control anywhere: every "Casts Shadow" checkbox in this
    // panel belongs to a LIGHT, so an author could only reach these by editing
    // the scene file by hand. That mattered more once a material could decide
    // what it casts - a blended surface opts back in with a cutoff, but a solid
    // one had no way to opt out at all.
    if (registry.all_of<RenderableComponent>(entity)) {
        if (ImGui::CollapsingHeader("Renderable", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& renderable = registry.get<RenderableComponent>(entity);

            ImGui::Checkbox("Visible##renderable", &renderable.isVisible);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Hidden here means not drawn AND not casting.");
            }

            ImGui::SameLine();
            ImGui::Checkbox("Casts Shadow##renderable", &renderable.castsShadow);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A receiver-only surface - a floor, a backdrop - costs\n"
                                  "nothing to leave out of the eighteen depth passes.");
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

                char assetOrm[512] = {};
                const size_t ormLen = std::min(asset->ormTexturePath.size(), sizeof(assetOrm) - 1);
                std::memcpy(assetOrm, asset->ormTexturePath.data(), ormLen);
                if (ImGui::InputText("ORM Map", assetOrm, sizeof(assetOrm))) {
                    asset->ormTexturePath = assetOrm;
                }
                std::string droppedOrm;
                if (acceptAssetDrop("SUPERSONIC_TEXTURE", droppedOrm)) {
                    asset->ormTexturePath = droppedOrm;
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

            // The blend state is per-material and the draw order depends on
            // it, so this is a rendering decision rather than a colour one.
            ImGui::ColorEdit3("Emissive", glm::value_ptr(material.emissiveColor));
            // Above 1 is the useful range: the scene target is floating point
            // and bloom thresholds at 1.0, so a strength under one makes a
            // surface pale rather than glowing.
            ImGui::DragFloat("Emissive Strength", &material.emissiveStrength,
                             0.05f, 0.0f, 50.0f, "%.2f");

            ImGui::Checkbox("Transparent", &material.transparent);
            if (material.transparent) {
                ImGui::SameLine();
                ImGui::TextDisabled("(alpha from Albedo, drawn back to front)");
            }

            // The other half of transparency, and the one foliage wants. Zero
            // is off, so the slider bottoms out at "no cutout" rather than
            // needing a checkbox beside it.
            ImGui::SliderFloat("Alpha Cutoff", &material.alphaCutoff, 0.0f, 1.0f, "%.2f");
            if (material.alphaCutoff > 0.0f) {
                ImGui::SameLine();
                ImGui::TextDisabled("(hard edge, stays opaque)");
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("(off)");
            }

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

            char ormBuffer[512] = {};
            const size_t ormLen = std::min(material.ormTexturePath.size(), sizeof(ormBuffer) - 1);
            std::memcpy(ormBuffer, material.ormTexturePath.data(), ormLen);
            if (ImGui::InputText("ORM Map", ormBuffer, sizeof(ormBuffer))) {
                material.ormTexturePath = ormBuffer;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Occlusion in red, roughness in green, metallic in blue -\n"
                                  "the way glTF packs them. Each channel MULTIPLIES the\n"
                                  "slider above it, so the sliders stay master controls.");
            }
            if (material.ormTexturePath.empty()) {
                ImGui::TextDisabled("Empty = the three sliders above, uniformly.");
            } else {
                // Only worth showing when there IS a map, because it decides
                // how much of that map's red channel to believe.
                ImGui::SliderFloat("Occlusion Strength", &material.occlusionStrength,
                                   0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How much of the RED channel is really occlusion.\n"
                        "glTF leaves red undefined in a metallic-roughness image, so\n"
                        "the importer sets this to 0 unless an occlusion texture\n"
                        "vouched for it. Zero means the channel is ignored entirely.");
                }
                if (material.occlusionStrength <= 0.0f) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(red ignored)");
                }
            }

            // The texture coordinate transform, applied to all three maps
            // above. Collapsed by default because the identity is what almost
            // every material wants and an always-open block of three more
            // controls buries the ones that matter.
            if (ImGui::TreeNode("UV Transform")) {
                ImGui::DragFloat2("UV Scale", &material.uvScale.x, 0.01f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How many times the texture repeats across the surface.\n"
                        "A sixteen-frame flipbook strip is (1/16, 1) here and an\n"
                        "offset of frame/16 below.");
                }

                float degrees = glm::degrees(material.uvRotation);
                if (ImGui::DragFloat("UV Rotation", &degrees, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
                    material.uvRotation = glm::radians(degrees);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("About the texture's TOP-LEFT corner, not its middle.\n"
                                      "Rotating about the centre is offset 0.5, rotate,\n"
                                      "offset -0.5 - written out rather than guessed at.");
                }

                ImGui::DragFloat2("UV Offset", &material.uvOffset.x, 0.005f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Slides the texture. Animate this from game code and\n"
                                      "the surface scrolls: rain, a conveyor, a waterfall.");
                }

                if (!material.HasUvTransform()) {
                    ImGui::TextDisabled("Identity - this material costs no transform slot.");
                }
                ImGui::TreePop();
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

            ImGui::Checkbox("Allow Sleep", &rb.allowSleep);
            if (rb.isSleeping) {
                ImGui::SameLine();
                ImGui::TextDisabled("(asleep)");
            }
            ImGui::TextDisabled("A settled body stops being simulated until "
                                "something touches or moves it.");
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
            Theme::DrawVec3Control("Center", box.center, 0.0f);
            ImGui::Checkbox("Is Trigger", &box.isTrigger);
            drawCollisionLayers(box.layer, box.collidesWith);
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
            Theme::DrawVec3Control("Center##sphere", sphere.center, 0.0f);
            ImGui::Checkbox("Is Trigger##sphere", &sphere.isTrigger);
            drawCollisionLayers(sphere.layer, sphere.collidesWith);
        }
    }

    ImGui::Spacing();

    // 4c. CapsuleColliderComponent
    if (registry.all_of<CapsuleColliderComponent>(entity)) {
        if (ImGui::CollapsingHeader("Capsule Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& capsule = registry.get<CapsuleColliderComponent>(entity);
            ImGui::DragFloat("Radius##capsule", &capsule.radius, 0.01f, 0.01f, 100.0f);
            ImGui::DragFloat("Height##capsule", &capsule.height, 0.01f, 0.01f, 100.0f);
            // Total, caps included, because that is the number an author
            // measures against a doorway. Said here because the alternative
            // convention - the straight section only - is just as common and
            // there is no way to tell them apart from the value.
            ImGui::TextDisabled("Height is the TOTAL, caps included. Below twice "
                                "the radius it is simply a sphere.");
            Theme::DrawVec3Control("Center##capsule", capsule.center, 0.0f);
            ImGui::Checkbox("Is Trigger##capsule", &capsule.isTrigger);
            drawCollisionLayers(capsule.layer, capsule.collidesWith);
        }
    }

    ImGui::Spacing();

    // 4c-ii. ConvexHullColliderComponent
    if (registry.all_of<ConvexHullColliderComponent>(entity)) {
        if (ImGui::CollapsingHeader("Convex Hull Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& hull = registry.get<ConvexHullColliderComponent>(entity);

            const bool followsMesh = hull.sourcePath.empty() && hull.sourcePrimitive.empty();
            if (followsMesh) {
                const auto* mesh = registry.try_get<MeshComponent>(entity);
                ImGui::TextDisabled("Source: this entity's mesh (%s)",
                                    mesh ? (mesh->filePath.empty() ? mesh->primitiveType.c_str()
                                                                   : mesh->filePath.c_str())
                                         : "none");
                ImGui::TextDisabled("The collider is the shape you can see, and follows it.");
                if (!mesh) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                       "No mesh on this entity, so there is nothing to be "
                                       "the hull of.");
                }
            } else {
                ImGui::TextDisabled("Source: %s", hull.sourcePath.empty()
                                                      ? hull.sourcePrimitive.c_str()
                                                      : hull.sourcePath.c_str());
                if (ImGui::Button("Follow the mesh##hull")) {
                    hull.sourcePath.clear();
                    hull.sourcePrimitive.clear();
                }
            }

            std::string dropped;
            if (acceptAssetDrop("SUPERSONIC_MODEL", dropped)) {
                hull.sourcePath = dropped;
                hull.sourcePrimitive.clear();
            }
            ImGui::TextDisabled("Drop a model here to use a simpler shape than the mesh.");

            if (const ConvexDecomposition* built =
                    ConvexHullCache::For(registry).Get(registry, entity, hull)) {
                size_t vertices = 0;
                size_t faces = 0;
                float residual = 0.0f;
                for (const ConvexHull& piece : built->pieces()) {
                    vertices += piece.vertices().size();
                    faces += piece.faces().size();
                    residual += piece.residual();
                }
                ImGui::Text("%zu piece(s), %zu vertices, %zu faces", built->pieces().size(),
                            vertices, faces);

                // The number worth reading: how much SOLID the collider has
                // that the mesh does not. It is what an object will catch on
                // that the model would have let through, and it is zero for a
                // shape that was already convex.
                if (built->meshVolume() > 0.0f) {
                    const float fraction = built->invented() / built->meshVolume();
                    ImGui::TextDisabled("Invents %.3f units of solid (%.1f%% of the mesh).",
                                        static_cast<double>(built->invented()),
                                        static_cast<double>(fraction * 100.0f));
                }
                if (residual > 0.0f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                       "%.3f units of the mesh sit outside the hull "
                                       "(the vertex cap bit).",
                                       static_cast<double>(residual));
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "This source is not a solid, so the collider does nothing. "
                                   "A hull needs four points that are not all in one plane.");
            }

            ImGui::TextDisabled("A hull is CONVEX: a doughnut collides as a disc.");
            ImGui::Checkbox("Is Trigger##hull", &hull.isTrigger);
            drawCollisionLayers(hull.layer, hull.collidesWith);
        }
    }

    ImGui::Spacing();

    // 4d. HeightfieldColliderComponent
    if (registry.all_of<HeightfieldColliderComponent>(entity)) {
        if (ImGui::CollapsingHeader("Heightfield Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& terrain = registry.get<HeightfieldColliderComponent>(entity);

            int width = static_cast<int>(terrain.width);
            int depth = static_cast<int>(terrain.depth);
            if (ImGui::DragInt("Width##heightfield", &width, 1.0f, 2, 1024)) {
                terrain.width = static_cast<uint32_t>(std::max(width, 2));
            }
            if (ImGui::DragInt("Depth##heightfield", &depth, 1.0f, 2, 1024)) {
                terrain.depth = static_cast<uint32_t>(std::max(depth, 2));
            }
            ImGui::DragFloat("Height Scale##heightfield", &terrain.heightScale, 0.01f, 0.0f, 50.0f);
            ImGui::DragFloat("Thickness##heightfield", &terrain.thickness, 0.05f, 0.0f, 200.0f);

            // Said here because nothing at runtime can say it: a collider built
            // from different numbers than the mesh is a surface somewhere the
            // terrain is not, and it looks exactly like a solver bug.
            ImGui::TextDisabled("Must match the Terrain mesh: %u x %u at %.2f.",
                                TerrainGenerator::kPrimitiveWidth,
                                TerrainGenerator::kPrimitiveDepth,
                                static_cast<double>(TerrainGenerator::kPrimitiveHeightScale));
            const bool matches = terrain.width == TerrainGenerator::kPrimitiveWidth &&
                                 terrain.depth == TerrainGenerator::kPrimitiveDepth &&
                                 terrain.heightScale == TerrainGenerator::kPrimitiveHeightScale;
            if (!matches) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                   "This does not match the Terrain primitive.");
                if (ImGui::Button("Match the mesh##heightfield")) {
                    terrain.width = TerrainGenerator::kPrimitiveWidth;
                    terrain.depth = TerrainGenerator::kPrimitiveDepth;
                    terrain.heightScale = TerrainGenerator::kPrimitiveHeightScale;
                }
            }

            ImGui::TextDisabled("Thickness is how far the solid reaches BELOW the "
                                "surface, which is how a buried body gets out.");
            ImGui::Checkbox("Is Trigger##heightfield", &terrain.isTrigger);
            drawCollisionLayers(terrain.layer, terrain.collidesWith);
        }
    }

    ImGui::Spacing();

    // 4e. JointComponent
    if (registry.all_of<JointComponent>(entity)) {
        if (ImGui::CollapsingHeader("Joint", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& joint = registry.get<JointComponent>(entity);

            static const char* kTypes[] = { "Point", "Distance", "Hinge", "Weld" };
            int type = static_cast<int>(joint.type);
            if (ImGui::Combo("Type##joint", &type, kTypes, IM_ARRAYSIZE(kTypes))) {
                joint.type = static_cast<JointComponent::Type>(
                    std::clamp(type, 0, IM_ARRAYSIZE(kTypes) - 1));
            }

            ImGui::Checkbox("Enabled##joint", &joint.enabled);

            // The other end, chosen from what is in the scene. Typed in, an
            // entity handle is a number with a version in it and nothing an
            // author could reasonably guess.
            const bool toWorld = joint.connectedBody == entt::null ||
                                 !registry.valid(joint.connectedBody);
            std::string current = toWorld ? "<world>" : "<unnamed>";
            if (!toWorld) {
                if (const auto* tag = registry.try_get<TagComponent>(joint.connectedBody)) {
                    current = tag->tag;
                }
            }
            if (ImGui::BeginCombo("Connected##joint", current.c_str())) {
                if (ImGui::Selectable("<world>", toWorld)) joint.connectedBody = entt::null;
                for (auto other : registry.view<TransformComponent>()) {
                    // Itself is not an option: a joint to itself has no two
                    // bodies to hold apart and the solver skips it.
                    if (other == entity) continue;
                    const auto* tag = registry.try_get<TagComponent>(other);
                    const std::string label =
                        (tag ? tag->tag : std::string("Entity")) + "##joint" +
                        std::to_string(static_cast<uint32_t>(other));
                    if (ImGui::Selectable(label.c_str(), other == joint.connectedBody)) {
                        joint.connectedBody = other;
                    }
                }
                ImGui::EndCombo();
            }

            Theme::DrawVec3Control("Anchor##joint", joint.anchor, 0.0f);
            Theme::DrawVec3Control(toWorld ? "World Point##joint" : "Other Anchor##joint",
                                   joint.connectedAnchor, 0.0f);
            ImGui::TextDisabled(toWorld
                ? "With no connected body the second anchor is a point in the WORLD."
                : "Both anchors are local to their own entity.");

            if (joint.type == JointComponent::Type::Distance) {
                ImGui::DragFloat("Distance##joint", &joint.distance, 0.01f, 0.0f, 1000.0f);
                if (ImGui::Button("Set from current##joint")) {
                    const auto* here = registry.try_get<TransformComponent>(entity);
                    glm::vec3 target = joint.connectedAnchor;
                    if (!toWorld) {
                        if (const auto* there =
                                registry.try_get<TransformComponent>(joint.connectedBody)) {
                            target = there->position + joint.connectedAnchor;
                        }
                    }
                    if (here) joint.distance = glm::length(target - (here->position + joint.anchor));
                }
                ImGui::Checkbox("Rope##joint", &joint.rope);
                ImGui::TextDisabled("A rope resists stretching only, so a chain can fold.");
            }

            if (joint.type == JointComponent::Type::Hinge) {
                Theme::DrawVec3Control("Axis##joint", joint.axis, 0.0f);
                Theme::DrawVec3Control("Other Axis##joint", joint.connectedAxis, 0.0f);
                ImGui::TextDisabled("The same axis seen from each end. Two bodies that "
                                    "start aligned want the same numbers in both.");

                // The LIVE angle, which is the only way limits can sensibly be
                // authored: zero is where the two ends' reference directions
                // coincide, and that is an arbitrary configuration rather than
                // one anybody could predict. Read the number, then set the
                // stops around it.
                const float live = hingeAngleOf(registry, entity, joint);
                ImGui::Text("Current angle: %.1f deg", static_cast<double>(glm::degrees(live)));

                ImGui::Checkbox("Limit##joint", &joint.useLimit);
                if (joint.useLimit) {
                    float minDegrees = glm::degrees(joint.minAngle);
                    float maxDegrees = glm::degrees(joint.maxAngle);
                    if (ImGui::DragFloat("Min##jointlimit", &minDegrees, 1.0f, -180.0f, 180.0f)) {
                        joint.minAngle = glm::radians(minDegrees);
                    }
                    if (ImGui::DragFloat("Max##jointlimit", &maxDegrees, 1.0f, -180.0f, 180.0f)) {
                        joint.maxAngle = glm::radians(maxDegrees);
                    }
                    if (ImGui::Button("Stops around current##joint")) {
                        joint.minAngle = live - glm::radians(45.0f);
                        joint.maxAngle = live + glm::radians(45.0f);
                    }
                }

                ImGui::Checkbox("Spring##joint", &joint.useSpring);
                if (joint.useSpring) {
                    ImGui::DragFloat("Frequency (Hz)##joint", &joint.springFrequency, 0.05f,
                                     0.0f, 30.0f);
                    ImGui::DragFloat("Damping Ratio##joint", &joint.springDamping, 0.01f,
                                     0.0f, 4.0f);
                    ImGui::DragFloat("Rest Angle##joint", &joint.springRestAngle, 0.01f,
                                     -3.14159f, 3.14159f);
                    ImGui::TextDisabled("1.0 damping returns to rest as fast as it can "
                                        "without going past. Below 1 it oscillates.");
                    ImGui::TextDisabled("A frequency and a ratio rather than a stiffness, "
                                        "so the same pair suits a light door and a heavy one.");
                }

                ImGui::Checkbox("Motor##joint", &joint.useMotor);
                if (joint.useMotor) {
                    ImGui::DragFloat("Speed (rad/s)##joint", &joint.motorSpeed, 0.05f,
                                     -50.0f, 50.0f);
                    ImGui::DragFloat("Max Torque##joint", &joint.maxMotorTorque, 0.1f,
                                     0.0f, 10000.0f);
                    ImGui::TextDisabled("Without a torque cap a motor is infinitely strong "
                                        "and drives whatever is in the way through a wall.");
                }
            }

            ImGui::DragInt("Solve Order##joint", &joint.solveOrder, 0.1f, -100, 100);
            ImGui::TextDisabled("Lower solves first. A chain converges far faster "
                                "root to tip than tip to root.");

            ImGui::DragFloat("Break Force##joint", &joint.breakForce, 1.0f, 0.0f, 100000.0f);
            ImGui::DragFloat("Break Torque##joint", &joint.breakTorque, 1.0f, 0.0f, 100000.0f);
            ImGui::TextDisabled("Zero on either means it cannot be broken that way.");
            if (joint.broken) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "This joint has let go.");
                ImGui::SameLine();
                if (ImGui::Button("Mend##joint")) joint.broken = false;
            }

            ImGui::SliderFloat("Stiffness##joint", &joint.stiffness, 0.0f, 1.0f);
            ImGui::TextDisabled("How much of the joint's error to take out per step. "
                                "Below about 0.3 it visibly sags.");
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

            ImGui::Separator();
            ImGui::TextDisabled("Parameters");

            // Authored per entity, so the same script can be two different
            // things - a fast crate and a slow one - without being two scripts.
            int removeAt = -1;
            for (size_t i = 0; i < script.parameters.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));

                char nameBuffer[64] = {};
                const size_t n = std::min(script.parameters[i].first.size(),
                                          sizeof(nameBuffer) - 1);
                std::memcpy(nameBuffer, script.parameters[i].first.data(), n);

                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer))) {
                    script.parameters[i].first = nameBuffer;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                ImGui::DragFloat("##value", &script.parameters[i].second, 0.05f,
                                 -1.0e9f, 1.0e9f, "%.3f");
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH)) removeAt = static_cast<int>(i);

                ImGui::PopID();
            }
            // Deferred, because erasing inside the loop invalidates the index
            // the rest of the iteration is using.
            if (removeAt >= 0) {
                script.parameters.erase(script.parameters.begin() + removeAt);
            }

            if (ImGui::SmallButton(ICON_FA_PLUS "  Add Parameter")) {
                script.parameters.emplace_back("speed", 1.0f);
            }

            if (!script.state.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Runtime state (not saved)");
                for (const auto& [name, value] : script.state) {
                    ImGui::Text("  %-16s %.3f", name.c_str(), static_cast<double>(value));
                }
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

    // 8a. ReflectionProbeComponent
    if (registry.all_of<ReflectionProbeComponent>(entity)) {
        if (ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& probe = registry.get<ReflectionProbeComponent>(entity);

            ImGui::DragFloat3("Half Extent##probe", glm::value_ptr(probe.halfExtent), 0.1f,
                              0.0f, 500.0f);
            ImGui::TextDisabled("A box about this entity. Objects whose centre is inside "
                                "light from this probe instead of the scene.");

            char hdri[512] = {};
            const size_t length = std::min(probe.hdriPath.size(), sizeof(hdri) - 1);
            std::memcpy(hdri, probe.hdriPath.data(), length);
            if (ImGui::InputText("HDRI##probe", hdri, sizeof(hdri))) probe.hdriPath = hdri;

            ImGui::DragFloat("Intensity##probe", &probe.intensity, 0.05f, 0.0f, 20.0f);

            // What the renderer actually did with it, which is the only way to
            // see that a probe is over the slot cap rather than merely subtle.
            if (probe.resolvedSlot < 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                   "Not bound: no HDRI, or more probes than slots.");
            } else {
                ImGui::TextDisabled("Bound to slot %d.", probe.resolvedSlot);
            }
        }
    }

    ImGui::Spacing();

    // 8b. AnimatorComponent
    if (registry.all_of<AnimatorComponent>(entity)) {
        if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& animator = registry.get<AnimatorComponent>(entity);

            // The clip used to be typed. An exporter decides what a clip is
            // called, so getting it right meant opening the .gltf in a text
            // editor - and a name that is one character out is indistinguishable
            // from a name that is right, because both leave the mesh in bind
            // pose.
            const auto* skin = registry.try_get<SkinnedMeshComponent>(entity);
            const std::vector<AnimationClip>* clips =
                (m_animationLibrary && skin) ? m_animationLibrary->GetClips(skin->skeletonID)
                                             : nullptr;

            if (clips && !clips->empty()) {
                // The preview names what is SET, not what resolves, so a name
                // that matches nothing stays on screen rather than being quietly
                // shown as the clip that would actually play.
                const char* preview =
                    animator.clipName.empty() ? "(first clip)" : animator.clipName.c_str();

                if (ImGui::BeginCombo("Clip", preview)) {
                    if (ImGui::Selectable("(first clip)", animator.clipName.empty())) {
                        animator.clipName.clear();
                        animator.warnedMissing = false;
                    }
                    for (const auto& clip : *clips) {
                        const bool selected = animator.clipName == clip.name;
                        if (ImGui::Selectable(clip.name.c_str(), selected)) {
                            animator.clipName = clip.name;
                            animator.warnedMissing = false;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // A combo writes only when something is chosen, which is what
                // keeps a name the rig does not have. Snapping an unmatched name
                // to the first clip would be scene data loss on load - the rig
                // is resolved a frame or two after the scene is read, and the
                // panel does not know the difference between "wrong" and "not
                // resolved yet".
                if (!animator.clipName.empty() &&
                    !m_animationLibrary->FindClip(skin->skeletonID, animator.clipName)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f),
                                       "'%s' is not a clip in this rig - it is kept, not played.",
                                       animator.clipName.c_str());
                }
            } else {
                // No rig resolved yet, so there is nothing to pick from. The
                // text box stays, because naming a clip BEFORE the rig arrives
                // is the only way to set one on an entity whose mesh has not
                // loaded - and removing it would make that impossible rather
                // than merely awkward.
                char clipBuffer[64];
                std::snprintf(clipBuffer, sizeof(clipBuffer), "%s", animator.clipName.c_str());
                if (ImGui::InputText("Clip", clipBuffer, sizeof(clipBuffer))) {
                    animator.clipName = clipBuffer;
                    animator.warnedMissing = false;
                }
                ImGui::TextDisabled("No rig loaded, so there is nothing to pick from yet.");
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

            // The off switch, and the only place a game gets to throw it: a
            // scene is authored here and packaged as it stands, so this
            // checkbox IS the shipped answer.
            ImGui::Checkbox("Fly Controls", &camera.flyControlsEnabled);
            if (camera.flyControlsEnabled) {
                ImGui::TextDisabled("W/A/S/D, Space, Shift and right-drag fly this camera in Play.");
                ImGui::TextDisabled("Turn off for a game that binds those keys - the defaults do.");
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
            ImGui::Checkbox("Fill Width", &panel.fillWidth);
            ImGui::SameLine();
            ImGui::Checkbox("Fill Height", &panel.fillHeight);
            ImGui::TextDisabled("Spread across the screen on that axis, whatever the");
            ImGui::TextDisabled("screen is - a menu background, the dim behind a modal,");
            ImGui::TextDisabled("the strip a bottom bar sits on. The other axis keeps");
            ImGui::TextDisabled("its anchor and size.");
            ImGui::Checkbox("Visible##panel", &panel.visible);
        }
    }

    ImGui::Spacing();

    // UIShapeComponent
    if (registry.all_of<UIShapeComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& shape = registry.get<UIShapeComponent>(entity);

            const char* kinds[] = { "Ring", "Disc", "Line" };
            int kind = static_cast<int>(shape.kind);
            if (ImGui::Combo("Kind", &kind, kinds, IM_ARRAYSIZE(kinds))) {
                shape.kind = static_cast<UIShapeComponent::Kind>(kind);
            }

            ImGui::Checkbox("World Space##shape", &shape.worldSpace);
            if (shape.worldSpace) {
                ImGui::TextDisabled("Drawn where this entity is, projected through the");
                ImGui::TextDisabled("camera. Needs a transform; parent it to what it marks.");
            } else {
                drawAnchorCombo("Anchor##shape", shape.anchor);
            }
            ImGui::DragFloat2("Offset##shape", glm::value_ptr(shape.offset), 1.0f, -4000.0f, 4000.0f);

            if (shape.kind == UIShapeComponent::Kind::Line) {
                ImGui::DragFloat3("Endpoint", glm::value_ptr(shape.endpoint), 0.05f);
                ImGui::TextDisabled("A world position when world space, otherwise an offset");
                ImGui::TextDisabled("in authored units from the start.");
            } else {
                ImGui::DragFloat("Radius", &shape.radius, 0.5f, 0.5f, 2000.0f);
                ImGui::DragInt("Segments", &shape.segments, 0.5f, 3, 256);
            }

            if (shape.kind != UIShapeComponent::Kind::Disc) {
                ImGui::DragFloat("Thickness", &shape.thickness, 0.1f, 0.1f, 64.0f);
            }

            ImGui::ColorEdit4("Color##shape", glm::value_ptr(shape.color));
            ImGui::TextDisabled("Sizes are authored units at 1080, like font size - so a");
            ImGui::TextDisabled("marker keeps its share of the screen at any resolution.");
            ImGui::Checkbox("Visible##shape", &shape.visible);
        }
    }

    ImGui::Spacing();

    // UIStackComponent
    if (registry.all_of<UIStackComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Stack", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& stack = registry.get<UIStackComponent>(entity);

            ImGui::Checkbox("Horizontal", &stack.horizontal);
            ImGui::TextDisabled("Off is a column, on is a row. Children are whatever is");
            ImGui::TextDisabled("parented to this entity in the hierarchy.");
            drawAnchorCombo("Anchor##stack", stack.anchor);
            ImGui::DragFloat2("Offset##stack", glm::value_ptr(stack.offset), 1.0f, -4000.0f, 4000.0f);
            ImGui::DragFloat("Spacing##stack", &stack.spacing, 0.5f, 0.0f, 400.0f);
            ImGui::TextDisabled("The anchor places the whole block. A child in a stack");
            ImGui::TextDisabled("ignores its own anchor and offset.");
            ImGui::Checkbox("Visible##stack", &stack.visible);
        }
    }

    ImGui::Spacing();

    // UIOrderComponent
    if (registry.all_of<UIOrderComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Order", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& ordering = registry.get<UIOrderComponent>(entity);

            ImGui::DragInt("Order", &ordering.order, 0.2f, -1000, 1000);
            ImGui::TextDisabled("Position among siblings inside a stack, low first.");
            ImGui::DragInt("Layer", &ordering.layer, 0.2f, -1000, 1000);
            ImGui::TextDisabled("Which overlay it belongs to, low drawn first. A pause");
            ImGui::TextDisabled("menu over a HUD wants a higher number here.");
            ImGui::TextDisabled("Two fields because a menu's buttons need both: a rank");
            ImGui::TextDisabled("in the column AND the layer the whole menu is on.");
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

    // UITextFieldComponent
    if (registry.all_of<UITextFieldComponent>(entity)) {
        if (ImGui::CollapsingHeader("HUD Text Field", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& field = registry.get<UITextFieldComponent>(entity);

            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", field.text.c_str());
            if (ImGui::InputText("Text##field", buffer, sizeof(buffer))) {
                field.text = buffer;
                // The caret is a byte offset into the string that just changed
                // underneath it. Left alone it could point into the middle of a
                // character, or past the end.
                field.caret = static_cast<int>(field.text.size());
            }

            std::snprintf(buffer, sizeof(buffer), "%s", field.placeholder.c_str());
            if (ImGui::InputText("Placeholder", buffer, sizeof(buffer))) {
                field.placeholder = buffer;
            }

            drawAnchorCombo("Anchor##field", field.anchor);
            ImGui::DragFloat2("Offset##field", glm::value_ptr(field.offset), 1.0f, -4000.0f, 4000.0f);
            ImGui::DragFloat2("Size##field", glm::value_ptr(field.size), 1.0f, 8.0f, 4000.0f);
            ImGui::DragFloat("Font Size##field", &field.fontSize, 0.5f, 4.0f, 300.0f);
            ImGui::DragFloat("Corner Radius##field", &field.cornerRadius, 0.5f, 0.0f, 64.0f);
            ImGui::DragInt("Max Length", &field.maxLength, 1.0f, 0, 512);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Characters, not bytes. 0 means no limit.");
            }

            ImGui::ColorEdit4("Background##field", glm::value_ptr(field.color));
            ImGui::ColorEdit4("Focused", glm::value_ptr(field.focusColor));
            ImGui::ColorEdit4("Border", glm::value_ptr(field.borderColor));
            ImGui::ColorEdit4("Focus Border", glm::value_ptr(field.focusBorderColor));
            ImGui::ColorEdit4("Text Color##field", glm::value_ptr(field.textColor));
            ImGui::ColorEdit4("Placeholder Color", glm::value_ptr(field.placeholderColor));

            ImGui::Checkbox("Enabled##field", &field.enabled);
            ImGui::SameLine();
            ImGui::Checkbox("Visible##field", &field.visible);

            ImGui::TextDisabled("%s", field.focused ? "focused - typing goes here"
                                                    : "not focused");
            ImGui::TextDisabled("Scripts read it through ctx->ui->getText.");
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
        if (!registry.all_of<CapsuleColliderComponent>(entity) &&
            ImGui::MenuItem("Capsule Collider")) {
            registry.emplace<CapsuleColliderComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<ConvexHullColliderComponent>(entity) &&
            ImGui::MenuItem("Convex Hull Collider")) {
            registry.emplace<ConvexHullColliderComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<JointComponent>(entity) && ImGui::MenuItem("Joint")) {
            registry.emplace<JointComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<HeightfieldColliderComponent>(entity) &&
            ImGui::MenuItem("Heightfield Collider")) {
            registry.emplace<HeightfieldColliderComponent>(entity);
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
        if (!registry.all_of<UITextFieldComponent>(entity) && ImGui::MenuItem("HUD Text Field")) {
            registry.emplace<UITextFieldComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<UIShapeComponent>(entity) && ImGui::MenuItem("HUD Shape")) {
            registry.emplace<UIShapeComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<UIStackComponent>(entity) && ImGui::MenuItem("HUD Stack")) {
            registry.emplace<UIStackComponent>(entity);
            ImGui::CloseCurrentPopup();
        }
        if (!registry.all_of<UIOrderComponent>(entity) && ImGui::MenuItem("HUD Order")) {
            registry.emplace<UIOrderComponent>(entity);
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
