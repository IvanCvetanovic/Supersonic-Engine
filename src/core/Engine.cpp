#include "core/Engine.hpp"
#include "core/Components.hpp"
#include "core/CameraSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/AudioSystem.hpp"
#include "core/ScriptEngine.hpp"
#include "core/ParticleSystem.hpp"
#include "core/TimeTravelDebugger.hpp"

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
    std::cout << "[EngineApp] Initializing EnTT 3D Entities, Components & Lighting..." << std::endl;

    // Create Interactive FPS Camera Entity
    auto cameraEntity = m_registry.create();
    auto& camera = m_registry.emplace<CameraComponent>(cameraEntity);
    camera.fov = 45.0f;
    camera.aspect = 1280.0f / 720.0f;
    camera.position = glm::vec3(0.0f, 1.2f, 4.0f);
    camera.yaw = -90.0f;
    camera.pitch = -10.0f;
    camera.updateCameraVectors();

    // Create Directional Light Entity
    auto lightEntity = m_registry.create();
    m_registry.emplace<TagComponent>(lightEntity, "DirectionalLight");
    auto& light = m_registry.emplace<LightComponent>(lightEntity);
    light.direction = glm::normalize(glm::vec3(0.6f, 1.0f, 0.5f));
    light.color = glm::vec3(1.0f, 0.95f, 0.85f);
    light.intensity = 1.2f;

    // Create Primary 3D Textured Cube Entity with Rotator Script
    auto mainCube = m_registry.create();
    m_registry.emplace<TagComponent>(mainCube, "MainTexturedCube");
    auto& transform1 = m_registry.emplace<TransformComponent>(mainCube);
    transform1.position = glm::vec3(0.0f, 0.0f, 0.0f);
    m_registry.emplace<RenderableComponent>(mainCube, 0u, 0u, true);
    m_registry.emplace<ScriptComponent>(mainCube, "RotatorScript");

    // Create Physics Falling Cube Entity
    auto physCube = m_registry.create();
    m_registry.emplace<TagComponent>(physCube, "Physics Cube");
    auto& transformPhys = m_registry.emplace<TransformComponent>(physCube, glm::vec3(1.5f, 4.0f, 0.0f));
    transformPhys.scale = glm::vec3(0.7f);
    m_registry.emplace<RigidBodyComponent>(physCube);
    m_registry.emplace<BoxColliderComponent>(physCube);
    m_registry.emplace<RenderableComponent>(physCube, 0u, 0u, true);

    // Create Particle Emitter Entity
    auto particleEntity = m_registry.create();
    m_registry.emplace<TagComponent>(particleEntity, "Particle Emitter");
    m_registry.emplace<TransformComponent>(particleEntity, glm::vec3(-1.5f, 0.0f, 0.0f));
    m_registry.emplace<ParticleEmitterComponent>(particleEntity);

    std::cout << "[EngineApp] Created Camera, Light, Rotator Cube, Physics Cube, and Particle Emitter." << std::endl;
}

void EngineApp::Run() {
    std::cout << "[EngineApp] Starting Main 3D Game Loop..." << std::endl;

    double lastTime = glfwGetTime();

    while (!m_window->ShouldClose()) {
        m_window->PollEvents();

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        // Process WASD movement & Right-Click Mouse Look for Camera
        CameraSystem::Update(m_registry, *m_window, deltaTime);

        // Update Subsystems: Physics, Audio, Scripting, and Particles (if not rewinding)
        if (!TimeTravelDebugger::IsRewinding()) {
            PhysicsSystem::Update(m_registry, deltaTime);
            AudioSystem::Update(m_registry, deltaTime);
            ScriptEngine::Update(m_registry, deltaTime);
            ParticleSystem::Update(m_registry, deltaTime);
            TimeTravelDebugger::RecordFrame(m_registry, static_cast<float>(currentTime));
        }

        // Get view & proj matrices from active CameraComponent
        glm::mat4 viewMatrix(1.0f);
        glm::mat4 projMatrix(1.0f);

        auto cameraView = m_registry.view<CameraComponent>();
        for (auto camEntity : cameraView) {
            auto& cam = cameraView.get<CameraComponent>(camEntity);
            cam.aspect = static_cast<float>(m_window->GetWidth()) / static_cast<float>(m_window->GetHeight() > 0 ? m_window->GetHeight() : 1);
            viewMatrix = cam.getViewMatrix();
            projMatrix = cam.getProjectionMatrix();
            break;
        }

        m_renderer->DrawFrame(m_registry, viewMatrix, projMatrix);
    }

    std::cout << "[EngineApp] Window close requested. Waiting for GPU idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

} // namespace Engine
