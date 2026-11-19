# Upgrading

What a game has to do when it moves to a newer engine. This file is the ledger of
every change that a game could notice, written from the game's side.

## The policy

On 19 November 2026 the owner decided that **the engine may make breaking changes
whenever they make it better**, and that the cost of upgrading a game is not a
reason to hold a change back. Planning documents older than that (the 1 October
hardening plan is the clearest case) describe the earlier policy, in which anything
a game could notice was held for a decision. For engine changes that policy is
replaced. Those documents stay accurate as history and are not edited.

The price of the freedom is this file. **A breaking change with no entry here is the
one way to get this policy wrong**, because the game finds out by itself, later,
with a bisect.

Determinism stays the default
([docs/planning/2026-11-19-owner-decisions.md](docs/planning/2026-11-19-owner-decisions.md)).
That fixes the *property* (a run reproduces itself, bit for bit, on every machine),
not the *values*: a change that moves a `StateHash` value, a replay file or a golden
is allowed and is recorded here like any other, and a change that makes a run stop
reproducing itself is not.

## What counts, and how it is labelled

Four levels. The silent ones come first because they are the dangerous ones: a
compile error announces itself, and a changed picture does not.

| Level | The game sees | Typical cause |
|---|---|---|
| **Behaviour** (silent) | It builds and loads, and something is different: a picture, a physics result, a hash, a frame time | A new default, a fixed bug a game relied on, a shader change |
| **Data** (silent until loaded) | A saved scene, prefab, `.meta`, manifest, replay or save file loads differently or not at all | A changed key, format version or recording layout |
| **Compile** | The game stops compiling, and the compiler names the site | A renamed or removed type, function or header; a changed signature |
| **Build** | The CMake targets, options, toolchain floor, dependencies or shader build differ | A new required compiler, a renamed target, a new vendored library |

## What an entry carries

```
### <short title>            <commit hash(es)>            <date>
Level:      Behaviour | Data | Compile | Build   (more than one is fine)
Changed:    what is different, in one or two sentences
Affects:    which games, and where - files, symbols, test names - as far as known
To do:      what the game has to change, step by step
Fallout:    which of the game's tests or goldens are expected to move, and why
Migrator:   the tool or loader that does it for you, or "none"
Switch:     how to get the old behaviour back, or "none"
Verified:   which game suites were run against it, or "not run"
```

## Rules

1. **The entry goes in the same commit as the change.** A change without one does
   not merge.
2. **Write what the game will see**, not what the engine did. "Every picture is a
   little darker" is useful. "Replaced the encode" is not.
3. **Say "not run" when it was not run.** An honest unknown beats a missing entry.
   Where a game's own suite can be run against the new engine, run it and record
   the result: the failures it expects belong in the entry, and a failure that is
   not in any entry is a break nobody recorded.
4. **A shim or a migrator is welcome and never required.** It is not a reason to
   hold a change back, and neither is the lack of one.
5. **Newest first.** Entries live under *Unreleased* until a game's pin is bumped
   past them. Then they move under a heading naming the commit that game moved to.

## The games that build against this engine

The repository knows four. The owner confirmed on 19 November 2026 which two are
live: an entry's *Affects* and *Verified* lines are written for those two, and the
other two are not tracked.

| Repository | Status |
|---|---|
| Penumbra-and-the-Castle-of-Shadows-Enhanced | **live**: an entry says what it does to this game |
| Magic-Portals-Enhanced | **live**: an entry says what it does to this game |
| The-Wolf-Brigade | test bed: no entry is written for it, and its suites are not run for the ledger |
| HUSK | test bed: no entry is written for it, and its suites are not run for the ledger |

A test bed still builds against the engine, and a change that breaks one of them may
say so in a line; what it does not get is the work of an entry, a migrator or a
"Verified" run. Either can be promoted back to live by editing this table.

## Unreleased

Nothing is recorded yet. The ledger starts with the first change made under the
policy above; anything earlier is in `git log` and in
[docs/planning/](docs/planning/README.md).
