# What the determinism story was actually missing

> **Audit, 28 August 2026.** Written against Supersonic at `bf491fd` and
> updated through `04ea68e` as the fixes landed the same day, after
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

### Three channels of input still scoped to the frame

This turned out to be the shape of the whole session, and it is worth stating as
a pattern rather than as three bugs, because each one was found only by going
and looking for the next.

Gameplay moved onto the fixed tick in `fb9f86e`. Everything a tick *reads* had
to move with it, and only the keypress did. The rest kept answering the
question a frame asks, which is wrong in both directions at once: a frame that
runs no tick loses the event — at 20 Hz on a 144 Hz display that is six frames
out of seven — and a frame that runs three hands it to all three.

- **`wasReleased`** was left on the frame edge while `wasPressed` was latched,
  under a comment saying both had moved. Worse than either being wrong alone: a
  script that charges on the press and fires on the release saw the press
  exactly once and the release never or three times, so the shot did not come
  out or came out in triplicate. (`bf491fd`)
- **Contacts** were accumulated per frame while `ContactTracker::Update` ran per
  tick, so `Enter`/`Stay`/`Exit` depended on how many ticks the frame ran. A
  pair that touched and separated inside one frame never reported `Exit`.
  (`76f67a0`)
- **UI clicks** were computed once per frame by the UI pass and read inside the
  tick through `ctx->ui->wasClicked`. One press of a Buy button did its action
  three times on a machine that was a frame behind. (`04ea68e`)

Three ABI bumps — 11, 12 and 13 — each leaving every struct byte-identical and
changing what a call answers. That is the one kind of change a plugin cannot
detect for itself, and the version list is the only place it can be told.

**The lesson worth keeping is the search, not the fixes.** "Which channel is
next" is a question with a finite answer: enumerate what a tick can read, and
check each one for whether the value it returns was computed per frame. That
enumeration is also exactly the list a recording has to serialise, which is why
doing it was a prerequisite rather than a detour.

### Input edges queued up while nothing was ticking

The latch that hands a press to exactly one tick is only correct while a tick is
coming to collect it. In the editor, while paused, and during a time-travel
scrub, none is — and nothing expired them, so the first tick after Play was
handed every key touched since the last one. A character jumping and firing on
frame one of Play from input given a minute earlier while the scene was being
authored. The same was true of clicks once they were latched, and both are now
dropped on every frame that runs no tick. (`76f67a0`, `04ea68e`)

### The clock never started over

`registry.clear()` leaves the context alone and nothing but the loop ever wrote
the tick counter, so it counted every step the *process* had run — across a
scene load, across Stop and Play, across loading a different level.

That reads like untidiness. It is not, and the reason is invisible from the
field: `SecondsF()` narrows `tick * fixedDelta` to a float, a script's elapsed
time is derived from it, and the narrowing happens *before* anything subtracts.
So the value a script integrates sits on a lattice whose spacing grows with the
tick count, and the same logical tick of the same scene produced a different
number depending on what the process had done first. Two runs of one scene
agreed only if they were the first thing their process did. (`76f2551`)

The rate assignment moved out of `if (root.Has("Simulation"))` at the same time:
every scene written before the tick could be authored has no such block, so
those scenes silently inherited the previous scene's rate.

## Found, tied to a line, not yet fixed

Listed so they are not rediscovered as mysteries. Each is a real defect with a
file and a line; none is speculative.

| | Where | What |
|---|---|---|
| **A scene load from inside a tick lands at frame scope** | `SupersonicApp.cpp:1174` | `applyPendingSceneLoad` runs once per frame, so the frame's remaining ticks run against the old scene, and how many that is depends on frame pacing. Scoping a recording to a single scene avoids it for v1, and the header names one scene, so say that is the limit. |
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

The open decision that was blocking the format has been made. **UI clicks are
tick-readable, so they are now latched per tick** the way keypresses already
were, which both fixes the live triple-fire bug and makes them recordable: a
tick's clicks are a set of entity ids, and entity ids are stable across loads
now that the hash and the load path agree about them. A recorded `TickInput`
therefore carries the clicked ids, by the same argument that says a mouse delta
must be recorded rather than re-derived — the value was computed at frame scope
and cannot be reconstructed from a different frame cadence.

What remains for Phase 2 is the file itself: `Input::TickInput`,
`CaptureTickInput` and `BeginReplayedTick`/`EndReplayedTick` are in
`Input.hpp`/`.cpp` and compile, with no tests and no caller. The writer, the
reader, the `--record` and `--replay` flags and the verification path are not
written.

One trap to record before it is: `CaptureTickInput` reads through the same
queries `BeginReplayedTick` diverts, so capturing during a replay returns the
replayed values. That is correct and useful, and it means a verifier comparing
re-captured input against the file would pass trivially. **The verification path
must compare `StateHash` and nothing else.**

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
