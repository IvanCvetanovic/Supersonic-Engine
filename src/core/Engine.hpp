#pragma once

#include <memory>
#include <string>

#include <entt/entt.hpp>

#include "platform/Window.hpp"
#include "renderer/VulkanContext.hpp"

namespace Engine {

// Sample EnTT components proving DOD ECS works
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
    entt::registry m_registry;
};

} // namespace Engine
