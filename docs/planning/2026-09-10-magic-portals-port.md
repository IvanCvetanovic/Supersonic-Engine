# Magic Portals Remake - the port, on level30

10 September 2026, branch `mp-spike`. This follows the spike
(`2026-09-10-magic-portals-spike.md`), which built S1 (per-axis locks) and F1 (a
kinematic body's velocity in its contacts), and confirmed F2, which is why the
player is dynamic. The spike's oracle rule stands:
- converter and role-table facts are pinned exactly;
- physics behaviour is judged by thresholds;
- a value the remake marks `_guess` is carried as data and never pinned.

## What "ported" means here

Five acceptance items, all headless, all on level30. The level is converted, and
it is read from outside the repository.

1. **The player walks and lands.**
   - **Body:** a dynamic capsule, rotation frozen and locked to the plane.
   - **Driven by:** `player.json`'s movement block, as data. Walk 160 px/s,
     acceleration 1400, friction 1800, air control 0.65; all four are `_guess`,
     carried and never pinned.
   - **Must:** land on converted geometry, the remake's own `verify_gameplay`
     assertion, and walk the floor without catching on the seams between pieces.
2. **Buttons hold doors open.**
   - **Pressing:** a `button` is pressed while any body rests on it. It counts
     touching bodies; it is not a latch.
   - **Opening:** it opens every `door_lift` with the same `switchIdx`. The door
     rises 126 px over its `stride` milliseconds, and closes the same way.
3. **Crates push and rest on buttons.** The player can push `crate.ent` onto a
   button, and its door opens. A crate standing on a rising door rides it.
4. **Crystals and the exit report.**
   - **Crystals:** collected when the player overlaps one.
   - **Exit:** reports being reached, gated on `portals.json`'s
     `level.exit_requires_all_crystals` (false by default).
5. **Portals.**
   - **Placement:** at a world point, refused inside a no-portal zone.
   - **Budget:** at most two live, from `properties`' `max_portals` or else
     `portals.json`'s default of 2. At the cap the oldest is recycled.
   - **Travel:** the pair is consumed when something goes through, and the
     transit is `Portal.hpp`. The player and teleportable bodies travel; the
     inline `crate` (`teleportable` 0) does not.

**Out of scope, on purpose:**
- **Presentation:** drawing beyond boxes (a view comes after the five items),
  camera follow and drag, audio, HUD.
- **Game flow:** golden score, retry and level flow, menus.
- **Other content:** every level but level30; and hazards, keys, minions,
  hinges and bosses, none of which level30 has.

## Decisions, taken before any code

**Statics get a material, through a new engine component.**
- **The problem:** the engine gives a collider with no rigid body `kFriction`
  (0.4) and `kRestitution` (0.3), and has no way to say otherwise. So every
  level static returned 0.3 of every landing, because restitution combines as the
  larger value, and gripped at √(0.4·μ).
- **The alternatives, and why not:**
  - **Kinematic statics:** nothing resting on a kinematic body may ever sleep,
    under the wake rule.
  - **New defaults:** that changes every scene, `test_determinism`'s constant
    with them.
  - **A zero-mass rigid body:** the integrator still moves one.
- **The component:** `PhysicsMaterialComponent` is friction and restitution for
  a collider that has no rigid body. A rigid body carries its own material and
  wins, and the constants stay the fallback. Its defaults equal the constants,
  so absent or left at defaults it changes nothing.
- **Values:** the oracle's. The remake converts no materials, so Godot's
  defaults apply to everything, friction 1 and bounce 0. Statics get 1.0 and 0,
  and bodies keep `LevelBuilder`'s 1.0 and 0.

**Crates weigh the oracle's 1 kg.**
- **Crates:** the converted scene carries no mass, so the remake's crates are
  Godot's default 1 kg, and the port keeps `RigidBodyComponent`'s default of
  1.0.
- **The player:** the remake's player is a `CharacterBody2D` and has no mass.
  The port's dynamic capsule is also 1.0, and its controller writes its velocity
  every tick, the way `move_toward` does.

Whether pushing works is judged by acceptance item 3, not by a mass ratio anyone
measured.

**Triggers are the port's own overlap queries, not the `ContactTracker`.**
- **The tracker's problems:** it is updated after the layers run, so a layer
  reads the previous tick's triggers. Its pairs also carry across unless
  `Clear()` is called.
- **What the port does instead:** it asks, in its own tick, which bodies
  overlap each trigger box, using `trigger_size` and `trigger_offset` from the
  converted level. That has no lag and carries nothing across a retry.

**Movers write their velocity along with their transform, through one helper.**
F1 (`89f48a8`) made the contact solve read a kinematic body's velocity. A mover
that moves its transform and leaves its velocity at zero carries nothing, and
does so silently. So a door is moved by one function that writes both, and a
test holds a crate on a rising `door_lift`.

## Risk, checked first

**Seams.** A dynamic capsule steered by velocity is not `move_and_slide`. The
risk is catching on the seams between adjacent pieces of level geometry, and
level30 has walls the player pushes crates against. The first player test walks
the floor end to end and asserts progress. If the capsule catches, the answer is
engine work on a character controller, and that is worth knowing before portals
are built on top of it.

## A finding for the remake, not read by the port

The original's entity files carry materials the converter drops. The values are
the `<Entity>` attributes in `reference/extracted/assets/entities/*.ent`:

| Entity | Friction | Restitution | Density |
|---|---|---|---|
| `platform.ent`, `single_block_plat.ent`, `double_block_plat.ent` | 0.2 | 0 | static |
| `block00.ent`, `collision_768x128.ent`, `collision_128x768.ent` | 0.4 | 0 | static |
| `crate.ent`, `crate_small.ent` | 0.96 | 0.2 | 0.4 |

Box2D mixes these the way the engine does, with geometric-mean friction and the
larger restitution. So in the original a crate bounces a little and slides on a
platform, and in the remake it does neither. The port follows the remake, and
nothing in it reads these files.

## Steps

1. `PhysicsMaterialComponent` in the engine; `LevelBuilder` gives it to statics.
2. The mover helper and the door, with the rider test on a `door_lift`.
3. The player controller, with the landing and seam tests.
4. Buttons, doors and crates on level30.
5. Crystals and the exit.
6. Portals.
7. A layer that draws it in boxes, behind `SUPERSONIC_BUILD_MAGICPORTALS`.

## Step 1 - statics get a material (built)

**The engine.** `PhysicsMaterialComponent` (ARCHITECTURE.md, beside the table
of how materials combine) carries a collider's friction and restitution when it
has no rigid body.
- **Plumbing:** it has a codec and an inspector section, and the section says
  when the component is being ignored.
- **Contacts:** the solver reads it at the one place a contact takes its
  material.
- **Tests:**
  - a dead box on a floor with no bounce stays down (under 0.05 m/s), while
    the fallback floor throws it back up above 0.5 m/s;
  - a flat box slid at 3 m/s stops sooner on a floor with friction 1 than on
    the fallback, and near v²/2μg's 0.459 m;
  - the component round-trips through `test_serialize`.
- **Determinism:** `test_determinism` keeps its constant.

**Magic Portals.** `LevelBuilder` gives every static 1.0 and 0, from
`kStaticFriction` and `kStaticRestitution`, and `test_mp_geometry` checks them.

**The spike, rerun.**
- **Locked:** the table is unchanged, with 0 thresholds failed.
- **`--unlocked`:** the resting crates now hold, at 0.0003 px, sleep on and
  off. Awake, they had drifted 22.98 px, because the fallback's bounce off every
  static kept them moving.
- **The pushed crate:** it still leaves the plane, at 3,052 px (sleep on) and
  3,790 px (sleep off), over the threshold by tick 10. So S1 is still what holds
  a pushed crate in its plane.

**Verified:**
- **GCC 13.3:** the full suite passes, 79 of 79.
- **MSVC 14.50:** the full build is clean, editor included, and the touched
  suites pass. The first MSVC build caught two things GCC did not: a missing
  using-declaration, and the new material pointers shadowing F1's `surfaceA`
  and `surfaceB` flags (C4456). They are now `materialA` and `materialB`.
