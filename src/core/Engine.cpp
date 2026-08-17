#include "core/Engine.hpp"
#include "core/Components.hpp"
#include "core/CameraSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/AudioSystem.hpp"
#include "core/ScriptEngine.hpp"
#include "core/ParticleSystem.hpp"
#include "core/RenderSystem.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "core/EcsUtils.hpp"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

namespace {
// A frame longer than this is treated as a hitch rather than elapsed time.
// Dragging the title bar on Win32 blocks glfwPollEvents inside the modal
// move/size loop, which used to hand physics a two-second delta.
constexpr float kMaxFrameDelta = 0.10f;

constexpr float kFixedPhysicsStep = 1.0f / 60.0f;
constexpr int kMaxPhysicsStepsPerFrame = 5;

// The script plugin lives next to the executable, so this works both from a
// build tree and from a packaged folder.
std::string scriptPluginPath() {
#if defined(_WIN32)
    constexpr const char* name = "GameScripts.dll";
#elif defined(__APPLE__)
    constexpr const char* name = "GameScripts.dylib";
#else
    constexpr const char* name = "GameScripts.so";
#endif
    std::error_code ec;
    const auto exeDir = std::filesystem::current_path(ec);
    (void)exeDir;
    // Relative to the working directory first (repo root during development),
    // then next to the binary.
    if (std::filesystem::exists(std::string("build/Debug/") + name, ec)) {
        return std::string("build/Debug/") + name;
    }
    if (std::filesystem::exists(std::string("build/Release/") + name, ec)) {
        return std::string("build/Release/") + name;
    }
    return name;
}
} // namespace

EngineApp::EngineApp() {
    std::cout << "[EngineApp] Initializing Engine Subsystems..." << std::endl;

    // Asset writes target these; create them before anything tries to save.
    std::error_code ec;
    std::filesystem::create_directories("assets/scenes", ec);
    if (ec) {
        std::cerr << "[EngineApp] Could not create assets/scenes: " << ec.message() << std::endl;
    }

    m_audioEngine = std::make_unique<AudioEngine>();
    // Stops voices when their entity goes away; sources loop by default.
    AudioSystem::Attach(m_registry, *m_audioEngine);

    // Scripts: built-ins first, then whatever the plugin adds on top.
    ScriptEngine::RegisterBuiltInScripts();
    m_hotReload = std::make_unique<HotReloadEngine>();
    m_hotReload->WatchPlugin(scriptPluginPath());

    m_window = std::make_unique<Window>(1280, 720, "Vulkan EnTT 3D Game Engine");

    auto requiredExtensions = m_window->GetRequiredExtensions();
    m_vulkanContext = std::make_unique<VulkanContext>(requiredExtensions);

    m_vulkanDevice = std::make_unique<VulkanDevice>(m_vulkanContext->GetInstance(), *m_window);
    m_swapchain = std::make_unique<VulkanSwapchain>(*m_vulkanDevice, *m_window);
    m_renderer = std::make_unique<VulkanRenderer>(*m_vulkanDevice, *m_swapchain, *m_window);

    // The editor's offscreen target registers a texture with the ImGui Vulkan
    // backend, so it must be created after the renderer has initialised it.
    m_editorLayer = std::make_unique<EditorLayer>();
    m_editorLayer->Init(*m_vulkanDevice,
                        m_swapchain->GetExtent().width,
                        m_swapchain->GetExtent().height);

    // The scene pipeline targets the editor's offscreen render pass.
    m_renderer->SetOffscreenRenderPass(m_editorLayer->GetOffscreen().GetRenderPass());

    initECS();
}

EngineApp::~EngineApp() {
    std::cout << "[EngineApp] Shutting down Engine Subsystems in reverse order..." << std::endl;

    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }

    // Clearing fires the destruction hook, which stops any voice still playing.
    m_registry.clear();
    AudioSystem::Detach(m_registry);

    // Order matters: the editor frees an ImGui descriptor set, which must
    // happen before ImGui_ImplVulkan_Shutdown runs in ~VulkanRenderer.
    if (m_editorLayer) {
        m_editorLayer->Shutdown();
        m_editorLayer.reset();
    }

    m_renderer.reset();
    m_swapchain.reset();
    m_vulkanDevice.reset();
    m_vulkanContext.reset();
    m_window.reset();
    m_audioEngine.reset();
}

void EngineApp::initECS() {
    std::cout << "[EngineApp] Initializing EnTT 3D Entities, Components & Lighting..." << std::endl;

    // Tagged, so it does not show up in the hierarchy as an anonymous
    // "Entity 0" that can be deleted without realising it is the camera.
    auto cameraEntity = m_registry.create();
    m_registry.emplace<TagComponent>(cameraEntity, "Main Camera");
    m_registry.emplace<TransformComponent>(cameraEntity, glm::vec3(0.0f, 1.2f, 4.0f));
    auto& camera = m_registry.emplace<CameraComponent>(cameraEntity);
    camera.fov = 45.0f;
    camera.aspect = 1280.0f / 720.0f;
    camera.position = glm::vec3(0.0f, 1.2f, 4.0f);
    camera.yaw = -90.0f;
    camera.pitch = -10.0f;
    camera.updateCameraVectors();

    auto lightEntity = m_registry.create();
    m_registry.emplace<TagComponent>(lightEntity, "Sun (Directional)");
    m_registry.emplace<TransformComponent>(lightEntity);
    auto& light = m_registry.emplace<LightComponent>(lightEntity);
    light.type = static_cast<int>(LightType::Directional);
    light.direction = glm::normalize(glm::vec3(0.55f, 1.0f, 0.42f));
    light.color = glm::vec3(1.0f, 0.96f, 0.88f);
    light.intensity = 2.6f;
    light.ambient = glm::vec3(0.10f, 0.11f, 0.14f);
    light.castsShadow = true;

    // Two point lights, to exercise the multi-light path the single hardcoded
    // direction in the old shader could not express.
    auto pointA = m_registry.create();
    m_registry.emplace<TagComponent>(pointA, "Point Light (Warm)");
    m_registry.emplace<TransformComponent>(pointA, glm::vec3(-3.2f, 1.6f, 2.0f));
    auto& lightA = m_registry.emplace<LightComponent>(pointA);
    lightA.type = static_cast<int>(LightType::Point);
    lightA.color = glm::vec3(1.0f, 0.55f, 0.25f);
    lightA.intensity = 7.0f;
    lightA.range = 14.0f;

    auto pointB = m_registry.create();
    m_registry.emplace<TagComponent>(pointB, "Point Light (Cool)");
    m_registry.emplace<TransformComponent>(pointB, glm::vec3(3.4f, 1.4f, 2.2f));
    auto& lightB = m_registry.emplace<LightComponent>(pointB);
    lightB.type = static_cast<int>(LightType::Point);
    lightB.color = glm::vec3(0.30f, 0.55f, 1.0f);
    lightB.intensity = 7.0f;
    lightB.range = 14.0f;

    // Ground plane. Without a receiver there is nothing for the shadow map to
    // fall on - the grid is a shader overlay, not geometry.
    auto ground = m_registry.create();
    m_registry.emplace<TagComponent>(ground, "Ground");
    auto& groundTransform = m_registry.emplace<TransformComponent>(ground, glm::vec3(0.0f, 0.0f, 0.0f));
    groundTransform.scale = glm::vec3(40.0f, 1.0f, 40.0f);
    m_registry.emplace<MeshComponent>(ground, "Plane", "", 0u, 0u);
    auto& groundMat = m_registry.emplace<MaterialComponent>(ground);
    groundMat.albedoTexturePath = "assets/textures/floor_tiles.png";
    groundMat.roughness = 0.92f;
    groundMat.metallic = 0.0f;
    auto& groundRenderable = m_registry.emplace<RenderableComponent>(ground);
    groundRenderable.castsShadow = false; // a flat receiver casting onto itself only adds acne

    auto mainCube = m_registry.create();
    m_registry.emplace<TagComponent>(mainCube, "Textured Cube");
    m_registry.emplace<TransformComponent>(mainCube, glm::vec3(0.0f, 0.9f, 0.0f));
    m_registry.emplace<MeshComponent>(mainCube, "Cube", "", 24u, 36u);
    auto& cubeMat = m_registry.emplace<MaterialComponent>(mainCube);
    cubeMat.albedoTexturePath = "assets/textures/uv_grid.png";
    cubeMat.roughness = 0.45f;
    m_registry.emplace<RenderableComponent>(mainCube);
    m_registry.emplace<ScriptComponent>(mainCube, "RotatorScript");

    auto sphere = m_registry.create();
    m_registry.emplace<TagComponent>(sphere, "Metal Sphere");
    m_registry.emplace<TransformComponent>(sphere, glm::vec3(-2.2f, 0.75f, 0.0f));
    m_registry.emplace<MeshComponent>(sphere, "Sphere", "", 0u, 0u);
    auto& sphereMat = m_registry.emplace<MaterialComponent>(sphere);
    sphereMat.roughness = 0.18f;
    sphereMat.metallic = 0.90f;
    m_registry.emplace<RenderableComponent>(sphere);

    auto physCube = m_registry.create();
    m_registry.emplace<TagComponent>(physCube, "Physics Cube");
    auto& transformPhys = m_registry.emplace<TransformComponent>(physCube, glm::vec3(2.0f, 4.0f, 0.0f));
    transformPhys.scale = glm::vec3(0.7f);
    m_registry.emplace<MeshComponent>(physCube, "Cube", "", 24u, 36u);
    auto& physMat = m_registry.emplace<MaterialComponent>(physCube);
    physMat.albedoTexturePath = "assets/textures/uv_grid.png";
    m_registry.emplace<RigidBodyComponent>(physCube);
    m_registry.emplace<BoxColliderComponent>(physCube);
    m_registry.emplace<RenderableComponent>(physCube);

    auto particleEntity = m_registry.create();
    m_registry.emplace<TagComponent>(particleEntity, "Particle Emitter");
    m_registry.emplace<TransformComponent>(particleEntity, glm::vec3(-1.5f, 0.5f, 0.0f));
    m_registry.emplace<ParticleEmitterComponent>(particleEntity);

    // A positioned audio source so the spatialisation is audible and testable.
    auto audioEntity = m_registry.create();
    m_registry.emplace<TagComponent>(audioEntity, "3D Audio Source");
    m_registry.emplace<TransformComponent>(audioEntity, glm::vec3(3.0f, 1.0f, 0.0f));
    auto& audioSource = m_registry.emplace<AudioSourceComponent>(audioEntity);
    audioSource.soundFile = "assets/audio/ambient.wav";
    audioSource.volume = 0.35f;

    std::cout << "[EngineApp] Scene created." << std::endl;
}

void EngineApp::Run() {
    std::cout << "[EngineApp] Starting Main 3D Game Loop..." << std::endl;

    double lastTime = glfwGetTime();

    while (!m_window->ShouldClose()) {
        m_window->PollEvents();

        const double currentTime = glfwGetTime();
        const float rawDelta = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;
        const float deltaTime = std::clamp(rawDelta, 0.0f, kMaxFrameDelta);

        // Swap in a rebuilt script plugin. Cheap: one stat unless it changed.
        m_hotReload->Poll();

        // Rebuild the offscreen target before anything else touches it.
        //
        // This has to happen before ImGui::NewFrame, not after ImGui::Render:
        // recreating the target frees its ImGui descriptor set, and once
        // ImGui::Image has recorded that texture ID into the frame's draw data,
        // freeing it leaves the draw call pointing at a released descriptor.
        // Nothing is recording and no draw data is live at this point.
        m_editorLayer->ApplyPendingResize();

        // NewFrame computes WantCaptureMouse/Keyboard for THIS frame, which is
        // what lets the camera know whether the UI owns the input. Running the
        // camera before this could only ever consult the previous frame's flags.
        m_renderer->NewImGuiFrame();

        const ImGuiIO& io = ImGui::GetIO();
        const bool uiWantsMouse = io.WantCaptureMouse;
        const bool uiWantsKeyboard = io.WantCaptureKeyboard || io.WantTextInput;

        CameraSystem::Update(m_registry, *m_window, deltaTime, !uiWantsKeyboard, !uiWantsMouse);

        if (!TimeTravelDebugger::IsRewinding()) {
            // Fixed-step physics. The accumulator is capped so a long hitch
            // costs fidelity rather than exploding the simulation.
            m_physicsAccumulator += deltaTime;
            int steps = 0;
            while (m_physicsAccumulator >= kFixedPhysicsStep && steps < kMaxPhysicsStepsPerFrame) {
                PhysicsSystem::Update(m_registry, kFixedPhysicsStep);
                m_physicsAccumulator -= kFixedPhysicsStep;
                ++steps;
            }
            if (steps == kMaxPhysicsStepsPerFrame) {
                m_physicsAccumulator = 0.0f;
            }

            AudioSystem::Update(m_registry, *m_audioEngine, deltaTime);
            ScriptEngine::Update(m_registry, deltaTime);
            ParticleSystem::Update(m_registry, deltaTime);
            TimeTravelDebugger::RecordFrame(m_registry, static_cast<float>(currentTime));
        }

        // Editor UI runs after the systems and before rendering, so gizmo drags
        // and inspector edits appear in the same frame instead of one late.
        m_editorLayer->SetScriptHostInfo(m_hotReload->IsLoaded(),
                                         m_hotReload->GetStatus(),
                                         m_hotReload->GetReloadCount());
        m_editorLayer->BuildUI(m_registry, *m_window);
        ImGui::Render();

        // Mesh and texture uploads submit their own transfers, so they happen
        // here rather than mid-recording.
        RenderSystem::SyncResources(m_registry,
                                    m_renderer->GetMeshRegistry(),
                                    m_renderer->GetTextureRegistry());

        glm::mat4 viewMatrix(1.0f);
        glm::mat4 projMatrix(1.0f);
        glm::vec3 cameraPosition(0.0f);

        if (const auto camEntity = FirstEntityOf(m_registry.view<CameraComponent>());
            camEntity != entt::null) {
            const auto& cam = m_registry.get<CameraComponent>(camEntity);
            viewMatrix = cam.getViewMatrix();
            projMatrix = cam.getProjectionMatrix();
            cameraPosition = cam.position;
        }

        m_renderer->DrawFrame(m_registry,
                              m_editorLayer->GetOffscreen(),
                              ImGui::GetDrawData(),
                              viewMatrix,
                              projMatrix,
                              cameraPosition);
    }

    std::cout << "[EngineApp] Window close requested. Waiting for GPU idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

} // namespace Engine
