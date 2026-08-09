#include "editor/SceneHierarchyPanel.hpp"
#include "core/Components.hpp"

#include <string>

namespace Engine {

SceneHierarchyPanel::SceneHierarchyPanel(entt::registry& registry)
    : m_registry(&registry) {}

void SceneHierarchyPanel::OnImGuiRender() {
    ImGui::Begin("Scene Hierarchy");

    if (m_registry) {
        auto view = m_registry->view<entt::entity>();
        for (auto entityID : view) {
            drawEntityNode(entityID);
        }

        // Click on blank space to deselect entity
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
            m_selectedEntity = entt::null;
        }

        // Context Menu to create entities
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            if (ImGui::MenuItem("Create Cube")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Cube");
                m_registry->emplace<TransformComponent>(entity);
                m_registry->emplace<MeshComponent>(entity, "Cube", "", 24, 36);
                m_registry->emplace<MaterialComponent>(entity);
                m_registry->emplace<RenderableComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create Physics Cube")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Physics Cube");
                m_registry->emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
                m_registry->emplace<MeshComponent>(entity, "Cube", "", 24, 36);
                m_registry->emplace<MaterialComponent>(entity);
                m_registry->emplace<RigidBodyComponent>(entity);
                m_registry->emplace<BoxColliderComponent>(entity);
                m_registry->emplace<RenderableComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create Sphere")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Sphere");
                m_registry->emplace<TransformComponent>(entity);
                m_registry->emplace<MeshComponent>(entity, "Sphere", "", 64, 128);
                m_registry->emplace<MaterialComponent>(entity);
                m_registry->emplace<RenderableComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create 3D Terrain")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Procedural Terrain");
                m_registry->emplace<TransformComponent>(entity, glm::vec3(0.0f, -2.0f, 0.0f));
                m_registry->emplace<MeshComponent>(entity, "Terrain", "", 256, 512);
                m_registry->emplace<MaterialComponent>(entity);
                m_registry->emplace<RenderableComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create Plane")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Plane");
                m_registry->emplace<TransformComponent>(entity, glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
                m_registry->emplace<MeshComponent>(entity, "Plane", "", 4, 6);
                m_registry->emplace<MaterialComponent>(entity);
                m_registry->emplace<RenderableComponent>(entity);
                m_selectedEntity = entity;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Create Audio Source")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "3D Audio Source");
                m_registry->emplace<TransformComponent>(entity);
                m_registry->emplace<AudioSourceComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create Particle Emitter")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Particle Emitter");
                m_registry->emplace<TransformComponent>(entity);
                m_registry->emplace<ParticleEmitterComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create Directional Light")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Directional Light");
                m_registry->emplace<TransformComponent>(entity);
                m_registry->emplace<LightComponent>(entity);
                m_selectedEntity = entity;
            }
            if (ImGui::MenuItem("Create Camera")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Camera");
                m_registry->emplace<TransformComponent>(entity);
                m_registry->emplace<CameraComponent>(entity);
                m_selectedEntity = entity;
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

void SceneHierarchyPanel::drawEntityNode(entt::entity entity) {
    std::string label = "Entity " + std::to_string(static_cast<uint32_t>(entity));

    if (m_registry->all_of<TagComponent>(entity)) {
        auto& tag = m_registry->get<TagComponent>(entity);
        if (!tag.tag.empty()) {
            label = tag.tag;
        }
    }

    ImGuiTreeNodeFlags flags = ((m_selectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)static_cast<uint32_t>(entity), flags, "%s", label.c_str());

    if (ImGui::IsItemClicked()) {
        m_selectedEntity = entity;
    }

    bool entityDeleted = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Entity")) {
            entityDeleted = true;
        }
        ImGui::EndPopup();
    }

    if (opened) {
        ImGui::TreePop();
    }

    if (entityDeleted) {
        if (m_selectedEntity == entity) {
            m_selectedEntity = entt::null;
        }
        m_registry->destroy(entity);
    }
}

} // namespace Engine
