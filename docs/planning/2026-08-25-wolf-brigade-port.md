# Porting Wolf Brigade to Supersonic

> **Plan, 25 August 2026.** Written against `D:/The-Wolf-Brigade` at its current
> state and Supersonic at `47473fa`. Every claim below was read out of one of
> those two trees; where a number is a guess it says so.

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
| Peak entities | Waves of 8–12 (`data/waves.json`), endless growing by 2 per wave. Tens, not thousands. |
| Data | All gameplay numbers already live in `data/*.json` — the port reads the same files. |

The peak entity count is the happiest number here. Supersonic submits one draw
and one push constant per entity with no instancing and no sort
(`src/core/RenderSystem.cpp`), which would be a problem at HUSK's ~5,000 and is
irrelevant at Wolf Brigade's few dozen.

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

---

## Phase 1 — 2D presentation (1–2 weeks)

The engine renders one way: perspective, PBR. The game needs neither.

| Item | Kind | Size |
|---|---|---|
| Orthographic camera | engine feature | ~1 day |
| Unlit / flat-colour material path | engine feature | ~1–2 days |
| Quad primitive with a pivot | engine feature | hours |
| Explicit draw order | engine **assumption change** | ~2–3 days |

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

**Done when:** a unit renders as ring-behind-body-behind-bar-behind-label in
that order, at a fixed screen size independent of window size, and a scene with
no orthographic camera renders byte-identically to before.

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
- **World-space UI.** The HP bar and the unit label are world-space children of
  the unit. There is no "label at a 3D position" in the engine.
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
