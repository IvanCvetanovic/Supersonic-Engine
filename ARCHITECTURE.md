# Supersonic Engine — Architecture

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

`SupersonicApp::Run` owns the frame. The order is deliberate and load-bearing:

```
poll events
apply pending viewport resize     <- before ImGui::NewFrame; see below
ImGui::NewFrame                   <- computes WantCaptureMouse/Keyboard
camera input                      <- gated on those flags
fixed-step physics, audio, scripts, particles   <- only while PLAYING
resolve world transforms          <- so the gizmo and picking see current data
editor BuildUI                    <- mutates ECS, records desired viewport size
ImGui::Render
resolve world transforms again    <- so rendering reflects that edit
mesh and texture uploads
renderer.DrawFrame                <- shadow pass, scene pass, ImGui pass
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

### 3b. Transform hierarchy

`TransformComponent` is **local to the parent**. `HierarchyComponent` holds the
parent link, and `TransformSystem` resolves the graph into
`WorldTransformComponent` once per frame.

Everything downstream — the scene pass, the shadow pass, viewport picking and the
gizmo — reads the **world** matrix. That is what makes parenting work everywhere
at once instead of in one place. `TransformSystem::UpdateWorldTransforms` runs
twice per frame: before the editor, so the gizmo and picking see current
matrices, and after it, so rendering reflects the edit just made.

Two invariants:

- **Reparenting preserves world placement.** `SetParent` recomputes the local
  transform under the new parent, so attaching something in the hierarchy does
  not teleport it. It also refuses to parent an entity under its own descendant.
- **Destroying a parent promotes its children to the root first.** Leaving a
  child pointing at a released handle is not merely stale: EnTT recycles
  handles, so it would later resolve to a completely different entity.

Parent links are serialized as an **array index**, never a raw `entt::entity`,
for the same recycling reason.

### 3c. Edit / Play / Paused

`PlayMode` snapshots the scene to an in-memory string on Play and restores it on
Stop, reusing the scene serializer — so the restore path is the same code as
loading a file, and the snapshot format cannot drift from the save format.

Gameplay systems (physics, scripts, particles, audio, the time-travel recorder)
run **only** while playing. The editor used to simulate permanently, which is
why the demo's physics cube had already fallen before you could look at it.

### 4. Ownership

```
SupersonicApp
├── AudioEngine        (XAudio2 on Windows, ALSA on Linux, no-op elsewhere)
├── HotReloadEngine    (watches the script plugin)
├── Window
├── VulkanContext      (instance, debug messenger)
├── VulkanDevice       (physical/logical device, queues, VMA allocator)
├── VulkanSwapchain
├── VulkanRenderer     (render passes, pipelines, MeshRegistry, TextureRegistry,
│                        ShadowMap, ImGui backend)
└── EditorLayer        (panels + the offscreen viewport target)
```

`EditorLayer` is owned by `SupersonicApp`, **not** by `VulkanRenderer`. It used to be
a by-value member of the renderer whose UI was built from inside `DrawFrame`,
which is what allowed the editor to destroy renderer resources mid-recording.
The renderer now receives the offscreen target and finished ImGui draw data as
parameters and knows nothing about the editor.

Teardown order is load-bearing: `EditorLayer` must be destroyed before
`VulkanRenderer`, because it frees an ImGui descriptor set that
`ImGui_ImplVulkan_Shutdown` invalidates.

### 4b. Assets

| Kind | Loader | Notes |
|---|---|---|
| Meshes | `MeshRegistry` | Cube/Sphere/Plane/Terrain primitives, OBJ, and glTF |
| glTF/GLB | `GltfLoader` (tinygltf) | Bakes each node's transform chain into its primitives |
| Textures | `TextureRegistry` (stb_image) | Cached by path; one descriptor set per texture |
| Audio | `AudioClip` | Uncompressed RIFF/WAVE |

Both registries cache failures so a missing or broken asset is not reopened
every frame, and both fall back to something visible (unit cube, checkerboard)
rather than dropping the draw.

`AssetWatcher` stats every path a scene names, once per frame, and invalidates
whatever cached it. Deliberately a poll: a few dozen paths is one stat each, and
this engine already pays that without anyone noticing. A real
`FindFirstChangeNotification`/inotify layer is worth writing when the count
reaches the hundreds.

Three rules hold across every cache it drives.

**Re-read in place, keeping the id.** Ids are indices into the library's vector
and every component in the scene holds one, so pushing a new entry on reload
leaves them all reading the values from before the edit.

**Keep what was loaded when the new read fails.** A save is not atomic and a
poll can land mid-write, so a file that no longer parses leaves the previous
values alone. Blanking them would turn every object using a material white for a
frame — and let the editor write that blank back.

**A cached miss must be repairable.** A file that failed to load is remembered as
a miss so it is not retried every frame, which means fixing it on disk has to be
what clears it, or it stays broken for the session.

`ConvexHullCache` is invalidated by the same callback, which is easy to forget
because a hull is not a file: it is built from one. Re-uploading an edited mesh
moves what you see and leaves what you hit where it was.

Two caches do not fit that shape.

A **rig** carries a `generation` as well as an id, because a reload replaces its
contents without changing the index — so anything derived from it, the joint
palette and the captured bind-pose bounds, goes on describing the previous
export. `SyncSkeletons` compares both. The joint-count resize alone hides the
problem whenever the count happens to match, which for a re-export is almost
always.

An **audio clip** cannot be dropped at all while it is playing. A voice reads its
sample buffer directly — XAudio2 is handed `clip->pcm.data()` and reads it from
its own thread, and the software mixer keeps a `const AudioClip*` whose contract
is "the clip must outlive the voice". So `AudioSystem::ReloadClip` stops the
voices, then drops the clip, then clears the handles the components hold; every
other order is either the use-after-free or a voice that can no longer be
stopped. Which voices to stop is answered by `AudioEngine`, which records what
each one is playing: `Update` never looks at `soundFile` again after starting a
voice, so a looping source pointed at another file leaves the old voice running
on the old clip — precisely the voice a component-side search would miss.

Writes made by the engine itself are acknowledged rather than fired. The
inspector edits a material in place and saves it, so without
`AssetWatcher::Acknowledge` the next poll reads the file back over the values
still being dragged. `MaterialLibrary` holds the watcher so `Save` cannot forget.

Callbacks fire after the poll's scan, not during it, because a callback is
allowed to watch and forget paths — which mutates the map the scan is walking.

### 4c. Lighting and shadows

Lights live in a **storage buffer**, and a fragment loops only the ones that can
reach it. The 8-light cap is gone; `kMaxLights` is 256 and now bounds how many
lights may exist in a frame rather than how many may touch one pixel.

**The froxel grid.** `core/ClusterGrid` cuts the view frustum into 16x9 screen
tiles by 24 depth slices - 3456 froxels - and the CPU posts each local light
into the ones its range sphere touches. Two more storage buffers carry the
result: one (offset, count) pair per froxel, and the flat index list those pairs
point into. The fragment finds its own froxel from `gl_FragCoord` and its
view-space depth, and walks that slice.

The slices are **exponential**, not uniform: a uniform division puts almost every
froxel out where the frustum is enormous and nothing is standing, and crams the
near field - where the lamps are - into the first one.

**Directional lights are never clustered.** They are the leading entries of the
light buffer, `lightCount.y` says how many, and every fragment loops them
unconditionally. A light that reaches everywhere is in every froxel, and
recording that would cost one index per cluster to say nothing. Keeping them as
a prefix also preserves the rule the rest of the renderer depends on: the
shadowed directional is at index 0, which is what `sky.frag` reads for its sun
disc and what the cascade lookup is gated on.

Two things this broke that a screenshot would not have shown:

- **The cascade gate was on the LOOP COUNTER.** `if (i == 0)` meant "the
  shadowed directional" only while the loop index and the light index were the
  same thing. Walking a froxel's list, counter 0 is "the first light in THIS
  tile", so the directional shadow would have been applied to whatever point
  light happened to sort first, differently in each tile. It tests the light's
  index now.
- **`pointShadowMaps[slot]` stopped being dynamically uniform.** The slot comes
  from a light found in this fragment's froxel, and two fragments of one quad
  can be in different froxels. Indexing a descriptor array that way needs
  `shaderSampledImageArrayNonUniformIndexing`, which this device does not
  enable - it validated cleanly and rendered correctly anyway. Both cubes are
  sampled at constant indices now and the slot only selects between the results,
  which is free at two casters and also closes a pre-existing dynamic-indexing
  gap.

The screen-space half of the mapping has a **CPU twin**,
`ClusterGrid::ClusterForFragment`, and the GLSL is a transliteration of it. That
exists because the one bug this feature shipped with in draft - the row index
mirrored about the horizon, since the projection flips Y for Vulkan while the
grid numbers rows in view space - lived only in GLSL where no test could reach
it, and was invisible in any scene whose lamps reach every froxel anyway.

`LightSelection` still exists and is now a genuinely exceptional path: it decides
which 256 survive rather than which 8.
`positionOrDirection.w` carries the type — 0 directional, 1 point, 2 spot —
compared against 0.5 and 1.5 in the shader because it arrives as a float inside
a vec4.

Point and spot lights fall off as `1 / (1 + 0.09d + 0.032d²)`, with `range` as a
hard cutoff: past it the light is skipped outright. That curve is not
inverse-square and is not defended as physical. The cutoff is what keeps a
distant lamp out of the loop entirely rather than leaving it in to contribute
nothing visible.

| | Directional | Point | Spot |
|---|---|---|---|
| Placement | `direction`, pointing **toward** the light | world position | world position, `direction` for aim |
| Shadow target | 4 layers of one 2048² array image | one 1024² cube per caster | one 1024² array layer per caster |
| Casters | 1, and it must be `lights[0]` | `PointShadow::kMaxShadowCasters` (2) | `SpotLight::kMaxShadowCasters` (2) |
| Lookup | pick a cascade by view depth, project, 3×3 PCF | sample by direction, 5 taps | project through `spotViewProj[slot]`, 3×3 PCF |
| Slot index | implicit: index 0 | `attenuation.y`, or -1 | `attenuation.w`, or -1 |

Placement goes through `LightWorldPosition`, which reads
`WorldTransformComponent` and falls back to the local `TransformComponent` only
when the hierarchy has not been resolved yet — which is the same answer for an
unparented light, and the only answer available before the first resolve.

This was a departure until recently, and worth keeping in mind as a shape of
bug rather than as a live one: `gatherLights` read the LOCAL transform, so a
torch parented to a character lit the world origin while the character walked
away from it. Everything else downstream — both shadow passes, picking, the
gizmo — already read the world matrix. Lights were the single exception.

**Which lights.** The array is fixed at `kMaxLights`, and a scene may hold more
than that. `SelectLights` decides which ones are passed: directional first,
since they light everything wherever they are and the shadow caster has to
reach `lights[0]`; then local lights by distance to the EDGE of their range, so
a floodlight reaching the shot beats a pinpoint lamp nearer the camera that
illuminates nothing. Stable among equals, so a scene does not reshuffle which
of its lights are lit between frames.

That is a selection rule, not culling — nine lights in one room still drops one.
What it replaced was worse than a rule: the loop took lights in registry order
and stopped at the ninth, and EnTT walks a pool in reverse insertion order, so a
scene kept the eight lights authored LAST. The sun is the first thing anyone
places, so a level that grew a twelfth lamp lost its directional light and every
shadow in the scene with it, silently. It is in `core/LightSelection.hpp` rather
than in the renderer because it needs nothing from Vulkan, and the suites
deliberately touch no Vulkan entry point.

Ambient is a scene-wide hemispheric term — sky colour above the horizon, ground
bounce below — and `FindAmbientLight` decides which light supplies it: the first
shadow-casting directional, then any directional, then any light at all. It is
chosen before the packing loop rather than read out of `lights[0]`, because it
used to fall out of EnTT's reverse iteration order, so the light created *last*
set the ambient for the whole scene and adding any lamp changed it.

**Cascades.** The directional shadow is four cascades in the layers of one array
image. The splits blend a uniform division with a logarithmic one (`lambda` = 0.85):
purely logarithmic packs almost everything into the first cascade and starves
the distance, purely uniform wastes the near cascades where the detail is
visible. The last split is pinned exactly to the shadow distance so the shader's
"past the last split means lit" test lines up with the fit rather than
approximately agreeing with it.

Each cascade fits the bounding sphere of its camera slice, not a box in light
space: a sphere is invariant under camera rotation, so the extent does not
breathe as the camera turns. The radius is padded by 1.02, quantised up to a
multiple of 0.5, and the world texel size derived from the **final** radius; the
centre is then snapped to that texel grid in light space, on X and Y only. The
order is load-bearing — deriving the texel before padding and quantising leaves
the snap grid mismatched against the grid actually sampled, and every shadow
edge crawls as the camera moves. Leaving Z alone keeps the depth row of the
projection identical across all four cascades and across frames.

The depth range along the light axis is measured over the whole scene rather
than per slice. `depthClampEnable` is `VK_FALSE`, so a near plane fitted tightly
to one slice would clip casters standing between the light and that slice and
drop their shadows with no other symptom. Each cascade also carries its own
culling frustum for the depth pass; culling it against the camera instead would
make shadows pop as casters behind the camera left view.

Acne is handled mostly in the shader: a normal offset scaled by that cascade's
world texel size, plus a residual 0.0006 constant. The rasteriser bias is
correspondingly small (0.6 constant, 1.1 slope). It was retuned down when
cascades arrived — the old constants were set against a fixed ortho range, and a
cascade's depth range spans the whole scene along the light axis, which turned
the same numbers into several times the world-space offset and visibly detached
contact shadows.

**Point lights** render six faces into a cube map, one render pass per face,
each culled against its own frustum. That is the reason for six passes rather
than one multiview pass: multiview would rasterise every caster into every face
and throw away exactly the culling the depth pass exists to do. The shader does
not need the six matrices — a face looks straight down its major axis through a
90° frustum, so the depth that was stored can be reconstructed from the distance
along that axis alone, and the matrices never have to reach the shader at all.
`test_pointshadow` pins the reconstruction against `BuildFaceViewProj`.

**Spot lights** reuse the `ShadowMap` class with `SpotLight::kMaxShadowCasters`
layers instead of four: the same render pass, format selection, per-layer
framebuffers and sampler, and the same project-and-compare lookup shape. What
they do not reuse is the fitting. A spot is one perspective frustum from
`SpotLight::BuildViewProj`, whose field of view is the outer angle doubled and
widened 5%; using the half angle would light a cone the map does not cover, and
everything outside it would be lit through walls. The Vulkan Y flip is applied
to the projection *before* it is combined with the view — negating the same
element of the finished product is not a flip at all, since that element is a
combination of the view's rows, so it corrupts the transform instead. A test
comparing it against the cascade convention is what caught that.

Every cube face and every spot layer is recorded and cleared each frame, whether
a light claimed the slot or not. An untouched image keeps the layout it was
created with while its descriptor claims it is ready to sample, and the cubes are
a single descriptor array — one untouched slot is enough to invalidate all of
them. The clear also discards whatever the previous frame left, which would
otherwise be a shadow cast by a light that has stopped casting; a cleared depth
of 1.0 reads as no occluder anywhere, and therefore as lit. All three depth
passes record through one depth-only pipeline, front-face culled so acne lands
on faces the camera cannot see.

**A light carries its own shadow slot.** The cube index lives in
`attenuation.y`, the spot layer in `attenuation.w`, -1 in each meaning "does not
cast". A parallel array indexed by light order would desync by construction,
because `gatherLights` *reorders* the lights: the first shadow-casting
directional is swapped into slot 0. Within `GpuLight` the same care applies at
field granularity — `attenuation.x = light.range` rewrites only x, precisely
because y, z and w already hold the cube slot, the spot's inner cone cosine and
the spot layer. Assigning the whole vec4 there is what once made every point
light report cube slot 0.

The directional case is an invariant spanning two files, and both halves are
needed:

```
gatherLights   -> swaps the first shadow-casting directional into lights[0],
                  and returns its direction
shader.frag    -> applies shadowFactor only when (i == 0 && w < 0.5)
```

Drop the swap and the cascades are still built from the direction `gatherLights`
returned, but applied to whichever light happened to land in slot 0 — shadows
cast from a direction nothing in the scene is lit from. The gate is
directional-*only*: a point light sitting in slot 0 still reaches its cube
through the later branch, and a second directional light is lit but never
shadowed. One shadowed directional light is the limit.

**Descriptor sets.** Set 0 is per-frame and has twelve bindings; set 1 is
per-material and has three, rebound per draw.

| Set 0 | Contents | Stages |
|---|---|---|
| 0 | UBO: camera, cascade transforms and splits, ambient, spot transforms, fog, cluster and environment parameters | vertex + fragment |
| 1 | cascade shadow maps, `sampler2DArray`, 4 layers | fragment |
| 2 | this frame's joint palette, storage buffer | vertex |
| 3 | point shadow cubes, `samplerCube[2]` | fragment |
| 4 | spot shadow maps, `sampler2DArray`, 2 layers | fragment |
| 5 | every light in the frame, storage buffer, directionals first | fragment |
| 6 | per-froxel (offset, count) table, storage buffer | fragment |
| 7 | the flat light-index list those pairs point into, storage buffer | fragment |
| 8 | irradiance cubes, `samplerCube[2]`, one per environment probe | fragment |
| 9 | prefiltered cubes, `samplerCube[2]`, one per environment probe | fragment |
| 10 | this frame's texture coordinate transforms, storage buffer | fragment |
| 11 | this frame's per-draw records, one per INSTANCE, storage buffer | vertex + fragment |

Bindings 5 through 7 are why there is no `MAX_LIGHTS` any more: the light array
used to live inside the uniform block as a fixed eight, and a storage buffer
makes its length a runtime count instead of a number compiled into every shader
that reads the block.

Binding 10 is there for the same reason binding 2 is. Both are per-frame arrays
a draw indexes into, because a push constant block is exactly 128 bytes — the
guaranteed minimum — and a joint palette and a texture coordinate transform are
both per-draw data that does not fit in what is left. Slot 0 of the transform
buffer is always the identity and is written every frame, so a draw that never
asked for one indexes it and gets its coordinates back unchanged, with no branch
and no bounds test in the shader.

Binding 11 is that argument carried all the way. The 128 bytes themselves became
per-frame array data: the scene pass no longer pushes anything, it writes one
`PushConstantData` record per instance into binding 11 and the shaders read
`instances[gl_InstanceIndex]`. That is what lets consecutive compatible draws be
one `drawIndexed` with an instance count, since the only thing that used to
differ between them travelled in the one piece of state a draw call cannot
batch. `gl_InstanceIndex` is a vertex input, so the fragment stage receives it
as a **flat** varying rather than reading it directly. The depth pass still
pushes a real push constant — it draws no instanced batches, so it has nothing
to gain and a smaller block to fill — which is why the 128-byte layout and its
`static_assert` are still live even though the scene shaders no longer declare
one.

The three sampler shapes are not interchangeable. The cascades must be one array
image because the per-fragment cascade choice is not dynamically uniform — it
differs within a quad at every split seam, which is undefined behaviour rather
than a style preference. The point shadows are an array of two separate cube
descriptors rather than one cube-array image, so no optional device feature is
required, and the slot they are indexed with is read out of the light block
rather than computed per fragment. The spots are a single descriptor over a
layered image, because a spot is sampled exactly like a cascade.

Set 1 is albedo, a tangent-space normal map, and one packed map holding
occlusion, roughness and metallic in R, G and B - the channels glTF packs them
into. A single set is what previously forced every object to sample one
globally bound texture.

One packed map rather than three separate ones, because that is what an
exporter writes and what an author paints, and because three bindings would
be three samplers and three descriptors for data that is one byte each. Its
channels **multiply** the per-material constants rather than replacing them:
replacing would make the two ways of authoring a surface exclusive - either
uniformly rough, or entirely at a texture's mercy with no way to dial the
whole thing smoother - while multiplying makes the constant a master control
and makes the no-map case exactly the arithmetic that was there before. It is
also what the glTF specification says the factors mean.

A material with no map samples a 1x1 white **data** texture, so the multiply
is a no-op and the fragment stage needs no branch. Its own built-in rather
than the white albedo, which is uploaded sRGB: white survives that decode
either way, so reusing it would have worked right up until somebody changed
the neutral to anything other than white.


**The red channel is the trap.** glTF says of a metallic-roughness texture that
"the red and alpha channels are not specified and their values are ignored" -
so red is legally anything, and exporters write zero. Read as occlusion, that
zeroes the ambient term for the whole surface: a valid file rendering pitch
black wherever no light directly reaches it, with nothing to say why. So
`MaterialComponent::occlusionStrength` says how much of red to believe, using
glTF's own formula `1 + strength * (sampled - 1)` - exactly 1.0 at strength 0,
so ignoring the channel is the same arithmetic rather than a branch. It travels
in `PushConstantData::emissive.w`, which was documented padding.

`GltfLoader::ChoosePackedMap` makes the decision and is split out of the import
so it can be tested without a file: the strength is the file's own only when an
occlusion texture named the SAME image, compared as resolved paths rather than
texture indices, because two texture entries can name one image through
different samplers. An occlusion texture with no metallic-roughness one is not
imported at all - an AO bake is greyscale, so its green and blue would drive
roughness and metallic too, and every crevice would come out smoother and less
dielectric than the surface around it.

Every built-in fallback is protected from `Invalidate` and `ReplaceRGBA` by one
predicate rather than a hand-written list, because `Acquire` caches a failed
load under the real path's key: invalidating that path would otherwise reach in
and destroy a texture every material in the scene is sharing. And the fallback
is a parameter rather than something derived from the `srgb` bool, which now
separates three kinds of texture and could only ever answer two - deriving it
handed a broken ORM path the flat normal, which as packed ORM reads as half
occlusion, half roughness and fully metallic.
The binding count is one constant, `VulkanPipeline::kMaterialBindingCount`,
read by both the layout and `TextureRegistry`'s descriptor pool. It used to be
a literal `2` in each, which is the shape of mistake that does not fail: a
pool sized for two bindings while the layout declares three does not error, it
quietly runs out of sets a third early, hundreds of materials into a scene
nobody was testing. The set cache is keyed on the whole triple of texture ids,
ordered rather than hashed - three 32-bit ids do not pack into a 64-bit key,
and a hash collision would render one material with another's maps and say
nothing about it.

Three values are duplicated into `shader.frag` by hand, each with a comment
naming the C++ constant it must match: `POINT_SHADOW_CASTERS`,
`SPOT_SHADOW_CASTERS` and the point light near plane, `0.05`. Nothing links
them, so the match is maintained by hand. That is the drift the depth pass
avoids structurally instead: it takes its transform in a push constant, so
`shadow.vert` never declares the UBO block at all and cannot fall out of step
with the three other declarations of it — a mismatch nothing diagnoses.

### 4d. Recording the depth passes, and not recording them

A frame has **eighteen** depth passes: four cascades, six cube faces for each of
two point-light slots, and one for each of two spot slots.

Every one of them used to walk the registry from scratch — two component
lookups per entity, a mesh-registry lookup, a `try_get` for the skin, and an
eight-corner transform to build the world bounds — and all of that is identical
in all eighteen, because the world bounds of a crate do not depend on which
light is looking at it. `RenderSystem::GatherShadowCasters` builds the list once
and the passes read it. The only per-pass work left in the loop is the frustum
test, which is the part that genuinely varies.

**And then most of them are not recorded at all.** A shadow map is a function of
the light's transform and the casters it can see; rendered twice from the same
inputs it is the same image twice. `ShadowPassSignature` reduces those inputs to
a 64-bit value and `ShadowCache` remembers what each pass was last recorded
from, so a pass whose signature has not changed is skipped.

| | recording every pass | skipping unchanged passes |
|---|---|---|
| demo scene, release | 0.093 ms | **0.011 ms** |
| 1000 casters, release | 0.50 ms | **0.25 ms** |
| demo scene, debug | 0.45 ms | 0.047 ms |
| 500 casters, debug | 3.32 ms | 0.40 ms |

**Both rows, because only one of them is the truth about a shipped game.** A
Debug build of this engine runs between three and two hundred times slower per
zone than a Release one — EnTT's lookups and GLM's operators are entirely
unoptimised — and the ratio is not uniform, so a Debug profile does not merely
scale the frame, it *reorders* it. At a thousand entities Debug says the frame
is 34 ms and that the editor's UI and the transform resolve dominate it;
Release says the frame is 1.5 ms and that the two shadow-recording zones are
the largest thing in it. Optimisations chosen from the first list would mostly
have been aimed at work that does not exist in the second.

This one survives the change of build — 0.24 ms out of a 1.5 ms release frame
is a sixth of it — because command recording is the one thing an optimiser
cannot remove. It is the zone with the smallest debug-to-release ratio in the
engine, 2.6x against 80x for the transform resolve and 210x for resource
syncing.

Three decisions in that are worth the words:

- **The signature is taken after culling, not over the whole scene.** A
  signature over every caster is dirtied by anything moving anywhere in the
  level, which in a scene where anything moves means never skipping a pass — a
  cache that is perfectly correct and never hits. Per-frustum means a crate
  moving at the far end of the level leaves the lamp over here alone. The cull
  is the cheap half of a pass, so paying for it twice on the frames that do
  record is a good trade against paying for the whole pass on the frames that
  need not.
- **A pass is always recorded the first time**, tracked by its own flag rather
  than a reserved signature value. A shadow image is created in an undefined
  layout and only reaches a readable one by being rendered through the pass, so
  a slot that has never been recorded is not stale, it is unusable — and since
  the cube maps are one descriptor array, one untouched slot invalidates all of
  them. Skipping is safe only *after* that first record: the pass declares
  `initialLayout = eUndefined`, so it discards what was there when it runs and
  leaves the image readable when it ends, and not running it leaves both the
  contents and the layout exactly as the last record left them.
- **An unclaimed slot signs for an empty pass under a zero light matrix.** Two
  frames with the slot unclaimed agree, so the clear is not repeated; the frame
  a light *leaves* a slot does not agree, and clears away the shadow it was
  casting.

The cost when nothing can be skipped is real and worth stating: on the demo
scene in play mode, where a scripted satellite moves every frame and keeps the
cascades dirty, the shadow half of the frame goes from 0.45 ms to 0.52 ms — the
signature is computed for every pass whether or not it saves one. The obvious
refinement is for the signature pass to hand its culled list to the recording
pass so the cull happens once rather than twice; it is not done.

Tested away from the renderer, in `tests/test_shadowcache.cpp`, because the
decision is the part that can be wrong and the recording is not: a cache that
never invalidates renders a perfectly plausible frame that happens to be last
frame's, and it looks entirely correct in a screenshot of a scene that is
standing still. An end-to-end pixel comparison would also need play mode to be
reproducible, and it is not — three runs of the same binary on the same scene
give three different images, because the simulation clock is the wall-clock
frame delta.

### 5. Colour pipeline

There is exactly **one** sRGB encode in the chain. It used to happen at the end of
`shader.frag`, which was correct while that shader produced the final image; it
now happens in `bloom_composite.frag`, at the end of the bloom chain, and nowhere
else.

`shader.frag` writes linear radiance, unbounded and un-encoded. It has to: the
bright pass thresholds at `kThreshold`, which is 1.0, so tone mapping before it
would flatten exactly the highlights bloom exists to find. The scene target is
therefore floating point, and stays floating point through the blur, until the
composite adds the bloom back, applies Reinhard and encodes once.

| Stage | Format | Why |
|---|---|---|
| Colour textures | `R8G8B8A8Srgb` | decoded to linear on read, which is what the PBR maths expects |
| Data textures (normal and packed ORM maps) | `R8G8B8A8Unorm` | a normal map stores directions and an ORM map stores three numbers the shader multiplies straight into roughness, metallic and occlusion; a transfer function would bend all of them |
| Offscreen scene colour and its MSAA resolve | `R16G16B16A16Sfloat` | linear HDR, so highlights can exceed 1.0 and the bright pass has something to select |
| Bloom bright and blur, half resolution | `R16G16B16A16Sfloat` | still linear; half resolution because a blur is low-frequency and full resolution costs four times the bandwidth for an image nobody can tell apart |
| Bloom composite output, full resolution | `R8G8B8A8Unorm` | tone-mapped and encoded here; an SRGB target would encode a second time |
| Swapchain surface | `B8G8R8A8Unorm` or `R8G8B8A8Unorm`, `eSrgbNonlinear` colour space | the composite output arrives already encoded, and ImGui's style colours are authored as display-referred sRGB; the backend converts neither |

`VulkanOffscreen::kColorFormat` is an alias for `BloomPass::kHdrFormat`, so the
scene target and the chain that reads it cannot drift apart. Both the
multisampled colour attachment and the resolve attachment carry that format,
which means the resolve averages linear radiance rather than encoded values.

Depth goes through `VulkanDevice::FindDepthFormat`'s candidate list, and the
shadow maps query `getFormatProperties` themselves before settling on
`D32Sfloat`, falling back to `D16Unorm` where it cannot be both attached and
sampled. Neither bloom format is checked against the device at all: both are used
unconditionally, with no fallback path.

**Nothing here fails loudly when it disagrees.** Every format involved is
individually legal, so a mismatch produces no validation error and no visible
failure at creation — only a wrong picture:

- An 8-bit scene target clips every value above 1.0 on store, which is precisely
  the set of pixels the bright pass exists to find. Bloom degrades into a uniform
  haze around near-white instead of light spilling from bright sources.
- Moving the encode moved the clear colour with it. The offscreen clear is
  `0.00023`, the linear radiance that comes back out at 0.02 on screen:
  `pow(x / (1 + x), 1/2.2) == 0.02`. It was 0.02 while it was written straight to
  a UNORM image and shown verbatim; carried unchanged through the new tone map
  and encode it arrived at about 0.18, which turned the background from
  near-black into mid-grey.
- Moving the encode also forced `grid.frag` to pre-linearise its authored sRGB
  colours with `pow(color.rgb, vec3(2.2))`, because it blends into a target that
  has not been encoded yet. Without it, a value meant to be displayed as 0.2 is
  treated as 0.2 of linear light and the grid reads far brighter than it was
  drawn.

**Transparency, and where its alpha stops mattering.** A material marked
transparent is diverted out of the opaque walk and drawn afterwards, sorted back
to front, through a pipeline with blending on and depth writes off. Particles
ride the same pipeline — their emitters default to an end alpha of zero, so
every one of them is authored to fade, and while they were on the opaque
pipeline that alpha was computed, pushed and discarded by the blend state.

The sky is drawn BETWEEN the opaque and transparent passes for the same reason.
A blended surface writes no depth, so a pane with nothing but sky behind it
leaves the depth buffer at the clear value, and a sky drawn after the whole
scene at z = 1.0 with a lessOrEqual compare passes that test and paints over it.
Transparency worked indoors and vanished against the horizon.

**Cutout, which is the half most world content wants.** A leaf card, a
chain-link fence, a grate is mostly holes with hard edges - not a pane of glass.
`MaterialComponent::alphaCutoff` discards any fragment whose alpha falls below
it, before anything is shaded, and a surface using it stays OPAQUE: it writes
depth, it needs no sorting, and it costs one compare.

Without it those surfaces had to be marked `transparent` and pushed through the
blended pass, where they sort against THEMSELVES - one leaf card in front of
another composites in whichever order the distance sort happened to pick, and
the result flickers as the camera moves. That is not a quality difference; it is
the wrong mechanism.

Zero is the off switch rather than a separate flag or a shader variant, the same
way a fog density of zero is how fog is turned off: the disabled path is then
the same arithmetic rather than a branch that can disagree with it. The test is
against the same alpha the blend pass would have used - texture times factor -
so a material reads the same whichever route it takes. glTF `alphaMode: MASK`
imports straight onto it, with the spec's 0.5 default when the file omits a
cutoff.

**Per SURFACE, and it was not.** The cutoff used to be the entity's in both
passes, and the entity's material is copied from the model's *first* surface on
import. So a leaf card packed as the fourth surface of a model whose first is
opaque was cut at zero — which is not cut at all — and drew as a solid
rectangle, in the camera and in all eighteen depth passes. There was no
authoring mistake to find: the file said `MASK`, the engine read it, and dropped
it one surface later. The mirror case was worse: a model whose *first* surface
was the cut-out one cut every other surface against that surface's albedo at its
own UVs, so a chassis grew leaf-shaped holes, cached by the shadow cache so it
looked stable and deliberate.

A surface's cutoff now wins when it names one, and the entity's stands
otherwise — a widening rather than a replacement, so the inspector's Alpha
Cutoff slider still reaches a surface that says nothing about alpha. In the
depth pass a caster became a *run of indices* rather than always a whole mesh,
so a cut-out surface is cut against its own texture. A mesh whose surfaces all
cut at zero is still one caster covering every index, which is the common case
and every one of HUSK's 166 materials — nothing that did one draw now does
eight. `firstIndex` had to join the pass signature: two surfaces of one mesh can
have the same index count, so without it the cache cannot tell one from the
other.

**Where that alpha reaches the light.** A depth pass records one bit per texel -
blocked or not blocked - so the two halves of transparency get the only two
answers that bit has.

A surface with a **cutoff** occludes where its albedo is opaque enough, tested
against the same `texture alpha x factor < cutoff` the scene pass makes. A
**blended** surface that named no cutoff does not cast at all: `transparent`
means exactly one thing in this renderer, the pipeline that turns depth writes
off, and a surface declining to occlude in the camera's depth buffer has no
business occluding in the light's. It cannot cast a partial shadow, so the
choice was between nothing and the solid black rectangle it used to cast, and
the rectangle is the one that is definitely wrong. Setting both is documented as
meaning "blend what survives the cut", so the cut is read first and what
survives it is what casts.

Three things about how it is done, each of which was the alternative's problem:

- **A second pipeline, not one compare in `shadow.frag`.** Elsewhere in this
  engine a cutoff of zero is the off switch precisely so the disabled path is
  the same arithmetic; here it cannot be. `discard` costs a pipeline its early
  depth rejection on every draw, a fragment stage that samples set 1 makes that
  set mandatory for every draw, and eighteen depth passes a frame is where a
  crate would pay for a leaf. `GatherShadowCasters` returns its list
  **partitioned** - solid casters first - so a pass switches pipeline once, and
  in a scene with no cut-out surface never switches at all.
- **The cut-out pipeline culls neither face.** Front-face culling pushes acne
  onto faces the camera cannot see, which is a good trade for a closed solid and
  a wrong one for a card: the cube's `-Y` face carries the `+Y` face's texture
  coordinates flipped in v, so culling the face the light strikes leaves the far
  one casting and the shape cast is the *mirror* of the shape drawn - worse than
  the rectangle, because it looks like it works. Measured, not assumed: the same
  card casts 1753 near-half against 1358 far-half pixels with `eNone` and 740
  against 1298 with `eFront`. The bias constants are unchanged and `eNone` is
  scoped to this pipeline, so no solid caster's acne moves - but a cut-out
  caster's does, because the map now records its near face rather than its far
  one and only the bias and the normal offset stand between that and
  self-shadowing. Measured too, on the case that would show it: a 1.5-unit cube
  at a cutoff low enough to discard nothing, so the cull mode is the only
  variable, differs from the same cube drawn solid in 31 pixels of 293695, none
  of them on its lit faces. That is the scale it has been checked at.
- **The material set is resolved in the gather, not the pass.**
  `AcquireMaterialSet` allocates, updates and can throw; the depth pass runs
  eighteen times a frame with a render pass open. Once per frame outside every
  pass, it is an ordinary call. A null set - the pool exhausted, already logged -
  leaves the cutoff at zero and degrades to the old solid rectangle, which makes
  "a positive cutoff has a set" true by construction.

`ShadowPassSignature` mixes the cutoff, the alpha factor and the descriptor set
**handle**, all inside the frustum cull. The handle rather than the texture id
for the reason the vertex buffer handle is already there: a reload swaps the
image under a stable id. Miss any of them and the cache serves a silhouette the
material no longer has, which looks entirely plausible.

All three depth call sites take the same pair of pipelines - four cascades, six
cube faces per point-light slot, one per spot slot - and each was checked by
toggling that light's shadow and looking at what changed, because clean
validation only certifies the passes that actually ran.

One place the alpha still does not reach is **particles**. A particle is not an
entity - it is a POD inside its emitter's component vector, with no transform and
no bounds - so it never enters the gather and casts nothing, before this change
or after it. Nor do particles interleave with transparent meshes: they are sorted
among themselves and drawn after, because a particle is a different mesh with a
different material set and merging the two lists would cost a rebind per draw at
the point in the frame with the most draws in it. A particle behind a pane
composites in the wrong order.

The per-entity override, `RenderableComponent::castsShadow`, is authored beside
`isVisible` in the inspector's Renderable section. Both were serialized and read
from the day they existed and neither had a control, because every "Casts Shadow"
checkbox in the editor belongs to a LIGHT - so the override was reachable only by
editing a scene file by hand. That matters once a material decides what it casts:
a blended surface opts back in with a cutoff, and a solid one had no way to opt
out at all.

**Images carried inside a model.** A `.glb` keeps its textures as bytes in the
binary chunk rather than as files beside it, and every texture in this engine is
a path: the registry opens files, hot reload watches files, and
`MaterialComponent` serialises a path.

That mismatch was worse than it looked. tinygltf is built with
`TINYGLTF_NO_STB_IMAGE`, because it ships its own stb copy and `TextureRegistry`
already defines `STB_IMAGE_IMPLEMENTATION` - and tinygltf treats a missing image
decoder as a hard parse error the moment a file *carries* an image rather than
naming one. So a `.glb` with a texture in it did not arrive untextured. It did
not arrive at all: no meshes, no skins, no animations, refused over an image the
importer never wanted decoded.

Both halves are fixed by the same observation. The importer registers a no-op
image loader that reports success, which is honest - it genuinely does not want
the pixels - and `ParseImage` leaves `image.bufferView` intact regardless. The
encoded bytes are then copied out of the buffer view verbatim into `cache/gltf/`
and the resulting path is handed on like any other. Verbatim rather than decoded
and re-encoded: they are already a PNG or a JPEG, and stb_image is going to
decode them again on the way to the GPU, so a re-encode would spend time
producing a worse copy of a file that already exists.

Into `cache/` because it is where per-machine build artefacts already live and
is already ignored by git. Extraction is skipped when the cached file is at
least as new as the model, so re-exporting the `.glb` still reaches the texture.

Base64 `data:` URIs are the remaining case and are reported rather than
imported - the branch they used to fall into joined them onto the model's
directory, producing a "texture path" several kilobytes long that could only
ever fail to open.

### 6. Scripting & hot reload

Scripts are looked up by name in `ScriptRegistry`. Built-ins and plugin scripts
use the same POD-only C ABI (`core/ScriptPluginApi.h`), which is what makes
reloading safe: nothing with a C++ layout, no `std::string` and no ownership
crosses into a module that gets unloaded while the process keeps running.

`HotReloadEngine` shadow-copies the plugin before loading so the build output
stays unlocked, debounces the write because linkers emit output in several
passes, and unregisters the plugin's scripts *before* freeing the module.

### 6b. Who owns the pointer

`Input` is deliberately GLFW-free: it takes one `RawInputState` snapshot per
frame and everything downstream queries that, which is what lets the mapping and
the edge detection be tested with no window. `InputPolling` is the only input
file that touches GLFW, and no test target links it.

Capturing the mouse has to cross that line, so what crosses is a *decision*, not
a call. `Input::SetCursorMode` records what the game asked for; two vetoes can
override it; `EffectiveCursorMode` is the answer, and `InputPolling` applies it.
All three inputs and the answer are ordinary statics with no device behind them,
so the whole arbitration is a headless test.

- **The host's veto.** Under a locked pointer GLFW reports unbounded virtual
  coordinates, and ImGui's GLFW backend feeds `glfwGetCursorPos` straight into
  `io.MousePos` without checking the mode — so every panel's hover test goes
  wrong at once, including the viewport-hovered flag the cameras gate on. Capture
  is therefore only allowed while a game is actually playing and the viewport has
  focus. A packaged game reports both unconditionally, so there it reduces to
  "while playing".
- **Focus.** A locked pointer is invisible and cannot leave the window, so a game
  that locks it and offers no release would trap whoever ran it. Losing the
  window releases it outright. That is the guaranteed way out.

Neither veto *overwrites* the request, which is why they are vetoes rather than
the host calling `SetCursorMode(Normal)` itself: alt-tabbing away and back, or
pausing and resuming, restores exactly what the game last asked for instead of
leaving it unable to look.

**The delta rebase is the part that actually bites.** Locking or releasing the
pointer teleports it — GLFW swaps screen coordinates for unbounded virtual ones,
and puts it back on the way out — and a teleport differenced against last frame
is a delta of several hundred pixels. The camera snaps to face somewhere else
entirely on the frame you capture, once, which reads as a broken mouse rather
than as a bug. `Input::Update` corrects the *baseline* rather than zeroing the
result, so the delta stays one subtraction with no second path to disagree with
it, and it keys off the EFFECTIVE mode so a veto rebases too.

`InputPolling::ApplyCursorMode` runs earlier in the frame than `Poll` — straight
after `glfwPollEvents`, before ImGui's new frame — for two reasons: ImGui samples
the cursor itself and would otherwise disagree with the engine for a frame, and
the position `Poll` reads has to already be in the new coordinate space, because
that is the frame `Update` rebases on.

`CameraSystem` reads `Input::MouseDelta()` rather than keeping a second baseline
of its own, which is how it inherits all of that. It also stops requiring the
right button while the pointer is locked: the button, the latch and the
hovered-gate all exist because a visible cursor is shared and has to be borrowed,
and a captured one is none of those things. Keeping them would mean a
first-person game steered by holding right-click. `EditorCamera` still polls GLFW
directly and still requires the button, which is correct for an editor viewport
where the pointer is genuinely shared.

Scripts reach it through `SupersonicScriptInput`: `mouseDelta`, `setCursorMode`
and `cursorMode`. The delta is not an axis — an axis is bipolar and clamped to
±1, which is right for a stick and wrong for a mouse, where the magnitude is the
movement and there is no maximum. `cursorMode` reports the effective mode, so a
script tests whether it actually has the pointer before treating motion as a
look.

### 6c. Who owns the keyboard

The same shape one layer up, and the same reason it needs one. Text cannot be
polled: shift, dead keys, Caps Lock and the layout all sit between a key going
down and a character existing, and none of it is recoverable from an array of
booleans. GLFW delivers characters through a callback, so `InputPolling`
accumulates them and drains them into `RawInputState` — after which they are an
ordinary field a test fills in by hand, exactly like every other.

`InputPolling::InstallCallbacks` runs **before** the renderer, because the
renderer initialises ImGui's GLFW backend and that backend keeps whatever
callback it displaces and calls it first. Installed before it, both get every
event. Installed after, ours silently replaces ImGui's and every text box in the
editor stops accepting characters, with nothing reporting why — so the function
checks GLFW's return value and logs if it landed second. Nothing else can catch
that ordering. Registering the character callback also installed the scroll one,
which had been declared, documented and never registered: `AccumulateScroll` had
no callers at all, so `Input::Scroll()` had returned zero since the day it was
written. Both are one line in the same function now. That much is readable from
the code; that a wheel event actually arrives is not something a headless run
can show, and `Input::Scroll()` still has no consumer in the engine.

**`Input::SetTextCaptureActive` is the keyboard's veto**, and ImGui's own
`io.WantTextInput` is not enough on its own: it knows about ImGui's widgets and
a field drawn by this canvas is invisible to it, so typing a name would also
walk the player forward and trip every editor shortcut on the way. While raised,
key-derived sources contribute nothing to actions and axes. Pad and mouse sources
are untouched — nobody types with a thumbstick, and `Fire` is bound to the same
mouse button that clicks the field — and the raw `IsKeyDown`/`WasKeyPressed`
queries stay truthful, because the Escape hatch out of a locked cursor depends
on them working while a field has focus.

Taking the keyboard away fires the ordinary edges, and that is a decision. The
first draft suppressed them, reasoning that a release nobody performed is not a
release; but the *level* changes, and an edge that does not fire when the level
does is worse — a game tracking movement by edges would still believe the player
was walking, with the run animation stuck mid-stride for as long as the name
took to type.

The flag is **assigned every frame** from `AnyTextFieldFocused`, in
`SupersonicApp`, rather than raised by `UIInput` and cleared elsewhere. A clear
that was ever missed — a collapsed panel, a render path that skipped the HUD —
would suppress every key forever with nothing on screen to explain it. Here
there is no clear to miss, and `UIInput` stays free of global state as well as
free of a window.

**Focus is the first decision in `UIInput` that is about the scene** rather than
about one element. A button resolves entity by entity because the answer depends
only on where the pointer is; focus cannot, because exactly one field may hold
it — so one pass decides and a second applies. A press *edge* decides outright:
on a field it takes focus, anywhere else (another field, a button, empty screen)
it gives it up. Absent a press, focus is kept, because typing does not involve
the mouse and losing a half-typed name to a drifting pointer would make the
widget unusable. A press from a pointer with no position — the cursor is locked,
so this is the player firing — decides nothing.

`UICanvas::EditText` is the rule itself, and it is where UTF-8 has to be right:
every edit is a splice into a `std::string`, and one made inside a character
produces a value the font will not draw and the ABI must not hand to a plugin.
Backspace removes a whole codepoint, the caret moves by codepoints, and
`maxLength` counts **characters** — a field authored to hold 24 that took 24
plain letters but 12 accented ones would be a bug report, not a design. Bytes
are real in exactly one other place, `getText`'s copy-out buffer, which truncates
on a character boundary; conflating the two limits is the mistake.

One honest gap: characters are applied before the edit keys within a frame, so
typing a letter and pressing Backspace inside the same 16 ms erases what was
before the letter and keeps it. That is the price of not carrying a merged,
ordered event queue — the only thing such a queue would buy over polling is the
OS repeat delay for Backspace, which ImGui already knows.

**None of this can be seen in a headless screenshot, and that is not new.**
`--screenshot` captures the offscreen 3D target, while the whole HUD is drawn
into an ImGui draw list that composites in the ImGui pass — so no button, label
or panel has ever appeared in one either. What a screenshot cannot reach, the
tests do: the rule in `test_uicanvas`, the arbitration in `test_uiinput`, the
round trip in `test_serialize`. What neither reaches is the character callback
itself and the caret blinking, and those want a person and a keyboard.

### 7. Physics

`PhysicsSystem` is stateless — static functions over the registry, like every
other system here. Nothing survives between calls except what lives in
components: `RigidBodyComponent` carries its own velocity, spin and surface
properties, and the step's contacts are returned through an out parameter rather
than kept in a file-static, because a stored contact list would not survive
loading a second scene.

It runs only while playing — or for one frame when the editor single-steps — and
not at all while the time-travel debugger is rewinding, since rewinding restores
recorded positions and velocities that the solver would immediately argue with.

### 7a. The fixed step

`SupersonicApp::Run` accumulates the frame delta and drains it in whole steps:

```
deltaTime = clamp(rawDelta, 0, 0.10)      <- a hitch is not elapsed time
accumulator += deltaTime
while accumulator >= 1/60 and steps < 5:
    PhysicsSystem::Update(registry, 1/60)
    accumulator -= 1/60
if steps == 5: accumulator = 0            <- drop the remainder, do not bank it
```

The step is fixed at 1/60 s so the same scene behaves the same way on a fast
machine and a slow one. Two separate guards protect it, and both discard time on
purpose:

- **The frame delta is clamped to 100 ms before it reaches the accumulator.**
  Dragging the title bar on Win32 blocks `glfwPollEvents` inside the modal
  move/size loop, which used to hand physics a two-second delta that it then
  faithfully tried to simulate.
- **At most five steps run per frame, and hitting that cap zeroes the
  accumulator** rather than carrying the remainder into the next frame. Banking
  it is the spiral where a machine that cannot keep up spends longer in the
  solver each frame than it did the frame before. A machine that cannot keep up
  therefore runs in slow motion instead, and simulated time and wall-clock time
  genuinely diverge under load.

`Update` clears the contact vector it is handed, so the app appends each step's
contacts into a second, frame-wide list: the editor is shown the frame's contacts
rather than the last step's, and reports the count and how many of them were
triggers. That is the only consumer outside the tests — nothing dispatches them
to gameplay.

### 7b. What one step does

```
for each non-kinematic RigidBodyComponent:
    wake it if it was moved or given a velocity from outside
    sleep it if it has been below the sleep thresholds for long enough
    gravity, linear damping, angular damping
    integrate rotation as a quaternion, write it back as Euler
    position += worldToLocal * (velocity * dt)
    resolve against the world ground plane, if the scene has one
gather every collider into world-space bodies and proxies
sweep and prune  ->  candidate pairs
for each pair: narrowphase -> a manifold of up to four points
               wake either side if the other can disturb it
               positional correction, once, at the centroid
               one velocity constraint per contact point
build the joint list, including bodies that have no collider at all
four sweeps of joint positional correction, re-reading as it goes
eight passes over every constraint: normal impulse, then friction,
                                    then every joint
```

Sleeping is decided during integration rather than after the solve, and the
velocity it judges is therefore the one the *previous* step settled on. That is
the right question to ask, and it is also the only place the decision can live:
`Update` returns early when the scene holds fewer than two colliders, and a lone
body resting on the world plane is exactly the case that has to be able to
sleep.

Velocity is world space; `TransformComponent::position` is local to the parent.
Every write of a world-space displacement therefore goes through the inverse of
the parent's basis, or a parented body drifts along its parent's axes instead of
falling straight down. The parent's world matrix comes from the
`WorldTransformComponent` cache that `TransformSystem` fills once per frame, so a
moving parent's matrix does not change between the steps of a single frame —
cheaper than re-resolving the hierarchy per step, and invisible at 60 Hz.

Damping is `pow(1 - damping, dt)`, not a per-step multiplier: a fixed multiplier
damps twice as hard at 120 Hz as at 60 Hz, so the same scene would settle
differently on a faster machine. Rotation is integrated by building a quaternion
from the angular velocity and composing it, then converting back to the Euler
triple the transform stores. Adding angular velocity into Euler angles directly
is only correct for spin about one axis at a time, and a body tumbling about two
at once wanders off in a way that looks like the physics is broken.

| Constant | Value | Meaning |
|---|---|---|
| Fixed step | 1/60 s | one `PhysicsSystem::Update` |
| Steps per frame | 5 max | past this, time is dropped |
| Gravity | −9.81 on Y | the **default**; per scene, and per body via `useGravity` |
| Ground plane | off | the **default**; per scene, and no entity represents it |
| Penetration slop | 0.005 | overlap left uncorrected |
| Correction factor | 0.8 | fraction of the rest removed per step |
| Rest velocity | 0.1 | below this, bounce is zeroed |
| Static defaults | restitution 0.3, friction 0.4 | used when a side has no `RigidBodyComponent` |

Slop and the 0.8 factor exist together: correcting overlap to exactly zero makes
resting stacks vibrate, because floating-point error re-creates the overlap on
the next step and the correction fires again forever.

The first two are not constants. `PhysicsSettings`, held in the registry's
**context** because there is exactly one per scene and no entity owns it, carries
gravity as a vector and the ground plane as a switch and a height. `Update` reads
it if it is there and uses these defaults if it is not, so a scene that has never
heard of it behaves as the table says. It is written into the scene file next to
the entity array and edited in the Inspector, which shows it where the panel
would otherwise be empty — there is no entity to select in order to reach
something the whole world shares.

### 7c. Broadphase: sweep and prune on one fixed axis

Proxies are sorted by their minimum X and scanned forward, breaking out of the
inner loop as soon as the next proxy starts past the current one's end. Y and Z
are then tested as ordinary interval overlaps. That turns the all-pairs test into
a sort plus the overlaps that actually exist.

The axis is X, always. Nothing measures which axis separates the scene best, so a
scene stacked vertically — every proxy sharing the same X interval — prunes
nothing and degenerates to the O(n²) test it was meant to replace. A room laid
out along X is fine; a tower is not.

Pairs where both sides have zero inverse mass are skipped: two immovable things
may overlap all they like. Zero inverse mass means no `RigidBodyComponent` at all
(which is how level geometry participates as an obstacle without being
simulated), `isKinematic`, or a non-positive mass.

The sort reorders the proxy vector in place, so a pair's indices refer to sorted
positions. `Proxy::index` carries the way back to the body list, which would
otherwise be a search.

### 7d. Narrowphase

Every collider is reduced at gather time to **two** descriptions, because the two
halves of the step want different things. `worldBounds` gives the world
axis-aligned box — the centre through the matrix, the extent through the absolute
value of its basis — and that is what the broadphase sorts and sweeps, because a
broadphase wants a conservative bound that is cheap to compare. The narrowphase
gets the box in its **own** frame: the three columns of the world matrix,
normalised, as its axes, with the length of each column taken out into that
axis's half extent, so a scaled crate collides at its scaled size. A sphere
collapses its three **scaled** half extents to the largest, so a non-uniformly
scaled sphere still becomes the sphere that contains it, and a capsule collapses
its two lateral scales the same way while keeping its own axis.

The word *scaled* there is load-bearing and used not to be. A sphere's radius was
read from the world axis-aligned box, which for a rotated transform is not the
sphere: `worldBounds` sums the three scaled axes' contributions to each world
axis, giving 1.41 times the radius at forty-five degrees about one axis and up to
1.73 in general. A sphere is the same shape whichever way it is turned, so the
radius has to come from the scale alone. Nothing caught it for as long as nothing
rolled — a ball dropped straight onto a box lands with its orientation still
exactly identity — and the failure needs rotation that keeps changing: put a ball
on a hill and it grows a fraction of a millimetre every step, hovering higher and
higher above the ground, without limit.

Three places read that bound and all three had it: the narrowphase gather, the
world ground plane, and the shape list the queries are built from. The last is
worth naming separately, because the box path beside it uses its world AABB
**deliberately** and says so - a query over-reports rather than misses, which is
the right direction for "what am I looking at". A sphere's rotation-dependence is
not conservatism of that kind. It is the same object being a different size
depending on which way it happens to be facing.

There are five collider shapes. A convex hull has its own section below and
its own file, because the box path here is written around three axes and four
corners per face and none of that generalises; an oriented box, however, IS the
unit cube hull with its extents on the basis, so box-against-hull is the hull
path rather than a fifth pair test.

Among the other four there are **four** pair tests rather than ten,
because a sphere is a capsule whose segment has no length. Writing sphere-against-sphere separately
would be a second implementation of the same arithmetic with its own edge cases,
and it was: the old one had no speculative margin, so two fast spheres passed
through each other while a sphere and a box did not.

| Pair | Test | Exactness |
|---|---|---|
| round / round | nearest points between the two axis segments, against the sum of radii | exact; covers sphere/sphere, sphere/capsule and capsule/capsule |
| box / round | nearest point between the segment and the box, then the round case against that | exact |
| box / box | separating axis theorem over fifteen axes, then Sutherland–Hodgman clipping of the incident face against the reference face | exact, and up to four contact points |
| heightfield / anything | nearest point on each cell triangle the shape's bounds overlap, with a face-versus-edge rule per query point | exact for the surface; see below for what a box and a capsule each approximate |

The fifteen axes are the six face normals — three per box — and the nine
cross products of one box's edge directions with the other's. The face axes alone
find every overlap where a face is involved and miss the edge-on-edge case
entirely, which is a plank resting on the corner of another plank passing through
it.

Three details in there are load-bearing, and each fails silently rather than
loudly if it is wrong:

- **Parallel axes.** The cross product of two parallel edge directions is the
  zero vector, and normalising it is a NaN. Every comparison against a NaN is
  false, so the axis reports *no overlap* and two axis-aligned boxes sitting
  inside one another are declared not to be touching. Degenerate axes are
  skipped instead, on a squared-length test against `kParallelEpsilon`; a
  genuinely parallel pair is always covered by the face axes anyway.
- **Near-ties between a face axis and an edge axis.** Two axes within
  floating-point noise of each other flip between steps, and the contact normal
  flips with them, which reads as a stack that shivers. An edge axis has to beat
  the best face axis by `kFaceBias` (1.02) before it is taken - by 2% whichever
  way the shapes are: the handicap multiplies an overlap and DIVIDES a gap.
  It used to multiply both, which made a gap wider and handed the tie to the
  edge exactly when the shapes were apart. Apart, an edge axis of two level
  boxes points where the face axis does (x crossed with z is y), so a crate
  coming in to land was caught by one edge-edge point instead of its face and
  was turned by the catch; locked to a plane, the kicks added up to a crate
  tipping 0.44 rad on a sinking platform (the Magic Portals spike). The hull
  path has the same contest and the same fix.
- **The clip.** Sutherland–Hodgman decides whether an edge crosses the plane by
  comparing the *signs* of the two endpoint distances, not the sign of their
  product: two distances small enough that their product underflows to zero lose
  the crossing, and the clipped face comes out missing a corner.

Sphere-against-box is solved by calling the box-against-sphere routine with the
arguments swapped and negating the normal, so there is one implementation rather
than two that can disagree.

Every pair test takes a **speculative margin**: how far apart the pair may be
and still report a contact, with a negative penetration standing for the size of
the gap. It is passed as the distance the two bodies travel this step, which is
what lets the solver stop a fast body on the surface instead of letting it pass
through. See the departures list for what that buys and what it does not.

**The capsule and the box.** Finding the nearest point between a segment and a
box by solving for the segment parameter directly is a case analysis over six
faces, twelve edges and eight corners, which is where a shape test of this kind
usually goes wrong. Instead the segment is taken into the box's frame and the two
sets are alternately projected onto each other — clamp a point onto the box, find
the nearest point on the segment to that, clamp again. Each step can only reduce
the distance between two convex sets, so it converges, and eight passes is well
past where the answer stops changing. The result is then handed to the
sphere-against-box test, so there is one piece of code deciding which face a
corner belongs to.

The tempting shortcut — testing only the capsule's two end spheres — is wrong for
exactly the arrangement a bridge is: a capsule lying across a narrow pillar it
does not touch at either end.

A capsule against a box returns a **manifold**, for the same reason box-against-
box does. A capsule lying ALONG a surface touches it in a line, and one contact
under its middle leaves it free to rock end over end about that point with
nothing anywhere else to resist: measured at about a radian per second still, ten
seconds after a nudge. So when the axis is within sixty degrees of the surface,
both ends are tested as well and kept if they hold up — both have to produce a
contact of their own, and both normals have to agree with the one the middle
found, or a capsule wedged into a corner would be pushed along the average of two
faces, into neither and out of the corner sideways. A capsule standing on a cap,
or leaning, still gets one point, because one is all it has.

**The heightfield.** Terrain is not a box and approximating it with a stack of
them takes thousands, each of which the broadphase has to sort every step. It is
worth a shape of its own because it is a **function**: `y = h(x, z)`, no
overhangs, so for any point there is exactly one cell beneath it, no triangle is
ever hidden behind another, and the cells a shape can touch are an index range
rather than a search. A general triangle-mesh collider has to build an
acceleration structure to reach the same answer.

What the function does *not* give away is the seam problem, and the obvious loop
gets it wrong on **flat** ground. A sphere over one triangle is also within reach
of its neighbour, whose nearest point is the shared edge — so the neighbour
reports a second contact whose normal leans sideways out of a surface that is not
bent at all, and a ball rolling across a field is shoved at every cell boundary
it crosses. Every mesh collider in existence carries a pile of internal-edge
filtering to undo this.

A heightfield does not need any of it, because it can tell the two cases apart
directly. A contact is a **face** contact when the nearest point is the
perpendicular foot and an **edge** contact otherwise. An edge contact is only
ever real where the surface creases upwards — the crest of a ridge, the rim of
the grid — and in exactly those places there is no face contact to be had. So per
query point: if it has a face contact, its edge contacts are the artefact and are
dropped; if it has none, they are the only thing holding it up and are kept. Per
*point*, not per manifold, because a capsule with one cap on flat ground and the
other on a ridge needs both kinds at once.

Two more rules earn their place:

- **Duplicates.** A sphere sitting exactly over a grid vertex is nearest to that
  one point on all six triangles that meet there. Contacts within a hundredth of
  a millimetre are merged, or the solver gets six copies of one contact and six
  times the impulse — so a body would bounce harder landing on a seam than a
  hand's width to either side of it, which is a physical difference between two
  places that are geometrically identical.
- **Thickness.** The solid extends a stated distance *below* the surface. A body
  that has ended up under the terrain — spawned there, dragged there by a gizmo,
  put there by a script — is pushed straight up out of the top rather than
  sideways by whichever cell it happens to be nearest, and past that depth it is
  through and falls. Without a bottom the surface is one-sided, which is the
  failure mode of every thin collider.

A capsule is its two end spheres, which is what the capsule-against-box path
already documents doing and is right for the shape's purpose: a character stands
on its lower cap. The limitation is stated rather than solved — a capsule lying
horizontally across a ridge that touches neither end rests on nothing.

A box is its eight corners against the surface **and** the surface's vertices
against the box. Corners alone is the usual shortcut and it is wrong in a way
that is easy to miss: a crate wider than a cell straddling a bump has none of its
corners under the ground and the bump straight through its floor.

The collider and the visible mesh are the same surface *by construction*, not by
agreement: `TerrainGenerator::SampleHeight` is the one expression both go
through, and the suite asserts that every one of the 4096 collider vertices
equals the mesh vertex bit for bit. A tolerance there would hide exactly the
drift the shared function exists to stop. The queries — `Raycast`,
`OverlapSphere`, `IsGrounded` — march the grid rather than testing its bounding
box, which is not a detail: the bounds of a landscape are the sky above it, so a
bounding-box answer is a character reporting that it is standing on the ground
while it falls past a mountain.

**Segments.** Two of the three things the segment arithmetic has to get right
fail silently. Parallel segments give a zero denominator in the closed-form
solve, and dividing by it is the NaN that makes two parallel capsules lying
against each other report no contact at all; there is no unique nearest pair
then, so any point along the overlap will do. And clamping one segment's
parameter to its own ends moves the nearest point on the *other* segment, so the
first parameter has to be solved again against the clamped one — without that, a
capsule resting past the end of another sits slightly inside it. Both branches of
that second solve are tested, because one of them is always the one nobody tried.

An entity carrying more than one collider is one shape: box, then capsule, then
sphere. Each pass skips an entity a previous one claimed, in the solver and in
the queries alike, or it would be gathered twice and collide with itself.

### 7e. Response

Each pair produces a manifold; each manifold produces a positional correction and
a list of velocity constraints; the constraints are then solved together.

**Positional correction**, once per pair, at the centroid of the manifold. The
overlap beyond the slop is shared out by inverse mass, so the heavier body moves
less and an immovable one does not move at all. The cached centres of both bodies
are then updated, because the pairs still to be resolved this step read them —
without that, a body wedged between two others is pushed apart twice and travels
twice as far as it should. It is deliberately not applied per contact point:
pushing out four times would move the body four times as far as the overlap
requires. A speculative pair is skipped entirely, because there is nothing to
push out of.

**One velocity constraint per contact point.** A face resting on a face is up to
four points, and resolving it as one is what leaves a crate balanced on a single
spot inside its own footprint, free to rotate about it. Each constraint stores
the arms from both centres, the effective mass along the normal — including the
angular term for both bodies, because the linear term alone applies an impulse
far too large for a glancing hit near a corner — and a target velocity, computed
**once**, from the velocities as they are before any impulse:

```
restitution   = |approach| < kRestVelocity ? 0 : max(bounceA, bounceB)
allowance     = speculative ? gap / dt : 0
targetVelocity = max(-restitution * approach, -allowance)
```

Restitution is dropped near rest or a settling box jitters forever. The larger of
the two terms wins, so restitution decides when there is a bounce and the
allowance only decides anything when there is not — summing them instead makes
them fight, and a ball that should have rebounded 0.05 units recovers 0.008.
Computing restitution inside the iteration rather than once would feed the solver
its own output, and a resting stack slowly climbs.

**Eight passes over every constraint.** Each pass applies only the *change*
needed to satisfy its contact, and the total per contact is accumulated and
clamped so it can never pull:

```
lambda        = (targetVelocity - approach) / normalMass
previous      = normalImpulse
normalImpulse = max(previous + lambda, 0)
lambda        = normalImpulse - previous
```

Clamping the accumulated impulse rather than each pass's change is the whole
trick. A contact may pull during one pass as long as the total stays a push,
which is what lets a later pass correct an earlier over-correction. Without the
accumulator, eight passes apply eight full impulses and the scene launches;
without the passes, every contact is resolved as though it were the only one in
the world, so the box in the middle of a stack is pushed out of the box below it
and straight into the box above, every step, and the stack sinks and shivers
rather than settling.

**Coulomb friction**, in the same pass, after the normal impulse. The tangent is
recomputed from the relative velocity *after* that impulse and at the contact,
spin included — friction acts on the surface speed, which is zero for a ball
rolling without slipping and is the entire reason a ball rolls instead of
sliding. The tangential impulse is clamped against the **accumulated** normal
impulse, so it can slow sliding but never reverse it and never exceeds what the
contact is actually being pressed together with.

Both impulses are applied linearly and angularly, at the arm from each body's
centre. That arm is the whole of the difference between a body that slides and
one that turns: an impulse through the centre only pushes, the same impulse
applied at a distance also spins.

| Quantity | Combined as | Why |
|---|---|---|
| Restitution | the larger of the two, clamped to 0.99 | a superball dropped on concrete has to bounce; the smaller or the average would let any dead surface kill every ball that touched it |
| Friction | geometric mean, each side clamped to 4 | a zero on either side takes the result to zero, so ice stays slippery against anything; an average would let a rough floor grip a puck |

Each side's number comes from one place. A rigid body carries its own
`restitution` and `friction`. A collider with no rigid body - level geometry -
takes them from its `PhysicsMaterialComponent`, and from `kRestitution` (0.3)
and `kFriction` (0.4) when it has none; on an entity with a rigid body the
component is ignored, so the two never compete. Until the component existed
every floor bounced at 0.3 and gripped at 0.4 with no way to say otherwise, and
a port whose original's floors do not bounce could not be matched. Its defaults
are the two constants, so a scene without one steps as it always did.

Inertia is built in the body's **own** axes and rotated into the world by them.
A box about its centre and a solid sphere both have no products of inertia, so
the local tensor stays three floats rather than a matrix, and the rotation is
`R I Rᵀ`. It has to be the same frame the narrowphase uses, or the same object
has two different masses depending on which way it is facing: a 5 × 1 × 1 plank
turned 45° about Y has a bounding box 4.24 across, whose moment about the axis an
impact turns it around is 1.58 against the plank's own 2.17. Taking the tensor
from the bounding box was right while box-box *collided* as its bounding box and
became wrong the moment SAT started colliding the box itself. Tested by running
the same impact twice with the whole scene turned 45° about the gravity axis: the
two spins now come out bit-identical, against a 16% difference before.
A capsule is the one approximation left: its tensor is that of a solid cylinder
of the same radius and total height, which puts the mass of the hemispherical
caps slightly further from the axis than it really is, so a capsule is a few
percent harder to tip end over end than it should be. Wrong in the stable
direction, and next to nothing beside the fact that the shape exists mostly for
characters, which usually freeze rotation anyway. `freezeRotation`,
`isKinematic`, a non-positive mass, a sleeping body and static colliders all
produce a zero tensor, which falls out of the arithmetic as “infinitely hard to
turn” without a branch at every use.

**Per-axis locks** (`lockPosition`, `lockRotation`) are the partial version, in
world axes: a locked axis is one the body neither moves along nor turns about.
The case that asked for them is a 2D game on this 3D solver. The Magic Portals
port locks position z and rotation x and y; unlocked, a resting crate left its
plane by 30 px in a minute (`docs/planning/2026-09-10-magic-portals-spike.md`).

- **Rotation lock:** the inverse inertia with the locked axes projected out,
  P I⁻¹ P, built once where the tensor is.
- **Position lock:** a factor on every linear impulse and positional
  correction, contacts and joints alike. The inverse mass a body presents along
  a direction counts only its free axes.
- **Integration:** zeroes the locked components of both velocities.
- **Joints:** where a lock leaves a row of zeros in an effective mass, the joint
  drops that row rather than the whole constraint.

Two properties are tested:
- a locked axis is held exactly, not nearly;
- a scene with no locks steps bit for bit as it did before. That holds because
  the two places a lock changes the arithmetic, an effective mass and the
  tensor, branch on whether there is one.

A lock holds a body where it is. It does not return the body to a plane it has
already left. Locks apply to dynamic bodies only: a kinematic body is moved by
code, and a static collider not at all.

### 7f. Joints

A constraint holding one body to another, or to a fixed point in the world.
Before this nothing did, so a door, a rope bridge, a ragdoll limb and a
suspension arm all had to be faked by a script writing transforms — which is not
a physical object, it is a body that ignores everything it touches.

Three types, from two primitives:

| Type | What it removes | What it is for |
|---|---|---|
| Point | three linear degrees of freedom: the two anchors must coincide | a ragdoll shoulder, a pendulum free to spin as it swings |
| Distance | **one**: the anchors must stay a given distance apart | a rope, a chain link, a pendulum that swings |
| Hinge | Point, plus the two rotational degrees of freedom that are not the axis | a door, a wheel, a lid |

The one degree of freedom is what makes Distance a different thing from Point
rather than a weaker one. Everything perpendicular to the line is left
completely alone, and that is what lets a pendulum swing instead of hanging
rigid wherever it was put. A `rope` resists stretching *only*, so the two ends
may drift together freely and are caught when the line goes taut; without that
every chain is a set of rigid rods and a hanging one cannot fold.

**The arithmetic lives in `core/Joints`**, for the reason `CollisionSAT` does:
this is the part where a sign error is a pendulum that gains energy until it
flies apart, and the only practical way to trust a three-by-three effective mass
is to call it with hand-checkable numbers. It has no registry, no components and
no frame — the caller does every lookup and hands over world-space vectors.

The effective mass is `(imA + imB)·I − skew(rA)·IinvA·skew(rA) −
skew(rB)·IinvB·skew(rB)`. The two skew terms are what make an anchor on the
**edge** of a body different from one at its centre; drop them and the joint
drags the body bodily without ever turning it, so a door swings by sliding.

**Both halves, like a contact.** A joint gets a velocity constraint *and* a
direct positional correction, because positions are integrated **before** the
solve — so a velocity change alone can never take out the error the current step
introduced, only the previous one's. Corrected by velocity alone with a
Baumgarte bias `β`, a body hanging under gravity settles at `g·dt²/β`, which at
60Hz with `β = 0.2` is **1.4 cm** below where it belongs: not drift, a steady
state. Moving the bodies as well brings that to `g·dt²(1−k)/k`, about 0.7 mm at
`k = 0.8`.

The position pass sweeps the list **four times**, re-reading each body's
transform at the top of every joint rather than trusting the copy taken when the
list was built. That is the difference between a Jacobi sweep and a Gauss-Seidel
one, and on a chain it is the difference between a rope that hangs at its length
and one that does not: link five has to see where link four has just been *put*.
Measured on a five-link rope at 60Hz, one pass left every link stretched by
three to five per cent; four brings it under half of one.

The angular half of a hinge is *not* corrected positionally. Writing a rotation
back means going through the transform's Euler triple, and there is nothing
pulling a hinge out of alignment the way gravity pulls a rope down every single
step — so it gets a bias term in the velocity solve and that is enough.

**Solved inside the same iteration as the contacts**, not in a loop of its own. A
body hanging from a rope *and* resting on the ground has to satisfy both at
once; solved apart, each undoes the other and the body walks a little further
out of place every step it is held by two things.

Four things that are load-bearing and fail quietly:

- **The other end is written as an index.** An `entt::entity` carries a version
  and is recycled, so persisting the handle reattaches the joint to whatever
  occupies that slot next time the scene loads. `SceneSerializer` writes it the
  way it already writes parent links; `ComponentCodec` carries everything else.
  The consequence is deliberate: a **prefab** with a joint keeps its shape and
  loses its other end, because the entity it pointed at is not part of the
  prefab.
- **A hinge needs an axis at each end.** One axis, taken through both bodies,
  compares the door to itself and measures nothing — so it would let it flop in
  any direction while looking exactly like a working hinge.
- **The perpendicular basis picks its reference by the axis's smallest
  component.** A fixed reference gives a zero-length cross product whenever the
  axis is parallel to it, and a hinge about world Y — a door — is the most
  ordinary thing anyone will build.
- **Waking travels across a joint.** A body hanging from something that has just
  been knocked has to wake, or half a chain hangs frozen in mid-air while the
  rest of it swings. An awake but equally *still* neighbour must not count, or
  two settled links hold each other awake forever — the same trap the contact
  path documents.

Two early returns had to go for any of this to run: a pendulum is one collider
on a static anchor, and a bob on a rope may have no collider at all, so “fewer
than two bodies” and “no pairs touching” are ordinary states for a scene that
still has constraints to solve.

**Open, and named rather than implied:** limits (a door that stops at ninety
degrees), motors, a breaking force, and a weld between two dynamic bodies.
Parenting is not a weld — a parented child integrates in its parent's space and
inherits that motion on top of its own. One joint per entity, which is what the
ECS gives and is also the right shape for a rope of N links or a ragdoll bone
held to its parent; a mechanism that genuinely needs two constraints on one body
needs a second entity.

**Limits, motors and breaking.** A hinge angle is measured between two
reference directions, one per body, each derived from that body's own axis by
`PerpendicularTo` and rotating with it. Deriving them from the *world* axes
instead would give directions that wander as the solver nudges the axes, and the
angle — and with it the limit — would wander too. Zero is where the two
coincide, which is an arbitrary configuration rather than a meaningful one, so
the inspector shows the live angle and offers to set the stops around it: these
are authored by reading the number, not by predicting it.

The stop is an **inequality with no velocity bias**, and that is the whole of its
design. A bias asks the solver to reach a return *speed*, and the body keeps that
speed once it is back in range because nothing takes it away again — so the stop
hands out energy. At the full rate a door arriving at six radians a second
crossed its entire range and slammed into the opposite stop; softened to half a
radian a second it still drifted forty-five degrees back off a ninety degree
stop. So the velocity half removes only the speed going *into* the stop, and the
overshoot — bounded by one step's travel — is walked out by **rotating** the body
in the position pass, exactly as the linear half of every joint is walked out by
moving it. Measured: a door shoved at six radians a second now rests at 89.95
degrees with 0.05 left in it.

That rotation only became writable once `TransformComponent::EulerFromRotation`
existed. See below.

The motor is a target speed about the axis with a **capped** impulse. Without the
cap it is infinitely strong and drives whatever is in the way through a wall
rather than stalling against it. The limit is solved *after* the motor, so a
motor driving into its own stop is overruled by the stop rather than fighting it
to a draw.

Breaking compares the impulse a joint applied over the whole step, divided by the
step, against a **force** and a **torque** separately — they are not the same
quantity, and adding their magnitudes would compare metres per second to radians
per second. Checked after every pass rather than inside them, because the first
iteration's over-correction is one the seventh is about to undo. `broken` is
runtime state and is deliberately not serialised: a level whose joints reloaded
already snapped is a level you could only play once.

A **weld** is a point constraint plus all three rotational degrees of freedom,
held by velocity alone: it keeps the relative orientation the two bodies had when
it started acting rather than driving them to one it was told about, because
there is nowhere to store a rest orientation and holding what they have is what
welding two things *means*. With the relative angular velocity driven to zero
every step there is no systematic drift, only the float error of the integration.

A **spring** pulls a hinge toward a rest angle, authored as a frequency in hertz
and a damping ratio rather than as a stiffness and a damping coefficient. That
pair is mass-independent, so the same numbers behave the same on a garden gate
and on a vault door.

The thing it had to avoid is a mistake this engine already made once: a hinge
limit with a velocity bias is a spring that hands out energy, and the door
bounced off its own frame. The difference is where the softness enters. A bias
alone leaves the body moving *away* from rest at a speed proportional to the
error, so it always crosses. Constraint-force mixing divides the impulse and
bleeds the accumulated one, so the constraint relaxes rather than insisting, and
can only ever remove energy. The three numbers the solver multiplies by are
derived on the physics side, because the step is in them:

    a1 = 2*zeta + h*omega    a2 = h*omega*a1    a3 = 1/(1 + a2)

written so gamma is never materialised: `1/(K + gamma)` reduces to
`m_eff * a2 * a3` and `gamma/(K + gamma)` reduces to `a3`. At zero frequency the
guards give (0, 1, 0) and the whole expression collapses to `-Cdot/mass` — the
same expression the motor uses against a zero target — so a joint with no spring
takes the identical float path it took before springs existed.

The guarantee "critically damped never overshoots" holds where the effective
mass is the true one. A door anchored at its **edge** has inertia about the
hinge of `I_cm + m*d^2`, while the spring reads its mass from the axis alone,
which knows nothing about the point constraint holding that edge: measured, the
same spring overshoots 0.036 rad on the door and exactly zero on a body whose
axis runs through its centre of mass. Widening it means solving the hinge as one
coupled system rather than as a point constraint plus an axis constraint.

**Solve order** is an authored integer, low first. Both sweeps are Gauss-Seidel
over one vector, so joint *i* reads what joint *i-1* just wrote — the order was
always the answer, it was simply whatever order the entity pool happened to be
in. A long articulated chain converges far faster root to tip. It is a priority
and deliberately not a dependency graph: cycles are ordinary here (a ragdoll
closed at the hips, a bridge tied at both ends) and a topological sort has no
answer for one, while a sort key always terminates. The sort is stable and runs
once before both sweeps, so joints that share a key keep the order they had —
which is what makes every scene written before the field existed solve as it
did — and the position sweep and the velocity sweep cannot disagree.

### 7g. Convex hulls

The last shape the narrowphase was missing. A ramp with a bevel, a rock, a
wedge, a wing — each is one convex solid, and each had to be approximated by
three or four boxes that never quite fit.

`ConvexHull` builds one from a point cloud by incremental hull: a seed
tetrahedron of four points that actually enclose a volume, then the point
farthest outside the current hull, repeatedly, removing every face it can see
and rebuilding from the horizon. Farthest-first is not an optimisation — it is
what gives the vertex cap a meaning, because stopping early leaves a hull whose
worst error is the distance of the next point it would have taken, reported as
`residual`.

**Coplanar triangles are merged into polygons**, and that is not cosmetic. SAT
clips an incident face against a reference face, so a triangular reference face
on the flat side of a crate gives a contact patch a third of the size it should
be and the crate rocks on the sliver. A cube comes back as six quads.

**The cap is a cap on cost.** Hull-against-hull tests every face normal of both
plus the cross product of every **edge pair**, so the cost is quadratic in the
edge count: two three-hundred-edge hulls would be ninety thousand axes. Sixty-
four vertices bounds it, and hulls are meant to be collision shapes rather than
render meshes.

There is no closed form to check a hull against, the way the heightfield has a
surface both sides can be asked for. So the suite uses two other things:
**structural invariants** that hold for every convex polyhedron and nothing else
— Euler's `V − E + F = 2`, and every vertex behind every face plane — and a
**differential** check that a hull built from a cube's eight corners collides
exactly the way the already-trusted box path does, across five arrangements.

Three things are load-bearing:

- **The reference face is chosen from the normal, not from the axis search.** A
  hull has two faces for every axis — the top and the bottom of a slab both
  answer to Y — and the search flips a normal to point from one shape toward the
  other, so the face that won may be the one facing away. Trusting its index put
  the reference plane at the *bottom* of a floor: every clipped point measured a
  metre behind it, all were dropped, and a crate fell through a slab the SAT had
  correctly reported it was standing on. It appeared in only one of the two pair
  orders, and the broadphase sorts its proxies, so which shape is `a` is not
  something the narrowphase gets to choose.
- **Plane normals go through the inverse transpose.** A hull is the only
  collider for which a non-uniform scale is exact — a sphere collapses its three
  extents to one radius, a capsule its two, but a linear transform of a convex
  set is still convex. Using the basis for a normal is right only while the
  scale is uniform, and the failure is a face whose normal no longer points out
  of it.
- **An oriented box IS the unit cube hull** with its half extents on the basis,
  so box-against-hull is the hull path rather than a fifth pair test with its
  own bugs.

The hull is built from the asset, because the CPU-side vertices are gone by the
time physics wants them: `MeshRegistry` uploads a mesh and keeps only the buffers
and the bounds, and it is renderer-side besides. `ConvexHullCache` loads it
through the same `ModelLoader` and `GltfLoader` entry points, with the primitive
sizes pinned in `ModelLoader` so the collider and the mesh cannot drift apart —
keyed by asset, trimmed at the top of a step, the same discipline as
`HeightfieldCache`.

**Named rather than implied:** a hull is *convex*, so a doughnut collides as a
disc. And a hull against a **heightfield** collides as the box that contains it,
because the heightfield's contact model is written around a point and a radius
and a hull is neither — so a wedge on a hill floats by the gap between itself and
its bounds. It does not fall through, which is the failure that would matter.

### 7h. The Euler convention, and the bug that hid in it

A hinged door exploded, and the cause was not the hinge.

The rotation integrator turned the transform's Euler triple into a quaternion
with `glm::quat(vec3)`, applied the step's spin, and wrote the result back with
`glm::eulerAngles`. **`glm::quat(vec3)` composes the three angles in the opposite
order from `getModelMatrix`.** For any orientation with more than one non-zero
angle it is a different rotation — measured at 0.33 on a matrix entry for
`(0.5, 0.7, 0.3)`, which is not a rounding difference.

Both halves of that round trip used the same wrong convention, so it was
self-consistent and every test passed. What was wrong was its relationship to the
matrix that renders and collides the body: the spin was applied about the wrong
axes. A body turning about **one** axis has one non-zero angle and the two
conventions agree exactly there — which is why nothing caught it. Every rotation
test in the suite spins about a single axis from rest. A door swinging past
ninety degrees picks up a second angle, and the error compounds until the body
reaches two hundred and eighty radians a second.

`TransformComponent::EulerFromRotation` inverts the matrix `getModelMatrix`
actually writes, and `getRotationMatrix` asks `getModelMatrix` for an unscaled
copy rather than writing the nine entries a second time. The gimbal pole — where
`cy` is zero and the two outer angles stop being separable — pins one at zero and
puts the whole turn in the other, which is the only choice that stays continuous
as the pole is approached.

### 7i. The optional world ground plane

A solid plane across the whole world at `groundPlaneY`, applied during
integration, per non-kinematic rigid body, before any collider is gathered.

**It is off unless the scene asks for it**, and that is a deliberate break with
what came before. It used to be unconditional and there was no switch. Nothing
could fall below y = 0 whether or not the scene had a floor, so a pit was not
expressible, a level built below the origin was unreachable, and a body that
should have fallen out of the world stopped dead on nothing. It did all of that
invisibly, because there is no entity to select and nothing to see — the only way
to find out it was there was to notice something not falling. The demo scene
relied on it; it now has a real collider under the floor it was already drawing.

Scenes written before the switch are **migrated**, not broken. Leaning on the
plane was a perfectly reasonable thing to do while the floor was free — a level
needed a collider only under the parts you could fall off — so reading one of
those with the new default would drop everything in it out of the world with no
message. `AssetVersion::Migrate` gives any scene below format version 2 the plane
it was authored against, and leaves alone one that already says what it wants.
That hook had been an empty placeholder since versioning landed; this is the
first thing to use it, and the reason it was worth writing before it was needed.

It is measured in **world** space, through the bottom of the body's collider
bounds. Both halves of that sentence are bug fixes. Clamping the transform origin
buried every body by half its height and made it impossible to rest anything
below the plane; testing the *local* position put the floor wherever the parent
happened to be, so a body parented ten units up rested in mid-air and never fell
at all. The reflection fires only when the body is actually moving into the plane
— inverting unconditionally re-launched bodies that were already rising — and is
zeroed below the rest velocity.

What it is still good for: a prototype scene with no floor built yet, and a
safety net under a level with holes in it. What it is still bad at, and why it is
not the default: it applies the body's own restitution and **no friction at
all**, so a body sliding on it keeps its horizontal speed indefinitely unless
linear damping takes it, and it is not a collider, so it reports no contact, has
no material, and no trigger or script ever hears about it. A rigid body with no
collider is treated as a unit cube for this test, and at unit scale comes to rest
with its origin half a unit above the plane.

`PhysicsSystem::IsGrounded` asks the same setting before counting the plane as
solid. The two live two hundred lines apart and have to agree, or a character
stands on a floor that is switched off.

### 7j. Sleeping

A body that has stayed below `kSleepLinearVelocity` (0.05 m/s) and
`kSleepAngularVelocity` (0.05 rad/s) for `kSleepTime` (half a second) stops being
simulated: no gravity, no integration, no impulses. Its velocity and spin are
zeroed, its position is recorded, and from then until something wakes it, it
stands in as an immovable obstacle for whatever is still awake.

The thresholds sit well under `kRestVelocity`, the speed at which bounce is
killed, on purpose. A body that has only just stopped bouncing has not settled,
and putting it to sleep there would freeze it one step into whatever it was still
doing.

**Sleeping removes the response, not the report.** The pair is still found and
the contact still published. Handing the broadphase the pretence would have been
cheaper — it drops a pair where both sides are immovable — but every settled body
would then fire a spurious *exit* at anything diffing the contact list, and a
trigger volume would forget about whatever fell asleep inside it. The saving is
the integrate-and-solve, not the sweep.

Four things wake a body, and the fourth is a rule about not sleeping at all:

| Cause | How it is noticed |
|---|---|
| Something lands on it | the pair loop wakes either side when the other is a rigid body moving above the sleep thresholds |
| A script writes its velocity | a sleeping body's velocity is *exactly* zero, so anything non-zero was written from outside — this is what keeps `setVelocity` and `addForce` working |
| Anything moves it | its transform is compared against the position it slept at; without this an editor gizmo drags a settled crate into the air and it hangs there |
| It is touching a kinematic body | it is never allowed to sleep in the first place |

The kinematic rule is the conservative one. A kinematic body is moved by code the
solver cannot see: the contact reads the velocity that code writes, but there is
no way to know the velocity is about to change, or that it was written at all -
no way to know the body is about to slide out from under whatever is standing on it — a lift would arrive at the
top floor with its cargo left behind in the air. Anything resting on one
therefore has its sleep timer reset every step the contact lasts. A *static*
collider, which is what an ordinary floor is, wakes nothing: resting on the floor
is the reason to sleep, not a reason to stay awake.

Waking happens **in place**, restoring the mass properties that were zeroed while
the body slept, so the impulse that woke it lands on the same step. One step of
latency is not a small difference here: the positional correction separates the
pair on the step of the impact, so by the next step there is no contact left and
the body that was hit never receives the impulse at all — a ball would bounce off
a sleeping crate and leave it exactly where it was.

Two bodies that are both merely still do **not** wake each other, or two crates
settling side by side would hold each other awake forever, which is the usual way
a sleeping implementation ends up never sleeping. The cost of that rule is one
step of cascade latency: a body woken by an impact still has zero velocity while
the rest of this step's pairs are walked, so a toppled stack wakes one layer per
step.

`allowSleep` on `RigidBodyComponent` opts a body out. It is the only part of this
that is serialised — `isSleeping`, the timer and the recorded position are
runtime state, re-derived within half a second of a scene loading. Writing them
would allow a scene to be saved with a body asleep in mid-air, which would then
never fall.

Verified by running the same scene twice, once allowed to sleep and once not: all
three bodies end at bit-identical positions. A resting body reaches a true fixed
point, where the distance it sinks under gravity in one step and the distance the
positional correction pushes it back cancel to the same float, so a sleeping body
is not frozen *near* where an awake one hovers — it is frozen exactly there.

### 7k. Triggers

A collider with `isTrigger` set is detected and deliberately not resolved: the
overlap is reported in the contact list and the body passes through, which is the
whole point of the flag. Either side being a trigger makes the contact one.

Two limits follow from where that is tested, and neither is visible from the
inspector checkbox:

- The broadphase skips pairs where both sides have zero inverse mass, so **a
  trigger volume only reports against a body with mass.** A trigger volume is
  usually a bare collider with no rigid body, and a character controller is
  usually kinematic — that pairing produces nothing at all.
- Contacts are dispatched per tick by `ContactTracker`
  (`src/core/ContactTracker.hpp`): both entities of a pair get an Enter, Stay or
  Exit event naming the other, with the normal pointing away from the reader and
  whether a trigger was involved. A layer finds it in `registry.ctx()` as a
  `ContactTracker*`, which `SupersonicApp` inserts; a script reaches it through
  the ABI's `contactCount` and `contactAt`. This bullet used to say there was no
  dispatch at all, which stopped being true when the tracker landed - the limit
  above is the one that still bites.

### 7l. World queries

`Raycast`, `OverlapSphere` and `IsGrounded` answer questions about the world
against **colliders**, which is what physics means by solid. Gameplay previously
had no way to ask at all: no ground check, no "what am I looking at", no trigger
radius.

The distinction from the editor's picking ray is real and worth keeping straight,
because a character's visual mesh and its collider are routinely different sizes:

| | `Raycast::PickEntity` | `PhysicsSystem::Raycast` |
|---|---|---|
| Iterates | entities with a world transform and a visible `RenderableComponent` | entities with a collider |
| Shape | local-space AABB — the collider if there is one, otherwise the mesh bounds | world-space AABB, or a sphere |
| Rotation | exact; the ray is transformed into local space | lost; a box is its world bounding box, and so is a capsule |
| Triggers | no distinction | skipped unless asked for |
| For | editor picking | gameplay |

The queries do not share the solver's narrowphase. A rotated box is queried as
the box that holds it, and so are a capsule and a convex hull, which
over-reports: a ray can graze a capsule's shoulder, or a chamfered hull's cut
corner, and be told it hit. Hulls were missing from the queries altogether until
the Magic Portals spike found a body at rest on one that `IsGrounded` said was
standing on nothing. That is the right direction to be wrong in
for "what am I looking at" and the wrong one for a bullet that has to be fair,
and it is the same approximation the solver itself used before SAT landed.

`Raycast` returns the nearest hit with point, normal and distance, and skips
triggers by default because a bullet should not stop at a checkpoint volume. The
slab test returns the entry distance and takes the normal from the entry axis; a
ray parallel to a slab either misses outright or is treated as unconstrained by
it, rather than divided into an infinity that poisons the comparison. A
zero-length direction returns no hit instead of normalising into a NaN that
reports a hit at an impossible distance. The sphere test uses the far root when
the origin is inside the sphere.

`OverlapSphere` appends rather than clearing, so a caller can accumulate across
several queries, and includes triggers by default — the opposite default to
`Raycast`, because a trigger radius is usually exactly what is being asked about.
Boxes are tested by the closest point to the sphere's centre, which is exact
against the box's world bounds.

`IsGrounded` returns true immediately when the point is no more than `distance`
above y = 0, since the solver treats that plane as solid even though no entity
represents it, and otherwise casts a short ray downward ignoring triggers.

Scripts reach `raycast` and `isGrounded` through the POD-only C ABI
(`SupersonicScriptPhysics`), with the live registry passed as the opaque pointer
so a script queries the world it is running in rather than a snapshot of it.
`OverlapSphere` is C++ only.

Every query rebuilds the whole shape list from the registry on each call. There
is no acceleration structure and no cache, so a script that raycasts once per
entity per frame walks every collider in the scene once per entity per frame.

### 7m. Documented departures

Listed rather than hidden, in the same spirit as the rest of this document.

- **No swept collision detection.** Positions are still advanced by
  `velocity * dt` and only then tested. What stops a fast body passing through a
  thin one is *speculative contacts*: the broadphase bound is widened by the
  distance each body travels this step, the narrowphase reports a pair that is
  apart but within that margin with a **negative** penetration, and the solver
  removes exactly the approach velocity the remaining gap cannot absorb, so the
  body lands on the surface instead of crossing it. That is a velocity
  constraint, not a swept test, and it is wrong in both directions. It removes
  approach velocity along the contact normal as found at the *start* of the
  step, so a body arriving at a glancing angle can be braked against the wrong
  face of what it is approaching. And the margin is the distance the pair could
  close in any direction, not the volume it actually sweeps, so two bodies
  passing diagonally near each other are reported and slowed for a collision
  that would never have happened.
- **No persistent broadphase.** The body and proxy lists are rebuilt from the
  registry, the world bounds recomputed and the proxies re-sorted every step,
  whether or not anything moved. Sleeping removes the integrate-and-solve for a
  settled body but deliberately not the sweep, so the cost of the broadphase is
  still proportional to every collider in the scene rather than to the part of it
  that is awake.
- **No warm starting between steps.** The eight solver passes accumulate an
  impulse per contact *within* a step, which is what lets a stack settle, but the
  constraint list is rebuilt from scratch every step and the accumulated impulses
  are thrown away with it. There is no contact cache and no matching of this
  step's manifold points against the last one's, so a tall stack still starts
  each step from zero and converges rather than resuming.
- **Positional correction uses one point per pair.** The velocity solve gets
  every manifold point; the correction gets the centroid, once, because pushing
  out per point would move the body as many times as far as the overlap requires.
  A pair that touches along a face is therefore separated along the average of
  where it touches, not per corner.
- **Kinematic bodies are not simulated.** Integration skips them and their
  inverse mass is zero, so nothing pushes them. Their velocity and spin ARE
  part of every contact they make: the solve reads them (and never writes
  them), friction acts on the surface speed between the two, and a moving one
  sweeps its gap like anything else. So a crate rides a sliding platform and
  goes down with a lift. Moving one is the script's job, and the job has two
  halves: the transform, and the velocity that says how fast it is moving. A
  platform moved without its velocity is only a displacement - it shoves what
  it meets through the positional correction and carries nothing, which is how
  every kinematic body behaved before this was fixed (the Magic Portals spike,
  F1). Joints are unchanged: a joint's end reads no kinematic velocity.
- **Single-threaded, on purpose.** Contacts are resolved in sequence and each one
  writes the velocity the next one reads; splitting that across workers changes
  the answer rather than speeding it up. The job system is spent where the work
  is genuinely independent — terrain generation, tangent bases, particle
  integration.

### 8. Concurrency

The engine is single-threaded by default and parallel in three places. `JobSystem`
is a queue, N workers and one pending counter — no work stealing and no job
graph, because nothing here needs one and a scheduler nobody can reason about is
worse than a loop.

**The pool.** `JobSystem::Initialize(0)` asks for one fewer worker than
`hardware_concurrency`, and never fewer than one, leaving a core for the thread
that submits the work — which is also the thread that records Vulkan commands. A
second `Initialize` is a no-op, so a subsystem can bring the pool up lazily
without coordinating with anyone. `SupersonicApp` brackets it deliberately:
`Initialize` runs before `initECS`, because mesh generation already dispatches,
and `Shutdown` is the last thing the destructor does, after the renderer, device
and window, because a worker holding a reference to any of them would otherwise
outlive it. `Shutdown` drains outstanding work before it joins, for the same
reason. The statistics panel prints `Workers: N`, or `Single-threaded` when no
pool is running.

**The fence is one counter, not one per dispatch.** `Wait()` means "everything
submitted so far has finished", not "my dispatch has finished". `ParticleSystem`
exploits that on purpose — it dispatches every emitter's integration and then
waits once, so several emitters overlap instead of being handled one after
another. It is only benign because every dispatch site is on the main thread.

`Wait()` does not block or sleep: it runs queued jobs itself and yields when there
are none, so a wait from the main thread does not idle a core through a dispatch
it issued. Because the counter is global, it must never be called from inside a
job — it would spin on a counter that its own unfinished group is holding down.

**Where work is actually dispatched.** Three subsystems, in four passes, and
nowhere else. Physics, animation and pose evaluation contain no `JobSystem` call
at all.

| Site | Unit | Group | Why the writes cannot collide |
|---|---|---|---|
| `TerrainGenerator` vertex pass | one vertex | 4096 | every vertex is a pure function of its own grid index; `out.vertices` is resized up front and written by index rather than appended |
| `TerrainGenerator` index pass | one quad | 4096 | each quad owns exactly six index slots, so the winding order is identical to the serial version rather than depending on completion order |
| `MeshData::computeTangents` | one vertex | 2048 | each iteration reads its own accumulator entry and writes its own vertex |
| `ParticleSystem` integration | one particle | 512 | each particle reads and writes only itself, and the emitter fields it consults are read-only by then |

They share one shape: **the destination is sized before the dispatch, and every
job writes only at its own `jobIndex`.** No job appends, resizes or allocates into
shared storage. Where a pass cannot be written that way it stays serial next to
one that can — the tangent *accumulation* loop is untouched because several
triangles add into the same vertex slot, and particle *spawning* is untouched
because it draws from a shared RNG and resizes the particle vector. Integration
is where the cost is anyway: it touches every live particle every frame while
only a handful spawn.

**Captures must outlive the fence, and this is where the real bug was.**
`computeTangents` captures `tan`, `bitan` and `this` by reference, and all three
are gone the moment the function returns. Without the `Wait` the workers write
into freed memory and every caller reads `vertices` while they are still being
written. Nothing caught it locally, because reaching the parallel path needs
*both* a running pool *and* a range longer than one group: `Dispatch` runs inline
when there is no pool, and also when the whole dispatch is a single group, since
handing one group to a worker and then waiting for it costs more than doing it on
the spot.

**What is deliberately not parallel.**

- **Collision response.** The narrowphase loop carries an explicit
  iteration-to-iteration dependency, not a vague hazard: after a positional
  correction it writes the shifted centres back into the cached bodies so the
  pairs still to be resolved *this step* see them. A body wedged between two
  others would otherwise be pushed twice as far as it should be.
- **Command recording.** `VulkanRenderer` creates one `vk::CommandPool` and hands
  it to `MeshRegistry` and `TextureRegistry` as well; the only other pool in the
  engine is the transient one `ThumbnailCache` owns for its one-shot uploads.
  Every command buffer allocated from either is primary — there are no secondary
  command buffers anywhere. Vulkan command pools are externally synchronised, so
  this is a hard constraint rather than a preference.
- **The transform hierarchy.** `UpdateWorldTransforms` resolves iteratively and
  memoises up the parent chain — a child's world matrix is only correct once its
  ancestors are resolved, which makes iteration order a data dependency.
- **Scripts.** `ScriptEngine::Update` iterates the `ScriptComponent` view and
  hands every script raycast, animation and UI function tables that each carry
  the registry pointer, so a script can query the physics world and mutate
  components on any entity in the middle of that iteration.

**The inline fallback is what keeps the tests repeatable.** With no pool running,
`Dispatch` runs the loop on the calling thread, so a caller never has to check
first and a headless test or a packaged tool needs no special case. No suite
except `test_jobs` and `test_meshgen` starts a pool, so every other one takes the
inline path and runs in a fixed order. `test_meshgen` starts a four-worker pool
for one case and requires that a 128×128 terrain built with workers is
bit-identical to the same terrain built without them — position, normal, UV and
tangent on every vertex, and index for index — which pins determinism rather than
merely "it did not crash".

**What `test_jobs` pins.**

| Invariant | How it is pinned | What breaks without it |
|---|---|---|
| Every item runs exactly once | 10,001 items in groups of 128 — a count deliberately not a multiple of the group size | an off-by-one at the tail silently loses a vertex |
| `Wait` is a real fence | 20 repeats, each summing the results immediately after the fence with no further synchronisation | a fence that is merely usually correct passes once and corrupts a mesh later |
| Item *i* lands in group *i / groupSize* | 1000 items in groups of 100, each recording its `groupIndex` | `groupIndex` would be worthless as a key for per-group scratch space |
| A throwing job still decrements the counter | one job throws at index 7; the pool must remain usable afterwards | a leaked counter hangs every later `Wait` — a frozen engine rather than the exception that caused it |
| Degenerate arguments are harmless | `jobCount` 0, `groupSize` 0, a null task | a zero group size divides by zero computing the group count |
| `Initialize` is idempotent and `Shutdown` is safe twice | a second `Initialize(8)` must leave the thread count alone, and the pool must then come back up cleanly | a second pool spun up while the first is still running |
| Work genuinely spreads | distinct `std::thread::id`s are counted, not elapsed time | a timing test that fails spuriously on a slow machine and passes on a fast broken one |

**Profiling across a dispatch.** `Profiler` is deliberately not thread-safe and
not nestable — its accumulator is a plain array behind a function-local static.
Every zone brackets a call made from the main thread in `Run()`, so a worker's
time is accounted to **the zone that dispatched the work and waited on it**, which
is the number that matters to a frame. `Particles` covers `ParticleSystem::Update`
including its integration jobs, and `ResourceSync` covers the terrain and tangent
passes, because `RenderSystem::SyncResources` is what reaches
`MeshRegistry::Acquire` and therefore mesh generation. Mesh generation during
`initECS` happens before the loop and is timed by nothing.

**Threads that exist but are not the pool.** `AudioEngine`'s ALSA backend spawns
its own writer thread, which pulls from `AudioMixer` while the main thread adds
and stops voices — that is the entire reason `AudioMixer` is mutex-guarded. The
Windows backend spawns no thread of its own and constructs no mixer; XAudio2 does
that summing internally, behind per-sound source voices. `AudioMixer` itself is
compiled on every platform — it carries no platform header, which is what lets
`test_mixer` exercise it without an audio device. `Log` is mutex-guarded too, and
its state is a function-local static specifically because a namespace-scope one
would race static initialisation against the first subsystem that logs during
construction, which on this codebase is `JobSystem::Initialize`.

**Limits, stated plainly.** One global counter means there is no way to wait on
one dispatch while another is still outstanding. There is no work stealing, so a
group that takes far longer than its neighbours is never redistributed. And
`Execute`, the single-task entry point, has no call site in the engine at all —
only `test_jobs` exercises it.

### 8b. The seam a game lives in

The engine was an application with an editor fused into it. `SupersonicApp`
owned the registry privately and the frame was a closed sequence of calls to
engine systems, so a game built on this had two options: edit
`SupersonicApp.cpp`, or express its logic through the script plugin's C ABI —
which is deliberately POD-only and is not where a simulation's own data
structures belong.

An **`EngineLayer`** is a peer rather than a subsystem. It gets the registry, it
is called at two defined points in the frame, and the engine assumes nothing
else about it.

| | when | delta | for |
|---|---|---|---|
| `OnFixedUpdate` | inside the physics loop, after the step | always `kFixedPhysicsStep` | simulation, and anything that must be reproducible |
| `OnUpdate` | after the engine's per-frame systems, before world transforms resolve | real elapsed time | interpolation, input, camera — anything keeping up with the display |

The fixed callback runs *after* physics in the same step, so a tick reads the
positions that step produced rather than the previous one's. The per-frame one
runs *before* the transforms resolve, so a layer that moves something has it
rendered this frame rather than next.

`LayerStack` runs layers in push order and detaches them in reverse — the only
safe order when they were pushed in dependency order. There are no priorities:
a game that needs its systems sequenced pushes them in sequence, which it knows
and the engine never could. The stack is walked by **index rather than by
iterator**, because a layer may push another from inside its own tick and that
reallocates the vector being walked; with an iterator the test for it hangs
rather than fails, a dangling iterator being what it is.

`SupersonicApp::Registry()` hands out the real registry. Deliberately: an ECS
whose registry is private is an ECS only its author can use, and every
alternative — a wrapper re-exporting a chosen subset, a message queue, a
component registration API — is a smaller EnTT that a game has to learn instead
of the one it already knows.

Two couplings were cut alongside it. The renderer no longer includes
`editor/Theme.hpp` or `editor/EditorFonts.hpp`; the UI style arrives as a
callback the application supplies, because the renderer is the only thing that
knows *when* fonts may be added — after the ImGui context exists and before the
Vulkan backend initialises — and is not the thing that should know what a font
is. And the editor's ground grid is drawn only when there is an editor: it sat
at the end of the scene pass with nothing in front of it, so a packaged game
opened on its own level with the grid and the origin axes drawn across it.

**Changing level.** `SceneManager` performs the load, and for a long time
nothing outside the editor could reach it: it was a private member of
`EditorLayer`, `EngineLayer` is handed only the registry, and the plugin ABI had
no scene entry. A packaged game therefore opened whatever the manifest named
and stayed there for the rest of its life — no menu to level, no level to level,
no restart on death — against a class whose entire job is to perform exactly
that transition.

It is owned by `SupersonicApp` now and published into the registry context as
`SceneManager*`, the same way `ContactTracker*` and `AudioEngine*` are, which is
how anything holding the registry reaches a service it cannot otherwise see. A
pointer rather than a value, because a scene load CLEARS the registry it would
have been stored in; `entt::registry::clear()` empties the component pools and
the entity list and leaves the context alone, and a test pins that, because a
script calling `loadScene` twice depends on it.

The load stays deferred, and the deferral is the whole design: `RequestLoad`
records the wish and `ApplyPending` performs it after the frame's iteration has
finished. A script runs inside a view over the registry, so a load performed
where it was asked for would invalidate what the caller is walking. The editor
keeps only what is genuinely its own — dropping a selection that names an entity
in a scene that is gone, and resetting an undo stack that describes one.

Still fused: the scene target lives in `EditorLayer`, and the scene pipeline is
built against its render pass. A game needs that target too — it is the HDR and
bloom chain — so this is misplaced ownership rather than a missing feature.

### 8c. Simulated time, and proving a run reproduces

Nothing here had a notion of simulated time. Everything that needed a clock read
the frame delta — the real, variable, machine-dependent time the last frame took
to draw — and accumulated it. `ScriptComponent::elapsed` did exactly that and
drove `std::sin` off the result, so every scripted motion in the engine was a
function of how fast the display was keeping up.

That was measured rather than suspected: three runs of one binary over one scene
produced three different images. It is also what stopped the shadow-pass cache
from being tested end to end — a rendered frame could not be compared against
anything.

**`SimulationClock`** is a tick counter in the registry's context, advanced once
per fixed step. Its seconds are *derived* — `tick * fixedDelta`, computed fresh —
rather than summed: a float added sixty times a second drifts, and drifts
differently depending on where the sum started, which would put the problem back
where it was found. There is a test that runs both for a hundred thousand ticks
and shows the summed one has left.

**`StateHash`** reduces the simulation to one number, so "it reproduces" stops
being a claim nobody can check. A screenshot cannot do this job: it compares
what was *drawn*, which is lossy and quantised to eight bits, so two runs that
have already diverged can photograph identically. Three decisions in it are
load-bearing:

- Floats are compared **by their bytes**. A tolerance would be a decision about
  how far two runs may drift before it counts — the exact question the test
  exists to answer, so it cannot also be the test's parameter.
- It is **order-independent**. EnTT iterates in an order that comes from how
  components were added rather than from the state, and folding them in sequence
  would report that as a divergence.
- Each entity's contribution is **seeded by its own id**, so two crates swapping
  positions is a different world even though the set of positions is unchanged.

Sleep state is in the hash. A body asleep on one machine and awake on another
has not diverged yet and will on the next thing that touches it.

**`--fixed-step`** feeds the simulation a constant delta. The simulation was
already deterministic given a tick count; what varied was how many ticks fit
into a frame, because the accumulator is fed real time. Pinning the delta pins
the tick count, and five runs of one scene then produce one image. Only the
simulation is pinned — the profiler still measures real elapsed time per zone —
and it stays opt-in, because a game that ignores how long a frame took plays in
slow motion the moment it drops below its target rate.

The three things this section used to end by listing as not done are done, and
each was a separate confusion rather than a missing feature:

- **The game tick is an authored rate.** It was the physics step, because there
  was one loop and therefore one number, so "how often does the world think" and
  "how often does the solver integrate" could not be asked separately. A scene
  writes `Simulation.TickRate` and the loop runs physics as substeps underneath
  it — a 20 Hz tick runs three 1/60 steps — so choosing a tick rate does not
  quietly change how every collision in the project behaves.
- **A frame is drawn between the last two ticks**, by the overstep fraction the
  tick loop could not consume. Without it a 20 Hz simulation is drawn at 20 Hz
  however fast the display runs and looks exactly like that. The fraction is
  written after the loop and is deliberately not readable from inside a tick:
  a tick that read it would depend on the frame rate, which is the whole thing
  this section exists to prevent.
- **Time the loop cannot run is counted rather than vanished.** It is still
  dropped, and dropping is right — catching up under sustained load never
  catches up. What was wrong was the silence: a machine that could not keep up
  ran every mission timer short and told nobody. `droppedSeconds` records both
  sources, the tick loop hitting its ceiling and the older `kMaxFrameDelta`
  clamp, which is the easier of the two to miss.

What that leaves is §8e: the inputs, which are the last thing a run needs to
reproduce and the only one that cannot be derived from the scene.

Two things the hash learnt later, both found by asking what a replay would fall
through. It was seeded on `entt::to_integral` — the whole EnTT handle, which
packs an index with the recycle counter `registry.clear()` bumps — so **the same
scene loaded twice hashed differently**, and every determinism claim was
implicitly scoped to one process that had loaded exactly one scene. And it
opened on `view<TransformComponent>`, which put the *count* inside the filter
too, so an entity without a transform was invisible twice over and a scene of
scripted HUD elements hashed identically to an empty registry. Both are fixed;
both had tests that could have been written at any point and were not.

### 8e. Replay

Determinism on its own is a property nobody can spend. §8c can prove one scene
stepped N times lands in one state — but a *run* is not only a scene and a tick
count, it is also everything the player did, and that had nowhere to be written
down. So a bug a player hit could still only be described, which is exactly what
determinism was supposed to stop being necessary.

`--record <path>` writes every tick's input and a state hash every second.
`--replay <path>` feeds it back and checks it, printing the first tick that
disagrees and exiting non-zero. The two are refused together: a run recording
the input it is being fed writes a file that agrees with itself by construction.

**What is recorded is what the tick was handed, not what the devices did.** The
obvious alternative — store the keys and stick positions, re-derive the rest —
cannot work, and the reason is easy to miss: some of what a tick reads is
computed per *frame*. A mouse delta is the clear case and a UI click is the one
that surprised us. A frame running three ticks hands the same delta to all
three; a frame running none hands it to nobody. Re-deriving that where the
frames fall differently produces different numbers from the same file. Freezing
the resolved value also makes a replay survive a rebind for free: the file says
the player moved, not that they held W.

Getting there meant moving three more channels off the frame and onto the tick
first — `wasReleased`, the contact list, and clicks. Each was found by asking
which channel was next, and that enumeration *is* the list a recording has to
serialise, so it was a prerequisite rather than a detour.

**Levels persist and edges do not**, which is the rule the encoding turns on.
Held actions and axis values carry until a line changes them; press, release,
click and the mouse delta belong to exactly the tick that names them. A format
that held a press the way it holds a key-down would report one keystroke on
every following tick — undoing, in the file, the latch that exists to give a
keypress to exactly one tick. Most ticks repeat the one before, so holding a key
for three seconds is two lines rather than a hundred and eighty.

**Floats are written as their bits.** Nothing here writes a decimal float that
reads back bit-identical — `ComponentCodec` uses the iostream default of six
significant digits — so a decimal axis value would not be the value the tick
saw, and the run would diverge slightly and intermittently for a reason that is
not a bug in anything.

**The tick count is written twice**, at the top and at the end, and that is not
redundancy: delta encoding makes half a file syntactically perfect. The reader
would carry the last levels forward, hand back the number of ticks the header
promised, and report success on half a session. Nothing in the data can reveal
that, so the file has to say where it ends.

Checkpoint zero is taken after the load and before the first tick, so a mismatch
there says *this is not the scene that was recorded* rather than letting the run
diverge for four thousand ticks and reporting the symptom. It is necessary and
not sufficient — the hash covers what a tick can change, not everything a scene
load establishes.

**A game's own state is in the oracle too, and had to be opened for.** Everything
`StateHash::Compute` walks is a registry, and a game built on an `EngineLayer` —
which is the shape this engine recommends — usually keeps its authoritative state
in a C++ object graph instead. Wolf Brigade's is a match, its units and a map of
resources, owned by a layer; none of it is components. So the oracle would have
walked a registry containing none of it and returned a number that agreed with
itself perfectly: a replay reproducing the *engine* and reporting success while
the *game* diverged on tick one.

`StateHash::RegisterContributor` closes that, mirroring `ComponentCodec`'s
registration — which was opened to a game's own components much earlier, and the
asymmetry was the bug. A contributor is handed a `Mixer` rather than returning a
number, so a game cannot accidentally use a different fold; contributions are
seeded by name and **added**, so the order they were registered in cannot change
the answer, which is the entity walk's argument about EnTT's iteration order one
level up. With none registered the hash is byte-for-byte what it was before any
of this, because a recording made earlier has to keep comparing against the
engine that made it.

**One number is written down, and it is the only test here about a machine
rather than about the code.** Every other determinism test compares two hashes
computed in one process, so a build whose floats behave differently agrees with
itself perfectly and passes all of them — reproducibility that holds within a
binary and not between two, which is the same failure one level up. So
`test_determinism` pins the hash of four seconds of the fixture scene as a
constant, and **it is one number across C runtimes**: MSVC 14.50 on the UCRT
and GCC 13.3 on glibc 2.39 both give `6794834318059694172` (`881310125714727098`
before the face-bias fix in the SAT details, which changed how the scene's boxes
come in to land).

It used to be one number per runtime. The Windows toolchains all linked the
UCRT and agreed with each other, while GCC and clang on glibc gave
`7854318744396420989`.

Measuring the split found it was entirely libm, one ulp at a time: the first
disagreement was an `atan2f` in `EulerFromRotation` on tick 29. On every
sampled case the UCRT was correctly rounded and glibc was not. Nothing in the
C standard asks either to be.

So the simulation path stopped calling libm. `DetMath.hpp` computes sin, cos,
asin, atan2 and pow in double from + - * / and sqrt and the exact functions,
rounds once to float, and is correctly rounded on every input sampled.
Contraction is off engine-wide. `test_detmath` pins DetMath's bits against a
table produced independently of any C++ compiler, so a toolchain that
reorders or fuses an operation fails a named row instead of a replay.

When the constant fails it is a decision rather than a chore:
- **The simulation changed**, and every recording on disk has stopped
  comparing.
- **The platform differs**, and that is now a finding about the platform, not
  a second number to write down.

Updating the number without deciding which is how the test stops meaning
anything.

One measured caveat worth keeping. Replaying the demo scene reproduces, and that
proves less than it looks: **nothing in `MainScene` reads input inside a tick**,
so changing a recorded mouse delta and replaying it produces an identical hash.
The claim that recorded input actually drives state is carried by
`test_replay`'s last few cases, which run a simulation that reads input and
check that changing one tick of it moves the world. An end-to-end run is only as
strong as the scene it is run against.

### 8d. Saving a game's own components

`ComponentCodec` is the single reader and writer for an entity, and it named
eighteen engine components and could not be opened to anything else. That is a
hard limit on what can be built here: a mid-match save and a rollback snapshot
are made of a game's own component types, and neither could be written by the
thing that writes every other component. A game's options were to fork the codec
or keep a second serializer beside it — and two writers over the same data always
drift, which is the argument this file was created to make.

A game registers a key, a writer and a reader. The writer emits only the
**value**; the codec owns the key, the indentation and the punctuation, so a
game cannot produce a file that fails to parse by forgetting a comma.

Everything registered is written inside a single `"Game"` member rather than
beside the engine's own keys. Not tidiness: it makes a collision between a
game's component name and an engine one impossible, now and for every component
the engine ever adds. A game naming something `Transform` is unremarkable and
must not be a scene-corrupting mistake.

Written **before** `HasRenderable`, which is the engine's last member and the
only one emitted without a trailing comma — that contract is what lets the
caller close the object.

Registration is global rather than per-registry, because it maps a *type* to its
format and a type does not mean two different things in two scenes.

Prefabs are parsed once and cached by path. Every spawn used to open the file,
read it and run the whole parser to produce a document identical to the last
one. Saving a prefab drops it from the cache, a prefab that failed to parse is
never cached — remembering a failure would make it stay broken until the editor
restarts — and prefab files are watched from startup so an external edit
invalidates them.

### 9. Game runtime and packaging

#### The manifest

There is one binary. The editor and a shipped game are the same executable, and
what tells them apart is `game.manifest`, a JSON file the packager writes next to
it. `GameRuntime::Load` reads it from `ExecutableDirectory()`, not from the
working directory, because a game is normally launched from somewhere other than
its own folder — and because the editor's own build folder must never contain
one.

| Key | Default | Effect |
|---|---|---|
| `Game` | `false` | The only thing that marks a game. The other keys are read only once it passes. |
| `Title` | `Supersonic Game` | Window title. The editor's is `Supersonic Engine`. |
| `StartupScene` | `assets/scenes/MainScene.scene` | Scene loaded before the first frame. |
| `Width` | `1280` | The window the game opens at, in pixels. In game mode the offscreen target follows the window every frame, so it is the render resolution too. |
| `Height` | `720` | The other half. Half a size is not a size, so a manifest naming one without the other gets the default pair rather than that width against somebody else's height. A value outside 64..16384 is refused *and logged* — a size nobody can see is a mistake, and a window they did not ask for with nothing to explain it is worse. |

The `Game` key, not the file's existence, is the switch, so a stray manifest in a
build tree cannot turn the editor into a game. `Parse` returns immediately when
it is false, which is why the other keys are never even looked at in the
editor case. A manifest that exists but does not parse logs an error and returns
the editor's manifest: there is nothing else to do without a scene to load, and
starting the editor silently would hide a packaging bug behind a window that
looks like it works.

`--window <W>x<H>` overrides the size for one run. The precedence — the flag,
then the manifest, then 1280×720 — is `GameRuntime::ResolveWindowSize`, a pure
function taking the manifest and the two option numbers, rather than three `if`s
inside `SupersonicApp`. It lives there because `SupersonicApp` needs a device, a
window and a swapchain before it can be constructed, so no suite can reach an
ordering written inside it; and the order is the whole feature, since reversing
it makes a flag that quietly does nothing.

`Parse` and `Serialize` are separate from `Load` so the format is testable
without a packaged folder on disk, and the packager and the loader both take the
filename from `GameRuntime::kManifestFilename` so they cannot disagree about it.

#### What a packaged game does differently

The difference is settled in two places. `SupersonicApp`'s constructor reads the
manifest before the window exists — the title depends on it — and from it decides
the window title, the startup scene and whether the first frame is already
playing. Everything that gets *drawn* follows instead from one branch at the top
of `BuildUI`, selected by the `isGame` flag handed to `EditorLayer::SetGameMode`.

| | Editor | Packaged game |
|---|---|---|
| Window title | `Supersonic Engine` | manifest `Title` |
| `BuildUI` | menu bar, dockspace, panels, editor shortcuts | `buildGameView`, and nothing below that line |
| `imgui.ini` | written | `IniFilename` set to null; a player's folder is no place for editor litter |
| Presentation | offscreen image inside the Viewport panel | the same image as one quad filling the window |
| Camera aspect | from the panel | from the window |
| UI rectangle | the Viewport panel | the whole main viewport |
| Pointer ownership | only while the viewport is hovered and no gizmo is being dragged | always the game's |
| Startup scene | the demo scene `initECS` builds, unless `--scene` | manifest `StartupScene`, which replaces it |
| Mode | Edit until Play is pressed, unless `--scene` | Playing from the first frame |

The startup branch is `m_manifest.isGame || !m_options.scenePath.empty()`, so
`--scene` loads a scene and enters Play in the editor too — a smoke test is only
worth running against the scene you want to smoke test. `applyScene` clears the
registry before rebuilding, so the demo camera, sun and cubes `initECS` just
created are gone rather than sitting alongside the loaded scene. `PlayMode::Play`
then runs immediately, because a game has no edit mode to be in: without it
nothing would move, no script would run and no sound would play. A packaged game
that opened the demo scene in Edit mode was the clearest symptom that packaging
shipped an editor.

A scene that fails to load does not stop the launch. The load is reported, the
failure is logged as *falling back to the built-in scene*, and Play starts
anyway — on the demo scene, in a game window, under the manifest's title. A
manifest naming a scene that is not there therefore looks like a shipped game
with the wrong content in it rather than like an error.

What the game process still carries is everything else. `EditorLayer` is
constructed and `Init` runs unconditionally, so a shipped game builds the
offscreen target — which it needs — and the content browser's `ThumbnailCache`,
which it does not. `SetGameMode` changes what `BuildUI` submits, nothing more.

#### What packaging actually is

`GamePackager::PackageStandaloneGame` copies the running executable — located
through `ExecutablePath()` rather than guessed from a build layout — into the
folder it is given, copies the shared libraries beside it, copies the asset trees
and writes the manifest. Library copies whose stem contains `.loaded` are
skipped: hot reload works by loading numbered copies of the script plugin, so a
build tree accumulates `GameScripts.loaded1.dll` and friends, and shipping those
puts a stale copy of the game's scripts in the release folder.

It is a copy, not a build. What that costs:

- **The shipped binary is the editor.** ImGui, the dockspace, the inspector, the
  content browser and the packager itself are still linked into it. The branch in
  `BuildUI` is the only thing that stops them being drawn, and a boolean in a
  text file beside the executable is the only thing that sets that branch.
- **`StartupScene` is hardcoded** in the packager to
  `assets/scenes/MainScene.scene`, rather than being the scene currently open.
  The output folder is a parameter, but its one caller — the File menu's
  *Package Standalone Game* — always passes `dist/GameRelease`. The `Title` is
  just that folder's name.
- **A missing asset tree is not a failure.** `copyTreeIfPresent` appends a note
  to the result message and returns success. The list — shaders, scenes,
  textures, models, materials, audio, prefabs, branding — is explicit because a
  packaged game once started with no textures, no models, no audio, no materials
  and no prefabs, every one of which a scene file names by path and expects to
  find.
- **A failed library copy is discarded.** The error code is cleared inside the
  loop, so a game missing a runtime library still reports that it packaged.

#### Two anchors that do not agree

The manifest is found relative to the executable. Almost everything it then names
is opened relative to the working directory.

```
executable directory -> game.manifest
                        GameScripts.dll, checked here first

working directory    -> assets/shaders/*.spv
                        the scene the manifest names
                        every texture, model, material and audio path in a scene
                        the assets/ trees the packager copies FROM
                        build/Release and build/Debug, the plugin fallback
```

The reason `GameRuntime::Load` uses the executable's directory — that a game is
launched from somewhere other than its own folder — is precisely the case in
which every path in the second column breaks. The two anchors coincide only while
the working directory happens to be the game folder. Start the same executable
from anywhere else and it finds its manifest, opens a window under the manifest's
title, and then dies: both shader readers throw on a `.spv` they cannot open,
and `main` turns that into a message box and a non-zero exit. That happens while
the offscreen target and the pipelines are being built, before `initECS` and
before the scene is loaded, so none of the other broken paths are ever reached.
Whichever anchor wins, both columns have to use it.

### 10. The in-game UI canvas

`UICanvas` is screen-space layout arithmetic and nothing else: no ImGui, no
Vulkan, no registry. That is the point. The failures here are placements that go
wrong at resolutions nobody tested on, and clicks that land on the wrong thing —
neither of which is visible in a screenshot, and both of which can be tested
without a device.

Placement has two parts. Every element names a `UIAnchor` — one of a 3×3 grid of
edges and corners — because an offset alone puts the score in the top-left at
1280×720 and somewhere in the middle of a 4K screen. That offset then always runs
**inward** from the anchor, so `(16, 16)` means "16 in from the corner" whichever
corner is chosen rather than pushing a bottom-right element off the screen. On a
centred axis there is no edge to come in from, so the offset is applied
positively. `anchorFraction` derives the horizontal and vertical fractions from
the enum's integer value (`index % 3`, `index / 3`) instead of switching on it,
so adding a row or a column cannot leave one case behind. Sizes, offsets, corner
radii and font sizes are authored against `kReferenceHeight`, 1080, and
multiplied by `screenSize.y / 1080`, which keeps a health bar the same fraction
of the screen on a laptop and on a 4K monitor.

`Place` itself is pure pixels; the scale multiply happens at the call sites, and
only two functions do it — `UIInput::Update` and `UISystem::Render`. They place
an element from the same component through the same function in the same frame:
`Render` runs the input pass itself rather than exposing an update a caller could
forget or order differently. Computing the rectangle twice is the surest way to
end up with a click target that does not match what is on screen.

Interaction is a small per-button state machine, and its rules are the part worth
writing down:

- **A press arms only on the edge** — down this frame, up last frame. Otherwise
  dragging a held pointer across a menu presses everything it crosses.
- **An armed button stays pressed while the pointer slides off it.** The
  rectangle is deliberately not re-tested while the press is held. Every real
  toolkit behaves this way, and a player adjusting their aim mid-press should not
  lose the press.
- **A click requires the release to happen over the button the press started
  on.** Testing only "the pointer is up and over the button" fires for a press
  that began somewhere else entirely.
- **`pointer.active == false` resets everything, hover included.** A button left
  armed while a dialog is open would fire the moment the dialog closed.
- **An invisible button is not a target.** Hiding a menu is how a game closes
  one, and a hidden menu that still swallowed clicks would block the game
  underneath it.
- **A disabled button draws greyed and keeps its place**, because a menu item
  that vanishes when unavailable moves everything below it. `UIInput` disables it
  by clearing `active` for that one button, which is the same reset an inactive
  pointer produces.

`wasDown` is derived from ImGui's edge queries each frame rather than remembered
across frames, so it cannot fall out of step with the frame. The pointer comes
from ImGui rather than from the engine's `Input` layer for the same reason the
HUD is drawn into an ImGui draw list: hit testing has to happen in the coordinate
space the drawing happened in. `active` is where the two worlds meet — the editor
passes `m_viewportHovered && !ImGuizmo::IsUsing()`, so a panel over the viewport
or a gizmo drag takes the pointer away from the HUD, and `buildGameView` passes
true unconditionally, because in a shipped game there is nothing else on screen
to take the pointer.

Drawing uses `ImGui::GetWindowDrawList()` and ImGui's font atlas. It is the only
text in the binary, and a second glyph rasteriser drawing the same fonts into the
same window would be a lot of code to arrive back where this starts. It takes the
window's draw list rather than the foreground one, so the HUD obeys ImGui's
stacking, and clips to the game rectangle, so a HUD anchored to the bottom of the
screen does not spill across the inspector below the viewport panel. Panels draw
first, then buttons, then text, so a label reads over its backdrop regardless of
the order the entities happen to be in.

Scripts reach the UI through `SupersonicScriptUI` in the plugin ABI —
`wasClicked`, `isHovered`, `setText`, `setVisible`, `setFill` — keyed by the same
`unsigned int` entity id the rest of that header uses. Without it the UI was
one-way: a button could be pressed and nothing in the game could find out.
`setText` copies the string immediately and does not retain the pointer, so a
script may pass a stack buffer it is about to overwrite. `hovered`, `pressed` and
`clicked` are rebuilt from the pointer every frame and are deliberately absent
from the scene format, because a button saved mid-press comes back stuck and a
click restored from a Play snapshot fires an action nobody asked for.

`UIInput::Update` used to return the number of buttons clicked this frame, so a
caller could ask whether the UI took the click before letting it through to the
world. Nothing called it, and **no correct guard could have been built from it** —
so it is gone rather than wired up.

It was wrong about the edge: a click lands on the *release*, and anything needing
a guard fires on the *press*, so on the frame a guard would read the count it is
zero for every button. Gating on it would have been a no-op that looked like a
fix. It was wrong about the set: only buttons were counted, and a full-screen
backdrop panel — supported here on purpose so a menu swallows clicks meant for
the game behind it — is not a button, so the count read zero in exactly the case
the guard existed for.

The question a guard has to ask is who owned the pointer when it went *down*.
`topmostUnderPointer` already computes that, over panels and fields as well as
buttons, and throws it away; that is where to start. Anything the simulation
reads has to come off `clickedThisTick` instead, because that is what a replay
assigns and no frame count is ever recorded. The one live consumer still without
a guard is the editor viewport's pick test.

### 11. Audio

`AudioEngine` is one class with three compile-time bodies. The preprocessor in
`AudioEngine.cpp` chooses between them and CMake decides what to link, so
nothing above `AudioEngine` carries a platform branch.

| Platform | Backend | Where sounds are summed | Selected by |
|---|---|---|---|
| Windows | XAudio2 | XAudio2 itself — one source voice per sound onto the mastering voice | unconditional; `xaudio2` and `ole32` ship with the OS |
| Linux, ALSA headers present | ALSA | `AudioMixer`, on a thread the engine owns | `SUPERSONIC_AUDIO_ALSA=1`, defined when `find_path` and `find_library` both succeed |
| Linux, headers absent | none | — | CMake prints `Audio backend: none (install libasound2-dev for sound on Linux)` and carries on |
| macOS, Android, anything else | none | — | falls through to the `#else` no-op |

A missing ALSA package must not fail the build for someone who does not care
about sound, so the no-backend path is a real implementation rather than a
compile error. That leaves two failure modes worth keeping apart: **no backend
compiled**, whose status is `no audio backend compiled for this platform`, and
**a backend that could not open a device**, whose status names the call that
failed — `XAudio2Create failed (hr=…)`, `CreateMasteringVoice failed (hr=…)`,
`snd_pcm_open failed: …`, `snd_pcm_set_params failed: …`. Both end with
`IsAvailable()` false and `Play` refusing before it reaches a device, and only
that string distinguishes them — it is logged once at startup and otherwise
reachable only through `GetStatus()`, which nothing calls.

Which produces the one sharp edge here: `AudioSystem::Update` never asks
`IsAvailable()`. With no device, `Play` returns `kInvalidVoice`, the update
loop reads that as a bad clip and latches `failedToLoad`, and the inspector
prints **"Clip failed to load."** beside a perfectly good WAV. The asset is
fine; the device is missing, and the only place that says so is the log.

`AudioClip::LoadWav` is a from-scratch RIFF/WAVE reader — enough to make
`AudioSourceComponent` mean something without taking on a codec dependency. It
walks the chunk list instead of assuming the canonical 44-byte layout, because
real files routinely carry `LIST` and `fact` chunks ahead of the data; it
honours word alignment between chunks and stops cleanly on one whose declared
size runs past the end of the file. It accepts format tag 1 (PCM) and 3 (IEEE
float), including `WAVE_FORMAT_EXTENSIBLE`, where the real tag is read out of
the sub-format GUID; one or two channels; and 8, 16 or 32 bits. Anything else
is rejected with a message naming the file and the reason rather than guessed
at. Those checks are independent, though, and the clip does not keep the format
tag, so a 32-bit integer PCM file passes and is then read as float everywhere
downstream — by the mixer and by the `WAVEFORMATEX` the XAudio2 path builds
from the bit depth alone. `AudioMixer`'s comment claiming the loader only
accepts 32-bit as IEEE float states the assumption, not a check. The mixer can
also decode 24-bit samples, which the loader will never hand it.

Clips are decoded whole into memory the first time something plays them, on the
game thread, inside `AudioSystem::Update`. A long ambient track is a hitch on
the frame it starts; nothing streams and nothing preloads. Decode failures are
cached so a missing file is not reopened every frame — and that cache is never
cleared, so the inspector's *Retry Load*, which only clears the component's
`failedToLoad`, re-enters `LoadClip`, finds the cached empty clip, and sets the
flag straight back. In Edit mode it does not even do that, because
`AudioSystem::Update` only runs while playing: the red line vanishes on the
click and returns on the next frame that simulates.

Positioning is deliberately modest. The listener is a camera: its position and
its right vector. Volume is inverse-distance — full inside `referenceDistance`,
`referenceDistance / distance` beyond it, silent past `maxDistance` — which is
better behaved near the listener than the `1/(1 + 0.1*d^2)` falloff it replaced.
Pan is the dot product of the direction to the source with the listener's right
vector, so turning the camera moves a sound between the speakers. A voice is
started at volume zero and given its attenuated volume by `SetVoiceParameters`
later in the same update, so nothing gets a frame at full volume before its
distance is known.

Two things are reserved rather than done: the listener's forward vector is read
and then discarded, the code marking it reserved for cone attenuation and
Doppler, and delta time is accepted by `Update` and discarded too, which is the
other half of what a Doppler term would need. Three more are simply not wired
up. The listener is whichever camera the view yields first — pool order, not
the `isPrimary` flag that `FindPrimaryCamera` exists to honour.
`AudioListenerComponent` is serialised and round-tripped and read by nothing:
no panel adds it and no system consults it. And source positions come from
`TransformComponent`, which is local to the parent, so a source attached to a
moving vehicle attenuates and pans from its offset rather than from where it
actually is.

Voice lifetime is tied to the entity, not to the sound. `AudioSystem::Attach`
stores the `AudioEngine*` in the registry context and connects
`on_destroy<AudioSourceComponent>`, which EnTT fires while the component is
still readable, so the hook can stop the voice it names. That covers
`registry.destroy()` and `registry.clear()` alike — scene loads and shutdown as
well as deleting one entity. Sources default to looping, so before this
existed, deleting an entity left an infinitely looping voice playing until the
process exited. The handle lives in the component as runtime state and is
deliberately not serialised; it means nothing in the next session. Teardown in
`~SupersonicApp` follows from the same rule — clear the registry, then
`Detach`, then destroy the engine, because the reverse closes the device with
voices still alive on it.

`AudioMixer` is the summing the ALSA path has to do for itself, with no
platform header in it, which is also what makes it testable without an audio
device. A voice is little more than a pointer to a clip, a fractional cursor
and a step — pitch and sample-rate conversion are the same operation, and a
fractional cursor is what stops every pitch being quantised to a ratio the
source rate happens to allow. Its semantics match the XAudio2 path on purpose:
volume clamped to [0, 1], pitch as a frequency ratio clamped to [0.5, 2],
constant-power pan. Clamping happens after summing rather than per voice, so
two half-volume sounds can still add up to full scale. The ALSA backend's
thread is not a job-system user: it is a `std::thread` paced by
`snd_pcm_writei` blocking until the device has room, and it recovers from
underruns but exits on an error `snd_pcm_recover` cannot fix, because spinning
on a dead device would peg a core forever. It is the only audio code the engine
runs off the game thread, and the mixer's mutex is what makes that safe; the
XAudio2 path holds no lock of its own, because every engine call into it
arrives on the game thread.
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
  `LightComponent`, and the script ABI carries no way to reach one. The ABI is
  a long way past "only a transform" now — at version 8 it carries input, world
  queries (`raycast`, `isGrounded`), animation control, the UI canvas, authored
  parameters and per-entity state, contacts, spawn and destroy, velocity and
  force, and `loadScene` — but every one of those is a fixed function-pointer
  block, so a script still cannot name a component the ABI did not anticipate.
  Widening it is an ABI bump, and the layout pins in `ScriptPluginApi.h` are
  what make that impossible to forget.

## Platform support

| Platform | State |
|---|---|
| Windows | Working. Primary development target. |
| Linux | Should build and run; not verified on hardware. |
| macOS | Instance creation is wired for MoltenVK (portability enumeration + bit). Not verified on hardware. |
| Android | **Not functional.** GLFW has no Android backend and the manifest expects a NativeActivity shared library CMake does not produce. See `platform/android/build_android.sh`. |
| iOS | **Not functional.** Same windowing problem, plus bundling and signing. |
