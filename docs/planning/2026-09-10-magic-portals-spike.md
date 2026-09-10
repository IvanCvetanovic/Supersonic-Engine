# Magic Portals Remake - the spike

Opened 10 September 2026. This is the third migration, after HUSK and the Wolf
Brigade re-sync. Its readiness assessment is in `2026-09-10-migration-readiness.md`
§Magic Portals Remake.

The remake is `Desktop/Magic-Portals-Remake` at `79af319` (5 August 2026), a
Godot 4.6.3 project. Its own harnesses at that commit, run on 10 September:

```
cd Magic-Portals-Remake
tools\godot.bat --headless --path game res://scenes/dev/verify_gameplay.tscn    552 passed, 0 failed
tools\godot.bat --headless --path game res://scenes/dev/verify_core.tscn         57 passed, 0 failed
```

On 10 September the owner confirmed that their rights cover the ports. Two
things in the remake predate that confirmation and are stale:
- the placeholder in `docs/permission.md`;
- the stop rule in its CLAUDE.md.

## A weaker oracle, and the rule for it

Wolf Brigade and HUSK each came with a harness that printed numbers the port
had to reproduce. Magic Portals does not, for two reasons:
- `verify_gameplay`'s 552 assertions are behavioural, and they run over Godot's
  2D physics, which is not being reimplemented. The engine's solver is 3D, so a
  body in it will not land, slide or come to rest on the same frame as in Godot.
- The remake's own data marks every movement and portal value `_guess`.

So this rule was written down before any measurement:

- **Pinned exactly.** Everything the converter and the role table decide:
  - level geometry;
  - entity names and roles;
  - counts, pairings, portal budgets and flags.

  These are decoded bytes. They are pinned to the integer, and coordinates to
  the double the file spells.
- **Asserted as behaviour.** Landing, riding, triggering and going through a
  portal. Each one is a property with a threshold, and the measurement behind
  the threshold is recorded here.
- **Never pinned.** Anything the remake marks `_guess`:
  - the player's speed, acceleration, friction and gravity;
  - the portal's exit offset and re-entry lockout.

  These are carried as data and never as a constant in a test. Pinning a guess
  would make the port bug-compatible with a placeholder, behind a green suite.

## The levels stay outside the repository

The converted levels are the original game's level data, so they are not
committed.

- **Where they are read from:** `SUPERSONIC_MAGICPORTALS_LEVELS`, a CMake cache
  path. It defaults to the remake cloned beside the engine
  (`../Magic-Portals-Remake/out/levels`).
- **When they are missing:** a suite that needs them exits 77, which ctest
  reports as skipped, and prints where it looked.

This is an engineering default, not a legal reading, and the owner can decide
otherwise.

## Step 1 - reading the levels

`games/magicportals/sim/Tscn.{hpp,cpp}` reads the subset of Godot's text-scene
format that the converter writes, and nothing else:
- seven node types;
- four resource types;
- eleven properties plus `metadata/*`;
- six value forms.

All of these were counted across the 128 levels. Anything outside the subset is
an error that names the line.

The alternative was a new emitter in the remake's converter. It was ruled out
because the remake is an oracle here, and its converter is itself under test by
`verify_gameplay`.

**`test_mp_tscn`** uses hand-written scenes, so it runs anywhere. It covers:
- every form once;
- both metadata spellings;
- CRLF line endings;
- 38 refusals, each pinned to its line and its reason.

**`test_mp_levels`** reads the 128 converted levels.
- **level30 in detail:**
  - the inventory;
  - the crates, lifts and buttons;
  - the exit trigger;
  - the polygons to the decimal.
- **Across all 128 levels:**
  - every type, property and metadata spelling, each counted independently with
    grep;
  - `load_steps` agrees with the resource count in every file.

Result on 10 September: `test_mp_tscn` 168 checks, 0 failures; `test_mp_levels`
113 checks, 0 failures. Both toolchains agree: GCC 13.3 on glibc (WSL) and
MSVC 14.50 at `/W4`, which is warning-free after one rename. The two floors are
160 and 110.

The skip path was checked by configuring with a directory that does not exist.
`test_mp_levels` printed the path and exited 77, and ctest reported it as
`Skipped` rather than as a pass.

### A finding for the remake: its hinges never get their limits

`behaviours.gd:65` reads metadata as `float(String(node.get_meta(key)))`. The
converter writes the eight `joint_*` keys bare, for example
`metadata/joint_lower_angle = -1.5`. That is 80 values across the ten hinges,
in level10c, 14b, 18b, 19b, 21b, 27a, 28a, 29a, 30a and 30c. Godot has no
`String` constructor that takes a float.

A probe in a scratch copy of the game called the remake's own
`Behaviours.meta_float`, and printed this on 4.6.3:

```
PROBE quoted -> -1.5
SCRIPT ERROR: Invalid call. Nonexistent 'String' constructor.
   at: meta_float (res://scripts/entities/behaviours.gd:66)
PROBE bare   -> 0.0
```

A bare value comes back as 0.0 with a script error. It is not even the fallback,
which was 7.0 in the probe.

The consequences at `behaviours.gd:292-303`:
- **Limits:** all ten hinges set `joint_enable_limit = 1`, and each has its own
  limits (lower angles from -3.1415 to 0). The flag reads as 0, so no hinge gets
  its limits.
- **Motor:** the motor path fails the same way. No hinge in the data enables
  its motor, so that half costs nothing today.
- **Anchors:** these come out right only because every anchor in the data is 0.

`verify_gameplay` checks only that there are ten hinges (`hinges pivot`). That
is how it stays at 552/0.

The finding is reported here, not fixed, because this work treats the remake as
read-only. The port is not exposed to it: `Value::AsNumber` reads both
spellings. Hinges are out of the spike in any case, since level30 has none.

This also corrects the readiness doc. Its argument for hinges rested partly on
the motor that Godot's `PinJoint2D` drops. No level uses a motor, so the part
that matters is the limits.

## Step 2 - the physics spike

The spike runs on level30, at 50 px per metre. Its thresholds were set before
running:

1. **Out-of-plane drift.** Bodies at rest and sliding on level30's geometry
   over 3,600 ticks.
   - **Pass:** |z| under 1 px (0.02 m) and tilt out of the plane under
     0.01 rad.
   - **Why it matters:** this number decides whether S1 (a per-axis lock) comes
     before the port or during it.
2. **Landing.** A player-sized body dropped at `main_char` (182, 203) comes to
   rest on level geometry.
   - It is asserted as *landed*: grounded, with vertical speed near zero. It is
     never asserted as *didn't fall*, because a didn't-fall check passes with
     every collider stripped.
   - A mutation with the colliders removed must fail it.
3. **Riders and triggers.** The spike measures what happens today; it does not
   fix either gap.
   - A body on a door lift tests F1.
   - The player against the exit door and the buttons tests F2.
4. **Portal velocity.** The traversal's structure is reproduced: velocity is
   rotated by the exit's rotation minus the entry's. The offset and lockout come
   from the remake's data.

Hinges are out of the spike (see above).

**How the spike models the player.** It uses a dynamic body with rotation
frozen. That is a spike instrument, not the port's answer. The remake's player
is a kinematic `CharacterBody2D` that slides, and the engine has no character
controller. The player's real body type is decided after these measurements.

### The tool, and what it was run with

The spike is an executable, `MagicPortalsSpike` in `games/magicportals/spike/`,
built with `-DSUPERSONIC_BUILD_MAGICPORTALS=ON`. It is not a ctest suite: a
failed threshold here is a finding about the engine, and a red test would only
ever say "not built yet". It prints each measurement beside its threshold and a
verdict.

The code it stands on is in `games/magicportals/sim/`:
- **`LevelBuilder`** turns a level into engine bodies: boxes, spheres, hull
  prisms and triggers.
- **`Prism`** writes each polygon as an OBJ prism, the S2 route.
- **`Portal`** is the traversal arithmetic.

Three new suites hold what must not regress:
- **`test_mp_geometry`** (61 checks) covers prisms, hulls and the builder, on
  hand-written shapes.
- **`test_mp_portal`** (26 checks) covers the traversal, against answers worked
  by hand.
- **`test_mp_levels`** (128 checks) now also turns all 13 distinct polygons in
  the 128 levels into prisms. Every one is exactly its own hull: one piece,
  every point a vertex, the right volume, and nothing invented.

The spike runs with these settings:
- **Scale and gravity:** 50 px per metre, and gravity 10 m/s², the original's
  Box2D (`Units.hpp`). Gravity is set through `PhysicsSettings`, over the
  engine's 9.81.
- **Step:** 1/60 s.
- **Depth:** static geometry is 2 m deep and moving bodies 1 m.
- **Materials:** friction 1 and restitution 0, Godot's `PhysicsMaterial`
  defaults, which the remake's crates run with.
- **Player instrument:** a 20 × 44 px capsule, dynamic, with rotation frozen.
- **Push speed:** 1 m/s.

The player's size and the portal numbers are read from the remake's JSON at run
time, never copied into code.

It was run on 10 September with MSVC 14.50 and with GCC 13.3 on glibc. The two
agree to the last printed digit in every row below.

### Result

| | Measurement | Threshold | Verdict |
|---|---|---|---|
| Drift, resting, sleep off | 30.51 px out of the plane, tilt 0.050 rad (`crate_ent_968`); over by tick 7 | < 1 px, < 0.01 rad | **FAIL** |
| Drift, resting, sleep on | 0.35 px, tilt 0.0118 rad; all four bodies asleep | same | **FAIL** |
| Drift, crate pushed across the seam | 2,890 px (sleep on) and 3,865 px (sleep off) along z, tilted 90°: it left the plane and fell out of the level | same | **FAIL** |
| Drift, player walking | 0.0000 px, 0.0000 rad, sleep on and off | same | PASS |
| Landing at `main_char` | lands on tick 1, rests at 202.28 px against 202.00 | by tick 45, within 1 px | PASS |
| Landing, every `StaticBody2D` removed | never lands (1,211 px down by tick 120) | must not land | PASS |
| Rider, slab rising 1 m/s | carried 0.996, sunk 0.44-0.48 px | measured | - |
| Rider, slab sinking 1 m/s | carried 0.895, up to 5.07 px of daylight | measured | - |
| Rider, slab sliding sideways 1 m/s | carried -0.007 | measured | - |
| Exit trigger, player dynamic | reported on 10 of 10 ticks | > 0 | PASS |
| Exit trigger, player kinematic | reported on 0 of 10 ticks | 0 if F2 holds | confirms F2 |
| Portal, solver keeps the written velocity | error 0 m/s after one tick | < 1e-4 m/s | PASS |

The traversal itself, run on `portals.json`'s guessed numbers, sent a traveller
arriving at (100, 0) px/s out at (500, 72) px with (0, 100) px/s. That matches
the answer worked by hand.

### What the numbers decide

**S1 comes before the port.** A resting crate leaves the plane by 30 px in a
minute, and a pushed one leaves it entirely.

Sleep is not the answer:
- **What it does:** it froze the resting bodies at 0.35 px, only because they
  stopped being simulated. The tilt still broke the threshold, from the landing
  at tick 7.
- **Why that isn't enough:** the first push wakes a body, and the pushed-crate
  row shows what follows.

The player row shows the drift comes in through rotation. The same solver, with
rotation frozen, held a capsule at exactly z = 0 for a minute of walking.

So the lock S1 needs covers two things:
- translation along z;
- rotation about x and y.

Rotation about z stays free, because the port's crates are `RigidBody2D` bodies
that turn in the plane. The readiness doc costs S1 at 3-5 days.

**An engine gap was found and fixed along the way: world queries could not see
hull colliders.** The first run's player rested 0.28 px from the right place and
was reported as never having landed. The cause was that `gatherShapes` collected
boxes, capsules, spheres and heightfields, but no hulls. So `Raycast`,
`IsGrounded` and `OverlapSphere` found nothing under a body standing on a hull,
and every platform in these levels is a hull.
- **The fix:** hulls are now queried as the box that holds them, as rotated
  boxes and capsules already were (ARCHITECTURE.md §7l).
- **The test:** `test_physics` gained `testQueriesSeeAHull`.
- **Why the port needed it:** it is the ground check every character needs.

**F1 bites in one direction only.** A kinematic slab that slides sideways leaves
its rider where it was (carried -0.007), which is F1 exactly. That half never
arises here, because every mover in the 128 levels travels vertically:
- **14 lifts:** their `a` and `b` markers differ only in y;
- **15 moving platforms;**
- **46 switched doors.**

This was counted from the converted levels against `entity_roles.json`.

The vertical half does arise:
- **Rising:** carries by displacement, 0.996 of the way.
- **Descending:** a slab going down at 1 m/s left up to 5.07 px of daylight
  under its rider, where free fall alone allows about 2.5 px (v²/2g). With the S1
  locks on (below) it left 10.11 px.

The likely mechanism, not yet confirmed: the solver leaves a kinematic body's
velocity out of the contact (ARCHITECTURE.md §7m), so a descending platform
looks stationary to it. Each contact stops the rider dead, and then it falls
again, a sawtooth.

Riding a lift down is something the player does, so this part of F1 is in scope
for the port. It is a velocity term in the contact solve, not the general case
the readiness doc costed at 2-4 days.

**F2 is real, and it shapes the choice of player body.** A kinematic player
never trips a trigger, so the port has two options:
- a kinematic player, which requires building F2 (0.5-1 day);
- a dynamic player.

The dynamic, rotation-frozen instrument held the plane, landed, and tripped the
exit on every tick. That makes it the cheaper starting point. What it leaves open
is feel: walking by velocity rather than by `move_and_slide`, and holding on to a
platform that is going down.

**Portals need no engine work.** A velocity written at the exit survives the
solver exactly.

### S1 - built, 10 September

`RigidBodyComponent` has `lockPosition` and `lockRotation` (ARCHITECTURE.md
§7e). `LevelBuilder` locks every body it builds: position z, and rotation x and
y (`kPlaneLockPosition/Rotation`). The spike's player and crates get the same.

Rerun on GCC 13.3 and MSVC 14.50, with the same numbers on both:
- **Drift:** every row reads exactly z 0.0000 px and tilt 0.00000 rad, sleep on
  and off, the pushed crate across the seam included. No threshold fails, where
  four did.
- **Landing, triggers and portal:** unchanged.
- **Riders:** sideways carry is 0.000, and a slab descending under its rider
  leaves 10.11 px of daylight (above).
- **`MagicPortalsSpike --unlocked`:** reproduces the first table to the last
  printed digit. So a body with no locks steps exactly as it did before, which
  is also what `test_determinism` keeping its constant says.

Tests:
- **`test_physics`** gains three tests, 263 checks in all:
  - a locked axis is held exactly, not nearly;
  - a joint anchored out of the plane cannot pull a locked body into it;
  - a point joint still holds a plane-locked bob. Its effective mass loses a
    row to the lock, and it used to be dropped whole.
- **`test_serialize`** round-trips both fields.
- **The full suite:** 79 of 79 pass under GCC. Every suite the locks touch
  passes under MSVC.

### Next

1. **F1's descending half.** Give a kinematic body's velocity to the contact
   solve, then rerun the spike's rider rows: a slab going down must take its
   rider with it.
2. **The port proper, on level30.** A dynamic player with rotation frozen,
   driven by the remake's `player.json` (as data), with the portal system on
   `Portal.hpp`.
