# What the determinism story was actually missing

> **Audit, 28 August 2026.** Written against Supersonic at `bf491fd`, after
> Phase 1 of [the 27 August roadmap](2026-08-27-engine-roadmap.md) landed and
> before Phase 2 was started. Every claim below is tied to a file and a line, or
> to a test in this repository that fails without the fix.
>
> It exists because the roadmap's central premise turned out to be wrong, and
> the roadmap is a dated record that must not be edited to agree with what was
> found later.

## The premise this replaces

The 27 August roadmap opens with a section called *The finding that should
decide the order*, and it says:

> **The determinism story is nearly finished and nobody noticed.** [...] That is
> the hard 80% of the only property this engine can offer that Godot cannot.

That was written from reading the three pieces — `SimulationClock`, `StateHash`,
`--fixed-step` — and finding each of them well built. Each of them is. The
mistake was inferring from three good components that the property they exist to
provide had been achieved, without running the experiment that would have said
so.

**The experiment: load the same scene twice and hash it.** It had never been
written, because every test in `test_determinism.cpp` built one registry and
never reloaded it. It fails:

```
a reloaded scene is the same state and must hash the same:
    659507729677990463 vs 3903311430927670655
```

An EnTT handle packs an index and a *version* — a recycle counter bumped on
destroy so a stale handle can be caught. `registry.clear()` destroys everything,
so a second load hands back the same indices carrying different versions.
`StateHash` seeded each entity on `entt::to_integral`, the whole handle, so the
seed moved while the world did not.

What that cost: every determinism claim the engine made was implicitly scoped to
*one process that has loaded exactly one scene*. The editor reloads on every
Stop and Play. A packaged game reloads the level the player just died in. And
the case the hash exists to serve — a recording made in one process compared
against a replay run in another — is precisely the case it could not serve.
"Five runs of one scene produce one image" was true, and was not evidence: a
`--frames` run loads once.

**So the honest estimate for Phase 2 is not 3–5 days.** The hard 80% was the
design, not the state. What follows is what a replay would have fallen through.

## The two kinds of hole

Sorting by symptom turned out to matter more than sorting by severity, because
the two kinds fail in opposite directions and only one of them is loud.

**A replay that comes out FALSE** is a hole that stops the run reproducing. It
is annoying, it is visible the first time anyone tries, and nobody ships on top
of it.

**A replay that comes out FALSELY TRUE** is a hole in the oracle. The run
diverged and the hash said it did not, because the hash was not looking at the
thing that diverged. This is the dangerous kind: it does not stop the feature
being built, demonstrated, or believed, and the first person it fails is
somebody relying on it.

Both block. The second is worth naming separately because it is the one that
would have survived a demo.

## Fixed here

### The oracle was smaller than its own stated rule

`StateHash.hpp` states the rule: *everything a step can change and a later step
can read*. Three things it named were in. Three things that meet the rule were
not.

- **The seed carried the recycle count.** Described above. Now
  `entt::to_entity`, the index alone — unique among live entities, which is the
  only property a seed needs. Two crates swapping positions is still a
  different world; the version was bookkeeping about handles and was never
  state.
- **Entities without a transform contributed nothing — and were not counted.**
  Both the contribution and `++entities` sat inside `view<TransformComponent>`,
  so the count that exists to stop things hiding was itself inside the filter
  that hid them. This is not a hypothetical shape: `ScriptEngine` deliberately
  runs scripts on entities *with or without a place in the world*, so that a
  script can drive a HUD element — and a HUD element has no transform by design,
  because it lives in screen space. The demo scene builds one. **A scene of
  scripted HUD elements hashed identically to an empty registry.**
- **Script state was not in it.** `ScriptComponent::state` is written by a tick
  and read by the next one, which is the rule exactly. It looks like scratch and
  is not serialised, and a cooldown or a state-machine phase kept in it decides
  what the next tick does. A divergence there would have been reported as
  agreement until the tick that finally turned the counter into a position —
  which names a tick thousands after the one that went wrong.

The last two are the *falsely true* kind. Neither would have stopped a replay
being built and shown working.

### A gesture scoped two different ways

`scriptWasPressed` was moved onto the latched tick edge when scripts moved
inside the tick; `scriptWasReleased` was left reading the frame edge, under a
comment saying both had moved. A script that charges on the press and fires on
the release saw the press exactly once and the release either never — the key
came up during a frame that ran no tick, which at 20 Hz on a 144 Hz display is
six frames out of seven — or three times, because the frame ran three ticks.
Fixed in `bf491fd`, with the ABI version bumped for a change that moves no
struct.

## Found, tied to a line, not yet fixed

Listed so they are not rediscovered as mysteries. Each is a real defect with a
file and a line; none is speculative.

| | Where | What |
|---|---|---|
| **The clock never resets** | `SceneSerializer.cpp:236` | A scene load assigns `fixedDelta` and nothing else. `tick` and `droppedSeconds` survive `registry.clear()` in the context and carry into the next scene. `SecondsF()` narrows `tick * fixedDelta` to a float *before* the subtraction in `script.elapsed`, so the value a script integrates sits on a lattice whose spacing depends on the base tick — the same logical tick produces a different number depending on what the process did earlier. |
| **Edit-mode edges arrive in one lump** | `SupersonicApp.cpp:1051` | `BeginTickInput` runs only inside the tick loop, which is gated on Play. `Input::Update` keeps latching every frame regardless. So every key pressed while the editor sat in Edit or Paused piles up, and the first tick after Play sees the union — a set that is a function of how long a human sat there. |
| **Contacts are frame-scoped, the tracker is tick-scoped** | `SupersonicApp.cpp:1042` | `m_contacts.clear()` is outside the tick loop; `m_contactTracker.Update(m_contacts)` is inside it. On the second tick of a frame the tracker is handed the first tick's contacts too, so `Enter`/`Stay`/`Exit` depend on how many ticks the frame ran. A pair that touches and separates within one frame never reports `Exit`; run the identical two ticks one per frame and it does. |
| **A scene load from inside a tick lands at frame scope** | `SupersonicApp.cpp:1174` | `applyPendingSceneLoad` runs once per frame, so the frame's remaining ticks run against the old scene, and how many that is depends on frame pacing. |
| **The broadphase sorts on a partial key** | `PhysicsSystem.cpp:319` | `std::sort` is not stable, so the order of equal keys — and therefore the impulse solve order for ties — is whatever the standard library does. Same-machine reproducible; not reproducible across two standard library implementations. |
| **`registry.ctx()` is read by every tick and hashed by nothing** | `StateHash.cpp:48` | Open-ended: hashing it wholesale is not obviously right, since much of what lives there is a pointer to an engine subsystem. Named because a tick-0 checkpoint cannot mean what it should until this is decided. |

## What this changes about Phase 2

The design still stands: record the resolved per-tick input, delta-encode it,
check `StateHash` at checkpoints. Two corrections the audit forced, both about
the file rather than the engine:

- **An edge is not a level.** Delta encoding that holds every channel until it
  changes turns a one-tick press into a press held for every tick until the next
  line — which is exactly the property `fb9f86e` was written to add, undone by
  the file format. Levels (`down`, axes, mouse) persist; edges (`press`, `rel`)
  apply to the tick named and are empty otherwise.
- **Decimal floats do not round-trip.** Nothing in this repository writes a
  float that reads back bit-identical — `ComponentCodec` uses the iostream
  default of six significant digits. A recorded axis value written as decimal is
  not the value the tick saw. The tick step has the same problem and is worse,
  because it is wrong for the whole run rather than for one tick.

And one open decision, which is the reason the file format is not written yet:
**UI clicks are tick-readable and are not in the recorded surface.** A script
reads `wasClicked` inside the tick; the flag behind it is written at render
scope from a screen rectangle, so it depends on frame cadence *and* on
resolution. Either the recorded tick input grows a UI channel — carrying the
clicked entity ids, by the same argument that says a mouse delta must be
recorded rather than re-derived — or UI-in-a-tick has to fail loudly. The file
format should not be built around a struct that is about to change shape.

## How this was found

One hundred and eleven agents over five lenses — what diverges, what the initial
state actually is, an adversarial read of the proposed file format, the frame
loop's structure, and the house conventions the new code has to match — with
every finding sent to two independent refuters instructed to default to
*refuted*. Seventy-three findings, forty-six survived.

The refutation pass earned its keep: twenty-seven claims died, and several of
the survivors came back with their mechanism confirmed and their stated
consequence corrected, which is the more useful outcome. Four survivors were
already fixed in the working tree and the agents could not know it.

The thing worth copying is not the scale. It is that the single most valuable
output was a nine-line test nobody had written, and the lens that produced it
was the one asked *is the state at tick 0 actually reproducible?* — a question
about the premise rather than about the code.
