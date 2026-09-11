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
7. **Launchers**, and the projectile blocker.
8. **The original's sprites.**
9. **Reflectors.** The remake marks what they reflect a guess. They stay inert
   until something in the data says, rather than being given an invented rule:
   the remake's own worst bug of this kind was `bounce`, built as a trampoline
   that threw the player out of level 4-7.
10. **Chapter 1's boss** (level31). The remake has its structure only.

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
- **Quarter turns only.** A breakable turned by any other angle is refused,
  with a named error. Every breakable in the game is turned by 0 or by a
  quarter turn.
- **The tick.** After the step: goals, then hazards, then stones, then
  portals. The portals are last because they move what goes through them.
- **Drawn.** Breakables are sandy and stones grey. A broken wall's box goes
  with its body. Before this, the layer would have left a destroyed body's box
  standing where it was last drawn.
- **Left for step 7.**
  - Levels 23 and 24 have a breakable wall and no stone, because their launcher
    throws `rolling_stone.ent`.
  - 22 of the game's 27 stones carry a `destroyable` flag, and nothing reads it
    yet. `rolling_stone_destroy.ent` exists beside `rolling_stone.ent`.

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
