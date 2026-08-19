#include "core/SupersonicApp.hpp"
#include "core/Log.hpp"
#include "core/Profiler.hpp"
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
#include "core/GameRuntime.hpp"
#include "core/SceneSerializer.hpp"
#include "platform/ExecutablePath.hpp"

#include "imgui.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <array>
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
    const std::filesystem::path exePath = ExecutablePath();
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

SupersonicApp::SupersonicApp(const LaunchOptions& options)
    : m_options(options) {
    SUPERSONIC_LOG_INFO("SupersonicApp") << "Initializing Engine Subsystems..." << std::endl;

    // Asset writes target these; create them before anything tries to save.
    std::error_code ec;
    std::filesystem::create_directories("assets/scenes", ec);
    if (ec) {
        SUPERSONIC_LOG_ERROR("SupersonicApp") << "Could not create assets/scenes: " << ec.message() << std::endl;
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

    // Read before the window exists, because it decides the title on it.
    m_manifest = GameRuntime::Load();

    m_window = std::make_unique<Window>(1280, 720,
                                        m_manifest.isGame ? m_manifest.title : "Supersonic Engine");

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

    m_editorLayer->SetGameMode(m_manifest.isGame);

    initECS();

    // --scene wins over the manifest, and applies in the editor too: a smoke
    // test is only worth running against the scene you want to smoke test.
    const std::string startupScene =
        m_options.scenePath.empty() ? m_manifest.startupScene : m_options.scenePath;

    if (m_manifest.isGame || !m_options.scenePath.empty()) {
        // The demo scene initECS just built is the editor's starting point, not
        // the game's. A packaged game that opened it was the clearest symptom
        // that packaging shipped an editor.
        const auto loaded = SceneSerializer::Deserialize(m_registry, startupScene);
        SUPERSONIC_LOG_INFO("SupersonicApp") << loaded.message << std::endl;
        if (!loaded.ok) {
            SUPERSONIC_LOG_ERROR("SupersonicApp") << "Falling back to the built-in scene." << std::endl;
        }

        // Straight into Play: a game has no edit mode to be in, and without
        // this nothing would move, no script would run and no sound would play.
        const auto started = m_playMode.Play(m_registry);
        if (!started.ok) {
            SUPERSONIC_LOG_ERROR("SupersonicApp") << started.message << std::endl;
        }
    }
}

SupersonicApp::~SupersonicApp() {
    SUPERSONIC_LOG_INFO("SupersonicApp") << "Shutting down Engine Subsystems in reverse order..." << std::endl;

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
    SUPERSONIC_LOG_INFO("SupersonicApp") << "Initializing EnTT 3D Entities, Components & Lighting..." << std::endl;

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
    // Hemispheric: a cool sky above, a warmer bounce off the ground below.
    // A single flat term lit the underside of everything exactly as brightly
    // and as blue as its top.
    light.ambient = glm::vec3(0.055f, 0.065f, 0.09f);
    light.ambientGround = glm::vec3(0.06f, 0.05f, 0.04f);
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

    // A spot light over the platform, aimed down and slightly forward. The
    // engine had no way to aim a light at all before this: a torch, a street
    // lamp or a stage light was a point light that lit the whole room.
    auto spot = m_registry.create();
    m_registry.emplace<TagComponent>(spot, "Spot Light");
    m_registry.emplace<TransformComponent>(spot, glm::vec3(0.4f, 5.4f, 2.6f));
    auto& spotLight = m_registry.emplace<LightComponent>(spot);
    spotLight.type = static_cast<int>(LightType::Spot);
    spotLight.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.35f));
    spotLight.color = glm::vec3(1.0f, 0.93f, 0.78f);
    spotLight.intensity = 14.0f;
    spotLight.range = 22.0f;
    spotLight.innerAngle = glm::radians(16.0f);
    spotLight.outerAngle = glm::radians(26.0f);
    spotLight.castsShadow = true;

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
    // Dropped spinning and slightly off centre, so the demo scene shows a
    // crate tumbling and settling rather than descending perfectly level -
    // which is all a scene without rotational dynamics could ever do.
    auto& physBody = m_registry.emplace<RigidBodyComponent>(physCube);
    physBody.angularVelocity = glm::vec3(0.0f, 1.2f, 2.4f);
    physBody.restitution = 0.45f;
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

    // A small HUD, so the in-game UI is visible in the viewport rather than
    // being a feature you have to know to go looking for. Screen-space, so
    // none of these carry a TransformComponent.
    auto hudTitle = m_registry.create();
    m_registry.emplace<TagComponent>(hudTitle, "HUD Title");
    auto& title = m_registry.emplace<UITextComponent>(hudTitle);
    title.text = "SUPERSONIC";
    title.anchor = UIAnchor::TopLeft;
    title.offset = glm::vec2(32.0f, 24.0f);
    title.fontSize = 40.0f;
    title.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.92f);

    auto hudScore = m_registry.create();
    m_registry.emplace<TagComponent>(hudScore, "HUD Score");
    auto& score = m_registry.emplace<UITextComponent>(hudScore);
    score.text = "SCORE 0";
    score.anchor = UIAnchor::TopRight;
    score.offset = glm::vec2(32.0f, 28.0f);
    score.fontSize = 30.0f;

    auto hudHealth = m_registry.create();
    m_registry.emplace<TagComponent>(hudHealth, "HUD Health Bar");
    auto& health = m_registry.emplace<UIPanelComponent>(hudHealth);
    health.anchor = UIAnchor::BottomLeft;
    health.offset = glm::vec2(32.0f, 32.0f);
    health.size = glm::vec2(360.0f, 26.0f);
    health.color = glm::vec4(0.90f, 0.26f, 0.28f, 0.95f);
    health.trackColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.55f);
    health.drawTrack = true;
    health.cornerRadius = 13.0f;
    health.fill = 0.72f;

    // A button, so the in-game UI is demonstrably interactive rather than
    // merely drawn. The script counts its own clicks into its own label.
    auto hudButton = m_registry.create();
    m_registry.emplace<TagComponent>(hudButton, "HUD Button");
    auto& demoButton = m_registry.emplace<UIButtonComponent>(hudButton);
    demoButton.label = "CLICK ME";
    demoButton.anchor = UIAnchor::BottomRight;
    demoButton.offset = glm::vec2(32.0f, 32.0f);
    demoButton.size = glm::vec2(240.0f, 56.0f);
    demoButton.fontSize = 24.0f;
    auto& buttonScript = m_registry.emplace<ScriptComponent>(hudButton);
    buttonScript.scriptName = "ClickCounterScript";

    SUPERSONIC_LOG_INFO("SupersonicApp") << "Scene created." << std::endl;
}

void SupersonicApp::Run() {
    SUPERSONIC_LOG_INFO("SupersonicApp") << "Starting Main 3D Game Loop..." << std::endl;

    double lastTime = glfwGetTime();

    // --frames renders a fixed count and exits. The check is at the top of the
    // loop rather than the bottom so --frames 0 keeps meaning "run until the
    // window closes" without a special case, and --frames 1 renders exactly
    // one frame rather than two.
    long long frame = 0;
    std::array<double, Profiler::kZoneCount> zoneTotals{};

    while (!m_window->ShouldClose()) {
        // Fold the frame that just finished into the totals. This has to happen
        // before the exit test, not after the increment, or the final frame is
        // counted in the divisor and never added to the sum.
        if (frame > 0) {
            for (std::size_t i = 0; i < Profiler::kZoneCount; ++i) {
                zoneTotals[i] += Profiler::Milliseconds(static_cast<ProfileZone>(i));
            }
        }

        if (m_options.maxFrames > 0 && frame >= m_options.maxFrames) {
            SUPERSONIC_LOG_INFO("SupersonicApp") << "Rendered " << frame
                      << " frame(s) as requested; exiting." << std::endl;

            // Report where the frames went. Without this the profiler is
            // visible only through the editor's statistics panel, which a
            // headless run has nobody to look at - and a measurement no script
            // can read is not one CI can act on.
            //
            // Plain stdout, not the log: this is what --frames was asked to
            // produce, so it is program output rather than a diagnostic about
            // producing it. Splitting a report across two streams - a heading
            // through the log and its rows through cout - would also let the
            // two interleave with anything else logging in between.
            std::cout << "[Profiler] Mean CPU cost per frame over "
                      << frame << " frame(s):" << std::endl;
            for (std::size_t i = 0; i < Profiler::kZoneCount; ++i) {
                const double mean = zoneTotals[i] / static_cast<double>(frame);
                if (mean < 0.005) continue;
                std::cout << "  " << Profiler::Name(static_cast<ProfileZone>(i))
                          << ": " << mean << " ms" << std::endl;
            }
            break;
        }
        ++frame;
        Profiler::BeginFrame();

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
                SUPERSONIC_PROFILE(Physics);
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

            { SUPERSONIC_PROFILE(Audio);     AudioSystem::Update(m_registry, *m_audioEngine, deltaTime); }
            { SUPERSONIC_PROFILE(Scripts);   ScriptEngine::Update(m_registry, deltaTime); }
            { SUPERSONIC_PROFILE(Animation); AnimationSystem::Advance(m_registry, *m_animationLibrary, deltaTime); }
            { SUPERSONIC_PROFILE(Particles); ParticleSystem::Update(m_registry, deltaTime); }
            TimeTravelDebugger::RecordFrame(m_registry, static_cast<float>(currentTime));
        }
        }

        // Resolve the parent/child graph before the editor runs, so the gizmo
        // and viewport picking operate on current world matrices rather than
        // last frame's.
        { SUPERSONIC_PROFILE(Transform); TransformSystem::UpdateWorldTransforms(m_registry); }

        // Editor UI runs after the systems and before rendering, so gizmo drags
        // and inspector edits appear in the same frame instead of one late.
        m_editorLayer->SetPlayMode(&m_playMode);
        m_editorLayer->SetMaterialLibrary(m_materialLibrary.get());
        m_editorLayer->SetRenderStats(m_renderer->GetRenderStats());
        m_editorLayer->SetContacts(m_contacts);
        m_editorLayer->SetScriptHostInfo(m_hotReload->IsLoaded(),
                                         m_hotReload->GetStatus(),
                                         m_hotReload->GetReloadCount());
        { SUPERSONIC_PROFILE(EditorUI);    m_editorLayer->BuildUI(m_registry, *m_window); }
        { SUPERSONIC_PROFILE(ImGuiRender); ImGui::Render(); }

        // Again, because the editor may have moved, reparented or created
        // entities. Rendering reads world matrices, so they must reflect what
        // the user just did rather than lagging a frame behind it.
        { SUPERSONIC_PROFILE(Transform); TransformSystem::UpdateWorldTransforms(m_registry); }

        // Shared materials resolve onto their components before the renderer
        // reads them, so an edit to one asset shows on every entity using it in
        // the same frame.
        MaterialSystem::Sync(m_registry, *m_materialLibrary);

        // Mesh and texture uploads submit their own transfers, so they happen
        // here rather than mid-recording.
        {
            SUPERSONIC_PROFILE(ResourceSync);
            RenderSystem::SyncResources(m_registry,
                                        m_renderer->GetMeshRegistry(),
                                        m_renderer->GetTextureRegistry());
        }

        // Poses are evaluated every frame regardless of play mode, so the
        // inspector can scrub an animation and see the result in the same frame.
        // Only the CLOCK is gated on play mode, above.
        //
        // After SyncResources, not before: SyncResources rewrites the render
        // bounds from the static mesh, and the pose bounds have to be the last
        // word or an animated character is culled against its bind pose.
        {
            SUPERSONIC_PROFILE(PoseEvaluation);
            AnimationSystem::SyncSkeletons(m_registry, *m_animationLibrary);
            AnimationSystem::EvaluatePoses(m_registry, *m_animationLibrary);
        }

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

    SUPERSONIC_LOG_INFO("SupersonicApp") << "Window close requested. Waiting for GPU idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

} // namespace Supersonic
