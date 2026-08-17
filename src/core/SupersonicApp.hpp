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
#include "core/PlayMode.hpp"
#include "core/Components.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/AnimationLibrary.hpp"
#include "core/MaterialLibrary.hpp"

namespace Supersonic {

class SupersonicApp {
public:
    SupersonicApp();
    ~SupersonicApp();

    SupersonicApp(const SupersonicApp&) = delete;
    SupersonicApp& operator=(const SupersonicApp&) = delete;

    void Run();

private:
    void initECS();

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

    entt::registry m_registry;

    // Physics runs on a fixed step fed by this accumulator, so a stalled frame
    // cannot integrate a two-second delta in one go.
    float m_physicsAccumulator{0.0f};

    // Reused between frames so the fixed-step loop does not allocate per step.
    std::vector<PhysicsSystem::Contact> m_stepContacts;
    std::vector<PhysicsSystem::Contact> m_contacts;
};

} // namespace Supersonic
