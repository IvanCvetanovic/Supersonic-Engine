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
    still not modelled.
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
- **g_portalCollisionRadius is 14 px.** The port reads the remake's guess, 16.
- **A placed portal perishes 20,000 ms after it opens** unless marked
  dontPerish (ETHCallback_portal). The port keeps its portals.
- **Every rolling stone crushes a character** it hits hard enough, not only the
  beholder's (step 11b, above).
- **DEFAULT_GRAVITY_SCALE is 2**, against the remake's 980 px/s^2 world.
- **174 named globals** are readable, among them the timings and radii that other
  steps carry as guesses.

Each touches levels that already play, so each wants its own step, with all
fifteen suites re-run.
