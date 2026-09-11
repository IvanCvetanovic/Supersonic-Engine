# Planning records

Working documents that decided what got built, kept because the *reasoning* is
the part that does not survive in a commit message. A commit says what changed.
These say what was considered and rejected, and on what evidence — which is the
thing you need when the question comes back around in six months.

**These are dated records, not live documents.** Nothing here is maintained
against `HEAD`, and some of it is wrong by now on purpose: a plan is a snapshot
of what was believed at the time it was written, and editing one to agree with
what shipped destroys the only useful thing about it.

**The live list is the Roadmap in [README.md](../../README.md#roadmap).** If this
directory and that section disagree, that section is right.

| Document | Date | What it is |
|---|---|---|
| [2026-08-19-findings.md](2026-08-19-findings.md) | 19 Aug 2026 | A consolidated audit of the engine — correctness defects, architectural debt, missing capability, infrastructure — with the six workstreams it was sorted into |
| [2026-08-19-two-games.md](2026-08-19-two-games.md) | 19 Aug 2026 | Whether this engine could host two real games that already exist, read from their source. The reasoning behind Phases 8–14 |
| [2026-08-19-plan.md](2026-08-19-plan.md) | 19 Aug 2026 | The execution plan: Phases 0–14, their ordering constraints, and a status block written as the work ran |
| [2026-08-25-wolf-brigade-port.md](2026-08-25-wolf-brigade-port.md) | 25 Aug 2026 | What it would take to port the Godot game Wolf Brigade to this engine, read from both trees. Phased, costed, and led by the finding that the game has no art yet |
| [2026-08-22-gap-audit.md](2026-08-22-gap-audit.md) | 22 Aug 2026 | A verification pass over the whole engine asking what is left, with every claim tied to a file and line |
| [2026-08-27-engine-roadmap.md](2026-08-27-engine-roadmap.md) | 27 Aug 2026 | What to build next and what not to, ordered by the finding that the determinism work was nearly done. Its Phase 5 table is the list of things deliberately not being built, with a reason per line |
| [2026-08-28-determinism-audit.md](2026-08-28-determinism-audit.md) | 28 Aug 2026 | What the determinism story was actually missing. Falsifies the premise the 27 August roadmap is ordered by, with the test that says so |
| [2026-08-28-competing-with-godot.md](2026-08-28-competing-with-godot.md) | 28 Aug 2026 | What it would take to compete with Godot on versatility and performance, costed in one-person weeks. Led by the finding that the engine's differentiator is unenforced by CI and blind to the only game in the tree |
| [2026-09-10-migration-readiness.md](2026-09-10-migration-readiness.md) | 10 Sep 2026 | Whether Wolf Brigade, HUSK and Magic Portals Remake can start moving onto the engine, read from all four trees - and what a first Linux build on a second machine found: a compiler crash, a fixture that only worked on MSVC, and determinism that does not cross to glibc |
| [2026-09-10-husk-port.md](2026-09-10-husk-port.md) | 10 Sep 2026 | The HUSK simulation port, written as it runs: pinned commit, the Rust oracle and the bit-for-bit standard, the order of work and its one gate, and the traps listed before they bite |
| [2026-09-10-wolf-brigade-resync.md](2026-09-10-wolf-brigade-resync.md) | 10 Sep 2026 | Bringing the Wolf Brigade port from the game's `ebf3d27` to `50741d1`, 36 commits that made it a hero-first game. Written as it runs, on the `wb-resync` branch, against the same harness-number standard as the port |
| [2026-09-10-magic-portals-spike.md](2026-09-10-magic-portals-spike.md) | 10 Sep 2026 | The Magic Portals Remake spike: an oracle that is behavioural rather than numeric, and the rule written down for it; a strict reader for the converted levels, which stay outside the repository; a remake bug that costs all ten hinges their limits; and the measured answers, each built where it was needed: S1 (per-axis locks), F1's descending half (a kinematic body's velocity in its contacts; the sideways half was moot, every mover is vertical), and F2 confirmed, so the player is dynamic. Found on the way: queries that could not see hull colliders, and a narrowphase face bias that gave separated shapes' ties to edge axes |
| [2026-09-10-magic-portals-port.md](2026-09-10-magic-portals-port.md) | 10 Sep 2026 | The Magic Portals port, on level30: five acceptance items (walk and land, buttons hold doors, crates on buttons, crystals and the exit, portals), what is out of scope, and the decisions taken before any code - a material for colliders with no rigid body, the oracle's masses, the port's own trigger queries, movers that write their velocity - plus the original's materials, which the remake drops. Then the steps as built: movers and a door that carries its rider, and the player, at the remake's 980 px/s² world gravity and gripping nothing, which was measured at the seam in level30's floor. Then buttons and their doors: only dynamic bodies press, as probed in the remake, through the port's own exact trigger test. Then crystals and the exit, which only the player triggers, firing on entry, with the remake's UNVERIFIED exit gate tested both ways |
| [2026-09-10-cross-platform-determinism.md](2026-09-10-cross-platform-determinism.md) | 10 Sep 2026 | Making the determinism claim hold across C runtimes. It measures which libm call parts Windows from Linux first (an `atan2f` on tick 29, where glibc is the one an ulp off), then replaces libm on the simulation path with functions IEEE pins down everywhere |

## What the two-games document changed

The most useful thing in this directory, and the least obvious. Two real games
were read — a 2D lane RTS in Godot and a deterministic 3D RTS in Rust/Bevy — and
the question asked was whether Supersonic could host them.

The answer reordered the roadmap. **None of the engine's famous limitations
blocked either game**: not the eight-light cap, not box-box-as-AABB, not the
missing CCD, not the missing navmesh. What blocked them was that the engine was
an *application with an editor fused into it*, and neither game is a scene —
both are programs. That is why Phase 8 is a layer seam rather than a rendering
feature, and why the work that looked most urgent from inside the engine turned
out not to be.

It also recorded a tension rather than silently acting on it: two items in the
gameplay workstream are actively *harmful* to a deterministic simulation, since
a script pushing a body the sim also owns is exactly the coupling that desyncs a
fixed tick.

## A caveat about the roadmap's opening claim

`2026-08-27-engine-roadmap.md` is ordered by a section called *The finding that
should decide the order*, which says the determinism work is "nearly finished"
and represents "the hard 80%". That is wrong, and the 28 August audit is the
record of how wrong.

The three components it points at are all well built. The mistake was inferring
from three good components that the property they exist to provide had been
achieved, without running the experiment. The experiment — load the same scene
twice and hash it — had never been written, and failed: the hash was seeded on
an EnTT handle including its recycle count, so every determinism claim the
engine made was scoped to one process that had loaded exactly one scene.

The roadmap's *ordering* survives this. Clock, then replay, then the small gaps
is still right. Its estimate does not.

## A caveat about the status block

`2026-08-19-plan.md` ends with a status block written while the work was
running, and it was written from intent rather than from verification. At least
one entry is wrong: it records Phase 5 as complete, and Phase 5's `Environment`
item — an irradiance cubemap and a prefiltered specular map — never shipped.
`shader.frag` still lights the ambient term from an analytic hemisphere.

That is worth knowing before trusting any other line in it, and it is why
the 22 August document ties every claim to a file and a line number instead.
