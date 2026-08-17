#include "editor/SceneHierarchyPanel.hpp"
#include "core/Components.hpp"
#include "core/TransformSystem.hpp"

#include <string>
#include <vector>

namespace Supersonic {

namespace {
constexpr const char* kDragPayload = "ENGINE_ENTITY";
} // namespace

SceneHierarchyPanel::SceneHierarchyPanel(entt::registry& registry)
    : m_registry(&registry) {}

void SceneHierarchyPanel::OnImGuiRender() {
    ImGui::Begin("Scene Hierarchy");

    if (m_registry) {
        // Roots only; children are drawn by their parent, which is what turns
        // this from a flat list into an actual tree.
        std::vector<entt::entity> roots;
        for (auto entity : m_registry->view<entt::entity>()) {
            const auto* hierarchy = m_registry->try_get<HierarchyComponent>(entity);
            const bool hasLiveParent = hierarchy && hierarchy->parent != entt::null &&
                                       m_registry->valid(hierarchy->parent);
            if (!hasLiveParent) roots.push_back(entity);
        }

        for (const auto entity : roots) {
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

bool SceneHierarchyPanel::drawEntityNode(entt::entity entity) {
    if (!m_registry->valid(entity)) return false;

    std::string label = "Entity " + std::to_string(static_cast<uint32_t>(entity));
    if (const auto* tag = m_registry->try_get<TagComponent>(entity)) {
        if (!tag->tag.empty()) label = tag->tag;
    }

    std::vector<entt::entity> children;
    for (auto candidate : m_registry->view<HierarchyComponent>()) {
        if (m_registry->get<HierarchyComponent>(candidate).parent == entity) {
            children.push_back(candidate);
        }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const bool opened = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity))),
        flags, "%s", label.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_selectedEntity = entity;
    }

    // Drag a row onto another to parent it there.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(kDragPayload, &entity, sizeof(entt::entity));
        ImGui::TextUnformatted(label.c_str());
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

    if (opened && !children.empty()) {
        for (const auto child : children) {
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
    if (ImGui::MenuItem("Create Camera")) {
        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Camera");
        registry.emplace<TransformComponent>(entity);
        registry.emplace<CameraComponent>(entity);
        m_selectedEntity = entity;
    }
}

} // namespace Supersonic
