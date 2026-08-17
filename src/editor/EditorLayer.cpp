#include "editor/EditorLayer.hpp"
#include "core/Components.hpp"
#include "core/SceneSerializer.hpp"
#include "core/Raycast.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "editor/GamePackager.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

#include <iostream>

namespace Engine {

namespace {
constexpr float kStatusVisibleSeconds = 6.0f;
constexpr const char* kScenePath = "assets/scenes/MainScene.scene";
} // namespace

void EditorLayer::Init(VulkanDevice& device, uint32_t initialWidth, uint32_t initialHeight) {
    m_offscreenPass = std::make_unique<VulkanOffscreen>(device, initialWidth, initialHeight);
    std::cout << "[EditorLayer] Dockable Editor Layer & Offscreen Viewport initialized." << std::endl;
}

void EditorLayer::Shutdown() {
    m_offscreenPass.reset();
    std::cout << "[EditorLayer] Editor Layer shutdown cleanly." << std::endl;
}

void EditorLayer::SetStatus(const std::string& message, bool isError) {
    m_statusMessage = message;
    m_statusIsError = isError;
    m_statusAge = 0.0f;
    (isError ? std::cerr : std::cout) << "[Editor] " << message << std::endl;
}

void EditorLayer::SetScriptHostInfo(bool pluginLoaded, const std::string& status, uint32_t reloadCount) {
    if (m_pluginLoaded && reloadCount > m_scriptReloadCount) {
        SetStatus("Script plugin reloaded (" + std::to_string(reloadCount) + ").");
    }
    m_pluginLoaded = pluginLoaded;
    m_scriptHostStatus = status;
    m_scriptReloadCount = reloadCount;
}

void EditorLayer::ApplyPendingResize() {
    if (!m_offscreenPass) return;
    if (m_desiredViewportWidth == 0 || m_desiredViewportHeight == 0) return;
    m_offscreenPass->RequestResize(m_desiredViewportWidth, m_desiredViewportHeight);
    m_offscreenPass->ApplyPendingResize();
}

void EditorLayer::drawStatusBar() {
    if (m_statusMessage.empty()) return;

    m_statusAge += ImGui::GetIO().DeltaTime;
    if (m_statusAge > kStatusVisibleSeconds) {
        m_statusMessage.clear();
        return;
    }

    const ImVec4 colour = m_statusIsError ? ImVec4(1.0f, 0.45f, 0.40f, 1.0f)
                                          : ImVec4(0.55f, 0.85f, 0.60f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextUnformatted(m_statusMessage.c_str());
    ImGui::PopStyleColor();
}

void EditorLayer::BuildUI(entt::registry& registry, Window& window) {
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
    ImGuiID dockspaceId = 0;
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        dockspaceId = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
    }

    int applyLayoutPreset = -1;

    // 2. Editor Main Menu Bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                const auto result = SceneSerializer::Serialize(registry, kScenePath);
                SetStatus(result.message, !result.ok);
            }
            if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
                const auto result = SceneSerializer::Deserialize(registry, kScenePath);
                SetStatus(result.message, !result.ok);
                if (result.ok) m_hierarchyPanel.SetSelectedEntity(entt::null);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Package Standalone Game")) {
                const auto result = GamePackager::PackageStandaloneGame("dist/GameRelease");
                SetStatus(result.message, !result.ok);
            }
            ImGui::Separator();
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
        if (ImGui::BeginMenu("Layout Presets")) {
            if (ImGui::MenuItem("Default Layout")) { applyLayoutPreset = 0; }
            if (ImGui::MenuItem("Viewport Focused")) { applyLayoutPreset = 1; }
            if (ImGui::MenuItem("Inspector Focused")) { applyLayoutPreset = 2; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    drawStatusBar();

    // Hotkeys. Suppressed whenever ImGui is routing keys to a widget, so typing
    // "Rock" into a text field no longer switches gizmo modes or wipes the scene.
    if (!io.WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            const auto result = SceneSerializer::Serialize(registry, kScenePath);
            SetStatus(result.message, !result.ok);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            const auto result = SceneSerializer::Deserialize(registry, kScenePath);
            SetStatus(result.message, !result.ok);
            if (result.ok) m_hierarchyPanel.SetSelectedEntity(entt::null);
        }
        if (!io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::TRANSLATE);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::ROTATE);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::SCALE);
        }
    }

    // 3. Render Hierarchy, Inspector, and Content Browser Panels
    m_hierarchyPanel.SetRegistry(registry);
    m_hierarchyPanel.OnImGuiRender();

    entt::entity selectedEntity = m_hierarchyPanel.GetSelectedEntity();
    m_inspectorPanel.OnImGuiRender(registry, selectedEntity);

    if (const auto browserStatus = m_contentBrowserPanel.OnImGuiRender(); !browserStatus.empty()) {
        SetStatus(browserStatus, true);
    }

    TimeTravelDebugger::RenderImGuiPanel(registry);

    // 4. Game Viewport Window displaying Offscreen Texture
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool viewportVisible = ImGui::Begin("Viewport");

    if (viewportVisible && m_offscreenPass) {
        const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

        if (viewportPanelSize.x >= 1.0f && viewportPanelSize.y >= 1.0f) {
            m_desiredViewportWidth = static_cast<uint32_t>(viewportPanelSize.x);
            m_desiredViewportHeight = static_cast<uint32_t>(viewportPanelSize.y);

            // The scene rasterises into the offscreen target, not the OS window,
            // so the camera's aspect ratio has to come from this panel. Taking it
            // from the window squashed everything by ~14% at the default layout.
            const float aspect = viewportPanelSize.x / viewportPanelSize.y;
            for (auto camEntity : registry.view<CameraComponent>()) {
                registry.get<CameraComponent>(camEntity).aspect = aspect;
                break;
            }
        }

        // Exact top-left of the image in screen space; used to convert mouse
        // position into viewport-local coordinates for picking and the gizmo.
        const ImVec2 viewportPos = ImGui::GetCursorScreenPos();

        ImGui::Image(m_offscreenPass->GetTextureID(), viewportPanelSize);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver()) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const glm::vec2 localMouse(mousePos.x - viewportPos.x, mousePos.y - viewportPos.y);

            for (auto camEnt : registry.view<CameraComponent>()) {
                const auto& camera = registry.get<CameraComponent>(camEnt);
                const Ray ray = Raycast::ScreenPointToRay(
                    localMouse, glm::vec2(viewportPanelSize.x, viewportPanelSize.y), camera);
                const entt::entity picked = Raycast::PickEntity(registry, ray);
                m_hierarchyPanel.SetSelectedEntity(picked);
                selectedEntity = picked;
                break;
            }
        }

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

        for (auto camEnt : registry.view<CameraComponent>()) {
            const auto& camera = registry.get<CameraComponent>(camEnt);
            m_inspectorPanel.RenderGizmo(registry, selectedEntity, camera, viewportPos, viewportPanelSize);
            break;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();

    // 5. Engine Statistics Panel
    ImGui::Begin("Engine Statistics");
    const float fps = io.Framerate;
    const float frameTime = 1000.0f / (fps > 0.0f ? fps : 60.0f);

    // storage<entt::entity>().size() counts released entities too, because EnTT
    // uses swap_only deletion. free_list() is the live count.
    const auto& entityStorage = registry.storage<entt::entity>();
    const auto entityCount = static_cast<uint32_t>(entityStorage.free_list());

    ImGui::Text("Graphics API:    Vulkan 1.2 (VMA 3.1)");
    ImGui::Text("Frame Time:      %.2f ms", frameTime);
    ImGui::Text("Framerate:       %.1f FPS", fps);
    ImGui::Text("Active Entities: %u", entityCount);
    if (m_offscreenPass) {
        ImGui::Text("Viewport Res:    %ux%u", m_offscreenPass->GetWidth(), m_offscreenPass->GetHeight());
    }

    ImGui::Separator();
    ImGui::TextDisabled("SCRIPT HOST");
    if (m_pluginLoaded) {
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.60f, 1.0f), "Plugin loaded");
        ImGui::Text("Reloads:         %u", m_scriptReloadCount);
    } else {
        ImGui::TextDisabled("Built-in scripts only");
    }
    if (!m_scriptHostStatus.empty()) {
        ImGui::TextWrapped("%s", m_scriptHostStatus.c_str());
    }
    ImGui::End();

    // 6. Camera Preview Window (Picture-in-Picture)
    ImGui::Begin("Camera Preview");
    ImGui::TextDisabled("Mirrors the active viewport camera.");
    ImGui::Separator();
    if (m_offscreenPass) {
        ImGui::Image(m_offscreenPass->GetTextureID(), ImVec2(240, 135));
    }
    ImGui::End();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }

    if (applyLayoutPreset >= 0 && dockspaceId != 0) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

        ImGuiID centre = dockspaceId;
        float leftRatio = 0.20f;
        float rightRatio = 0.25f;
        float bottomRatio = 0.25f;

        if (applyLayoutPreset == 1) {          // Viewport focused
            leftRatio = 0.14f; rightRatio = 0.16f; bottomRatio = 0.16f;
        } else if (applyLayoutPreset == 2) {   // Inspector focused
            leftRatio = 0.16f; rightRatio = 0.42f; bottomRatio = 0.20f;
        }

        const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, leftRatio, nullptr, &centre);
        const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, rightRatio, nullptr, &centre);
        const ImGuiID bottomLeft = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, bottomRatio, nullptr, nullptr);
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, nullptr);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
        ImGui::DockBuilderDockWindow("Content Browser", bottomLeft);
        ImGui::DockBuilderDockWindow("Viewport", centre);
        ImGui::DockBuilderDockWindow("Engine Statistics", right);
        ImGui::DockBuilderDockWindow("Camera Preview", rightBottom);
        ImGui::DockBuilderDockWindow("Inspector", rightBottom);
        ImGui::DockBuilderDockWindow("Time-Travel Rewind Debugger", bottomLeft);
        ImGui::DockBuilderFinish(dockspaceId);

        SetStatus("Applied workspace layout preset.");
    }

    ImGui::End(); // End DockSpace
}

} // namespace Engine
