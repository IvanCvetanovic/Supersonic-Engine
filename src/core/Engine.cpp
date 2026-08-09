#include "core/Engine.hpp"
#include <iostream>

namespace Engine {

EngineApp::EngineApp() {
    std::cout << "[EngineApp] Initializing Engine Subsystems..." << std::endl;

    m_window = std::make_unique<Window>(1280, 720, "Vulkan EnTT Game Engine");

    auto requiredExtensions = m_window->GetRequiredExtensions();
    m_vulkanContext = std::make_unique<VulkanContext>(requiredExtensions);

    m_vulkanDevice = std::make_unique<VulkanDevice>(m_vulkanContext->GetInstance(), *m_window);
    m_swapchain = std::make_unique<VulkanSwapchain>(*m_vulkanDevice, *m_window);
    m_renderer = std::make_unique<VulkanRenderer>(*m_vulkanDevice, *m_swapchain, *m_window);

    initECS();
}

EngineApp::~EngineApp() {
    std::cout << "[EngineApp] Shutting down Engine Subsystems in reverse order..." << std::endl;

    // CRITICAL: Wait for GPU device idle before destroying rendering resources
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }

    m_registry.clear();
    m_renderer.reset();
    m_swapchain.reset();
    m_vulkanDevice.reset();
    m_vulkanContext.reset();
    m_window.reset();
}

void EngineApp::initECS() {
    std::cout << "[EngineApp] Initializing EnTT ECS Registry..." << std::endl;

    entt::entity dummyEntity = m_registry.create();
    m_registry.emplace<TagComponent>(dummyEntity, "MainPlayerDummy");
    m_registry.emplace<PositionComponent>(dummyEntity, 1.0f, 2.0f, 3.0f);

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
        m_renderer->DrawFrame();
    }

    std::cout << "[EngineApp] Window close requested. Waiting for device idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

} // namespace Engine
