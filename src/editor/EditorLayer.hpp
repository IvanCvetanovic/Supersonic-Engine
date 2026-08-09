#pragma once

#include <memory>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanOffscreen.hpp"
#include "editor/SceneHierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/ContentBrowserPanel.hpp"
#include "platform/Window.hpp"

namespace Engine {

class EditorLayer {
public:
    EditorLayer() = default;
    ~EditorLayer() = default;

    void Init(VulkanDevice& device, uint32_t initialWidth, uint32_t initialHeight);
    void Shutdown();

    void OnImGuiRender(entt::registry& registry, Window& window);

    VulkanOffscreen& GetOffscreen() { return *m_offscreenPass; }
    SceneHierarchyPanel& GetHierarchyPanel() { return m_hierarchyPanel; }
    InspectorPanel& GetInspectorPanel() { return m_inspectorPanel; }

private:
    std::unique_ptr<VulkanOffscreen> m_offscreenPass;
    SceneHierarchyPanel m_hierarchyPanel;
    InspectorPanel m_inspectorPanel;
    ContentBrowserPanel m_contentBrowserPanel;

    bool m_showDemoWindow{false};
};

} // namespace Engine
