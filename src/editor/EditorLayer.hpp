#pragma once

#include <memory>
#include <string>

#include "imgui.h"

#include "core/PlayMode.hpp"
#include "core/SceneManager.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanOffscreen.hpp"
#include "editor/SceneHierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/ContentBrowserPanel.hpp"
#include "editor/ThumbnailCache.hpp"
#include "editor/EditorCamera.hpp"
#include "editor/EditHistory.hpp"
#include "core/RenderSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/MaterialLibrary.hpp"
#include "platform/Window.hpp"

namespace Supersonic {

// Owns the editor UI and the offscreen target the scene renders into.
//
// This used to be a by-value member of VulkanRenderer, with OnImGuiRender
// called from inside DrawFrame. That inversion is what allowed the editor to
// destroy GPU resources the renderer had already recorded into a live command
// buffer. SupersonicApp now owns the editor and drives it before the renderer.
class EditorLayer {
public:
    EditorLayer() = default;
    ~EditorLayer() = default;

    EditorLayer(const EditorLayer&) = delete;
    EditorLayer& operator=(const EditorLayer&) = delete;

    // Must be called after the ImGui Vulkan backend is initialised, because the
    // offscreen target registers a descriptor set with it.
    void Init(VulkanDevice& device, uint32_t initialWidth, uint32_t initialHeight);

    // Must be called before ImGui_ImplVulkan_Shutdown.
    void Shutdown();

    // Builds the whole editor UI for this frame. Mutates ECS state and records
    // the desired viewport size, but never touches GPU resource lifetimes.
    void BuildUI(entt::registry& registry, Window& window);

    // A packaged game runs the same binary as the editor, so the difference is
    // this flag rather than a separate build. When set, BuildUI draws the game
    // filling the window and nothing else: no menu bar, no dockspace, no
    // panels, no editor shortcuts.
    void SetGameMode(bool gameMode) { m_gameMode = gameMode; }

    // Applies any viewport resize requested during BuildUI. Call at the top of
    // the frame, before the renderer starts recording.
    void ApplyPendingResize();

    // Performed by SupersonicApp between frames, NOT from inside BuildUI: a
    // load clears and refills the registry, and BuildUI is running while
    // panels iterate views over it.
    void ApplyPendingSceneLoad(entt::registry& registry);
    SceneManager& GetSceneManager() { return m_sceneManager; }

    VulkanOffscreen& GetOffscreen() { return *m_offscreenPass; }
    SceneHierarchyPanel& GetHierarchyPanel() { return m_hierarchyPanel; }
    InspectorPanel& GetInspectorPanel() { return m_inspectorPanel; }

    // Transient status line shown in the editor, so failures stop being
    // console-only messages a GUI user never sees.
    void SetStatus(const std::string& message, bool isError = false);

    // Hot-reload state, shown in the statistics panel.
    void SetScriptHostInfo(bool pluginLoaded, const std::string& status, uint32_t reloadCount);

    // Drives the Play/Pause/Stop toolbar. Non-owning.
    void SetPlayMode(PlayMode* playMode) { m_playMode = playMode; }

    // The viewport's own fly camera, used in edit mode so flying around does
    // not move the scene's game camera.
    EditorCamera& GetEditorCamera() { return m_editorCamera; }

    // Whether the viewport panel currently owns the pointer / the keyboard.
    // The camera used to be gated on ImGui's WantCaptureMouse, which is true
    // for the whole viewport window - so right-drag look never fired at all.
    bool IsViewportHovered() const { return m_viewportHovered; }
    bool IsViewportFocused() const { return m_viewportFocused; }

    // Shared material assets, for the inspector's editor and the browser's
    // create/assign actions. Non-owning.
    void SetMaterialLibrary(MaterialLibrary* library);

    // Last frame's culling counters, shown in the statistics panel.
    void SetRenderStats(const RenderSystem::Stats& stats) { m_renderStats = stats; }

    // This frame's collision contacts. Counted in the statistics panel, because
    // collision that resolves correctly is otherwise indistinguishable from
    // collision that never ran.
    void SetContacts(const std::vector<PhysicsSystem::Contact>& contacts);

private:
    void drawStatusBar();

    // Floating chrome over the viewport image: gizmo mode on the left, live
    // counters on the right. Overlaid rather than docked, because a 3D viewport
    // is the one panel whose whole value is its area.
    void drawViewportOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize);

    // Ctrl+Z / Ctrl+Y and the Edit menu, plus the once-per-frame commit that
    // turns "the scene changed" into an undo step.
    void handleUndoRedo(entt::registry& registry);

    // An undo swaps the whole registry, so every entity handle the panels are
    // holding is stale.
    void afterHistoryJump(entt::registry& registry, const std::string& what);

    // Must be called right after DockSpace(), before any panel is submitted.
    void buildLayout(unsigned int dockspaceId, int preset);

    std::unique_ptr<VulkanOffscreen> m_offscreenPass;
    std::unique_ptr<ThumbnailCache> m_thumbnails;
    SceneHierarchyPanel m_hierarchyPanel;
    InspectorPanel m_inspectorPanel;
    ContentBrowserPanel m_contentBrowserPanel;

    bool m_gameMode{false};
    bool m_showDemoWindow{false};

    std::string m_statusMessage;
    bool m_statusIsError{false};
    float m_statusAge{0.0f};

    // Set by BuildUI, consumed by ApplyPendingResize.
    uint32_t m_desiredViewportWidth{0};
    uint32_t m_desiredViewportHeight{0};

    bool m_pluginLoaded{false};
    std::string m_scriptHostStatus;
    uint32_t m_scriptReloadCount{0};

    // The camera the viewport is currently looking through: the editor's in
    // edit mode, the scene's primary in play mode. Picking, the gizmo and the
    // renderer all go through this so they cannot disagree.
    const CameraComponent& viewportCamera(entt::registry& registry) const;

    // The whole of the UI in game mode: the rendered scene, edge to edge.
    void buildGameView(entt::registry& registry);

    PlayMode* m_playMode{nullptr};
    EditorCamera m_editorCamera;
    EditHistory m_history;
    MaterialLibrary* m_materialLibrary{nullptr};
    bool m_viewportHovered{false};
    bool m_viewportFocused{false};
    RenderSystem::Stats m_renderStats{};

    // Rolling frame-time history for the statistics graph. A single number
    // jitters too much to read; the shape of the last two seconds does not.
    // Console panel state. Info is on by default and warnings and errors are
    // never off by default: a filter that hides errors until someone turns them
    // on is a filter that hides errors.
    SceneManager m_sceneManager;
    bool m_showSaveAs{false};

    bool m_consoleShowInfo{true};
    bool m_consoleShowWarnings{true};
    bool m_consoleShowErrors{true};
    bool m_consoleAutoScroll{true};
    ImGuiTextFilter m_consoleFilter;

    static constexpr int kFrameHistory = 120;
    float m_frameTimes[kFrameHistory]{};
    int m_frameTimeCursor{0};
    float m_smoothedFrameTime{0.0f};
    uint32_t m_contactCount{0};
    uint32_t m_triggerCount{0};

    // Ensures the built-in layout is applied once on a fresh install rather
    // than fighting a user's saved arrangement every frame.
    bool m_defaultLayoutApplied{false};

    // Preset requested from the menu, applied at the top of the next frame.
    int m_pendingLayoutPreset{-1};
};

} // namespace Supersonic
