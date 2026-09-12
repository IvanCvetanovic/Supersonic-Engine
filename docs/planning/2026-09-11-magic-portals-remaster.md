# Magic Portals - the remaster

11 September 2026, on `main`. This continues
`2026-09-10-magic-portals-port.md`, which ported five behaviours and played
them on level30. The aim now is the whole game on the engine:
- the remake's 128 levels, four chapters of 32;
- drawn with the original's sprites.

## What the owner settled, 11 September

The owner played the original, so their testimony outranks what the remake
inferred from identifiers.
- **Art.** The original sprites, read from the remake's converted output beside
  the engine, as the levels already are. Never committed.
- **Static portals stay after use.** This confirms the remake's assumption
  (`portal_system.gd:14-17`). portals.json's `static_portals_persist` is true.
- **The camera follows the player, with no dragging.** This overrides the
  remake's reading of `dragMoveCamera` (`original-gameplay.md` §4).
- **The screen showed part of a level, and scrolled** as the player walked
  (asked the same day). How much it showed is read from the levels in step 2a.
- **Stones broke the sandy walls** (asked in step 6), including the eight the
  data leaves unflagged.
- **Portals are fired** (asked in step 7).
  - Tapping sends a shot from the character toward the tap, and the portal
    opens where the shot arrives.
  - A wall in between means no portal: the shot fails.
  - A crate or a stone in between stops it too: "it would hit them before
    reaching the tapped location" (asked in step 8).
  - A failed shot costs nothing, "since it would never appear" (asked in step
    8).
  - Reflectors bounce the shot.

  This overrides the remake, which opens a portal at the tap at once. The
  original's script names the parts: `portal_launch`,
  `computeProjectileOrigin`, `projectileDestiny`, `computePortalFinalPos`,
  `destroyOnStaticHit`, `computeReflectVector`, `portal_reflect`,
  `ETHCallback_anti_projectile_wall`.
- **A destroyier kills the character** (asked in step 7). This was the
  remake's reading, and is now testimony.

## The rules carried over

- **The acceptance rule.** What the converter and the role table decide is
  pinned exactly. Physics is judged by thresholds. Anything the remake marks
  `_guess` is carried as data and never pinned.
- **Switches.** A behaviour switch the remake marks UNVERIFIED is tested both
  ways.
- **Outside the repository.** Levels, data and art are read from outside the
  repository and never committed.
- **The remake is read-only.** It is the oracle.

## The inventory

`test_mp_start` starts all 128 levels as `Game::Start` does, then drops the
player for three seconds. Its first run, on 11 September and before static
portals were ported:

| Chapter | Start | What refuses the rest |
|---|---|---|
| 1 | 24 of 32 | static portals (level0, level1), timed crystals (levels 14, 23, 24, 26, 28), a moving no-portal zone (level10) |
| 2 | 23 of 32 | timed crystals (9 levels) |
| 3 | 28 of 32 | timed crystals (4 levels) |
| 4 | 2 of 32 | `no_gravity` (18 levels), `darkest` (12 levels) |

Every level that started, landed.
- **Starts is not plays.** The builder turns the entities of roles the port does
  not play into inert scenery.
- **The suite's three outcomes.** It lists, against each level, the roles it
  leaves inert (`Roles::IsPorted`, the one list to extend as roles land). A
  level plays only when that list is empty.
- **Two flags are refused outright.** `Game::Start` now refuses `no_gravity` and
  `darkest`. Left alone, a zero-gravity level would drop everything in it and a
  dark one would be lit, and either would look like a level that works.

Chapter 1's roles the port does not play yet, counted by the census script
over the level files:

| Role | Levels | Placements |
|---|---|---|
| demolisher (rolling stones) | 6 | 9 |
| reflector | 6 | 9 |
| static portal | 3 | 7 |
| hazard | 3 | 4 |
| launcher | 3 | 3 |
| projectile blocker | 1 | 1 |
| boss | 1 | 1 |

## Order

Chapter 1 first, then the others in the same way, each opened with the
inventory.

1. **Static portals.** level0 is the game's first level. It grants no
   placements and is solved with the portals it ships.
2. **The game around a level.**
   - chapters.json's order, with the exit leading to the next level;
   - retry;
   - the camera following the player, from `camera_start`;
   - a HUD with the portals spent against the level's golden score.
3. **Timed crystals.** Five chapter-1 levels.
4. **Moving no-portal zones.** level10.
5. **Hazards, and death.**
6. **Rolling stones** and the walls they break.
7. **Launchers.**
8. **The portal shot, and the projectile blocker.** Added on 11 September,
   when step 7 found that portals are fired (see the owner's answers). Level
   1-7's blocker can only stop a portal shot, so it moved here from step 7.
9. **The original's sprites.** Split in three (see step 9):
   - 9a, additive blending in the engine;
   - 9b, the levels' own art;
   - 9c, the original's art for what no level pictures: the player, the
     portals a shot opens, and the shot. It waits on the owner's word about
     the character's frames.
10. **Reflectors.** The remake marks what they reflect a guess. The owner said
    on 11 September that they bounce the portal shot, so they come after it.
    The owner said it bounces "under an angle" and did not know how far it
    goes on; they will judge that by playing (see step 10). Nothing is to
    be invented: the remake's own worst bug of this kind was `bounce`, built as
    a trampoline that threw the player out of level 4-7.
11. **Chapter 1's boss** (level31). The remake has its structure only. Split
    in two when the original's script turned out to be readable whole:
    - 11a, the portal traversal as the original does it, which the boss is won
      by;
    - 11b, the beholder.

## Step 1 - static portals (built)

What `portal_system.gd` does with static portals, now in `Portals`:
- **Pairing.** A static portal leads to the static portal whose index is its
  `destiny`. When no static portal has that index, it leads to the first placed
  portal: level1's tutorial ships one static portal and grants one placement.
  One that names itself leads nowhere. One that is inactive, or has no index,
  is passed over.
- **What is spent.** Placed ends are spent on use. Static ends stay, by the
  owner's word and `static_portals_persist`. With that setting off, both static
  ends go.
- **Coming back.** A traveller put down inside its exit does not go back while
  it stays there, because an entry is an edge, as `body_entered` is.
- **Drawn** as boxes at their triggers, in the level's red and blue.

`test_mp_statics`, 65 checks:
- **level0's four portals** are pinned to the file: index, destiny, colour and
  place. So is its budget of 0, which refuses a tap.
- **A static pair** carries the player to within 20 px of its exit, and neither
  end goes. With static portals not persisting, both do.
- **level1's portal** leads nowhere with nothing placed. Once a portal is
  placed, it leads there, and the placed portal is spent while the static one
  stays.
- **level0 played from the spawn by holding right:** completed in 3.07 s,
  through both static pairs, with no portal placed.

Chapter 1 after step 1, from `test_mp_start`:

| | Levels |
|---|---|
| Play (14) | 0, 1, 2, 3, 4, 7, 9, 13, 15, 16, 17, 27, 29, 30 |
| Start, with roles left inert (12) | 5 hazard; 6 projectile blocker; 8, 18, 21 demolisher; 11, 20 reflector; 12, 19, 22 demolisher and reflector; 25 launcher; 31 boss |
| Refused (6) | 10 moving no-portal zone; 14, 23, 24, 26, 28 timed crystals |

Across the game, 79 of 128 levels start. The floors in `test_mp_start` are
raised to these counts.

GCC 13.3 and MSVC 14.50 agree to the digit on every Magic Portals suite:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_statics | 65 | 0 |
| test_mp_start | 346 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_layer | 10 | 0 |

On both, level0 plays through in the same 3.07 s.

## Step 2a - the level order and the camera's arithmetic (built)

Step 2 is split, on review. The level order and the camera land first, with
suites of their own. The layer then uses them, so a failure there has one
cause.

**The level order.**
- **What reads it:** `Chapters` reads `chapters.json`, the converter's reading
  of `goldenScores.enml`: four worlds of 32, each level with its golden score.
- **Where from:** beside the levels (`SUPERSONIC_MAGICPORTALS_CHAPTERS`),
  because it is converter output.
- **`Next`** stays within a world, as the remake's `advance_level` does. What a
  finished chapter means is left to its caller.
- **A finding for the remake:** its `original-gameplay.md` §3 says seven levels
  ship a golden score of 0. The file has eight: level0, level14, level0a,
  level1a, level6a, level27a, level1b and level5c. `test_mp_chapters` pins what
  the file says.

**The view.** The owner remembers the screen scrolling. How much it showed is
read from the levels:
- **Height:** 73 of the 128 levels are exactly 256 px tall, and none is
  shorter.
- **Width:** the narrowest level is 456 px wide, against the original's
  455x256 background art.
- **So:** a view 256 px tall at the window's shape, 455 px wide at 16:9. At
  that size no level fits whole, and most scroll by one or two screens.
- **The remake's 1280x720** is marked a placeholder in its own
  `project.godot`. At that size 127 of 128 levels would fit whole, and a
  following camera would never move.

The number goes into the port's own data with the layer, so play can retune it.

**The camera's arithmetic.** `Camera`, pure and without data:
- **Where it starts:** at `camera_start`, which sits near a level's exit. It
  holds there for `hold_time_s`.
- **Easing:** it then eases to the player, closing `dt / follow_lag_s` of the
  gap each tick. Both numbers are `portals.json` guesses.
- **Per tick:** the remake eases per frame. The port eases per tick, so a tap
  meets the camera a replay meets.
- **Bounds:** it never shows past (0, 0) or `level_bounds`.

Two of these are the port's, not the remake's: the remake's camera reads
neither the hold nor any limits. Both come from the original. `cameraHoldTime`
and `clampCameraPos` are identifiers in its bytecode, and the `level_bounds`
role note pairs the marker with `clampCameraPos`.

`camera_start` is read as the view's centre, as the remake reads it. Read as the
top-left corner, the way Ethanon places its camera, the view comes out nearly the
same once it is held inside the level: every `camera_start` sits at or near y 0,
and near a level's far end.

| Suite | Checks | Failures |
|---|---|---|
| test_mp_chapters | 39 | 0 |
| test_mp_camera | 13 | 0 |

Both pass on GCC 13.3 and MSVC 14.50.

## Step 2b - the game around a level (built)

The layer now plays the game, not one level:
- **Order.** It starts at a level named on the command line (`--level`,
  default `level0`), in `chapters.json`'s order. Reaching the exit records the
  level and loads the next one at once, as the remake advances.
- **Retry.** R rebuilds the level from what was read when it loaded, so a retry
  never touches the disk. The remake caches its level scene for the same reason.
- **Skip.** A level the port refuses says why, and N skips it. N is the remake's
  development shortcut, kept while levels are refused. Start may have built part
  of a refused level before it refused, and none of it is left.
- **A world's end** is the chapter's end. The remake opens its menu there; the
  port has none yet, so it says so.
- **The camera** is `Camera::Follow`, moved on the tick and drawn between ticks
  (`InterpolatedCameraComponent`, as HUSK's is). A new level is a cut, not a pan.
- **The view** is the port's own data: `games/magicportals/data/view.json`,
  committed because it is the port's and not Asantee's. Its note carries the
  evidence for 256.
- **The HUD** shows the level, its crystals, the portals spent against the golden
  score, and the last level cleared, marked gold where it earned it.

`test_mp_layer` is rewritten around what a player can see. A tap is only made
where the screen shows the spot. So level30's play-through now waits for the
camera:
- the far portal is tapped at the start, while the camera is over the exit end;
- the near portal is tapped once the camera has come to the player.
The route depends on what is on screen, not on how long the camera holds, which
is a guess. It clears level30 in the same 0.43 s and loads level31.

The new checks:
- **Order and retry.** level0 walked right clears as 1-1 with no portal, which
  is gold, and loads level1 with the player at its spawn. A portal placed and
  then R gives level1 as it loaded.
- **The camera.** It never shows past level0 on the way.
- **Skip.** level10 says it has a moving no-portal zone, and leaves nothing
  behind; N loads level11.
- **The chapter's end.** N on level31 ends the chapter, and R does nothing
  there.
- **A bad start.** A start that is no level says so.

GCC 13.3 and MSVC 14.50 agree to the digit on every Magic Portals suite:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_layer | 42 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 346 | 0 |

The headless renders of level0 and level30 show the 256 px view following the
player.

**The HUD is not in them, and never has been in any game's.** `UISystem` draws
text through ImGui's draw list over the game, after the scene. `--screenshot`
writes the offscreen scene image (`SupersonicApp.cpp:952-959`), so it never
holds the UI. HUSK's and Wolf Brigade's screenshots lack their HUDs for the same
reason. The HUD's text is checked through the layer's accessors, not by a
picture. Its look waits for someone at the window.

## Step 3 - timed crystals (built)

What `behaviours.gd`'s `TimedCollectible` does, now in `Goals`:
- **The rule.** An inline `crystal` with a `time` goes that long after the
  level starts. `crystal.ent` never has one. Chapter 1 has eight timed
  crystals, from 5 to 12 s:
  - level14 and level26 have one each;
  - level23 has three;
  - level24 has two;
  - level28 has one.
- **A gone crystal** cannot be collected, and it still counts against the
  exit's switch, because the remake's `crystals_remaining` never drops for one.
  So with `exit_requires_all_crystals` on, a level whose timed crystal went can
  no longer be finished. The switch is off in `portals.json`, and marked
  UNVERIFIED.
- **Order within a tick.** The player collects before the timers run down, as
  Godot runs a frame's physics before its `_process`.
- **The blink.** In its last two seconds a timed crystal dims and brightens, on
  the remake's formula, as brightness rather than alpha. It is a guess, as the
  remake's is, and nothing depends on it.
- **Not crystals.** Three later levels' `end_delay` nodes also carry a `time`.
  They are not crystals, and not part of this step.

`test_mp_timed`, 50 checks:
- **The files.** Chapter 1's eight timed crystals are pinned to them.
- **Expiry.** level14's crystal is there at 11.9 s and gone by 12.05 s. Once
  gone, it cannot be collected.
- **Collected first,** it stays collected.
- **The exit switch** is run both ways.

The inventory after step 3:

| Chapter | Start | Play |
|---|---|---|
| 1 | 31 of 32 | 16 |
| 2 | 32 of 32 | 0 |
| 3 | 31 of 32 | 0 |
| 4 | 2 of 32 | 0 |

That is 96 of 128 levels starting, up from 79. What is left in chapter 1:

| What | Levels |
|---|---|
| Moving no-portal zone | 10 |
| Hazards | 5, 23, 24 |
| Rolling stones | 8, 12, 18, 19, 21, 22 |
| Launchers | 23, 24, 25 |
| Projectile blocker | 6 |
| Reflectors | 11, 12, 19, 20, 22, 28 |
| Boss | 31 |

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_timed | 50 | 0 |
| test_mp_layer | 42 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 363 | 0 |

## Step 4 - what moves: platforms, lifts and patrolling zones (built)

**First, a correction to the inventory.** Step 1's `Roles::IsPorted` counted
lifts and moving platforms as played. They were not:
- the builder makes their bodies kinematic, so they could carry a rider;
- but nothing moved them. Only doors moved.
So levels 26 and 27 were counted as playing while their lifts stood still. The
roles came off the list until this step, and this step puts them back.

What `behaviours.gd`'s `Mover` does, now in `Mover`:
- **Moving platforms** swing vertically about where the level places them, by
  half their stride either way: `sin(k × speed × t) × stride / 2`.
  - `speed` is a frequency, by the remake's reading of the data.
  - `k` is the remake's guess (`RATE_SCALE_K` = 2), which it keeps in a script.
    The port carries it in its own `games/magicportals/data/movers.json`.
  - The sine is `DetMath`'s, so a swing comes out the same on every C runtime.
- **Lifts** go from the marker their `a` names toward the one `b` names, and
  back, turning the moment they arrive. No lift in chapter 1 has a speed of its
  own, so all run at the remake's default, 120 px/s. That is also carried in
  `movers.json`.
- **Both move through `MoveKinematic`,** which writes the velocity with the
  position, so they carry what stands on them as doors do.
- **Patrolling no-portal zones.** An `anti_portal_agent` with a speed and a
  stride swings like a platform, on the axis its `direction` names. A tap is
  tested against where the zone is now. Zones are now drawn as well, behind
  everything, so they never cover the player.

**A finding for the remake.** Each of the game's three patrolling agents, two
in level10 and one in level13b, stands on a static `antiportal` of its own:
- each field (scale 3.5, or 5 in level13b) holds the agent's whole swing and its
  own 16 px radius;
- so in the remake the patrol never changes where a tap is refused.

The pairing suggests the original's field travels with its agent. The port keeps
the remake's two zones as they are. Whether the field moves is a question for
playing the original, not something to invent.

**Noted, not changed.** `Trigger.cpp`'s exact overlap test turns a box with the
platform's `std::cos` and `std::sin`. The rest of the simulation's trigonometry
uses `DetMath`. This is the simulation's one call left to libm, and it is noted
here for the determinism work.

`test_mp_movers`, 53 checks:
- **The files.** Chapter 1's movers are pinned to them:
  - level5's and level10's platforms;
  - the four lifts in levels 26, 27 and 28, with their markers;
  - level10's three agents and the fields they stand on.
- **The platform.** level5's platform swings exactly its 64 px stride over its
  4.49 s period, at the place the swing says, with the velocity that gets it
  there.
- **The lift.** level27's lift is at its far marker after 64 ticks (128 px at
  120 px/s) and back after 128.
- **A patrolling zone** refuses a tap where it is and not where it has left. In
  level10 itself, the field keeps that spot refused.

`test_mp_layer`'s skip check moves to level26c, a dark level the port still
refuses, since level10 now plays.

The inventory after step 4:

| Chapter | Start | Play |
|---|---|---|
| 1 | 32 of 32 | 17 |
| 2 | 32 of 32 | 0 |
| 3 | 32 of 32 | 0 |
| 4 | 2 of 32 | 0 |

That is 98 of 128 levels starting. Every level of chapters 1 to 3 starts. What is
left in chapter 1:

| What | Levels |
|---|---|
| Hazards | 5, 23, 24 |
| Rolling stones | 8, 12, 18, 19, 21, 22 |
| Launchers | 23, 24, 25 |
| Projectile blocker | 6 |
| Reflectors | 11, 12, 19, 20, 22, 28 |
| Boss | 31 |

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_movers | 53 | 0 |
| test_mp_timed | 50 | 0 |
| test_mp_layer | 42 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 365 | 0 |

## Step 5 - hazards, and death (built)

What `level_runtime.gd` does with a hazard, now in `Hazards`:
- **The trigger is the remake's box, not the hazard's shape.**
  - `_attach_trigger` gives every hazard the box its `trigger_size` names, or
    the 16 px fallback. It never reads the `Area2D` the converter builds.
  - None of chapter 1's four hazards carries a `trigger_size`.
  - So level5's `death_area` is a 60x16 octagon, and only its middle 16x16
    kills. The same holds for each `destroyier`.

  The port does what the remake does, as it does for buttons. The original's
  sensor would fire on the whole shape. Which of the two is right is a question
  for playing the original, and until then the port plays the remake's box.
- **Who dies, and when.** Only the player, and on entry, as `body_entered`
  fires. So a player put down inside a hazard dies on the first tick.
- **A death is a retry, at once** (`main.gd:155-158`). The layer counts deaths
  for the HUD. Should the exit and a hazard both report on one tick, the exit
  wins, because the remake's order of two triggers in a frame is not defined.
- **The launcher does nothing in the remake.** `destroyier.ent` is also what
  despawns a launcher's throws (`hazards.gd:420-422`). But the remake's
  launcher only announces what it would throw, because the converter emits
  levels, not entities a game could spawn. Until step 7, a `destroyier` here is
  only a hazard.
  - *Update, step 7.* The owner confirmed that a destroyier kills the
    character, so that is testimony now. The port's launchers throw, and a
    destroyier also takes back what they throw.
- **Drawn** at the box that kills.

`test_mp_hazards`, 19 checks:
- **The files.** Chapter 1's four hazards are pinned, each the 16 px fallback at
  its node.
- **The edge.** Beside the box but inside the octagon, the player lives. In the
  box, it dies on that tick, and the hazard is named.

`test_mp_layer` adds a check that dying in level5 reloads it on the same tick,
with the player at its spawn and one death counted.

The inventory after step 5: chapter 1 has 32 levels starting and 18 playing.
Chapters 2 to 4 are unchanged. What is left in chapter 1:

| What | Levels |
|---|---|
| Rolling stones | 8, 12, 18, 19, 21, 22 |
| Launchers | 23, 24, 25 |
| Projectile blocker | 6 |
| Reflectors | 11, 12, 19, 20, 22, 28 |
| Boss | 31 |

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_hazards | 19 | 0 |
| test_mp_movers | 53 | 0 |
| test_mp_timed | 50 | 0 |
| test_mp_layer | 49 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 365 | 0 |

## Step 6 - rolling stones, and the walls they break (built)

What the remake's Demolisher does (`hazards.gd:97-120`), now in `Demolish`:
- **A stone breaks whatever breakable thing it touches.** Breaking takes the
  whole body away, collision included. A stone is not a hazard: it kills
  nothing, and level 1-9 spawns the player 58 px from one.
- **What is breakable, and the owner's ruling.**
  - The remake breaks only what carries `metadata/breakable`.
  - Eight `breakable_wall` placements carry no flag:
    - chapter 1: levels 8, 12, 18, 21 (two), 23 and 24;
    - chapter 2: level3a.

    So in the remake a stone cannot break the wall in 1-9, which is the level
    the achievement "Like a rolling stone" names.
  - **The gap is in the original's data, not the converter.**
    - Each of those walls is an inline entity in its `.esc`, and its CustomData
      has no `breakable`. level8's carries only `blink`.
    - Ethanon reads an inline entity as it stands
      (`ETHEntityProperties.cpp:202-224`). Only a `<FileName>` reference would
      pull in `breakable_wall.ent`, and that file does set the flag.

    So the original decides by something the data does not show.
  - The owner played the original and confirmed on 11 September that stones
    broke those walls. The port's `demolish.json` lists `breakable_wall` as
    breakable by name, with that note. Both readings are tested.
- **Touching.** It is judged in the port's own tick after the step, as the
  port's triggers are.
  - The test is the stone's circle against the breakable's box, grown by
    `demolish.json`'s contact margin: 1 px, and the port's own guess. The
    margin is there because a solver leaves a resting stone touching a wall,
    not overlapping it.
  - The box is the shape's bounds, so an octagonal wall is met at most its
    1.5 x 6.3 px chamfer early.
  - The margin grows the box on all four sides, so at a corner a stone reaches
    the margin times the square root of 2, not the margin. At 1 px that is
    noise. Anyone who raises the margin should know it.
- **Quarter turns only.** A breakable turned by any other angle is refused,
  with a named error. Every breakable in the game is turned by 0 or by a
  quarter turn, and `test_mp_start` still starts all the levels it did. So only
  new data can reach this refusal, and it stops the whole level from starting.
  If a later census turns up a level refused for this reason, this rule is why.
- **The tick.** After the step: goals, then hazards, then stones, then
  portals. The portals are last because they move what goes through them.
- **Drawn.** Breakables are sandy and stones grey. A broken wall's box goes
  with its body. Before this, the layer would have left a destroyed body's box
  standing where it was last drawn.
- **Left for step 7.**
  - Levels 23 and 24 have a breakable wall and no stone, because their launcher
    throws `rolling_stone.ent`. Step 7 built that.
  - 22 of the game's 27 stones carry a `destroyable` flag, and nothing reads it
    yet. `rolling_stone_destroy.ent` exists beside `rolling_stone.ent`. Step 7
    found `isDestroyableByClaw` in the original's script, so the flag may
    belong to the boss's claw rather than to launchers.

`test_mp_demolish`, 132 checks:
- **The files.** Chapter 1's stones and breakables are pinned: each wall's box,
  and whether the level flags it. The counts are also pinned with the name rule
  off, which is the remake's reading.
- **Breaking.** level8's stone, thrown at its wall, breaks it, and nothing else
  goes. The throw is 300 px/s from 6 px away, and the wall breaks on tick 16.
  With the flag alone, the stone stops at the wall.
- **Not breaking.** Left alone, the stone stays where it is and breaks nothing.
  The player walking into the wall breaks nothing.
- **The edge.** On its own, a quarter pixel either side of the margin.
- **1-9, played.** One portal goes at the hint arrow behind the stone, and one
  ahead of the player. Holding right takes the player through them and pushes
  the stone off the ledge into the wall. The player reaches the exit in 3.85 s.

`test_mp_layer` adds a check that a broken wall's box goes on the tick it
breaks.

The inventory after step 6: chapter 1 has 32 levels starting and 21 playing.
Chapters 2 to 4 are unchanged. What is left in chapter 1:

| What | Levels |
|---|---|
| Launchers | 23, 24, 25 |
| Projectile blocker | 6 |
| Reflectors | 11, 12, 19, 20, 22, 28 |
| Boss | 31 |

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_demolish | 132 | 0 |
| test_mp_hazards | 19 | 0 |
| test_mp_movers | 53 | 0 |
| test_mp_timed | 50 | 0 |
| test_mp_layer | 52 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 365 | 0 |

## Step 7 - launchers (built)

What an `entity_launcher` does, now in `Launchers`. The remake's launcher only
announces what it would throw, so this part is the port's own:
- **A throw.** Every `stride` ms, a launcher throws the entity its `entity`
  names, at its node. All three in the game throw `rolling_stone.ent` from
  above the level: every 1.5 s in levels 23 and 24, and every 2.5 s in level 25.
- **What a thrown stone is.** `launchers.json` records it from
  `rolling_stone.ent`: a rigid circle of 30 px that demolishes and travels. So
  from the tick it is thrown, it breaks walls and goes through portals as a
  placed stone does.
- **When the first comes.** One stride after the level starts, as the remake's
  launcher waits. The original has a `LAUNCH_TIME` constant whose value is not
  decoded, so `launchers.json` carries the delay as a guess.
- **Taking back.**
  - A launcher's min/max is a cull box, as the remake reads it. A thrown stone
    whose centre leaves the box is removed.
  - So is one that touches a `destroyier`, in the box the port gives it as a
    hazard.
  - Only thrown stones are removed.

  In level23, a minute of throwing leaves nothing behind: 41 thrown and 41
  taken back.
- **The tick.** Throws come before the step, so a new stone is in the next one.
  Taking back comes after the stones break things and before the portals.
- **Drawn** grey: a box for each thrown stone, made and unmade with it.

**What step 7 found: portals are fired.**
- Level 1-7's `anti_projectile_wall` stands in a level with no turret and no
  launcher. In chapter 1, the only thing it could stop is a portal shot.
- The original's script has the shot's parts: `portal_launch`,
  `computeProjectileOrigin`, `projectileDestiny`, `computePortalFinalPos`,
  `destroyOnStaticHit`, `computeReflectVector`, `portal_reflect`.
- The owner confirmed it: a shot flies from the character to the tap and opens
  the portal where it arrives. A wall in between fails it, and reflectors
  bounce it.

So the remake's placement, a portal at the tap at once, is wrong. The shot and
the projectile blocker are now step 8. Two questions go to the owner before it
is built:
- whether crates and stones stop a shot;
- whether a failed shot costs a portal.

The first decides which routes solve. The second decides what the golden
score counts. Both were answered before step 8 was built.

`test_mp_launchers`, 90 checks:
- **The files.** The three launchers are pinned: each one's place, what it
  throws, its stride and its cull box. The destroyiers under them are pinned
  too.
- **Throwing.** The first stone comes on tick 90, which is the one stride that
  `launchers.json` holds. It appears at its launcher, and is at once a stone and
  a traveller. After that, one comes every stride.
- **Taking back.**
  - The first stone falls out of level23 and is taken back within a stride, and
    nothing still holds it. A minute later none are left.
  - A destroyier takes a stone back on the tick. With the despawners removed,
    the same stone stays.
  - Just inside the cull box's bottom edge a stone stays, and just past it the
    stone goes. Only the bottom edge is tested. In level23 the other three sit
    behind the level's own walls, which push a stone back inside before the
    cull looks.
- **Through a portal pair.**
  - Level23's wall and level24's wall are each broken by the first thrown
    stone, at 1.98 s and 1.95 s.
  - Level25's button is pressed by it at 2.90 s, and its door rises.

`test_mp_layer` adds a retry in level23 after its first throw. The retry takes
every thrown stone and its box away and sets the launcher's count back to
nothing. The next throw comes a whole first delay after the retry, on the same
tick as the first did.

The inventory after step 7: chapter 1 has 32 levels starting and 24 playing.
Chapters 2 to 4 are unchanged. What is left in chapter 1:

| What | Levels |
|---|---|
| The portal shot, and the projectile blocker | 6, and every level's placement |
| Reflectors | 11, 12, 19, 20, 22, 28 |
| Boss | 31 |

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_launchers | 90 | 0 |
| test_mp_demolish | 132 | 0 |
| test_mp_hazards | 19 | 0 |
| test_mp_movers | 53 | 0 |
| test_mp_timed | 50 | 0 |
| test_mp_layer | 62 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 365 | 0 |

## Step 8 - the portal shot, and the projectile blocker (built)

What the owner described, now in `Shot` and `Portals`:
- **A tap fires.** The shot leaves the player's centre toward the tap, at
  `shot.json`'s speed of 600 px/s, which is a guess. One shot flies at a time;
  a tap while one flies is ignored.
- **What stops it.**
  - Every solid body on its way: walls and floors, doors and platforms, crates
    and stones, thrown stones included.
  - A projectile blocker. Level 1-7's `anti_projectile_wall` is a 64x128 box
    that nobody sees and the player walks through.

  A trigger never stops a shot, and neither does a portal or the player.
- **Where it opens.** At the tap, once the shot gets there. It can still fail
  there: inside a no-portal zone, or at the cap with recycling off. The
  original has `portal_fail_in_antiportal` for the first.
- **What a failure costs.** Nothing. There is no portal and no count, as the
  owner said. `shotsFired` and `shotsFailed` are counted apart from
  `portalsUsed`, which is what the golden score reads.
- **Exact in the plane.**
  - The port has its own segment test. It reads box colliders turned with their
    entity, circles, and the polygon LevelBuilder now records for each hull.
    The engine's raycast sees boxes and hulls only by their bounds.
  - The turn uses DetMath, so a grazing shot is judged the same on every C
    runtime.
- **What is guessed.**
  - The speed.
  - That the shot leaves from the player's centre. `computeProjectileOrigin`
    is not decoded.
  - That the no-portal zone is checked where the shot arrives, not along the
    way.
  - One shot at a time, after `hasProjectileAround`.
  - The cooldowns, `FIRST_PORTAL_MIN_TIME` and `NEXT_PORTAL_MIN_TIME`, are
    still not modelled. **No longer true - step 21 builds them**, at 300 ms and
    400 ms with both clocks starting at zero.
- **`TryPlace` stays.** It is what an arriving shot calls, and the suites use
  it to put a portal where a test needs one:
  - the suites for transit, buttons, movers and launchers use it;
  - the play-throughs that claim a level is played now fire shots.
- **Drawn.** A small blue box shows the shot in flight, in the blue of
  `projectile.ent`'s light.

What changed because of it:
- **Level30's play-through is gone.** Its portal pair skipped the doors from
  the spawn. With shots, the crates and the closed doors stand between the
  spawn and everywhere past them, so only the designed solve is left.
  - The layer's play-through is now 1-9.
  - The level30 tap test aims over the crates. It also checks that a tap into
    `crate_969` opens nothing and costs nothing.
- **1-9's route changed.** From the spawn, a shot at the space behind the
  stone meets the stone.
  - The player walks right onto the slope first. From there, a shot to
    (30, 30) clears the stone by about 10 px.
  - Then one more shot goes just ahead of the player, and holding right does
    the rest.
  - It takes 3.88 s in the simulation, and 3.90 s through the layer, with all
    three crystals.

  That this is the designed route is not claimed.
- **Known gaps.**
  - Level30's designed solve has never been played through by the port,
    either before this step or after it. That solve puts crates onto buttons
    to open the doors. `test_mp_play` covers the crate-onto-button capstone on
    its own, so the pieces are tested, but the whole route is not.
  - The inventory counts roles, not routes. "Plays" means every role in the
    level is ported and the player lands. Of the 25 levels that play, only
    1-9's route has been played with shots. If a later level turns out to have
    no route under the shot rule, the inventory will not show it.

`test_mp_shot`, 58 checks:
- **The files.** Level 1-7's blocker is pinned with its box.
- **Flying.**
  - A shot lands where it was aimed after the flight time `shot.json` gives:
    5 ticks for 48 px.
  - Nothing opens before it arrives.
  - A second tap while it flies is refused.
- **Stopping.**
  - Level1's floor (`platform_ent_621`) stops a shot. It costs nothing: the
    level's one portal is still there, and the next shot lands.
  - Level8's stone stops the shot aimed at the hint arrow.
  - Level 1-7's blocker stops a shot. With the blocker removed, the same shot
    lands.
  - A shot into a no-portal zone opens nothing.
  - Level0 grants no portal, so a tap there fires nothing.
- **The segment test on its own.**
  - A segment enters a turned box's bounds but misses the box, and the test
    finds no hit.
  - A hull's triangle and a circle are each met where the arithmetic says.

The inventory after step 8: chapter 1 has 32 levels starting and 25 playing.
Chapters 2 to 4 are unchanged. What is left in chapter 1:

| What | Levels |
|---|---|
| Reflectors | 11, 12, 19, 20, 22, 28 |
| Boss | 31 |

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_shot | 58 | 0 |
| test_mp_launchers | 90 | 0 |
| test_mp_demolish | 133 | 0 |
| test_mp_hazards | 19 | 0 |
| test_mp_movers | 53 | 0 |
| test_mp_timed | 50 | 0 |
| test_mp_layer | 65 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 365 | 0 |

## Step 9 - the art (9a to 9d built)

Split in three, so that what needs nobody's word lands first:
- **9a, the engine.** Additive blending, which the levels' glows need.
- **9b, the levels' own art.** Every sprite the converter wrote, drawn from
  its `out/assets`. Every texture path is one a level file names.
- **9c, the original's art for what no level pictures.** The player is
  `dark_mage.ent`, whose sheet `magic_portals_hd.png` holds sixteen 40x56
  frames. The portals a shot opens are `portal.ent`, drawing
  `portal_halo.png`. The shot is `projectile.ent`, drawing `projectile.png` in
  six frames. These are in the extracted original, not in the converter's
  output. Which of the sheet's frames are standing and walking, and which way
  they face, is the owner's to say. Composing a walk cycle from the sheet
  would be inventing it.

### 9a - additive blending in the engine (built, fa28d7d)

- **The need.** 99 sprites across the levels carry Godot's
  `BLEND_MODE_ADD`: 71 anti-portal zones, 19 spirals, 7 static portals, a key
  and a satellite. (9a's commit message says 98; the sweep below counts 99.) `portal_halo.png` and `projectile.png` are RGB with no alpha
  channel, so mixed they would be black squares.
- **The engine.** `MaterialComponent::blend` is `Alpha` or `Additive`, and
  means something only on a transparent material. The renderer has a third
  scene pipeline, which adds.
- **The order.** The blended pass records the sorted draws in runs of one
  blend. A halo between two sprites stays between them. Grouping by blend
  would save binds and put the halo in front.
- **Tests.** `test_draworder` (85 checks) and `test_materials` (210). GCC's
  ctest passes 91 of 91. MSVC builds everything, and the blend suites and
  their neighbours pass.

### 9b - the levels' art (built)

`Sprites` reads every `Sprite2D`:
- **What it reads.**
  - The texture. It is a `res://` path, resolved against the directory above
    the levels, where the converter writes the art.
  - The sprite's offset.
  - The entity node's position, rotation and `z_index`.
  - A `CanvasItemMaterial`. With `blend_mode` 1 it adds; 0 mixes.
- **Size.** Godot draws a `Sprite2D` centred, at its image's size, and the
  converter never writes a scale. The size is read from the image's header.
  The converter copies PNGs and one BMP, `black.bmp`, a doorway's black at
  40x100.
- **Order.** Godot's canvas order: `z_index`, then the file.
- **Strict,** like the reader under it. These are errors naming the line:
  - a sprite that is not an entity's own;
  - a texture that is not a `res://` image;
  - an image that cannot be sized;
  - a blend the port does not draw.

The layer draws each sprite as a quad, unlit, blended as the level says:
- **What a sprite follows.**
  - Its node's body: platforms, lifts, doors, crates and stones.
  - Or the crystal, static portal or patrolling no-portal zone it pictures.

  It goes when that goes: a crystal taken, a wall a stone broke, a static
  portal spent. A timed crystal fades by alpha, as the remake's guess fades it.
- **Thrown stones** are drawn as `rolling_stone.ent` is, with
  `rolling_stone.png`. `launchers.json` now names each throwable's sprite,
  taken from its `.ent`.
- **Depth.** Each sprite gets a slot from the back, a hair apart. The player
  takes the slot after the last sprite at `z_index` 0 or below. That is where
  the remake draws its player: added after the level's nodes, at `z_index` 0.
- **Boxes.**
  - The bodies' boxes are kept, hidden behind the art, and B shows them.
  - The player, the portals a shot opens and the shot stay boxes until 9c.
  - Without the art, on a machine without the remake's `out/`, the level is
    still played, drawn as boxes, and the layer says why.
- **Where the art is.** `--art` names the directory. `--levels` elsewhere
  brings its art with it.
- **A C4458 from step 8 is fixed.** `Portals::Shoot`'s local `shot` hid the
  member. MSVC warned; GCC does not.

What is not drawn, named here rather than found later:
- **Light.** The original lit its levels, with torches, `applyLight` and
  emissive colours. The remake draws unlit, and so does the port.
- **Particles.** Portals, crystals and the shot have particle systems (`.par`)
  in the original. None is drawn.
- **Tint.** `portal_static.ent`'s emissive colour (0.5, 0.4, 1.0) is not
  applied. Static portals are not tinted red and blue, as the level data
  calls them.
- **Scale.** An anti-portal zone's sprite is its image's size, while the zone
  refuses taps within `scale` times the radius. The converter writes no scale
  on the sprite, and the converter is the oracle.
- **Buttons** show no pressed state. The original's frames for it are not
  known.

`test_mp_sprites`, 68 checks. It is not skipped without the levels; only its
second half is:
- **Without the levels.**
  - The header reader, on files the suite writes: a PNG, a top-down BMP, an
    old OS/2 BMP.
  - It refuses a GIF, a cut header, a zero size and a missing file, naming
    them.
  - A hand-written scene, out of order, comes back in canvas order with every
    property. Its turned offset lands where the arithmetic says.
  - A missing image, a subtracting blend and a sprite under a body are each
    refused, by name.
- **With them.**
  - Level8's 23 sprites, in order, each image present, the sky first; six
    pinned with their image, size, place and `z_index`.
  - Level0's four static portals, the halo, added.
  - The one sideways offset, on level 4-32's dragon.
  - A turned block on level 1-2.

`test_mp_layer`, now 91 checks:
- level8's 23 quads, unlit, mixed, each with its image. The sky is the
  farthest back, at its own size.
- The bodies' boxes are hidden, B shows every one, and the player stays a box.
- A crystal taken takes its picture, and nothing outlives the layer.
- Level0's four halos are added.
- Without the art the level plays as boxes and says why.
- A broken wall takes its picture, and a thrown stone is drawn as a rolling
  stone and goes on a retry.

Rendered on both toolchains, 120 frames each:
- under GCC on lavapipe, with validation on, which reports nothing;
- under MSVC on the laptop's AMD Radeon 780M.

Level 1-1 shows its static portals as white halos over the arches, added.
Level 1-9 shows the stone, the three crystals, the hint arrow, the sandy wall
and the slope. The player is the one box left in either.

The inventory is unchanged: chapter 1 has 32 levels starting and 25 playing.
GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_sprites | 68 | 0 |
| test_mp_layer | 91 | 0 |
| test_mp_launchers | 93 | 0 |
| test_mp_shot | 58 | 0 |
| test_mp_demolish | 133 | 0 |
| test_mp_hazards | 19 | 0 |
| test_mp_movers | 53 | 0 |
| test_mp_timed | 50 | 0 |
| test_mp_camera | 15 | 0 |
| test_mp_chapters | 39 | 0 |
| test_mp_statics | 65 | 0 |
| test_mp_play | 131 | 0 |
| test_mp_start | 365 | 0 |

**Every level's art, read (c467272).** `test_mp_start` now runs the reader
over all 128 levels, whether or not they start. A refusal would draw a level
as boxes while the inventory still counted it as playing. All 128 read: 2,883
sprites, 99 of them added. The totals are pinned.

### 9c - the portals a shot opens, and the shot (built)

- **What draws them.** The original's own entities:
  - `portal.ent` draws `portal_halo.png`, added.
  - `projectile.ent` draws `projectile.png`, added, cut 6 x 1.

  The new port data file `art.json` names each one, and `sim/Art` reads it.
  `test_mp_sprites` pins these as the `.ent` files' facts. It checks that the
  images are cut as the `.ent` says: 64x64, and six frames of 64x64.
- **Where the images are.**
  - They are in the original's extracted assets beside the remake, read and
    never committed. `SUPERSONIC_MAGICPORTALS_ORIGINAL` names the directory,
    and its default follows the levels.
  - Without the images, the portal and the shot are boxes. A test that needs
    them skips, and says where it looked.
- **Drawing.**
  - Each placed portal gets a halo, just behind the player's slot, since the
    player walks into it. The shot goes just in front.
  - The shot's sheet plays on the engine's `SpriteAnimationComponent`, on the
    tick.
  - Their boxes stay behind them, and B shows them.
- **Guessed.** How fast the shot's frames play: 15 a second.
  `_projectileFrameTimer`'s stride is not decoded.
- **Not reproduced.**
  - The particles both entities carry, and the shot's light.
  - The shot's `PivotAdjust` (4, 0), and any turn toward its flight. The
    sheet is drawn upright at the shot's point.
  - What tells the two placed portals apart. They look alike; the original
    coloured them with particles (`portal_red.par`, `portal_blue.par`).

`test_mp_layer` now has 109 checks:
- A shot fired on level 1-2 is drawn as `projectile.ent`'s sheet: six frames,
  added, looping, with its box hidden.
- The portal it opens is the halo, added, with its box hidden until B.
- Without the original's images, the portal is its box.

`test_mp_sprites` now has 77 checks. GCC 13.3 and MSVC 14.50 agree to the
digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_sprites | 77 | 0 |
| test_mp_layer | 109 | 0 |
| test_mp_start | 496 | 0 |

The other MP suites are unchanged from 9b.

### 9d - the player (built)

The owner said on 11 September that the character turns as it walks in
different directions. The rest comes from the original's data and code.
- **What draws it.** `dark_mage.ent`: `magic_portals_hd.png`, cut 4 x 4 into
  cells of 40x56, mixed. It starts on frame 4, with `PivotAdjust` (0, 2).
  - Ethanon puts an image's pivot on its entity
    (`ETHSpriteEntity::ComputeInScreenSpriteCenter`), so the character is
    drawn 2 px above its body's centre.
  - These are the `.ent`'s facts, and `test_mp_sprites` pins them.
- **Which row walks which way.** This is derived, not testimony:
  - The original's `DIRECTION` enum decodes to `MOVING_DOWN` 0, `MOVING_LEFT`
    1, `MOVING_RIGHT` 2 and `MOVING_UP` 3.
  - Ethanon's own sample picks a character's row by its direction and its
    column by a frame timer (`Sample-project/main.angelscript:102`). The
    original's character has the same parts: `frameStride`, `idleColumn` and
    `findCharacterDirection`.
  - The sheet's row 1 faces left and row 2 right. The start frame, 4, is on
    the left row, so a level starts with the character facing left.

  If the walk looks wrong when the owner plays, this is the line to revisit.
- **How it moves.**
  - While a direction is held, it walks through that row's four columns.
  - When the key is let go, it stands on the idle column of the way it last
    walked.
  - It turns when the direction turns.
  - The animation runs on `SpriteAnimationComponent`, on the tick.
- **Guessed.**
  - How fast it walks: 10 frames a second, for the original's `frameStride`.
  - Which column it stands on: 0, for `idleColumn`. The start frame is the
    only anchor for that.
- **Not used.** Rows 0 and 3, the arm-out poses. The character's `pushing`
  suggests them, and nothing says when.
- **Walking into a wall** still walks in place, because the walk follows the
  key, not the speed.

Two 9b checks were brought up to date. One is the hand-written `art.json`
that must be refused for a sheet with no frame rate; it now needs a
character too. The other said the player is a box; now the player is drawn
exactly once, as the character or as its box.

Rendered on the laptop's AMD Radeon 780M, levels 1-1 and 1-9 show the
character standing on the floor, facing left on its start frame.

| Suite | Checks | Failures |
|---|---|---|
| test_mp_sprites | 82 | 0 |
| test_mp_layer | 120 | 0 |

The other MP suites are unchanged.

## Step 10 - reflectors (built)

What the owner said on 11 September: a reflector reflects the shot "under an
angle". They did not know how far it goes on, and will tell by playing the
levels. So the rule below is data in `shot.json`, and each part of it is
marked a guess.
- **The turn.** A shot that comes within the catch radius of a reflector comes
  off it, mirrored across the reflector's plane. A vertical plane turns back
  its x, and a horizontal plane its y.
- **How far it goes on.** It goes on for the rest of the distance it was fired.
  A flat reflector therefore sends it to the tap, mirrored across the line
  where it met the reflector. The two readings offered earlier - "the tap
  mirrored" and "it goes on" - are this one rule.
- **How near is near.** `reflect_agent.ent` has no collision (shape 0) and no
  size, so the original tested the distance in script. The catch radius is
  half its 32 px sprite: 16 px, a guess.
- **How many.** The original's script keeps a `hasBeenReflected` flag, not a
  count. So a shot comes off at most one reflector, and `max_reflections` is
  1. This also rules out a shot bouncing between two reflectors forever. The
  reflector a shot last came off cannot turn it again at once, whatever the
  limit.
- **What comes first.** Along each tick's stretch of flight, the first thing
  met decides: a body or a blocker ends the shot, and a reflector turns it.
- **Reaching them.** All nine of chapter 1's reflectors lie inside their
  level's bounds, so the camera, following the player, can show each of them.
  Level 1-20's is 8 px from the left edge, and level 1-21's is at y 360 of
  512. Whether 16 px is enough to hit them in play is the owner's to judge.

`Portals::Find` reads each `reflect_agent`'s position and plane. The reflector
role is now ported.
- **A reflector with no plane.** The game has 22 reflectors: 13 vertical, 8
  horizontal, and one with no plane. That one is `reflect_agent_ent_961`, on
  level 2-27 (`level26a`). `reflect_agent.ent` carries no default, and the
  remake reads a missing plane as vertical. So `shot.json` gives
  `default_plane` "vertical", as a guess, and `test_mp_shot` tests it both
  ways.
- **Found by the inventory.** The first run of this step refused that whole
  level over its one missing plane, and chapter 2 fell to 31 levels starting.
  Its floor caught the drop.

`test_mp_shot`, now 90 checks:
- **The files.** The reflectors of level 1-12 (three, horizontal) and level
  1-13 (one, vertical) are pinned.
- **In an empty plane.**
  - A shot off a horizontal reflector opens its portal at the tap mirrored
    across the line where it met the reflector. Its way is exactly as long as
    the one fired. With the reflector taken away, the same shot opens its
    portal at the tap.
  - Two facing reflectors are tested with one reflection allowed and with
    two. The shot lands where each rule says: at x -32, and at x 184.
- **The circle test on its own.** It meets a circle where the arithmetic says,
  never passing 11 px off a 10 px circle or stopping short of one, and at once
  when it starts inside.
- **Level 2-27's plane-less reflector** takes the default either way.

The inventory after step 10: chapter 1 has 32 levels starting and **31
playing**. Levels 11, 12, 19, 20, 22 and 28 all play now, and only the boss,
level 31, is left. That counts roles, not routes: none of the six has been
played through, and the owner will play them to judge the reflection rule.
Chapter 1's floor rises to 31. Chapters 2 to 4 are unchanged.

**The floor of 31 rests on the reflection guess.** Six of those levels depend
on a rule the owner has not confirmed. If the owner finds the rule wrong, they
may have no route at all, and this floor will still pass: the inventory counts
roles, not routes (step 8's known gaps).

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_shot | 90 | 0 |
| test_mp_start | 496 | 0 |

The other MP suites are unchanged from 9d.

## Step 11a - the portal traversal, as the original does it (built)

The boss (step 11b) is won by the owner's own move: "I would put a portal under
him and then one under the falling rock and if the rock enter my portal, it
would exit under the beholder and hit him from under." Under the port's rule,
which was the remake's, a rock that fell in came out still falling. The remake's
portals.json marks that rule a guess, every field of its transit block.

**The original's script can now be read whole.** The remake's tools/asbc stops
at byte 13,028 of android_game.bin's 523,826. Its encoded-integer reader shifts
the leading bits twice. as_restore.cpp:1310 takes `(b & 0x3F) << 8` and adds the
next byte; the remake's reader shifts those bits and then shifts them again. So
every value from 64 up comes out wrong, and the first one desynchronises the
stream. With that one line fixed, in a copy outside both repositories, the
reader gets through the whole module to its last byte:
- 2,096 functions;
- 1,278 used functions;
- 878 strings;
- 174 globals.

The remake's tool is the owner's to fix; this records the bug and its line.
Neither the decoder nor its listings are committed: the listings are the
original's compiled code.

What it says of a traversal:
- **teleportToOther** (PortalManager.angelscript, bytes 144944..145873) puts the
  traveller at its partner's own position, `SetPositionXY(other.GetPositionXY())`.
  There is no offset.
- **invertLinearVelocity** (Portal.angelscript, bytes 360172..360571) then turns
  its velocity back. A body's is multiplied by -1. A character's is multiplied by
  (1, -1): it keeps its x.
- There is no speed scale.

So a rock that falls into one portal comes out of the other rising, as fast as
it fell. That is the owner's move, and it confirms the reading.

Also read, and not changed here:
- **computePortalFinalPos** (Portal.angelscript, bytes 355236..356384). The
  portal opens at the tap when the ray there is clear, 10 px into a teleportable
  body in the way, and not at all past a static one. That is what the port does.
- **The entry.** An EntityGrabber at the portal, g_portalCollisionRadius wide,
  meets a circle by its radius and any other shape by overlap. The port's trigger
  circle is the same test.
- **Two differences, for a later step.** Neither gates the boss, and both touch
  every level.
  - g_portalCollisionRadius is 14 px; the port reads the remake's guess, 16.
  - ETHCallback_portal kills a placed portal 20,000 ms after it opens, unless it
    is marked dontPerish. The port keeps it.

**What the port does now.**
- The port's own transit.json carries the original's rule: momentum_mode
  "invert", exit_speed_scale 1, exit_offset_px 0. Game::LoadData reads it in place
  of the remake's transit block.
- Portal::ExitVelocity has the invert mode, and is told whether the traveller is
  the player.
- The lockout, 0.2 s, stays the remake's guess. Nothing in teleportToOther
  waits, and whatever else stops a traveller going straight back is not decoded.
- The port's entries are edges, so a traveller put down inside its exit does not
  go back until it has left.

Tests:
- **test_mp_portal** pins the invert arithmetic: a body turned back whole, a
  character in y only, out at the exit itself. It pins transit.json's three
  decoded values too.
- **test_mp_launchers** now checks, on levels 23 and 24, that the stone comes out
  of the far portal at it, rising, as fast as it fell in. Each wall still breaks:
  the stone goes up, and comes down again past where it came out.
- **test_mp_play**: the player comes out at the exit itself, still walking right.
- **test_mp_statics**: a stone through level0's static pair goes once and does
  not come back. Static portals stay, so that is where a traveller could go back
  and forth; a placed pair is spent at once and could not show it.

Everything else passed unchanged, with times moved. level0 holding right now
completes in 3.20 s; it was 3.07. Level 1-9 (level8) with two shots completes in
4.00 s; it was 3.88.

**Routes are not re-proved.** The inventory is unchanged: 31 playing in chapter
1. It counts roles, not routes. A level whose designed solve needed a body to
come out still falling may now need another. None of the suites' routes broke,
and the rest are unplayed. This joins the reflection caveat.

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_portal | 37 | 0 |
| test_mp_launchers | 95 | 0 |
| test_mp_statics | 68 | 0 |
| test_mp_play | 133 | 0 |

The other MP suites are unchanged in count.

## Step 11b - chapter 1's boss, the beholder of level 1-32 (built)

The remake built a boss's structure and said plainly that it was a placeholder:
its attack patterns were "bytecode we can name but not read". They can be read
now (step 11a), so the port plays what ETHCallback_beholder
(assets/Beholder.angelscript, bytes 374987..380209) does. boss.json carries every
number with the bytes it came from.

**What the script does.**
- **Spawning.** level31's `adder` marker names beholder.ent in its custom data,
  and ETHCallback_adder (Adder.angelscript, bytes 372581..372886) puts that
  entity where the marker stands: (729, 80).
- **Its life.** BEHOLDER_MAX_HP is 2 and it dies when its hp is -1 or less, so it
  takes three hits. Its colour is (1, hp / 2, hp / 2): it reddens with each.
  BEHOLDER_RADIUS is 48.
- **Seeking** (BEHOLDER_STATE_SEEKING). It aims at its own x plus the player's
  offset clamped to 64 px, at its start height plus 8 px times
  cos(elapsedTime + PIb), and goes there by followUp: a PositionInterpolator over
  2000 ms eased by smoothEnd, sin(v * PIb), taken up again from where it has got
  to once 1000 ms have passed. When the player has been within 32 px of its x for
  more than 400 ms it shuts its eye.
- **Throwing rocks** (THROW_ROCK), for more than 7900 ms. overTimeEntityAdder
  drops rolling_stone.ent at the player's x and y -32 every 2000 ms. That clock
  is never reset between rounds, so a later round's first rock comes early and
  the round can hold four. It is the original's, and the port keeps it.
- **Being hurt.** A rolling stone within its radius whose velocity against
  (0, -1) is above 0 - rising - breaks and takes an hp. That is the owner's move,
  and step 11a is what makes it possible.
- **Hurt** (GOT_DAMAGE), for more than 7200 ms. Every 1200 ms it fires
  beholder_spike.ent at every 40 degrees from 0 to 360 inclusive, clockwise from
  up, the next ring turned by 10: ten spikes, two of them the same way. Each
  flies at 140 px/s through everything, is deleted off screen, and kills a
  character whose size times 0.7 holds its point.
- **Touching it.** The player within its radius has its hp set to 0.
- **Dead** (DEAD). After more than 3550 ms it explodes - `explode`, radius 80 px,
  which kills the player in it - level31's button is put at its `button_dest`,
  and the beholder goes. The button starts at (565, 244), buried in the floor
  where nothing can press it, and rises to (528, 220). It opens door_lift_748,
  which stands between the player and the exit. That the risen button opens the
  door and the exit is then reached is tested; that the door cannot be got past
  some other way - over it through a portal pair, say - is not.
- **Its rocks.** overTimeEntityAdder marks each `destroyOnStaticHit`, and
  ETHBeginContactCallback_rolling_stone (MiscCallbacks.angelscript, bytes
  346486..347456) breaks such a stone on anything static it runs into and kills a
  character it hits with an intensity above 2.5 - its velocity along the line
  between them, in Box2D units of 50 px.

**The owner's account, 11 September, against the script.** They agree but for
the spikes.

| The owner | The script |
|---|---|
| Three lives, and it reddens with each hit | hp 2, dead below 0: three hits; colour (1, hp/2, hp/2) |
| Rocks sent back through portals hit it from below | a rising rolling stone within 48 px |
| Three rocks, one at a time | 7900 ms of one every 2000 ms |
| A button rose when it died | button put at button_dest |
| Five spikes, at random, every 1.5 s | ten, 40 degrees apart, turned 10 every other ring, every 1.2 s |

The script is followed, and the difference is written into boss.json.

**What the port does.** sim/Boss.hpp and Boss.cpp, in the original's own order:
its time, its death, the phase it began the turn with, what is around it, then
that phase's turn. A hit changes the next turn's phase, as the original reads its
state before it looks around.
- **In the tick.** Before the step, where each rock is and how fast it goes.
  After the step and before the portals, what its rocks ran into; after the
  portals, its own turn, so it looks at where they left its rocks.
- **Touching, for its rocks.** Judged by the port's own overlap tests, as the
  demolish rule is: where the rock is, and where it would have got to had nothing
  stopped it. The engine's solver stops a fast body short of what it is closing
  on (speculative contacts), and a rock falling on the player at 571 px/s was
  measured left 1.0 px above its head, where Box2D's BeginContact would have
  fired. So a rock counts as touching within 2 px: a port threshold, marked a
  guess in boss.json, and not the demolish margin.
- **A rock a portal holds** goes through it rather than landing.
- **The button** is raised by moving both the Puzzle box that is pressed and the
  body the level's picture follows.
- **Whatever the boss kills the player with** comes out as a hazard death, which
  the layer already retries on.
- **Its spikes** are culled at the level's extent grown by the original's 16 x 128
  margin. The original culls on the screen; the port's level has no camera.
- **The role.** `boss_spawn` is played when Boss::Plays says the port has that
  boss: an adder naming beholder.ent. The other four boss markers, in chapters 2
  to 4, stay inert.

**Not ported**, and recorded rather than invented: the sounds, the earthquake,
the smoke and the explosions' pictures, the final blast's line of sight and its
effect on anything but the player. The crush belongs to every rolling stone in
the original; the port gives it only to the beholder's, so that step 6's levels
are not re-litigated on a threshold whose unit conversion is inferred. **That is
a correction step 6 owes**, and the owner's word on whether stones crushed the
character would settle it.

**Drawn as the original draws it** (art.json). beholder.ent's two frames - the
eye open, and shut while it throws rocks - reddening with its hp and pulsing as
bounce() has it in each phase: (1, 1.02) and (1.02, 1) over 400 ms seeking,
(1, 1) and (1.25, 1.25) over 600 ms hurt, (1.1, 1.1) and (1.25, 1.25) over 400 ms
dead. Its CustomData scale, 0.75, is replaced on its first turn, because bounce()
calls SetScale, which sets the scale outright (ETHEntity.cpp:601). Its spikes are
turned to where they fly and stand on their pivot; its rocks are drawn as stones.

Tests. **test_mp_boss** pins boss.json against the script, and level31's
beholder, button and button_dest. On a bare rig: the glide and its wobble, the
rounds of rocks and the clock they carry (three, then four), a rising stone
hurting it and a falling one not, three hits and the button rising 213 turns
later, the rings of ten and their turn, the spike kill and the safe ground under
it, the radius kill, and its rocks breaking and crushing. Then level31 itself: a
rock dodged breaks on the floor, one stood under crushes, and the level is won as
the owner won it.

**Level 1-32, played.** Three portal pairs in the air, three hits, the button,
the exit. One thing the level teaches: near the door the beholder floats in front
of wall04, which reaches down to y 106 over x 544 to 608, so a rock rising under
it there breaks on the wall. The player has to draw it left first. The suite
prints the run:

```
level31 as the owner won it: 3 hit(s), 3 traversal(s), 6 portal(s), the button pressed, completed
```

The inventory after step 11b: **chapter 1 has 32 levels starting and 32
playing**, and the port plays every level of the chapter. That counts roles, and
for level31 one played route. The other 31 levels' own routes are still unplayed,
and the reflection caveat above still stands.

GCC 13.3 and MSVC 14.50 agree to the digit:

| Suite | Checks | Failures |
|---|---|---|
| test_mp_boss | 126 | 0 |
| test_mp_sprites | 94 | 0 |
| test_mp_layer | 129 | 0 |
| test_mp_start | 496 | 0 |

level31 renders on lavapipe under xvfb: the beholder's eye hangs in its archway,
and the dark mage stands on the floor below it.

## What the decode leaves for later steps

The whole script reads now, so what follows is fact rather than guess, and none
of it is changed here.
- **g_portalCollisionRadius is 14 px, and it is NOT the antiportal's radius.**
  This bullet used to read "the port reads the remake's guess, 16", which
  conflated two unrelated quantities. The port's `collisionRadiusPx` drives both
  a placed portal's own trigger AND the radius in which an antiportal refuses a
  tap, and only the first is what the constant governs.
  - **A portal's own radius is 14** (`Portal.angelscript`, bytes 297250..297291;
    `PortalManager` stores `getScale() * 14`). One scale unit is one port pixel -
    `preLoop` calls `updateScaleFactor(256)` and the port's view is 256 px tall -
    so it is 14 port pixels.
  - **An antiportal's radius is half its own sprite, scaled**, and comes from the
    entity rather than from any constant. `isPointInAntiPortalField`
    (`AntiPortalManager.angelscript`, bytes 334800..335020) is
    `squaredDistance(field.GetPositionXY(), p) < r*r` with
    `r = GetSize().x * 0.5`, and a dead field refuses nothing. Read out of the
    engine source, which is the only trustworthy spec here:
    - the script's `GetSize` binds to `ETHScriptEntity::GetCurrentSize`
      (`ETHScriptObjRegister.generic.cpp:222,358`) - a rename worth knowing, as
      no `GetSize` member exists;
    - `ETHSpriteEntity::GetCurrentSize` (`ETHSpriteEntity.cpp:652`) returns
      `m_pSprite->GetFrameSize() * m_properties.scale`;
    - `antiportal.ent` draws `white_ring.png` with `SpriteCut 1x1`, and that file
      is **128 x 128**, so the frame is the whole image;
    - `SGlobalScale::scaleEntity` multiplies `m_properties.scale` by the node's
      own `scale` custom data, and the scene-wide pass that calls it treats an
      entity with no `scalable` key as scalable, which every antiportal is.
  - So the refusal radius reads as **64 x the node's scale** in the port's
    pixels, where the port uses 16 x scale: a quarter of it, on the 45 levels
    that carry an antiportal (64 nodes, every one carrying a `scale`, from 1.3
    to 13).
  - **NOT APPLIED, and the port still plays 16 x scale.** One thing in the level
    data argues the other way and no file can settle it. level6's
    antiportal_687 is at (288, 192) with scale 3; at 64 its field covers
    x 96..480 of a level spanning x -64..576, and the projectile blocker
    anti_projectile_wall_ent_743 at (313, 185) falls entirely inside it. A wall
    that stops a shot is pointless where no portal may be placed anyway.
    test_mp_shot's blocker case says the same as data: with the blocker removed
    its shot must land at (380, 150), 101 px from the field - outside at 16 x 3,
    inside at 64 x 3. Changing it turned that suite and test_mp_movers red, and
    those assertions are evidence rather than chores, so the number stayed.
    placement.json carries the decoded 64 beside the 16 it plays.
  - **What would settle it** is the owner playing the original: how big the white
    ring is on screen next to the character, and whether a portal can be placed
    just outside the blocker in 1-7.
  - It is also why the ring looks wrong. In the original the ring is DRAWN at
    `128 x scale`, so the picture and the rule are the same circle: what you see
    is the field. The remake draws it at a flat 128 px and refuses within
    `16 x scale`, and the port followed it - which is the "Scale" item under step
    9's "what is not drawn".
  - Not modelled either: a destroyed antiportal (`antiportal_explosion.ent`)
    refuses nothing, and the original's test is strict (`<`) where the port's
    is `<=`.
- **A placed portal perishes 20,000 ms after it opens** unless marked
  dontPerish (ETHCallback_portal). The port keeps its portals.
- **Every rolling stone crushes a character** it hits hard enough, not only the
  beholder's (step 11b, above).
- **DEFAULT_GRAVITY_SCALE is 2**, against the remake's 980 px/s^2 world.
- **174 named globals** are readable, among them the timings and radii that other
  steps carry as guesses.

Each touches levels that already play, so each wants its own step, with all
fifteen suites re-run.

## Step 12 - what playing it found (12 September)

The owner played the port for the first time, from level 1-1. Three reports, and
the first was a bug no suite could have caught.

**Level 1-2's portal led nowhere.** The player places their one portal, walks
into it, and passes straight through. The port paired a placed portal only with
another placed portal (`Portals::Tick`), so on a level granting a single
placement there was no partner and no traversal.

The original does it in `PortalManager::doTeleporting` (bytes 139692..140259),
which branches on the level's budget:
- `maxPortals == 2`: the pair, through `teleportToOther` - what the port had.
- `maxPortals == 1`: `teleportToFirstStaticPortal` (bytes 145999..146550). It
  takes `SeekEntity('portal_static')` - the first static portal in the level's
  own order - moves the traveller there, kills the placement, sets that static
  inactive, and flips velocity to (x, -y), which is the character rule step 11a
  already carries.

Both branches compare against an immediate, not a variable: `CMPIu` is encoded
`rW_DW_ARG`, so the disassembly's `v2` and `v1` operands are the literals 2 and
1. The port now takes the second branch when `budget == 1`, exiting at the first
live static portal. Consumption already erases the placement, and the static
persists, so nothing bounces back: with no placement left, entering the static
leads nowhere.

In chapter 1 only level1 grants exactly one placement. level0 and level14 also
ship static portals (four and two) but grant none, so the branch cannot fire
there. `test_mp_statics` covered this partnership in one direction only - the
static leading to the placement - which is why the suites were green while the
level was unplayable; it now runs both ways.

**Nothing in the surroundings moves.** True, and the reason is that the levels
carry nothing to move. Motion in the original is particle systems, held in each
entity's own `.ent` and dropped by the converter: level1.tscn has no frame data
at all. What chapter 1 would animate, by placements: crystal 114, door_bg 32,
light 19, reflect_agent 9, anti_portal_agent 8, portal_static 7 - 189 in all,
each with a `<ParticleSystem>` (crystal emits two sparkles on a 1700 ms life;
fire.png burns through a 4x1 cut). Separately 45 entities in the game carry a
multi-frame `<SpriteCut>`. Porting this means a particle system in the engine
and a reader for the `.ent` block. Not built, and not folded into a bug fix.

**No main menu.** Correct, and never claimed: the port starts at the level it is
given and prints that the chapter is complete at a world's end. The original's
`level_select0..3` scenes and its `WorldSelector` are there to build from.

Also this day: a refused level now says which one it was and why, in the log as
well as on the HUD. Only the very first failure was reported before, and only
when nothing had loaded, so a chapter that stopped part way left no record.

GCC 13.3 and MSVC 14.50 agree: 18 suites, 0 failures, `test_mp_statics` 74.

## Step 13 - the menu (built)

Asked for after the first play-through: the port had no front door, it started
at whatever level it was given. The original's own three screens, drawn with its
own art.

**Where the layout comes from.** The menus are built in script, not placed in a
scene - `level_select0.esc` holds one entity, `world_select_bg.ent` - so the
shape is decoded rather than converted:
- `WorldSelector`'s `PageProperties`: `numItems` 4, `columns` 2, `rows` 1, back
  at normalized (0.5, 0.05) and forward at (0.5, 0.95), buttons named
  `sprites/world_icon`, locked `sprites/lock_icon.png`, empty
  `sprites/level_soon_button.png` (bytes 167326..168612).
- `PageManager`: `buttonsPerPage = columns * rows`, `numPages =
  ceil(numItems / buttonsPerPage)` (bytes 57749..58780); `PageProperties`' own
  defaults are `columns` 4, `rows` 3, so the level grid is twelve to a page.

The port shows all four worlds at once rather than two to a page: a window is
not a phone, and paging two icons would need the swipe (`Swyper`) it does not
carry. The page buttons sit just inside the edge, because the original's own
(0.5, 0.05) and (0.5, 0.95) put half a button off a landscape window.

**Drawn as quads, not UI components.** `UIImageComponent` takes an uploaded
texture handle and `UIButtonComponent` is a coloured rounded rectangle with a
text label; neither shows a PNG named by path, which is what every button here
is. So the menu draws the way the levels do, on the same orthographic camera,
and is clicked through the same screen-to-plane mapping - which is now not
gated on a level being loaded.

**Entry is unchanged.** No `--level` opens the menu; a named level is entered
directly. Every suite, every headless render and the owner's own command line
reach the game exactly as before, and a test pins it.

**What is not the original's.** NOTHING IS LOCKED: the original gates worlds
and levels behind a save its `ScoreManager` keeps, and the port keeps no save.
A port decision, not a decoded rule. Also left out: the swipe, the page
counter, the popups, the cheat button, and `Verdana64_shadow.fnt` - a level's
number is drawn with the engine's own text.

**A defect caught before building.** `PressMenu` took its button by reference,
and pressing one lays the screen out again - clearing the vector the button
lives in - before the level it names is read back out. It takes the button by
value; the click loop passes exactly such a reference.

`test_mp_layer` grew five: the walk from the main screen to a level, paging to
the second page's first level (1-13), Escape out of a level to its grid, a
named level skipping the menu, and a click landing on the button under it.

**Both backgrounds were missing, and nothing said so.** The first cut of this
shipped and ran with no background behind either screen. The original keeps its
menu art in two places - the title, the buttons and the icons under `sprites/`,
but `main_menu_bg.png` and `world_select_bg.png` among its `entities/`, beside
the `.ent` files that place them - and `menuImage` looked only in the first.

The wrong path is not the interesting half. `buildMenu` skipped an image it
could not read WITHOUT A WORD, so a menu with nothing behind it looked like a
menu meant that way, and no test could see the difference. Now the lookup tries
both directories, an unreadable menu image is logged, and the walk-to-a-level
test asserts the background and the title are actually drawn. That is the same
shape as the refused-level message earlier in step 12: a quiet path where
something could go missing and nothing reported it.

GCC 13.3 and MSVC 14.50 agree: 18 suites, 0 failures, `test_mp_layer` 152.

## Step 14a - what the entities' particle systems say (read)

Asked for with the menu: nothing in the surroundings moves. It does not move
because the levels carry nothing to move. The converter writes a level's
sprites and drops the rest of each entity, and the motion is not sprite
animation at all - it is a PARTICLE SYSTEM in each entity's own `.ent`, which
the converter never read. `level1.tscn` holds no frame data of any kind.

**What the files hold.** 81 of the original's 190 `.ent` files carry particle
systems, 102 systems in all, because 21 entities hold TWO - `portal_static`
among them, which is one of chapter 1's own emitters. A reader taking only the
first would draw half of every static portal and say nothing about the rest, so
`Particles::Load` returns all of an entity's, each parsed out of its own
`<ParticleSystem>...</ParticleSystem>` chunk: read per `<Particles>` block
instead, the first system's children answer for the second as well.

Every one of the 102 states the SAME seventeen attributes and ten child
elements, so the reader demands all of them and refuses a file missing one
rather than taking a default.

**Decoded, not guessed:**
- `alphaMode` is gs2d's `Video::ALPHA_MODE` (Video.h): `AM_PIXEL` 0, `AM_ADD`
  1, `AM_ALPHA_TEST` 2, `AM_NONE` 3, `AM_MODULATE` 4. Every emitter in the game
  is 1, so EVERY PARTICLE IS ADDED - which is also why `sparkles.bmp` works at
  all, a bitmap with no alpha channel whose black ground adds nothing.
- `animationMode` is `ETHParticleSystem::FRAME_ANIMATION_MODE`:
  `PLAY_ANIMATION` 1, `PICK_RANDOM_FRAME` 2. Crystals play their sheet by age;
  fire picks a frame at random.
- The files are UTF-16 LE with a byte-order mark, all 190 of them, so the
  reader decodes that itself. Neither `Tscn` nor `Json` would.
- The bitmaps live in a THIRD directory of the original's, `particles/`, beside
  its `entities/` and its `sprites/`. The suite checks that every bitmap named
  by all 102 systems is really there - the check that catches the mistake step
  13 made twice with art that then went missing in silence.

**What chapter 1 would animate**, by placements: crystal 114 (`sparkles.bmp`, 2
particles, 1700 ms), door_bg 32 and light 19 (both `fire.png`, 12 particles),
reflect_agent 9 and anti_portal_agent 8 (`onda.png`), portal_static 7
(`portal_particle.png`). 189 in all.

**Why not the engine's own particle system.** It has one, and it is the wrong
shape for this. Its pass binds ONE mesh, ONE pipeline and ONE material set for
every particle in the frame and deliberately bypasses `PlanPass`, with its own
comment naming the exit condition: "If a particle ever varies its material,
this is where the rule has to come from PlanPass rather than from here."
Ethanon's particles vary their bitmap per emitter on the first level -
`sparkles.bmp` and `fire.png` at once - so this would invert that pass's
premise in a subsystem HUSK and Wolf Brigade also render through. Its emitter
draws untextured cubes under a hardcoded gravity besides. The port already
draws hundreds of textured, additively blended, rotated quads a level, so the
particles will be drawn the way everything else in it is.

**A count I got wrong, and the test caught.** The first sweep asserted 102
files because the survey behind it counted `<ParticleSystem>` MATCHES rather
than files. The reader said 81; the reader was right about files and the survey
about systems. Both are now pinned, along with `portal_static` yielding two.

**Not built yet:** the motion. That is the layer's, per frame, with its own
generator and a capped pool per emitter - never in `sim/`, never on the tick,
never in the state hash. Ethanon's own arithmetic is the spec
(`ETHParticleManager::UpdateParticleSystem`, `ResetParticle`,
`PositionParticle`): `frameSpeed = min(elapsed, 250)/1000 x 60`, direction
gathering gravity, position gathering direction, angle gathering `angleDir`,
size gathering `growth` clamped to its range, colour lerped from `Color0` to
`Color1` by age, the frame by age or at random, release staggered across
`(lifeTime + randomLifeTime) x id/count` unless `allAtOnce`, and a reset on
expiry until the `repeat` cap.

GCC 13.3 and MSVC 14.50 agree: 18 suites, 0 failures, `test_mp_sprites` 147.

## Step 14b - the surroundings move (built)

The reader's systems, carried and drawn. A torch burns, a doorway glows, a
static portal turns, a crystal sparkles.

**Where it lives, and why that is the whole design.** In the layer, per FRAME,
in `OnUpdate`. A particle is a picture: nothing here touches `Game::Level`, the
fixed tick or the state hash, so the port's cross-platform determinism cannot
be moved by it. The test that matters says so directly - a level that has
ticked sixty times and never been drawn holds no particle at all. The generator
is the layer's own and seeded, rather than the engine's single process-global
stream that every other drawer shares, so a run looks the same twice.

**Ethanon's arithmetic, in its own units** (`ETHParticleManager`'s
`UpdateParticleSystem`, `ResetParticle` and `PositionParticle`): a frame capped
at 250 ms and turned into sixtieths of a second; direction gathering gravity,
position gathering direction, angle gathering `angleDir`, size gathering
`growth` clamped between `minSize` and `maxSize`; colour lerped from `Color0`
to `Color1` by age; release staggered across `(lifeTime + randomLifeTime) x
id/count` in pool order unless `allAtOnce`; a reset on expiry until the
`repeat` cap, and then that particle stops. Every spread is applied as the
original applies it - plus or minus half, but `randAngleStart` from zero.

**Drawn the way the port draws everything else:** one textured quad per live
particle, added rather than mixed because every emitter in the game is
`AM_ADD`, turned to its own angle, sized to its own size at its bitmap's
shape, and given its frame through `SpriteAnimationComponent` - by age for
`PLAY_ANIMATION`, chosen once at birth for `PICK_RANDOM_FRAME`.

**Bounded.** No emitter draws more than 64 particles whatever its `.ent` asks;
chapter 1's largest asks for 32, so it is a guard for a later chapter. A
collected crystal takes its sparkle with it, and unloading a level takes its
pools - a retry would otherwise stack a second on the first.

**What is seen, and what is not.** level1 renders with its torch alight, its
doorway glowing red and its static portal haloed, and the run loads three
bitmaps out of the original's `particles/` where it loaded none before. The
crystals of level26 are NOT resolvable by eye at 1280x720 - a 12 px sparkle
fading to transparent - so they are pinned by counting quads rather than
claimed from a picture.

**Not done, and deliberately:** an emitter sits where its placement sits and
does not follow a body that moves; `Luminance`/emissive and `boundingSphere`
are read but unused, so nothing is culled by its own bounds.

`test_mp_layer` grew two, to 157: a level's entities emit on frames and not on
ticks, and their particles go with the level.

GCC 13.3 and MSVC 14.50 agree: 18 suites, 0 failures, `test_mp_layer` 157.

## Step 20 - a game can draw geometry it built itself (built)

`MeshRegistry::Upload` and `Replace` are public, and their comments describe
exactly what they are for: "a mesh rebuilt every frame - fog, a dynamic terrain
patch, a debug overlay". A game could call them and could not draw the result.

`RenderableComponent::meshID` is not authored. `SyncResources` recomputes a
`ResourceSignature` from `MeshComponent`'s `primitiveType` and `filePath` every
frame and re-resolves the id from it, so an id written there by hand was
overwritten on the next frame. There was no field naming already-uploaded
geometry, which made the whole Upload/Replace pair unreachable from a game -
a capability the engine offered and could not deliver.

`MeshComponent::meshKey` names it. Resolution goes through `Find` rather than
`Acquire`, because the geometry already exists; the key is mixed into the
signature, because it selects a mesh as surely as a path does and is the one
input with no other trace on the entity - a component switched from one
uploaded key to another has the same primitive, the same path and the same
material, and a signature blind to the key would keep drawing the first
geometry for ever.

Two decisions worth stating:

- **A key naming nothing yet leaves `meshID` alone.** Falling back to the cube
  would put one on screen for every frame between an entity being created and
  its geometry being uploaded - at least one frame for anything built during
  the frame, and a cube nobody asked for in a game that may own none.
- **It is not persisted.** The geometry behind a key exists only because
  something uploaded it this session, so a saved scene naming a key nothing has
  built would resolve to nothing on load. `ComponentCodec` writes the primitive
  and the path and deliberately not this, like `importMaterialOnResolve`.

**A COVERAGE LIMITATION, recorded rather than left to be discovered.** No suite
can construct a `MeshRegistry`: it needs a device and a command pool, and
`test_tilemap` and `test_shadowcache` both record the same constraint for their
own walks. So what is tested is that the key reaches the signature
(`test_resourcesync`, which grew a case for it) - the half that decides whether
the new branch runs at all. The branch itself is proved by the game drawing
generated geometry, which is a weaker guarantee and worth knowing before
trusting it.

This was found while building the medal screen, which needs the original's
Matura lettering: glyph quads uploaded once under a key and `Replace`d as the
counter ticks. It is the second engine gap that screen turned up, after the
BMFont reader below.

## Step 19 - the engine reads a BMFont (built), and a correction to its case

`BitmapFont` parses BMFont's text `.fnt` and turns a string into glyph quads
over the font's page, which a caller uploads through `MeshRegistry` and draws
like any other textured quad. No rasteriser, no pipeline, no layout engine, no
ImGui. `test_bitmapfont` writes its own descriptors, so it never touches the
original's Matura fonts - those are Asantee's and stay outside this repository
with the art and the sounds.

**A CORRECTION TO THE COMMIT MESSAGE, recorded because it overstated the case.**
It says world-space text "could not be drawn AT ALL, by any game on this
engine". That is not true, and this port disproves it: the level grid's number
labels already use `UITextComponent` with `worldSpace = true`, and have since
the menu was built. World-space text existed.

What did not exist is world-space text IN A FONT THE GAME SUPPLIES.
`UITextComponent` draws through ImGui's atlas, so it draws ImGui's typeface; the
finish screen's whole point is the original's Matura lettering, which no path
could produce. That is still a real gap and still worth the engine filling, but
it is a narrower one than the message claimed, and the difference matters: one
is "the engine cannot draw text in the world", which is false, and the other is
"the engine can only draw text in its own font", which is true.

## Step 18 - why things disappeared: every quad was culled as a POINT (fixed)

`ModelLoader::GenerateQuad` filled in its vertices and indices and returned
without calling `computeBounds()`. `MeshData` defaults its bounds to
(0,0,0)..(0,0,0) and `clear()` - which that function calls first - resets them
there. So every quad this engine has ever generated carried a DEGENERATE POINT
as its local AABB. `createGpuMesh` copies it onto the `GpuMesh`;
`RenderSystem.cpp:332` copies it onto the renderable every frame, ungated. The
frustum then tested each quad AGAINST ITS OWN CENTRE.

A sprite therefore vanished the instant its centre crossed the edge of the
view, while the quad itself was still metres on screen - and the bigger the
sprite, the sooner it went, because its edges reach furthest from that one
point. `GenerateCube` and `GeneratePlane` both call `computeBounds`;
`GenerateQuad` was the only primitive that did not. It also skipped
`computeTangents`, which `GeneratePlane` calls.

**This is an ENGINE bug, not a port one.** Any game drawing `Quad` primitives
was affected. Magic Portals surfaced it because it draws almost nothing else.

**What it explains**, including every symptom that killed an earlier theory:

- scenery disappearing MID-SCREEN, which no amount of panning accounts for;
- the largest background art going first, and small central things surviving;
- returning depending on where the player walks - it is a pure function of
  camera position;
- the collision boxes blinking with the art: they are quads too;
- validation silent, `dropped` zero, `drawn`/`culled` looking healthy - the
  Vulkan usage was always correct; the geometry handed to the cull was not.

**How it was found, and why it took so long.** Seven causes were put forward and
the evidence killed all seven: wrong culling, the sprite reader ignoring
`position`, a recycled entity handle, a shared instance buffer, a frame-sync
race, pre-interpolation world matrices, and a jumping camera. Two experiments
did the real work. One frame in flight - `MAX_FRAMES_IN_FLIGHT = 1` - did not
stop it, which eliminated the whole frame-parallelism class in a single
observation. The validation layer, once actually installed and genuinely
loaded, reported nothing about this engine's rendering across a live
reproduction, which cleared the API usage.

What finally caught it was instrumenting `RenderSystem`'s own gather loop,
where the camera, the world matrix and the draw decision are the same frame by
construction. Every earlier diagnostic sampled the wrong moment: the layer
reads the camera in `OnUpdate`, which runs BEFORE the world transforms are
resolved, and printed it beside the PREVIOUS frame's counters - two frames on
one line, which cannot show this. The instrumented run logged 17,995 culled
sprites and every single one had IDENTICAL min and max.

**Why the tests were blind to it, which is the part worth remembering.**
`test_mp_layer`'s cull check reported "8935 sprite-ticks on screen, 0 culled"
and was quoted as exonerating culling. It builds its own registry, never runs
`SyncResources`, and so reads `RenderableComponent`'s own -0.5/+0.5 defaults
rather than the mesh's bounds - it was measuring unit boxes the game never
used. A test that shares the code under test's own wrong assumption agrees with
it. That is also why those numbers do not move now that the bug is fixed.

`test_meshgen` had bounds assertions for the cube and for the box and NONE for
the quad, which is the exact shape of the hole this fell through. It now asks
the question of every generated primitive at once - no primitive may have more
than one flat axis, and every vertex must lie inside its own bounds - plus the
quad's own numbers stated directly. Both toolchains: 447 checks, 0 failures.

Three other engine defects were found on the way and fixed:

- `RenderSystem::Stats` never carried `PassPlan::dropped`. Refused draws were
  counted and thrown away, so a frame that silently declined to draw part of
  the scene was indistinguishable from one that drew all of it.
- Last frame's render counters were visible to the editor's statistics panel
  and to nothing else, so a GAME could not tell whether the frame it just drew
  had culled or refused anything. Published into the registry context now,
  beside `MeshRegistry` and `TextureRegistry`.
- `VulkanContext::ValidationLayersActive()` reported INTENT rather than
  reality: it was set whenever the debug messenger was created, and
  `VK_EXT_debug_utils` is an extension the loader provides with or without the
  validation layer. A Release build printed "validation ACTIVE" while writing
  "Continuing WITHOUT validation" to stderr in the same run. It checks the
  layer itself now.

## What the sounds decode to (read here, built in step 17)

The owner asked for sound after playing. The port has never had any. The
original ships 46 mp3s in its `soundfx/`, and `AudioManager` names every one of
them - so this is decoded rather than guessed, and written down here before it
is built because getting at it took three wrong turns.

**Two kinds of hook.** Twenty name their file as a literal string - the
beholder's, the dragons', the ghosts', the shock diamonds', which is to say
chapter 2 and after. The rest index a global array, `g_sfxNames`, which
`loadSounds` walks to preload the lot.

**`g_sfxNames`, in its own order** (30 entries, bytes 307518..308601):

```
 0 button          1 explosion_small   2 portal_fail      3 portal_reflect
 4 demolition      5 fireball          6 portal_killed    7 teleport
 8 explosion_huge  9 portal_created   10 portal_launch   11 final_door_sound
12 stone_hit      13 elevator         14 burn            15 wood01
16 wood02         17 door_open        18 fall            19 projectile_reflect
20 magical_sweep_08  21 dark_whoosh_17  22 wood_drag     23 crystal_gather_G
24 crystal_gather_E  25 spike_hit     26 crystal_temp_alert  27 enemy_spotted
28 flesh_heavy_impact_03  29 orchestral_transition_stinger_06
```

**CORRECTED: that array is 30 entries, not 35, and the music is a different
array.** This section first listed five more names at 30..34 - the four music
loops and the rocket - as though they were the tail of `g_sfxNames`. They are
not: the initialiser allocates the array with `PshC4 30`, and the five tracks
are built by their OWN function (bytes 308620..308853) into `g_musicNames`:

```
 0 running_with_wolves_loop_b   1 finding_wonderland_60_loop
 2 warlords_loop_a              3 rocket_space_shuttle_rocket_distant
 4 back_to_nestopia
```

Two arrays, two initialisers, indexed by different hooks - and the mistake
mattered, because every music index would have been read five past the end of
the wrong array. The four `get*MusicName` getters settle which is which:
`getBossMusicName` takes `g_musicNames[0]`, `getGameMusicName` [1],
`getMenuMusicName` [2], `getRocketMusicName` [3].

**The two arrays plus ten literal names account for 45 of the 46 files.** The
forty-sixth is `crystal_gather.mp3`, named nowhere in the program: a leftover
beside the `_G` and `_E` variants that replaced it.

**How the order was checked, because the first reading was wrong by six.** The
array's initialiser sits in a block the decoder labels `MouseCursor::getName` -
a global init has no symbol of its own and borrowed a neighbour's - and a first
pass swept in six strings from the blocks beside it, putting every index out by
six. Two hooks settle it: `playDoorOpenSound` indexes 17, which is `door_open`,
and `playCrystalPickSound` computes `rand(1) + 23`, which is the two
`crystal_gather` variants side by side. Both land exactly, and only at this
offset.

**What chapter 1 would use**, and these read true - the hook's name and the
file's agree:

| Hook | File |
|---|---|
| playPortalCreatedSound | portal_created |
| playPortalLaunchSound | portal_launch |
| playPortalFailedSound | portal_fail |
| playPortalKilledSound | portal_killed |
| playTeleportSound | teleport |
| playCrystalPickSound | crystal_gather_G or _E, at random |
| playCrystalTempAlertSound | crystal_temp_alert |
| playDoorOpenSound / playDoorCloseSound | door_open |
| playFinalDoorSound | final_door_sound |
| playElevatorSound | elevator |
| playStoneHitSound | stone_hit |
| playDemolitionSound | demolition |
| playFallSound / playDieByFallSound | fall |
| playExplosionSound | explosion_huge |
| playDeathSound | dark_whoosh_17 |
| playVictorySound | magical_sweep_08 |
| playBeholderDamageSound | beholder_damage (a literal) |
| playBeholderSpikesSound | spikes (a literal) |

**SETTLED, by reading what reaches `PlaySample`.** The table above was built by
taking the index on the line before the array is pushed, and that is the wrong
rule: a hook sets the sample's SPEED on one entry and PLAYS another.
`playWoodDragSound` is the proof - it sets the speed on entry 2 and plays entry
22, `wood_drag`, which is what its name says. Keyed on the play instead, all 61
hooks resolve and the names agree with the files throughout.

Three corrections to the table above, and one addition:
- `playWoodDragSound` plays `wood_drag`, not `portal_fail`. A random speed
  between 0.5 and 2.0, rate-limited by a 200 ms timer.
- `playDoorUnlockSound` really is `crystal_gather_G` at half speed, and
  `playCrystalVanishSound` really is `spike_hit`. Surprising, and what the
  bytecode says.
- Some hooks play TWO samples: `playExplosionSound` is `explosion_huge` and
  `explosion_small` together; `playLightOffSound` is
  `reverse_magical_sweep_08` and `portal_created`; `playLightOnSound` and
  `playRoundaboutSound` are `magical_sweep_08` and `portal_created`.
- Only two compute their entry: `playCrystalPickSound` takes `rand(1) + 23`,
  the two `crystal_gather` variants, and `playRandomWoodSound` the two `wood`
  ones.

**What blocked it was the engine, not the decode.** The audio loader was
WAV-only - `AssetDatabase` lists `.wav` alone, `AudioSystem` re-reads `.wav`,
the default clip is `ambient.wav` - and the original ships 46 mp3s, 3.3 MB.
There was no mp3 decoder in `third_party` and no ffmpeg, sox or anything like
them on this machine.

## Step 16 - the engine decodes MP3 (built)

The owner chose the decoder rather than converting the files. Media Foundation
does it: it ships with Windows, so it costs no third-party dependency, and it
mirrors the bargain `AudioEngine` already strikes - XAudio2 is a real backend
on Windows and a documented no-op elsewhere, and now decoding is too. It also
leaves the original's mp3s where they are, with no converted copies of somebody
else's assets lying about, which is the rule the art has followed throughout.

Vendoring `dr_mp3` or `minimp3` would have been the portable answer and was not
available: neither is on this machine, and several thousand lines of somebody
else's decoder is not something to type out or fetch blind.

- `AudioClip::LoadMp3` reads through an `IMFSourceReader` configured for
  interleaved 16-bit PCM, so MF inserts its own decoder and the clip contract
  is unchanged. It holds the same limits `LoadWav` states - one or two channels
  - and refuses a file that changes format part way through rather than mixing
  two rates into one buffer.
- `AudioClip::Load` picks the loader by extension, and `AudioEngine::LoadClip`
  goes through it. A name the engine does not read is refused BY NAME, rather
  than handed to the WAV parser to report "not a RIFF/WAVE file" and send
  whoever reads that looking in the wrong place.
- Media Foundation is started once per process and never shut down, which is
  deliberate: `MFShutdown` is per-process, and one thread decoding while
  another shuts the platform down is a crash inside somebody else's library.
- CMake links `mfplat`, `mfreadwrite` and `mfuuid` beside `xaudio2` on Windows.

**The cost, stated plainly: there is no sound on Linux.** `LoadMp3` there fails
with a reason saying Media Foundation is Windows only. If sound on Linux is
ever wanted, that is when a vendored decoder earns its place.

**Tested on both halves of that split**, which is the whole reason both
toolchains matter here:
- `test_audio` gains two device-free cases - an unknown extension refused by
  name, and a missing `.mp3` failing with a reason rather than crashing. Its
  floor rose from 47 to 54 with them: a floor left behind by the tests it
  guards stops guarding.
- `test_mp_sprites` decodes one of the original's own files, `door_open.mp3`.
  On Windows it asserts a valid clip, one or two channels, a rate, 16-bit and a
  duration above zero; elsewhere it asserts the refusal. That difference is
  visible in the counts: 155 checks on MSVC against 152 on GCC.

GCC 13.3 and MSVC 14.50 agree: 18 suites, 0 failures, `test_audio` 69 and 72
(the voice cases need an output device), `test_mp_sprites` 152 and 155.

**The port still played nothing at this point.** The engine could decode the
original's sounds; wiring the hooks to the events is step 17 below.

**Details worth keeping.** Volume and sample speed are set before every
`PlaySample`; `playDoorOpenSound` scales the speed by `3000 / doorOpenStride`,
so a slow door is a slow sound; crystal pickups are rate-limited by a 50 ms
timer, so a run of them does not stack; `playRandomWoodSound` and
`playCrystalPickSound` compute their index rather than stating it.

## Step 17 - the port plays them (built)

The owner asked to hook all the sounds, and said there was time to do it
properly. So the whole `AudioManager` is carried, not the dozen hooks chapter 1
happens to reach.

**The table is data, in `games/magicportals/data/sounds.json`**, read by
`sim/Sounds.hpp`. Three parts: `hooks` is the original's AudioManager function
by function, with the files, volume, sample speed and rate limit each sets;
`music` is the five tracks; `events` maps what the PORT can watch happen to one
of those hooks. A hook no event names is carried anyway, so the table stays the
original's rather than being trimmed to what today's port can see.

**Four corrections to the decode above, all found by reading the getters** that
the first pass skipped because they return a name instead of playing it:

- `getButtonSoundName` returns entry 0, `button.mp3`. That is the framework's
  Button sound - a MENU button, and the only thing in the game that plays that
  file. The port had it mapped to `portal_killed`, which was a guess.
- `getRetryButtonSound`, `getRestartLevelButtonSound`, `getExitLevelButtonSound`,
  `getSkipLevelButtonSound`, `getNextLevelButtonSound`,
  `getItemSelectButtonSoundName` and `getStartAchievementSoundName` all return
  entry 7. Every button a level puts up sounds like a teleport.
- `playCrystalTempAlertSound` reads the SAME `AudioManager.crystalSoundTimer`
  as `playCrystalPickSound`. It is one 50 ms gate across both, not one per
  hook, so the two hold each other off rather than each holding itself.
- `playAchievementPickSound` plays entry 0 and `playLightSwitchSound` is a bare
  call to `playKeyUnlockSound`. Both were missing.

**The music, which the earlier pass did not mention at all.**
`playMusic(name, volume, loop)` loads the track, sets its looping and volume,
plays it - and does nothing at all while the music switch is off. The menu is
`warlords_loop_a` at 1.0, looping. A level is `playGameMusic(isBossFight)`:
`running_with_wolves_loop_b` at 0.6 for a boss, `finding_wonderland_60_loop` at
0.7 otherwise, both looping. `setDefaultMusicVolume` derives the same two
numbers independently, which is a clean second reading of them.

**The port latches on the tick and plays on the frame**, which is the particles'
division and for the particles' reason. `latchSimSounds` diffs the simulation
against what it looked like last tick - counters that went up, flags that turned
over - and pushes event NAMES; `OnUpdate` resolves each to a hook and plays it.
So no clip, no random draw and no missing file can reach `Game::Level`, the
simulation's clock or the state hash, and a run with no audio device takes
exactly the same path through the simulation. That is not a nicety: it is how
every suite runs, and it is what keeps a replay a replay. The random draws get
the layer's own seeded generator, beside the particles' and for the same reason.

**Divergences, stated rather than implied:**

- In Ethanon a sample's speed and volume are persistent state on the NAMED
  sample, so a hook changes how every later hook that plays the same file
  sounds. That is why `playVictorySound` sets the speed back to 1.0 - the light
  and roundabout hooks leave `magical_sweep_08` at 0.6 - and why
  `playDoorUnlockSound` inherits whatever volume the crystal pickup last set.
  The engine's `Play()` takes volume and pitch per voice, so each hook is played
  with what it sets and nothing bleeds. The bleed is a quirk of the original's
  mixer, not a sound anyone designed, and it is not reproduced.
- XAudio2 gives a source voice a frequency ratio of 2 unless it was created to
  allow more, so a door quicker than 1500 ms would ask for more than the voice
  can do. The pitch is clamped rather than left to fail; the original has no
  such ceiling.
- The port has no `MusicSwitchManager`, so its music plays unconditionally.
- And still, from step 16: there is no sound on Linux at all.

**A looping voice is never "finished"**, so `ReapFinishedVoices` leaves it alone
for ever by design. The layer holds the music voice and stops it itself, on
every track change and in `OnDetach` - without which menu to level to menu would
leave two tracks playing over each other.

`test_mp_sounds` is new. It tests the reader on files it writes (refusing two
files with no word on whether they play together or one at random, an event
naming a hook that is not there, an unreadable speed, a rate limit with no timer
to keep it on), then pins the decode against the port's real table - including
every surprising entry, because those are the ones a future reader would
"correct": the crystal VANISH is `spike_hit`, unlocking a door is a
`crystal_gather` at half speed, a menu button is the only `button.mp3`, and
light-on and roundabout are byte for byte the same hook. Its last case opens the
original's own `soundfx/` and asserts every file the table names is really
there - the assertion that would have caught three silent-missing-art bugs.

`test_mp_layer` gains the other half, and it is the half that can only be
tested with NO audio device: the layer latches, `LatchedSounds` says what, and a
frame with nothing to play on drops them rather than piling them up. level0 is
the fixture because `test_mp_statics` already reports it "completed after 3.20 s,
2 traversal(s)" - so it is known to reach its exit and to go through a portal on
the way, rather than hoped to. It latches exactly four sounds: two teleports,
the exit, and the medal. It grew from 172 checks to 178.

GCC 13.3 (19 suites) and MSVC 14.50 (17 of them - `test_mp_tscn` and
`test_mp_levels` are GCC-only in this loop) agree: 0 failures, `test_mp_sounds`
99 checks with all 78 files the table names present in the original's
`soundfx/`, `test_mp_layer` 178.

## Step 15 - what the second play-through asked for (built)

Four reports, after playing the menu and the animated levels. Two are fixed
here; one is the sounds, decoded above and built in step 17; and one - things
disappearing while the player walks - was still unexplained when this was
written, and is picked up at the end of this document.

**The chapter icons were stretched.** `world_icon0..3.png` are 84 x 128 and the
port drew them square, because `layOutMenu` sized every button from the box's
height alone. Each is now sized from its OWN image's aspect.

**The level grid held twelve, and the original holds sixteen.** The port took
`PageProperties`' defaults, 4 columns by 3 rows. The owner's screenshot of the
original shows 4 by 4, and `createLevelSelectState` (WorldSelector.angelscript,
bytes 365279..365960) settles it: it sets `columns` 4 and `rows` 4 for levels,
overriding those defaults. The grid's box and button size changed with it so
sixteen do not touch. The page arrows moved to the middle of either side, where
the screenshot shows them: the decoded normalized pair is ambiguous about which
number is x, and a picture of the game beats a guess at an argument order.

**A finished level shows the medal it earned.** The original does not go
straight on - `GameStateController::writeScore` raises a `LevelFinishedLayer` -
so nor does the port. Reaching an exit now stops on a medal screen over the
frozen level, with three buttons: play it again, go on, pick another.

The tier is `computeScore`'s, decoded (ScoreManager.angelscript, bytes
363979..364204): a level with crystals and none collected is BRONZE whatever
else; within the golden score it is GOLD with every crystal and SILVER without;
within the golden score and two more, SILVER; beyond that, BRONZE. The medal
art is the original's own `medal_gold_l.png`, `medal_silver_l.png`,
`medal_bronze_l.png`, and the buttons are its `button_restart.png`,
`button_right.png` and `list_button.png`.

Placed against the CAMERA's view rather than the menu's own box, because the
level is still on screen behind it and the level's pixels are what a click maps
to. NOT ported: the `ScoreCounter` that counts the portals up inside the medal,
the `Matura` fonts and their text, the crystal readout beside it, and the
plaque. The count is on the HUD line instead of in the medal.

**Levels no longer advance by themselves**, which is the point of the screen and
which four suites had encoded the other way: they detected "cleared" by the
current level CHANGING. They now wait for the medal and dismiss it as a player
does.

**The medal's buttons were invisible, and only GCC said so.** `buildMenu` holds
a second switch on the button kind - the one that picks each button's image -
and the three new kinds were not in it, so they fell through with no image and
no quad: present in the data, absent from the screen. `-Wswitch` caught it;
MSVC said nothing. The test counted BUTTONS, which is why it passed; it now
counts the quads drawn for them. That is the third time in two days that the
failure was art silently missing (the menu's backgrounds, nearly the particle
bitmaps, now these), and the lesson each time is the same: assert the thing is
drawn, not that it was asked for.

GCC 13.3 and MSVC 14.50 agree: 18 suites, 0 failures, `test_mp_layer` 168.

## Still unexplained: things appear and disappear while the player moves

Reported after playing, and not reproduced. Three readings have been refused by
the evidence:
- **Bounds culled as a point.** `RenderableComponent::localBoundsMin/Max`
  default to -0.5 and +0.5, a real box, not zero.
- **The orthographic frustum.** `Frustum::FromMatrix` is Gribb/Hartmann from
  the combined matrix, which is projection-agnostic, and `IntersectsAABB`
  rejects only a box wholly outside a plane.
- **A buffer overflowing.** A level draws 6 to 23 drawables against a 65,536
  instance cap and 4,095 uv slots.

A correction to an earlier reading of this, recorded because it was wrong and
was written down as though it were not: `RenderSystem::SyncMeshes` does not
exist by that NAME - the only mention of it is the comment in `Components.hpp`
- but the behaviour it describes does. `RenderSystem.cpp` copies
`gpuMesh->boundsMin/boundsMax` onto the renderable as it resolves resources, so
a renderable is culled against its MESH's bounds and not against a default box.
The cull box and the drawn geometry come from the same mesh and agree by
construction, which also disposes of the idea that a quad might be bounded half
a sprite away from where it is drawn.

**The owner's account, 12 September.** The window neither moved nor changed
size; it happens while WALKING, so while the camera pans; 1-2 loses one sprite
and 1-3 several. That last part matches a number already measured and not
weighed properly: across the 32-level sweep, level2 - which is 1-3 - swings
from 9 drawables drawn to 14, the widest spread of any level, against 20
sprites in the file. level1 barely moves.

So the next step is not another reading of the renderer. It is to walk the
player through 1-3 in a test and run the renderer's own cull - `Frustum`
against each sprite's transformed bounds - tick by tick, against whether the
sprite is really within the view. That either catches it headlessly or clears
culling for good.

**That test was written, and it cleared culling - but only after it was fixed,
and the first version of it was worse than useless.**

It walked the player right for 240 ticks through 1-2 and 1-3, resolved the
world transforms, built `Frustum::FromMatrix(proj * view)` and compared each
visible renderable's transformed AABB against the view rectangle. It reported
6826 and 3854 sprite-ticks with none wrongly culled, and that reading was
recorded here as "culling is exonerated".

It was not. The test only inspected sprites **wholly inside** the view
rectangle. A sprite too big to fit on screen is never wholly inside, so every
one of them was passed over in silence - and most of a level's scenery is
exactly that big. The suspects were the only things the test never looked at.
It is the same failure as the invisible menu buttons: a check that passes by
looking at the wrong population, and reports a large, reassuring number while
doing it.

Rewritten to ask the question the renderer actually asks - does the box OVERLAP
the view at all - it inspects a third to three-quarters more, and both
toolchains agree exactly:

| level | sprite-ticks on screen | wrongly culled |
|---|---|---|
| level1 (1-2) | 8935 | 0 |
| level2 (1-3) | 6824 | 0 |

15,759 sprite-ticks of walking, oversized scenery included, and not one sprite
that overlapped the view failed `IntersectsAABB`. The test also prints the
offending sprite's own local bounds when it ever does fail, which will tell a
stale-bounds fault from a frustum one without the test having to assume what
the default box is.

**So culling is cleared, and the fault is in presentation** - somewhere between
what the layer places each frame and what reaches the screen - which is where
the stale-pixel regions in the owner's screenshots pointed in the first place.
The next reading is what `syncSprites` does with a sprite whose follower is
gone, and what the drawn set does across a camera pan, not the frustum.

## Step 21 - chapter 2: carrancas, the portal cooldowns, fire, burning and bombs (built)

Chapter 2 went from 10 of 32 playing to 27, and the game from 42 of 128 to 59.
Three pieces of work, all decoded from the original's own bytecode rather than
taken from the remake, which marks every one of these numbers `_guess` and gets
each of them wrong.

**The carrancas** - the wall gargoyles that spit fireballs, which the role table
calls turrets. `carrancaCallback` seeds `elapsedTime` from the node's
`startStride`, gathers each frame, fires when it PASSES `stride`, and resets to
zero. So level 2-1's carranca, whose `startStride` is 0 and whose `stride` is
1500, fires its first fireball a stride in, not at once as the remake does
(`hazards.gd:346-349`). A test that counted fireballs would pass either way, so
the FIRST one is timed. The reset is a reset and not a subtraction: the original
writes zero, so a long frame's overshoot is dropped rather than making a carranca
fire twice to catch up. The fireball's numbers are its own `.ent`'s - 130 px/s
and a 16 x 16 sensor - against the remake's invented 180.

**The portal cooldowns**, which step 8 left open above. A tap is refused for the
first 300 ms of a level and for 400 ms after the one it took, and both clocks
start at zero. Applied with an explicit zero-means-off, because `<=` on two zeroed
clocks silently disarmed the suites' opt-out and cost 41 assertions across two
suites before it was read properly.

Beside them, the remake's single guessed `collision_radius_px` splits into the two
radii the original keeps apart: a portal's own entry radius, decoded as 14 and
applied, and the quite separate radius an antiportal refuses a tap within. That
second one decodes as 64 and is **not applied** - level 1-7's blocker data
contradicts it and both toolchains' suites disagreed with it - so it is recorded
as decoded-but-not-applied with what would settle it, and the remake's 16 is kept
on purpose. Deliberately leaving a decoded number on the shelf is the unusual
call here; the failing assertions were treated as evidence rather than as chores.

**Fire, burning and bombs** are one mechanism in the original: a flag on an
entity, set by whatever reaches it, read back by that entity's own callback. The
port follows that shape, and the three numbers a test can tell apart all move.

| | decoded | the remake |
|---|---|---|
| a fire agent's reach | 32 | 24 |
| how long a crate burns | 1000 ms | 2000 ms |
| a blast's radius | 80 | 96 |

The reach is the load-bearing one. `ETHCallback_fire_agent` (bytes
350254..351485) tests `squaredDistance(other, self) < size.x * size.x`, so
`size.x` is a RADIUS and the test is centre to centre. `GetSize()` registers to
`ETHSpriteEntity::GetCurrentSize`, which returns `m_pSprite->GetFrameSize()`
times the scale - the collision box is read only by an entity with no sprite at
all - and `fire_agent.png` is 32 x 32 at scale 1. The remake reads the entity's
38 x 38 `<Collision>` instead and halves it, which is why a crate 28 px from a
flame burns here and does not there. That exact distance is what the suite tests,
because it is the only one that separates the two readings.

**Fire kills the player; a blast cannot.** `explode` (bytes 342023..342895)
grabs through `isBreakableOrExplosiveOrBurnable`, which does not include
characters, and no `main_char` placement carries any of those three flags - so
although `barrel_bomb` passes `killPlayer` true, the player is never in the
grabbed set. The fire agent's own sweep has no such filter and kills outright.
Both directions are tested, because that asymmetry is exactly what a remake
smooths away without noticing.

**Only crates burn away.** `burn()` sets a flag and plays a sound; the fade to
black and the removal that makes burning a PUZZLE live in `manageBurnable` (bytes
420664..421145), whose one caller is `crateCallback`. A `shock_agent` carries the
burnable flag too and merely holds it when lit. Building "everything burnable
dies" would have deleted three levels' shock agents and changed their puzzles, so
`fire.json` names the ten crate spellings and nothing else fades.

**A chain ripples a tick at a time.** Fire and blasts do not detonate a bomb;
they set `explode` on it, and the bomb's own callback (bytes 339851..340212)
reads that flag, removes itself, and only then blasts. The port takes the
requested set before blowing any of it, so a request made during a blast waits
its own turn. level 2-13's three bombs, 39 to 42 px apart, go off over two ticks
and not one.

The blast is a sphere against each candidate's REAL collider with a line-of-sight
ray behind it, so a wall shields what stands behind it. No engine gap: the engine
already had `OverlapSphere` and `Raycast`, which is what `EntityGrabber` (bytes
23924..24990) and `getFirstContactExcept` need.

**Two things are deliberately not built, and are written down rather than left to
be noticed.** A blast also sets `destroy` on every breakable it grabs; a
breakable wall is Demolish's body to take away, and one body with two owners is
how a level ends up half broken. Nine chapter-2 levels place both a bomb and a
breakable wall (11a, 12a, 13a, 15a, 16a, 18a, 20a, 23a, 28a), so that is a
follow-up rather than a footnote. And the fire agent's fourth branch,
`burnProjectile`, puts out a flying `projectile.ent`; the port has no such body,
since its shot is a segment resolved within a tick, so a shot fired THROUGH a
flame opens its portal here where the original would have snuffed it. That fix
belongs in `Shot`'s segment test.

What still stops chapter 2 is `hinge` in four of the five levels that remain -
level27a, level28a, level29a and level30a - and in the fifth, level31a, chapter
2's own BOSS, which wants `boss_spawn` and `waypoint`. So finishing the chapter
is two pieces of work and not one. `hinge` has eight placements in all, the other
four in chapter 3 (level14b, level18b, level19b, level21b), so building it pays
twice.

For the record, since the counts above are easy to misread: what stops the other
two chapters is not roles left inert but levels refused outright - `no_gravity` in
18 chapter-4 levels and `darkest` in 12 more, which is every one of the 30 that do
not start. Chapter 3 starts all 32 and plays none, held by `waypoint` (30),
`enemy_spawn` (29), `keyhole` and `locked_door` (26 each) and `key` (24).

GCC 13.3 and MSVC 14.50 agree: 0 failures in every suite, `test_mp_fire` 43
checks. One suite would not run under MSVC at all - Smart App Control blocks a
freshly linked unsigned exe with 4551, and the runner's three relink-and-retry
attempts were exhausted - so `test_mp_turrets` was run again on its own and came
back 25 checks, 0 failures. That is a fact about this machine and not about the
port, written down so that a future green run which is quietly one suite short is
not read as a pass.

## Step 22 - hinges, and the contact a joint has to win (built)

Chapter 2 goes from 27 of 32 playing to 31, and the game from 59 of 128 to 63.
Seesaws and spinning platforms - the bodies the original pins to an anchor with a
revolute joint - and one engine gap that had to be filled before any of them
would hold still.

**The one role with no tick.** Every other system in the port ticks something;
this one attaches a `JointComponent` when the level is built and the engine's
solver does the rest. That is the point: a hinge driven by a script writing
transforms is not a physical object, it is a body that ignores what it touches,
and the player has to be able to stand on these and tip them.

**Nothing had to be decoded from script, and nothing had to be added to the
converter.** There is no hinge callback in the bytecode at all - the joint is
Box2D data, nested in the `.ent`'s `<Collision>` beside `<Polygon>` and
`<Compound>` - and the converter already carries it through as eight bare
`metadata/joint_*` keys plus `metadata/revoluteJoint`, a STRING naming the other
body by its entity name. In all ten placements that string is `anchor`, and every
level that has one places an `anchor` node, so this is body to body and never body
to world.

The converter drops the original's `attachPointB`, and it costs nothing: Box2D's
`Initialize` puts both ends of the joint on one world point, so B's anchor is
simply where A's lands, expressed in B's frame. The attach points are fractions of
the half collision box (`ETHRevoluteJoint::ComputeAnchorPosition`), and level27a
checks out to within a pixel - the anchor at x 460 with a 256-wide box and an
`attachPointBX` of -0.8 gives 460 - 102.4 = 357.6, where the platform stands at
357.

**The engine gap: a joint has to win against the contact.** In level27a the
platform's compound overlaps its own anchor's box by 89 px, which is normal -
a hinge sits inside the frame it swings on. Nothing in `PhysicsSystem` suppressed
that pair, so the solver shoved the two apart every step while the joint pulled
them back, and the platform climbed out of its own pivot. Box2D, PhysX and Bullet
all carry `collideConnected` for exactly this reason, so it is the engine's gap
and not the port's: `JointComponent::collideConnected`, defaulting false, with the
pair dropped where the registry is in hand rather than inside `SweepAndPrune`,
which is a pure function over proxies and is exposed for testing on that basis.
Membership only - the pair ORDER the broadphase emits is left exactly as it was,
which that file guards carefully for determinism. `test_physics` pins it with a
SLACK ROPE across the overlap, ten metres of it over half a metre, so the joint
applies no impulse in any case and the flag is the only thing that differs.

**The angle, which took three attempts.** Box2D measures from the pose at
creation, so the authored range is relative to how the level was built; the
engine's hinge angle is zero where the two bodies' reference directions coincide,
which its own header calls an arbitrary configuration. So the range is measured
from the angle the joint rests at - and that rest angle is taken from the level's
own rotations rather than from the registry, because `Find` runs before anything
has stepped and a world transform may not be resolved. It still goes through
`Joints::HingeAngle`, so there is one answer to the question and not two.

The SIGN was got wrong twice, both times the same way: by trusting a measurement
taken under conditions that confounded it.

- Derived, the two negations cancel - Box2D reads B relative to A, the engine
  reads A relative to B, and the y-flip negates each again - so the limits are
  ADDED. That is correct.
- It was then negated on the strength of a reading taken three seconds into a
  swing WITH GRAVITY ON, by which time gravity had pulled the bar down past rest.
  Indistinguishable from an inverted sign.
- Read six ticks in, before the bar can reach anything, +z plainly RAISES the
  angle. Reverted.

Two things hid it. The solver ORDERS the pair itself, so swapping min and max does
nothing at all and only a negation was ever doing anything. And a 254 px seesaw on
a 127 px arm meets the level's own geometry before either stop - level30a settles
at 2.11 turned one way and 1.67 the other, neither of them a limit - so "it
stopped early" is the ordinary case and says nothing about which way the angle
runs. The suite therefore pins the DIRECTION, read in the one window where nothing
else has touched the bar, the clamp at both ends, and the arithmetic; it does not
claim the bar reaches a stop, because in a real level it does not.

level30a is the only level in the game that could have caught this: it swings
1.1325 one way and 0.4382 the other, where every other placement is a symmetric
quarter turn that passes whichever sign is used.

**Not built, and written down instead.** Motors: all ten placements set
`enable_motor` 0, and `ETHCallback_spinning_platform` fetches the joint every
frame and zeroes its motor speed besides. `ETHCallback_spinning_cross` turns its
body at a constant 0.6 rad/s and no level places a `spinning_cross`. And
level21b's `motor_seesaw` is limited to 0 .. 1.5708 - pinned at one end of its own
range - with no motor and no callback, which is recorded as unexplained rather
than reasoned backwards into something that must work somehow.

What is left of chapter 2 is one level: level31a, its BOSS, which wants
`boss_spawn` and `waypoint` and so belongs with chapter 3's work rather than
here.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_hinge` 28 checks, and 98 of 128 levels starting with 63 playing on both.
Because `collideConnected` is an ENGINE change rather than a port one, the whole
engine suite is run as well and not only these: every game here links that contact
filter, and two green Magic Portals runs say nothing about the other two. It
passes, 99 of 99, with `test_physics` at 282 checks and `test_joints` at 93.

Nine of those ninety-nine would not START on the first sweep - `***Not Run`,
`BAD_COMMAND`, "Process not started" - which reads exactly like a physics change
having broken the audio, rendering and determinism suites. It is Smart App Control
refusing freshly relinked unsigned binaries, the same thing that stopped
`test_mp_turrets` above, and `ctest` swallows the launch code so the usual 4551
never appears. The tell is that every binary was present on disk and one of them
passed on a plain rerun with no rebuild at all; relinked and re-run, all nine are
green. Written down because "the engine went red after a physics change" is the
wrong conclusion to reach, and this machine offers it twice a day.
