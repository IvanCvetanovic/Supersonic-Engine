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

### 4c. Lighting and shadows

The UBO carries up to **8 lights** (`kMaxLights`) with a count, and the fragment
shader loops over them. The cap is fixed and small so the whole set fits in a
plain uniform buffer, with no storage buffer and no bindless machinery.
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
| Placement | `direction`, pointing **toward** the light | `TransformComponent::position` | `TransformComponent::position`, `direction` for aim |
| Shadow target | 4 layers of one 2048² array image | one 1024² cube per caster | one 1024² array layer per caster |
| Casters | 1, and it must be `lights[0]` | `PointShadow::kMaxShadowCasters` (2) | `SpotLight::kMaxShadowCasters` (2) |
| Lookup | pick a cascade by view depth, project, 3×3 PCF | sample by direction, 5 taps | project through `spotViewProj[slot]`, 3×3 PCF |
| Slot index | implicit: index 0 | `attenuation.y`, or -1 | `attenuation.w`, or -1 |

The placement row is precise about the component: `gatherLights` reads
`TransformComponent`, the local transform, not `WorldTransformComponent`. That
is a departure from §3b, written down here rather than smoothed over.

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

**Descriptor sets.** Set 0 is per-frame and has five bindings; set 1 is
per-material and has two, rebound per draw.

| Set 0 | Contents | Stages |
|---|---|---|
| 0 | UBO: camera, cascade transforms and splits, ambient, spot transforms, the light array | vertex + fragment |
| 1 | cascade shadow maps, `sampler2DArray`, 4 layers | fragment |
| 2 | this frame's joint palette, storage buffer | vertex |
| 3 | point shadow cubes, `samplerCube[2]` | fragment |
| 4 | spot shadow maps, `sampler2DArray`, 2 layers | fragment |

The three sampler shapes are not interchangeable. The cascades must be one array
image because the per-fragment cascade choice is not dynamically uniform — it
differs within a quad at every split seam, which is undefined behaviour rather
than a style preference. The point shadows are an array of two separate cube
descriptors rather than one cube-array image, so no optional device feature is
required, and the slot they are indexed with is read out of the light block
rather than computed per fragment. The spots are a single descriptor over a
layered image, because a spot is sampled exactly like a cascade.

Set 1 is albedo and a tangent-space normal map. A single set is what previously
forced every object to sample one globally bound texture.

Three values are duplicated into `shader.frag` by hand, each with a comment
naming the C++ constant it must match: `POINT_SHADOW_CASTERS`,
`SPOT_SHADOW_CASTERS` and the point light near plane, `0.05`. Nothing links
them, so the match is maintained by hand. That is the drift the depth pass
avoids structurally instead: it takes its transform in a push constant, so
`shadow.vert` never declares the UBO block at all and cannot fall out of step
with the three other declarations of it — a mismatch nothing diagnoses.

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
| Data textures (normal maps) | `R8G8B8A8Unorm` | a normal map stores directions; a transfer function would corrupt them |
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

### 6. Scripting & hot reload

Scripts are looked up by name in `ScriptRegistry`. Built-ins and plugin scripts
use the same POD-only C ABI (`core/ScriptPluginApi.h`), which is what makes
reloading safe: nothing with a C++ layout, no `std::string` and no ownership
crosses into a module that gets unloaded while the process keeps running.

`HotReloadEngine` shadow-copies the plugin before loading so the build output
stays unlocked, debounces the write because linkers emit output in several
passes, and unregisters the plugin's scripts *before* freeing the module.

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
    gravity, linear damping, angular damping
    integrate rotation as a quaternion, write it back as Euler
    position += worldToLocal * (velocity * dt)
    resolve against the world ground plane
gather every collider into world-space bodies and proxies
sweep and prune  ->  candidate pairs
for each pair: narrowphase, positional correction, normal impulse, friction
```

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
| Gravity | −9.81 on Y | per body, gated on `useGravity` |
| Ground plane | y = 0 | unconditional; no entity represents it |
| Penetration slop | 0.005 | overlap left uncorrected |
| Correction factor | 0.8 | fraction of the rest removed per step |
| Rest velocity | 0.1 | below this, bounce is zeroed |
| Static defaults | restitution 0.3, friction 0.4 | used when a side has no `RigidBodyComponent` |

Slop and the 0.8 factor exist together: correcting overlap to exactly zero makes
resting stacks vibrate, because floating-point error re-creates the overlap on
the next step and the correction fires again forever.

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

### 7d. Narrowphase, and what it approximates

Every collider is reduced at gather time to a world-space shape by
`worldBounds`: the centre through the matrix, the extent through the absolute
value of its basis. That is exact for translation and scale, and **conservative
for rotation — a rotated box is replaced by the axis-aligned box that encloses
it**. A sphere collapses its three world half extents to the largest, so a
non-uniformly scaled sphere becomes the sphere that contains it.

| Pair | Test | Exactness |
|---|---|---|
| sphere / sphere | distance between centres against the sum of radii | exact |
| box / sphere | closest point on the box, clamped per axis | exact against the box's world AABB; the sphere is never approximated as its own box, which would catch it on corners |
| box / box | world AABB overlap, separated along the axis of least overlap | **approximate** — both boxes collide as their bounding boxes |

Sphere-against-box is solved by calling the box-against-sphere routine with the
arguments swapped and negating the normal, so there is one implementation rather
than two that can disagree.

The cost of the box-box approximation is worth stating plainly, because it is
visible in play: a crate rotated 45° about Y collides as a box roughly 1.41 times
wider than it is, and a plank tilted on its end sweeps a volume far larger than
the plank. Bodies stop short of surfaces and come to rest on nothing you can see.
It over-reports rather than letting things pass through, which is the right
direction to be wrong in, but it is wrong.

Choosing the axis of least overlap is what makes a body landing on top of another
get pushed up rather than sideways. The failure mode is the other half of the
same rule: a body that has already sunk more than half way in separates along
whichever axis is now shallowest, which need not be the one it arrived on.

An entity carrying both a box and a sphere collider is treated as a box. The
sphere pass skips it explicitly, in the solver and in the queries alike, or it
would be gathered twice and collide with itself.

### 7e. Response

Each pair is resolved in three parts, in this order.

**Positional correction.** The overlap beyond the slop is shared out by inverse
mass, so the heavier body moves less and an immovable one does not move at all.
The cached centres of both bodies are then updated, because the pairs still to be
resolved this step read them — without that, a body wedged between two others is
pushed apart twice and travels twice as far as it should.

**The normal impulse.** Relative velocity is measured at the contact point, spin
included. Where the bodies are already separating the impulse is skipped, and so
is the friction that would have followed it — the correction above has already
been applied by then — because an impulse there sucks them back together. The
effective mass includes the angular term for both bodies, not merely the sum of
inverse masses; the linear term alone applies an impulse far too large for a
glancing hit near a corner. Restitution is dropped to zero when the approach
speed is below the rest threshold, otherwise a settling box jitters forever.

**Coulomb friction.** The tangent is recomputed from the relative velocity
*after* the normal impulse, spin included — friction acts on the surface speed,
which is zero for a ball rolling without slipping and is the entire reason a ball
rolls instead of sliding. The tangential impulse is clamped to the combined
friction times the magnitude of the normal impulse, so it can slow sliding but
never reverse it.

Both impulses are applied linearly and angularly, at the arm from each body's
centre. That arm is the whole of the difference between a body that slides and
one that turns: an impulse through the centre only pushes, the same impulse
applied at a distance also spins.

| Quantity | Combined as | Why |
|---|---|---|
| Restitution | the larger of the two, clamped to 0.99 | a superball dropped on concrete has to bounce; the smaller or the average would let any dead surface kill every ball that touched it |
| Friction | geometric mean, each side clamped to 4 | a zero on either side takes the result to zero, so ice stays slippery against anything; an average would let a rough floor grip a puck |

Inertia is taken from the shape a body **collides** as, not the shape it was
authored as: the tensor is built from the world-axis-aligned half extents with no
rotation applied to it. A box about its centre and a solid sphere both have no
products of inertia, so the local tensor stays three floats rather than a matrix.
Taking that local tensor and rotating it would describe neither the box nor its
bounding box, and would disagree with the very contacts it is used to resolve.
`freezeRotation`, `isKinematic`, a non-positive mass and static colliders all
produce a zero tensor, which falls out of the arithmetic as "infinitely hard to
turn" without a branch at every use.

### 7f. The world ground plane

There is an unconditional solid plane at y = 0. It is applied during integration,
per non-kinematic rigid body, before any collider is gathered.

It is measured in **world** space, through the bottom of the body's collider
bounds. Both halves of that sentence are bug fixes. Clamping the transform origin
to y = 0 buried every body by half its height and made it impossible to rest
anything below the plane; testing the *local* position put the floor wherever the
parent happened to be, so a body parented ten units up rested in mid-air and
never fell at all. The reflection fires only when the body is actually moving
into the plane — inverting unconditionally re-launched bodies that were already
rising — and is zeroed below the rest velocity.

The honest part: the plane exists whether or not the scene contains a floor,
nothing can fall below it, and there is no switch to turn it off. It applies the
body's own restitution and **no friction at all**, so a body sliding on the world
plane keeps its horizontal speed indefinitely unless linear damping takes it. A
rigid body with no collider is treated as a unit cube for this test, and at unit
scale comes to rest with its origin at y = 0.5.

### 7g. Triggers

A collider with `isTrigger` set is detected and deliberately not resolved: the
overlap is reported in the contact list and the body passes through, which is the
whole point of the flag. Either side being a trigger makes the contact one.

Two limits follow from where that is tested, and neither is visible from the
inspector checkbox:

- The broadphase skips pairs where both sides have zero inverse mass, so **a
  trigger volume only reports against a body with mass.** A trigger volume is
  usually a bare collider with no rigid body, and a character controller is
  usually kinematic — that pairing produces nothing at all.
- There is no dispatch. Contacts reach `EditorLayer` as a count and stop there.
  There is no `OnTriggerEnter`, no per-entity event and no enter/stay/exit
  distinction, so a script cannot currently learn that a trigger fired.

### 7h. World queries

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
| Rotation | exact; the ray is transformed into local space | lost; a box is its world bounding box |
| Triggers | no distinction | skipped unless asked for |
| For | editor picking | gameplay |

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

### 7i. Documented departures

Listed rather than hidden, in the same spirit as the rest of this document.

- **No continuous collision detection.** Positions are advanced by
  `velocity * dt` and only then tested for overlap. The step length is therefore
  a speed limit: anything travelling further in 1/60 s than the combined
  thickness of itself and the collider it hits passes clean through, and nothing
  is reported. A fast projectile wants a `Raycast` along its own motion, not a
  collider. The world ground plane is the one exception, because it is a clamp on
  position rather than a swept test.
- **No sleeping, and no persistent broadphase.** The body and proxy lists are
  rebuilt from the registry, the world bounds recomputed and the proxies
  re-sorted every step, whether or not anything moved. Cost is proportional to
  every collider in the scene, not to the part of it that is awake.
- **One solver pass, no warm starting.** Each pair is visited exactly once per
  step, in whatever order the sweep produced. A stack of boxes settles slowly and
  imprecisely, because the impulse the bottom box needs in order to carry the
  stack is discovered one contact at a time and thrown away at the end of the
  step.
- **One contact point per pair.** Box-box uses the centre of the overlapping
  region, standing in for what is really a face or an edge, which is why a box
  settling flat rocks slightly before it comes to rest. The point is at least on
  the correct side of the centre of mass, which is what decides which way a crate
  tips when it lands on the corner of something.
- **No collider offset.** `BoxColliderComponent` carries a size and
  `SphereColliderComponent` a radius; neither has a local position, so the shape
  is centred on the transform origin and so is the centre of mass. A mesh
  authored with its origin at its feet collides half buried.
- **Kinematic bodies are not simulated.** Integration skips them and their
  inverse mass is zero, so nothing pushes them. What they do to what they touch
  is narrower than it sounds: their velocity and spin are excluded from the
  contact solve, so a moving one shoves a body out of its way through the
  positional correction but imparts no impulse. A crate does not ride a moving
  platform, it is only displaced by it. Moving one is the script's job, and
  moving one quickly moves it through things.
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
| `Game` | `false` | The only thing that marks a game. The other two keys are read only once it passes. |
| `Title` | `Supersonic Game` | Window title. The editor's is `Supersonic Engine`. |
| `StartupScene` | `assets/scenes/MainScene.scene` | Scene loaded before the first frame. |

The `Game` key, not the file's existence, is the switch, so a stray manifest in a
build tree cannot turn the editor into a game. `Parse` returns immediately when
it is false, which is why the other two keys are never even looked at in the
editor case. A manifest that exists but does not parse logs an error and returns
the editor's manifest: there is nothing else to do without a scene to load, and
starting the editor silently would hide a packaging bug behind a window that
looks like it works.

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

One piece is built and not connected. `UIInput::Update` returns the number of
buttons clicked this frame so that a caller can ask whether the UI took the click
before letting it through to the world underneath. `UISystem::Render` discards
that return value, and nothing between it and the editor viewport's pick test
consults it. The guard the API was written for does not exist yet.

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
  no longer "only a transform" — it also carries input, world queries
  (`raycast`, `isGrounded`), animation control and the UI canvas — but every one
  of those is a fixed function-pointer block, so a script still cannot name a
  component the ABI did not anticipate.
- **Lights are placed from `TransformComponent`, not `WorldTransformComponent`.**
  `gatherLights` reads the local transform, so a light parented to something is
  positioned by its offset within that parent rather than by where the parent
  actually is. Everything else downstream — the scene pass, both shadow passes,
  picking and the gizmo — reads the world matrix, which is what §3b says makes
  parenting work everywhere at once. Lights are the exception, and a parented
  lamp lights the wrong place.

## Platform support

| Platform | State |
|---|---|
| Windows | Working. Primary development target. |
| Linux | Should build and run; not verified on hardware. |
| macOS | Instance creation is wired for MoltenVK (portability enumeration + bit). Not verified on hardware. |
| Android | **Not functional.** GLFW has no Android backend and the manifest expects a NativeActivity shared library CMake does not produce. See `platform/android/build_android.sh`. |
| iOS | **Not functional.** Same windowing problem, plus bundling and signing. |
