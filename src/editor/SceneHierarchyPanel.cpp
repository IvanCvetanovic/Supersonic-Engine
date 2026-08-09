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
            if (ImGui::MenuItem("Create Empty Entity")) {
                auto entity = m_registry->create();
                m_registry->emplace<TagComponent>(entity, "Empty Entity");
                m_registry->emplace<TransformComponent>(entity);
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
