# Engine Architecture

## Manifesto

> Game state lives in flat EnTT components. Logic lives in systems that operate
> over those components. Vulkan memory is allocated exclusively through VMA.

Where the code departs from that, the departure is written down below rather
than left as an aspiration the code quietly contradicts.

---

## Core Principles

### 1. Data-Oriented Design & ECS

- **Entities** are 32-bit identifiers (`entt::entity`) with no behaviour.
- **Components** are flat data structs. A few carry small helper methods
  (`TransformComponent::getModelMatrix`, `CameraComponent::getViewMatrix`) —
  a deliberate exception, noted under *Known departures* below.
- **Systems** are stateless free functions over registry views. Per-entity state
  belongs in the component, not in a file-static: `ScriptComponent` owns its own
  clock and `ParticleEmitterComponent` owns its own particles precisely so two
  entities cannot interfere with each other.

### 2. Vulkan Rendering

- **API**: Vulkan 1.2 via `vulkan/vulkan.hpp`.
- **Memory**: all GPU allocation goes through VMA. There are no raw
  `vkAllocateMemory` calls.
- **Windowing**: GLFW with `GLFW_NO_API`.
- **Validation layers**: requested on Debug builds. If the layer is missing the
  engine prints a prominent, unmissable warning and continues — see
  *Validation layers* below, because this matters more than it sounds.

### 3. Frame structure

`EngineApp::Run` owns the frame. The order is deliberate and load-bearing:

```
poll events
apply pending viewport resize     <- before ImGui::NewFrame; see below
ImGui::NewFrame                   <- computes WantCaptureMouse/Keyboard
camera input                      <- gated on those flags
fixed-step physics, audio, scripts, particles
editor BuildUI                    <- mutates ECS, records desired viewport size
ImGui::Render
mesh uploads
renderer.DrawFrame                <- records and submits
```

Two constraints are easy to break and expensive when broken:

1. **The offscreen viewport target may only be recreated at the top of the
   frame.** Recreating it destroys its framebuffer, images and ImGui descriptor
   set. Doing that while a command buffer has already recorded a render pass
   against them is a use-after-free that `vkDeviceWaitIdle` does not save you
   from, because the offending buffer has not been submitted yet.
2. **The resize must land before `ImGui::NewFrame`, not after `ImGui::Render`.**
   Once `ImGui::Image` has recorded the viewport texture into the frame's draw
   data, freeing that descriptor set leaves the draw call pointing at released
   memory.

### 4. Ownership

```
EngineApp
├── AudioEngine        (XAudio2 on Windows; explicit no-op elsewhere)
├── HotReloadEngine    (watches the script plugin)
├── Window
├── VulkanContext      (instance, debug messenger)
├── VulkanDevice       (physical/logical device, queues, VMA allocator)
├── VulkanSwapchain
├── VulkanRenderer     (render passes, pipelines, MeshRegistry, ImGui backend)
└── EditorLayer        (panels + the offscreen viewport target)
```

`EditorLayer` is owned by `EngineApp`, **not** by `VulkanRenderer`. It used to be
a by-value member of the renderer whose UI was built from inside `DrawFrame`,
which is what allowed the editor to destroy renderer resources mid-recording.
The renderer now receives the offscreen target and finished ImGui draw data as
parameters and knows nothing about the editor.

Teardown order is load-bearing: `EditorLayer` must be destroyed before
`VulkanRenderer`, because it frees an ImGui descriptor set that
`ImGui_ImplVulkan_Shutdown` invalidates.

### 5. Colour pipeline

There is exactly **one** sRGB encode in the chain, and it happens in
`shader.frag`. Everything downstream is therefore UNORM:

| Stage | Format | Why |
|---|---|---|
| Checkerboard texture | `R8G8B8A8Srgb` | decoded to linear on read, which is what the PBR maths expects |
| Offscreen colour target | `R8G8B8A8Unorm` | the shader already encoded; an SRGB target would encode again |
| Swapchain surface | `B8G8R8A8Unorm` | ImGui writes sRGB-authored colours and performs no conversion |

Changing any one of these without the others reintroduces a double encode.

### 6. Scripting & hot reload

Scripts are looked up by name in `ScriptRegistry`. Built-ins and plugin scripts
use the same POD-only C ABI (`core/ScriptPluginApi.h`), which is what makes
reloading safe: nothing with a C++ layout, no `std::string` and no ownership
crosses into a module that gets unloaded while the process keeps running.

`HotReloadEngine` shadow-copies the plugin before loading so the build output
stays unlocked, debounces the write because linkers emit output in several
passes, and unregisters the plugin's scripts *before* freeing the module.

---

## Validation layers

`VK_LAYER_KHRONOS_validation` is requested on Debug builds. If it is not
installed the engine prints a boxed warning and continues without it.

This is worth stating plainly: **two showstopper bugs survived six commits
because validation was silently unavailable.** A null sampler written into a
descriptor and a framebuffer destroyed mid-recording both present as raw
`0xC0000005` access violations without the layer, and as immediate, precise
error messages with it.

Install the Vulkan SDK, or set `VK_LAYER_PATH` to a directory containing
`VkLayer_khronos_validation.json`.

---

## Known departures from the manifesto

Listed rather than hidden.

- **Components carry some logic.** `getModelMatrix`, `getViewMatrix`,
  `getProjectionMatrix` and `updateCameraVectors` live on components. This is a
  pragmatic choice, but note it is what put the Vulkan Y-flip inside a component
  method — the source of a picking bug where `Raycast` unprojected through an
  already-flipped matrix. If you move projection construction into a system,
  make the clip-space convention an explicit parameter.
- **`TimeTravelDebugger`, `CameraSystem` and `ScriptRegistry` hold static
  state.** The debugger's history and the camera's first-mouse latch are
  genuinely global; the registry is a process-wide service. `ScriptEngine` and
  `ParticleSystem` no longer do — their state moved into components.
- **`LightFlickerScript` is native, not a plugin script.** It needs
  `LightComponent`, and the script ABI deliberately carries only a transform.

## Platform support

| Platform | State |
|---|---|
| Windows | Working. Primary development target. |
| Linux | Should build and run; not verified on hardware. |
| macOS | Instance creation is wired for MoltenVK (portability enumeration + bit). Not verified on hardware. |
| Android | **Not functional.** GLFW has no Android backend and the manifest expects a NativeActivity shared library CMake does not produce. See `platform/android/build_android.sh`. |
| iOS | **Not functional.** Same windowing problem, plus bundling and signing. |
