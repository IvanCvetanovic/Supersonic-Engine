# Updating the engine

> **Plan, 27 August 2026.** Written against Supersonic at `c9669a6`, HUSK at
> `D:/Test Game`, and Wolf Brigade at `D:/The-Wolf-Brigade`. Every number below
> was read out of one of those three trees; where something is an estimate it
> says so.
>
> Written after ten gaps were closed in one day, all of them found by asking
> what two real games needed rather than by looking at the engine on its own.

## The finding that should decide the order

**The determinism story is nearly finished and nobody noticed.** `SimulationClock`
is built — a tick counter whose seconds are derived rather than summed, with a
test showing the summed version drifts over a hundred thousand ticks.
`StateHash` is built — byte-wise, order-independent, seeded per entity id.
`--fixed-step` is built, and five runs of one scene produce one image.

That is the hard 80% of the only property this engine can offer that Godot
cannot. Godot's physics is explicitly non-deterministic across platforms and its
own documentation says not to rely on it. Three things stand between here and a
guarantee, and ARCHITECTURE §8c already names all three.

So the order is not "biggest gap first". It is: **finish the one thing that is
almost done and is worth having, then stop adding engine features.**

That matters because of the second finding: **both games are now unblocked, and
what remains for them is not engine work.** Wolf Brigade's simulation is ported
and reproduces all 22 of the original's harnesses, but nothing draws it. HUSK is
23,400 lines of Rust to rewrite. Neither is shortened by anything below.

---

## Phase 1 — The game clock

**Why first:** everything in Phase 2 rests on it, and the three items are the
last of §8c. *Estimate: 3–5 days.*

### 1.1 A tick rate the game authors

`kFixedPhysicsStep = 1.0f / 60.0f` at `SupersonicApp.cpp:57` is the physics step
*and* the game tick, because there is only one. HUSK simulates at 20 Hz; a game
wanting 30 gets 60 whether it wants it or not, and pays for the extra 30.

Split them. Physics keeps its own step — a solver has a stability reason for its
rate — and the game tick becomes an authored number in the scene or the
manifest. They are not the same question and should not share a constant.

### 1.2 Scripts are framed, not ticked

`ScriptEngine::Update(m_registry, deltaTime)` at `SupersonicApp.cpp:1043` runs
**outside** the fixed-step loop, on the frame delta. So gameplay written as a
script is a function of how fast the display is keeping up — which is exactly
the bug §8c was written about, still present in the one place a game actually
writes its logic.

This is the item most likely to bite a real game and the least visible. Move
scripts inside the tick.

### 1.3 Interpolation by overstep fraction

Without it, a 20 Hz simulation renders at 20 Hz no matter the frame rate, and
looks it. HUSK already solves this in its own `interp.rs` by reading
`overstep_fraction()` and lerping between the last two tick positions — which is
the shape to copy, because it is the shape a ported game will expect to find.

Expose the fraction; let the render transform be a lerp of the previous and
current tick state. Note the trap up front: the *previous* state has to be
stored per tick, not per frame, or the lerp is between two copies of the same
value and does nothing while looking like it works.

### 1.4 Stop discarding simulated time

`SupersonicApp.cpp:1034-1036` zeroes the accumulator after five steps. That is
the standard spiral-of-death guard, and it is right for *physics* — dropping
fidelity beats freezing. It is wrong for a *game clock*: every mission timer,
cooldown and build queue runs slow under load, and slow by an amount that
depends on the player's machine.

Keep the cap on physics. For the game tick, either run to catch up or report the
debt to the game so it can decide. Do not silently throw it away.

**Done when:** a scene with a scripted timer, run at 30 fps and at 144 fps for
the same wall-clock duration, reports the same tick count and the same
`StateHash`.

---

## Phase 2 — Replay

**Why:** it is what determinism is *for*, it falls out almost free once Phase 1
lands, and it is the single feature that would make somebody pick this engine
over Godot. *Estimate: 3–5 days.*

Record the input stream per tick; play it back into a fresh run; assert the
`StateHash` matches at every checkpoint. Three things come out of one feature:

- **Reproducible bug reports.** A player sends a file, not a description.
- **Regression tests over a whole match** rather than over a function. Wolf
  Brigade's 22 harnesses become one replay each.
- **Lockstep multiplayer for a fraction of the netcode** — send inputs, not
  state. This does not make the engine networked; it removes the reason most of
  the networking would have existed.

The hash already exists and is already the right shape. The work is input
capture, a file format, and a `--replay` flag beside `--fixed-step`.

**Done when:** `--replay` of a recorded session reproduces its final `StateHash`
byte for byte, and a deliberate one-tick divergence is reported with its tick
number.

---

## Phase 3 — The four small gaps

*Estimate: 1–2 days for all four.* None blocks a game; all are real.

- **`Skeleton::skinRadius` is declared, documented and never written.** It is
  meant to inflate pose bounds so an animated mesh is not culled by the
  bind-pose box it has walked out of. Nothing computes it and nothing reads it,
  so a character walking out of its bind bounds pops — for node rigs *and* for
  skinned meshes. Compute it in `buildSkeletonFromNodes`; union it into the pose
  bounds.
- **A cut-out shadow cuts against the first surface.** `GatherShadowCasters`
  resolves one material set per caster, so a multi-surface model whose holes are
  on its fourth surface casts the silhouette of its first. Nothing HUSK needs is
  affected — all 166 of its materials are `OPAQUE` — but a leaf card packed into
  a multi-material model shadows wrongly.
- **`UIInput::Update`'s return value is discarded.** §10 already says the guard
  it was written for does not exist.
- **The flycam polls W/A/S/D directly with no off switch**, which fights any
  game that binds those keys. It is the editor camera reaching into a shipped
  game.

---

## Phase 4 — What the games need, which is not engine work

Listed so it is not mistaken for the engine's backlog.

**Wolf Brigade** — the simulation is ported and verified; nothing renders it.
Only `tests/CMakeLists.txt` links `WolfBrigadeSim`. It needs a view layer: the
lane, the units as coloured quads, the HUD, the audio hookup. *Estimate: weeks.*
Note the finding from the port plan: the game has **no art** — 21 buttons, 20
labels, 19 `ColorRect`s — so this is layout and wiring, not asset work.

**HUSK** — 23,400 lines of Rust, 7,746 of simulation and 15,654 of client. The
simulation depends only on Bevy's ECS, so it ports the way Wolf Brigade's did:
as a self-contained library with its own 20 Hz tick, driven by game code.
*Estimate: months.* Port the sim first and stand it up under the existing
harness pattern before any of the client, because the sim has a hash oracle and
the client does not.

---

## Phase 5 — Deliberately not doing

The honest list of what Godot has and this will not, with the reason.

| | Why not |
|---|---|
| **A scripting language** | The largest real gap: every gameplay change is a compile and nobody who is not a C++ programmer can touch behaviour. Also the largest project on this page by an order of magnitude — a language, a binding layer, a debugger. Hot-reloaded C++ is the compromise already made. |
| **A 2D engine** | Sprites, tilemaps, 2D physics, 2D lights. Wolf Brigade is 2D and will be built out of unlit quads instead. Worth revisiting only if a third 2D game appears. |
| **Pathfinding** | No navmesh, no A*, no flow fields, nothing. HUSK brings its own; Wolf Brigade is single-lane. Build it when a game needs it, and build the kind that game needs. |
| **Networking** | Lockstep replay (Phase 2) covers the case this engine's games actually have. General replication does not. |
| **GPU particles, localization, theme system, console export** | Real gaps, none load-bearing for either game. |

**The gap that cannot be closed by writing code:** ecosystem. Godot has an asset
library, thousands of tutorials, and a Stack Overflow answer for the thing you
are stuck on at 2am. This engine has one person and a very well-commented
codebase. That is a genuine trade and it should be made with open eyes.

---

## The order, in one line

Clock → replay → the four small gaps → stop, and go build the games.

Phases 1 and 2 are about a week together and are the only ones that change what
this engine *is*. Phase 3 is a day. Everything after that is game work, and the
engine should stop growing until a game asks it to.
