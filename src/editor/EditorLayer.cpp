#include "editor/EditorLayer.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#include "core/Components.hpp"

#include <iostream>

namespace Engine {

void EditorLayer::Init(VulkanDevice& device, uint32_t initialWidth, uint32_t initialHeight) {
    m_offscreenPass = std::make_unique<VulkanOffscreen>(device, initialWidth, initialHeight);
    std::cout << "[EditorLayer] Dockable Editor Layer & Offscreen Viewport initialized." << std::endl;
}

void EditorLayer::Shutdown() {
    m_offscreenPass.reset();
    std::cout << "[EditorLayer] Editor Layer shutdown cleanly." << std::endl;
}

void EditorLayer::OnImGuiRender(entt::registry& registry, Window& window) {
    // 1. Enable Fullscreen Central Dockspace
    ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("DockSpace Demo", nullptr, windowFlags);
    ImGui::PopStyleVar(2);

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
    }

    // 2. Editor Main Menu Bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(window.GetNativeWindow(), GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Gizmo Mode")) {
            if (ImGui::MenuItem("Translate (W)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::TRANSLATE); }
            if (ImGui::MenuItem("Rotate (E)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::ROTATE); }
            if (ImGui::MenuItem("Scale (R)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::SCALE); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Hotkey shortcuts for ImGuizmo mode
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::TRANSLATE);
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::ROTATE);
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::SCALE);
    }

    // 3. Render Hierarchy and Inspector Panels
    m_hierarchyPanel.SetRegistry(registry);
    m_hierarchyPanel.OnImGuiRender();

    entt::entity selectedEntity = m_hierarchyPanel.GetSelectedEntity();
    m_inspectorPanel.OnImGuiRender(registry, selectedEntity);

    // 4. Game Viewport Window displaying Offscreen Texture
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
        uint32_t vpWidth = static_cast<uint32_t>(viewportPanelSize.x);
        uint32_t vpHeight = static_cast<uint32_t>(viewportPanelSize.y);

        if (vpWidth != m_offscreenPass->GetWidth() || vpHeight != m_offscreenPass->GetHeight()) {
            m_offscreenPass->Recreate(vpWidth, vpHeight);
        }
    }

    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    ImVec2 viewportPos = ImVec2(windowPos.x + contentMin.x, windowPos.y + contentMin.y);

    ImGui::Image(m_offscreenPass->GetTextureID(), viewportPanelSize);

    // Overlay Viewport Toolbar
    ImGui::SetCursorPos(ImVec2(10, 30));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.18f, 0.85f));

    if (ImGui::Button("Translate (W)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::TRANSLATE); }
    ImGui::SameLine();
    if (ImGui::Button("Rotate (E)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::ROTATE); }
    ImGui::SameLine();
    if (ImGui::Button("Scale (R)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::SCALE); }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Render ImGuizmo 3D Manipulators over Viewport
    auto cameraView = registry.view<CameraComponent>();
    for (auto camEnt : cameraView) {
        const auto& camera = cameraView.get<CameraComponent>(camEnt);
        m_inspectorPanel.RenderGizmo(registry, selectedEntity, camera, viewportPos, viewportPanelSize);
        break;
    }

    ImGui::End();
    ImGui::PopStyleVar();

    // 5. Engine Statistics Panel
    ImGui::Begin("Engine Statistics");
    float fps = io.Framerate;
    float frameTime = 1000.0f / (fps > 0.0f ? fps : 60.0f);
    uint32_t entityCount = static_cast<uint32_t>(registry.storage<entt::entity>().size());

    ImGui::Text("Graphics API:    Vulkan 1.3 (VMA 3.1)");
    ImGui::Text("Frame Time:      %.2f ms", frameTime);
    ImGui::Text("Framerate:       %.1f FPS", fps);
    ImGui::Text("Active Entities: %u", entityCount);
    ImGui::Text("Viewport Res:    %ux%u", m_offscreenPass->GetWidth(), m_offscreenPass->GetHeight());
    ImGui::End();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }

    ImGui::End(); // End DockSpace
}

} // namespace Engine
