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

## Step 2 - movers (built)

**The role table.** `Roles` reads the remake's `entity_roles.json` at run time,
from `SUPERSONIC_MAGICPORTALS_DATA`. That file is the remake's own design table
of what each of the original's entity names does. `Roles` refuses a name listed
under two different roles, where the remake lets the last one read win.

**Movers in the builder.** Given the table, `LevelBuilder` builds the moving
roles as kinematic bodies with the oracle's material, as the remake makes them
`AnimatableBody2D`. The moving roles are `moving_platform`, `lift` and
`switched_door`, the remake's `MOVING_ROLES`.
- **Without the table,** as the spike builds, nothing changes, and the spike's
  tables came out the same to the digit.
- **`BuildEntity`** builds one piece of a level on its own.

**Moving them.**
- **`Mover::MoveKinematic`** is the only thing that moves a kinematic body, and
  it writes the velocity along with the transform.
- **`Mover::Door`** is `behaviours.gd`'s gated door: up 126 px over its
  `stride`, back the same way. Reversed mid-travel, it resumes from where it is.

**`test_mp_play`**, 35 checks. It skips without the converted levels and the
remake's data. It pins:
- **level30's roles:** three buttons and the three `door_lift`s they open, five
  crystals, the exit, the spawn and the bounds; no static portals and no
  `properties` entity.
- **its movers:** the three `door_lift`s, built kinematic, which leaves 12
  statics; and their strides (1000, 3000 and 3000 ms) and poses.
- **a rider:** a crate rides `door_lift_976` up, flies on when the door stops,
  comes to rest on it, and rides it back down.

**A finding about the door.** The door starts and stops dead, because
`behaviours.gd` lerps it at one speed (126 px/s for `door_lift_976`).
- **What happens:** when the door stops, a crate riding it is still rising at
  126 px/s. It flies on by v²/2g, 15.9 px at the spike's 10 m/s², then lands
  back on the door.
- **What the first test got wrong:** it expected the crate to stop with the
  door, and read the flight as a 13 px gap.
- **Why the flight counts:** it is the evidence the crate was carried. A crate
  that was only shoved along has no speed of its own to fly with. So the test
  now checks the flight, to within 3 px of v²/2g.
- **The remake and the original:** the remake's `AnimatableBody2D` stops the
  same way, so its crates should fly too. Whether the original's doors ease in
  and out is not known.

**Verified.** GCC 13.3 and MSVC 14.50 alike give 35 checks and 0 failures, and
the other Magic Portals suites are unchanged. The first build refused a
temporary string handed to `Json::Parser`. Its constructor from an rvalue is
deleted, because the parser keeps a reference to what it reads.

## Step 3 - the player: decisions, before the code

**World gravity is the remake's: 980 px/s², or 19.6 m/s².**
- **Where 980 comes from:** the remake's `project.godot` sets no gravity, so its
  crates fall at Godot 4's default, `physics/2d/default_gravity`.
- **Why the spike differed:** it ran at the original's decoded Box2D constant,
  10 m/s², because its job was to measure the solver against the original. The
  port's job is to play like the remake, the same argument that gave statics
  Godot's material.
- **Two constants, two jobs.** `Units::kGravity` stays the decoded constant it
  documents, and the spike keeps using it. The port's world gravity is its own
  constant, `Units::kRemakeWorldGravityPx`.
- **What moves because of it.** `test_mp_play` switches to the port's gravity,
  so the rider's flight after the door stops becomes v²/2g at 980 px/s²:
  8.1 px, where it was 15.9 px at the spike's gravity. The test still derives
  the number from whatever gravity it sets.

**The player falls at its own gravity.**
- **The rate:** `player.json`'s 1200 px/s², marked `_guess` and carried as data,
  capped at its 900 px/s. That is what the remake's player does, independently
  of the world's gravity.
- **Every tick, not only in the air.** `move_and_slide` snaps a character to
  the floor, and a dynamic body has no such snap. A body that stopped applying
  gravity when grounded would hover off a lift going down, which is the F1 bug
  in another form. On the floor, the contact takes the gravity back out.

**How hard the player grips was measured, not argued: it grips nothing.** A
`CharacterBody2D` feels no friction, which argues for 0. But acceptance item 3
is pushing a crate, and the seam is a 3.2 px notch that a gripping capsule and
a sliding one may cross differently. So the first player test walked the seam
at both, traced tick by tick.
- **At friction 1 the player fights its own steering.** The controller writes
  velocity each tick, and the contact then takes most of it back, with
  friction up to the player's weight. On flat ground 1400 px/s² of
  acceleration comes out at about 200 px/s², 3.3 px/s a tick. In the notch, the
  far chamfer holds the player at vx 0 for 26 ticks before it crawls out.
- **At friction 0 the notch costs one tick.** The player drops to 112 px/s for
  a tick, hops about a pixel, and is back at 160 px/s three ticks later.
  Sliding up a 26.6° chamfer keeps cos² of the speed, 128 px/s, so Godot's own
  `move_and_slide` would dip nearly as far. The rest is the first chamfer's
  contact, met at speed.
- **So `Player::kFriction` is 0.** Whether a frictionless player still pushes a
  crate hard enough is step 4's to measure.

**The seam test checks two things.** The walk is right from the spawn (x 182),
across the notch at x 256 where `platform_ent_895` meets `platform_ent_785`, to
x 300.
- **The player gets there.**
- **It never catches.** No tick after it reaches walking speed may drop below
  half of it while the button is held. A capsule that catches for a fifth of a
  second and then pops free still gets there, and is still broken. The line is
  at half because the chamfer's geometry alone takes a fifth, and a catch
  reads 0.

**The first draft walked into the crate.** It walked to x 320, taken to be
short of `crate_969`. But the crate is 58 px wide at x 352, so its face is at
x 323, and the capsule's face meets it with the capsule's centre at x 313. The
lowest speed that draft reported, 59.6 px/s at friction 0, was the player
starting to push the crate, not the seam.

**Built and verified.** The player is `games/magicportals/sim/Player`, with
two new checks in `test_mp_play`.
- **Landing:** the player comes to rest on `platform_ent_895` within
  `verify_gameplay`'s 45 ticks, and does not land with every static removed.
- **The seam:** the player walks across without catching.

GCC 13.3 and MSVC 14.50 agree to the digit: 42 checks and 0 failures.
- **Friction 0:** the player reaches x 300 at 0.800 s, with its slowest held
  tick at 112.0 px/s.
- **Friction 1:** it takes 1.717 s, and stops dead in the notch.

The door rider's flight is now checked at the new gravity, and the other Magic
Portals suites are unchanged.

**Item 2 is not done until the full level agrees.** The rider test built
`door_lift_976` on its own, because in the level it rises into the space under
977 and 978. Before buttons count as working, step 4 has to push a crate onto
`button_975` (idx 0) and open `door_lift_976` (switchIdx 0) in the whole level,
with all three doors present. Step 4 did this.

## Step 4 - buttons and doors: decisions, before the code

**Only dynamic bodies press a button. That was measured in the remake.**
- **What looked likely, and was wrong.** Each of level30's three button
  triggers overlaps the static beneath it by 4 px, and a Godot `Area2D` can
  report static bodies. So the floor looked as if it would hold every door open
  from the first frame.
- **The probe.** A scene in a scratch copy of the remake ran the remake's own
  `LevelRuntime` on level30.
  - **The triggers:** 16×16 at their nodes.
  - **The geometry:** a shape query with each trigger's own shape finds the
    static beneath it.
  - **What the areas report:** nothing. In 120 frames no channel changed and no
    door moved.
- **The control, so the empty answer means something.** A 30×30 rigid body
  dropped at (94, 190) did register. Channel 0 was pressed 8 frames later, and
  `door_lift_976` rose its 126 px to y 66 while 977 and 978 stayed shut.
- **The original agrees.** Box2D pairs a sensor only with a dynamic body.
- **So a button counts rigid bodies that are not kinematic.** A door moving
  over a button would not press it either. level30 has no such case to measure.

**The trigger is the remake's 16 px fallback, not the converter's 26×16 area.**
- **Why the fallback:** `_attach_trigger` gives a role entity with no
  `trigger_size` a 16 px box, and level30's buttons have no `trigger_size`.
- **What the remake ignores:** the switch listens only to that `Trigger`. It
  never reads the converter's `Body` area, which is 26×16, the original's own
  fixture.
- **Treatment:** ported the way the remake does it, and recorded beside the
  materials as a remake finding.

**The overlap test is the port's own, not a new engine query.**
- **What the engine offers:** `OverlapSphere` and no box query. Its queries test
  anything but a sphere by its bounding box.
- **Why not add `OverlapBox`:** it would be no more exact than a test in the
  port, and it would still count a crate tipped on a corner as covering its
  whole bounding box.
- **What was built instead:** `sim/Trigger` is exact in the plane for a turned
  box, a sphere and a capsule. Crystals and the exit will use it too, so the
  engine needs no change.

**A change on a channel sets its doors, and the last change wins,** as with the
remake's `EventBus`. With two buttons on one channel, the door follows whichever
changed last, not whichever is held. level30 has one button per channel.

**Built and verified.** The code is `sim/Trigger` and `sim/Puzzle`. Five new
checks in `test_mp_play` run in the whole level with all three doors present:
- **Wiring:** the three buttons' channels and 16 px boxes, and the doors'
  `switchIdx`.
- **The floor presses nothing.** The static top under each trigger is checked
  to reach into it first, so the empty result is the rule's doing, not a gap.
- **A dropped crate.** The small crate, dropped where the probe dropped its
  control, presses channel 0 inside the remake's 8 ± 3 ticks. It holds every
  tick after, `door_lift_976` opens while 977 and 978 stay shut, and the plate
  lets go the tick after the crate is removed. Then the door closes.
- **The player presses and lets go.** It presses, and lets go, with its centre
  at x 112, where its capsule's edge meets the trigger's.
- **A pushed crate.** The frictionless player pushes level30's `crate.ent` onto
  `button_975` in 0.233 s, moving it 11.3 px. The crate then holds the plate on
  its own, and the door fully opens.

GCC 13.3 and MSVC 14.50 agree to the digit: 75 checks and 0 failures. The other
Magic Portals suites are unchanged.

**What step 4 did not do.** No crate rides a rising door in the full level, and
no crate is sent to the raised buttons 1 and 2. Both need portals (step 6).

## Step 5 - crystals and the exit: decisions, before the code

**Only the player triggers them,** as in the remake (`level_runtime.gd:270-274`).
There, crates are pushed through crystals and must not collect them. level30
tests this without a rig, because `crate_ent_968` stands on `crystal_ent_999`
and `crystal_ent_1001` from the first frame.

**Both fire on entry, not while the player is inside.** In the remake they are
an `Area2D`'s `body_entered`.
- **The exit:** it reports each entry. `main.gd` completes the level on an
  entry only if `exit_is_open()`.
- **With the exit gated:** a player already standing in the exit when the last
  crystal goes does not finish, and has to leave and come back in. A port that
  checked "is inside now" would pass every other test and still get this wrong,
  so one test is exactly this case.
- **A crystal:** the edge does not show, because the crystal is gone after the
  first time.

**The exit's switch is UNVERIFIED, not `_guess`, and is tested both ways.**
- **The rule it doesn't fit:** the acceptance rule carries `_guess` numbers as
  data. `level.exit_requires_all_crystals` is a different kind of unknown: no
  one knows whether the original has the behaviour at all.
- **How it is handled:** carried the same way, with a test for each setting.
  That gives the gate test something to check, rather than only a `false`.
- **Strictness:** a missing key is an error, where the remake falls back to
  `false`.

**Within a tick, crystals go before the exit.** The remake does not define the
order of two triggers in one frame.

**Timed crystals are refused, not ported.** The inline `crystal` with a `time`
expires in the remake. level30 has none.

**Built and verified.** The code is `sim/Goals`. Five new checks in
`test_mp_play`:
- **Where they are:** the five crystals and the exit sit where level30 puts
  them. The crystals are 28×24 boxes, and the exit is 8×24 at (712, 125).
- **Only the player:** `crate_ent_968` covers crystals 999 and 1001 for two
  seconds and collects neither.
- **Collecting:** the player, put on `platform_ent_966`, collects
  `crystal_ent_998` on the first tick, and nothing else.
- **The exit, switch off:** walking right, the player enters with its centre
  just past x 698, where its capsule's edge meets the trigger's, and the level
  completes with 4 crystals out.
- **The exit, switch on:** standing in the exit with one crystal out, the entry
  reports but does not complete. Taking the last crystal while the player
  stands there changes nothing. Out and back in, the level completes.

No test stands the player near crystal 1031. From the top of `block00_ent_967`,
the capsule's cap would reach exactly to that crystal's lower edge, so the
result would come down to float rounding.

GCC 13.3 and MSVC 14.50 agree to the digit: 98 checks and 0 failures. The other
Magic Portals suites are unchanged.

**What step 5 did not do.** The player is put near the crystals and the exit by
the tests. Reaching them by play needs portals (step 6), as do the crate
covering 999 and 1001 and the crystals above the floor.

## Step 6 - portals: decisions, before the code

**Entries are per portal and per body.** The exit's entry was one flag,
because only the player uses it. A portal can hold the player and several
crates at once, and a pair has two portals.
- **How it works:** each placed portal keeps the travellers inside it as of the
  last tick, and a traveller appearing in that set is an entry.
- **A new portal starts empty,** so a crate that a portal is placed over comes
  in on the next tick. That matches Godot, which reports a body already inside
  a new area as entering it.

**Placed portals are circles, so `Trigger` gained a circle.** It is as exact as
the box: a turned box is met in its own frame. A rig in `test_mp_play` checks a
box turned 45°, where a test against its bounding box would be wrong. level30's
crates stand square, so the level itself would never show the difference.

**level30 does not exercise the turn through `rotate_to_exit`.** Placed portals
are never rotated, so on level30 the exit velocity is the entry velocity,
scaled. That is indistinguishable from `preserve`. `test_mp_portal` covers the
turn on its own.

**portals.json mixes its markers within a block.** `placement` is marked
`_guess`, but its own note says `default_max_portals` was confirmed by the
owner.
- **Pinned:** that one value, with the code's own cap of a pair. level30's
  budget of 2 is pinned.
- **Carried as data:** `collision_radius_px` beside it, and every `transit`
  field.
- **Run both ways:** `on_cap_recycle_oldest`, a `_guess` about behaviour, like
  the exit's switch.

**The remake leaves the portal cooldowns unmodelled.**
`first_portal_min_time_s` and `next_portal_min_time_s` are in portals.json, and
`portal_system.gd` never reads them. The port does not either. That is a remake
finding, of a kind with the ignored 26×16 button area.

**Static portals and moving no-portal zones are refused, not ported.** level30
has neither. The no-portal zone rule, a tap within the radius times the zone's
`scale`, is ported from the remake's code. It is tested only with a zone the
test supplies, never against a level that has one.

**Within a tick, portals go last.** The order is the buttons and doors, the
player's steering, the physics step, the crystals and the exit, then the
portals. A body put through a portal is seen by the triggers where it came out
on the next tick. In Godot, a body moved in a signal is seen by areas on the
next step too.

**Built and verified.** The code is `sim/Portals` and the circle in
`sim/Trigger`. Seven new checks in `test_mp_play`:
- **The rig:** a box turned 45° is exact against both a box trigger and a
  circle trigger.
- **level30's portals:** a budget of 2 and no zones. The travellers are the
  player, `crate_ent_968` and `crate_small_ent_973`. `crate_969` is not one.
- **The cap, both ways:** recycling keeps two portals and drops the oldest.
  Refusing turns the third tap down.
- **A no-portal zone the test supplies:** a tap a pixel inside it is refused,
  and one a pixel outside is placed.
- **The player through a pair:** walking into a floor portal, the player comes
  out of its partner, clear of it along its velocity. It keeps its walking
  speed, and the pair is spent.
- **The inline crate:** a pair over `crate_969` stays for a second, and the
  crate does not go through.
- **The capstone:** a portal over `crate_ent_968` and one above the raised
  `button_980`.
  - The crate goes through on the next tick, and the pair is spent.
  - It lands on the block and presses channel 1, and holds it.
  - `door_lift_978` opens while 977 and 976 stay shut, in the whole level.

GCC 13.3 and MSVC 14.50 agree to the digit: 131 checks and 0 failures. The
other Magic Portals suites are unchanged.

**Where the port stands.** All five acceptance items have tests on level30:
walking and landing, buttons holding doors, crates pushed onto and portalled
onto buttons, crystals and the exit, and portals.
- **Not yet shown:** a full solve of level30 from the spawn to the exit, played
  through with taps and walking alone. The tests still place portals, and the
  player where needed.
- **What comes next:** step 7, a drawing layer, which turns taps into
  `Portals::State::TryPlace` through the camera.

## Step 7 - the layer: decisions, before the code

**One tick for the suites and the layer (`sim/Game`).**
- **What moved:** `test_mp_play`'s start-up and tick moved into `sim/Game`,
  split around the physics step. Before the step come the buttons and doors,
  then the player's steering. After it come the crystals and the exit, then the
  portals.
- **Why the split:** the app steps physics itself, before each layer's
  `OnFixedUpdate`. So the layer runs `AfterStep` then `BeforeStep`, with the
  app's step between. That is the suites' sequence, begun half a tick later.
- **The step matches exactly:** the app's `kFixedPhysicsStep` is 1/60, and so
  is the port's tick, so the app takes one step of 1/60 per tick, as
  `Game::Tick` does. If the layer and the suites ever disagree, it will be
  something other than the tick.

**Input is read on the tick.** Left and right, or A and D, are the remake's
two-button pad, and a tap places a portal. There is one tap per tick at most,
so a fast frame cannot spend the budget twice in one step.

**The camera is orthographic and fixed, fitted to level30's bounds** with a 5%
margin. The level fits on one screen, so camera follow, pan and zoom stay out
of scope, as the list at the top already says.

**The seam test goes through the camera.**
- **Two paths:** a tap is aimed with the camera's forward matrices, the
  renderer's path. The layer reads it back through `ScreenPointToRay` and the
  plane z = 0, a different path. A flip or an offset between them shows.
- **Absolute checks:** the level's centre must land at the viewport's centre,
  and its top edge up the screen.

**level30 is played from the spawn with taps and walking alone.** The route is
a portal on the floor ahead, its partner above `platform_ent_966`, and right
held. This is not the designed solve: a pair of portals skips the doors,
because the remake refuses a portal only inside a no-portal zone, and level30
has none. The exit's switch is off by default.

**The drawing is boxes, deliberately plain,** as HUSK's first view is. It draws
the bodies at their shapes' bounds, the buttons in their pressed colour, the
crystals still out, the exit, the placed portals, and two lines of text.

**Built and verified.** The code is `sim/Game`, the layer in the
`MagicPortalsGame` library, and the `MagicPortals` executable.
- **`test_mp_play`** now runs on `Game::Tick`, and is unchanged at 131 checks.
- **`test_mp_layer`,** 10 checks:
  - the layer builds level30 at 60 Hz;
  - the level's centre shows at the viewport's centre, with its top up the
    screen;
  - two taps put portals within half a pixel of where they pointed;
  - level30 is played from the spawn to the exit with taps and walking alone.
    The exit is reached 0.43 s after right goes down, with 1 of 5 crystals and
    2 portals.

GCC 13.3 and MSVC 14.50 agree to the digit, and the executable builds clean on
both.
