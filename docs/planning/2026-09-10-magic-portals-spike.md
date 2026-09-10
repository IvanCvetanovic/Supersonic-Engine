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

## Step 2 - the physics spike (next)

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
