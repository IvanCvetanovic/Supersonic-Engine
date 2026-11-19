# Owner decisions, 19 November 2026

A dated record of what the owner decided in one conversation, written down so that
a session which has lost that conversation does not have to guess. It decides
nothing by itself: the reasoning behind each line is in the sessions that produced
it, and the detailed plan these decisions feed does not exist yet.

## 1. What the engine is for

A **universal engine**, measured by capability: what a developer can make with it,
and how. The comparison set is Godot, Unity and Unreal, in that order of
reachability: Godot's feature set first, then Unity's, then chosen Unreal pillars.
"At least in performance" meant *what the engine has to offer*, not benchmark
speed.

The owner's own games, which set the priority weights: **2D platformers, 3D
strategy games and 3D shooters.** They pull on different parts of the engine
(tilemaps and a tight 2D character controller; many units, pathfinding and lockstep
networking; a first-person controller, weapons and predicted networking), and the
capability matrix scores every feature for each of the three.

## 2. Breaking changes are allowed

The engine may break what a game sees whenever that makes it better, and the cost of
upgrading a game is not a reason to hold a change back. What replaces the old rule
is the record in [UPGRADING.md](../../UPGRADING.md). Dated planning documents before
this one describe the earlier policy and are history.

## 3. Determinism stays the default

The owner left the call to the engineering judgement ("keep the determinism if you
deem it necessary"). It is kept, for three reasons: it is already built and tested,
as far as is known none of Godot, Unity or Unreal offers it natively (not checked),
and two of the owner's three genres use it directly (lockstep networking and replays
for strategy, rollback and tests for platformers).

What the word commits the engine to, so that it is not stretched or abandoned by
accident:

- **The property** is that the same inputs give the same result, bit for bit, on
  every machine. No simulation result may depend on thread timing, the wall clock,
  uninitialised memory or the platform's maths library.
- **The values are not frozen.** A change may alter what a run produces (a
  `StateHash` value, a replay file, a golden). That is a breaking change and gets an
  [UPGRADING.md](../../UPGRADING.md) entry. What it may not do is make a run stop
  reproducing itself. `tools/verify-replay.sh` and `test_determinism` are the gate.
- **Speed is pursued inside it first.** Parallel simulation with fixed partitions
  and ordered reductions is a research item, and the 28 August document's claim that
  threading and SIMD are permanently off the table is treated as too strong until
  it is tested.
- **If that fails, a separate opt-in Fast mode, per game.** A physics-heavy shooter
  is the likely user. Rendering is outside the hash either way.

## 4. A small speed suite stays, as a guardrail

Not the goal: a smoke alarm that says when a change made the engine slower, while
hundreds of features are being added. `tools/bench` already holds `physics_bench`
and `dedupe_bench`, outside ctest.

- **Counts are asserted, milliseconds are recorded.** Draw calls submitted, entities
  walked and allocations per frame can be held by a test on any machine; a
  millisecond threshold is flaky and gets deleted (the 28 August document's advice).
- **A few fixed scenes**, for example thousands of cubes, a thousand physics bodies,
  a few hundred animated characters, and the lit 2D path.
- **CPU timings can run in the cloud; GPU timings need the owner's PC**, through the
  local agent, and baselines are per machine.
- **Not started.** It is the first wave of the detailed plan, together with GPU
  timers.

## 5. What happens next

1. The capability-matrix workflow (run `wf_3a1aa15b-4ff`, 19 domains, each surveyed
   against the code and the competitors, then independently re-checked) produces the
   matrix and a gap list weighted for the three genres.
2. The detailed plan is written from that, with costs, dependencies, waves, and a
   statement per item of what the cloud can verify and what needs the owner's PC,
   device or eyes. The frontier-rendering roadmap
   (`docs/planning` copy to be added with the plan) is its rendering lane.

## 6. Still open

- Which two of the four games that build against the engine are live (the status
  column in [UPGRADING.md](../../UPGRADING.md)).
- Which GPU does the owner's verification (the 28 August document names an RTX 3060
  Laptop, the 11 November device run an integrated Radeon 780M). It decides whether
  ray tracing and the GPU-side speed suite can be run locally.
- Where committed shader blobs are built: the owner's PC through the local agent each
  time, or a dispatched CI job that uploads them. The policy above allows shader
  changes; it does not remove the need for the pinned compiler.
