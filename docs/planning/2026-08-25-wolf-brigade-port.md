# Porting Wolf Brigade to Supersonic

> **Plan, 25 August 2026.** Written against `D:/The-Wolf-Brigade` at its current
> state and Supersonic at `47473fa`. Every claim below was read out of one of
> those two trees; where a number is a guess it says so.
>
> **Amended the same day** after a deeper read of the game finished. The
> conclusion did not move, but two numbers did: the peak drawable count was
> wrong by an order of magnitude (I counted entities, not the CanvasItems each
> one is made of), and endless mode turns out to be unbounded by data. Three
> requirements were missing entirely — a tint that can brighten past 1.0,
> immediate-mode shapes, and how much of the screen is world-space UI.

## The finding that should decide the schedule

**Wolf Brigade has no art yet.** Across all 44 scenes there is not one
`Sprite2D`, `AnimatedSprite2D`, `TextureRect`, `TextureButton`, `TileMap` or
`NinePatchRect`. The node census is 21 `Button`, 20 `Label`, 19 `ColorRect`,
16 `Node2D`, 6 `VBoxContainer`, 5 `HBoxContainer`, 1 `Camera2D`. The only images
in the repository are `broll/*.png` — milestone screenshots — and `icon.svg`.

A unit is four coloured rectangles and a label (`scenes/unit.tscn`):

```
Unit (Node2D)
  SelectionRing (ColorRect)
  Body          (ColorRect)
  HPBar/Bg      (ColorRect)
  HPBar/Fill    (ColorRect)
  Label
```

Two scripts draw immediately rather than through nodes — `entities/order_marker.gd`
and `ui/marquee.gd`. That is the entire visual language of the game.

This matters more than any single engine gap, because the expensive half of
porting a 2D game to a 3D engine is normally the sprite pipeline: atlases,
animation, filtering, draw-order, texture formats. **None of that exists to
port.** `CLAUDE.md` says the art and systems will be "heavily replaced" later,
which means the cheapest moment to move engines is now, before there is a
Godot-shaped art pipeline to bring across.

The corollary is uncomfortable and worth saying plainly: what you would be
porting today is a mechanically complete **skeleton**, not a finished game. If
the intent is to finish it in Godot and port afterwards, this plan roughly
doubles and grows a phase for sprites and animation that currently has no
content.

## What the port is actually made of

| | |
|---|---|
| GDScript | 76 files, ~8,150 lines (~4,500 under `scripts/`) |
| Godot coupling | ~14% of lines in the ten largest scripts touch nodes, signals, tweens or groups. The other ~86% is logic. |
| Peak drawables | **~350–400**, not the tens I first wrote. A unit is 5 CanvasItems and a building 7, so a busy campaign frame is roughly 40–60 units (200–300 quads) + ~8 buildings (56) + 12 resource nodes + ≤15 projectiles + ≤64 pooled damage labels. Endless mode is **unbounded by data** — enemies gain 8% HP per wave and accumulate, so the ceiling is the player's kill rate, not a number in a file. |
| Data | All gameplay numbers already live in `data/*.json` — the port reads the same files. |

Supersonic submits one draw and one push constant per entity with no instancing
and no sort (`src/core/RenderSystem.cpp`). **Phase 0 measured it** — see below.
A busy campaign frame is free; endless has a ceiling around 800 units.

---

## Phase 0 — Get the lane on screen (week 1)

Not a feature. A measuring stick. Until something of this game renders in
Supersonic, every estimate below is a guess.

Build one `EngineLayer` that spawns a row of camera-facing quads with flat
colours through the **existing perspective renderer**, driven by a hardcoded
lane. No orthographic camera, no unlit path, no port of the game logic — an
emissive-only material on a quad is near enough to a `ColorRect` to prove the
seam.

- **What it proves:** that the layer seam, the fixed step, the HUD and the input
  path can host this game at all, and gives a frame to measure everything
  against.
- **What breaks today:** nothing — this is additive.
- **Done when:** a headless `--frames N --screenshot` shows the lane, and the
  pixel count is stable across two runs with `--fixed-step`.

If Phase 0 goes badly, stop and re-plan. It is deliberately cheap enough to
throw away.

### Phase 0 result — DONE, 26 August 2026

`games/wolfbrigade/`, built with `-DSUPERSONIC_BUILD_WOLFBRIGADE=ON`. A separate
executable linking `SupersonicCore`, pushing one `EngineLayer`. **Nothing had
ever called `SupersonicApp::PushLayer` before this** — the seam was argued for in
`EngineLayer.hpp` and never used, which is not the same as working. It works: the
layer clears the registry, builds its own camera, light, ground and lane, and the
engine renders it.

The lane reads. Each unit is five drawables in `unit.tscn`'s own order —
SelectionRing, Body, HPBar/Bg, HPBar/Fill, Label — and at a glance you can tell
the units apart, see the health fills differ, and watch the two sides converge.

**The measurement, Release, 900 frames, `--fixed-step`, deltas against a
5-drawable baseline so startup cancels:**

| drawables | ms/frame | cost of the drawables |
|---|---|---|
| 5 | 4.00 | baseline |
| 400 | 4.16 | **+0.16 ms** |
| 2,000 | 7.75 | +3.75 ms |
| 4,000 | 12.37 | +8.37 ms |
| 8,000 | 22.15 | +18.14 ms |

About **0.002 ms per drawable**, near-linear with a slight upward trend. Read off
that table:

- **A busy campaign frame — the plan's 350–400 — costs 0.16 ms.** The worry was
  unfounded at campaign scale. One draw and one push constant per entity is
  simply not the problem here that it would be for HUSK.
- **The endless ceiling is roughly 3,000–4,000 drawables, or 600–800 units**,
  where the lane alone eats half a 60 Hz budget. Past that, instancing or a
  culling pass stops being optional. Since endless has no data-driven cap, that
  is a real design limit rather than a theoretical one — and it is now a number
  rather than a worry.

These figures include the editor UI, which a shipped game would not run, so they
are pessimistic.

**Three things the spike exposed that reading could not:**

1. **The editor grid draws over the game.** Expected — it is disabled by the
   `isGame` manifest — but it means every screenshot until packaging has a grid
   through it.
2. **Depth ordering only works because the quads are not coplanar.** The spike
   nudges each layer 0.01 toward the camera. That is the shortcut item 1.4
   exists to replace, and the spike makes it visible rather than pretending.
3. **The perspective curve is obvious.** A 60-unit lane bends away at the edges
   in a way a `Camera2D` never would, which is item 1.1 earning its place at the
   top of Phase 1.

---

## Phase 1 — 2D presentation (1–2 weeks)

The engine renders one way: perspective, PBR. The game needs neither.

| Item | Kind | Size |
|---|---|---|
| Orthographic camera | engine feature | ~1 day |
| Unlit / flat-colour material path | engine feature | ~1–2 days |
| Quad primitive with a pivot | engine feature | hours |
| Explicit draw order | engine **assumption change** | ~2–3 days |

The game is a **strictly 1D lane**: every unit sits at the same ground row and
only x ever changes (`unit.gd:313`, `:280-281`). The camera has no zoom at all —
`Camera2D.zoom` is read as a divisor and never assigned, so it is (1,1) for the
whole game. It pans along x within a 6000-wide world showing 1920 at a time, and
shakes through `offset` so the shake never fights the pan clamp. That makes the
camera work smaller than a general 2D camera would be.

**Orthographic.** `CameraComponent::getProjectionMatrix()` hardcodes
`glm::perspective` with no mode field; `grep -i ortho` over `src/` finds only
the shadow frustum. Add a mode enum and an `orthoSize`, branch to `glm::ortho`
keeping the existing `proj[1][1] *= -1.0f` Vulkan Y-flip, serialise the two new
fields. The consumers to check are picking (`Raycast::ScreenPointToRay` inverts
the projection but assumes a perspective ray) and the shadow cascades.

**Unlit.** Every surface goes through the PBR path. A `ColorRect` equivalent
wants its authored colour out the other side, untouched by lights, ambient or
IBL. Cheapest honest form: a material flag that makes the fragment shader return
albedo directly, before the lighting accumulation.

**Draw order is the assumption change.** Godot gives every `CanvasItem` a
`z_index` and draws children in explicit order — `scenes/unit.tscn` documents
its own child order as the draw order. Supersonic iterates the registry
*unsorted* and relies on the depth buffer. For flat quads at the same depth that
is not an ordering at all. This needs a sort key on the renderable and a sorted
opaque pass, which is a change to how the renderer decides what to draw when.

### Three things the first draft of this plan missed

**A tint that can brighten.** The hit flash sets `modulate = Color(2.4, 2.4,
2.4, a)` and tweens it back to 1.0 (`juice.gd:18`) — it relies on a per-object
multiplier **above 1.0** pushing the colour toward white. So the flat-colour path
needs a tint multiplier and an alpha, not a flat colour. Cheap if designed in
now, annoying to retrofit. The scene target is already floating point, so values
above 1 survive to the tone mapper.

**Immediate-mode shapes — and this entry was wrong.** Two overlays are drawn
rather than composed: `order_marker.gd:30-33` is
`draw_arc(centre, r, 0, TAU, 40, colour, 3.0, true)` plus `draw_circle`, and
`marquee.gd:19-23` is a filled `draw_rect` plus a 2px outline.

I wrote "Supersonic has no line or arc primitive". **That is false for screen
space.** `UISystem` already draws through ImGui's draw list — `AddRectFilled`,
`AddRect` (an outlined rect), `AddLine` for the text caret — and ImGui's
`AddCircle(centre, radius, colour, 40, thickness)` is Godot's `draw_arc` call
verbatim. So the **marquee**, which is screen space because it lives on a Godot
`CanvasLayer`, needs no engine work at all.

What genuinely does not exist is a **world-space** line: the 3D pipeline
hardcodes triangle topology, and nothing draws a line in the world. That is the
order marker, and it alone.

**Tweens are load-bearing for the look.** Death, depletion, the attack squash and
the hit flash are all SceneTree tweens on `modulate` and `scale`, several with
`set_parallel` and `tween_callback(queue_free)`. The port brings its own
interpolation (Phase 3), but the *renderable* has to expose per-object tint and
scale for it to drive.

**Done when:** a unit renders as ring-behind-body-behind-bar-behind-label in
that order, at a fixed screen size independent of window size; a hit flash
brightens toward white and returns; and a scene with no orthographic camera
renders byte-identically to before.

### Phase 1 result — DONE, 26 August 2026

| item | | how it was proved |
|---|---|---|
| 1.1 orthographic camera | ✅ | Rays from opposite screen edges point identically and start apart; **perspective rays still diverge** as the control; the perspective matrix does not move by one bit. Picking was the half that was actually wrong — it unprojects a direction and hangs it off the eye, which is exactly inverted for a projection with no eye point. |
| 1.2 unlit path | ✅ | Turn the sun off: **1 of 1,597** interior unit pixels change. Disable the branch: **1,450 of 1,450**. Edges are counted separately (243 of 877) because 4× MSAA blends them against a background that *is* lit. |
| 1.3 quad primitive | ✅ | White vertices asserted, **with the cube's deliberate rainbow as the control**. |
| 1.4 draw order | ✅ | The spike's quads were staggered 0.01 in z so depth would order them. They are now coplanar with the key alone deciding, and the frame is **0 of 293,695 pixels different** from the staggered version. Disabling the sort changes 5,334. |
| 1.5 tint above 1.0 | ✅ | Free: `albedoColor` is unclamped on the unlit path, so a value above one blooms. |
| 1.7 per-object scale | ✅ | **Already existed** — per-entity, in the model matrix, and exposed to scripts. Deleted from the list rather than built. |
| 1.6 immediate shapes | ✅ | `UIShapeComponent` — ring, disc, line — drawn through the same ImGui draw list as the HUD, at a **projected world position**. What was missing was never the shape: ImGui's `AddCircle` takes Godot's `draw_arc` arguments verbatim, segment count included. It was the world position. Proved in a running frame: a marker's vertices average to within 2 px of where `ProjectToScreen` says the point is, **with the same component in screen space as the control** — it moves to its anchor, hundreds of pixels away. Behind the camera it draws nothing; dropping that cull puts a mirrored marker in front of the viewer, on a unit that is not there. |
| 1.8 lane camera | — | Port-side, and the spike already pans. |

**Three things learned that no amount of reading would have given:**

1. **The scene's camera is only used when NOT editing.** In edit mode the editor
   camera drives the viewport, so the spike's orthographic settings were ignored
   until it was run with `--scene`, which enters Play. A game needs that or an
   `isGame` manifest.
2. **A flattened cube is not a flat colour.** `GenerateCube` gives every corner a
   different colour on purpose, and the shader multiplies albedo by it, so the
   first "flat" quads came out as rainbow gradients. That is the entire reason
   item 1.3 exists as a separate primitive.
3. **Building the game target does not recompile shaders.** `frag.spv` went
   seventeen minutes stale and three mutation runs silently tested the
   unmutated shader, nearly producing the conclusion that the test did not
   discriminate. It discriminates completely. Build `SupersonicEngine` after
   touching a shader.

---

## Phase 2 — The UI (2–3 weeks)

The biggest single surface, and the one most likely to be under-estimated: 21
buttons, 20 labels, 19 rectangles, and container-driven layout.

**What already exists.** `UICanvas` has an anchor enum, `Place(anchor, offset,
size, screen)`, and — usefully — `kReferenceHeight = 1080.0f` with
`ScaleFor(screenSize)`, so the HUD is already authored in resolution-independent
units against a 1080-high reference. That is most of Godot's
`stretch/mode=canvas_items` contract, already built.

**What does not exist:**

- **Containers.** `VBoxContainer`, `HBoxContainer`, `CenterContainer`,
  `ScrollContainer` — 16 uses across the scenes. Supersonic places elements at
  an anchor plus an offset; nothing stacks, flows, or measures children.
- **World-space UI, and it is most of the entity.** Of a unit's five drawables,
  three are world-space UI: the HP bar (two quads) and a Label. A building has a
  progress bar on top of that. Pooled floating damage numbers are world-space
  Labels too, soft-capped at 64 (`fx.json:8`). There is no "label at a world
  position" in the engine at all, and this is not a garnish — it is the majority
  of what is on screen.
- **`aspect=expand`.** The HUD scales by height; the game view's aspect handling
  has no equivalent.

Two routes, and the choice should be made after Phase 0:

1. **Grow `UICanvas`** a vertical/horizontal stack and a measure pass. Keeps one
   UI system. Days per container type.
2. **Let the port drive ImGui directly** from its layer. ImGui already does
   layout, and the HUD renderer is ImGui underneath. Faster, but the game's UI
   then lives outside the engine's UI system, and `UICanvas` becomes decorative.

Route 2 is faster and worse. Recommend 1 for the containers the game actually
uses — stack and centre — and nothing more.

**Done when:** the pause menu and the HUD render from the port's own layer at
two different window sizes with the same layout.

---

### Phase 2 progress — 26 August 2026

Route 1 was taken: `UICanvas` grew the containers the game actually uses, and
nothing more.

| item | | how it stands |
|---|---|---|
| 2.1 stack containers | ✅ | `UICanvas::LayoutStack` plus `UIStackComponent`. One bool for the axis, because `HBoxContainer` and `VBoxContainer` differ by one axis. Spacing goes *between*, not after: three items have two gaps, and a stack that trails one is half a gap off centre and looks like nothing is wrong. |
| 2.2 centring | ✅ | The stack's own anchor, which is `CenterContainer`: the **block** is centred, not each child on the same point. The test is written against that mistake — anchoring each child independently puts them all on top of one another, which reads as the layout having done nothing. |
| 2.3 one layout, two readers | ✅ | `UICanvas::StackedRects`, computed once per frame and handed to both the input pass and the draw pass. Each used to call `Place()` for itself, which is safe only while both compute the same answer — and the moment a container decides a position, "compute it twice" becomes "compute it twice from different information". |
| 2.4 world-space labels | ✅ | `UITextComponent::worldSpace` + `UICanvas::ProjectToScreen`, projected through the camera that actually drew the frame rather than the editor camera. Verified **in a running frame**, not only as a projection function: the glyph vertices straddle the projected point, measured as two edge gaps rather than a mean because glyph shapes move a mean by several pixels and hanging the label off the point instead of centring it moves it by half the string — which are the same size. |
| 2.5 world-space bars | ~ | **Decided, not demonstrated.** In an orthographic 2D view an HP bar is a quad rather than a UI element: Phase 1's unlit quad, parented to the unit, scaled by HP, ordered by `sortKey`. Every piece of that exists and is tested; nothing has yet driven a bar's width from a health value in a running frame. Building a second world-space path through the UI system for rectangles would have been a parallel renderer, so this stays the plan — but it is a plan, and the row above it is a measurement. |
| 2.7 aspect handling | ✅ | **Already satisfied.** `orthoHeight` with an aspect-derived width *is* `aspect=expand`, and `kReferenceHeight = 1080.0f` with `ScaleFor()` already gave the HUD resolution-independent authored units. Deleted from the list rather than built. |
| 2.8 layer/overlay stacking | ✅ | See below — it was two features, not one. |
| 2.6 pooled damage numbers | — | Port-side, and now expressible: a world-space label, pooled by the game, soft-capped at 64. |
| 2.9 scroll container | — | One use in the whole game, and it may not survive the redesign the port implies. Left until the port asks for it. |

**2.8 was two features.** The draw half is what the item said: the four draw
loops run once per layer, low to high, so a pause menu covers the HUD it is
drawn over. The input half was not on the list and had to be built anyway —
every button answered "am I under the pointer" for itself, which is right only
while no two overlap. Two that did both highlighted and both fired on one
click. Overlap was an authoring mistake before an overlay could be drawn over a
HUD; afterwards it is the ordinary case, and an overlay that covers the health
bar while the button under it keeps taking clicks is not covering anything.

Panels take part in that reckoning, and that is what makes a menu modal with no
flag saying so. A panel is drawn under every button on its own layer, so it can
only ever block one on a *lower* layer — a backdrop swallowing clicks meant for
the game behind it, and nothing else. A button on its own backdrop still works,
which is how every HUD built so far is put together.

`UIStackComponent` and `UIOrderComponent` were also going through the codec
nowhere, so a container authored in the editor came back scattered and a
ranking came back gone. Both now round-trip and both have inspector sections.
That was not in the estimate and is the kind of thing that silently expands the
items after it: a component is not finished when it is read by a system, it is
finished when it survives a save.

**The HUD cannot be screenshotted, and this changes how UI is verified.**
`--screenshot` reads back `offscreen.GetPresentedImage()` — the colour target
the 3D scene rendered into. The HUD goes into an ImGui draw list that is
composited into the swapchain *after* that, so it is never in the PNG. The
whole screenshot-and-diff workflow the renderer work leans on is structurally
unavailable here. What replaced it: a headless `ImGuiContext` with
`ImGuiBackendFlags_RendererHasTextures` set — ImGui 1.92 builds glyphs on
demand, so that flag is the whole of what a backend has to provide — one
`NewFrame`/`Render`, and then read `ImDrawData`'s vertex buffer directly. Which
colour appears first is draw order; how many vertices each contributed is
whether anything was drawn twice. Drawing layers high-to-low fails it; leaving
the layer guard off one of the four loops reports 8 vertices where 4 are
expected.

**And entt's iteration order is not creation order.** This suite was first
written expecting the two panels to come out in the order they were created and
got the reverse. That is not a bug to work around — it is the reason
`UIOrderComponent` exists rather than the order being read off the scene — but
it is worth knowing before writing an assertion that depends on it.

**The order marker closed Phase 1.** Item 1.6 was the last one open, and it
landed here rather than in Phase 1 because it is the same machinery: a shape
that follows a point in the world is a label that follows a point in the world
with `AddCircle` instead of `AddText`. Sizes are in authored units at the
reference height, like `fontSize`, so a marker keeps its share of the screen
rather than growing as the camera closes in — for a game whose `Camera2D.zoom`
is read as a divisor and never assigned, those are the same thing.

**Still open for Phase 2:** the scroll container (2.9), and the port-side
pooling (2.6). The **done when** condition — the pause menu and the HUD
rendering from the port's own layer at two window sizes with the same layout —
is not met yet, because the port's own layer does not draw a pause menu yet.
Everything it needs from the engine is there.

---

## Phase 3 — Port the game (3–5 weeks)

Mechanical translation of ~8,150 lines, of which ~86% is logic that has no
Godot in it.

The ~14% that does needs decisions made once and applied everywhere:

| Godot thing | Becomes |
|---|---|
| `signal` / `EventBus` autoload | a plain event struct in the registry context — the engine already publishes `ContactTracker*` and `SceneManager*` this way |
| Node lifecycle, `queue_free` | entt create/destroy; the engine's deferred-destroy already exists |
| `create_tween` | the port's own interpolation — 2 uses in `unit.gd` |
| `get_node` / `$Node` paths | entity handles held by the owning system |
| Autoloads (`GameState`, `DataLoader`) | layer members |

`data/*.json` is read as-is; the engine has a JSON parser (`core/Json.hpp`).

**Done when:** the simulation runs headless in a test executable, waves spawn on
schedule, and a save round-trips — all with no renderer attached. This is the
phase with the best test story, and it should be taken.

---

## Phase 4 — Touch and multi-pointer input (1–2 weeks engine, ~90 lines port)

`scripts/input/input_controller.gd` is a four-state gesture machine —
`NONE / UNDECIDED / PAN / MARQUEE` — keyed on **per-finger index**, with a 12px
movement threshold and a 300ms hold test, arbitrating hold-then-drag (marquee)
against quick-drag (pan).

The machine itself is port work: ~90 lines of plain state, no engine change.

What the engine cannot express is its *input*. `RawInputState`
(`src/core/Input.hpp`) is `keys[349]`, `mouseButtons[8]`, **one**
`mousePosition`, `scroll`, and one pad. There is no finger id, no contact count,
no per-contact delta. `UICanvas::UIPointer` is a single device-agnostic pointer
fed from ImGui's mouse.

So: widen `RawInputState` with a small fixed-capacity array of contacts
(id, position, phase), and give `UIPointer` a source. On desktop the mouse
synthesises contact 0, which is also how the game runs before Android exists.

**Done when:** the marquee and the pan are both reachable with a mouse, and the
gesture machine's thresholds are unit-tested against a scripted contact stream
with no window.

### Phase 4 progress — 26 August 2026

**The engine half is done. The phase is not**, and the difference matters: the
gesture machine is port work against a game that is not in this repository, so
"the marquee and the pan are both reachable with a mouse" cannot be shown here.
What can be, and is:

`RawInputState` carries up to eight contacts — an id and a position each, in the
same style as `textCharacters`/`textCharacterCount`. A count of zero is the
desktop case and is byte-for-byte the engine that existed before the field did,
which an existing case asserts directly.

**Phase is derived, not reported.** A snapshot says what is touching the screen
right now, exactly as `keys` says what is held right now; "began" and "ended"
are not in a snapshot, they are what a comparison between two snapshots
produces. `Input::Update` does that comparison, which is what keeps the part
with the logic in it testable without a device. The case that forces the design:
a lifted finger is *not in the current frame at all*, so it has to be
synthesised, at its last position, for exactly one frame — without which a
gesture that ends on release never ends, and the machine sits in PAN forever
with nothing moving.

**Not stationary, just `Moved` with a zero delta.** Whether a finger has moved
is a threshold question, and Wolf Brigade's threshold is 12 px — which is its
number, not the engine's. A 2 px hold test disagrees about the same frame and
neither answer belongs here.

**The mouse is contact 0 while its left button is held**, through
`Input::SynthesiseMouseContact`. That lives in `Input` rather than in the
polling layer for one reason: otherwise a gesture machine written against
contacts is dead code until Android exists, which is the same as untested, and
the day it stops being dead code is the worst day to find out it was wrong.
Held, not hovering — making hovering count fails three cases, because a contact
that exists whenever the pointer is over the window begins every gesture the
moment the mouse enters it.

Five mutations, all caught: never reporting a lift (4 failures), giving a new
finger a delta from somewhere (3), letting an empty slot become a phantom finger
at the origin (1), trusting a count larger than the array (1), and counting a
hover as a touch (3).

**Contacts obey the host's veto, and had to be made to.** `SuppressCursorCapture`
exists so the mouse stops being the game's while the editor owns it - between
plays, on an unfocused viewport, after the player presses Escape. Contacts are a
second thing derived from the same mouse and were the one path around it: a drag
across an inspector field *while playing* would have arrived as a marquee the
player never drew. Layer updates are gated on play mode, so an edit-mode gizmo
drag reaches nothing today - but that is a fact about `SupersonicApp`, not about
the input layer, and it is the wrong thing to depend on. Removing the veto fails
two cases.

**`UIPointer` did not get a `source`.** The plan asked for one. Nothing in
`UIInput` or `UISystem` would branch on it today, so it would be a field nobody
reads that later reads as done — the same failure the 2.5 row above was
corrected for. It goes in when something asks the question.

**Still open:** the gesture machine itself (~90 lines, port-side), and the
script ABI, which exposes `mouseDelta` and not contacts. The port is a C++
layer rather than scripts, so nothing needs it yet.

---

## Phase 5 — Android (2–4 months, and it is not the same phase as the rest)

Everything above lands a desktop game. This lands *the* game, and it is the
majority of the remaining cost.

The build does not currently get past CMake configure — see the Platforms
section of the README, corrected on 25 August. The work:

- **Build.** Guard GLFW, `imgui_impl_glfw` and the `glfw` PUBLIC link behind
  `NOT ANDROID`; otherwise `GLFW_BUILD_X11` defaults ON under the NDK (CMake
  sets `UNIX=1`) and `find_package(X11 REQUIRED)` kills the configure. Emit a
  `SHARED` library rather than an executable — `AndroidManifest.xml` expects a
  NativeActivity to `dlopen` `libSupersonicEngine.so`. Add gradle, APK assembly
  and signing, which do not exist.
- **Entry point.** There is no `android_main` or `ANativeActivity_onCreate`
  anywhere. `src/platform/AndroidNativeApp.cpp` has zero callers and is filtered
  *out* of every non-Android build.
- **Surface.** `vkCreateAndroidSurfaceKHR` instead of `glfwCreateWindowSurface`,
  plus suspend/resume and surface-loss swapchain recreation — which desktop
  never forced anyone to get right.
- **Audio.** Android is explicitly routed to the documented no-op
  (`CMakeLists.txt:308`). A port that booted would be silent. Needs OpenSL ES or
  AAudio.
- **Assets.** Every loader reads through `std::ifstream`, `std::filesystem` and
  `stbi_load(path)`. On Android assets live inside the APK behind
  `AAssetManager`. This is a change to every loader, not one.
- **Textures.** Everything uploads as RGBA8 (`TextureRegistry.cpp`); there is no
  BC/ASTC/ETC2 path. Irrelevant while the game is coloured rectangles, and a
  hard requirement the moment art arrives.
- **Lifecycle.** The game autosaves off Android lifecycle notifications
  (`scripts/main.gd`), which have no counterpart in the engine.

---

## Order, and why

1. **Phase 0** — because every number in this document is a guess until
   something is on screen.
2. **Phase 1 and 3 in parallel** — the simulation port needs no renderer, and
   the renderer work needs no simulation.
3. **Phase 2** — after 1, because the UI needs the flat-colour path.
4. **Phase 4** — desktop-with-a-mouse first. The gesture machine is testable
   without a touchscreen.
5. **Phase 5** — last, and only when the game is provably running on desktop.

The trap this order is built to avoid is spending three months on Android before
anyone can see whether the lane RTS reads correctly at all.

## Honest total

Phases 0–4 land Wolf Brigade on desktop with a mouse: roughly **two to three
months**. Phase 5 lands it on Android: **two to four months more**, and it is
the phase with the least engine work already done and the most unknowns.

Porting it to desktop is not shipping it. Wolf Brigade is an Android game with
touch as its primary input, and a desktop build is a staging post, not a
product.

## What would change this plan

- **If the art lands first**, add a sprite pipeline phase — atlases, animation,
  filtering, compressed formats — and expect Phase 1 to double.
- **If the target becomes desktop**, delete Phase 5 and most of Phase 4, and the
  whole thing is a two-month job.
- **If the game is to be finished in Godot first**, do not start any of this
  yet. The cheapest moment to move is while the visual language is still
  nineteen coloured rectangles.
