#pragma once

#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/WorldShapes.hpp"
#include "core/ScreenOverlay.hpp"

#include "platform/Window.hpp"
#include "platform/NativeWindowControl.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanSwapchain.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "editor/EditorLayer.hpp"
#include "core/AudioEngine.hpp"
#include "core/HotReloadEngine.hpp"
#include "core/AssetWatcher.hpp"
#include "core/PlayMode.hpp"
#include "core/GameRuntime.hpp"
#include "core/Components.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/ContactTracker.hpp"
#include "core/InputRecording.hpp"
#include "core/LayerStack.hpp"
#include "core/AnimationLibrary.hpp"
#include "core/MaterialLibrary.hpp"
#include "core/LaunchOptions.hpp"
#include "core/SceneManager.hpp"

namespace Supersonic {

class SupersonicApp {
public:
    // `manifest` lets a game DECLARE ITSELF rather than be discovered.
    //
    // Without it the only way to be a game was to have a `game.manifest` beside
    // the executable, which is what the packager writes - so a game linking the
    // engine and building its own world in a layer was, on its own developer's
    // machine, the editor: it got the demo scene it did not ask for, sat in edit
    // mode, and never ran a tick. Its layer's OnFixedUpdate was never called,
    // which is a game that does not start.
    //
    // Passing one skips the file lookup entirely. Null keeps the old behaviour
    // and is what the engine's own main() does.
    explicit SupersonicApp(const LaunchOptions& options = {},
                           const GameManifest* manifest = nullptr);
    ~SupersonicApp();

    SupersonicApp(const SupersonicApp&) = delete;
    SupersonicApp& operator=(const SupersonicApp&) = delete;

    void Run();

    // ---- The seam a game lives in ----
    //
    // The registry was private and the frame was a closed sequence, so there
    // was nowhere for anyone else's code to be. These are the whole of the
    // opening: somewhere to put systems, and access to the world they operate
    // on.
    //
    // Push before Run. A layer pushed after it starts is attached correctly,
    // but the frame it lands in has already begun.
    void PushLayer(std::unique_ptr<EngineLayer> layer);

    // The scene. Handed out deliberately: an ECS whose registry is private is
    // an ECS only its author can use, and every alternative - a wrapper
    // re-exporting a chosen subset, a message queue, a component registration
    // API - is a smaller EnTT that a game has to learn instead of the one it
    // already knows.
    entt::registry& Registry() { return m_registry; }

    // How many layers are attached, for the editor and for tests.
    std::size_t LayerCount() const { return m_layers.Size(); }

private:
    void initECS();

    void applyPendingSceneLoad();

    // Reads the window's events during the constructor and while a layer is
    // attached, when the game asked (GameManifest::pumpEventsDuringStartup).
    // A no-op otherwise, and on a platform whose window is not GLFW's.
    void pumpStartupEvents();

    // Before the swapchain is made: waits out a minimised window and clears the
    // resized flag the pumps may have raised. A no-op unless the game opted in.
    void settleWindowForSwapchain();

    // Open the file named by --record or --replay, once the scene is loaded.
    void setUpRecording(const std::string& startupScene);

    // Record or feed one tick's input. False means a replay has run out, which
    // is what stops the loop rather than letting it feed the run nothing and
    // call the result a reproduction.
    bool stepRecording(uint64_t tick);

    // Write the recording out, or report whether the replay reproduced.
    void finishRecording();

    // Read the frame just presented back and write it as a PNG. One function
    // for --screenshot and --screenshot-every: on the last frame both read the
    // same presented image, through this function, in the same loop iteration,
    // so the two files cannot differ by a byte.
    void writeScreenshot(const std::string& path);

    // The same for --screenshot-ui, from the swapchain copy the renderer made
    // of that frame (VulkanRenderer::ReadSwapchainCapture). Not consuming, so
    // the last stamped frame and the final file are one copy, twice.
    void writeUiScreenshot(const std::string& path);

public:
    // Whether a --replay run disagreed with the hashes it was checked against.
    //
    // Read by main() to choose the exit status, because a verification that
    // reports failure only in its own log is one no CI job can act on - and
    // being runnable without a person watching is the whole point of a replay.
    bool ReplayDiverged() const;

    // How many PNGs --screenshot, --screenshot-ui and --screenshot-every
    // actually wrote. Read by main() against LaunchOptions::CapturesOwed,
    // counted on success rather than on failure so that no failure path,
    // present or future, can be missed by the count.
    long long CapturesWritten() const;

private:

    ContactTracker m_contactTracker;
    AssetWatcher m_assetWatcher;
    LaunchOptions m_options;

    // Which scene is open. Owned here rather than by the editor, and published
    // into the registry context as a pointer, because a scene switch is a GAME
    // operation: a menu opening a level, a level loading the next one, a death
    // restarting the current one. While the editor owned it, a packaged game
    // was exactly one scene for the whole of its life - the manifest named a
    // startup scene and nothing could ever ask for a different one.
    SceneManager m_sceneManager;

    std::unique_ptr<Window> m_window;

    // What a game may ask of the window - fullscreen, a size, the modes its
    // monitor offers - published into the registry context as WindowControl*.
    // Its requests are applied at the top of the frame, before events are
    // polled; see NativeWindowControl::ApplyPending.
    std::unique_ptr<NativeWindowControl> m_windowControl;

    std::unique_ptr<VulkanContext> m_vulkanContext;
    std::unique_ptr<VulkanDevice> m_vulkanDevice;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<VulkanRenderer> m_renderer;

    // The editor owns the offscreen viewport target and is driven by Run(),
    // not by the renderer. See EditorLayer for why that ordering matters.
    std::unique_ptr<EditorLayer> m_editorLayer;

    // Real output device. Degrades to a documented no-op where no backend is
    // compiled, instead of silently discarding every computed volume.
    std::unique_ptr<AudioEngine> m_audioEngine;
    std::unique_ptr<AnimationLibrary> m_animationLibrary;
    std::unique_ptr<MaterialLibrary> m_materialLibrary;

    // Watches the script plugin and swaps it in when it is rebuilt.
    std::unique_ptr<HotReloadEngine> m_hotReload;

    // Edit / Play / Paused, with a scene snapshot restored on Stop.
    PlayMode m_playMode;

    // Empty (isGame false) in the editor, which is the usual case.
    GameManifest m_manifest;

    entt::registry m_registry;

    // Immediate-mode world-space shapes, owned here rather than in the
    // registry: the registry is CLEARED by a scene load, and a buffer a game
    // layer holds a pointer to must not be one of the things that goes.
    WorldShapes m_worldShapes;

    // The screen overlay, owned here for the same reason: a game's HUD is
    // emitted from a layer, into a buffer that must survive a scene load.
    ScreenOverlay m_screenOverlay;

    // A game's own systems. Empty in the editor, which is why nothing else in
    // this file changes shape when there is no game.
    LayerStack m_layers;

    // Escape was pressed while a game held the pointer, so the editor has it
    // back until the viewport is clicked again. Latched rather than momentary:
    // a game asks for the lock every frame, so a one-frame release would be
    // swallowed before anyone could move the mouse. Editor only.
    bool m_escapeReleasedCursor{false};

    // Whether a --hidden run has said once that it dropped a game's window
    // request. Once, because a game re-applying its settings every frame
    // would otherwise write the same line sixty times a second.
    bool m_droppedHiddenWindowRequest{false};

    // Run has begun: the frame loop reads the window's events itself, and the
    // startup pumps stop.
    bool m_running{false};

    // Physics runs on a fixed step fed by this accumulator, so a stalled frame
    // cannot integrate a two-second delta in one go.
    float m_physicsAccumulator{0.0f};

    // Reused between frames so the fixed-step loop does not allocate per step.
    std::vector<PhysicsSystem::Contact> m_stepContacts;

    // THIS TICK's contacts, which is what the contact tracker is fed and
    // therefore what a script sees when it asks what it touched. Cleared at the
    // top of every tick.
    std::vector<PhysicsSystem::Contact> m_contacts;

    // The run being written down, or the one being played back. At most one is
    // ever set - LaunchOptions refuses both, because a run recording the input
    // it is being fed writes a file that agrees with itself by construction.
    std::unique_ptr<InputRecording> m_recording;
    std::unique_ptr<InputRecording> m_replay;

    // The first checkpoint a replay disagreed with, and whether there was one.
    //
    // Kept rather than acted on immediately: the run carries on to the end so
    // the log shows how far the divergence spread, and the exit status is
    // decided once at the bottom. Reporting only the FIRST is the useful part -
    // after a divergence every later checkpoint disagrees too, and the tick
    // that matters is the one where the two runs stopped being the same.
    bool m_replayDiverged{false};
    uint64_t m_replayDivergedAtTick{0};

    // writeScreenshot and writeUiScreenshot, each file that reached the disk.
    long long m_capturesWritten{0};
    uint64_t m_replayExpectedHash{0};
    uint64_t m_replayActualHash{0};

    // THIS FRAME's, for the editor's count. Two vectors because the two
    // consumers want different things: a tracker turning contacts into
    // Enter/Stay/Exit has to be handed exactly one tick's worth or the phases
    // depend on how many ticks the frame ran, while a panel showing a number to
    // a person wants the frame, or it flickers whenever the frame runs more
    // than one. One vector served both, and served the tracker wrongly.
    std::vector<PhysicsSystem::Contact> m_frameContacts;
};

} // namespace Supersonic
