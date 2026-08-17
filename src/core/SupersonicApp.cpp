#include "core/SupersonicApp.hpp"
#include "core/Components.hpp"
#include "core/CameraSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/AudioSystem.hpp"
#include "core/ScriptEngine.hpp"
#include "core/ParticleSystem.hpp"
#include "core/JobSystem.hpp"
#include "core/AnimationSystem.hpp"
#include "core/MaterialSystem.hpp"
#include "core/Input.hpp"
#include "platform/InputPolling.hpp"
#include "core/RenderSystem.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "core/EcsUtils.hpp"
#include "core/TransformSystem.hpp"

#include "imgui.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace {
// A frame longer than this is treated as a hitch rather than elapsed time.
// Dragging the title bar on Win32 blocks glfwPollEvents inside the modal
// move/size loop, which used to hand physics a two-second delta.
constexpr float kMaxFrameDelta = 0.10f;

constexpr float kFixedPhysicsStep = 1.0f / 60.0f;
constexpr int kMaxPhysicsStepsPerFrame = 5;

// Absolute path of the running binary, or empty when the platform has no cheap
// way to ask. Used to find the script plugin that was built alongside it.
std::filesystem::path executablePath() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) return {};
    return std::filesystem::path(buffer);
#else
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : self;
#endif
}

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

    // Next to the running executable first. Checking build/Debug before
    // build/Release meant a Release build loaded a stale Debug plugin - which
    // the ABI version check caught, but only as a refusal to load any scripts
    // at all, with a message that blamed the plugin rather than the lookup.
    const std::filesystem::path exePath = executablePath();
    if (!exePath.empty()) {
        const std::filesystem::path beside = exePath.parent_path() / name;
        if (std::filesystem::exists(beside, ec)) return beside.string();
    }

    // Then the usual build trees, relative to the working directory.
    for (const char* config : {"build/Release/", "build/Debug/"}) {
        const std::string candidate = std::string(config) + name;
        if (std::filesystem::exists(candidate, ec)) return candidate;
    }
    return name;
}
} // namespace

SupersonicApp::SupersonicApp() {
    std::cout << "[SupersonicApp] Initializing Engine Subsystems..." << std::endl;

    // Asset writes target these; create them before anything tries to save.
    std::error_code ec;
    std::filesystem::create_directories("assets/scenes", ec);
    if (ec) {
        std::cerr << "[SupersonicApp] Could not create assets/scenes: " << ec.message() << std::endl;
    }

    // Workers come up before anything that might dispatch to them: mesh
    // generation during initECS already parallelises its per-vertex passes.
    JobSystem::Initialize();

    // Before any system can ask for input, and before the first frame.
    Input::LoadDefaultBindings();

    m_audioEngine = std::make_unique<AudioEngine>();
    m_animationLibrary = std::make_unique<AnimationLibrary>();
    m_materialLibrary = std::make_unique<MaterialLibrary>();
    // Stops voices when their entity goes away; sources loop by default.
    AudioSystem::Attach(m_registry, *m_audioEngine);

    // Scripts: built-ins first, then whatever the plugin adds on top.
    ScriptEngine::RegisterBuiltInScripts();
    m_hotReload = std::make_unique<HotReloadEngine>();
    m_hotReload->WatchPlugin(scriptPluginPath());

    m_window = std::make_unique<Window>(1280, 720, "Supersonic Engine");

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
    m_renderer->SetOffscreenRenderPass(m_editorLayer->GetOffscreen().GetRenderPass(),
                                       m_editorLayer->GetOffscreen().GetSampleCount());

    initECS();
}

SupersonicApp::~SupersonicApp() {
    std::cout << "[SupersonicApp] Shutting down Engine Subsystems in reverse order..." << std::endl;

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
    m_animationLibrary.reset();
    m_materialLibrary.reset();

    // Last: a worker holding a reference to anything above would otherwise
    // outlive it. Shutdown drains outstanding work before joining.
    JobSystem::Shutdown();
}

void SupersonicApp::initECS() {
    std::cout << "[SupersonicApp] Initializing EnTT 3D Entities, Components & Lighting..." << std::endl;

    // Tagged, so it does not show up in the hierarchy as an anonymous
    // "Entity 0" that can be deleted without realising it is the camera.
    auto cameraEntity = m_registry.create();
    m_registry.emplace<TagComponent>(cameraEntity, "Main Camera");
    m_registry.emplace<TransformComponent>(cameraEntity, glm::vec3(0.6f, 2.6f, 9.5f));
    auto& camera = m_registry.emplace<CameraComponent>(cameraEntity);
    camera.fov = 50.0f;
    camera.aspect = 1280.0f / 720.0f;
    camera.position = glm::vec3(0.6f, 2.6f, 9.5f);
    camera.yaw = -92.0f;
    camera.pitch = -12.0f;
    // Stated rather than left to the component default, so the scene says which
    // camera play mode renders through instead of it falling out of pool order.
    camera.isPrimary = true;
    camera.updateCameraVectors();

    auto lightEntity = m_registry.create();
    m_registry.emplace<TagComponent>(lightEntity, "Sun (Directional)");
    m_registry.emplace<TransformComponent>(lightEntity);
    auto& light = m_registry.emplace<LightComponent>(lightEntity);
    light.type = static_cast<int>(LightType::Directional);
    light.direction = glm::normalize(glm::vec3(0.55f, 1.0f, 0.42f));
    light.color = glm::vec3(1.0f, 0.96f, 0.88f);
    light.intensity = 1.6f;
    light.ambient = glm::vec3(0.05f, 0.055f, 0.07f);
    light.castsShadow = true;

    // Two point lights, to exercise the multi-light path the single hardcoded
    // direction in the old shader could not express.
    auto pointA = m_registry.create();
    m_registry.emplace<TagComponent>(pointA, "Point Light (Warm)");
    m_registry.emplace<TransformComponent>(pointA, glm::vec3(-3.2f, 1.6f, 2.0f));
    auto& lightA = m_registry.emplace<LightComponent>(pointA);
    lightA.type = static_cast<int>(LightType::Point);
    lightA.color = glm::vec3(1.0f, 0.55f, 0.25f);
    lightA.intensity = 2.2f;
    lightA.range = 14.0f;

    auto pointB = m_registry.create();
    m_registry.emplace<TagComponent>(pointB, "Point Light (Cool)");
    m_registry.emplace<TransformComponent>(pointB, glm::vec3(3.4f, 1.4f, 2.2f));
    auto& lightB = m_registry.emplace<LightComponent>(pointB);
    lightB.type = static_cast<int>(LightType::Point);
    lightB.color = glm::vec3(0.30f, 0.55f, 1.0f);
    lightB.intensity = 2.2f;
    lightB.range = 14.0f;

    // Ground plane. Without a receiver there is nothing for the shadow map to
    // fall on - the grid is a shader overlay, not geometry.
    auto ground = m_registry.create();
    m_registry.emplace<TagComponent>(ground, "Ground");
    auto& groundTransform = m_registry.emplace<TransformComponent>(ground, glm::vec3(0.0f, 0.0f, 0.0f));
    groundTransform.scale = glm::vec3(40.0f, 1.0f, 40.0f);
    m_registry.emplace<MeshComponent>(ground, "Plane", "", 0u, 0u);
    auto& groundMat = m_registry.emplace<MaterialComponent>(ground);
    // Through a shared asset rather than inline values, so the sample scene
    // exercises the path and the material is editable from the browser.
    groundMat.materialPath = "assets/materials/TiledFloor.material";
    groundMat.albedoTexturePath = "assets/textures/floor_tiles.png";
    groundMat.normalTexturePath = "assets/textures/tiles_normal.png";
    groundMat.roughness = 0.72f;
    groundMat.metallic = 0.0f;
    auto& groundRenderable = m_registry.emplace<RenderableComponent>(ground);
    // A demo-scene choice, not a rule: this plane is only ever a receiver, so
    // excluding it from the depth pass costs nothing. Geometry that should cast
    // onto other geometry must leave castsShadow on - front-face culling in the
    // shadow pass is what handles self-shadowing acne in general.
    groundRenderable.castsShadow = false;

    auto mainCube = m_registry.create();
    m_registry.emplace<TagComponent>(mainCube, "Textured Cube");
    m_registry.emplace<TransformComponent>(mainCube, glm::vec3(0.0f, 0.9f, 0.0f));
    m_registry.emplace<MeshComponent>(mainCube, "Cube", "", 24u, 36u);
    auto& cubeMat = m_registry.emplace<MaterialComponent>(mainCube);
    cubeMat.albedoTexturePath = "assets/textures/uv_grid.png";
    cubeMat.normalTexturePath = "assets/textures/tiles_normal.png";
    cubeMat.roughness = 0.45f;
    m_registry.emplace<RenderableComponent>(mainCube);
    m_registry.emplace<ScriptComponent>(mainCube, "RotatorScript");

    // Parented to the rotating cube: it inherits the rotation, which is the
    // whole point of a transform hierarchy. Flat transforms could not express
    // this at all.
    auto satellite = m_registry.create();
    m_registry.emplace<TagComponent>(satellite, "Satellite (child)");
    auto& satelliteTransform = m_registry.emplace<TransformComponent>(satellite, glm::vec3(1.6f, 0.6f, 0.0f));
    satelliteTransform.scale = glm::vec3(0.35f);
    m_registry.emplace<MeshComponent>(satellite, "Sphere", "", 0u, 0u);
    auto& satelliteMat = m_registry.emplace<MaterialComponent>(satellite);
    satelliteMat.albedoColor = glm::vec4(1.0f, 0.45f, 0.25f, 1.0f);
    satelliteMat.roughness = 0.35f;
    m_registry.emplace<RenderableComponent>(satellite);
    m_registry.emplace<HierarchyComponent>(satellite, mainCube);

    auto sphere = m_registry.create();
    m_registry.emplace<TagComponent>(sphere, "Metal Sphere");
    m_registry.emplace<TransformComponent>(sphere, glm::vec3(-2.2f, 0.75f, 0.0f));
    m_registry.emplace<MeshComponent>(sphere, "Sphere", "", 0u, 0u);
    auto& sphereMat = m_registry.emplace<MaterialComponent>(sphere);
    sphereMat.roughness = 0.18f;
    sphereMat.metallic = 0.90f;
    m_registry.emplace<RenderableComponent>(sphere);

    // glTF import: a node hierarchy with TRS transforms and PBR materials,
    // loaded through tinygltf, which was vendored but unreferenced until now.
    auto monument = m_registry.create();
    m_registry.emplace<TagComponent>(monument, "Monument (glTF)");
    m_registry.emplace<TransformComponent>(monument, glm::vec3(3.6f, 0.0f, 0.5f));
    m_registry.emplace<MeshComponent>(monument, "", "assets/models/monument.gltf", 0u, 0u);
    auto& monumentMat = m_registry.emplace<MaterialComponent>(monument);
    monumentMat.albedoTexturePath = "assets/textures/uv_grid.png";
    monumentMat.roughness = 0.55f;
    m_registry.emplace<RenderableComponent>(monument);

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

    // A static platform the falling cube lands on, so the sample scene actually
    // exercises entity-versus-entity collision rather than only the ground
    // plane. No RigidBodyComponent, which is what makes it immovable.
    auto platform = m_registry.create();
    m_registry.emplace<TagComponent>(platform, "Platform (Static)");
    auto& platformTransform = m_registry.emplace<TransformComponent>(platform, glm::vec3(2.0f, 0.6f, 0.0f));
    platformTransform.scale = glm::vec3(2.0f, 0.3f, 2.0f);
    m_registry.emplace<MeshComponent>(platform, "Cube", "", 24u, 36u);
    auto& platformMat = m_registry.emplace<MaterialComponent>(platform);
    platformMat.albedoColor = glm::vec4(0.55f, 0.57f, 0.62f, 1.0f);
    platformMat.roughness = 0.7f;
    m_registry.emplace<BoxColliderComponent>(platform);
    m_registry.emplace<RenderableComponent>(platform);

    // A second body, offset so it lands on the first and the pair has to be
    // separated rather than merely stopped by the floor.
    auto stackedCube = m_registry.create();
    m_registry.emplace<TagComponent>(stackedCube, "Falling Sphere");
    auto& stackedTransform = m_registry.emplace<TransformComponent>(stackedCube, glm::vec3(2.35f, 6.5f, 0.0f));
    stackedTransform.scale = glm::vec3(0.6f);
    m_registry.emplace<MeshComponent>(stackedCube, "Sphere", "", 1024u, 5766u);
    auto& stackedMat = m_registry.emplace<MaterialComponent>(stackedCube);
    stackedMat.albedoColor = glm::vec4(0.85f, 0.35f, 0.20f, 1.0f);
    stackedMat.metallic = 0.2f;
    auto& stackedBody = m_registry.emplace<RigidBodyComponent>(stackedCube);
    stackedBody.mass = 2.0f; // heavier, so the mass term in the response is visible
    m_registry.emplace<SphereColliderComponent>(stackedCube);
    m_registry.emplace<RenderableComponent>(stackedCube);

    // Skinned character, so the sample scene exercises the animation path and
    // its shadow rather than leaving it to a test fixture nobody looks at.
    auto animated = m_registry.create();
    m_registry.emplace<TagComponent>(animated, "Bender (Skinned)");
    auto& animatedTransform = m_registry.emplace<TransformComponent>(animated, glm::vec3(-2.6f, 0.0f, 1.2f));
    animatedTransform.scale = glm::vec3(0.9f);
    m_registry.emplace<MeshComponent>(animated, "", "assets/models/bender.gltf", 12u, 48u);
    auto& animatedMat = m_registry.emplace<MaterialComponent>(animated);
    animatedMat.albedoColor = glm::vec4(0.85f, 0.55f, 0.25f, 1.0f);
    animatedMat.roughness = 0.45f;
    auto& animator = m_registry.emplace<AnimatorComponent>(animated);
    animator.clipName = "Bend";
    m_registry.emplace<RenderableComponent>(animated);

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

    std::cout << "[SupersonicApp] Scene created." << std::endl;
}

void SupersonicApp::Run() {
    std::cout << "[SupersonicApp] Starting Main 3D Game Loop..." << std::endl;

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

        // Devices are read once, here, and every consumer queries the snapshot.
        // Polling per-consumer would give two systems different answers within
        // the same frame, and edge detection would fire more than once.
        InputPolling::Poll(*m_window);

        const ImGuiIO& io = ImGui::GetIO();

        // Edit mode flies the editor's own camera; play mode drives the
        // scene's. Before this the two were the same object, so positioning the
        // view in the editor silently authored the game camera.
        if (m_playMode.IsEditing()) {
            m_editorLayer->GetEditorCamera().Update(*m_window, deltaTime,
                                                    m_editorLayer->IsViewportHovered());
        } else {
            CameraSystem::Update(m_registry, *m_window, deltaTime,
                                 m_editorLayer->IsViewportFocused() && !io.WantTextInput,
                                 m_editorLayer->IsViewportHovered());
        }

        // Gameplay only runs in play mode. The editor used to simulate
        // permanently, so a scene could never be authored and then tried.
        const bool stepping = m_playMode.ConsumeSingleStep();
        if (m_playMode.ShouldSimulate() || stepping) {
        if (!TimeTravelDebugger::IsRewinding()) {
            // Fixed-step physics. The accumulator is capped so a long hitch
            // costs fidelity rather than exploding the simulation.
            m_physicsAccumulator += deltaTime;
            int steps = 0;
            m_contacts.clear();
            while (m_physicsAccumulator >= kFixedPhysicsStep && steps < kMaxPhysicsStepsPerFrame) {
                PhysicsSystem::Update(m_registry, kFixedPhysicsStep, &m_stepContacts);
                // Accumulated across the frame's steps, so the count the editor
                // shows is the frame's contacts rather than the last step's.
                m_contacts.insert(m_contacts.end(), m_stepContacts.begin(), m_stepContacts.end());
                m_physicsAccumulator -= kFixedPhysicsStep;
                ++steps;
            }
            if (steps == kMaxPhysicsStepsPerFrame) {
                m_physicsAccumulator = 0.0f;
            }

            AudioSystem::Update(m_registry, *m_audioEngine, deltaTime);
            ScriptEngine::Update(m_registry, deltaTime);
            AnimationSystem::Advance(m_registry, *m_animationLibrary, deltaTime);
            ParticleSystem::Update(m_registry, deltaTime);
            TimeTravelDebugger::RecordFrame(m_registry, static_cast<float>(currentTime));
        }
        }

        // Resolve the parent/child graph before the editor runs, so the gizmo
        // and viewport picking operate on current world matrices rather than
        // last frame's.
        TransformSystem::UpdateWorldTransforms(m_registry);

        // Editor UI runs after the systems and before rendering, so gizmo drags
        // and inspector edits appear in the same frame instead of one late.
        m_editorLayer->SetPlayMode(&m_playMode);
        m_editorLayer->SetMaterialLibrary(m_materialLibrary.get());
        m_editorLayer->SetRenderStats(m_renderer->GetRenderStats());
        m_editorLayer->SetContacts(m_contacts);
        m_editorLayer->SetScriptHostInfo(m_hotReload->IsLoaded(),
                                         m_hotReload->GetStatus(),
                                         m_hotReload->GetReloadCount());
        m_editorLayer->BuildUI(m_registry, *m_window);
        ImGui::Render();

        // Again, because the editor may have moved, reparented or created
        // entities. Rendering reads world matrices, so they must reflect what
        // the user just did rather than lagging a frame behind it.
        TransformSystem::UpdateWorldTransforms(m_registry);

        // Shared materials resolve onto their components before the renderer
        // reads them, so an edit to one asset shows on every entity using it in
        // the same frame.
        MaterialSystem::Sync(m_registry, *m_materialLibrary);

        // Mesh and texture uploads submit their own transfers, so they happen
        // here rather than mid-recording.
        RenderSystem::SyncResources(m_registry,
                                    m_renderer->GetMeshRegistry(),
                                    m_renderer->GetTextureRegistry());

        // Poses are evaluated every frame regardless of play mode, so the
        // inspector can scrub an animation and see the result in the same frame.
        // Only the CLOCK is gated on play mode, above.
        //
        // After SyncResources, not before: SyncResources rewrites the render
        // bounds from the static mesh, and the pose bounds have to be the last
        // word or an animated character is culled against its bind pose.
        AnimationSystem::SyncSkeletons(m_registry, *m_animationLibrary);
        AnimationSystem::EvaluatePoses(m_registry, *m_animationLibrary);

        // Whichever camera the viewport is showing - the same choice the
        // editor makes for picking and the gizmo, so all three agree.
        const CameraComponent* renderCamera = &m_editorLayer->GetEditorCamera().Get();
        if (!m_playMode.IsEditing()) {
            if (const auto camEntity = FindPrimaryCamera(m_registry); camEntity != entt::null) {
                renderCamera = &m_registry.get<CameraComponent>(camEntity);
            }
        }

        m_renderer->DrawFrame(m_registry,
                              m_editorLayer->GetOffscreen(),
                              ImGui::GetDrawData(),
                              *renderCamera);
    }

    std::cout << "[SupersonicApp] Window close requested. Waiting for GPU idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

} // namespace Supersonic
