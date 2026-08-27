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

### The test story is better than this plan assumed — 26 August 2026

The game is at **`D:/The-Wolf-Brigade`**, and it ships **22 headless
verification harnesses** (`tools/verify_*.gd`) that already assert its
behaviour and print their numbers. All 22 pass today. Godot 4.7.1 is
Steam-installed at
`D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe`;
`tools/verify_all.bat` runs the lot in about two minutes.

So the port is not verified by reading the GDScript and believing the result.
Each harness is a **vertical slice with pass/fail already encoded**, and the
method is the one the rest of this engine is built on — differential comparison
against code already trusted — with a real oracle rather than a reconstructed
one:

1. Port the GDScript one harness exercises.
2. Port the harness into a C++ test.
3. Require the C++ test to reproduce **the numbers the Godot harness printed**.

`verify_waves` already hands over four of them: 30 raiders, 1 brute, 31 enemies
across 5 waves. Those become constants in the C++ test, with the command to
re-derive them written beside them.

**Three rules this imposes.**

- **The game repo is read-only.** It is the oracle; an edit there invalidates
  every comparison the port is verified by.
- **Nothing shells out to Godot from `ctest`.** That binary is Steam-managed on
  another volume and will not exist on a build machine. A harness is run once,
  by hand, and its numbers are recorded.
- **The stepping is part of the specification, not an implementation detail.**
  `verify_waves` drives `wd._process(2.0)` exactly 300 times. A port that
  accumulates instead of stepping, or steps at a different dt, drifts — and the
  debugging goes into the port when the disagreement is in the harness.

### Slices ported so far

| slice | oracle | how it was proved |
|---|---|---|
| gesture machine | `verify_touch` A | Every case that harness asserts, at its own coordinates. Five mutations caught, including both comparison operators — the GDScript uses `>` on distance and `>=` on time. |
| data + validator | `verify_data` | The ten key counts the original prints (`world -> 11 keys`, `units -> 5`, …), 0 cross-reference issues, and **nine deliberate typos** — one per check — each caught. Six mutations caught. |
| wave director | `verify_waves` | **The first slice the oracle actually constrains.** 30 raiders, 1 brute, 31 across five waves, reaching wave 5 — the output of 300 steps through the schedule, the difficulty scaling and the spawn interval, and not derivable by reading. Matched on the first run. Plus victory only on a cleared field, defeat on the Town Hall, and the empty-final-wave soft-lock the original wrote its own case against. Eight mutations, all caught. |
| buildings | `verify_buildings` | **two bolts pooled** from a tower in three seconds, plus construction, training queues, footprints and the deposit-point rule. Thirteen mutations; **four survived** the shipped data and needed authored fixtures — one of which had to be SEARCHED for. |
| save + restore | `verify_snapshot`, `verify_continue` | Capture → JSON → restore → re-capture, plus independent ground truth off the LIVE restored entities. **Fifteen mutations, three survived** and each pointed at something real. |
| upgrades + meta | `verify_upgrades`, `verify_meta` | More computed numbers than any other pair: soldier damage 8→12, Town Hall 1000→1500, three Armory levels at exactly 40+80+120, a wave-3 loss worth 30 renown, a stacked gather rate of 1.8. **Eight mutations, all caught on the first pass** — the first slice where none survived. |
| worker builds | `verify_buildings` 2, 6 | The unit's BUILDING state, which closes the seam between the two halves: a worker sent to a site finishes it, and **any idle worker resumes an abandoned one**. Four mutations, all caught. |
| combat | `verify_combat` | **soldier 36/60, Town Hall 934/1000** — and the second one is the first number in the whole port that DISAGREED. Plus the lane index, orders beating instincts, and the arrow pool. Nine mutations, all caught after two blind spots were closed. |
| worker economy | `verify_economy` 2, 2b + `verify_units` FSM | **tree 79/100, banked +20, carried 1** — 140 steps of 0.2s between a tree at 1300 and a deposit at 1000, matched exactly, for both resources. Plus the FSM (spawns idle at 30 hp, a move order strips the y, arrival releases the order) and the flee reflex. Twelve mutations; **four survived the first pass** and needed cases the original's own harness cannot see. |
| economy state | `verify_economy` 1, 2c, 3 | Extraction clamps (200 → 170 → 0), spending is atomic, and `resources_changed` carries the new TOTAL. Difficulty scales the opening balance to the numbers the presets imply — 450 on Easy, 240 on Hard — and owned meta lands flat on top of it, not through it. Six mutations, all caught. Sections 2 and 2b, the worker gather/deliver loop, wait for `unit.gd`. |
| build placement | `verify_buildings` placement, `verify_input` 3 | **The last slice with an oracle.** A Town Hall at 2000 with a footprint of [1940, 2060]: 2000 refused, 4000 allowed, edge-to-edge buildable. Plus the four assertions only `verify_input` makes - cancel spends nothing, and the Back gate's truth table including the ghost that outlives the match. Eighteen mutations. Gives `GestureMachine::SetPlacementMode` its first real caller. |
| tone synthesis | `verify_audio` + a verbatim probe | **The strongest oracle in the whole set, because the numbers are computation output.** Eleven synthesised sounds reproduced sample for sample - counts, first, second and middle samples, peaks, and a sum and sum-of-squares over every sample of all eleven buffers. Matched on the first run. Sixteen mutations, fifteen caught; the sixteenth is proved equivalent rather than excused. |
| match boot (`main.gd`) | `verify_snapshot`, `verify_meta` award-wiring, `verify_save` wave-recording | **The whole-match numbers, which no earlier slice could reach**: 270 wood, 963 hit points, six units, three gathering, one enemy alive — off a real boot rather than a board built by hand. Twenty-six mutations, twenty-five caught; the one survivor was predicted and is why the sink stays minimal. Found three real defects on the way: a restored endless run whose director was not endless, a re-booted match carrying the last run's wave clock, and a fresh run that never saw the Armory levels it was bought with. |

**A fixture that could not fail, caught by its own mutation.** `ScaleWaveCount`
guards with `max(1, ...)` so Easy never rounds a wave's single brute away — and
at shipped values that guard is unreachable, because 0.7 of one rounds to one
anyway. The first version of the case asserted it at Easy, and removing the
guard changed nothing. It now runs against an authored preset at 0.2, where the
floor is the only thing standing between a scripted wave and losing its
centrepiece.

**Two things the data slice cost that reading would not have found.**

`Json::Parser` holds a **reference** to the text it is given, so
`Parser(buffer.str())` parses a string that has already been destroyed. It does
not crash — it fails on the first character and reports ten perfectly good files
as malformed JSON, a long way from the constructor. The rvalue constructor is
now `= delete`d, so the compiler says it instead. Nothing in the engine was
relying on it.

And **absence is not a type error**. The GDScript reaches nearly every shape
check through `dict.get(key, {})`, where a missing key yields the empty default
and never reaches the check; reading an absent key here gives a null, and
treating that as "should be an object" reports every building without a
`researches` list. Making that mistake deliberately fails five cases.

**Counting issues by substring, not in total.** The first version of the
discrimination cases counted totals, and replacing `buildings.json` to break one
cost also removed two buildings that an upgrade elsewhere referenced. That is
collateral from the fixture rather than the check under test, and a total makes
the file brittle in a way that reads as the validator misbehaving.

### The save slice, and why it was designed before it was written

This is the one slice with novel design in it: the original resolves node
references through Godot instance ids and a capture map, and the port has
`Damageable*` and world indices instead. Three independent designs were
produced, scored, and then attacked; the winner was the one whose ownership
assumption is already true of the tree — **nothing in `sim/` owns a sim entity**.
So the save is an OBSERVER: capture walks whatever the caller enumerates,
restore builds into whatever the caller provides. Deciding where entities live
belongs to the slice that ports `main.gd`, which has information this one does
not.

**One monotonic id space across units, buildings and resource nodes**, straight
from `snapshot.gd`, which says why: a unit's attack target may be a Unit OR a
Building, and one space lets that reference come back with no type tag in the
file. Two spaces would need a tag, and a tag is a thing that can be wrong.

Three things the design pass caught that writing-then-testing would not have.

**Round-trip equivalence is blind to whatever both sides omit.** The original's
own harness says so in a comment — "that's how the building-cooldown gap first
hid" — and answers it with independent ground truth. This suite does the same
AND makes the digest far less lossy: Godot renumbers its ids on rebuild so its
digest throws away the entire stats block, every reference, `move_target`,
`carry` and `attack_cd`. Only `sid` and the refs actually renumber here, so
everything else is compared exactly and the refs are compared by TRANSLATING
each id to its referent's own record — which catches "relinked to the wrong
entity of the same type", something no amount of dropping could.

**A relink test that steps even once passes with relink deleted.** The thinking
tick re-acquires within 0.125s and usually picks the same thing. The board has
to discriminate: two nodes, the worker on the FARTHER one, asserted at zero
steps — dropped relink gives null, re-acquisition gives the nearer one. The
precondition is asserted inside the test so a later edit cannot quietly make the
board indiscriminate.

**Six significant digits is the default, and every oracle tolerance passes under
it.** The original compares cooldowns within 0.001 and elapsed within 0.5; both
survive a truncating writer. This port's whole numeric argument is that a
cooldown decremented by 0.1 lands on a different side of zero in float than in
double — a save that rounds it reintroduces exactly that. The writer uses
`%.17g` and the test asserts the text is byte-identical across a round trip.

**And an unknown id is skipped, never fabricated.** `UnitStats::FromJson` on a
missing row returns every default — a player-faction worker — so a saved raider
whose row was re-tuned away would come back on the player's side, counted by no
enemy tally, handing them a victory. A building is worse: a missing row is
`maxHp 1`, and the hp clamp then puts a saved Town Hall back at one hit point.
The schema version does not cover this; it guards the shape of the FILE, and
nobody bumps it when only `units.json` changes.

**Three mutations survived the first pass, and all three were the test's fault
rather than the code's.** A sink that does more than the contract requires hides
what the contract promises: the Board called `SetTrainTimes` itself, so removing
it from `Restore` changed nothing, and it ignored the `complete` flag because
`FromSave` sets the state a moment later. Both needed a deliberately minimal
sink that does only what a sink must. The third was the profile levels, where
the guarantee turned out to be structural — the document does not carry them at
all — and the test now asserts that directly, because the day somebody adds
`meta_levels` to `ToSave` "for completeness" is the day a stale save can undo a
Reset Progress.

**What this slice cannot do**, stated so it is not claimed later:

- **`verify_autosave` is out of reach entirely.** Six of its seven assertions
  are window and OS plumbing — quit interception, Android Back, an
  application-paused notification. The seventh is pure simulation and has no
  call site here, which means the original's anti-farm property (a finished run
  clears its Continue, and a later autosave must not re-write it) is
  **unenforced in the port**. `ClearRun` exists and nothing calls it.
- **Five of `verify_continue`'s assertions** read a menu button's visibility.
  What survives is the predicate the decision rests on, `Snapshot::IsValid`.
- **The exact numbers in `verify_snapshot`** — 270 wood, 963 hit points, six
  units — come from booting the whole match through `main.gd`. The port has no
  match boot, so this suite builds its own board and asserts the same
  PROPERTIES against numbers it sets itself.
- **The run file is not interchangeable with Godot's.** Colours are written
  differently, a unit's position is `global_position` there and plain here, and
  ids are minted in enumeration order rather than scene-tree child order.
  `kVersion = 1` is the PORT's schema 1.
- **The deposit and flee references are deliberately not saved.** Both are
  cached indices into a world-owned list — an idea this port introduced with no
  GDScript analogue — and saving them would force an id space onto every `World`
  implementation. A restored worker stands still for one thinking tick and then
  re-acquires the nearest deposit. That cost is pinned as a passing assertion
  rather than left to be discovered as a position mismatch.

### C++ has no reflection, and that turned out to be an improvement

GDScript applies an upgrade with `stats.set(field, stats.get(field) + delta)` and
`push_warning`s at RUN TIME when the field does not exist. C++ cannot do the
first, so `UnitStats::ApplyDelta` and `BuildingStats::ApplyDelta` write the
mapping out by hand: an `if` chain from field name to member, returning `false`
for anything nobody has.

That is worse in one way — adding a field means remembering to add it there —
and better in two. An effect naming a field nobody has is *reported* rather than
silently doing nothing. And the set of upgradable fields becomes something a
test can enumerate, so `test_wb_progression` checks **every effect field in
upgrades.json and meta.json against the entity it targets, at build time**. The
original can only find the same typo by someone reading a warning in a log.

Worth noting where the boundary is: `DataValidator` already cross-references
effect *targets* (is "soldier" a unit?) but has never checked field *names* (is
"damge" a stat?). This closes that, from the other side.

**What the oracle proves, and what it does not.** Four slices running, surviving
mutations have needed authored fixtures because the shipped data cannot reach
the branch. That is not a flaw in the method — it is a fact about `verify_*`.
Those harnesses were written against shipped values, so they exercise the paths
the game actually takes. The oracle proves fidelity on the LIVED path; mutation
testing proves it everywhere else. Neither substitutes for the other, and a
slice that only did the first would be a port that agrees with the original
right up until a designer changes a number.

**Godot's `is_instance_valid` has no equivalent here, and one place needed it.**
A worker building a site that gets destroyed under it is released in the
original because the building is freed after its fade and the pointer goes
stale. Nothing goes stale in this port, so the question is asked directly —
`IsAlive()` alongside `IsComplete()`. Without it a worker stands over rubble
pouring time into something that cannot accept it, forever. Expect the same
question wherever the original leans on instance validity; the save slice will
be full of them.

### A mutation that had to be hunted for

The buildings slice reset the training remainder to zero rather than carrying
it, matching the original. Four mutations survived the first pass and three
were the usual thing — behaviour the shipped data cannot reach, needing an
authored fixture: a building with no build time that nobody pre-placed, a tower
that reloads faster than it thinks, and a building with an attack RANGE but no
damage (a Town Hall has neither, so removing the guard changes nothing).

The fourth was different. **Carrying the remainder is almost never observable.**
The overshoot at the end of a unit is under one step, so carrying it can only
buy back a whole step once enough have piled up — and at most step sizes the
accumulation error eats it first. Three archers at 7.0s and a step of 0.6 finish
on identical steps either way; so do 0.7 and 0.9. It took a search over step
sizes to find that **four units at 3.0s stepped by 0.05** is where the two
answers separate, and by then they are three steps apart.

That is the honest shape of the choice: not a bug that shows up in a fight, a
slow drift that shows up in a long queue. Worth pinning precisely because
nothing else would ever notice.

### GDScript's `float` is 64-bit, and that changed the answer

The raider-versus-Town-Hall case is the first thing in this port that came out
wrong: **928 against the original's 934**, stable across four re-runs of the
oracle. One extra hit of six damage in twelve seconds.

The cause is not the port's logic. It is a cooldown of 1.0 decremented by 0.1
every step, which reaches zero or below after **eleven** decrements in double
and **ten** in float — 0.1 is unrepresentable in both and the errors accumulate
in opposite directions. Eleven steps of 0.1 is an attack every 1.1 seconds for a
unit whose data says one per second.

GDScript's `float` is 64-bit. Godot's `Vector2` is **not** — it holds 32-bit
`real_t` — so the original runs positions in single precision and every other
scalar in double, and a port that picks one width for both is wrong wherever a
scalar accumulates. The simulation now follows that split exactly: `delta`,
cooldowns, timers and accumulators are `double`; positions stay `glm::vec2`.
With it, 934.

Two things this implies for every remaining slice:

- **A test that steps by `0.1f` is not stepping by 0.1.** Widened to a double
  that literal is 0.10000000149011612, and over a hundred steps that is a
  different simulation. Every dt in the port's suites is now a double literal.
- **It moved a fixture across a boundary.** Ten steps of 0.1 come to
  0.9999999999999999 in double and 1.0000001 in float, and a wave scheduled at
  t=1.0 had been starting on the tenth step and now starts on the eleventh.
  That case was sitting on the boundary and now sits well clear of it — the
  same trap, for the third time.

**The harness's own dt hid three bugs.** `verify_economy` steps at 0.2s and the
worker gathers at 1.0/sec, so a step never asks for more than one unit of wood —
and three mutations survived because of it: charging the gather accumulator for
what was ASKED rather than what came out, gathering past the carry capacity, and
leaving a tree only on the thinking tick rather than the moment it runs dry. All
three are invisible at that dt and all three are real. They needed a five-second
step, where the asked-for amount and the available amount can disagree, and a
60Hz one, where a tick is twelve steps away.

That is the general lesson: **reproducing the oracle's numbers is necessary and
not sufficient.** The harness proves the port agrees with the original on the
path the harness walks. Mutation testing is what finds the paths it does not.

**Everything before the waves slice was structural.** Gestures asserted state
transitions read out of the GDScript; data asserted key counts; economy asserted
balances recomputed from the same file both sides read. All strong, none of them
a differential check — a shared misreading of the data would have passed both
sides. `verify_waves` is where that changes, because its numbers are simulation
OUTPUT. It is worth reaching for that kind of oracle first in each remaining
slice, and worth noticing when a slice does not have one.

**Two fixtures that sat on a boundary.** The endless-growth case first ran a
window in which three waves had started and read a partial spawn drain as a
wrong count. And the spawn-priming case first sampled the accumulator at exactly
0.8 after seven additions of `0.1f`, which is a question about float rounding
rather than about the port. Both now sample well clear in both directions.

**The priming needed its own case at a small dt.** The accumulator is set to a
full interval when a wave starts, so the first enemy comes out on the same step
rather than 0.8s later. At the harness's two-second dt that is invisible — both
answers spawn the same two raiders — so removing it changed nothing any test
could see. At 0.1s it is the difference between a wave that begins and one that
begins with a beat of silence.

**One divergence to remember.** Godot's `Dictionary` preserves insertion order;
this engine's `Json::Object` is a `std::map` and is sorted by key. Nothing in
the data depends on object order — the wave schedule and the difficulty menu
order are both JSON arrays, and that is asserted rather than assumed — but
anything ported later that iterates an object and cares about the sequence has
to sort explicitly rather than inherit it.

### The match boot, and the three bugs that were hiding behind not having one — 27 August 2026

Porting `main.gd` gave the port a `Match`: the first thing in it that owns an
entity. Every suite before this built its board by hand, and
`test_wb_snapshot`'s own header said what that cost — "its specific numbers …
come from booting the whole match through `main.gd`. The port has no match boot,
so this suite builds its own board and asserts the same PROPERTIES against
numbers it sets itself."

Those numbers now come off a real boot, and they matched on the first run. The
ledger, because a number nobody can explain is a number nobody can defend when
it changes:

- **270 wood** is `economy.json`'s 300 times hard's 0.8, which is 240, plus
  three workers each banking one full carry of ten inside sixteen seconds. They
  spawn at 1680 / 1740 / 1800, the nearest tree to all three is the one at 1900,
  ten seconds of gathering at 1.0/sec, then a walk back to the Town Hall at 1500
  — and all three are back on the tree at t=16, which is also the **3 gathering**.
- **6 units** is three starting workers, one hand-placed raider, and **two
  soldiers the Barracks trained**. Those two are the load-bearing pair: they
  exist only if a building finishing a unit reaches a spawn, and that edge is
  what this slice adds.
- **963 hit points** is the Town Hall's authored 1000 less the 37 the harness
  deals. The 1000 is the boot number inside it.

**Elapsed is not 16.0, and saying it is would have been the port marking its own
homework.** Eighty additions of the double nearest 0.2 come to
15.999999999999975; the harness compares elapsed within 0.5 for exactly this
reason. Both sides do the same additions in the same order, so the residue is
identical rather than lucky — which is why the suite asserts the tolerance AND
the exact value, the second as a determinism pin. The two soldiers arrive on
steps 40 and 80, and forty additions of 0.2 reach 8.000000000000004 against a
train time of 8.0: a margin of four parts in a quadrillion, on the right side,
and the same margin in Godot.

**Three real defects, none of which any existing test could see.**

**A restored endless run was not endless.** `WaveDirector::Setup` is the only
thing that ever sets `m_endless`, it reads the mode off `GameState`, and
`Snapshot::Restore` never calls it — while the original's `snapshot.gd` calls
`wd.setup()` from *inside* restore, right after `from_save` and before the
entities, precisely so the mode is re-read. So a Continue of an endless run came
back with a director that would never generate another endless wave: the run
silently became "survive the five scripted waves and then nothing, forever". The
existing suite asserted `state.IsEndless()` and never the director's, which is
why it passed. Proved before it was fixed, with a temporary assertion that
failed.

The fix is that the Match arms the director **twice**, and both calls are
load-bearing. The first, before the restore, puts the schedule in place, because
`FromSave` clamps the saved wave index against `m_waves` and a never-armed
director clamps a wave-four run back to zero and replays the whole match. The
second, after it, re-reads a mode that did not exist yet when the first ran.
That this is safe is a fact about `Setup` rather than a hope: it writes the
schedule and the configuration, `FromSave` writes the counters, and the two sets
are disjoint. Both have their own test and both mutations go red.

**A re-booted match inherited the previous run's wave clock.** Godot never has
this problem — a fresh match is a fresh scene and therefore a brand-new director
— so there is no line in `main.gd` that corresponds to the fix. A `Match` is an
object that gets re-booted, and `Setup` deliberately touches no counter, so the
second run began with the first one's elapsed already past its opening waves and
its dead still counted as alive: it would spawn a backlog on its first step and
could never declare victory. `WaveDirector::Reset` is new, and it is exactly the
set `FromSave` writes.

**A fresh run never saw the Armory levels it was bought with.** `GameState::Reset`
adds the persistent starting bonus and reads it out of its own copy of the meta
levels — and until this slice the only writer of those levels was the restore
path. Deeper Coffers applied to a Continue and silently not to a New Game, which
is the one direction a player would never report: they would simply never see
the thing they paid for. One line, before `Reset` rather than after.

**`Snapshot::ClearRun` finally has a caller, and the anti-farm property is
enforced.** It is a pair and the port had neither half. A finished run banks its
renown and drops its Continue — the original does both in its game-over overlay,
and the *decision* half belongs in the simulation — and `AutosaveRun` refuses to
write once the run is over, which is what stops the next backgrounding putting
the file straight back. Without either, a player could reach game over, bank the
renown, and resume the same run from disk to bank it again. `Progression.hpp`
already claimed this ("so this cannot be farmed by quitting and resuming"); it is
now true rather than a comment about a caller that did not exist.

**Nothing is ever erased from the three entity vectors, and that is the design.**
`World.hpp` claims a deposit index is "the same question Godot's
`is_instance_valid` answers, asked in a way that cannot dangle". That is true
only while the container is append-only. An index is a *positional alias*, not
an identity: erase a building from the middle and a worker's cached index
silently names a different live Town Hall — it still passes `DepositExists`, the
worker still delivers, the wood still balances because nothing in the economy
cares who banked it, and no assertion anywhere notices. Erasing turns a crash
into a wrong answer, which is strictly worse. What it costs is stated rather than
hidden: memory grows with total spawns rather than with what is standing, and
every fighter walks the corpses at 8 Hz for the rest of the run. `DeadUnits()`
exists so a test can put a number on it, and the eventual fix — a reap *between*
frames that unregisters from the lane and invalidates every cached index in the
same moment — is a slice, not a line.

**One thing decided rather than demonstrated.** `Match::Step` runs
Buildings → Units → Projectiles → Director, read off the scene tree: `main.tscn`
declares `World` before `WaveDirector`, `World`'s children in that order, and
nothing in the game sets `process_priority`. The suite asserts only that this
order agrees with the harness's (Units → Buildings → Director) on the board the
oracle numbers come from — which it does — so swapping the two entity phases
would go unnoticed by every number in this port. What IS measured is the
frame-entry rule: the entity counts are taken once, before anything runs, so a
unit trained during the buildings phase waits for the next frame the way a node
added to a Godot tree mid-frame does. That has its own case, with a raider
placed inside the newborn's aggro so that "did it think this frame" is the only
question the assertion can be answering — without which an Idle soldier that had
thought and found nothing is indistinguishable from one that never thought.

**Twenty-six mutations, twenty-five caught.** Three survived the first pass and
all three were fixture gaps of the kind this project keeps re-learning: the
`Layout` struct's defaults are the shipped file's numbers, so removing the
`world.json` read entirely changed nothing until an authored 4000-wide world
with its ground line at 640 was written; no board had an unfinished building on
it, so a sink that always reported "finished" — a free Barracks on every
Continue — walked through the round-trip check; and one anchor matched two call
sites. The **one deliberate survivor** is a sink that *also* joins a building
against `units.json`, which changes nothing observable — and that is precisely
why the sink must not do it. A sink that duplicates what `Snapshot::Restore`
promises cannot tell you whether Restore is keeping its promise; that guard
stays in `test_wb_snapshot`'s `BareSink`.

**What this slice deliberately did not do: migrate the nine existing suites onto
a `Match`.** It looks like a free cleanup and it is not.
`Town::NearestEnemyBuilding` returns null unconditionally even though that
fixture owns real buildings, so the raider-walks-to-the-Town-Hall path is dead
code in `test_wb_buildings` today; `Battlefield`'s deposits are bare floats whose
`DepositExists` is a bounds check with no liveness at all, so combat's flee
reflex runs against a deposit that can never be invalidated; and
`test_wb_progression`'s fixture resets the run in its constructor, which an
un-booted `Match` does not do and a booted one contaminates with twelve resource
nodes. Swapping in a real `Match` hands three suites behaviour they have never
had, and any assertion that is green *because* of a stub flips. That is a
migration with three named deltas, and it is its own slice — the next one.

**And what it does not claim.** `verify_autosave`'s six window-and-OS assertions
are still out of reach: a static library has no `auto_accept_quit` to intercept.
What is ported is the ACTION each notification takes, which is `AutosaveRun()`;
whoever owns a window calls it on close and on background. One asymmetry is
written into the header rather than left to be found: the original's
`add_renown` writes the profile through to disk, and this banks into a `Profile`
the caller owns and then deletes the Continue — so a crash between those two
lines costs the player the run and the renown it earned.

### Surveying the nine harnesses nobody had looked at — 27 August 2026

Twelve of the twenty-two `verify_*` harnesses are reproduced and three are known
to have no headless oracle. The remaining **nine had never been surveyed**, and
"every harness with an oracle is ported" was being said without anyone having
checked. So they were read, run, and ranked.

**Two things came out of it immediately, and both are in.**

**The shipped `endless` block was read everywhere and simulated nowhere.** Every
endless case in `test_wb_waves` runs against an *authored* `waves.json` —
which is right, because authored values are the only way to reach a branch
shipped data cannot — with the result that `start_delay 90`, `interval 75`,
`base_count 8`, `count_growth 2`, `hp_growth 0.08` and `brute_every 2` were
validated by the data suite and stepped by nothing. The one case that did use
shipped data asserted only inequalities: "past wave 5", "more than 30 raiders".
A port that had the endless cadence wrong by a factor of two would have passed
it.

`verify_endless` has the numbers: **wave 12, 132 spawns, 4 brutes, a toughest
raider at 59 against a base of 40**, from 500 steps of 2.0 at difficulty
`normal`. They are simulation output in the sense this project prefers — the
sum over seven waves of `8 + 2e` raiders, a heavy on every second one, and
growth measured from the first endless wave rather than compounded. The port
reproduced all four on the first run, and the oracle was re-derived by hand
afterwards rather than taken from a paste.

**Five hundred steps of 2.0 is part of the specification.** t=1000 falls between
the seventh endless wave at 960 and the eighth at 1035, and the seventh's twenty
raiders finish leaving the spawn edge around t=977. Twenty steps more is eight
waves and a half-drained queue; twenty fewer is six. That is the same
window-too-wide trap this port has now hit three times, and it is written beside
the case.

**Two assertions in the combat suite were looser than the harness they came
from.** `verify_projectiles` asserts an in-flight arrow's *position* is
unchanged after game over; the port asserted only that it had not landed and
had dealt no damage — both of which are satisfied by an implementation that
keeps flying arrows and merely suppresses the damage, leaving a volley drifting
across a frozen board behind the game-over panel. And the pool-reuse case
bounded the pool at `<= 6` where the original pins it at exactly **3**: three
concurrent shots make three arrows, and three more after those retire make no
more. A pool that grew by one every *other* shot passed the bound.

Both are now the harness's own assertions. Six mutations, all caught.

**What the survey says is NOT worth porting, so nobody re-proposes it.**

- **`verify_export` and `verify_project`** — every assertion is a `ProjectSettings`
  read or a substring match against `export_presets.cfg`. Two of export's six are
  literal duplicates of project's. Nothing.
- **`verify_flow`** — Godot's resource filesystem and scene paths. The one line
  with a decision in it (`tree.paused = false` before every transition) has no
  port analogue.
- **`verify_pause`** — eight assertions, six of them `Control.visible` and
  `SceneTree.paused`.
- **`verify_input` sections 1 and 2** — real mouse events through
  `mouse_filter`, `CenterContainer` layout, CanvasLayer routing, and GUI input
  surviving `get_tree().paused` via `PROCESS_MODE_ALWAYS`. That last is the most
  Godot-specific assertion in all twenty-two.
- **The freed-node half of `verify_projectiles`** — the harness calls `free()`
  and leans on `is_instance_valid`. The port models a *dead* `Damageable*`, not
  a freed one; a dangling pointer there is undefined behaviour, and inventing an
  analogue would be fiction.
- **`verify_fx`'s floating numbers and `verify_project`'s camera clamp** are
  both real logic with real oracles — a pool soft cap of 64 that recycles rather
  than grows, and a clamp that centres when the world is narrower than the view
  — but both are presentation, and `Match.hpp`'s not-ported ledger already
  defers them by name. Revisiting is a decision, not an oversight.

**Two slices the survey found. The first was taken the same day; the second is
the next item.**

1. **Tone synthesis** (`audio.gd::_synth_tone`, ~30 lines) — **DONE, below.**
2. **`BuildPlacement`** (`build_placement.gd`, ~120 lines) — **DONE, below.**
   Weaker oracle —
   behaviour with decisions in it rather than computed numbers — but it is
   gameplay, and it unblocks two things this port has already written down as
   waiting for it: `Match`'s `_back_cancels_placement` predicate, and
   `GestureMachine::SetPlacementMode`, which is a flag nothing in the port ever
   sets from real state. The assertion worth having is the one no other harness
   makes: **cancelling a placement spends nothing and builds nothing**.

**And the fixture migration is NOT the next item, whatever the last note said.**
The claim was that nine suites should drop their hand-rolled `World`s. Measured
rather than assumed: the four migratable ones hold 64, 62, 72 and 13 lines of
scan code against `Match`'s 77, so the entire upside is deleting about 211 lines
that already agree with each other. Two of the five cannot migrate at all
without losing a real instrument — `test_wb_combat`'s `Battlefield` scans stub
buildings that COUNT HITS, and `test_wb_worker`'s `TestWorld` is the only world
in the port with no projectile pool, which is what proves an archer without one
simply does not shoot. And the delta that was supposed to justify the churn
turns out not to exist: `Town::NearestEnemyBuilding` returning null is harmless
because **every tower case steps only the tower and never the raider**, so
migrating would flip no assertion at all.

That last point is the real finding hiding inside the migration question. The
gap in `test_wb_buildings` is not its fixture — it is that no case there ever
steps an enemy at a tower.

### The tones are the sound, and they are the best oracle in the set — 27 August 2026

Wolf Brigade ships no audio files for the same reason it ships no art: every
sound in `audio.json` is a `tone` block — a frequency, a duration, a waveform
and a volume — that the game turns into a short 16-bit mono buffer at load. So
a port without them is a silent game rather than a game whose audio arrives
later, and `sim/AudioTones.{hpp,cpp}` is now that half of `audio.gd`. Only that
half: the voice pool, the round-robin, the mute flag and the dB busses are
engine-side plumbing and belong with whoever owns a device.

**Why this is the strongest oracle the port has had.** Every number is
COMPUTATION OUTPUT. The first three slices asserted structure — transitions,
key counts, balances recomputed from the same file both sides read — where a
shared misreading passes both sides. A shared misreading of `audio.json` cannot
make a square wave and a sawtooth produce the same bytes. All eleven sounds
matched on the first run.

**Two truncation traps live in the shipped data, and both discriminate.** The
sample count is `int(44100 * ms / 1000.0)`, truncated toward zero rather than
rounded — 55ms is 2425.5 samples and 45ms is 1984.5, so `attack` and `shoot`
come out one sample SHORTER than a rounding port gives, while the other nine
agree either way. And a sawtooth's first sample at full volume is
`int(-16383.5)`, which truncates to **-16383** and not to the -16384 a floor
would give; `destroy` and `defeat` are the two that reach it.

**A mutation walked through eleven sounds, and fixing it is the lesson.** The
first version pinned four samples per sound — first, second, middle and peak —
and narrowing the phase accumulator to single precision changed none of them. A
float phase is good to about seven digits, so its error is far below one count
early in a buffer and only grows into something visible late; every sample the
suite happened to look at was early. The fix is a sum and a sum of squares over
EVERY sample of all eleven buffers, computed the same way on both sides. With
them the same mutation fails sixteen assertions. **Spot checks measure where you
looked; a checksum measures the buffer.**

**And one mutation survives on purpose.** Truncating the sample count and
flooring it are indistinguishable, because they differ only for negative values
and every negative value is clamped to one sample. That is a *proof* that the
mutation cannot change an answer, not an admission that nothing is watching, and
it is written in the header beside the line. The truncation is spelled the way
the GDScript spells it so the two files read the same — which is the whole of
why it is a truncation.

**The probe, and why it is not in the game repository.** The harness only
reports one buffer's size ("7938 bytes"), so the rest of the table came from
running `_synth_tone` verbatim over the real `audio.json` in a throwaway Godot
project outside the game tree. Its `train` row reproduces the harness's own
7938, which is what says the copy is faithful. The game repository is the
oracle; it does not get edited in order to be measured.

---

### Build placement, and the end of the harness list — 27 August 2026

`build_placement.gd` was the last file in the game with an oracle behind it, and
porting it exhausts the list: **there is no `verify_*` harness left that holds a
number or a decision worth reproducing.** Everything from here is a choice about
what to build rather than about what to match.

The ghost is gone — a translucent rectangle following the pointer is
presentation. What survives is what it was *drawing*: a candidate x and a
validity flag.

**The order of operations in `Confirm` is the whole slice**, and it is three
different bugs if any line moves. It re-clamps and re-validates BEFORE spending,
so the price is paid for the spot the building lands on rather than the one the
pointer was over. An invalid spot returns WITHOUT cancelling, so a player who
taps a wall slides over and tries again instead of walking back to the menu. And
a cost that became unaffordable mid-placement cancels WITHOUT building. Each is
one line; each has its own case.

**Two divergences Godot gets for free and this port has to ask for.** Placement
scans the player-buildings *group*, which a destroyed building leaves when it is
freed — nothing is freed here, so without an explicit liveness check the rubble
of a fallen barracks would reserve that stretch of lane for the rest of the run.
And the overlap test is **x-only**, not the rectangle test the port already has:
the original calls itself a "single-lane x check" and is right to, because two
buildings sharing a stretch of lane collide whatever their heights. The two
agree on every board the game can build — everything sits on one ground line —
and differ only on a fixture that puts a building where the game cannot.

**A flag that has never had a caller finally has one.**
`GestureMachine::SetPlacementMode` has existed since the input slice with
nothing in the port ever setting it from real state — only a test ever touched
it. Placement now emits `placementActiveChanged`, and the suite pins the
non-obvious half: beginning a placement while one is already open is a SWAP, so
it announces false and then true rather than staying silent. An input controller
that only listens for edges would otherwise never learn the building changed.

**The Back gate is ported whole**, because the original kept it a pure predicate
on purpose — its own comment says so, so that a harness could verify the gate
without tripping the `quit()` in the other branch. The regression it exists for
is asserted directly: victory or defeat can land while a ghost is still up, that
ghost then sits behind the game-over overlay where nobody can see it, and Back
must EXIT on the first press rather than spend one cancelling something
invisible.

**What is not claimed.** `verify_input` does not run headless — its own header
says so, and a headless run fails six assertions because an offscreen viewport
will not route GUI mouse picking. Every one of those six is a real click on a
`Control`. What this suite reproduces is the placement state those clicks were
driving; the clicks themselves are Godot mechanism and stay unported.

---

**The phase is not nearly closed, whatever the table's length suggests.** The
done-when is "the simulation runs headless in a test executable, waves spawn on
schedule, **and a save round-trips**". Nothing yet ports `save.gd` or
`snapshot.gd`, and those four harnesses serialise every class written so far —
including the `Damageable*` and world-index indirections this port introduced,
which have no GDScript analogue and need a deliberate stable-id design. That is
where the novel work in Phase 3 actually is.

*(Written before the save slice landed, and left standing because the order it
argues for is the order that was taken. Both halves are now done: the save
round-trips, and as of 27 August it round-trips through a real match boot.)*

**Order**, by dependency rather than by file size: `input_controller` (Phase 4's
open half, and the smallest slice with an oracle) → `data` → `economy` →
`units`/`combat` → `waves` → `buildings` → `upgrades`/`meta` → the four
save-shaped harnesses last, because they serialise everything above them and are
the "a save round-trips" half of the done-when. `unit.gd` is the largest file in
the project and is deliberately not first.

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

**The engine half is done. The phase is not:** the gesture machine itself is
port work and had not been written when this was recorded. What the engine now
provides, and how it was measured:

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

**The gesture machine is now ported** — `games/wolfbrigade/sim/GestureMachine`,
verified against `tools/verify_touch.gd` section A. Every case that harness
asserts is asserted here at the same coordinates, because a case that
discriminates in GDScript at 5.4 px of jitter and a 70 px drag is only known to
discriminate at those numbers.

Two differences from the GDScript, both forced. Godot pushes
`InputEventScreenTouch`/`ScreenDrag`; this engine polls contacts once a frame —
and the correspondence is exact, which is what the contact work above was for: a
press is a `Began`, a drag is a `Moved` carrying its own delta, a lift is an
`Ended`. And everything stays in SCREEN space, where the GDScript converts taps
and the committed box through the viewport's canvas transform: doing that here
would drag a camera into the one file whose value is being testable without one,
so the caller converts.

Five mutations, all caught: making the hold test strictly greater (1 failure),
the drag threshold at-least (1), the pan un-negated (2), letting a second finger
steal the gesture (3), and leaving the finger latched through a placement-mode
change (2). The two comparison operators are worth the two cases on their own —
the GDScript uses `>` on distance and `>=` on time, and swapping either changes
which gesture a borderline drag becomes.

**Still open:** the script ABI, which exposes `mouseDelta` and not contacts. The
port is a C++ layer rather than scripts, so nothing needs it yet.

> **Correction, later the same day.** This section originally said the machine
> could not be written because the game "is not in this repository". It is not
> in this repository and it IS on this machine, at `D:/The-Wolf-Brigade` — I
> had searched one directory and generalised from it. See the Phase 3 section
> for what that changes, which is most of the plan's test story.

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
