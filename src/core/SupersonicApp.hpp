#pragma once

#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "platform/Window.hpp"
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
#include "core/LayerStack.hpp"
#include "core/AnimationLibrary.hpp"
#include "core/MaterialLibrary.hpp"
#include "core/LaunchOptions.hpp"
#include "core/SceneManager.hpp"

namespace Supersonic {

class SupersonicApp {
public:
    explicit SupersonicApp(const LaunchOptions& options = {});
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

    // A game's own systems. Empty in the editor, which is why nothing else in
    // this file changes shape when there is no game.
    LayerStack m_layers;

    // Physics runs on a fixed step fed by this accumulator, so a stalled frame
    // cannot integrate a two-second delta in one go.
    float m_physicsAccumulator{0.0f};

    // Reused between frames so the fixed-step loop does not allocate per step.
    std::vector<PhysicsSystem::Contact> m_stepContacts;
    std::vector<PhysicsSystem::Contact> m_contacts;
};

} // namespace Supersonic
