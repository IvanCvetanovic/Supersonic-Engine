#include "editor/SceneHierarchyPanel.hpp"
#include "editor/EditorIcons.hpp"
#include "core/Components.hpp"
#include "core/TransformSystem.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace Supersonic {

namespace {
constexpr const char* kDragPayload = "ENGINE_ENTITY";
} // namespace

SceneHierarchyPanel::SceneHierarchyPanel(entt::registry& registry)
    : m_registry(&registry) {}

void SceneHierarchyPanel::BuildIndex(
    const entt::registry& registry,
    std::unordered_map<entt::entity, std::vector<entt::entity>>& outChildren,
    std::vector<entt::entity>& outRoots) {

    // The buckets are kept and emptied rather than dropped, so a panel that
    // runs every frame stops allocating after the first one.
    for (auto& bucket : outChildren) bucket.second.clear();
    outRoots.clear();

    for (auto entity : registry.view<entt::entity>()) {
        const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);

        // A parent that has been destroyed leaves its children pointing at a
        // released handle. They are roots, not orphans hidden under something
        // that no longer exists - and entity recycling means that handle may
        // later belong to something else entirely, so `valid` is not optional.
        const bool hasLiveParent = hierarchy && hierarchy->parent != entt::null &&
                                   registry.valid(hierarchy->parent);
        if (hasLiveParent) {
            outChildren[hierarchy->parent].push_back(entity);
        } else {
            outRoots.push_back(entity);
        }
    }
}

void SceneHierarchyPanel::OnImGuiRender() {
    ImGui::Begin("Scene Hierarchy");

    if (m_registry) {
        // One pass over the scene answers both questions: which entities are
        // roots, and which entities each parent owns. The walk below then looks
        // its children up instead of searching the whole pool for them.
        BuildIndex(*m_registry, m_children, m_roots);

        for (const auto entity : m_roots) {
            drawEntityNode(entity);
        }

        // Dropping onto empty space detaches to the root.
        ImGui::Dummy(ImVec2(0.0f, 24.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragPayload)) {
                m_pendingReparentChild = *static_cast<const entt::entity*>(payload->Data);
                m_pendingReparentParent = entt::null;
                m_hasPendingReparent = true;
            }
            ImGui::EndDragDropTarget();
        }

        // Click blank space to deselect. IsMouseDown + IsWindowHovered used to
        // clear the selection on the same frame a row set it.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered()) {
            m_selectedEntity = entt::null;
        }

        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu",
                ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            drawCreateMenu();
            ImGui::EndPopup();
        }

        // Applied after the walk, never during it.
        if (m_hasPendingReparent) {
            TransformSystem::SetParent(*m_registry, m_pendingReparentChild, m_pendingReparentParent);
            m_hasPendingReparent = false;
            m_pendingReparentChild = entt::null;
            m_pendingReparentParent = entt::null;
        }

        if (m_pendingDelete != entt::null && m_registry->valid(m_pendingDelete)) {
            // Children are promoted to the root first so they never point at a
            // released handle, which entity recycling would turn into a wrong
            // parent later.
            TransformSystem::OnParentDestroyed(*m_registry, m_pendingDelete);
            if (m_selectedEntity == m_pendingDelete) {
                m_selectedEntity = entt::null;
            }
            m_registry->destroy(m_pendingDelete);
        }
        m_pendingDelete = entt::null;
    }

    ImGui::End();
}

namespace {

// Icon for an entity, from the most telling component it carries. Ordered most
// specific first: a camera that also has a light is still a camera.
const char* iconForEntity(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<CameraComponent>(entity))            return ICON_FA_VIDEO;
    if (registry.all_of<LightComponent>(entity)) {
        const auto& light = registry.get<LightComponent>(entity);
        return light.type == 0 ? ICON_FA_SUN : ICON_FA_LIGHTBULB;
    }
    if (registry.all_of<AnimatorComponent>(entity))           return ICON_FA_PERSON_RUNNING;
    if (registry.all_of<ParticleEmitterComponent>(entity))    return ICON_FA_FIRE;
    if (registry.all_of<AudioSourceComponent>(entity))        return ICON_FA_VOLUME;
    if (registry.all_of<ScriptComponent>(entity))             return ICON_FA_CODE;
    if (registry.all_of<RigidBodyComponent>(entity))          return ICON_FA_CUBES;
    if (const auto* mesh = registry.try_get<MeshComponent>(entity)) {
        if (!mesh->filePath.empty())                          return ICON_FA_DIAGRAM;
        if (mesh->primitiveType == "Sphere")                  return ICON_FA_CIRCLE;
        if (mesh->primitiveType == "Plane")                   return ICON_FA_SQUARE;
        if (mesh->primitiveType == "Terrain")                 return ICON_FA_MOUNTAIN;
        return ICON_FA_CUBE;
    }
    return ICON_FA_LAYER_GROUP;
}

} // namespace

bool SceneHierarchyPanel::drawEntityNode(entt::entity entity) {
    if (!m_registry->valid(entity)) return false;

    // The name, without building a string to hold it.
    //
    // This used to compose "Entity N" for every row and then throw it away
    // whenever a tag existed, then concatenate the icon onto the front - three
    // heap allocations per row, per frame, to print two things ImGui is
    // perfectly happy to be handed separately.
    char fallback[32];
    const char* name = nullptr;
    if (const auto* tag = m_registry->try_get<TagComponent>(entity)) {
        if (!tag->tag.empty()) name = tag->tag.c_str();
    }
    if (!name) {
        std::snprintf(fallback, sizeof(fallback), "Entity %u",
                      static_cast<uint32_t>(entity));
        name = fallback;
    }

    // Looked up, not searched for. See m_children.
    const auto childIt = m_children.find(entity);
    const std::vector<entt::entity>* children =
        (childIt != m_children.end() && !childIt->second.empty()) ? &childIt->second : nullptr;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;
    if (!children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    // An icon per row, picked from what the entity actually is. A list of
    // twelve identical text labels is far harder to scan than the same list
    // with a shape at the head of each line.
    const char* icon = iconForEntity(*m_registry, entity);

    const bool opened = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity))),
        flags, "%s  %s", icon, name);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_selectedEntity = entity;
    }

    // Drag a row onto another to parent it there.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(kDragPayload, &entity, sizeof(entt::entity));
        ImGui::Text("%s  %s", icon, name);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragPayload)) {
            m_pendingReparentChild = *static_cast<const entt::entity*>(payload->Data);
            m_pendingReparentParent = entity;
            m_hasPendingReparent = true;
        }
        ImGui::EndDragDropTarget();
    }

    bool deleted = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Entity")) {
            m_pendingDelete = entity;
            deleted = true;
        }
        if (ImGui::MenuItem("Save as Prefab")) {
            m_pendingPrefabSave = entity;
        }
        if (m_registry->all_of<HierarchyComponent>(entity)) {
            if (ImGui::MenuItem("Detach from Parent")) {
                m_pendingReparentChild = entity;
                m_pendingReparentParent = entt::null;
                m_hasPendingReparent = true;
            }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Create Child")) {
            // A child created here is parented to the row it was opened on.
            if (ImGui::MenuItem("Empty")) {
                const auto child = m_registry->create();
                m_registry->emplace<TagComponent>(child, "Child");
                m_registry->emplace<TransformComponent>(child);
                m_pendingReparentChild = child;
                m_pendingReparentParent = entity;
                m_hasPendingReparent = true;
                m_selectedEntity = child;
            }
            if (ImGui::MenuItem("Cube")) {
                const auto child = m_registry->create();
                m_registry->emplace<TagComponent>(child, "Child Cube");
                m_registry->emplace<TransformComponent>(child, glm::vec3(0.0f, 1.5f, 0.0f));
                m_registry->emplace<MeshComponent>(child, "Cube", "", 24u, 36u);
                m_registry->emplace<MaterialComponent>(child);
                m_registry->emplace<RenderableComponent>(child);
                m_pendingReparentChild = child;
                m_pendingReparentParent = entity;
                m_hasPendingReparent = true;
                m_selectedEntity = child;
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (opened && children) {
        // Copied, because drawing a child can create or reparent an entity and
        // that rehashes the map this vector lives in. The copy is only made for
        // rows that are actually open, which in a large scene is a handful.
        const std::vector<entt::entity> openChildren = *children;
        for (const auto child : openChildren) {
            drawEntityNode(child);
        }
        ImGui::TreePop();
    }

    return deleted;
}

void SceneHierarchyPanel::drawCreateMenu() {
    auto& registry = *m_registry;

    auto spawn = [&](const char* name, const char* primitive, const glm::vec3& position,
                     const glm::vec3& scale = glm::vec3(1.0f)) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, name);
        auto& transform = registry.emplace<TransformComponent>(entity, position);
        transform.scale = scale;
        registry.emplace<MeshComponent>(entity, primitive, "", 0u, 0u);
        registry.emplace<MaterialComponent>(entity);
        registry.emplace<RenderableComponent>(entity);
        m_selectedEntity = entity;
        return entity;
    };

    if (ImGui::MenuItem("Create Empty")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Empty");
        registry.emplace<TransformComponent>(entity);
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create Cube"))   spawn("Cube", "Cube", glm::vec3(0.0f, 0.5f, 0.0f));
    if (ImGui::MenuItem("Create Sphere")) spawn("Sphere", "Sphere", glm::vec3(0.0f, 0.5f, 0.0f));
    if (ImGui::MenuItem("Create Plane"))  spawn("Plane", "Plane", glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    if (ImGui::MenuItem("Create 3D Terrain")) {
        spawn("Procedural Terrain", "Terrain", glm::vec3(0.0f, -2.0f, 0.0f));
    }

    if (ImGui::MenuItem("Create Physics Cube")) {
        const auto entity = spawn("Physics Cube", "Cube", glm::vec3(0.0f, 5.0f, 0.0f));
        registry.emplace<RigidBodyComponent>(entity);
        registry.emplace<BoxColliderComponent>(entity);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Create Audio Source")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "3D Audio Source");
        registry.emplace<TransformComponent>(entity);
        registry.emplace<AudioSourceComponent>(entity);
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create Particle Emitter")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Particle Emitter");
        registry.emplace<TransformComponent>(entity);
        registry.emplace<ParticleEmitterComponent>(entity);
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create Directional Light")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Directional Light");
        registry.emplace<TransformComponent>(entity);
        registry.emplace<LightComponent>(entity);
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create Point Light")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Point Light");
        registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
        auto& light = registry.emplace<LightComponent>(entity);
        light.type = 1;
        light.intensity = 3.0f;
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create HUD Text")) {
        const auto entity = m_registry->create();
        m_registry->emplace<TagComponent>(entity, "HUD Text");
        // Deliberately no TransformComponent: a HUD element lives in screen
        // space and has nothing to place in the world.
        m_registry->emplace<UITextComponent>(entity);
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create HUD Bar")) {
        const auto entity = m_registry->create();
        m_registry->emplace<TagComponent>(entity, "HUD Bar");
        auto& panel = m_registry->emplace<UIPanelComponent>(entity);
        panel.anchor = UIAnchor::BottomLeft;
        panel.size = glm::vec2(360.0f, 26.0f);
        panel.color = glm::vec4(0.85f, 0.22f, 0.24f, 0.95f);
        panel.drawTrack = true;
        panel.fill = 0.7f;
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create HUD Button")) {
        const auto entity = m_registry->create();
        m_registry->emplace<TagComponent>(entity, "HUD Button");
        m_registry->emplace<UIButtonComponent>(entity);
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create Spot Light")) {
        const auto entity = m_registry->create();
        m_registry->emplace<TagComponent>(entity, "Spot Light");
        m_registry->emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
        auto& light = m_registry->emplace<LightComponent>(entity);
        light.type = static_cast<int>(LightType::Spot);
        light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        light.intensity = 6.0f;
        light.range = 20.0f;
        m_selectedEntity = entity;
    }
    if (ImGui::MenuItem("Create Camera")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Camera");
        registry.emplace<TransformComponent>(entity);
        registry.emplace<CameraComponent>(entity);
        m_selectedEntity = entity;
    }
}

entt::entity SceneHierarchyPanel::ConsumePrefabSaveRequest() {
    const entt::entity requested = m_pendingPrefabSave;
    m_pendingPrefabSave = entt::null;
    return requested;
}

} // namespace Supersonic
