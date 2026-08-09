#pragma once

#include <memory>
#include <entt/entt.hpp>

#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanOffscreen.hpp"
#include "platform/Window.hpp"
#include "editor/SceneHierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"

namespace Engine {

class VulkanRenderer;

class EditorLayer {
public:
    EditorLayer() = default;
    ~EditorLayer() = default;

    void Init(VulkanDevice& device, uint32_t initialWidth, uint32_t initialHeight);
    void Shutdown();

    void OnImGuiRender(entt::registry& registry, Window& window);

    VulkanOffscreen& GetOffscreen() { return *m_offscreenPass; }

private:
    std::unique_ptr<VulkanOffscreen> m_offscreenPass;
    SceneHierarchyPanel m_hierarchyPanel;
    InspectorPanel m_inspectorPanel;

    bool m_showDemoWindow{false};
};

} // namespace Engine
