#include "editor/EditorLayer.hpp"
#include "core/AssetDatabase.hpp"
#include "core/AssetRepointer.hpp"
#include "core/Log.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/Theme.hpp"
#include "core/MaterialSystem.hpp"

#include <algorithm>
#include "core/JobSystem.hpp"
#include "core/Profiler.hpp"
#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/SceneSerializer.hpp"
#include "core/UISystem.hpp"
#include "core/ViewportInfo.hpp"
#include "core/PrefabSerializer.hpp"
#include "core/Raycast.hpp"
#include "core/TilemapSystem.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "core/EcsUtils.hpp"
#include "editor/GamePackager.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

#include <cstring>
#include <iostream>

namespace Supersonic {

namespace {
constexpr float kStatusVisibleSeconds = 6.0f;
} // namespace

void EditorLayer::Init(VulkanDevice& device, uint32_t initialWidth, uint32_t initialHeight,
                       vk::SampleCountFlagBits maxSamples) {
    m_offscreenPass = std::make_unique<VulkanOffscreen>(device, initialWidth, initialHeight, maxSamples);
    m_thumbnails = std::make_unique<ThumbnailCache>(device);
    m_contentBrowserPanel.SetThumbnails(m_thumbnails.get());
    SUPERSONIC_LOG_INFO("EditorLayer") << "Dockable Editor Layer & Offscreen Viewport initialized." << std::endl;
}

void EditorLayer::SetMaterialLibrary(MaterialLibrary* library) {
    m_materialLibrary = library;
    m_inspectorPanel.SetMaterialLibrary(library);
    m_contentBrowserPanel.SetMaterialLibrary(library);
}

void EditorLayer::SetAnimationLibrary(AnimationLibrary* library) {
    m_inspectorPanel.SetAnimationLibrary(library);
}

void EditorLayer::Shutdown() {
    // Before the offscreen target, and both before ImGui_ImplVulkan_Shutdown:
    // each holds ImGui descriptor sets that have to be handed back first.
    m_contentBrowserPanel.SetThumbnails(nullptr);
    m_thumbnails.reset();
    m_offscreenPass.reset();
    SUPERSONIC_LOG_INFO("EditorLayer") << "Editor Layer shutdown cleanly." << std::endl;
}

void EditorLayer::SetStatus(const std::string& message, bool isError) {
    m_statusMessage = message;
    m_statusIsError = isError;
    m_statusAge = 0.0f;
    if (isError) {
        SUPERSONIC_LOG_ERROR("Editor") << message;
    } else {
        SUPERSONIC_LOG_INFO("Editor") << message;
    }
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


void EditorLayer::OnSceneLoaded(entt::registry& registry, const SerializationResult& result) {
    SetStatus(result.message, !result.ok);
    if (result.ok) {
        // The selection is an entt::entity into a registry that no longer
        // exists. EnTT recycles handles, so a stale one does not dangle - it
        // resolves to a completely different entity in the new scene.
        m_hierarchyPanel.SetSelectedEntity(entt::null);
        // Reset, not Clear: its own comment says to call this after a load,
        // because undoing past one makes no sense - the undo stack describes a
        // scene that is no longer open.
        m_history.Reset(registry);
    }
}

void EditorLayer::drawViewportOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize) {
    const float pad = 12.0f;

    // Both overlays are child-less windows placed over the viewport image.
    // NoInputs on the readout matters: it sits where the user drags to orbit,
    // and a panel that swallowed those clicks would be worse than no panel.
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Brand::Bg0.x, Brand::Bg0.y, Brand::Bg0.z, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_Border, Brand::Line);

    // ---- Gizmo mode, top left ----
    ImGui::SetNextWindowPos(ImVec2(viewportPos.x + pad, viewportPos.y + pad), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.82f);
    if (ImGui::Begin("##viewport_tools", nullptr, kFlags)) {
        const ImGuizmo::OPERATION current = m_inspectorPanel.GetGizmoOperation();

        const auto modeButton = [&](const char* label, ImGuizmo::OPERATION op, const char* tip) {
            const bool active = current == op;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, Brand::Amber);
            if (ImGui::Button(label, ImVec2(34.0f, 28.0f))) {
                m_inspectorPanel.SetGizmoOperation(op);
            }
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };

        modeButton(ICON_FA_ARROWS, ImGuizmo::TRANSLATE, "Translate  (W)");
        ImGui::SameLine();
        modeButton(ICON_FA_ROTATE, ImGuizmo::ROTATE, "Rotate  (E)");
        ImGui::SameLine();
        modeButton(ICON_FA_MAXIMIZE, ImGuizmo::SCALE, "Scale  (R)");
    }
    ImGui::End();

    // ---- Live counters, top right ----
    ImGui::SetNextWindowPos(ImVec2(viewportPos.x + viewportSize.x - pad, viewportPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);
    if (ImGui::Begin("##viewport_stats", nullptr, kFlags | ImGuiWindowFlags_NoInputs)) {
        ImGui::PushStyleColor(ImGuiCol_Text, Brand::TextDim);
        ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextColored(Brand::Cyan, "%.2f ms", m_smoothedFrameTime);

        ImGui::PushStyleColor(ImGuiCol_Text, Brand::TextDim);
        ImGui::Text(ICON_FA_EYE "  %u drawn  %u culled", m_renderStats.drawn, m_renderStats.culled);
        ImGui::PopStyleColor();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
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

bool EditorLayer::paintTiles(entt::registry& registry, entt::entity selected, bool imageHovered,
                             const ImVec2& viewportPos, const ImVec2& viewportSize) {
    const InspectorPanel::TileBrush& brush = m_inspectorPanel.GetTileBrush();
    if (!brush.painting) return false;
    if (selected == entt::null || !registry.valid(selected)) return false;
    if (!registry.all_of<TilemapComponent, WorldTransformComponent>(selected)) return false;

    // From here the brush owns the click even when nothing is painted: a
    // stroke that runs off the edge of the map must not pick whatever is
    // behind it and drop the map out of the inspector mid-stroke.
    if (!imageHovered || ImGuizmo::IsOver() || ImGuizmo::IsUsing()) return true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return true;

    const ImVec2 mousePos = ImGui::GetMousePos();
    const glm::vec2 localMouse(mousePos.x - viewportPos.x, mousePos.y - viewportPos.y);
    const CameraComponent& camera = viewportCamera(registry);
    const Ray ray = Raycast::ScreenPointToRay(
        localMouse, glm::vec2(viewportSize.x, viewportSize.y), camera);

    auto& map = registry.get<TilemapComponent>(selected);
    const glm::mat4& world = registry.get<WorldTransformComponent>(selected).matrix;

    uint32_t column = 0;
    uint32_t row = 0;
    if (!TilemapSystem::CellFromRay(map, world, ray, column, row)) return true;

    // Only when it changes, so holding the button over one cell is one write
    // and not sixty a second - and so the content hash the renderer compares
    // each frame sees a map that has settled rather than one being rewritten
    // with the same value.
    const int32_t cell = brush.Cell();
    if (map.At(column, row) != cell) map.Set(column, row, cell);
    return true;
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
    ImGui::DockBuilderDockWindow("Console", bottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

namespace {

// The pointer, as the UI sees it.
//
// Taken from ImGui rather than the engine's Input layer for one reason: the UI
// is drawn into an ImGui draw list, in ImGui's coordinate space, and hit
// testing has to happen in the same space as the drawing or the click target
// drifts from what is on screen. `active` is where the two worlds meet - the
// editor hands it false whenever a panel, a menu or a drag owns the mouse.
UICanvas::UIPointer uiPointer(bool active) {
    const ImGuiIO& io = ImGui::GetIO();

    UICanvas::UIPointer pointer;
    pointer.position = glm::vec2(io.MousePos.x, io.MousePos.y);
    pointer.down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    // Down last frame, which is what separates a press from a hold. Derived
    // rather than remembered, so it cannot fall out of step with the frame.
    pointer.wasDown = pointer.down ? !ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                                   : ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    // A locked pointer is not anywhere.
    //
    // io.MousePos above comes from glfwGetCursorPos, and under GLFW_CURSOR_
    // DISABLED that reports unbounded VIRTUAL coordinates - ImGui's backend
    // passes them through without checking the mode. A HUD hit-tested against
    // them hovers and fires buttons at random under a cursor nobody can see,
    // and a shipped game hands this `true` unconditionally because nothing else
    // on screen competes for the pointer. There being no pointer at all is the
    // case that assumption missed.
    pointer.active = active && Input::EffectiveCursorMode() != CursorMode::Locked;
    return pointer;
}

// The keyboard for one frame, from two sources.
//
// Characters come from the ENGINE's snapshot, not from io.InputQueueCharacters,
// and not only for layering: ImWchar is 16 bits in this build, so ImGui's queue
// cannot carry a codepoint above the basic plane, while GLFW hands us UTF-32.
//
// The edit keys come from ImGui, because ImGui is the only thing here that
// knows the operating system's key repeat delay and rate - and holding
// Backspace has to erase more than one character. That split is why no key
// callback is needed.
UICanvas::UIKeyboard uiKeyboard(bool active) {
    const ImGuiIO& io = ImGui::GetIO();

    UICanvas::UIKeyboard keyboard;
    keyboard.characters = Input::TypedCharacters();
    keyboard.characterCount = Input::TypedCharacterCount();

    keyboard.backspace     = ImGui::IsKeyPressed(ImGuiKey_Backspace, true);
    keyboard.deleteForward = ImGui::IsKeyPressed(ImGuiKey_Delete, true);
    keyboard.caretLeft     = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
    keyboard.caretRight    = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);
    keyboard.caretHome     = ImGui::IsKeyPressed(ImGuiKey_Home, false);
    keyboard.caretEnd      = ImGui::IsKeyPressed(ImGuiKey_End, false);
    keyboard.submit        = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    keyboard.cancel        = ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    // An ImGui widget already has the keyboard. Both it and a HUD field are fed
    // by the same GLFW callback, so without this, renaming an entity in the
    // inspector would also type into whatever field the scene happens to have.
    keyboard.active = active && !io.WantTextInput;
    return keyboard;
}

} // namespace

void EditorLayer::buildGameView(entt::registry& registry) {
    // A shipped game has no dockable panels to remember, and writing an
    // imgui.ini into the player's game folder is editor litter.
    ImGui::GetIO().IniFilename = nullptr;

    // The scene already renders into the offscreen target and comes back tone
    // mapped; presenting it is one textured quad filling the window. Doing it
    // through ImGui rather than a dedicated blit keeps a single presentation
    // path - the same image, the same descriptor, drawn edge to edge instead of
    // inside a dockable panel.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##GameView", nullptr, flags);

    // A game owns the mouse and keyboard outright. In the editor these track
    // whether the pointer is over the viewport panel, because the panel is one
    // window among many; here there is nothing else to give input to.
    m_viewportHovered = true;
    m_viewportFocused = true;

    const ImVec2 size = viewport->Size;
    if (m_offscreenPass && size.x >= 1.0f && size.y >= 1.0f) {
        // The target is the window's size times the render scale (1 unless a game's DynamicResolution says otherwise): see
        // ScaledExtent. ImGui::Image below stretches it to the window.
        m_desiredViewportWidth = ScaledExtent(size.x, m_renderScale);
        m_desiredViewportHeight = ScaledExtent(size.y, m_renderScale);

        // The render target is the window now, not a panel inside it, so the
        // camera's aspect comes from the window.
        const float aspect = size.x / size.y;
        if (const auto camEntity = FindPrimaryCamera(registry); camEntity != entt::null) {
            registry.get<CameraComponent>(camEntity).aspect = aspect;
        }
        m_editorCamera.SetAspect(aspect);
        m_editorCamera.SetViewportHeight(size.y);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image(m_offscreenPass->GetTextureID(), size);

        // The HUD, over the game and nothing else. Nothing else is on screen
        // to take the pointer, so it is always the game's.
        // The camera the viewport is SHOWING, chosen exactly as SupersonicApp
        // chooses it - editor camera while editing, the scene's primary camera
        // otherwise. A world-space label placed through a different camera than
        // the one that drew the frame lands somewhere plausible and wrong.
        const CameraComponent* uiCamera = &m_editorCamera.Get();
        if (m_playMode && !m_playMode->IsEditing()) {
            if (const auto camEntity = FindPrimaryCamera(registry); camEntity != entt::null) {
                uiCamera = &registry.get<CameraComponent>(camEntity);
            }
        }
        const glm::mat4 uiViewProj =
            uiCamera->getProjectionMatrix() * uiCamera->getViewMatrix();

        const UIRect gameRect{ glm::vec2(origin.x, origin.y),
                               glm::vec2(origin.x + size.x, origin.y + size.y) };

        // Where the game is, for the game. Published every frame rather than
        // once, because the window can be resized and a layer holding a stale
        // rectangle would unproject a click to somewhere plausible and wrong.
        // Always the pointer's, here: in a packaged game there is nothing else
        // on screen for it to be over.
        registry.ctx().insert_or_assign<ViewportInfo>(ViewportInfo{ gameRect, true });

        UISystem::Render(registry, gameRect, uiPointer(true), uiKeyboard(true), uiViewProj);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void EditorLayer::BuildUI(entt::registry& registry, Window& window) {
    if (m_gameMode) {
        // Nothing below this line runs in a shipped game: no menu bar, no
        // dockspace, no panels, and none of the editor keyboard shortcuts,
        // which would otherwise let a player press Ctrl+Z during play.
        buildGameView(registry);
        return;
    }

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
                const auto result = m_sceneManager->Save(registry);
                SetStatus(result.message, !result.ok);
            }
            if (ImGui::MenuItem(ICON_FA_FLOPPY "  Save Scene As...")) {
                m_showSaveAs = true;
            }
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Reload Scene", "Ctrl+O")) {
                // Deferred: this runs inside BuildUI, while panels are iterating
                // views over the registry a load would clear and refill.
                m_sceneManager->RequestLoad(m_sceneManager->CurrentPath());
            }
            if (ImGui::MenuItem(ICON_FA_PLUS "  New Scene")) {
                m_sceneManager->RequestNew();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import Assets")) {
                // Mints an identity for anything under assets/ that has none,
                // and recovers the identity of anything that was renamed
                // outside the editor by matching it on contents. The only part
                // of asset identity that writes, which is why it is a menu item
                // rather than something that happens on load.
                const auto result = AssetDatabase::Instance().Import("assets");
                if (!result.ok) {
                    SetStatus("Import Assets: no 'assets' folder here.", true);
                } else {
                    // The scene that is already OPEN. Identity has survived a
                    // rename since asset identity landed, but only through the
                    // file: a component holds a path, the guid that would
                    // resolve it was spent at load time, so the running editor
                    // went on naming a file that is not there any more.
                    //
                    // Not undoable, on purpose. The old path does not exist -
                    // undoing this would restore a broken reference, which is
                    // not a state anybody wants back. It marks the scene dirty
                    // instead, because the change is real and has to be saved
                    // to survive.
                    const RepointResult repointed =
                        RepointAssets(registry, m_materialLibrary, result.moved);
                    if (repointed.Total() > 0 && m_sceneManager) m_sceneManager->MarkDirty();

                    std::string message = "Imported assets: " + std::to_string(result.minted) +
                                          " new, " + std::to_string(result.adopted) +
                                          " recovered from a rename";
                    if (repointed.Total() > 0) {
                        message += ", " + std::to_string(repointed.Total()) +
                                   " reference(s) in this scene re-pointed";
                    }
                    SetStatus(message + ". Save the scene to stamp the identities into it.",
                              false);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Package Standalone Game")) {
                const auto result = GamePackager::PackageStandaloneGame(
                    "dist/GameRelease", m_sceneManager->CurrentPath());
                SetStatus(result.message, !result.ok);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
#if SUPERSONIC_WINDOW_GLFW
                glfwSetWindowShouldClose(window.GetNativeWindow(), GLFW_TRUE);
#else
                // The editor never runs on a borrowed window (WindowBackend.hpp);
                // this keeps the file compiling there.
                (void)window;
#endif
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
        if (ImGui::BeginMenu("View")) {
            // The 2D switch, and the reason it is a viewport control rather
            // than a scene one: it changes how you are LOOKING at the level,
            // not what the level is. A game's own camera says whether it is 2D
            // in its CameraComponent, which the inspector can now author; this
            // is the editor's own eye, and authoring a 2D scene through a
            // perspective view is the thing that was impossible.
            bool ortho = m_editorCamera.IsOrthographic();
            if (ImGui::MenuItem("2D View (Orthographic)", nullptr, &ortho)) {
                m_editorCamera.SetOrthographic(ortho);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Right-drag pans, the wheel zooms, WASD pans. "
                                  "Flying forward is not offered because under a "
                                  "parallel projection it changes nothing you can see.");
            }
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

    // Hotkeys. Suppressed whenever anything is routing keys to a text box, so
    // typing "Rock" into one no longer switches gizmo modes or wipes the scene.
    //
    // Two flags because there are two kinds of text box. io.WantTextInput
    // covers ImGui's own - the inspector's name field, the content browser's
    // filter. TextCaptureActive covers the HUD's, which ImGui has never heard
    // of: without it, typing an R or an S into a field the GAME drew would
    // still re-bind the gizmo and save the scene.
    const bool editing = !m_playMode || m_playMode->IsEditing();

    if (!io.WantTextInput && !Input::TextCaptureActive() &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            const auto result = m_sceneManager->Save(registry);
            SetStatus(result.message, !result.ok);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            m_sceneManager->RequestLoad(m_sceneManager->CurrentPath());
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

    // Clicking a material in the browser assigns it to whatever is selected.
    // The browser reports the click and the editor owns the selection, so
    // neither has to know about the other.
    if (const std::string clicked = m_contentBrowserPanel.ConsumeMaterialClick(); !clicked.empty()) {
        if (m_materialLibrary && selectedEntity != entt::null && registry.valid(selectedEntity)) {
            if (MaterialSystem::Assign(registry, selectedEntity, *m_materialLibrary, clicked)) {
                SetStatus("Assigned " + clicked + ".");
            } else {
                SetStatus("Could not load " + clicked + ".", true);
            }
        } else {
            SetStatus("Select an entity first, then click a material to assign it.");
        }
    }

    // Save as Prefab, from the hierarchy row's context menu.
    if (const entt::entity toSave = m_hierarchyPanel.ConsumePrefabSaveRequest();
        toSave != entt::null && registry.valid(toSave)) {
        // Named after the tag, so a prefab is findable in the browser without
        // a save dialog the editor does not have. Non-filename characters are
        // stripped rather than rejected: a tag is free-form text.
        std::string name = "Prefab";
        if (const auto* tag = registry.try_get<TagComponent>(toSave); tag && !tag->tag.empty()) {
            name.clear();
            for (const char c : tag->tag) {
                const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
                name += safe ? c : '_';
            }
            if (name.empty()) name = "Prefab";
        }

        const std::string path = "assets/prefabs/" + name + ".prefab";
        const auto result = PrefabSerializer::SavePrefab(registry, toSave, path);
        SetStatus(result.message, !result.ok);
    }

    // Double-clicking a .prefab tile places it.
    if (const std::string scene = m_contentBrowserPanel.ConsumeSceneClick(); !scene.empty()) {
        m_sceneManager->RequestLoad(scene);
    }

    if (const std::string prefab = m_contentBrowserPanel.ConsumePrefabClick(); !prefab.empty()) {
        SerializationResult result;
        const entt::entity placed = PrefabSerializer::InstantiatePrefab(registry, prefab, &result);
        SetStatus(result.message, !result.ok);
        if (placed != entt::null) {
            // In front of the editor camera, not at the position the prefab
            // was saved from. A clone that lands exactly on top of its
            // original is invisible, and double-clicking a prefab then looks
            // like it did nothing at all.
            if (auto* transform = registry.try_get<TransformComponent>(placed)) {
                const CameraComponent& camera = m_editorCamera.Get();
                transform->position = camera.position + camera.front * 6.0f;
            }

            m_hierarchyPanel.SetSelectedEntity(placed);
            // Placing an entity is an edit like any other. Without this the
            // scene text changes and handleUndoRedo picks it up on the next
            // settled frame anyway - but recording it here means the step
            // exists before anything else can be done to the new entity.
            m_history.CommitIfChanged(registry);
        }
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
            m_editorCamera.SetViewportHeight(viewportPanelSize.y);
            if (const auto camEntity = FindPrimaryCamera(registry); camEntity != entt::null) {
                registry.get<CameraComponent>(camEntity).aspect = aspect;
            }
        }

        // Exact top-left of the image in screen space; used to convert mouse
        // position into viewport-local coordinates for picking and the gizmo.
        const ImVec2 viewportPos = ImGui::GetCursorScreenPos();

        ImGui::Image(m_offscreenPass->GetTextureID(), viewportPanelSize);

        // Read here, while the image is still the last item. The overlay
        // below is its own window, so a pointer over one of its buttons is
        // not over the image - which is what keeps a brush stroke from
        // landing on the cell under a tool button.
        const bool imageHovered = ImGui::IsItemHovered();

        // After the image, so it draws over it rather than under.
        drawViewportOverlay(viewportPos, viewportPanelSize);

        // The game's own UI, drawn against the viewport rather than the window,
        // so a HUD authored here lands in the same place when the game ships.
        // Live only while the viewport owns the mouse, and never while a gizmo
        // is being dragged: a menu button under the gizmo would otherwise
        // swallow the drag, and clicking through a panel that happens to
        // overlap the viewport would press a button the user cannot see.
        const bool uiOwnsPointer = m_viewportHovered && !ImGuizmo::IsUsing();

        // Same camera choice as the game-mode path above, and for the same
        // reason: a world-space label has to be projected through whatever
        // camera drew the frame it is being drawn over.
        const CameraComponent* uiCamera = &m_editorCamera.Get();
        if (m_playMode && !m_playMode->IsEditing()) {
            if (const auto camEntity = FindPrimaryCamera(registry); camEntity != entt::null) {
                uiCamera = &registry.get<CameraComponent>(camEntity);
            }
        }
        const glm::mat4 uiViewProj =
            uiCamera->getProjectionMatrix() * uiCamera->getViewMatrix();

        const UIRect gameRect{ glm::vec2(viewportPos.x, viewportPos.y),
                               glm::vec2(viewportPos.x + viewportPanelSize.x,
                                         viewportPos.y + viewportPanelSize.y) };

        // The same publication the game-mode path makes, and the reason it is
        // in both: a game running inside the editor has to see the PANEL, not
        // the window. The origin here is most of the way across the screen,
        // and a layer that assumed zero would unproject every click to a point
        // that is off by exactly the inspector's width.
        //
        // `uiOwnsPointer` rather than mere containment: a point inside the
        // viewport rectangle can still be under a floating tool window or a
        // gizmo being dragged, and a game acting on that click would be acting
        // on one the person meant for the editor.
        registry.ctx().insert_or_assign<ViewportInfo>(ViewportInfo{ gameRect, uiOwnsPointer });

        UISystem::Render(registry, gameRect,
                         uiPointer(uiOwnsPointer),
                         // FOCUS, not hover: moving the pointer off the viewport
                         // while typing must not lose half a name.
                         uiKeyboard(m_viewportFocused),
                         uiViewProj);

        // The tilemap brush takes the click when it is on. Dragging keeps
        // painting, one cell per frame the pointer is over a new one, and the
        // whole stroke is one undo step because handleUndoRedo waits for the
        // button to come up before it commits.
        const bool brushOwnsClick =
            paintTiles(registry, selectedEntity, imageHovered, viewportPos, viewportPanelSize);

        if (!brushOwnsClick && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver()) {
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

        // The gizmo-mode buttons moved into drawViewportOverlay, which draws
        // them as icons in a floating panel rather than as a row of labelled
        // buttons pinned to the image with a hardcoded cursor position.

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

    // io.DeltaTime, not 1000/io.Framerate. io.Framerate is ImGui's own mean
    // over the last 60 frames, so feeding it to the graph plotted a smoothed
    // series and then smoothed it again - a 100 ms stall arrived as a 1.7 ms
    // bump, under a comment promising that hitches stayed visible. The raw
    // per-frame delta is the only series that can contain one.
    const float frameTime = io.DeltaTime * 1000.0f;

    // storage<entt::entity>().size() counts released entities too, because EnTT
    // uses swap_only deletion. free_list() is the live count.
    const auto& entityStorage = registry.storage<entt::entity>();
    const auto entityCount = static_cast<uint32_t>(entityStorage.free_list());

    // Rolling history. The instantaneous number swings by several milliseconds
    // frame to frame, which reads as noise; the shape over two seconds is the
    // part that actually tells you something.
    m_frameTimes[m_frameTimeCursor] = frameTime;
    m_frameTimeCursor = (m_frameTimeCursor + 1) % kFrameHistory;
    m_smoothedFrameTime = m_smoothedFrameTime == 0.0f
                        ? frameTime
                        : m_smoothedFrameTime * 0.92f + frameTime * 0.08f;

    float worstFrame = 0.0f;
    for (const float sample : m_frameTimes) worstFrame = std::max(worstFrame, sample);

    ImGui::Text("Graphics API:    Vulkan 1.2 (VMA 3.1)");
    ImGui::Text("Frame Time:      %.2f ms", static_cast<double>(m_smoothedFrameTime));
    ImGui::Text("Framerate:       %.1f FPS", fps);

    // Scaled to the worst frame in the window rather than a fixed ceiling, so a
    // hitch is visible instead of being flattened against the top.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Brand::Bg0);
    ImGui::PlotLines("##frametime", m_frameTimes, kFrameHistory, m_frameTimeCursor,
                     nullptr, 0.0f, std::max(worstFrame * 1.15f, 4.0f),
                     ImVec2(-1.0f, 34.0f));
    ImGui::PopStyleColor();
    ImGui::Text("Active Entities: %u", entityCount);
    if (m_offscreenPass) {
        ImGui::Text("Viewport Res:    %ux%u", m_offscreenPass->GetWidth(), m_offscreenPass->GetHeight());
    }

    ImGui::Separator();
    Theme::SectionLabel(ICON_FA_CLOCK "  CPU FRAME");
    {
        // Measured, not apportioned: these are the phases Run() executes in
        // sequence, each bracketed where it is called. They do not sum to the
        // frame time - present, the swap and the driver's own work are outside
        // every zone - so the total is shown rather than implied.
        double measured = 0.0;
        for (std::size_t i = 0; i < Profiler::kZoneCount; ++i) {
            measured += Profiler::Milliseconds(static_cast<ProfileZone>(i));
        }

        for (std::size_t i = 0; i < Profiler::kZoneCount; ++i) {
            const auto zone = static_cast<ProfileZone>(i);
            const double ms = Profiler::Milliseconds(zone);

            // Zones that cost nothing this frame are dimmed rather than hidden:
            // a row that vanishes is a row nobody notices coming back.
            if (ms < 0.005) {
                ImGui::TextDisabled("%-16s   --", Profiler::Name(zone));
            } else {
                ImGui::Text("%-16s %5.2f ms", Profiler::Name(zone), ms);
            }
        }
        ImGui::Separator();
        ImGui::Text("%-16s %5.2f ms", "Measured", measured);
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
    // The skipped count belongs beside the other two, not instead of them.
    // Once depth passes started being skipped, a still scene showed
    // "Shadow: 0 drawn / 0 culled" - which is what a broken shadow pass looks
    // like, and there is no other signal that the pass ran at all.
    ImGui::Text("Shadow: %u drawn / %u culled / %u of 18 passes skipped",
                m_renderStats.shadowDrawn, m_renderStats.shadowCulled,
                m_renderStats.shadowPassesSkipped);

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

    // 7. Console
    //
    // The engine's diagnostics used to exist only on a terminal, which a
    // packaged game does not have and an editor user is not looking at. Every
    // message explaining why an entity will not move, why a surface is
    // untextured or why an asset failed to load went somewhere nobody reads.
    ImGui::Begin("Console");
    {
        ImGui::Checkbox("Info", &m_consoleShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_consoleShowWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &m_consoleShowErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Scroll", &m_consoleAutoScroll);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) Log::Clear();

        ImGui::SameLine();
        m_consoleFilter.Draw("##consolefilter", -1.0f);

        ImGui::Separator();

        const auto entries = Log::Snapshot();
        if (const std::size_t dropped = Log::DroppedCount(); dropped > 0) {
            // A console that has lost history says so, rather than quietly
            // starting mid-story and looking complete.
            ImGui::TextDisabled("%zu earlier message(s) dropped from the buffer.", dropped);
        }

        ImGui::BeginChild("##consolescroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& entry : entries) {
            const bool wanted =
                (entry.level == Log::Level::Error   && m_consoleShowErrors)   ||
                (entry.level == Log::Level::Warning && m_consoleShowWarnings) ||
                (entry.level <= Log::Level::Info    && m_consoleShowInfo);
            if (!wanted) continue;

            // Match against category and message together, so "Vulkan" finds
            // the subsystem and "texture" finds the thing that went wrong.
            const std::string line = "[" + entry.category + "] " + entry.message;
            if (!m_consoleFilter.PassFilter(line.c_str())) continue;

            ImVec4 colour = Brand::TextDim;
            if (entry.level == Log::Level::Error)        colour = ImVec4(1.0f, 0.45f, 0.40f, 1.0f);
            else if (entry.level == Log::Level::Warning) colour = Brand::Amber;

            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        // Only when already at the bottom, so scrolling back to read something
        // is not yanked away by the next frame's log line.
        if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();

    // Save As. A modal rather than a menu field so the path is committed by an
    // explicit press: typing into a live field would rename the open scene on
    // every keystroke.
    if (m_showSaveAs) {
        ImGui::OpenPopup("Save Scene As");
        m_showSaveAs = false;
    }
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char pathBuffer[512] = {};
        if (pathBuffer[0] == '\0') {
            const std::string& current = m_sceneManager->CurrentPath();
            const size_t n = std::min(current.size(), sizeof(pathBuffer) - 1);
            std::memcpy(pathBuffer, current.data(), n);
        }
        ImGui::TextDisabled("Relative to the project root.");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##saveaspath", pathBuffer, sizeof(pathBuffer));

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            const auto result = m_sceneManager->SaveAs(registry, pathBuffer);
            SetStatus(result.message, !result.ok);
            pathBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pathBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

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

    {
        SUPERSONIC_PROFILE(UndoCommit);
        if (++m_framesSinceUndoCheck >= kFramesBetweenUndoChecks) {
            m_framesSinceUndoCheck = 0;
            m_history.CommitIfChanged(registry);
        }
    }
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
