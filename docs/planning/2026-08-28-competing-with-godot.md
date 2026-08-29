# Competing with Godot

> **Plan, 28 August 2026.** Written against Supersonic at `b6c7869`, after Phases
> 1–3 of [the 27 August roadmap](2026-08-27-engine-roadmap.md) and the
> [determinism audit](2026-08-28-determinism-audit.md) all landed.
>
> Commissioned as: make this engine as versatile as Godot, and *optimal in
> performance, if not better*. The 2D engine, pathfinding "and everything else"
> from that roadmap's Phase 5 "not doing" list are explicitly back on the table.
> A custom scripting language is **not** — that decision stands, and where
> scripting appears below it means embedding Lua, never writing a language.
>
> Every number here is measured on this machine (i7-12700H / RTX 3060 Laptop,
> Release build) or read out of the tree, and says which it is.

## Three things decide this plan, and none of them is a feature

**1. The headline claim is not enforced.** `.github/workflows/ci.yml:26-27` reads
`on: workflow_dispatch:` — `push:` and `pull_request:` are commented out at
lines 19 and 21, switched off in August to save Actions minutes. Nothing gates a
push. And the record→replay round trip has never been a build step. So
"deterministic replay that fails CI on divergence" is true of the library and
false of the repository. The evidence that this already costs something: the
README badge says **36 suites**, `README.md:267` and `AGENTS.md:62` both say
"Thirty-six suites", and the build registers **48** by that pattern and runs
**61** under ctest. The `doc-facts` job exists precisely to catch that, and it
has not run.

**2. The oracle cannot see the only game in the tree.** `StateHash.hpp` exposes
exactly one function — `uint64_t Compute(const entt::registry&)` — and **no
registration API**. `ComponentCodec.hpp:81` has `RegisterComponent`, opened for
exactly this reason. Serialisation was opened to a game's own state; the oracle
was not. Wolf Brigade's authoritative state is a C++ object graph (`GameState`,
`std::map` resources, an `EventBus`) owned by a layer, not components in the
registry. When the sim is finally wired to the view, `StateHash::Compute` will
walk a registry that contains none of it — and report agreement.

That is the *falsely-true* failure this project has now hit three times: the hash
seeded on a recycle counter, the hash blind to script state, and the demo scene
that reads no input so a green replay proves nothing. Each time it looked like a
working feature. This is the same shape, one level up, and it is aimed at the
engine's single differentiator.

**3. The engine is not slow, and "faster than Godot" is the wrong claim.**
Measured: **10,000 visible cubes cost 4.68 ms of CPU — 214 fps — and the 60 Hz
ceiling is roughly 38,000 drawables.** The GPU is idle throughout (`Frame Wait`
≈ 0.02 ms); this is a CPU-bound engine with room. Against the demand: the
author's own spike measured a real Wolf Brigade campaign frame at **400
drawables, +0.16 ms**, and concluded "the worry was unfounded at campaign scale."
That is roughly a 10× margin on the game that exists.

The deeper point is structural. **Determinism forbids the tools Godot uses to be
fast.** Godot 4.4+ ships Jolt: multithreaded, SIMD, BVH broadphase. This engine's
oracle is bit-equality with no tolerance, which rules out threading the solver
(accumulation order becomes thread scheduling), reordering the broadphase
(`PhysicsSystem.cpp:312-325` exists to pin it), and SIMD in the solver (different
association and FMA contraction, different rounding). Three of the four
techniques that make a modern physics engine fast are permanently off the table.
That is a trade already made, not a gap that closes with effort.

## What "compete with Godot" can honestly mean

Three readings, and only two are achievable by one person.

**Match Godot's breadth.** Not achievable, and it is worth saying why plainly
rather than as a caveat. Godot's feature surface is thousands of person-years.
The gap that cannot be closed by writing code is ecosystem: an asset library,
thousands of tutorials, and a Stack Overflow answer at 2am. Also — the repository
is **private**, has **no tags and no releases** in 248 commits, and ships no
binary. There is currently nothing for an ecosystem to form around, which makes
ecosystem work premature by exactly one step rather than merely expensive.

**Be the better choice for a specific kind of game.** Achievable, and mostly
built. Deterministic simulation games — lockstep RTS, replay features, competitive
multiplayer, speedrun verification, and *automated regression tests over
gameplay* — are where Godot is structurally weak, because determinism is cheap in
a flat ECS and expensive in a scene tree. `StateHash::Compute` is one function
over one registry; Godot has no equivalent primitive at all. This is the only
claim on this page that is checkable by a sceptic, defensible by one person, and
already 80% built.

**Be excellent for its author's two games.** Already nearly true, and the two
games are the honest demand signal.

The plan below leads with the second reading and delivers the versatility work
regardless, because that is what was asked for. Where a workstream has no demand
signal, it says so rather than quietly dropping it.

## Phase 0 — one afternoon, and do it first

Fix the suite count in three places (`README.md:12`, `README.md:267`,
`AGENTS.md:62`), fix `ARCHITECTURE.md`'s stale ABI version against
`ScriptPluginApi.h:47`'s 13, then restore `push:` and `pull_request:` in
`ci.yml`. **In that order** — `doc-facts` fails on the numbers today, and a gate
that is red on its first automatic run gets switched back off.

Highest value per hour on this page, and not a feature.

## Phase 1 — make the claim true. 1–2 weeks

Nothing else on this page is worth more.

**Open the oracle.** Give `StateHash` the registration API `ComponentCodec`
already has, so a game can contribute its own state. Then decide, and write down,
which claim the engine makes: *"replay reproduces the engine's state"* (true
today, and much smaller than advertised) or *"replay reproduces the game"* (what
the README implies, and what needs this). Either is defensible; shipping the
second sentence while the first is what is built is not.

**Make CI enforce it.** A fixture scene whose script reads an axis inside the tick
and writes state the hash covers, recorded, replayed, and compared as a build
step. MainScene cannot serve — it reads no input inside a tick, which is why its
green replay proves reproduction and not that input drives it.

**Run the cross-platform experiment once.** Record under MSVC, replay under
libstdc++, compare per-checkpoint. Days. It is placed here not because it is the
most valuable but because **its result changes the cost of everything below it**:
if Euler-through-`asin` or the per-body `pow` damping diverge, the fix is a
rotation-representation change inside `PhysicsSystem`, and that is a thing to
discover before six weeks of navigation agents are hashing on top of it.

Budget 2–3× on anything touching the replay oracle. Phase 2 was estimated at 3–5
days and took seven prerequisite commits before a line of the file format existed.

## Phase 2 — the defects. ~1.5 weeks

Found while costing the 2D work, and they are bugs rather than features:

- **Blended draws are not depth-sorted against the view direction.** The
  transparent pass sorts by distance in a way that breaks for an orthographic
  camera. Half a day, and it reuses the exact A/B pixel-diff oracle the cut-out
  shadow fix used. It blocks rendering Wolf Brigade, which makes it a
  prerequisite rather than a nicety.
- **`ClusterGrid` and `ShadowCascades` assume a perspective frustum.** An ortho
  camera needs a linear z distribution, not a logarithmic one — the log split
  exists because a perspective frustum grows and an ortho one does not. 2–3 days.
- **Three path anchors disagree.** The manifest resolves from the executable
  directory; shaders load CWD-relative; the packager reads its source tree
  CWD-relative. One helper, three call sites, 2–3 days.

**Ship the `TimeTravelDebugger` fix here too — one day.** `RecordFrame` is called
at `SupersonicApp.cpp:1370` gated on play mode and **not on the editor**, so a
packaged game runs it every frame. `EntityStateSnapshot` is 68 bytes padded and
`MAX_HISTORY_FRAMES` is 1200, so at 8,000 entities that is **653 MB resident**
plus an O(N) walk with a `try_get` per entity. `m_manifest.isGame` is already the
discriminator used at `SupersonicApp.cpp:352`. One condition.

## Phase 3 — render Wolf Brigade. 4–5 weeks

The 4,086-line simulation is verified against the original's 22 harnesses and
**is not in the game binary**: `games/wolfbrigade/CMakeLists.txt:21` links
`SupersonicCore` only, and the 218-line layer that draws is a coloured-quad spike
that never includes `sim/`.

Every renderer feature the August port plan asked for has since shipped —
orthographic camera, unlit path, quad primitive, `sortKey`, world-space text and
shapes. What remains is wiring, plus one thing nobody costed: **the UI half is
~60 controls** (21 buttons, 20 labels, 19 `ColorRect`s) against a UI system with
no containers, no scroll view and no theme. That is where a "2–3 week" estimate
becomes five. Build exactly the containers those 60 controls need and let the
general shape emerge from a real caller.

> **Correction, 29 August.** The two paragraphs above are wrong in three ways,
> and left standing they commission a week of work nobody needs. Kept rather
> than rewritten, because a dated planning record that edits itself to have been
> right is worth nothing — but do not re-estimate from them.
>
> 1. **The sim IS in the game binary.** `games/wolfbrigade/CMakeLists.txt:31`
>    links `WolfBrigadeSim`, and the layer steps a real `Match` on the tick.
>    Phase 3 shipped.
> 2. **It is 44 screen-space controls, not 60.** The census counted scene files,
>    not UI: 13 of the 19 `ColorRect`s and 3 of the 20 `Label`s are world-space
>    entity visuals, and `WolfBrigadeLayer.cpp` already draws all 16 as quads.
>    44 + 16 = 60, so the original number reconciles exactly — it was just
>    counting two different things.
> 3. **"No containers, no theme" is false.** `UIStackComponent` is Godot's
>    HBox/VBox with `spacing`, block-level `anchor` (= CenterContainer) and
>    explicit child order; `layoutStacksImpl` nests, measures text through font
>    metrics, and centres children on the cross axis. `UICanvas.hpp` had already
>    counted the real game: **15 of its 16 container uses are covered.** And
>    there is no theme to port — the original has zero `.tres` files, just
>    inline overrides that map onto fields the components already have. Only the
>    scroll-view clause survives, and that ScrollContainer never scrolls: the
>    armory's four rows are 442 units in a 560 viewport, and a sixth would be
>    the first to overflow.
>
> What is actually missing is correctness, not capability, and it does not show
> up in a screenshot. Two are fixed as of this correction: a hidden button or
> field still reserved its slot in a stack, and — worse — hiding a *container*
> left its children with no rectangle, so they fell back to their own anchors
> and every button in a hidden menu piled onto the middle of the screen, still
> clickable over the live game. That is how this game opens all three of its
> modals. Still open: `UIOrderComponent` is read as both the intra-stack sort
> key and the draw layer, which collides on exactly one screen (the pause menu,
> at layer 9 with ranked children), and there is no `selected` look for the five
> radio buttons.
>
> The sequencing also changes. Build the **in-match HUD and bottom bar first** —
> it needs no engine change at all. The main menu is blocked behind three
> unported subsystems that are not UI work (`save.gd`'s difficulty, mode, muted
> and volume keys; ~250 lines of audio state; 43 lines of screen routing), and
> that, not widgets, is what "2–3 weeks becomes five" should have been pointing
> at.
>
> One constraint found while proving this, and it decides how every screen is
> written: **a button rebuilt mid-gesture never fires.** `pressed` lives on the
> component and a click is the transition off it, so a HUD rebuilt from the
> simulation every tick — the obvious shape, and the one the lane's quad pool
> uses — hands the release to a component that was never pressed. Rebuild a
> screen when it *changes*. Pinned by `test_uiinput`.

This phase produces the first screenshot of a real game running on this engine,
which is the artifact that makes every other claim on this page legible.

## Phase 4 onward — the versatility work, in the order that buys the most

Costed in one-person weeks. The ordering is by genres unblocked per week spent.

| | Weeks | What it actually is | Demand today |
|---|---|---|---|
| **Sprites + ortho editor** | 2–3 | `SpriteComponent` (region, pivot, flip, PPU), frame animation on the tick, nearest filtering via `.meta`, ortho editor camera and constrained gizmo | Shaped by Phase 3 |
| **Nav: grid + flow field + agents** | 3 | `NavGrid`, integer costs, `WorldShapes` overlay, `StateHash` extension, a deliberately-broken replay test first | HUSK has 923 lines of its own |
| **Nav: A\* + steering** | 3 | Per-agent paths, local avoidance | None yet |
| **Lua (embedded, not written)** | 5–6 | Second backend behind the existing `ScriptRegistry`; determinism work is the bulk | Unlocks zero genres |
| **Tilemaps + Light2D occluders** | 3–5 | | **Zero** across 44 Wolf Brigade scenes |
| **Android / iOS / web export** | 6+ | GLFW has no backend on any of them | None |

**Sequence sprites *after* Wolf Brigade renders**, not before. That is the single
biggest ordering change here: a real caller will have told you which of these are
actually needed and in what shape, which is the discipline that made the layer
seam right and the pathfinding decision right.

**Nav stops after the grid unless a game asks.** No navmesh, written or vendored
— vendoring Detour surrenders bit-reproducibility on the one subsystem where
determinism is the selling point.

**Lua goes last, deliberately.** Three input-channel rescopings landed in the last
week (ABI 11, 12, 13); embedding a VM before those settled would have embedded the
bugs behind an interpreter.

## Performance: what is measured, and what to do about it

Measured at N≈8,000 with one directional light and no game logic: Physics 4.70 ms,
Scene Record 2.17, Shadow Record 1.19, Frame Prepare 0.80, Transform 0.50.

**Where the engine is already ahead, structurally.** The visibility walk costs
**27 ns/entity** — an eight-corner AABB transform plus six plane tests over two
packed EnTT arrays, where Godot chases pointers through `Object`-derived nodes.
No script VM on the engine path: 10–50× against GDScript on tight loops, and
roughly a **wash against GDExtension** — say that yourself before a sceptic does.
Startup, scene load, and frame-time variance likewise.

**Where it is behind, and will stay behind.** No instancing, no LOD, no occlusion
culling, no streaming, no compute pipeline at all. Each is 3–6 weeks and Godot
has all of them. One person does not close that on breadth.

**Do three things, not fifteen.**

1. The `TimeTravelDebugger` gate (above). One day, and it is a shipping bug.
2. **Assert counts, never milliseconds** — draw calls submitted, entities walked,
   allocations per frame. Half a week. Sixty-one suites and **not one asserts on
   any cost quantity**; `Stats::drawn` reaches only the ImGui panel, with no path
   a test can read. A millisecond threshold is flaky across machines and gets
   deleted within a month; "one draw call per drawable" is a property a test can
   hold, and when instancing lands the count drops and the test records it.
3. **Instancing — but not now.** 3–4 weeks, the largest win available, and
   completely determinism-free since the hash reads no render state. Trigger it
   on *sustained drawables crossing ~3,000, or HUSK's first mission rendering,
   whichever comes first.* That number is the measured endless-mode design ceiling,
   not a guess.

**And one premise worth one command before any of it:** every "CPU-bound"
conclusion rests on `Frame Wait ≈ 0.02 ms` measured in a **755×389 offscreen
viewport** with untextured cubes. Run one scene fullscreen at 1080p and see
whether `Frame Wait` moves. If it does, every CPU millisecond above buys nothing.

## What I would not build

**The entire physics performance programme — 6+ weeks, zero users.** It is the
perfect trap: a real O(n²) (measured at **133.7 ms for one step of 10,000
co-linear bodies**), an elegant fix, a subtle determinism story, a satisfying
commit message. And **both games have zero colliders** — `2026-08-19-two-games.md`
recorded that after reading both game trees: *"Wolf Brigade contains no collider,
no `Area2D` and no physics body anywhere in the project… HUSK excludes a physics
engine on purpose and would be actively harmed by one."*

Worth naming what happened during this very study: three independent agents, none
of which opened `docs/planning/`, re-derived the physics broadphase as the top
priority — because "lane RTS" *sounds* like x-band clustering. That is the
description-not-measurement failure this project has a documented history of,
reproduced live inside the study meant to prevent it.

Also not building: `ClusterGrid` parallelisation (HUSK ships **zero** point
lights; the proposal is dimensioned for 300), threaded command recording
(strictly dominated by instancing), LOD/occlusion/streaming (no seam exists,
neither game is near the scale), and sub-millisecond zones at entity counts
nobody has.

And one trap to leave alone permanently: **do not cull-gate `EvaluatePoses`.** It
writes the bounds the culler tests against, so gating it on culling makes culling
depend on its own output — a character that leaves the frustum stops widening its
bounds and can fail to re-enter.

## Calendar, one person, five-day weeks

- **Phases 0–3** → a rendered Wolf Brigade by **early November 2026**
- **Phase 4 (sprites, ortho editor)** → a visibly more capable engine by **late November**
- **Nav (grid, flow, agents)** → **February 2027**
- **Lua** → **April 2027**

Shortest path to a *visibly more capable engine*: Phase 0 + Phase 2 + sprites,
≈ 4.5 weeks. Shortest path to a *shippable game*: Phase 0 + the blended-sort fix
+ Phase 3, ≈ 5.5 weeks. **The game path is worth more, because it produces
evidence rather than surface.**

**If it all takes twice as long, cut in this order:** Lua (unlocks zero genres,
and the C++ plugin path works); nav A\* and steering (HUSK brings its own);
the ortho editor (hand-editing scene JSON is survivable for one author); sprite
frame animation (Wolf Brigade ships no art).

**Never cut:** Phase 0 (an afternoon), Phase 1 (it is the entire differentiator
and it is currently unenforced), or the Phase 2 defects (two are bugs, and one is
in the pass every sprite in the engine would draw through).

## The one-line version

Make the claim true, fix the four bugs, render the game you already finished — and
only then decide how much of Godot's surface is worth copying.
