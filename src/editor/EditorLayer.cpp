#include "editor/EditorLayer.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/Theme.hpp"
#include "core/JobSystem.hpp"
#include "core/Components.hpp"
#include "core/SceneSerializer.hpp"
#include "core/Raycast.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "core/EcsUtils.hpp"
#include "editor/GamePackager.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

#include <iostream>

namespace Supersonic {

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

const CameraComponent& EditorLayer::viewportCamera(entt::registry& registry) const {
    // Edit mode looks through the editor's own camera; play mode looks through
    // the scene's, which is what makes Play show the game's point of view.
    if (m_playMode && !m_playMode->IsEditing()) {
        if (const auto entity = FindPrimaryCamera(registry); entity != entt::null) {
            return registry.get<CameraComponent>(entity);
        }
    }
    return m_editorCamera.Get();
}

void EditorLayer::buildLayout(unsigned int dockspaceId, int preset) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    float leftRatio = 0.20f;
    float rightRatio = 0.24f;
    float bottomRatio = 0.30f;

    if (preset == 1) {          // Viewport focused
        leftRatio = 0.14f; rightRatio = 0.16f; bottomRatio = 0.22f;
    } else if (preset == 2) {   // Inspector focused
        leftRatio = 0.16f; rightRatio = 0.40f; bottomRatio = 0.26f;
    }

    // Splitting a node turns it into a parent, so the original id is no longer
    // a leaf you can dock into. The last argument returns the id of the
    // remaining half, and that is what must be docked to - passing nullptr
    // there is why panels ended up floating.
    ImGuiID centre = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, leftRatio, nullptr, &centre);
    ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, rightRatio, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, bottomRatio, nullptr, &centre);

    ImGuiID leftTop = left;
    const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &leftTop);

    ImGuiID rightTop = right;
    const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &rightTop);

    ImGui::DockBuilderDockWindow("Scene Hierarchy", leftTop);
    ImGui::DockBuilderDockWindow("Content Browser", leftBottom);
    ImGui::DockBuilderDockWindow("Viewport", centre);
    ImGui::DockBuilderDockWindow("Inspector", bottom);
    ImGui::DockBuilderDockWindow("Time-Travel Rewind Debugger", bottom);
    ImGui::DockBuilderDockWindow("Engine Statistics", rightTop);
    ImGui::DockBuilderDockWindow("Camera Preview", rightBottom);

    ImGui::DockBuilderFinish(dockspaceId);
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


    // 2. Editor Main Menu Bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_FA_FLOPPY "  Save Scene", "Ctrl+S")) {
                const auto result = SceneSerializer::Serialize(registry, kScenePath);
                SetStatus(result.message, !result.ok);
            }
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open Scene", "Ctrl+O")) {
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
        if (ImGui::BeginMenu("Edit")) {
            const bool menuEditing = !m_playMode || m_playMode->IsEditing();
            ImGui::BeginDisabled(!menuEditing || !m_history.CanUndo());
            if (ImGui::MenuItem(m_history.UndoLabel().c_str(), "Ctrl+Z")) {
                if (m_history.Undo(registry)) afterHistoryJump(registry, "Undone.");
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!menuEditing || !m_history.CanRedo());
            if (ImGui::MenuItem(m_history.RedoLabel().c_str(), "Ctrl+Y")) {
                if (m_history.Redo(registry)) afterHistoryJump(registry, "Redone.");
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::TextDisabled("%zu step(s) of history", m_history.UndoDepth());
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Gizmo Mode")) {
            if (ImGui::MenuItem(ICON_FA_ARROWS "  Translate (W)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::TRANSLATE); }
            if (ImGui::MenuItem(ICON_FA_ROTATE "  Rotate (E)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::ROTATE); }
            if (ImGui::MenuItem(ICON_FA_MAXIMIZE "  Scale (R)")) { m_inspectorPanel.SetGizmoOperation(ImGuizmo::SCALE); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Layout Presets")) {
            if (ImGui::MenuItem("Default Layout")) { m_pendingLayoutPreset = 0; }
            if (ImGui::MenuItem("Viewport Focused")) { m_pendingLayoutPreset = 1; }
            if (ImGui::MenuItem("Inspector Focused")) { m_pendingLayoutPreset = 2; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("ImGui Demo Window", nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // ---- Play / Pause / Stop toolbar ----
    if (m_playMode) {
        const bool playing = m_playMode->IsPlaying();
        const bool paused = m_playMode->IsPaused();

        // Tinted while simulating, so the mode is obvious at a glance rather
        // than something you infer from whether things are moving.
        if (playing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.22f, 1.0f));
        } else if (paused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.42f, 0.12f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.26f, 1.0f));
        }

        if (ImGui::Button(playing ? ICON_FA_PAUSE "  Pause##play" : ICON_FA_PLAY "  Play##play", ImVec2(96, 0))) {
            if (playing) {
                m_playMode->Pause();
                SetStatus("Paused.");
            } else {
                const auto result = m_playMode->Play(registry);
                SetStatus(result.message, !result.ok);
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::BeginDisabled(m_playMode->IsEditing());
        if (ImGui::Button(ICON_FA_STOP "  Stop##play", ImVec2(90, 0))) {
            const auto result = m_playMode->Stop(registry);
            SetStatus(result.message, !result.ok);
            m_hierarchyPanel.SetSelectedEntity(entt::null);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FORWARD_STEP "  Step##play", ImVec2(90, 0))) {
            if (m_playMode->IsPlaying()) m_playMode->Pause();
            m_playMode->RequestSingleStep();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const char* label = playing ? "PLAYING" : (paused ? "PAUSED" : "EDIT MODE");
        const ImVec4 colour = playing ? ImVec4(0.55f, 0.90f, 0.60f, 1.0f)
                            : paused  ? ImVec4(0.95f, 0.82f, 0.35f, 1.0f)
                                      : ImVec4(0.60f, 0.62f, 0.68f, 1.0f);
        ImGui::TextColored(colour, "%s", label);

        if (!m_playMode->IsEditing()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- Stop restores the scene as it was before Play.");
        }
        ImGui::Separator();
    }

    drawStatusBar();

    // Hotkeys. Suppressed whenever ImGui is routing keys to a widget, so typing
    // "Rock" into a text field no longer switches gizmo modes or wipes the scene.
    const bool editing = !m_playMode || m_playMode->IsEditing();

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
        // Gated on edit mode, matching the Edit menu. Undo replaces the whole
        // registry, so running it mid-play wiped the simulating scene - and
        // because handleUndoRedo bails out during play, nothing rebalanced the
        // stacks afterwards and the history collapsed for good.
        if (editing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            // Ctrl+Shift+Z is the other half of the same muscle memory.
            if (io.KeyShift) {
                if (m_history.Redo(registry)) afterHistoryJump(registry, "Redone.");
                else SetStatus("Nothing to redo.");
            } else {
                if (m_history.Undo(registry)) afterHistoryJump(registry, "Undone.");
                else SetStatus("Nothing to undo.");
            }
        }
        if (editing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            if (m_history.Redo(registry)) afterHistoryJump(registry, "Redone.");
            else SetStatus("Nothing to redo.");
        }
        // Gizmo mode shortcuts only in edit mode. During play they served no
        // purpose and the play camera's own W/A/S/D re-forced TRANSLATE on every
        // frame the key was held.
        if (editing && !io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::TRANSLATE);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::ROTATE);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_inspectorPanel.SetGizmoOperation(ImGuizmo::SCALE);
        }
    }

    // The dockspace is submitted AFTER the menu bar, toolbar and status
    // line, because DockSpace() consumes every remaining pixel of the
    // window. Anything drawn after it has no room and silently vanishes.
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        dockspaceId = ImGui::GetID("MyDockSpace");

        // Layout construction must happen BEFORE DockSpace() and before any
        // panel is submitted. DockSpace() creates the node itself, so testing
        // for a missing node after calling it never fires, and building the
        // layout at the end of the frame leaves every window half-docked.
        // DockBuilderDockWindow works by name, so the windows need not exist.
        if (!m_defaultLayoutApplied && ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
            m_pendingLayoutPreset = 0;   // fresh install: build the default layout
        }
        if (m_pendingLayoutPreset >= 0) {
            buildLayout(dockspaceId, m_pendingLayoutPreset);
            m_pendingLayoutPreset = -1;
            m_defaultLayoutApplied = true;
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
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

    // Sampled here rather than from io.WantCaptureMouse, which is true
    // everywhere inside an ImGui window and therefore true across the whole
    // viewport.
    m_viewportHovered = viewportVisible && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    m_viewportFocused = viewportVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    if (viewportVisible && m_offscreenPass) {
        const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

        if (viewportPanelSize.x >= 1.0f && viewportPanelSize.y >= 1.0f) {
            m_desiredViewportWidth = static_cast<uint32_t>(viewportPanelSize.x);
            m_desiredViewportHeight = static_cast<uint32_t>(viewportPanelSize.y);

            // The scene rasterises into the offscreen target, not the OS window,
            // so the camera's aspect ratio has to come from this panel. Taking it
            // from the window squashed everything by ~14% at the default layout.
            const float aspect = viewportPanelSize.x / viewportPanelSize.y;
            m_editorCamera.SetAspect(aspect);
            if (const auto camEntity = FindPrimaryCamera(registry); camEntity != entt::null) {
                registry.get<CameraComponent>(camEntity).aspect = aspect;
            }
        }

        // Exact top-left of the image in screen space; used to convert mouse
        // position into viewport-local coordinates for picking and the gizmo.
        const ImVec2 viewportPos = ImGui::GetCursorScreenPos();

        ImGui::Image(m_offscreenPass->GetTextureID(), viewportPanelSize);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver()) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const glm::vec2 localMouse(mousePos.x - viewportPos.x, mousePos.y - viewportPos.y);

            {
                const CameraComponent& camera = viewportCamera(registry);
                const Ray ray = Raycast::ScreenPointToRay(
                    localMouse, glm::vec2(viewportPanelSize.x, viewportPanelSize.y), camera);
                const entt::entity picked = Raycast::PickEntity(registry, ray);
                m_hierarchyPanel.SetSelectedEntity(picked);
                selectedEntity = picked;
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

        {
            const CameraComponent& camera = viewportCamera(registry);
            m_inspectorPanel.RenderGizmo(registry, selectedEntity, camera, viewportPos, viewportPanelSize);
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
    Theme::SectionLabel(ICON_FA_MICROCHIP "  JOBS");
    if (JobSystem::IsInitialized()) {
        ImGui::Text("Workers: %u", JobSystem::ThreadCount());
    } else {
        ImGui::TextDisabled("Single-threaded");
    }

    ImGui::Separator();
    Theme::SectionLabel(ICON_FA_CUBES "  PHYSICS");
    ImGui::Text("Contacts: %u  (%u trigger)", m_contactCount, m_triggerCount);

    ImGui::Separator();
    Theme::SectionLabel(ICON_FA_EYE "  CULLING");
    ImGui::Text("Scene:  %u drawn / %u culled", m_renderStats.drawn, m_renderStats.culled);
    ImGui::Text("Shadow: %u drawn / %u culled", m_renderStats.shadowDrawn, m_renderStats.shadowCulled);

    ImGui::Separator();
    Theme::SectionLabel(ICON_FA_CODE "  SCRIPT HOST");
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

    // Recorded last, after every panel has had its say, so one frame of
    // editing is one undo step.
    handleUndoRedo(registry);

    ImGui::End(); // End DockSpace
}

void EditorLayer::SetContacts(const std::vector<PhysicsSystem::Contact>& contacts) {
    m_contactCount = static_cast<uint32_t>(contacts.size());
    m_triggerCount = 0;
    for (const auto& contact : contacts) {
        if (contact.isTrigger) ++m_triggerCount;
    }
}

void EditorLayer::handleUndoRedo(entt::registry& registry) {
    // Play mode mutates the scene every frame by design; recording that would
    // fill the history with physics ticks and bury the actual edits. PlayMode
    // restores the pre-Play scene on Stop, which is the state already recorded
    // here, so nothing is lost by skipping it.
    if (m_playMode && !m_playMode->IsEditing()) return;

    // Only once the edit has settled. Mid-drag the scene changes every frame,
    // and a gizmo drag would otherwise become a hundred undo steps that each
    // rewind by a pixel.
    if (ImGuizmo::IsUsing()) return;
    if (ImGui::IsAnyItemActive()) return;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

    m_history.CommitIfChanged(registry);
}

void EditorLayer::afterHistoryJump(entt::registry& registry, const std::string& what) {
    // Deserialisation rebuilds the registry from scratch, so the selected
    // handle refers to an entity that no longer exists - and picking a stale
    // handle out of EnTT is how you get a crash rather than a wrong selection.
    const entt::entity selected = m_hierarchyPanel.GetSelectedEntity();
    if (selected != entt::null && !registry.valid(selected)) {
        m_hierarchyPanel.SetSelectedEntity(entt::null);
    }
    SetStatus(what);
}

} // namespace Supersonic
