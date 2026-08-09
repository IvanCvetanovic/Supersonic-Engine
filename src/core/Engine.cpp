#include "core/Engine.hpp"
#include "core/Components.hpp"

#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

EngineApp::EngineApp() {
    std::cout << "[EngineApp] Initializing Engine Subsystems..." << std::endl;

    m_window = std::make_unique<Window>(1280, 720, "Vulkan EnTT 3D Game Engine");

    auto requiredExtensions = m_window->GetRequiredExtensions();
    m_vulkanContext = std::make_unique<VulkanContext>(requiredExtensions);

    m_vulkanDevice = std::make_unique<VulkanDevice>(m_vulkanContext->GetInstance(), *m_window);
    m_swapchain = std::make_unique<VulkanSwapchain>(*m_vulkanDevice, *m_window);
    m_renderer = std::make_unique<VulkanRenderer>(*m_vulkanDevice, *m_swapchain, *m_window);

    initECS();
}

EngineApp::~EngineApp() {
    std::cout << "[EngineApp] Shutting down Engine Subsystems in reverse order..." << std::endl;

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
    std::cout << "[EngineApp] Initializing EnTT 3D Entities & Components..." << std::endl;

    // Create Main Camera Entity
    auto cameraEntity = m_registry.create();
    auto& camera = m_registry.emplace<CameraComponent>(cameraEntity);
    camera.fov = 45.0f;
    camera.aspect = 1280.0f / 720.0f;
    camera.position = glm::vec3(0.0f, 1.5f, 3.5f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

    // Create Primary 3D Textured Cube Entity
    auto mainCube = m_registry.create();
    m_registry.emplace<TagComponent>(mainCube, "MainTexturedCube");
    auto& transform1 = m_registry.emplace<TransformComponent>(mainCube);
    transform1.position = glm::vec3(0.0f, 0.0f, 0.0f);
    m_registry.emplace<RenderableComponent>(mainCube, 0u, 0u, true);

    // Create Secondary Orbiting 3D Cube Entity
    auto satelliteCube = m_registry.create();
    m_registry.emplace<TagComponent>(satelliteCube, "SatelliteCube");
    auto& transform2 = m_registry.emplace<TransformComponent>(satelliteCube);
    transform2.position = glm::vec3(-1.8f, 0.5f, -0.5f);
    transform2.scale = glm::vec3(0.5f);
    m_registry.emplace<RenderableComponent>(satelliteCube, 0u, 0u, true);

    std::cout << "[EngineApp] Created Camera entity and 2 3D Cube entities." << std::endl;
}

void EngineApp::Run() {
    std::cout << "[EngineApp] Starting Main 3D Game Loop..." << std::endl;

    while (!m_window->ShouldClose()) {
        m_window->PollEvents();

        float time = static_cast<float>(glfwGetTime());

        // Update 3D entity rotations in ECS
        auto view = m_registry.view<TransformComponent, RenderableComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            transform.rotation.y = time * 1.2f;
            transform.rotation.x = time * 0.6f;
        }

        // Get view & proj matrices from CameraComponent
        glm::mat4 viewMatrix = glm::lookAt(glm::vec3(0.0f, 1.5f, 3.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projMatrix = glm::perspective(glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
        projMatrix[1][1] *= -1.0f; // Y-flip for Vulkan projection

        m_renderer->DrawFrame(m_registry, viewMatrix, projMatrix);
    }

    std::cout << "[EngineApp] Window close requested. Waiting for GPU idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

} // namespace Engine
