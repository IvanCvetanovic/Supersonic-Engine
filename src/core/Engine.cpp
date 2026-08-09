#include "core/Engine.hpp"
#include <iostream>

namespace Engine {

EngineApp::EngineApp() {
    std::cout << "[EngineApp] Initializing GLFW Window and Vulkan Context..." << std::endl;

    m_window = std::make_unique<Window>(1280, 720, "Vulkan EnTT Game Engine");

    auto requiredExtensions = m_window->GetRequiredExtensions();
    m_vulkanContext = std::make_unique<VulkanContext>(requiredExtensions);

    initECS();
}

EngineApp::~EngineApp() {
    std::cout << "[EngineApp] Cleaning up Engine resources..." << std::endl;
    m_registry.clear();
}

void EngineApp::initECS() {
    std::cout << "[EngineApp] Initializing EnTT ECS Registry..." << std::endl;

    // Task 3: Create a single dummy Entity with dummy Components
    entt::entity dummyEntity = m_registry.create();
    m_registry.emplace<TagComponent>(dummyEntity, "MainPlayerDummy");
    m_registry.emplace<PositionComponent>(dummyEntity, 1.0f, 2.0f, 3.0f);

    // Read back and log to verify EnTT integration
    auto view = m_registry.view<TagComponent, PositionComponent>();
    for (auto [entity, tag, pos] : view.each()) {
        std::cout << "[EnTT Verification Success] Entity handle: " << static_cast<uint32_t>(entity)
                  << " | Tag: '" << tag.name
                  << "' | Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")"
                  << std::endl;
    }
}

void EngineApp::Run() {
    std::cout << "[EngineApp] Starting Main Game Loop..." << std::endl;

    while (!m_window->ShouldClose()) {
        m_window->PollEvents();
    }

    std::cout << "[EngineApp] Window close requested. Exiting main loop." << std::endl;
}

} // namespace Engine
