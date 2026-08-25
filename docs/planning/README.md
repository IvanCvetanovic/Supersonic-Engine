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

## A caveat about the status block

`2026-08-19-plan.md` ends with a status block written while the work was
running, and it was written from intent rather than from verification. At least
one entry is wrong: it records Phase 5 as complete, and Phase 5's `Environment`
item — an irradiance cubemap and a prefiltered specular map — never shipped.
`shader.frag` still lights the ambient term from an analytic hemisphere.

That is worth knowing before trusting any other line in it, and it is why
the 22 August document ties every claim to a file and a line number instead.
