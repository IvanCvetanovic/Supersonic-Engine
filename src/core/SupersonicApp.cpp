#include "core/SupersonicApp.hpp"

#include "core/WorldShapes.hpp"
#include "renderer/ScreenCapture.hpp"
#include "core/AssetDatabase.hpp"
#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "core/ConvexHullCache.hpp"
#include "core/InterpolationSystem.hpp"
#include "core/Components.hpp"
#include "core/CameraSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/AudioSystem.hpp"
#include "core/ScriptEngine.hpp"
#include "core/ParticleSystem.hpp"
#include "core/SpriteAnimationSystem.hpp"
#include "core/JobSystem.hpp"
#include "core/AnimationSystem.hpp"
#include "core/MaterialSystem.hpp"
#include "core/Input.hpp"
#include "core/UIInput.hpp"
#include "platform/InputPolling.hpp"
#include "core/RenderSystem.hpp"
#include "core/TimeTravelDebugger.hpp"
#include "core/EcsUtils.hpp"
#include "core/TransformSystem.hpp"
#include "core/SimulationClock.hpp"
#include "core/StateHash.hpp"
#include "core/Application.hpp"
#include "core/InputRecording.hpp"
#include "core/RenderSettings.hpp"
#include "core/GameRuntime.hpp"
#include "core/SceneSerializer.hpp"
#include "core/PrefabSerializer.hpp"
#include "platform/ExecutablePath.hpp"
#include "editor/Theme.hpp"
#include "editor/EditorFonts.hpp"

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

// What the SOLVER wants, which is not what a game wants. Physics is stable at
// a rate chosen for the solver; a game thinks at a rate chosen for the game.
// These were one constant because there was one loop, and that made a 20 Hz
// simulation impossible to ask for.
constexpr float kFixedPhysicsStep = 1.0f / 60.0f;

// The default game tick, and deliberately the same as the physics step so a
// scene that says nothing behaves exactly as it did before any of this.
constexpr float kDefaultGameTick = 1.0f / 60.0f;

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

SupersonicApp::SupersonicApp(const LaunchOptions& options, const GameManifest* manifest)
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

    // So saving a material from the inspector does not read itself back as an
    // edit somebody else made. Both outlive each other here: the watcher is a
    // member by value and the library is reset explicitly in Shutdown.
    m_materialLibrary->SetWatcher(&m_assetWatcher);
    // Stops voices when their entity goes away; sources loop by default.
    AudioSystem::Attach(m_registry, *m_audioEngine);

    // Scripts: built-ins first, then whatever the plugin adds on top.
    ScriptEngine::RegisterBuiltInScripts();
    m_hotReload = std::make_unique<HotReloadEngine>();
    m_hotReload->WatchPlugin(scriptPluginPath());

    // Read before the window exists, because it decides the title on it.
    //
    // A DECLARED manifest wins outright and skips the lookup. A game linking
    // the engine knows it is a game; requiring it to also ship a file beside
    // its own executable to be believed made "run it from the build tree" a
    // different program from "run the packaged copy".
    m_manifest = manifest ? *manifest : GameRuntime::Load();

    // A packaged game re-anchors to its own folder before anything opens a file
    // by relative path. This has to happen BEFORE the renderer is built: the
    // first thing to break otherwise is shader loading, which throws during
    // pipeline creation with a message about a .spv - a long way from the
    // launcher's working directory that actually caused it.
    //
    // The editor is deliberately left alone. It is launched from the project
    // root on purpose, and a build tree is not laid out like a packaged folder.
    if (m_manifest.isGame) {
        if (AnchorAssetRootToExecutable()) {
            SUPERSONIC_LOG_INFO("SupersonicApp")
                << "Packaged game: assets resolve from " << AssetRoot().string();
        } else {
            // Not a warning any more, because the ordinary case reaches it: a
            // game run out of its build tree has its executable in build/ and
            // its assets at the project root, and resolving from the working
            // directory is then exactly right. Anchoring only happens when the
            // assets are genuinely beside the executable, which is what a
            // packaged folder looks like and a build tree does not.
            SUPERSONIC_LOG_INFO("SupersonicApp")
                << "No assets beside the executable; they resolve from "
                << AssetRoot().string() << " as the editor's do.";
        }
    }

    // THREE SOURCES, ONE ANSWER, and the order is the point: the flag is for
    // one run, the manifest is what the game ships as, and the default is what
    // an engine with neither opens at.
    //
    // The flag beating the manifest is what makes a measurement at another
    // resolution repeatable. The number this decides is also the RENDER
    // resolution for a game - in game mode the offscreen target follows the
    // window every frame - so it is the resolution the whole scene pass costs
    // at, which is exactly what somebody re-running that measurement is asking
    // about.
    uint32_t windowWidth = 0;
    uint32_t windowHeight = 0;
    GameRuntime::ResolveWindowSize(m_manifest, m_options.windowWidth,
                                   m_options.windowHeight, windowWidth, windowHeight);

    m_window = std::make_unique<Window>(static_cast<int>(windowWidth),
                                        static_cast<int>(windowHeight),
                                        m_manifest.isGame ? m_manifest.title : "Supersonic Engine");

    // Before the renderer, because the renderer initialises ImGui's GLFW
    // backend and that backend chains to whatever it finds already installed.
    // Installed after it instead, these would replace ImGui's own and every
    // text box in the editor would stop accepting characters. There is no
    // compile-time guard for the ordering; InstallCallbacks logs if it lands
    // second.
    InputPolling::InstallCallbacks(*m_window);

    auto requiredExtensions = m_window->GetRequiredExtensions();
    m_vulkanContext = std::make_unique<VulkanContext>(requiredExtensions);

    m_vulkanDevice = std::make_unique<VulkanDevice>(m_vulkanContext->GetInstance(), *m_window);
    m_swapchain = std::make_unique<VulkanSwapchain>(*m_vulkanDevice, *m_window);
    // The editor's appearance, handed to the renderer rather than reached for
    // by it. This is the only line in the engine that decides what the UI looks
    // like, and it is in the application - which is where the editor is.
    m_renderer = std::make_unique<VulkanRenderer>(
        *m_vulkanDevice, *m_swapchain, *m_window,
        [](float dpiScale) {
            EditorFonts::Load(dpiScale);
            Theme::ApplyEngineDarkTheme(dpiScale);
        });

    // The editor's offscreen target registers a texture with the ImGui Vulkan
    // backend, so it must be created after the renderer has initialised it.
    m_editorLayer = std::make_unique<EditorLayer>();
    m_editorLayer->Init(*m_vulkanDevice,
                        m_swapchain->GetExtent().width,
                        m_swapchain->GetExtent().height);

    // The scene pipeline targets the editor's offscreen render pass.
    // Before the first resize, so the bloom chain is rebuilt from the cache
    // rather than from SPIR-V. Dragging the viewport edge previously destroyed
    // and recompiled three pipelines per resize, reading four .spv files off
    // disk each time, while the renderer's own cache sat unused.
    m_editorLayer->GetOffscreen().SetPipelineCache(m_renderer->GetPipelineCache());

    m_renderer->SetOffscreenRenderPass(m_editorLayer->GetOffscreen().GetRenderPass(),
                                       m_editorLayer->GetOffscreen().GetSampleCount());

    m_editorLayer->SetGameMode(m_manifest.isGame);
    m_editorLayer->SetSceneManager(&m_sceneManager);

    // The same answer, given to the renderer. Without it the scene pass drew
    // the editor's ground grid into a shipped game.
    m_renderer->SetEditorOverlaysVisible(!m_manifest.isGame);

    // Prefabs, watched once at startup.
    //
    // Meshes and textures are watched from the components that name them, once
    // per frame; a prefab is named by nobody - it is dragged in from the
    // content browser - so there is no component to sweep. Scanned here
    // instead. A prefab created DURING the session is not picked up, which is
    // a smaller gap than the one it closes and is not worth a directory scan
    // every frame to fix.
    {
        std::error_code prefabScanError;
        const auto prefabRoot = std::filesystem::path("assets") / "prefabs";
        if (std::filesystem::is_directory(prefabRoot, prefabScanError)) {
            size_t watched = 0;
            for (const auto& entry : std::filesystem::directory_iterator(prefabRoot, prefabScanError)) {
                if (entry.is_regular_file(prefabScanError) && entry.path().extension() == ".prefab") {
                    m_assetWatcher.Watch(entry.path().generic_string());
                    ++watched;
                }
            }
            if (watched > 0) {
                SUPERSONIC_LOG_INFO("AssetWatcher")
                    << "Watching " << watched << " prefab(s) for changes.";
            }
        }
    }

    // Art hot reload. The plugin has been watched since hot reload shipped;
    // textures and meshes could not be, because nothing could un-cache them.
    m_assetWatcher.SetCallback([this](const std::string& path) {
        bool reloaded = false;
        if (m_renderer) {
            reloaded |= m_renderer->GetTextureRegistry().Invalidate(path);
            reloaded |= m_renderer->GetMeshRegistry().Invalidate("file:" + path);
        }

        // Materials re-read in place, keeping their id. MaterialSystem::Sync
        // copies the asset onto every component that names it EVERY frame, so
        // re-reading the asset is the whole reload - there is nothing else to
        // notify, and an entity picks the change up on the next frame whether
        // or not it was visible when the file changed.
        if (m_materialLibrary) reloaded |= m_materialLibrary->Reload(path);

        // Rigs and clips live in the same .gltf/.glb as the mesh, which is
        // already watched - so this path was already arriving here every time
        // an animator re-exported, and being used to invalidate the GPU mesh
        // and nothing else. The skeleton and the clips parsed the first time
        // the file was seen were kept for the life of the session.
        if (m_animationLibrary) reloaded |= m_animationLibrary->Reload(path);

        // Audio is the one that cannot simply drop a cache entry: a voice reads
        // the clip's samples from an audio thread, so the clip has to stop
        // being played before it stops existing.
        if (m_audioEngine) {
            reloaded |= AudioSystem::ReloadClip(m_registry, *m_audioEngine, path) > 0;
        }

        // And the COLLISION shape built from that model. Re-uploading the mesh
        // moves what you see and leaves what you hit where it was, which is the
        // worst way for this to be half-done: the object looks edited and
        // behaves as though it is not, and nothing says which of the two is
        // lying.
        reloaded |= ConvexHullCache::For(m_registry).Invalidate(path) > 0;

        // Prefabs are parsed once and kept, so a prefab edited on disk would
        // otherwise keep spawning the version the editor first read. Cheap
        // enough to do unconditionally: the cache is small and a path that is
        // not a prefab simply is not in it.
        PrefabSerializer::ClearCache();
        if (reloaded) {
            // RenderSystem re-resolves every renderable each frame, so dropping
            // the cache entry is the whole reload - the next SyncResources
            // re-reads the file and hands back a fresh id.
            SUPERSONIC_LOG_INFO("AssetWatcher") << path << " changed on disk; reloading.";
        }
    });

    // Published through the registry context, the way AudioSystem publishes its
    // engine, so ScriptEngine can reach it without a wider signature and a game
    // linking the library can reach it at all.
    m_registry.ctx().insert_or_assign<ContactTracker*>(&m_contactTracker);

    // The same way, and for the same reason. A game switching level has to be
    // able to say so from wherever its code lives - an EngineLayer, or a script
    // through the plugin ABI - and neither of those has ever been able to see
    // the editor. Published as a pointer rather than a value because clearing
    // the registry is exactly what a scene load does, and the object doing it
    // should not live inside the thing being cleared.
    m_registry.ctx().insert_or_assign<SceneManager*>(&m_sceneManager);

    // The two registries a game has to reach, and only those two.
    //
    // NOT the renderer. A game does not need to drive a frame - the engine does
    // that - but it does need to put geometry and pixels into one, and until
    // this line there was no way to. MeshRegistry::Replace and
    // TextureRegistry::ReplaceRGBA are public and documented for exactly this
    // use, and a layer could not see either of them: m_renderer is a private
    // unique_ptr on a class a game never holds.
    //
    // Anything that generates its world rather than loading it needs this.
    // Terrain built from a heightfield is a mesh nobody exported; fog of war is
    // a texture that changes every few frames; a minimap is both. Each was
    // blocked outright, not made awkward.
    //
    // Published after the renderer is constructed, which it is - the renderer is
    // built well above this, and these are references into it rather than copies
    // of it, so the lifetime is the renderer's and it outlives the registry.
    // The world-space shape layer, for the same reason: a game's effects are
    // emitted from a layer, and the renderer consumes and clears them once a
    // frame. Published as a pointer into a member the app owns, because an
    // immediate buffer must survive a scene load that clears the registry.
    m_registry.ctx().insert_or_assign<WorldShapes*>(&m_worldShapes);

    m_registry.ctx().insert_or_assign<UIImageStore*>(&m_renderer->GetUIImageStore());
    m_registry.ctx().insert_or_assign<MeshRegistry*>(&m_renderer->GetMeshRegistry());
    m_registry.ctx().insert_or_assign<TextureRegistry*>(&m_renderer->GetTextureRegistry());

    // Asset identities, before the first scene is read.
    //
    // A SCAN, not an import: this reads the sidecars that are already there and
    // writes nothing. A project that has never been imported scans to nothing,
    // every reference resolves by its path exactly as it always did, and the
    // only difference is a count. `--import-assets` is what mints.
    {
        const auto scanned = AssetDatabase::Instance().Scan("assets");
        if (scanned.ok) {
            SUPERSONIC_LOG_INFO("AssetDatabase")
                << "Assets: " << scanned.identified << " with an identity, "
                << scanned.unidentified << " without"
                << (scanned.orphaned > 0
                        ? ", " + std::to_string(scanned.orphaned) +
                              " orphaned sidecar(s) - run --import-assets to recover them"
                        : std::string())
                << "." << std::endl;
        }
    }

    // The demo scene is the EDITOR's starting point, and a game has its own.
    // Building it for one meant a game opened with a camera, a sun and eleven
    // props it never asked for, sitting in front of whatever it built itself.
    if (!m_manifest.isGame) initECS();

    // --scene wins over the manifest, and applies in the editor too: a smoke
    // test is only worth running against the scene you want to smoke test.
    const std::string startupScene =
        m_options.scenePath.empty() ? m_manifest.startupScene : m_options.scenePath;

    if (m_manifest.isGame || !m_options.scenePath.empty()) {
        // A game whose world comes from a LAYER has no scene file, and that is
        // a legitimate shape rather than a misconfiguration - Wolf Brigade
        // builds its board out of a simulation, not out of entities somebody
        // placed. Asking the serializer to open "" logged a parse failure and
        // an alarming line about falling back, on the startup path of a game
        // that was working correctly.
        const bool hasScene = !startupScene.empty();
        SerializationResult loaded{true, "No startup scene; the game builds its own world."};
        if (hasScene) loaded = SceneSerializer::Deserialize(m_registry, startupScene);
        SUPERSONIC_LOG_INFO("SupersonicApp") << loaded.message << std::endl;
        if (!loaded.ok) {
            SUPERSONIC_LOG_ERROR("SupersonicApp") << "Falling back to the built-in scene." << std::endl;
        } else if (hasScene) {
            // So the manager names the scene that is actually open. Otherwise
            // Save would write it over the default scene's file.
            m_sceneManager.AdoptLoaded(startupScene);
        }

        // Straight into Play: a game has no edit mode to be in, and without
        // this nothing would move, no script would run and no sound would play.
        const auto started = m_playMode.Play(m_registry);
        if (!started.ok) {
            SUPERSONIC_LOG_ERROR("SupersonicApp") << started.message << std::endl;
        }
    }

    setUpRecording(startupScene);
}

// Open the recording, or the file being replayed, once the scene is loaded.
//
// AFTER the load and after Play, deliberately: the clock a replay pins and the
// tick-zero hash it checks are both properties of the world the run starts
// from, and neither exists until the scene is in the registry.
void SupersonicApp::setUpRecording(const std::string& startupScene) {
    if (!m_options.replayPath.empty()) {
        auto loaded = std::make_unique<InputRecording>(
            InputRecording::Load(m_options.replayPath));
        if (!loaded->ok) {
            SUPERSONIC_LOG_ERROR("Replay") << loaded->error << std::endl;
            return;
        }

        // The scene is a warning rather than a refusal. A replay pointed at the
        // wrong scene is caught properly by the tick-zero hash below, which
        // knows what the world actually is; the path only knows what it was
        // called, and a project that has renamed a level would fail here for no
        // reason.
        if (!loaded->scenePath.empty() && loaded->scenePath != startupScene) {
            SUPERSONIC_LOG_WARN("Replay")
                << "recorded against " << loaded->scenePath << " but running "
                << startupScene << "." << std::endl;
        }

        // THE STEP IS TAKEN FROM THE FILE, not from the scene. The scene
        // authors a rate and the recording knows the exact float the run
        // actually used; if a level has been retuned since, replaying it at the
        // new rate would produce a different number of ticks per second and
        // diverge for a reason that has nothing to do with the bug.
        auto& clock = m_registry.ctx().contains<SimulationClock>()
                          ? m_registry.ctx().get<SimulationClock>()
                          : m_registry.ctx().emplace<SimulationClock>();
        clock.fixedDelta = loaded->fixedDelta;

        SUPERSONIC_LOG_INFO("Replay")
            << "Replaying " << loaded->ticks.size() << " tick(s) from "
            << m_options.replayPath << " at " << (1.0f / loaded->fixedDelta) << " Hz."
            << std::endl;

        m_replay = std::move(loaded);
        return;
    }

    if (!m_options.recordPath.empty()) {
        m_recording = std::make_unique<InputRecording>();
        m_recording->scenePath = startupScene;

        const auto* clock = m_registry.ctx().find<SimulationClock>();
        m_recording->fixedDelta = clock && clock->fixedDelta > 0.0f ? clock->fixedDelta
                                                                    : kDefaultGameTick;

        SUPERSONIC_LOG_INFO("Record")
            << "Recording " << startupScene << " to " << m_options.recordPath << "."
            << std::endl;
    }
}

// One tick's worth of recording or replaying, at the top of the tick.
//
// Returns false when a replay has run out of recorded ticks, which is what
// stops the loop rather than letting it carry on feeding the run nothing and
// calling the result a reproduction.
bool SupersonicApp::stepRecording(uint64_t tick) {
    // The checkpoint is taken BEFORE the tick runs, so checkpoint N is the
    // world after N ticks - which makes checkpoint zero the state the run
    // started from, and that is the one worth having. A mismatch there says
    // "this is not the scene that was recorded" instead of letting the run
    // diverge for four thousand ticks and reporting the symptom.
    const bool atCheckpoint = (tick % InputRecording::kDefaultCheckpointInterval) == 0;

    if (m_replay) {
        if (tick >= m_replay->ticks.size()) return false;

        if (atCheckpoint) {
            if (const ReplayCheckpoint* expected = m_replay->CheckpointAt(tick)) {
                const uint64_t actual = StateHash::Compute(m_registry);
                if (actual != expected->hash && !m_replayDiverged) {
                    // Only the FIRST. After a divergence every later checkpoint
                    // disagrees as well, and the tick worth reporting is the one
                    // where the two runs stopped being the same.
                    m_replayDiverged = true;
                    m_replayDivergedAtTick = tick;
                    m_replayExpectedHash = expected->hash;
                    m_replayActualHash = actual;
                    SUPERSONIC_LOG_ERROR("Replay")
                        << "diverged at tick " << tick << ": expected " << expected->hash
                        << ", got " << actual << "." << std::endl;
                }
            }
        }

        const Input::TickInput& recorded = m_replay->ticks[static_cast<size_t>(tick)];
        Input::BeginReplayedTick(recorded);
        UIInput::BeginReplayedTickClicks(m_registry, recorded.clicked);
        return true;
    }

    if (m_recording) {
        if (atCheckpoint) {
            m_recording->checkpoints.push_back(
                ReplayCheckpoint{tick, StateHash::Compute(m_registry)});
        }

        // Captured AFTER BeginTickInput and BeginTickClicks, which is what
        // decides the edges this tick owns, and before anything reads them.
        Input::TickInput captured = Input::CaptureTickInput();
        captured.clicked = UIInput::ClicksThisTick(m_registry);
        m_recording->ticks.push_back(std::move(captured));
    }

    return true;
}

void SupersonicApp::applyPendingSceneLoad() {
    if (!m_sceneManager.HasPending()) return;

    SerializationResult result{};
    if (!m_sceneManager.ApplyPending(m_registry, result)) return;

    if (result.ok) {
        SUPERSONIC_LOG_INFO("SupersonicApp") << result.message;
    } else {
        // A failed load leaves the previous scene open - Deserialize parses
        // before it touches the registry - so this is a report, not a crash.
        SUPERSONIC_LOG_ERROR("SupersonicApp") << result.message;
    }

    // The editor's reaction to a load it may not have asked for: its selection
    // and its undo stack both describe a scene that is no longer open.
    if (m_editorLayer) {
        m_editorLayer->OnSceneLoaded(m_registry, result);
    }

    // The new scene starts from a whole tick, not from part of one.
    //
    // Whatever the accumulator was holding was time owed to the level being
    // left, and the tick loop breaks out the moment a load is queued precisely
    // so that time is not spent there. Carrying it across would spend it in the
    // new scene instead - a level that begins a fraction of a tick in, by an
    // amount that depends on when during the frame the previous level asked to
    // leave.
    //
    // Deliberately NOT counted as dropped time. droppedSeconds means "this
    // machine could not keep up", which a game can read and act on; a scene
    // change is not that, and folding it in would make every level transition
    // look like a performance problem.
    m_physicsAccumulator = 0.0f;
}

void SupersonicApp::PushLayer(std::unique_ptr<EngineLayer> layer) {
    if (!layer) return;
    SUPERSONIC_LOG_INFO("SupersonicApp") << "Attached layer '" << layer->Name() << "'.";
    m_layers.Push(std::move(layer), m_registry);
}

SupersonicApp::~SupersonicApp() {
    // First, and before the registry is destroyed: a layer holding entity
    // handles can still use them in OnDetach, and one detached afterwards
    // could not.
    m_layers.Clear(&m_registry);

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
    // fall on - the grid is a shader overlay, not geometry. It is also the
    // scene's floor, now that there is no unconditional one: everything that
    // falls in this scene falls onto a collider that exists.
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

    // A slab UNDER the visible surface: the mesh is a flat plane with no
    // thickness, and a collider with a zero half extent is a degenerate box the
    // narrowphase cannot separate anything from. Half a unit down and half a
    // unit thick puts its top face exactly on the plane, where the eye expects
    // the floor to be, and leaves something solid behind it.
    auto& groundCollider = m_registry.emplace<BoxColliderComponent>(ground);
    groundCollider.center = glm::vec3(0.0f, -0.5f, 0.0f);

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

    // Every frame's sample, not a running sum.
    //
    // A mean is the wrong statistic for "where does the frame go". It is the
    // one number a single contended frame can move arbitrarily far, and on a
    // machine doing anything else at the time it does: the editor's UI zone was
    // measured at 1.4 ms and at 16.5 ms across identical runs of the same
    // binary, while the render zones beside it did not move. Deciding what to
    // optimise from that is deciding from whatever else the machine was doing.
    //
    // The median is what the frame usually costs and the maximum is the worst
    // one seen, which is the pair that answers both questions anyone asks of a
    // profile. Kept only under --frames, where the count is bounded by
    // construction.
    std::vector<std::array<double, Profiler::kZoneCount>> zoneSamples;
    if (m_options.maxFrames > 0) {
        zoneSamples.reserve(static_cast<size_t>(m_options.maxFrames));
    }

    // AND WHAT THE FRAME COST IN QUANTITIES, not only in milliseconds.
    //
    // A millisecond threshold is flaky across machines and gets deleted within
    // a month. "This scene submits one draw call per drawable" is a property,
    // and when instancing lands the number drops and this is what records it.
    //
    // Sampled here rather than assembled at the end because the renderer zeroes
    // its counters at the top of every DrawFrame - the totals do not exist
    // anywhere by the time the loop is over.
    //
    // The counts reach the editor's statistics panel too, and that is exactly
    // the problem this solves: a measurement no script can read is not one CI
    // can act on, which is the same argument the profiler report above makes
    // for itself.
    std::vector<RenderSystem::Stats> statsSamples;
    if (m_options.maxFrames > 0) {
        statsSamples.reserve(static_cast<size_t>(m_options.maxFrames));
    }

    // Or until a game asks. Both leave by the same path, so there is exactly
    // one shutdown rather than one for the window and another for the game.
    while (!m_window->ShouldClose() && !Application::QuitRequested()) {
        // Fold the frame that just finished into the totals. This has to happen
        // before the exit test, not after the increment, or the final frame is
        // counted in the divisor and never added to the sum.
        if (frame > 0 && m_options.maxFrames > 0) {
            std::array<double, Profiler::kZoneCount> sample{};
            for (std::size_t i = 0; i < Profiler::kZoneCount; ++i) {
                sample[i] = Profiler::Milliseconds(static_cast<ProfileZone>(i));
            }
            zoneSamples.push_back(sample);
            statsSamples.push_back(m_renderer->GetRenderStats());
        }

        if (m_options.maxFrames > 0 && frame >= m_options.maxFrames) {
            SUPERSONIC_LOG_INFO("SupersonicApp") << "Rendered " << frame
                      << " frame(s) as requested; exiting." << std::endl;

            // Captured here, after the last DrawFrame and before teardown, so
            // the PNG is the frame that was actually presented rather than
            // whatever survives shutdown.
            if (!m_options.screenshotPath.empty()) {
                std::string error;
                const auto& offscreen = m_editorLayer->GetOffscreen();
                const bool ok = ScreenCapture::WritePng(
                    *m_vulkanDevice, m_renderer->GetCommandPool(),
                    offscreen.GetPresentedImage(),
                    offscreen.GetWidth(), offscreen.GetHeight(),
                    m_options.screenshotPath, error);
                if (ok) {
                    SUPERSONIC_LOG_INFO("SupersonicApp")
                        << "Wrote " << m_options.screenshotPath << " ("
                        << offscreen.GetWidth() << "x" << offscreen.GetHeight() << ").";
                } else {
                    SUPERSONIC_LOG_ERROR("SupersonicApp") << "Screenshot failed: " << error;
                }
            }

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
            // The build configuration, because without it the numbers mean
            // nothing. A Debug build of this engine is between three and two
            // hundred times slower per zone than a Release one - EnTT's lookups
            // and GLM's operators are entirely unoptimised - so a Debug profile
            // ranks the frame in an order a shipped game never sees. Reporting
            // a measurement without saying what was measured is the same defect
            // as reporting a mean and calling it typical.
#ifdef NDEBUG
            const char* buildConfig = "release";
#else
            const char* buildConfig = "DEBUG - see ARCHITECTURE section 4d";
#endif
            std::cout << "[Profiler] CPU cost per frame over "
                      << zoneSamples.size() << " frame(s), median and worst ("
                      << buildConfig << "):" << std::endl;

            std::vector<double> column;
            column.reserve(zoneSamples.size());
            for (std::size_t i = 0; i < Profiler::kZoneCount; ++i) {
                column.clear();
                double worst = 0.0;
                std::size_t worstFrame = 0;
                for (std::size_t f = 0; f < zoneSamples.size(); ++f) {
                    column.push_back(zoneSamples[f][i]);
                    // WHICH frame, not only how bad. A twenty-millisecond
                    // worst case on frame one is a scene loading; the same
                    // number on frame four hundred is a stall in play, and the
                    // two want completely different work.
                    if (zoneSamples[f][i] > worst) {
                        worst = zoneSamples[f][i];
                        worstFrame = f + 1;
                    }
                }
                if (column.empty()) continue;

                // nth_element, not a full sort: the median is the only order
                // statistic wanted and this runs once per zone at exit.
                const std::size_t middle = column.size() / 2;
                std::nth_element(column.begin(), column.begin() + middle, column.end());
                const double median = column[middle];

                if (median < 0.005 && worst < 0.05) continue;
                std::cout << "  " << Profiler::Name(static_cast<ProfileZone>(i))
                          << ": " << median << " ms (worst " << worst
                          << " on frame " << worstFrame << ")" << std::endl;
            }

            // The quantities. Median and worst, exactly as above and for the
            // same reason: one contended frame moves a mean arbitrarily far,
            // and a scene load is not what the frame usually costs.
            if (!statsSamples.empty()) {
                std::cout << "[Counts] Per frame over " << statsSamples.size()
                          << " frame(s), median and worst:" << std::endl;

                struct Counter {
                    const char* name;
                    uint32_t RenderSystem::Stats::*field;
                };
                static constexpr Counter kCounters[] = {
                    { "Drawables drawn",     &RenderSystem::Stats::drawn },
                    { "Drawables culled",    &RenderSystem::Stats::culled },
                    { "Blended drawables",   &RenderSystem::Stats::transparentDrawn },
                    { "Draw calls",          &RenderSystem::Stats::drawCalls },
                    { "Particles",           &RenderSystem::Stats::particlesDrawn },
                    { "Shadow casters",      &RenderSystem::Stats::shadowDrawn },
                    { "Shadow culled",       &RenderSystem::Stats::shadowCulled },
                    { "Skinned matrices",    &RenderSystem::Stats::skinnedMatrices },
                    { "Shadow passes saved", &RenderSystem::Stats::shadowPassesSkipped },
                };

                std::vector<uint32_t> counts;
                counts.reserve(statsSamples.size());
                for (const Counter& counter : kCounters) {
                    counts.clear();
                    uint32_t worst = 0;
                    for (const RenderSystem::Stats& sample : statsSamples) {
                        counts.push_back(sample.*counter.field);
                        worst = std::max(worst, sample.*counter.field);
                    }

                    const std::size_t middle = counts.size() / 2;
                    std::nth_element(counts.begin(), counts.begin() + middle, counts.end());

                    // A counter that stayed at zero all run is left out, as a
                    // zone that cost nothing is. Printing nine zeroes for a
                    // scene with no shadows and no particles buries the two
                    // numbers somebody came to read.
                    if (counts[middle] == 0 && worst == 0) continue;

                    std::cout << "  " << counter.name << ": " << counts[middle]
                              << " (worst " << worst << ")" << std::endl;
                }
            }
            break;
        }
        ++frame;
        Profiler::BeginFrame();

        // Self-check for deferred destruction, only under --frames.
        //
        // Invalidating a live texture and a live mesh mid-run is the exact
        // situation the deferred queue exists for: the image being dropped is
        // still named by command buffers submitted on the previous two frames.
        // Doing it here, under validation, is the only way to find out whether
        // the queue actually holds them long enough - there is no way to test
        // this without a device, so it rides on the headless smoke run.
        if (m_options.maxFrames > 0 && frame == m_options.maxFrames / 2) {
            const bool tex = m_renderer->GetTextureRegistry().Invalidate(
                "assets/textures/uv_grid.png");
            const bool mesh = m_renderer->GetMeshRegistry().Invalidate("primitive:Sphere");
            SUPERSONIC_LOG_INFO("SelfCheck")
                << "Mid-run invalidate: texture=" << (tex ? "dropped" : "absent")
                << ", mesh=" << (mesh ? "dropped" : "absent")
                << "; both will be re-acquired next frame.";

            // Drive something through the transparent pipeline as well. It has
            // its own blend state, its own depth-write setting and its own
            // sort, and none of that is reachable from a test without a device
            // - so the headless run is where it gets exercised.
            size_t madeTransparent = 0;
            for (auto [entity, material] : m_registry.view<MaterialComponent>().each()) {
                if (madeTransparent >= 2) break;
                material.transparent = true;
                material.albedoColor.a = 0.5f;
                ++madeTransparent;
            }
            SUPERSONIC_LOG_INFO("SelfCheck")
                << "Marked " << madeTransparent << " material(s) transparent; "
                << "the blended pass draws from the next frame on.";

            // And drive one material through the emissive path above 1.0, so
            // the bright pass has something to find. Like the blend state, this
            // is only reachable with a device.
            size_t madeEmissive = 0;
            for (auto [entity, material] : m_registry.view<MaterialComponent>().each()) {
                if (material.transparent) continue;
                material.emissiveColor = glm::vec3(1.0f, 0.55f, 0.15f);
                material.emissiveStrength = 4.0f;
                ++madeEmissive;
                break;
            }
            SUPERSONIC_LOG_INFO("SelfCheck")
                << "Set " << madeEmissive << " material(s) emissive above 1.0.";
        }

        m_window->PollEvents();

        // Who owns the pointer this frame, decided before anything reads it.
        //
        // The editor's veto, and it is not politeness. Under a locked pointer
        // GLFW reports unbounded virtual coordinates, and ImGui's GLFW backend
        // feeds those straight into io.MousePos without checking the mode - so
        // every panel's hover test goes wrong at once. A captured mouse is
        // therefore only allowed while a game is actually running and the
        // viewport has focus; the rest of the time the request is held, not
        // lost, and comes back the moment play resumes.
        //
        // A packaged game has no viewport to focus, and IsViewportFocused
        // reports true for it, so this reduces to "while playing".
        //
        // Escape gives it back too, and only in the EDITOR. Getting out of a
        // captured game there otherwise means clicking Stop, which needs the
        // pointer you do not have - escapable by alt-tabbing, which is a poor
        // answer to "put the editor back". Latched until the viewport is
        // clicked again, because a game re-asks for the lock every frame and a
        // one-frame release would be swallowed instantly.
        //
        // Not in a shipped game: there Escape belongs to whatever the game
        // wants it for, and losing the window is still the guaranteed way out.
        if (!m_manifest.isGame) {
            // Not while a name is being typed: Escape abandons a focused field,
            // and the field consumes it a frame before this sees it. Without
            // the guard, backing out of a text box would also drop the editor
            // out of mouse-look. The raw key query is deliberately not gated by
            // the text veto, precisely so this check still works.
            if (!Input::TextCaptureActive() && Input::WasKeyPressed(Key::Escape)) {
                m_escapeReleasedCursor = true;
            }
            if (m_editorLayer->IsViewportHovered() &&
                Input::IsMouseButtonDown(MouseButton::Left)) {
                m_escapeReleasedCursor = false;
            }
        }

        Input::SuppressCursorCapture(m_escapeReleasedCursor || m_playMode.IsEditing() ||
                                     !m_editorLayer->IsViewportFocused());

        // Who has the keyboard, decided the same way and in the same place.
        //
        // An assignment every frame rather than a flag something raises and
        // something else clears: a clear that is ever missed - a panel
        // collapsed, a render path that skips the HUD - would suppress every
        // key forever with nothing on screen to explain it. Here there is no
        // clear to miss.
        //
        // Read from the components rather than pushed by UIInput, so that file
        // stays free of global state as well as free of a window, and the focus
        // rules can be tested without resetting anything.
        Input::SetTextCaptureActive(UIInput::AnyTextFieldFocused(m_registry));
        InputPolling::ApplyCursorMode(*m_window);

        const double currentTime = glfwGetTime();
        const float rawDelta = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        // A constant delta when asked for, so a run reproduces. See
        // LaunchOptions::fixedDelta: the simulation is already deterministic
        // given a tick count, and this is what pins the tick count.
        //
        // Only the SIMULATION is pinned. The profiler still measures real
        // elapsed time per zone, so a fixed-step run still says truthfully how
        // long the work took - it just always does the same amount of it.
        const float deltaTime = m_options.fixedDelta > 0.0f
                                    ? m_options.fixedDelta
                                    : std::clamp(rawDelta, 0.0f, kMaxFrameDelta);

        // TIME THIS CLAMP THREW AWAY, which is the older of the two leaks and
        // the easier to miss. A frame longer than kMaxFrameDelta is treated as
        // a hitch and its excess never reaches the accumulator at all - so
        // fixing only the tick loop's own drop would close the visible half and
        // leave this one, and a run that lost a second here would still report
        // that it lost nothing.
        const double clampedAway =
            (m_options.fixedDelta <= 0.0f && rawDelta > kMaxFrameDelta)
                ? static_cast<double>(rawDelta - kMaxFrameDelta)
                : 0.0;

        // Swap in a rebuilt script plugin. Cheap: one stat unless it changed.
        m_hotReload->Poll();

        // Same cadence and same reasoning as the plugin poll above: a stat per
        // watched path, and only a changed write time costs anything.
        m_assetWatcher.Poll();
        if (m_options.maxFrames > 0 && frame == 2) {
            SUPERSONIC_LOG_INFO("SelfCheck") << "Watching "
                << m_assetWatcher.WatchedCount() << " asset path(s) for changes.";
        }

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
            // Both gates, because they cover different things and this camera
            // reads Input's RAW key snapshot, which no veto touches - only
            // actions and axes are silenced while a name is being typed.
            // io.WantTextInput knows about ImGui's own widgets;
            // TextCaptureActive knows about the HUD's, and a packaged game has
            // only the second kind - so without it, typing a name into a field
            // flies the camera forward, unconditionally, in exactly the shipped
            // game this was built for.
            //
            // Whether the camera may be flown AT ALL is the camera's own answer
            // now, and it is checked inside Update rather than here: these two
            // say who owns the device this frame, which is a different question
            // from whether this scene wants a fly camera.
            CameraSystem::Update(m_registry, deltaTime,
                                 m_editorLayer->IsViewportFocused() && !io.WantTextInput &&
                                     !Input::TextCaptureActive(),
                                 m_editorLayer->IsViewportHovered());
        }

        // Gameplay only runs in play mode. The editor used to simulate
        // permanently, so a scene could never be authored and then tried.
        const bool stepping = m_playMode.ConsumeSingleStep();

        // Nothing is going to consume this frame's input edges, so drop them.
        //
        // The latch that hands a press to exactly one tick is only correct
        // while a tick is coming to collect it. In the editor, while paused, and
        // while the time-travel debugger is scrubbing, none is - and the
        // presses queued up instead of expiring, so the first tick after Play
        // was handed every key touched since the last one. A character jumping
        // and firing on frame one from input given a minute earlier, in the
        // editor, before the game started.
        //
        // Deliberately outside the rewind check as well as the play check,
        // because a scrub runs no ticks either.
        if (!(m_playMode.ShouldSimulate() || stepping) || TimeTravelDebugger::IsRewinding()) {
            Input::DiscardPendingTickInput();
        }

        if (m_playMode.ShouldSimulate() || stepping) {
        if (!TimeTravelDebugger::IsRewinding()) {
            // THE GAME TICK IS THE OUTER LOOP and physics substeps inside it.
            //
            // The two used to be one loop at one rate, so "how often does the
            // world think" and "how often does the solver integrate" were the
            // same question. They are not: HUSK simulates at 20 Hz, and under
            // the old arrangement it would have paid for 60 and had no way to
            // ask for anything else.
            //
            // The substep count keeps the solver at its own rate whatever the
            // game chooses - a 20 Hz tick runs three 1/60 physics steps, a
            // 60 Hz tick runs one - so changing the game rate does not change
            // how physics behaves, which would be a very quiet way to make
            // every collision in a project feel different.
            auto& clock = m_registry.ctx().contains<SimulationClock>()
                              ? m_registry.ctx().get<SimulationClock>()
                              : m_registry.ctx().emplace<SimulationClock>();
            const float gameTick = clock.fixedDelta > 0.0f ? clock.fixedDelta : kDefaultGameTick;

            const int physicsSubsteps = std::max(
                1, static_cast<int>(std::lround(gameTick / kFixedPhysicsStep)));
            const float physicsStep = gameTick / static_cast<float>(physicsSubsteps);

            clock.droppedSeconds += clampedAway;

            m_physicsAccumulator += deltaTime;
            int steps = 0;

            // What the whole frame touched, for the editor's contact count.
            // Separate from the per-tick list below, because the two want
            // opposite things and used to be one vector serving both badly.
            m_frameContacts.clear();

            while (m_physicsAccumulator >= gameTick && steps < kMaxPhysicsStepsPerFrame) {
                // THIS TICK'S CONTACTS, cleared per tick rather than per frame.
                //
                // The clear used to sit outside this loop while the tracker
                // below ran inside it, so the second tick of a frame was handed
                // the first tick's contacts as well as its own - and Enter,
                // Stay and Exit became a function of how many ticks the frame
                // happened to run. A pair that touched on one tick and came
                // apart on the next never reported Exit at all if both ticks
                // fell in one frame, and did report it if they did not.
                //
                // That is the same class of bug as scripts running on the frame
                // delta, in the same place, and it was carried over deliberately
                // when scripts moved inside the tick - the note that used to be
                // here said "frame-cumulative rather than per tick, which is
                // what it already was". It was harmless while the tracker was
                // read once a frame by the editor. It stopped being harmless
                // when a script started reading it inside a tick.
                m_contacts.clear();

                // Inside the loop, before anything moves. See the note on
                // BeginTick for why once per frame would look like it worked.
                InterpolationSystem::BeginTick(m_registry);

                // Hand this tick the input edges nothing has consumed yet. A
                // frame that runs no tick would otherwise lose the press, and a
                // frame that runs three would report it to all three.
                Input::BeginTickInput();

                // And the same for clicks, which are computed once per frame by
                // the UI pass and read inside the tick by any script that has a
                // button. Separate call because a click belongs to an entity
                // and Input knows nothing about the registry.
                UIInput::BeginTickClicks(m_registry);

                // Write this tick's input down, or feed it back in. AFTER the
                // two latches above, which decide what this tick owns, and
                // before anything reads them.
                //
                // A replay that has run out of recorded ticks stops the loop
                // here rather than carrying on: a run fed nothing would keep
                // simulating, keep hashing, and eventually be described as a
                // reproduction of something it stopped following.
                if (!stepRecording(clock.tick)) break;

                for (int sub = 0; sub < physicsSubsteps; ++sub) {
                    SUPERSONIC_PROFILE(Physics);
                    PhysicsSystem::Update(m_registry, physicsStep, &m_stepContacts);
                    if (sub + 1 < physicsSubsteps) {
                        m_contacts.insert(m_contacts.end(),
                                          m_stepContacts.begin(), m_stepContacts.end());
                    }
                }

                // The simulation's own clock, advanced once per TICK and never
                // from the frame delta. Everything that needs to know what time
                // it is in the world reads this; anything reading a wall clock
                // instead cannot be replayed.
                ++clock.tick;

                // Sprite flipbooks, on the tick and not on the frame.
                //
                // Per frame this would animate at whatever rate the display
                // was keeping up at, which is the defect SimulationClock was
                // built to end, and it would sit outside the replay entirely.
                // Before the layers, so a game that reads a sprite's frame in
                // its own FixedUpdate reads this tick's rather than the last
                // one's - the same ordering argument that puts the layers after
                // physics.
                {
                    SUPERSONIC_PROFILE(Sprites);
                    SpriteAnimationSystem::Update(m_registry, gameTick);
                }

                // A game's simulation, on the game's tick. AFTER physics, so a
                // tick reads the positions this step just produced rather than
                // the previous one's.
                {
                    SUPERSONIC_PROFILE(GameLayers);
                    m_layers.FixedUpdate(m_registry, gameTick);
                }

                m_contacts.insert(m_contacts.end(), m_stepContacts.begin(), m_stepContacts.end());

                // The tick is complete, so fold the whole of it into the
                // frame's running total - which is what the editor shows, and
                // which would blink whenever a frame ran more than one tick if
                // it reported only the last. Appended wholesale here rather
                // than mirrored at each of the inserts above, so there is one
                // place that decides what a tick's contacts are.
                m_frameContacts.insert(m_frameContacts.end(),
                                       m_contacts.begin(), m_contacts.end());

                // SCRIPTS RUN ON THE TICK, which is the whole point of the
                // exercise. They used to run once per frame on the frame delta,
                // so a script's motion was a function of how fast the display
                // was keeping up - the exact bug SimulationClock was built to
                // end, surviving in the one place a game actually writes its
                // logic.
                //
                // Before the tracker, so "what did I touch" is answered about
                // the contacts this frame has produced so far rather than the
                // previous frame's. Frame-cumulative rather than per tick,
                // which is what it already was.
                m_contactTracker.Update(m_contacts);

                { SUPERSONIC_PROFILE(Scripts); ScriptEngine::Update(m_registry, gameTick); }

                // Immediately after, and inside the same tick. Spawning or
                // destroying during the script pass invalidates the iteration
                // the pass is in the middle of, and a spawn queued by one tick
                // must exist before the next one runs rather than appearing at
                // the end of the frame.
                ScriptEngine::ApplyPendingCommands(m_registry);

                // What this tick produced is what the next frames interpolate
                // towards. After the scripts, so it captures the tick's whole
                // result rather than physics' half of it.
                InterpolationSystem::EndTick(m_registry);

                // The recorded input stops standing in for the devices here,
                // and not one line later: everything after this point in the
                // frame runs per frame, and the editor camera reading a
                // replayed mouse delta would take the view away from whoever is
                // watching the replay.
                Input::EndReplayedTick();

                m_physicsAccumulator -= gameTick;
                ++steps;

                // A TICK ASKED FOR A DIFFERENT SCENE, so stop ticking this one.
                //
                // The load itself still happens at frame scope, below BuildUI,
                // because clearing and refilling the registry while a panel is
                // iterating views over it is the crash that placement is there
                // to avoid. What was wrong was carrying on: the load was
                // requested by a tick and the frame kept running the remaining
                // ticks against the scene being left, so how much extra
                // simulation a level transition ran depended on how far behind
                // the machine happened to be.
                //
                // On a fast machine, none. On one running late, up to four more
                // ticks of a level the game has already decided to leave -
                // which is enough for the player to be killed by something in a
                // scene they were no longer in.
                if (m_sceneManager.HasPending()) break;
            }

            // What the loop could not run, DROPPED AND COUNTED rather than
            // dropped in silence.
            //
            // There is no third option: catching up under sustained load never
            // catches up, and this is the guard against that. What was wrong
            // before was not the drop but that nothing recorded it - a machine
            // that could not keep up ran every mission timer short and said
            // nothing to anyone.
            if (steps == kMaxPhysicsStepsPerFrame && m_physicsAccumulator > 0.0f) {
                clock.droppedSeconds += static_cast<double>(m_physicsAccumulator);
                m_physicsAccumulator = 0.0f;
            }

            // HOW FAR INTO THE NEXT TICK THIS FRAME IS. Written after the loop,
            // because it is exactly the remainder the loop could not consume.
            clock.alpha = gameTick > 0.0f
                              ? std::clamp(m_physicsAccumulator / gameTick, 0.0f, 1.0f)
                              : 0.0f;

            // And the frame is drawn between the last two ticks. After the loop
            // and after alpha, so it uses the remainder this frame arrived at.
            InterpolationSystem::Apply(m_registry, clock.alpha);

            { SUPERSONIC_PROFILE(Audio);     AudioSystem::Update(m_registry, *m_audioEngine, deltaTime); }
            { SUPERSONIC_PROFILE(Animation); AnimationSystem::Advance(m_registry, *m_animationLibrary, deltaTime); }
            { SUPERSONIC_PROFILE(Particles); ParticleSystem::Update(m_registry, deltaTime); }

            // The per-frame half, after the engine's own systems and before the
            // world transforms are resolved below - so a layer that moves
            // something has it rendered this frame rather than next.
            { SUPERSONIC_PROFILE(GameLayers); m_layers.Update(m_registry, deltaTime); }

            // THE EDITOR ONLY, and it was not gated at all.
            //
            // This is a scrubbing tool: it exists so somebody watching the
            // editor can drag a slider back through the last twenty seconds,
            // and there is no slider in a packaged game. But it was gated on
            // play mode rather than on being the editor, and a packaged game is
            // ALWAYS in play mode - so every shipped build paid for it, every
            // frame, with nothing able to read the result.
            //
            // What it costs is not small. EntityStateSnapshot is 68 bytes once
            // padded and MAX_HISTORY_FRAMES is 1200, so a scene of eight
            // thousand entities holds 653 MB of history a player cannot reach,
            // on top of an O(N) walk with a try_get each and 1200 live vectors
            // being trimmed from the front.
            if (!m_manifest.isGame) {
                TimeTravelDebugger::RecordFrame(m_registry, static_cast<float>(currentTime));
            }
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
        m_editorLayer->SetAnimationLibrary(m_animationLibrary.get());
        m_editorLayer->SetRenderStats(m_renderer->GetRenderStats());
        // The FRAME's, not the tick's: a count that showed only the last tick
        // would flicker on any frame that ran more than one.
        m_editorLayer->SetContacts(m_frameContacts);
        m_editorLayer->SetScriptHostInfo(m_hotReload->IsLoaded(),
                                         m_hotReload->GetStatus(),
                                         m_hotReload->GetReloadCount());
        { SUPERSONIC_PROFILE(EditorUI);    m_editorLayer->BuildUI(m_registry, *m_window); }

        // After BuildUI, not inside it. A queued scene load clears and refills
        // the registry, and BuildUI runs while every panel is iterating views
        // over it - performing the load there invalidates what the caller is
        // walking. This is the same reason the viewport resize is deferred to
        // the top of the frame.
        //
        // Driven here rather than by the editor because the request no longer
        // has to come from one: a layer or a script can queue a load, and in a
        // packaged game neither the menu nor the level that asked for it knows
        // the editor exists.
        applyPendingSceneLoad();

        // A load replaces every entity, so last frame's pairs describe a scene
        // that no longer exists: carrying them would report an Exit for handles
        // that have been recycled into different entities. Clearing in edit mode
        // generally is the same argument - nothing simulates there, so entering
        // Play must start from no contacts rather than from whatever was
        // touching when Stop was pressed.
        if (m_playMode.IsEditing()) {
            m_contactTracker.Clear();
        }

        // Clicks nothing is going to consume, dropped DOWN HERE rather than
        // beside the keyboard's equivalent at the top of the frame.
        //
        // The two look like the same thing and are not, because the two kinds
        // of input are computed at opposite ends of the frame. Devices are
        // polled before the tick loop, so a discard placed there sees this
        // frame's key edges. Clicks are computed by the UI pass inside BuildUI,
        // which runs AFTER the tick loop - so the same discard placed there saw
        // only the PREVIOUS frame's, and the last click made before Play was
        // pressed survived into the first tick of the run. Which is the bug the
        // latch was added to fix, arriving by a different route.
        //
        // Same every-frame assignment as the tracker above, and for the reason
        // written there: a clear that is ever missed leaves a click queued with
        // nothing on screen to explain why the game did something on its first
        // tick.
        if (!m_playMode.ShouldSimulate()) {
            UIInput::DiscardPendingClicks(m_registry);
        }

        // Re-published because a scene load clears the registry, and a stale
        // context entry is worse than a missing one.
        m_registry.ctx().insert_or_assign<ContactTracker*>(&m_contactTracker);
        { SUPERSONIC_PROFILE(ImGuiRender); ImGui::Render(); }

        // Again, because the editor may have moved, reparented or created
        // entities. Rendering reads world matrices, so they must reflect what
        // the user just did rather than lagging a frame behind it.
        { SUPERSONIC_PROFILE(Transform); TransformSystem::UpdateWorldTransforms(m_registry); }

        // Shared materials resolve onto their components before the renderer
        // reads them, so an edit to one asset shows on every entity using it in
        // the same frame.
        MaterialSystem::Sync(m_registry, *m_materialLibrary);

        // AFTER Sync, and per frame in BOTH modes.
        //
        // After, because Sync copies a shared asset's numbers over every entity
        // using it, and a sprite's cell is per entity by design - the comment on
        // MaterialComponent::uvScale says so in as many words, since twenty
        // flames share one material and each is on its own frame. Running this
        // first would leave the order deciding whether a flipbook survives being
        // linked to an asset.
        //
        // In both modes, because the clock is gated on play and showing a cell
        // is not. Without this an authored sprite drew its whole sheet in the
        // editor until Play was pressed, which is the thing the orthographic
        // viewport exists to prevent.
        SpriteAnimationSystem::Apply(m_registry);

        // The scene's look, handed to the pass that applies it. Pushed every
        // frame rather than on change: the offscreen target and its bloom pass
        // are rebuilt whenever the viewport resizes, so a value written once
        // would be lost the first time the panel edge is dragged.
        if (auto* bloom = m_editorLayer->GetOffscreen().GetBloom()) {
            const auto* rendering = m_registry.ctx().find<RenderSettings>();
            BloomPass::Settings applied;
            if (rendering) {
                applied.threshold = rendering->bloomThreshold;
                applied.softKnee = rendering->bloomSoftKnee;
                applied.intensity = rendering->bloomIntensity;
                applied.exposure = rendering->exposure;
            }
            bloom->SetSettings(applied);
        }

        // Mesh and texture uploads submit their own transfers, so they happen
        // here rather than mid-recording.
        // Register whatever the scene currently names, so a path that arrives
        // by drag-and-drop or by loading a scene starts being watched without
        // anyone remembering to say so. Watch() returns immediately for a path
        // it already knows, so this is a hash lookup per asset per frame.
        for (auto [entity, mesh] : m_registry.view<MeshComponent>().each()) {
            if (!mesh.filePath.empty()) m_assetWatcher.Watch(mesh.filePath);
        }
        for (auto [entity, material] : m_registry.view<MaterialComponent>().each()) {
            if (!material.albedoTexturePath.empty()) m_assetWatcher.Watch(material.albedoTexturePath);
            if (!material.normalTexturePath.empty()) m_assetWatcher.Watch(material.normalTexturePath);
            // Every texture a material names, or editing that one and saving it
            // does nothing at all: the watcher never fires, so Invalidate never
            // runs, so the registry's generation never moves, so the resource
            // signature never changes and SyncResources never re-acquires it.
            // The whole chain is silent from the first missing line.
            if (!material.ormTexturePath.empty()) m_assetWatcher.Watch(material.ormTexturePath);

            // And the shared asset itself. The three paths above are the
            // RESOLVED ones Sync copied out of it, so without this line editing
            // a .material in a text editor changes nothing until a restart -
            // and worse, editing it to name a DIFFERENT texture leaves the old
            // texture watched and the new one not.
            if (!material.materialPath.empty()) m_assetWatcher.Watch(material.materialPath);
        }

        // And the sounds. Iterating on a footstep meant restarting the editor
        // for exactly the same reason a material did.
        for (auto [entity, source] : m_registry.view<AudioSourceComponent>().each()) {
            if (!source.soundFile.empty()) m_assetWatcher.Watch(source.soundFile);
        }

        // A hull collider that names its own source. When it names none it is
        // built from the entity's own mesh, which the loop above already
        // watches - so only this case was outside everything.
        for (auto [entity, hull] : m_registry.view<ConvexHullColliderComponent>().each()) {
            if (!hull.sourcePath.empty()) m_assetWatcher.Watch(hull.sourcePath);
        }

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

        // Timed from inside, in four parts. One zone around this call said
        // 6.08 ms and could not say how much of it was work.
        m_renderer->DrawFrame(m_registry,
                              m_editorLayer->GetOffscreen(),
                              ImGui::GetDrawData(),
                              *renderCamera);
    }

    finishRecording();

    SUPERSONIC_LOG_INFO("SupersonicApp") << "Window close requested. Waiting for GPU idle..." << std::endl;
    if (m_vulkanDevice && m_vulkanDevice->GetDevice()) {
        m_vulkanDevice->GetDevice().waitIdle();
    }
}

// Write the recording out, or say whether the replay reproduced.
//
// At the end of Run rather than at the end of each tick, because a recording is
// only useful whole - a file flushed per tick would cost a write at 60 Hz and
// still be truncated if the process died, which is the case the end marker
// already refuses to read.
void SupersonicApp::finishRecording() {
    if (m_recording) {
        // A final checkpoint at whatever tick the run stopped on, so the end of
        // the session is checked even when it does not land on the interval.
        // Without it the last few seconds of every recording are unverified,
        // which is where a bug being recorded usually is.
        const auto* clock = m_registry.ctx().find<SimulationClock>();
        const uint64_t finalTick = clock ? clock->tick : 0;
        if (m_recording->CheckpointAt(finalTick) == nullptr) {
            m_recording->checkpoints.push_back(
                ReplayCheckpoint{finalTick, StateHash::Compute(m_registry)});
        }

        std::string error;
        if (InputRecording::Save(*m_recording, m_options.recordPath, error)) {
            SUPERSONIC_LOG_INFO("Record")
                << "Wrote " << m_recording->ticks.size() << " tick(s) and "
                << m_recording->checkpoints.size() << " checkpoint(s) to "
                << m_options.recordPath << "." << std::endl;
        } else {
            SUPERSONIC_LOG_ERROR("Record") << error << std::endl;
        }
        return;
    }

    if (m_replay) {
        // THE FINAL CHECKPOINT, which the tick loop can never reach.
        //
        // stepRecording stops the loop the moment `tick` reaches the recorded
        // count, so a checkpoint written AT that count - which finishRecording
        // always writes, because the end of a session is where a bug being
        // recorded usually is - was compared against nothing. The run ended, the
        // most valuable checkpoint in the file went unread, and the verdict said
        // it reproduced.
        //
        // Found by corrupting it and watching a replay pass: the whole reason
        // tools/verify-replay corrupts the LAST checkpoint rather than the
        // first. The roadmap's own Done-when is "reproduces its final StateHash
        // byte for byte", and that was the one comparison not being made.
        if (!m_replayDiverged) {
            const auto* clock = m_registry.ctx().find<SimulationClock>();
            const uint64_t finalTick = clock ? clock->tick : 0;
            if (const ReplayCheckpoint* expected = m_replay->CheckpointAt(finalTick)) {
                const uint64_t actual = StateHash::Compute(m_registry);
                if (actual != expected->hash) {
                    m_replayDiverged = true;
                    m_replayDivergedAtTick = finalTick;
                    m_replayExpectedHash = expected->hash;
                    m_replayActualHash = actual;
                    SUPERSONIC_LOG_ERROR("Replay")
                        << "diverged at tick " << finalTick << ": expected " << expected->hash
                        << ", got " << actual << "." << std::endl;
                }
            }
        }

        if (m_replayDiverged) {
            // Plain stdout as well as the log, because this is the ANSWER the
            // run was asked for rather than a diagnostic about producing it -
            // the same argument the profiler summary makes a few hundred lines
            // above.
            std::cout << "Replay diverged at tick " << m_replayDivergedAtTick
                      << ": expected " << m_replayExpectedHash
                      << ", got " << m_replayActualHash << std::endl;
        } else {
            const auto* clock = m_registry.ctx().find<SimulationClock>();
            const uint64_t reached = clock ? clock->tick : 0;
            std::cout << "Replay reproduced " << reached << " of "
                      << m_replay->ticks.size() << " recorded tick(s) across "
                      << m_replay->checkpoints.size() << " checkpoint(s)." << std::endl;
        }
    }
}

bool SupersonicApp::ReplayDiverged() const { return m_replayDiverged; }

} // namespace Supersonic
