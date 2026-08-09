#pragma once

#include <memory>
#include <string>

#include <entt/entt.hpp>

#include "platform/Window.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanSwapchain.hpp"
#include "renderer/VulkanRenderer.hpp"

namespace Engine {

struct PositionComponent {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct TagComponent {
    std::string name;
};

class EngineApp {
public:
    EngineApp();
    ~EngineApp();

    EngineApp(const EngineApp&) = delete;
    EngineApp& operator=(const EngineApp&) = delete;

    void Run();

private:
    void initECS();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkanContext;
    std::unique_ptr<VulkanDevice> m_vulkanDevice;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<VulkanRenderer> m_renderer;
    entt::registry m_registry;
};

} // namespace Engine
