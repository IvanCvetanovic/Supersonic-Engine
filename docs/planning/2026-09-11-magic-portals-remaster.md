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
- **Both flags are played now, and nothing is refused.** This bullet read "two
  flags are refused outright" for most of the remaster's life, and the table
  above is the dated snapshot that belonged with it. `darkest` stopped being
  refused at Step 29 and is carried; `no_gravity` stopped at Step 31 and is
  built - zero world gravity, no walking at all, and the recoil of a portal shot
  as the only way to move. No level property refuses a level any more, and all
  128 start.

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

## Step 23 - chapter 3: minions, their patrols, and the floor that kills only them (built)

Chapter 3 goes from 0 of 32 playing to 7, and the game from 63 of 128 to 70. The
minions of worlds 3 and 4, and one role the remake's table gets wrong.

**Nothing is placed.** No level in the game holds a `minion.ent`, a
`ghost_minion.ent` or a `dark_mage.ent` - zero, counted across all 128. Every
enemy is spawned from an invisible marker that stamps the patrol onto what it
spawns and then deletes itself, and the marker's `waypointName` is a PREFIX
rather than a name: `wayA` means the nodes `wayA0`, `wayA1`, ... The original
counts them by seeking prefix + n until one is missing, which is why the walk can
wrap by asking whether the next index has any data at all. 62 markers in 35
levels; every one carries a prefix, 56 resolve two waypoints and 6 resolve one.

`holdTime` is a dwell, not a travel time, and the lone-waypoint markers carry
`999999999` - which is how the designers wrote a guard that stands. It needs no
special case: it is a wait nothing outlasts.

| | decoded | the remake |
|---|---|---|
| patrol speed | 39 px/s | 60 |
| sight range | none at all | 220 |

The speed is a literal 1.3 per physics step, and that step is FIXED: the game's
`AverageFPSRateManager` calls `SetFixedTimeStep(true)` and hands it 0.0333333, so
the divisor is 33.3333 ms and 1.3 px per step is 39 px/s. Sight has no range
anywhere in it - what bounds the cone is a walk over 256-px buckets, not a radius
- so the remake's 220 is not a wrong number but an invented mechanic. Its own
`actors.json` marks both `_guess`.

**The wider threshold, which took two wrong turns.** Both were caught by the
suite rather than by reading, and both are the same mistake as step 22's sign: a
reading that fit the evidence in front of it and nothing else.

- Derived: the original advances to the next waypoint inside `minDist` (8 px) and
  stops moving inside `minDist * 2` (16 px), so both gate the walk. Wrong.
- What the failing check showed: the guard reached its post but never adopted its
  sentinel hold. It could not - stopped 16 px out, it can never reach the 8 px
  that counts as arriving, so it freezes one step short of its own waypoint for
  good. Applied to every minion, nothing in the game would ever arrive.
- What the branch actually says: instruction 1070 compares `numWaypoints` against
  1 and sets the flag to a literal false when it differs. ONLY a minion with
  exactly one waypoint evaluates the distance test. It is what stops a lone guard
  being driven the last stretch to its post; a patrol walks all the way in.

**And stopping is not a zero, which exposed the second wrong turn.** The original
has two different behaviours and neither writes zero: a minion serving its hold
has its velocity multiplied by `(0.3, 1)`, a brake on x that decays over a few
frames; one inside the wider threshold is not touched at all, and is left to
carry on and to friction. Implementing the second honestly broke the guard - it
sailed through its post and never parked.

The cause is the port's body, not the rule. `LevelBuilder` builds a rigid CIRCLE
and `kPlaneLockRotation` leaves z free, so friction turns a slide into a ROLL and
nothing stops. `minion.ent` is `fixedRotation="1"` and cannot do that, so the
spawned body now locks z as the original does. The suite shoves a guard at its
post at three times a walk and checks it still parks, because a comment claiming
"same post, reached differently" with no case exercising it is not a claim worth
keeping. A capsule builder would have hidden this entirely.

**enemy_killer is not a hazard, and that is a correction to the role table.** The
remake files it under `hazard` beside `death_area.ent` and `lava`, so the port
killed the player on it. Its callback does nothing of the kind:
`ETHBeginContactCallback_enemy_killer` is 69 instructions that open with
`isMinion(other)` and jump to the return when that is false - the destroy, the
earthquake, `playMinionFallSound` and the skull it drops are all inside that
branch. The entity has no `.ent` at all; it is defined inline in each scene, and
level0b's reads `shape=1 sensor=0 static=1` with a collision of 768 x 128 x 16.
So it is a solid floor to stand on, below the bounds of 44 levels, and it is
TILED - 30 of those levels lay one slab and 14 lay two, so a finder that took the
first would leave half of them inert. `data/hazards.json` takes it out of the
hazards by name; `sim/Minions.hpp` is what acts on it. No chapter-1 or -2 level
places one, so nothing that already played could have caught this.

**Not built, and written down instead.** The sight cone (120 degrees over three
256-px buckets, with an occlusion ray and no range); the shot (2000 ms cadence,
`fakeRadius` 16, and `killPortal`, which reaches into portal state nothing else
in this port writes from outside); the `cantReach` gate, which skips the whole
move block unless a probe towards the destination comes back null - not built
because a ray between two points near a platform can clip the floor, and a false
positive there freezes every minion in all seven levels that now play. A minion
is also `breakable` and `burnable` as well as `teleportable`; only the last is
wired, since `Fire` and `Demolish` fill their lists from the scene and a minion
does not exist when they run. The marker's `entityName` override is read by the
original and used exactly once in the whole game (level31b's `ghost_minion.ent`);
`alternativeName` it also reads, and no level sets it at all.

One census trap worth recording: counting markers by the prefix `minion_spawn`
gives 63 and an apparent marker with no `waypointName`. It is `minion_spawn_point`
in level31b, which is in no role at all - it is the world-3 boss's spawn anchor,
sought by name so `ghost_utility_spawn.ent` can be put at its position. That
level's unclaimed `wayA0` and `wayA1` belong to the boss, not to a patrol.

**What is left of chapter 3**, read off the inventory rather than inferred:
`keyhole` and `locked_door` in 26 levels each and `key` in 24 - one mechanism,
paired by COLOUR rather than by proximity, and the largest single block left in
the game; `pickup` in 9; `shock_field` in 7; `boss_spawn` in 2 (level31a, which
is chapter 2's last, and level31b); `gravity_well` in 1. Chapter 4 is still
stopped at the door, where `no_gravity` refuses 18 levels and `darkest` 12.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_minions` at 77 checks, and 98 of 128 levels starting with 70 playing on
both. Nothing under `src/` changed this time - the port builds on the
`collideConnected` step 22 added and asked nothing new of the engine - so unlike
that step these suites are the whole verification surface, and the engine's other
games are untouched.

Both wrong turns above cost a full run of both toolchains, and neither would have
been found by reading harder: the first showed up only as a guard that reached
its post without adopting its hold, the second only as one that reached its post
and would not stop. That is twice now, counting step 22's sign, that the decode
looked settled and the suite disagreed - and both times the suite was right.

## Step 24 - chapter 3: keys, the keyholes they open, and the doors that go with them (built)

Chapter 3 goes from 7 of 32 playing to 19, and the game from 70 of 128 to 83.
Chapter 4 gains its first playable level as well. The largest single step of the
remaster so far, and the mechanism that held more of chapter 3 than any other.

**Three roles, one of them solid.** `door_locked.ent` is shape=1, static=1, a
30 x 126 collider. `keyhole.ent` has no collision block at all, and `key.ent` is
density 0 with custom data holding nothing but `color`. So a key is not a body
here - it is state with a position and an owner - and unlocking a door is the
deletion of the only one of the three the player can walk into.

| | decoded | the remake |
|---|---|---|
| picked up within | 26 px | 40 |
| unlocks within | the same 26 px | a second, separate 40 |

**One range, written once and read twice.** The key's init writes
`squaredRange = scale(26)^2`; the pickup poll and the unlock test both read that
one value. `actors.json` invents two numbers where the original has one, and
guesses both. Pickup is a poll and not a contact - there is no contact callback
for a key anywhere in the binary, which the remake also says and is right about -
and the filter is `isCharacter OR isMinion`, so **a minion can carry a key**.

**`unlocked` is one-way.** The string appears exactly twice in the whole binary:
the key writes it, the keyhole reads it. Nothing re-locks a door, so the port's
state for it is one bool. The keyhole then holds 1000 ms, fades over 500, and
deletes both its door and itself; the door's own callback does nothing but paint
it, so the keyhole drives the entire mechanism.

**Pairing is by colour, and the port pairs level-wide.** Colour is an explicit
`metadata/color` string on all three roles and is absent from none of them in any
of the 128 levels. The original finds a door with `seekNeighbourEntity` over a
3x3 block of buckets and falls back to a global `seekEntity`, so the bucket walk
is an optimisation rather than a rule - which is why the port simply takes the
nearest door of the keyhole's colour. Across all 41 pairs that is the same
answer. level20b is the only level in the game holding two pairs of one colour,
and it is the one place a nearest-door rule could have been wrong: each of its
yellow keyholes sits 57 px from its own door and 216 px or more from the other, a
four-fold margin, and the suite pins all three of its pairs by position.

**Counts the remake gets wrong, left unexplained.** `actors.json` says the colour
multisets are identical per level per role. Counted over all 128 they are not:
39 keys against 41 keyholes and 41 doors. level0c, level31b and level31c each
hold a keyhole and a door with no key, and level18c holds a red key with no lock
at all. The red key has its own sound, its own help popup and an achievement, and
two of those levels are boss levels - but none of that has been decoded, so it is
recorded as measured and the explanation left open, as step 22's `motor_seesaw`
was.

**Not built, and written down instead.** The fly-in animation a spent key plays -
some 400 instructions of timed `WaypointManager` waypoints with `smoothEnd`
filters - is presentation, and this port has no renderer. With it: `fixKeyAngle`,
the idle bob of an unowned key, the red key's per-frame light, the four effect
entities, the earthquake, the pick and unlock sounds, and `setKeyColor`, which
repaints a blue or red key by swapping its sprite - yellow being the base art,
which is why yellow dominates every count above.

**Three wrong turns, all of them in the test rather than the module.** Worth
recording because the pattern is new: the decode was right first time and the
harness was not.

- A pointer compared against itself. `FindKey` hands back a pointer into the
  state, so reading it again after ticking asked whether the key had moved away
  from where it now was. Always false, and it says nothing.
- A fixed offset invalidated by the step order. `Keys::Tick` runs AFTER the
  physics step, and level7b's key lies between two collision polygons, so a
  player put down 28 px away was resolved somewhere else before the keys were
  judged - the suite reported "still 24 px from the key, and not 22". The fix was
  not to loosen the range but to sweep six offsets, read back the distance the
  module actually measured, and pin the decision against that; what pins the
  number is the pair of brackets, a key taken from beyond 20 px and one left
  lying from inside 40.
- Two WSL runs in flight at once. `sync-wsl.sh` hard-resets `~/ss-src`, and both
  runs share it and the build tree, so the second built against the first's
  sources and reported the previous revision's failures while MSVC on the same
  edits was green. Not a compiler disagreement: a concurrency mistake, and the
  rule that only one may run at a time exists for exactly this.

What stands between here and the rest of chapter 3, read off the inventory:
`pickup` in 9 levels, `shock_field` in 7, `boss_spawn` in 2 and `gravity_well` in
1. `pickup` is the natural next step and not because it is the largest - the
diamonds' carry path is byte-for-byte the key's (`ByIDChooser` with a global
fallback, the 40 px `forceFollowUpPosition` leash, `followUp(60, 20)`, the
`shallLeave` drop), differing in a 30 px range, an `isCharacter`-only filter
and the payload: a carried shock diamond goes nearly invisible at alpha 0.1 and
destroys the first minion it comes within range of, then deletes itself. So it
generalises what is already built rather than adding machinery.

**"Differing only in" was wrong by two, and step 25 says how.** The diamond's
candidate loop ALSO deletes the diamond outright when a character it is
considering `shallLeave`s - tested before any distance is measured - and the
pickup and the strike are a whole FRAME apart rather than one tick's work.
Neither is visible from the carry path, which is where that sentence was read
from, and the second is precisely what a port written to the key's shape gets
wrong. `shock_field`
does not: its agent is a one-shot builder that spawns a SECOND entity carrying
the lethal radius, the radius is per-placement and ranges from 28 to 105 across
21 placements, three of the agents patrol with their own speed and stride, and
eleven are burnable.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_keys` at 96 checks, and 98 of 128 levels starting with 83 playing on
both. Nothing under `src/` changed, so these suites are the whole verification
surface and the engine's other games are untouched.

## Step 25 - chapter 3: shock diamonds, and the minion that dies to one (built)

**The playing count does not move, and that is the honest answer rather than a
failure.** 98 of 128 levels start and 83 play, exactly as after step 24. The
`pickup` role covers TWO entities - `shock_diamond.ent` and `fire_diamond.ent` -
and only the shock one is built here, so `Roles::IsPorted` still refuses the
role. IsPorted is per-role and cannot say "served for one entity name and not the
other", so admitting `pickup` would mark all nine levels the inventory lists as
played in order to be right about five of them:

```
9  pickup: level15b level16b level17b level19b level26b level28b level29b level30b level31b
              \_____________ shock, built ______________/  \______ fire, not built ______/
```

The inventory is what tells the remaster what is left. It is worth more intact
than flattering, and the role is admitted when the fire diamond lands.

**Selected by entity name, never by role**, which is the whole reason the step
splits that way. `entity_roles.json` files both diamonds under `pickup` and its
own note says they are distinct callbacks - which they are: the fire diamond sets
`hasFireDiamond` on its CARRIER, and that flag's only other reader is
`PortalManager::computePortalFinalPos`, so holding one moves where a portal
opens. Selecting on the role would have handed four chapter-3 levels a diamond
that kills minions the original never lets it touch. Both names live in
`data/diamonds.json`, and the suite asserts level28b, level29b, level30b and
level31b each yield none.

| | a key | a shock diamond |
|---|---|---|
| range, one value used twice | 26 px | 30 px |
| who may carry it | `isCharacter` OR `isMinion` | `isCharacter` alone |
| taken and acting | the same tick | a frame apart |
| the remake's guess | 40 px, twice | a 28 x 24 box |

**The pickup and the strike are a frame apart.** The callback branches on
`ownerID` at the top. The unowned arm polls for a carrier, writes `ownerID` and
then RETURNS - it ends in a jump straight to the function's exit - so nothing
else happens on the frame a diamond is taken. The payload that strikes a minion
lives in the carried arm, which that same branch reaches on the NEXT frame.
`Keys::Tick` deliberately acquires, trails and unlocks all in one tick; this must
not, and the suite pins it by putting a minion 8 px away - well inside the 30 -
then asserting it is alive after one tick and struck after two.

**The carry path came out first, as its own commit.** The carried arm of
`ETHCallback_shock_diamond` is the key's instruction for instruction: alpha 0.1,
`GetInt('ownerID')`, `ByIDChooser`, a `seekNeighbourEntity` over the buckets
around itself with a global `SeekEntity` behind it, and the same `scale(40)`
leash. So it moved into `Carry.hpp` before any diamond existed, as a change that
altered nothing. `Carried` is a BASE rather than a member, so `key.atPx` and
`key.owner` stayed where they were, `Game.cpp` and `test_mp_keys.cpp` were not
touched at all, and the suite stood at 104 checks before the move and 104 after
it on both toolchains - which is what makes that count a regression test rather
than a rewritten one. Who may carry a thing needed no flag either: the CALLER
builds the list, so a key is offered the player and every minion still standing
and a diamond only the player.

**The minion is destroyed by Minions, not by the diamond.** The original calls
the same `destroy()` the killer floor calls, and here `Minions` walks its own
list every frame - a body destroyed behind its back would sit in that list
invalid, to be read on the next tick. So `Diamonds::Tick` returns what it struck
and `Game` hands it to `Minions::Take`, which marks the minion gone, destroys it
and returns it for `Forget` exactly as `Cull` does, under a counter of its own
because the decode distinguishes a floor from a strike.

**`bounce` is not physics, and reading it as physics would have cost the step.**
An unowned diamond calls `bounce(0.9, 0.9, 500)`, which looks like a restitution
and a duration. It is neither: `bounce` lives in
`ETHFramework/utilEntityEffect.angelscript` and drives a `blinkElapsedTime`
counter, so it is the idle pulse an unowned diamond turns with - the analogue of
the key's idle bob, and the reason a diamond is state with a position here rather
than a body. Taken the other way it would have become a bouncing rigid body: step
23's rolling minion again, caught this time before any code was written.

**Not built, and written down instead.** The alpha of 0.1 a carried diamond goes
to, the idle pulse, `shock_diamond_pick.ent`, `shock_death.ent` at the minion and
`shock_strike.ent` angled at the midpoint between the two, both sounds, and the
two earthquakes - `startEarthquake(15, 0)` on the pickup, `(25, 100)` on the
strike. With them the remake's `metadata/trigger_size` of 28 x 24, which the
original never consults: it polls a RADIUS of 30 and tests no box, so this is the
key's 26-against-40 disagreement in another shape.

One branch is recorded as UNREACHABLE rather than skipped: the candidate loop
deletes the diamond outright when a character it is considering `shallLeave`s,
tested before any distance is measured. In this port a carrier that has ceased to
be never reaches that list, because `Game` rebuilds it each tick from the player
and the minions still standing. There is nothing to build, which is not the same
as something declined.

And the fire diamond entire, with `gutter_mouth.ent` beside it - the second
role-table misclassification after `enemy_killer`. `entity_roles.json` files it
under `scenery_fx` as a "decorative drip emitter"; its callback destroys a fire
diamond on contact, and all three levels that place one - level29b, level30b and
level31b - place a fire diamond too. Nothing reads it until the fire diamond is
built, so it is recorded in `data/diamonds.json` and left alone.

The census, measured: 8 shock diamonds across 6 levels, level15b and level16b
holding two each and level17b, level19b, level26b and level29c one each; and 5
fire diamonds across 5 levels. 7 of the 8 are in chapter 3. The eighth is
level29c's, and `darkest` stops that level starting at all, as `no_gravity` stops
level15c - which is why 11 levels hold a diamond and the inventory reports 9.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_diamonds` at 85 checks, `test_mp_keys` unchanged at 104 and
`test_mp_minions` unchanged at 77, and 98 of 128 levels starting with 83 playing
on both. Nothing under `src/` changed.

What is left of chapter 3, off the same inventory: `shock_field` in 7 levels,
`boss_spawn` in 2, `gravity_well` in 1 - and the fire diamond, which admits
`pickup` to IsPorted when it lands and moves all nine of those levels at once.

## Step 26 - chapter 3: shock fields, and the node a lethal ring swings about (built)

Chapter 3 goes from 19 of 32 playing to 23, and the game from 83 of 128 to 87.
The first movement in the count for three steps, and `shock_field` IS admitted to
`Roles::IsPorted` where `pickup` was not - because this role covers one mechanism
under two spellings (`shock_agent` and `shock_agent.ent`), not two mechanisms
under one name. level23b, level24b, level25b and level27b had it as their only
inert role; level26b and level30b still hold `pickup`, and level27c still holds
`gravity_well`.

**Two entities in the original, one point here.** A placed `shock_agent` is a
one-shot builder guarded by an `areaAdded` flag: on its first frame it spawns a
separate `shock_area.ent` at its own position with z -10, sizes it to the
diameter with `scaleToSize`, and writes `currentScale`, `squaredRadius`,
`ownerID`, `speed`, `stride` and `direction` onto it. The AREA carries the lethal
radius; the AGENT is the sprite. Both then oscillate on the same numbers from the
same seed, so the port models one moving point. That is safe to do and the
ordering says why: the area is created at instruction 54 of the agent's first
frame and the agent's own `linearMotion` runs at instruction 287 of that same
frame, so the area captures its `originalPos` BEFORE the agent has moved. Both
centres are the node; the only divergence is one frame of phase, which at the one
moving placement in the game is 0.03 px on a 64 px stride.

**The placed node is the CENTRE of the swing, not the starting position.** `angle`
seeds at 0 and the displacement is `cos(angle) * stride * sign(speed)`, so
`cos(0) = 1` puts the ring a full stride from its node on the very first frame,
and it swings through the node to the far side. A port that read the node as the
start would misplace level27b's ring by 64 px and pass every radius test ever
written against it, which is why the suite asserts the DISPLACEMENT: a full stride
away after one tick, and the far side after half a period.

| | decoded | what the level file suggests |
|---|---|---|
| lethal radius | `areaRadius`, 28 to 105 per placement | a 30 px CircleShape2D on every agent |
| the kill | a poll EVERY frame | an Area2D that might be entered |
| the axis | always vertical | `metadata/direction`, which is never read |

**The converter's collider is not the ring.** Every agent node carries a `Body`
(Area2D) holding a `CollisionShape2D` whose `CircleShape2D` has `radius = 30` -
the same 30 in every level, against radii of 28 to 105. It is sprite-sized
geometry with nothing to do with the lethal zone, and taking it for the kill shape
would have shrunk level30b's 94 px ring to a third of itself. `LevelBuilder` gives
an Area2D `isTrigger`, so it is a sensor the player passes through rather than a
platform - which is the port already being faithful, since the original's agent
drives no physics at all. The port moves that entity with the field so the sensor
and the sprite follow the ring, and takes the radius from `areaRadius` alone.

**`direction` is copied and never read.** The agent reads `metadata/direction`
with `GetString` and `SetString`s it onto the area - and then neither callback
consults it: both pass a hardcoded flag to `linearMotion`. So movement is always
vertical, and that is recorded rather than implemented. Both placements carrying
the key say `vertical` anyway, so nothing in the game could tell the two readings
apart.

**It polls rather than being entered.** A hazard fires on `body_entered` and
remembers whether the player was already inside; a ring tests
`squaredDistance(player, ring) < squaredRadius` every frame and then writes
`hp = 0`. The kill is reported here and folded into `Hazards` by
`Game::AfterStep`, as the boss's, the carrancas' and the fire's are, so there is
one death in the port and not five.

**Burnable needed no wiring, and the check was worth making.** 11 of the 21
placements carry `metadata/burnable`, of which only level30b's is in a level that
starts. `Fire::Find` is flag-filtered, so that agent genuinely IS in Fire's
burnables list and genuinely does get `burned` set - but removal is gated on
`fire.json`'s `fade_names`, which holds only the crate spellings. `fire.json`'s
`burn._note` already said so in as many words, two steps ago. So no ring can
outlive its agent, and the agent's own destroy path - `deleteEntity(childID)`,
which takes the ring with it - stays unreachable for as long as the blast's
`destroy` remains unbuilt. When that lands, this is its second caller.

The census, measured: 21 placements across 16 levels, 10 of them in the 7 levels
that start. Only 2 move at all - level14c and level27b - and `no_gravity` stops
level14c starting, so exactly one swinging ring is reachable in the game as the
port plays it.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_fields` at 63 checks, `test_mp_keys` unchanged at 104, `test_mp_diamonds`
at 85 and `test_mp_minions` at 77, and 98 of 128 levels starting with 87 playing
on both. The swinging ring reached a player that never moved after 67 ticks on
both toolchains - earlier than the half period, because it only has to come within
36 px rather than travel all the way to the far end.

One environment note, so the run is not read as cleaner or dirtier than it was:
under MSVC `test_mp_demolish` returned 4551 - Smart App Control refusing a
freshly-linked unsigned exe, which this machine does intermittently - and the
runner's three relink-and-retry attempts did not clear it. Running the binary
already linked gives 133 checks and 0 failures, and GCC ran the same suite green
in the same sweep. Nothing in this step touches Demolish.

What is left of chapter 3: `pickup`'s other half, the fire diamond, in 4 levels
that start; `boss_spawn` in 2; and `gravity_well` in 1, which is chapter 4's
mechanism appearing early in level27c.

## Step 27 - chapter 3: fire diamonds, and the tap that stops being a portal (built)

Chapter 3 goes from 23 of 32 playing to **31**, and the game from 87 of 128 to
**95** - the largest single movement in the count since chapter 2. `pickup` is
admitted to `Roles::IsPorted` at last. It was held out through step 25 for a
stated reason - `IsPorted` is per-role and cannot say "served for one entity name
and not the other", so admitting it while only the shock diamond was built would
have claimed the four fire-diamond levels in order to claim the five shock ones -
and that reason is now spent. Eight of the nine levels reporting `pickup` inert
flip; the ninth is level31b, which also places a `boss_spawn`. **Chapter 3 now has
exactly one level left.**

**It is not a change to where a portal opens, and step 25 said it was.** So did
`diamonds.json`. Both were wrong and this step corrects them in place rather than
quietly ceasing to repeat them. Holding a fire diamond means **no portal opens at
all**. The carry arm calls `turnProjectilesIntoFireBalls` every frame, which walks
`GetEntityArray('projectile.ent')` calling `burnProjectile` on each, and
`burnProjectile` is `killProjectile` - a bare `DeleteEntity` - followed by
`addFireball` along the projectile's own direction. The shot is deleted a frame
after it leaves and a fireball stands where it was. The trade the mechanic makes
is your portals for a ranged igniter.

**The 64 is a guard, not a reach.** This is the trap, and the step walked into it
twice before the bytecode closed it - once as "extend the flight 64x", once as
"bypass the flight and place directly". `PortalManager::computePortalFinalPos`
branches at instruction 38 on `GetUInt('hasFireDiamond')` off the character and,
when set, returns `origin + (destPos - origin) * scale(64)` and jumps straight to
the function's exit: past `GetClosestContact`, past the anti-portal test, past the
retry loop, without ever writing its `hasFailed` or `preferedTeleportEntity`
out-params. That reads as a 64x range extension and is not one.

| | what it looks like | what it is |
|---|---|---|
| `* scale(64)` | 64x the reach | 64x the stored range, and nothing else |
| skipping `GetClosestContact` | the shot ignores walls | there is no landing point to compute |
| not writing `hasFailed` | a shot that cannot fail | a shot that never reports placement at all |

`addProjectile` computes `dirVector = normalize(finalPos - casterPos)`, and
`normalize` is scale-invariant, so the 64 **cannot move the aim by a degree**. Its
only surviving effect is the other thing `addProjectile` does with `finalPos`:
`SetFloat('squaredDistance', squaredDistance(finalPos, casterPos))`. That makes
the stored range enormous, so the projectile cannot reach its `destiny` and call
`insertPortal` during the single frame it exists before the conversion kills it.
A guard against landing.

**So `Portals` is untouched by this step** - no reach multiplier, no new
`TryPlace` caller, no change to `Flight`. The port needs no guard because `Game`
runs the conversion *before* `Portals::Tick` advances the flight, reaching the
same end by ordering. The most heavily pinned subsystem in the port did not have
to move, and `test_mp_shot` (90), `test_mp_portal` (37) and `test_mp_layer` (205)
are unchanged as a result, which is the evidence that it did not.

**The flag is the diamond's, not the player's.** The carry arm does
`SetUInt('hasFireDiamond', 1)` on the character *every frame* it is carried, and
the destroyed arm seeks a character and sets it to 0. So `carrierHasFire` is
recomputed every tick rather than latched, and a diamond that dies takes the
power with it. A latched flag would have survived the drain and broken every
level below.

**One fireball, one parameter apart.** `addFireball` and both fireball callbacks
live in `ETHCallback_carranca.angelscript`: a fire diamond's fireball *is* a
carranca's, the same `fireball.ent`. So this reuses `Turrets::Fireball` and
`turrets.json`'s 130 px/s and 16 x 16 rather than inventing a second projectile.
What differs is `killMainCharacter`, which is a **parameter and not a property of
the entity**: a carranca passes it set, `burnProjectile` passes immediate 0, and
`ETHBeginContactCallback_fireball` tests it first and returns on a character when
it is clear. A carranca's fireball kills the player; the one your own tap bought
cannot touch you. That is `Turrets::Fireball::killsPlayer`, and the suite stands
the player in its own fireball for a second to prove it.

What a fireball does to what it hits, in the callback's own order: against a
sensor it acts on exactly **one** name - `shock_agent` / `shock_agent.ent` -
destroying that ring and itself, and does nothing to any other sensor; against
anything solid it destroys itself and then `burn()`s it if `isBurnable` and
`explode()`s it if `isExplosive`. A minion is not `isCharacter` and carries
neither flag, so **fireballs do not kill minions**. The two diamonds do not
overlap: one kills a minion and the other cannot. Both flags are set after
`Fire::Tick`, which keeps the one-tick split `fire.json` already argues for, so a
chain of bombs still ripples a tick at a time.

**The gutter mouth is the counterweight, and without it these levels are
unwinnable.** `entity_roles.json` files `gutter_mouth.ent` under `scenery_fx` as a
"decorative drip emitter". Its callback is
`ByNameChooser('fire_diamond.ent')` into `seekNeighbourEntity`, then
`scaledCollide`, then `destroy()` on the diamond - and destroying the diamond
clears `hasFireDiamond`. The drain is how the player hands the fire back and gets
portals again. That is a second role-table misclassification of the kind
`enemy_killer` was. Its mouth is the node's own `trigger_size` at its
`trigger_offset` - 10 x 51 hung 46 px *below* the node in level30b - a tall thin
slot under the sprite rather than a box on the node, and the suite asserts that
geometry rather than assuming a radius.

The census, measured: `fire_diamond.ent` has 5 placements across 5 levels -
level15c, level28b, level29b, level30b and level31b. `gutter_mouth.ent` has 3, in
level29b, level30b and level31b - every one of them in a level that also places a
fire diamond, and none anywhere else in the game. level30b puts its drain at
(384, 182) directly under its diamond at (384, 134). **level28b places no drain**,
which makes taking that one a one-way choice. level15c sits its diamond between
two `barrel_bomb.ent` at (320.5, 98.5) and (320.5, 142.5), which is the mechanic
stated outright: take it, shoot the bombs.

Also corrected here: `Fire.hpp` said `burnProjectile` "puts out a flying
projectile.ent", which reads as *extinguishes* when the truth is *converts*. That
line was written two steps ago and was wrong.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_diamonds` at 126 checks, `test_mp_keys` unchanged at 104, `test_mp_fields`
at 63 and `test_mp_minions` at 77, and 98 of 128 levels starting with 95 playing on
both. `test_mp_shot` (90), `test_mp_portal` (37) and `test_mp_layer` (205) are
unchanged on both toolchains, which is the measurement that says the portal path
did not move. `test_mp_demolish` ran green under MSVC this time, so the Smart App
Control artifact noted in step 26 did not recur.

One honesty note about what the count measures. `test_mp_start` counts a level as
playing when no role is left inert, which is not the same as proving it can be
finished. level28b places a fire diamond and **no** gutter mouth, so taking that
diamond is irreversible and the level must be soluble either with fireballs alone
or by leaving the diamond where it lies. That is a claim about the original's
design rather than about this port's faithfulness, and it has not been verified by
playing it - recorded here rather than left for the count to imply.

What is left of chapter 3: `boss_spawn`, in level31b alone.

## Step 28 - chapter 3: the ghost of level 3-32, which only fire can hurt (built)

Chapter 3 goes from 31 of 32 playing to **32**, and the game from 95 of 128 to
**96**. **Chapter 3 is complete.** Worlds 1 and 3 now play in full, and world 2 is
short exactly one level: level31a's dragon.

**The fire diamond is the weapon, and that is why this step had to follow it.**
The ghost's SHOOTING arm ends with `isBurned(this)`: burned, it goes to DAMAGE,
takes `hp += -1`, calls `healBurn(this)` and sounds. Nothing else in level31b can
burn anything at all. The only fire in the level is the fireballs a portal shot
*becomes* while a fire diamond is carried - step 27's mechanism. Built in the
other order this would have been an invulnerable boss, and nothing in the suite
would have said so, because every other test would still have passed.

**It is the game's second boss and shares nothing with the first**, so it is its
own module and `Boss.hpp` stays the beholder's. Nothing in `Boss::State` - no
rocks, no spikes, no button - serves any of it, and `test_mp_boss`'s 126 checks
are untouched as a result, which is the same argument that kept `Portals` out of
step 27 and paid off there too.

| | the beholder (1-32) | the ghost (3-32) |
|---|---|---|
| moves by | a glide with a wobble | a WaypointManager path |
| hurt by | a rolling stone rising through it | FIRE, and nothing else |
| hp | 2 (three hits) | 3 |
| radius | 48 | 64 |
| when hurt | fires rings of spikes | summons an escort and waits |
| on death | raises the level's button | drops a key |

**The rage loop would have deadlocked on the obvious reading**, and this is the
trap of the step. RAGE_MODE waits for the minion it summoned to be killed, and it
seeks that one *by name*: `SeekEntity('minion.ent')`, which is what its
`spawnMinion` gives it. But level31b **also** spawns a patroller of its own at
level start - and that one is a `ghost_minion.ent`, a different entity, which
never satisfies the seek. Counting every minion in the level would leave the count
never zero, so the boss would sit in rage for ever and never summon: not a slow
fight, a hang. `Game` counts only the escorts this boss named.

**The red line gates the shooting.** Every 3000 ms it seeks `dark_mage.ent` and
fires `addEnemyShoot` from `scale(-20, -32)`, but only when the player's x is past
the x of the level's `red_line` entity - (262, 110) in level31b. A player on the
left of the arena is left alone. The suite uses that as a lever: parked behind the
line the boss never shoots, so three whole damage cycles can be driven without the
player being killed in the middle of them.

**`killPortal` stays deferred.** `minions.json` set that branch aside as a step of
its own because it reaches into portal state nothing else in this port writes from
outside. It is *not* inherited here: it fires only when a shot's target is named
`portal.ent`, and this boss always passes the player as its target. The ghost's
shot only kills the player, with the same `fakeRadius = scale(16)` that file
already decoded.

**The fight is the lock.** level31b holds a keyhole at (152, 204) and a
`door_locked` at (120, 160), both `metadata/color` yellow, and **no key anywhere**.
The only key in the level is the one this boss drops when it dies. The key is
yellow because `key.ent`'s own CustomData carries a single string variable,
`color`, valued yellow - the level agrees with the entity rather than being the
source of the value, and taking it from the keyhole would have been right by
accident in exactly one level.

It also **restocks the player** on that same 3000 ms beat, with
`addEntityIfItCantBeFound`: a barrel bomb at `barrel_spawn` and a fire diamond at
`fire_diamond_spawn`. The fire diamond restock IS built - a diamond is state in a
plain vector, not a body - so dropping one down level31b's gutter mouth is
survivable rather than final. The barrel bomb restock is not: `Fire` fills its
bombs from the scene, and a mid-run bomb is the same mid-run-Add step
`minions.json` already names for `Fire::Burnable` and `Demolish::Breakable`.

**Where it starts is a consequence, not a choice.** In the original a `ghost.ent`
is created by a dying `ghost_minion` and flies *to* `ghost_pos`. The port does not
build that transformation - `minions.json` records that a marker's `entityName` is
never honoured - so the ghost begins at `ghost_pos`, its four appear waypoints all
coincide, and the appearance keeps its DURATION (1200 + 2500 + 50 + 0 = 3750 ms)
and loses its path. What those waypoints drove besides position - the colour, the
alpha, the swelling scale - is presentation this port has no renderer for anyway.

**And the honesty trap was not there.** Admitting `boss_spawn` to
`Roles::IsPorted` would have claimed level31a's unbuilt dragon - the exact mistake
`pickup` was held out of for two steps. It does not arise: `test_mp_start` already
asks per NODE (`role == kBossSpawn && Boss::Plays(...)`), so the role stays out of
`IsPorted` and the predicate simply learns a second boss. level31a's dragon and
chapter 4's dark dragon stay inert and stay honestly counted.

Three corrections to things written earlier, two of them mine. `Minions.hpp` and
`Minions.cpp` both said level31b has a marker "carrying no waypointName at all".
It does not: `minion_spawn_908` carries `"wayB"` like every other marker in the
game. The node that carries none is `minion_spawn_point`, which has **no role at
all**, is never collected by `Minions::Find`, and is this boss's summon anchor.
`data/minions.json` had it right and the code comments did not; the
empty-`waypointName` branch is defensive rather than exercised, and now says so.
Third, `Ghost` stopped declaring a `Waypoint` of its own: the escort's patrol IS
an ordinary minion patrol, so it uses `Minions::Waypoint`, which is what
`Minions::Summon` takes. Two identical structs for one thing was the defect; the
conversion error was only the symptom.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_ghost` at 73 checks, `test_mp_minions` unchanged at 77, `test_mp_boss` at
126, `test_mp_keys` at 104 and `test_mp_diamonds` at 126, and 98 of 128 levels
starting with 96 playing on both. The ghost fired once in the 3333 ms after the
player crossed the red line - one beat of its 3000 ms interval - and took 3 burns
before dropping a yellow key, identically on both toolchains.

`test_mp_boss` and `test_mp_minions` being unchanged is the measurement that
matters here, in the same way `test_mp_shot` and `test_mp_portal` were in step 27:
a second boss was added beside the first and a second way to build a minion beside
`Spawn`, and neither disturbed what was already pinned.

What is left of the whole remaster: `boss_spawn` in level31a alone - chapter 2's
dragon, which is five callbacks and a knight of its own - and then chapter 4,
where `no_gravity` refuses 18 levels and `darkest` refuses 12.

*(Corrected by step 29: that sentence was imprecise. `gravity_well` was inert in
level27c at the time too, which "and then chapter 4" covers only loosely -
level27c is a chapter-4 level that already started. The inventory had reported it
all along.)*

## Step 29 - chapter 4: `darkest` carried rather than refused, and the role it uncovered (built)

Chapter 4 goes from 2 of 32 starting to **14**, and the game from 98 of 128 to
**110**. Playing moves by one - chapter 4 from 1 to 2, the game from 96 to **97** -
and that small number is the honest point of this step rather than a
disappointment in it. `darkest` was the only thing **refusing** those twelve
levels. It was never the only thing in them.

**What the flag actually is.** The original's level-properties reader does
`CheckCustomData('darkest')` and, when it is set,
`SetAmbientLight(DARKEST_AMBIENT_LIGHT)` - and that single call is the whole of
the flag in that function. `DARKEST_AMBIENT_LIGHT` is built at
`Game.angelscript` bytes 306783..306841 as `vector3(0.01, 0.01, 0.01)`: very
nearly black rather than black. `art.json` records that number where a renderer
would look for it.

**This port has no ambient light at all.** `Sprites` carries no colour and no
tint, so there is nothing for 0.01 to multiply, and a dark level draws exactly as
a lit one either way. Refusing to start the level did not make that more honest -
it hid twelve levels whose every role the port already plays or already does not.
So the flag is CARRIED on `Game::Level` and acted on by nothing, and says so in
`art.json`, in `Game.cpp` and in the suite.

The one decoded gameplay consequence is not drawing at all:
`canSeeAnything = GetAmbientLight() != DARKEST_AMBIENT_LIGHT`, so a minion in a
dark level is **blind**. `minions.json` recorded that and said there was "nothing
to build against until `darkest` exists". It exists now; minion sight is still the
step it always was, so the blindness costs nothing today and is the first thing
that has to be honoured when sight lands.

**`no_gravity` is still refused, and the two flags are no longer the same case.**

| | `darkest` (12 levels) | `no_gravity` (18 levels) |
|---|---|---|
| in the original | one `SetAmbientLight` | world gravity to zero, AND read again in three classes |
| the mechanic | none | `PortalManager` calls `applyImpulse`: the recoil of a shot is how you move |
| what this port has | no ambient light to change | a solver that would simply drop everything |
| so | carried | refused, and honestly |

Started without the impulse, a zero-gravity level would drop everything in it and
still look like a level that works, which is exactly what that guard exists to
prevent.

**What the twelve levels turned out to hold**, which is the real yield of the
step. The inventory can now see inside levels it had never opened, and it found a
role the port has never met:

```
  10  torch: level21c level22c level23c level24c level25c level26c
              level28c level29c level30c level31c
   2  boss_spawn: level31a level31c
   2  gravity_well: level20c level27c
```

`torch` is new. It was invisible for as long as every level carrying it was
refused, and it is now the single largest blocker left in the game.

**A test that was pinning the limitation, not a regression - and I called it one
before reading it.** `test_mp_layer`'s `NSkipsWhatThePortRefuses` loaded level26c,
asserted `SimLevel() == nullptr`, and asserted the load error contained the word
`"darkest"`. All three failed the moment level26c started, which is precisely what
this change intends. The test is about the SKIP, and about a refused level leaving
nothing behind - both still true while `no_gravity` refuses eighteen levels - so
it is pointed at level18c and level19c, a pair that is still the shape it is
testing, rather than deleted.

**And the play floors were raised, which they should have been three steps ago.**
`test_mp_start`'s own header says "the floors are raised as roles land", but only
the START floors ever had been: chapters 2, 3 and 4 all carried a play floor of 0
while measurably playing 31, 32 and 1. A regression that stopped thirty-one levels
playing would have passed in silence. They now hold what both toolchains measure.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_start` at 534 checks with 12 of 12 darkest levels starting and carrying
the flag, `test_mp_layer` back to 205 with the skip test exercising level18c into
level19c, and 110 of 128 levels starting with 97 playing on both.

What is left of the whole remaster, and the inventory can now see all of it:
`torch` in 10 levels, `no_gravity` in 18, `boss_spawn` in level31a and level31c,
and `gravity_well` in level20c and level27c.

## Step 30 - chapter 4: the torches, and the wall of light one drops (built)

Chapter 4 goes from 2 of 32 playing to **11**, and the game from 97 of 128 to
**106**. That is the largest movement of the whole remaster, and it is a dividend
of step 29 rather than of this step: admitting `darkest` barely moved the count
itself, but it let the inventory see inside twelve levels it had never started,
and `torch` was what it found in ten of them.

**It is a toggle, not an unlock**, and that is the whole mechanism.
`entity_roles.json` calls a torch "what makes the dark levels navigable" - right
about the importance, wrong about the shape. The navigating is not the light.
Shoot the unlit torch (`light_off.ent`) and it lights, and the level's **wall of
light** begins to go. Shoot the orbiting flame that appears in its place and the
torch goes back out and **the wall comes back**. A port that built only the first
half would play all ten levels correctly and still be wrong about what it built.

| | what it looks like | what it is |
|---|---|---|
| a torch | scenery in a dark room | a switch that removes a solid |
| lighting one | the room gets brighter | your shot is consumed, and a wall starts to go |
| the flame left behind | an effect | the way to undo it, and it is moving |
| the 1500 ms | a fade | the window in which the signal can exist at all |

**The shot is spent either way.** `hasProjectileAround` polls `scale(24)` around
the torch, and lighting one calls `killProjectile` - the same bare `DeleteEntity`
the fire diamond's conversion uses - so a shot that works a switch never goes on
to open a portal. The port runs the torch block **before** `Portals::Tick` for
exactly that reason.

**A fireball works a switch too.** `hasProjectileAround` accepts `projectile.ent`
or, with the flag its call sites pass set, `fireball.ent`. So a fire diamond's
fireballs light torches - the third place step 27's mechanism has turned out to be
load-bearing, after the ghost being burnable and the ghost's own diamond restock.

**The 1500 ms is load-bearing, not a flourish.** `destroy()` only *flags* the
wall; `ETHCallback_light_wall` counts to 1500 ms before deleting itself.
`addFireSignalIfNecessary` runs immediately after the destroy and only spawns a
signal while a `light_wall.ent` can still be **found** - which it can, precisely
because the wall is flagged and not yet gone. Take the wall away on the frame it
is flagged and no signal can ever appear, and the toggle silently collapses into
the one-way unlock it is not.

**The signal orbits** rather than sitting where the torch stood: it keeps an
`originalPos`, spins an angle at 300 degrees a second, and places itself at
`originalPos + scale(18) * (-cos, sin)`. What the player must hit to undo their own
switch is moving, and the suite asserts that it moves rather than only that it
exists.

**The wall goes through Demolish.** `light_wall.ent` carries `metadata/breakable`,
so it is already a `Demolish::Breakable` with a body and a box that Demolish walks
every frame. Torch reports, Game funnels, Demolish's `broken` is set and its body
cleared - rather than a body destroyed behind a list still walking it, which is
the rule `Minions::Take` exists for. Putting the wall **back** is the only place in
this port that adds a level's own body mid-run, through
`LevelBuilder::BuildEntity`.

`Game::AfterStep` now takes the `Data`, as `BeforeStep` does, because that rebuild
needs the scene - the wall node's body and shape are its children. The alternative
was for `Level` to borrow a scene pointer, a lifetime hazard introduced for one
feature. There were three call sites, so the signature moved instead.

**Why this step needed its suite more than most.** `test_mp_start` counts ROLES,
and `light_wall.ent` is in no role table at all - `RoleOf` answers empty and
`IsPorted` answers true. The moment `torch` was admitted, all ten levels would
read "playing" whether or not the wall ever moved, with a solid 30 x 126 body
still standing across six of them and nothing in the inventory able to notice.
`test_mp_torch` therefore watches the **body**: alive at 1.4 s, gone from the
registry after 1.5 s, and solid again once the signal is shot.

**A counting mistake of mine, recorded because it nearly shipped.** The first pass
said three torches per level and 30 in the game. There is **one** per level and
**10** in the game. The three came from counting grep LINES rather than nodes - a
torch is a node header, a `metadata/entity_name` line and a child `Sprite` line.
The identical trap had been caught for `light_wall` (5 lines, 1 wall) an hour
earlier in the same session and was walked straight back into. Counting lines is
not counting entities, and `torch.json` now says so where the census is.

The census, measured: 10 torches across 10 levels, one each - level21c to
level26c and level28c to level31c, every one a `darkest` level. 6 light walls
across 6 levels, one each - level25c, level26c, level28c, level29c, level30c,
level31c. So **four** of the ten (level21c to level24c) have no wall at all, and
there a torch only lights the room and spends a shot; those four were already
finishable and the other six were not.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_torch` at 80 checks, `test_mp_layer` unchanged at 205 through the widened
`AfterStep`, `test_mp_start` at 534, and 110 of 128 levels starting with 106
playing on both. Chapter 4's play floor is raised from 2 to 11 to match, with the
three that still do not play named: level20c and level27c hold `gravity_well`, and
level31c places chapter 4's own boss beside its torch.

What is left of the whole remaster: `no_gravity` in 18 levels - the only one of
the four that is a genuine mechanic rather than a gap, since a portal shot's
recoil is how the player moves there - `boss_spawn` in level31a and level31c, and
`gravity_well` in level20c and level27c.

## Step 31 - chapter 4: zero gravity, and the recoil that moves you (built)

Chapter 4 goes from 14 of 32 starting to **32**, and from 11 playing to **24**.
The game goes from 110 of 128 starting to **128**, and from 106 playing to
**119**. **Every level in Magic Portals now starts.** `no_gravity` was the last
thing in the whole game that stopped one, and nothing refuses a level any more.

**It is a movement mode, not a flag**, which is exactly why it was refused for so
long. One property turns on three separate things, and a level built with any one
of them missing still starts and cannot be finished:

| | |
|---|---|
| the world | `setGravity(V2_ZERO)`, against `(0, scale(10))` - NOTHING falls, not the player, not a crate |
| the buttons | `MainCharacter::update` skips `ScreenPad::update` entirely. There is no walking at all |
| the tap | `PortalManager` calls `applyImpulse`, and that is the only way to move |

**The number that nearly went wrong, and the coincidence that nearly sold it.**
`applyImpulse` is
`SetLinearVelocity(GetLinearVelocity() + normalize(playerPos - finalPos) * scale(1.4))`.
`scale` is one multiply by `m_scaleFactor`, which is `GetScreenSize().y /
m_absoluteSize` with `m_absoluteSize` **480**, so at the reference height the call
is the number itself. The same wrapper wraps the world's gravity as `scale(10)`,
and `Units.hpp` already has that 10 decoded as Box2D's `DEFAULT_GRAVITY` in metres
per second squared - so the 1.4 is **metres per second**, which the engine's
velocities already are, and needs no conversion at all. 70 px/s at
`kPixelsPerMetre` 50.

The wrong road was very inviting. The remake's player gravity is 1200 px/s^2 and
the original's is 10, a ratio of 120, which would have made the recoil 168 px/s -
sitting beautifully beside the remake's walk speed of 160, one walking-speed per
shot, far too neat to be an accident. It *was* an accident: `kPixelsPerMetre` is
50, and `player.json` marks its own 1200 `_guess` and says in as many words that
it "will NOT match". A coincidence between a guess and a real number is not
evidence, and chasing `m_absoluteSize` instead of admiring the arithmetic is what
caught it.

**Every accepted tap shoves, landed or not.** `managePortalInsertion` loads
`m_noGravity` at instruction 158 and jumps past the call at 161, so the shove
happens only in these levels; and the call at 166 sits AFTER the `hasFailed` join
at 157, where the vibrate block at 149-156 is the only thing on the failed side.
So a tap that goes on to hit a wall moves the player exactly as one that opens a
portal. The port applies it in `Portals::State::Shoot` - the same seam, before the
shot has resolved - and the suite asserts the shot is still in the air when the
velocity changes.

**Added, never set.** `GetLinearVelocity()` at 48, `opAdd` at 51, `SetLinearVelocity`
at 57. Shots accumulate, and drift is the mechanic; a set would reset the player
on every tap and make these eighteen levels a different game.

**`Player::Steer` is not edited.** It is the most heavily pinned function in the
port - `test_mp_play` walks level30's seam through it at friction 0 and 1 - and
the whole mode is achieved by not CALLING it. That one absence is both halves of
the original's behaviour at once: no walk, and no fall (`Player.hpp` records that
Steer applies gravity every tick deliberately, a dynamic body having no floor
snap). Branching inside it would have reached into the same function every level
in the game steers through; skipping the call reaches nothing.

**A resolution bug, recorded rather than matched.** `m_scaleFactor` is screen
height over 480, so the original's recoil grew on taller screens - 1.4 at 480,
3.15 at 1080. The port takes the reference value. `zerog.json` says so, along with
the smaller divergence that the original aims away from `finalPos` where the port
aims away from the tap.

**What the step uncovered, as `darkest` uncovered `torch`.** Opening eighteen
levels made the inventory able to see inside them, and it found `bouncer` in
level1c, level6c and level16c - "a solid polygon surface that throws things off
it, 64x64, shape=3, static, non-sensor", real geometry rather than a trigger. It
had been read earlier in the session, in `setNoGravityLinearMotionProperties` and
`ETHCallback_bounce`, and written off as a decorative bob. That was right about
the player's physics and wrong about the question: it is a ROLE the port owes, and
asking only "does this move the player?" missed it. The inventory caught what the
reading did not.

**A settle is not a fall, and the test was fixed rather than loosened.** The first
run failed three levels for drifting 3.75 px in three seconds. The tempting fix is
a wider threshold. The truth is that 3.75 px appeared after ONE second and after
THREE alike: these levels place the player overlapping what is under it and the
solver pushes it out once, then stops. So `test_mp_zerog` now measures the two
seconds AFTER the settle and gets **0.0000 px** - and a second of RIGHT held down
also moves it **0.0000 px**. A fall, for scale, is some 4400 px over the same
three seconds.

**A test that lost its subject.** `NSkipsWhatThePortRefuses` named a level the
port turned away - level26c for `darkest`, then level18c for `no_gravity` - and
checked that the skip got past it and that a refusal left nothing behind. There is
now no such level anywhere in the game, so it was not re-pointed a third time; it
was reshaped. What survives is the half that was always the point - that moving on
UNLOADS what came before - measured against a FRESH attach of the same level, so a
level left behind shows up as a count that does not match. It holds exactly.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_zerog` at 81 checks, `test_mp_layer` at 206 through the reshaped skip,
`test_mp_start` at 552, and **128 of 128 levels starting with 119 playing** on
both. Chapter 4's floors are raised from 14/11 to 32/24.

The eight chapter-4 levels that still do not play are the five holding
`gravity_well` (level16c, level17c, level18c, level20c, level27c), the three
holding `bouncer` (level1c, level6c, level16c - seven distinct levels, because
level16c holds both), and level31c's boss.

What is left of the whole remaster, and all of it now visible rather than hidden
behind a refusal: `gravity_well` in 5 levels - one callback family,
`ETHCallback_gravity_agent` with `retrieveRondaboutCount`, `computeRoundabout` and
`ETHCallback_gravity_area` - `bouncer` in 3, and `boss_spawn` in level31a and
level31c.

## Step 32 - chapter 4: the bouncers, and a role table that was wrong (built)

Chapter 4 goes from 24 of 32 playing to **26**, and the game from 119 of 128 to
**121**. All 128 still start. level1c and level6c play; level16c does not, because
it places a gravity well beside its bouncer.

**The role table is wrong about this one, and that is the whole finding.**
`entity_roles.json` calls a bouncer "a solid polygon surface that THROWS THINGS
OFF IT, 64x64, shape=3, static, non-sensor". The geometry half is right and the
throwing is not. `ETHCallback_bounce` imparts no impulse to anything, ever: it
checks whether `speed` is set, injects the defaults if not, and calls
`linearMotion`, which moves the slab ITSELF. Anything that gets thrown off is only
what any moving solid does to what rests on it. A port that believed the table
would have built a trampoline that exists nowhere in the original.

**What it actually is**, decoded in full:

    angle  += unitsPerSecond(speed)        // 1.2 rad/s, min(200 ms) frame clamp
    offset  = cos(angle) * stride          // stride 1.0
    pos     = originalPos + getScale() * rotateZ(0) * (0, offset, 0)

`ETHCallback_bounce` passes `vertical = true`, so it bobs up and down; `rotateZ`
is by an angle of 0 and turns nothing. **The amplitude is one pixel** - `stride`
is 1.0 and `getScale()` is 1 at the 480-tall reference, the same factor
`zerog.json` decodes - and a full bob takes 2*PI/1.2 = **5.24 s**. It is
decoration, and it is built anyway, because the alternative was to admit the role
without the motion, which is what `torch` nearly shipped with `light_wall.ent`.

**The numbers come from the binary, not from the levels**, which is the reverse of
every other mover in this port. `setNoGravityLinearMotionProperties` writes
`speed = 1.2` and `stride = 1.0` onto the entity, and no `bounce` node in any
level carries a speed, a stride or a direction - all four were checked.
`Mover::OscillationFromNode` refuses a node without speed and stride for exactly
that reason, so it could not be reused even had the curve matched.

**A sibling of `Mover::Oscillation`, not a use of it**, on four counts: cosine not
sine, no half-stride, a rate in radians per second with a frame clamp rather than
`rateScale * speed * t`, and constants from the binary rather than from a node.
What IS shared is `Supersonic::DetMath`, so a bob comes out the same on every C
runtime as a swing does, and `Mover::MoveKinematic`.

**The real work was `Roles::Moves`, not the motion.** A `bounce` node is authored
as a `StaticBody2D`, and `LevelBuilder` only makes a body kinematic when
`Roles::Moves` says the role moves. Without that one line the module would have
computed perfect positions every frame onto a slab that carried nothing standing
on it - and at ONE PIXEL of travel, no test of where the slab is could ever have
noticed. So `test_mp_bounce` asserts the BODY is kinematic, as `test_mp_torch`
watches a body rather than a role count. This is also the first step in the
remaster to change a body's TYPE at build time, in three levels that already
passed; none of them stopped landing.

**cos(0) = 1, so a bouncer starts at full displacement**, a whole stride from the
node the level places it on. The node is the MIDDLE of the bob and never where it
begins - the same trap `fields.json` records for the shock rings, and the one a
sine would have got wrong while looking right in motion.

**A failure worth keeping rather than tidying away.** Building this broke three
stillness assertions in `test_mp_zerog`, which were pointed at level1c: the player
rests on that level's bouncer and rides it, 2.3919 px over two seconds. The cheap
fix is to widen a tolerance; that would have buried the only observable evidence
anywhere that the slab carries its passenger. Instead the stillness checks moved
to level2c - five crystals, a door, a block, and nothing that moves, where the
player now measures **0.0000 px** - and the ride became an assertion of its own in
level1c. `test_mp_start`'s tolerance note was corrected too: it gave one cause for
that drift where there are two, a one-off settle AND a bouncer's ride.

**The orbit counter is recorded, not built.** `computeRoundabout` keeps
`roundSum_`, `cwRoundCount_` and `ccwRoundCount_` per body, and
`retrieveRondaboutCount` returns the sum of the two directions. One grep found its
only consumer: `checkForLevelAchievements` in `ScoreDashboard.angelscript`, which
calls `dispatchAchievement(60, notify, 2500)` when the player has orbited three
times. It is achievement #60, a toast, and the whole achievement system is
unported - `Keys.hpp` and `Minions.hpp` already record theirs the same way. Worth
the grep rather than the guess: had it been a goal, a gravity well that attracted
correctly and never counted would have left five levels unfinishable.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_bounce` at 189 checks reporting 4 bouncers across 3 levels,
`test_mp_zerog` at 84 through the relocated stillness checks and the new ride,
`test_mp_start` at 552, and **128 of 128 levels starting with 121 playing** on
both. Chapter 4's play floor is raised from 24 to 26.

Seven levels in the game still do not play: `gravity_well` in level16c, level17c,
level18c, level20c and level27c, and `boss_spawn` in level31a and level31c.

## Step 33 - chapter 4: the gravity wells, and a zone no level file mentions (built)

Chapter 4 goes from 26 of 32 playing to **31**, and the game from 121 of 128 to
**126**. All 128 still start. **Two levels in the whole game are left**, and both
are bosses: level31a's dragon and level31c's dark dragon.

**A well is three things, and only two of them are in the level file.** The node
gives a position, a radius, a `StaticBody2D` and a `CircleShape2D` - so the solid
circle builds itself (a portal shot already died on it: `Shot::FirstBody` walks
sphere colliders and skips only triggers), and the attraction can be read off the
radius. The third is invisible: `ETHCallback_gravity_agent` **adds an
`antiportal.ent` at its own position on its first tick**, and no `.tscn` in the
game mentions it. A port built from the level data alone would let a portal open
inside a well while every role count still read "playing" - the same blindness
`light_wall.ent` had before `test_mp_torch` watched a body. `test_mp_wells`
asserts it through `TryPlace`.

**The force**, decoded from `ETHCallback_gravity_area` (bytes 461443..462571):

    forceDir  = normalize(centre - body)          // TOWARD the centre: it attracts
    forceBias = smoothEnd(1 - d^2/r^2)
    force     = forceDir * forceBias * forceLength * (dt_ms / 16.6666)
    velocity += force                             // added, never set

The operand order was checked rather than assumed, because it is the entire
difference between a well and a fan: `vector2::opSub` takes `thisPos` as the
object and `bodyPos` as the argument, so the pull runs inward. Only DYNAMIC
bodies feel it - `DynamicBodyChooser::choose` rejects `IsStatic()` and anything
with no physics controller, so platforms and walls stand still inside a well.

**Two strengths, and the branch is the world's gravity.** `GetGravity() ==
V2_ZERO` gives **0.18**, anything else **0.5** - which splits these five levels
exactly: level16c, level17c and level18c are `no_gravity` levels; level20c and
level27c are not. The stronger pull is where gravity is already fighting it. The
port reads `Game::Level::noGravity` rather than interrogating the registry,
because that flag is what set the registry's gravity in the first place.

**The zone radius is `radius - 12`, not `radius`.** The agent sizes its antiportal
with `scaleToSize(area, vector2(w, h))` where `w = h = radius*2 - scale(24)`, and
`scaleToSize` reads `GetSize()` and calls `Scale(size / currentSize)` - so that is
the entity's SIZE. An antiportal refuses a tap within `GetSize().x * 0.5`. Halving
gives `radius - 12`, twelve pixels at every one of the ten placements, and
`radius` was what these notes said before the arithmetic was followed through.
`Portals::NoPortalZone` gained an explicit `radiusPx` for it - the existing
`antiportalRadiusPx` is a constant 64 times a node's scale, and these zones run
52 to 248 px - defaulting to zero so every antiportal already in the game is
untouched.

**The census, measured by reading every `metadata/radius` in chapter 4**: ten
placements across five levels - level16c 106; level17c 106, 140, 106, 106;
level18c 140, 64, 64; level20c 260; level27c 128. A correction worth recording:
`test_mp_zerog` prints "8 gravity wells across 3 of the eighteen", which is right
for what it measures - it walks the eighteen zero-gravity levels only, so level20c
and level27c were never in its loop. The game-wide total is ten across five, and
the 8 nearly became a documented fact.

**One number is inferred and says so.** `smoothEnd(v)` is `sin(v * PIb)`, and
`PIb` is a global the decoder renders by name and never defines - every appearance
is a use. Three independent ones fix it at PI/2: `smoothEnd` must reach 1 at
v = 1; `PI + PIb` is a half-turn plus a quarter; and `getAngle` brackets its
quadrants with `PIb/2` and `PI + PIb/2`. `gravitywell.json` records it as
**inferred, not decoded**, because a data file that calls an inference a decode is
worse than one that admits the gap.

**The orbit counter is recorded, not built.** `computeRoundabout` keeps
`roundSum_`, `cwRoundCount_` and `ccwRoundCount_` per body and
`retrieveRondaboutCount` sums both directions. One grep found its only consumer:
`checkForLevelAchievements`, calling `dispatchAchievement(60, notify, 2500)` after
three orbits. Achievement #60, a toast, and the achievement system is unported -
`Keys.hpp` and `Minions.hpp` record theirs the same way. Worth the grep rather
than the guess: had it been a goal, a well that attracted perfectly and never
counted would have left five levels unfinishable.

**Two test premises expired, and both were rewritten rather than relaxed.**
`test_mp_bounce` pinned `!IsPorted("gravity_well")` as proof that admitting
`bouncer` had not admitted everything - a guard doing its job, failing the moment
the thing it pinned stopped being true. It now pins `kBossSpawn`. And
`test_mp_start`'s zero-gravity check rests on "nothing pulls it and nothing steers
it"; a well is precisely a thing that pulls it, so level17c - which holds FOUR,
more than any level in the game - moves a stationary player by design.

That second fix was nearly the wrong one twice. Widening the tolerance would have
been the settle-versus-fall mistake a third time. Worse, the first draft read
`outcome.landed = pulled ? registry.valid(level.player) : ...`, which is
unconditionally true - a check-shaped hole, written in the same hour as a note
saying exemptions were dishonest. What stands instead asserts something that can
actually fail: the drift is finite and under 1000 px, which catches the two things
a newly written force plausibly gets wrong - a `normalize` NaN at dead centre, and
runaway acceleration.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_wells` at 47 checks reporting 10 wells across 5 levels, a zone refusing a
portal within 94 px of (256, 128) and a player pulled from 53.0 px to 18.7 px of a
well in half a second; `test_mp_bounce` at 189, `test_mp_zerog` at 84,
`test_mp_start` at 552, and **128 of 128 levels starting with 126 playing** on
both. Chapter 4's play floor is raised from 26 to 31.

What is left of the whole remaster: **two bosses**. level31a's dragon
(`ETHCallback_dragon`, a claw, a knight and a knight spawn, `isDestroyableByClaw`)
and level31c's dark dragon.

## Step 34 - chapter 2: the dragon that cannot be killed, and a camera no level asked for (built)

Chapter 2 goes from 31 of 32 playing to **32**, and the game from 126 of 128 to
**127**. All 128 still start. **One level in the whole game is left**: level31c's
dark dragon.

**The role table calls this boss a placeholder, and it is 8.2 KB of bytecode.**
`entity_roles.json`'s `scenery_fx` note reads "`dragon_claw.ent` - part of the
chapter-2 boss, which is a placeholder". The boss is `ETHCallback_dragon` (bytes
380209..383437), a claw (383726..386175), `isDestroyableByClaw` (383437..383726),
a knight (386379..388631) and a knight spawn (386175..386379) - five callbacks.
That is the second correction this port has made to the role table, after
`bouncer`, and both are recorded where a reader of the table would look.

**It has no hit points and no body.** There is no hp, no damage arm and no death
arm, and the node carries no `<Body>` - so nothing can hurt the dragon and the
dragon cannot touch the player. **Level 2-32 is won by reaching the exit** while
it strafes you and its claw eats the floor behind you. Every other boss in the
game is a fight; this one is a chase, and building it as a fight would have been
building the wrong thing.

**The one level in 128 that sets `auto_camera`, and the port never read it.**
level31a's `properties` carries `auto_camera = "dragon.ent"`; no other `.tscn` in
the game has the key at all, and neither this port nor the remake's GDScript
honoured it. `Game::Game` branches on it (110437..111736) and builds an
`AutoCameraController` in place of the ordinary one, whose entire update is:

    masterPos = master.GetPositionXY()                // the dragon
    cameraPos = vector2(min(max(camMin.x, masterPos.x), camMax.x), 0)
    SetCameraPos(cameraPos)

A **hard lock** to the followed entity's x. No lag, no easing, y pinned to 0.
`ICameraController`'s constructor sets `camMin` to (0,0) and `camMax` to
`findCamMax()`, which is `SeekEntity("max").GetPositionXY() - GetScreenSize()` -
the same arithmetic as `Camera::Clamp`, expressed for a top-left camera.

**That is why the claw needed no camera passed in**, and it was the design
question this step opened with. Every number in the claw is measured from
`GetCameraPos()`, and `GetCameraPos()` here *is* the dragon - so the sim derives
the camera from the boss it already owns, `Game::Tick` keeps its signature, and
the layer supplies only the view's WIDTH. The clamp that width feeds does not bite
until x > 2177, past `take_off` at 2160, where the dragon has already climbed away.

**`GetCameraPos()` is the view's top-left corner**, and that was checked rather
than assumed: `eth_util.angelscript`'s `isPointInScreen` subtracts it from a point
and tests the result against `[0, GetScreenSize()]`. Read as a centre - which is
how the port's own `Camera.hpp` reads `camera_start` - every margin in the claw
would have sat half a view, some 227 px, to the left of where it belongs.

**Two readings were corrected by going back to the listing rather than to notes.**
The claw has **six** waypoints, not five: `addWaypoint` is called at ins 54, 82,
107, 132, 157 and 182, and the last two are identical - which is exactly why
`setCurrentWaypoint(getNumWaypoints() - 2)` leaves it motionless rather than
parked mid-swing. And the dragon fires **before** it takes off, not after: ins
634-637 load `tookOff`, negate it, and reach the `elapsedTime > 4000` test on that
branch, while the fallthrough clears the gate. The same idiom agrees twice more -
the follower tracks the player while `!tookOff` (178-181) and the animation runs
while `!tookOff` (531-534). Both had been written down the other way round.

**The sight gate is asked inside out, and the data file says so.** The original
fires only if `GetClosestContact(dragon -> player)` returns something
`isCharacter`. `Shot::FirstBody` walks box, sphere and hull colliders; the player
is a **capsule** (`Player::Spawn`), so `FirstBody` can never return it - the first
cut of this module asked `hit->body == player` and produced a dragon that never
fired once. Teaching `FirstBody` about capsules would have made every portal shot
in the game stop on the player, which the original does not do. Since the segment
ends at the player, "the closest thing is the character" and "nothing solid lies
before the endpoint" are one statement about one world, and the second is the one
this port's primitive can answer. `test_mp_dragon` pins **both** sides: a shot
taken through clear air, and a shot refused with a floor in the way.

**What the claw takes is scenery, which settled the ownership question.** The four
names `isDestroyableByClaw` accepts - `double_block_plat_no_emissive.ent`,
`platform`, `single_block_plat_no_emissive.ent`, `block00_no_emissive.ent` - are
in no role at all in `entity_roles.json`. So a crushed platform is not a mover,
not a breakable and not a traveller: nothing walks a list behind it. The removal
still goes through `Game`'s `Forget` funnel, as a burnt crate does. level31a
places 22 of them, counted from the file rather than assumed.

Ethanon gathers the claw's candidates from four spatial-hash buckets around the
camera, and the port walks the level's own whitelist instead. Stated as an
equivalence that holds **here** and for a reason: level31a is 256 px tall and the
view is 256 px tall, so the bucket neighbourhood spans the level's whole height
and only the two x tests are live.

**The knight is claimed without being built**, and that is the one claim in this
step not backed by a mechanism. Its spawn does nothing until
`GameStateController::isFinished` - the port's `Goals::completed` - and the layer
leaves a level on the tick that goes true, so none of it could ever be seen.
`dragon.json` says so in the open, and what makes the claim honest is that
level31a's PLAY does not rest on it: `test_mp_dragon` asserts the dragon flies,
the claw takes the floor, the bodies leave the registry, and the exit's ground
survives the camera's clamp.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite,
`test_mp_dragon` at 151 checks reporting a 6-point 1500 ms swing sweeping every
250 ms, 22 platforms the claw may take, the camera's left edge at 286.0 with the
dragon at x 286.0 after ten seconds, 10 of 22 taken by thirty and 21 by sixty, a
fireball at 130 px/s from 56 px above the player and a shot refused with the floor
in the way; `test_mp_layer` at 206, `test_mp_start` at 552, and **128 of 128
levels starting with 127 playing** on both. Chapter 2's play floor is raised from
31 to 32.

What is left of the whole remaster: **one boss**, level31c's dark dragon.

## Step 35 - chapter 4: the dark dragon, and the last level of the game (built)

Chapter 4 goes from 31 of 32 playing to **32**, and the game from 127 of 128 to
**128**. **All 128 levels start and all 128 play.** There is no unbuilt mechanic
left in the remaster.

**The whole level is one mechanism**, and decoding level31c meant decoding all of
it at once:

  - level31c is `darkest` and holds an unlit torch. A portal shot passing within
    24 px lights it, which takes the **light wall** away (Torch) and, in the same
    act, **arms this boss**.
  - `ETHCallback_dark_dragon_spawn` (bytes 388631..389418) waits 3000 ms, sees the
    `light_from_projectile.ent` the torch added, and marks itself destroyed -
    which deletes nothing: `destroy` is a custom uint (347560..347658).
  - 4000 ms later it adds `dark_dragon.ent` at its own place, calls
    `breakDarkDragonWall`, and deletes itself.
  - The dragon flies in over 7400 ms through `shout` to `fighting` and shoots
    fireballs at the player on the same sight gate as level31a's dragon.
  - **It is hurt only by fire**, as the ghost is. level31c places no fire agent and
    no fire diamond. Its only fire is a **barrel bomb**, which is `teleportable`,
    and the only thing that can light that bomb is the dragon's **own fireball** -
    the port's fireball path already asks an overlapping bomb to go off.
  - Every wound: hp -1, the player's **portals are taken away**, the fire interval
    shortens by 600 ms, it **shoots out the torch**, and it restocks the bomb.
  - At hp 0 it falls over 9000 ms and drops a **yellow key**, and level31c places
    the only yellow keyhole and locked door and no key of its own.

**The boss supplies its own ammunition and its own detonator**, and that is not a
reading. `addEntityIfItCantBeFound` restocks `barrel_bomb` after every shot it
takes, so a fight that would otherwise end at the first wasted bomb instead hands
the weapon back - and the boss's own shot is what arms it. The same shape as the
ghost restocking the fire diamond that kills it, one chapter later and tighter.

**`darkest` becomes a mechanic here, for the first and only time in 128 levels.**
Twelve levels carry the flag and eleven use it for atmosphere; this one makes light
a resource, gives you one torch to make it with, and has the boss shoot that torch
out every time you hurt it. `art.json` has carried `darkest` as a decoded but
unapplied note since chapter 4 began - this is what it was for.

**Hurt by a blast, and tested by hand for the ghost's reason.** `Fire::Blast`
reaches what it grabs by BODY, and `dark_dragon.ent` is a sensor (`sensor 1`,
`density 0`, `gravityScale 0`, Collision 220 x 220) that no level places - so
`Fire::Find` can never hold it, exactly as it can never hold the ghost.
`Fire::State` now reports **where** its bombs went off, and `Game` tests those
against the boss, which is the same shape as the fireball-versus-ghost test
already beside it. The reach is the boss's own radius and the blast's together:
the original grabs with a sphere against that real 220 x 220 collider, and the
port takes the box as its half extent, 110 px. `burnable` in the entity's own
custom data is what puts it in the blast's filter at all.

**Killing portals is not a refund.** `PortalManager::killAll` becomes
`placed.clear()` and `flight.reset()`; `budget` and `portalsUsed` are deliberately
left alone, so a wound costs the portals you have and not the ones you spent.

**One gap, stated rather than papered over.** The original's death also adds a
`single_block_plat_no_emissive.ent` at `platform_pos`. This port cannot:
`LevelBuilder::BuildEntity` builds a NODE the scene holds, and no node stands at
that marker - the original makes the body from the `.ent` itself. There is
deliberately **no flag for it** in `DarkDragon::Turn`, because a flag `Game` could
not act on is a hole shaped like a check. The risk: the key drops at (78, 208) and
the keyhole is at (592, 200), so if that platform is the only way back across, the
level may not be finishable without it. Nothing in the data settles that - it needs
the level played. Building it needs a way to place a body from an `.ent` with no
node behind it, which is the same shape as `LevelBuilder::BuildRigidCircle`.

**One divergence, also stated.** The original's marker watches for the
`light_from_projectile.ent` a lit torch adds; the port reads `Torch::State::lit`,
which counts the same event and is monotonic. A player who lit the torch and shot
the flame back out inside the three-second window would, in the original, leave no
light to be found and disarm the summon; here the arming stands. Recorded rather
than built, because modelling it means modelling that entity's own lifetime, which
nothing else in the game reads.

**A test premise expired, and it was rewritten rather than relaxed.** The first cut
of `test_mp_darkdragon` ticked six seconds and asserted a shot. It failed, and the
gate was right: the boss fights at (16, 56) and the player spawns at (710, 48),
694 px away through the pillars level31c stands between them. What the suite pins
now is **both** sides - no shot across its own arena, and a shot the moment the
player comes within 40 px.

GCC 13.3 and MSVC 14.50 agree: 0 failures in every Magic Portals suite and no
warnings on either, `test_mp_darkdragon` at 92 checks reporting a summon 3000 +
4000 ms after the torch and an arrival over 7400 ms, a blast beside it taking hp
from 3 to 2 and two portals away, a shot refused across 694 px of its own arena
and taken from 40 px, and a fall of 9.00 s that dropped its key; `test_mp_dragon`
at 151, `test_mp_layer` at 206, `test_mp_start` at 552, and **128 of 128 levels
starting and 128 playing** on both. Chapter 4's play floor is raised from 31 to 32.

**The remaster is complete.** Every mechanic the original has is built, and what is
not built is named in the data file that carries each decode. What is left is
playing it: level31c's missing platform above, and level28b's fire diamond with no
gutter mouth, are the two places where only the game itself can say.

## Step 36 - the pit that did not kill, and the screen that was not there (built)

Two reports from the owner's second play, and both were the same absence seen from
different ends: **the level had no edge**.

**A PIT WAS NOT A DEATH, IT WAS AN INFINITE FALL.** `Hazards::State::Tick` tested
`Trigger::Overlaps` against placed hazard boxes and nothing else, and `Hazards::Rules`
held only `notHazardNames` - there was no concept of the level's extent anywhere in
the port. A player who walked off a ledge fell for ever, which is what the owner
found by jumping into one.

`GameStateController::checkGameLost` (bytes 123759..125310) holds **two** deaths.
Instructions 0..116 are the hp death; instructions 117..184 are the bounds test, and
it reads:

```
charPos.x > maxPos.x || charPos.y > maxPos.y || charPos.x < minPos.x || charPos.y < minPos.y
```

with, from the constructor (bytes 121296..122160, instructions 60..114):

```
size   = g_scale.scale(vector2(32, 48))
maxPos = SeekEntity('max').GetPositionXY() + size * 2
minPos = size * -2
```

**The margin is `size * 2` at BOTH ends, and reading it as a bare `size` on the max
end was a misread of the disassembly worth recording.** Instructions 77..80 compute
`size * 2` into a variable and instruction 81 is `VAR v8`, which pushes that
variable's INDEX; instruction 96 is `GETREF`, whose operand is a STACK OFFSET rather
than a variable index - so the listing's `GETREF v2[size]` annotation names the wrong
variable. `size * 2` has no other consumer in the function, and a compiler does not
emit a multiply it never reads. The result is symmetric with the `minPos` line, which
is what a reader would expect anyway. In the port's unscaled pixels that is
**(64, 96)**, and it lives in `hazards.json` with the whole derivation beside it.

The marker is the one the port already had: `max`, which `Camera` reads for its clamp
and `boss.json` for the beholder's spike cull. **Every one of the 128 levels places
one.** The test is guarded on actually having it, because `loadLevel` refuses a level
with no `level_bounds` but `Game::Start` does not - and every sim suite goes through
`Game::Start`. Left unguarded, an absent marker read as (0, 0) would have killed the
player on the first tick of all 128 levels.

**AND DYING IS TWO MOMENTS, exactly as finishing turned out to be in step 35.** Death
was an instant retry here: the tick it happened, the level was back as it loaded, with
no screen at all. `checkGameEnd` counts `gameEndElapsedTime` against `gameLostDelay`
before it raises `levelLostLayer`, and `gameLostDelay` is the register `gameWonDelay`
is copied FROM - so it is the same **1400 ms** the finish already waits, and
`kDeathDelayMs` is written as `kFinishDelayMs` rather than as a second number.

**The sounds were wrong in a way only the decode shows.** `checkGameLost` plays
exactly one cue and only for a fall: `playDieByFallSound`, at the moment it happens.
The hp death plays **nothing**. `playDeathSound` belongs to `checkGameEnd`, 1400 ms
later, where the lost screen goes up - and this port was playing it at the instant of
death. `player_fell` is now its own event and `player_died` is the screen's.
`playFallSound` is a third cue again and is **not** a death: sfx 18, gated on a 200 ms
`charHitSoundTimer` with a volume argument, which is a landing thump.

**THE LOST SCREEN'S VEIL IS NOT THE MEDAL SCREEN'S**, and that was the trap worth
catching. `LevelLostLayer` (bytes 276429..277344) `addSprite`s `fade_edge.png` at
`V2_ZERO` sized `(screenSize.x * 0.9, screenSize.y)` at **ARGB(180,...)**, where the
medal screen pulls the same file one and a half screens wide at **200**. A top-left
origin 0.9 of a screen wide IS a centre of (0.45, 0.5) - the same origin-to-centre
conversion the finish veil's note already records. The `stretched` maker had 200
hardcoded in it, so alpha is the caller's now.

Its two buttons are `button_restart.png` and `list_button.png` at (0.4, 0.6) and
(0.6, 0.6): **a row**, where the medal screen's three are a column. Same
last-pushed-is-the-first-argument rule, opposite answer - the constant that varies
here is the x - which is why it is written down twice. They are the medal screen's own
`Retry` and `List` kinds, so `PressMenu` needed nothing new, and `openDead` follows
`openFinished` rather than `openMenu` **because `openMenu` unloads the level and sets
`m_current` to -1**, after which the restart button's own `if (m_current < 0) return
false` would have made it silently do nothing.

**THE WALK ARROWS HAD MARGINS THE ORIGINAL DOES NOT HAVE.** They were placed just
inside the corners on the reasoning that a mouse should be able to reach all of a
button; the owner's answer was that the original crops them. Its capture shows each
disc running off the screen at the side AND off the bottom, with only the glyph and a
collar in view. They are at (0.04, 0.94) and (0.96, 0.94) at 0.30 of the view's height
now, and the top-right pair keeps the small inset the capture shows. Those fractions
are **measured from the capture, not decoded**, and say so - the original builds these
against `GetScreenSize` at `g_scale` and no fraction of the screen is written down
anywhere in the binary.

**TWO TEST PREMISES EXPIRED, AND BOTH WERE REWRITTEN RATHER THAN RELAXED.**

`test_mp_layer`'s `DeathIsAnInstantRetry` asserted the behaviour this step removes. It
is two cases now: death is a beat and then a screen, and a fall out of level1 is its
own death with its own cue.

`test_mp_minions`'s killer-floor case failed on both toolchains, and the failure was
right. level0b's `max` is at (1052, 256), so with the (64, 96) margin the level ends
at (1116, 352) - and the case parks the player on a killer floor centred at
(1152, 448), which spans x 768..1536 and y 384..512. **It was quietly standing the
player outside the level on both axes**, and nothing noticed because nothing tested
the edge. What it always MEANT - that `enemy_killer` takes minions and never the
player - is now asked as "whoever killed it, it was not the killer floor", with the
fall named as what did.

**What this makes visible in level31c.** The same bounds test turns step 35's recorded
platform gap from a silent infinite fall into a death. The y=240 row there is
`light_wall` 126x30 at (64, 240) → x 1..127, `single_block` 64x32 at (224, 240) →
x 192..256, then `platform_no_emissive` 256x32 twice → x 256..768. The light wall
carries `metadata/breakable` and lighting the torch takes it away - and the torch must
be lit to summon the boss at all - so once the fight starts there is **no floor from
x 0 to 192**, the key drops at (78, 208) inside that hole, and `platform_pos`
(160, 240) with that same 64x32 box spans **x 128..192**: exactly the bridge from
wall-end to single_block-start. That is the next step, and it is smaller than
`darkdragon.json` feared - `DarkDragon` already parses `platform_pos` and
`platform_entity`; only the `Turn` flag and a builder for a body with no node behind
it are missing.

GCC 13.3 and MSVC 14.50 agree: **0 failures in every Magic Portals suite** and no
warnings on either. `test_mp_layer` 205 → **217**, reporting "level5: died, and the
lost screen came up 1400 ms later with 2 button(s)" and "level1: fell past the level's
edge and died"; `test_mp_hazards` 19 → **43**, pinning the extent, the (64, 96) margin
and all six bounds cases; `test_mp_sounds` 99 → **103**; `test_mp_minions` 77 → **78**;
and **128 of 128 levels still start and play** on both. MSVC returned 4551 on
`test_mp_turrets` once - Smart App Control blocking a freshly linked unsigned exe, not
a failure - and passed it on the relink the runner does for exactly that.

## Step 37 - the platform the last level needs, and a body with no node (built)

Step 36's level edge turned step 35's recorded gap from a silent fault into a visible
one, so this closes it. **level31c's missing death platform is built.**

**WHY IT IS NOT A FLOURISH.** The floor at y=240 there is `light_wall` 126x30 at
(64, 240) → x 1..127, `single_block_plat_no_emissive` 64x32 at (224, 240) → x 192..256,
then `platform_no_emissive` 256x32 twice → x 256..512 and x 512..768. The light wall
carries `metadata/breakable`, and lighting the torch takes it away - **and the torch
must be lit to summon the boss at all**. So from the moment the fight starts there is
no floor whatever from x 0 to 192. The key drops at (78, 208), inside that hole, and
`platform_pos` is (160, 240) with that same 64x32 box: **x 128..192**, exactly the
bridge from where the wall stood to where the standing floor begins. Without it the
only key in the level sits across a pit - and since step 36 gave the port an edge, a
player who goes for it now falls out of the world and dies instead of falling for ever.
That is the difference between a level that cannot be finished and one that says so.

**THE MISSING ROAD WAS A BODY WITH NO NODE.** `LevelBuilder::BuildEntity` builds a NODE
the scene holds; the original's `AddScaledEntity` makes a body from the `.ent` itself,
and no node stands at that marker. `LevelBuilder::BuildStaticBox` is that road, beside
`BuildRigidCircle` which is the same shape for what a launcher throws. It emplaces a
transform, a tag and a `BoxColliderComponent` at `kStaticDepthMetres` and **no**
`RigidBodyComponent` - which is exactly what `BuildNode` does for a `StaticBody2D`, and
what makes this a floor rather than something that falls the instant it is made.

`DarkDragon::Turn` carries `droppedPlatform` now. It deliberately carried nothing while
`Game` had no way to act on it, on the grounds that a flag nobody can act on is a hole
shaped like a check; the road exists, so the flag does.

**NEITHER ITS SIZE NOR ITS PICTURE IS WRITTEN DOWN ANYWHERE, and that is the point.**
level31c places **three** `single_block_plat_no_emissive` nodes of its own, so
`DarkDragon::Find` keeps the first as a template. `Game` sizes the body with
`LevelBuilder::ShapeBoundsPx` on that node - its own `CollisionPolygon2D`, running
x -32..32 and y -16..16 - and the layer borrows its `Sprite2D` texture. So the 64x32 in
the suite's output is **derived from the level**, not from a constant this step typed:
if the template's polygon were ever different, the test would say so rather than agree.

**The picture had to be borrowed, not loaded.** `entities/` holds
`single_block_plat_no_emissive.ENT` and **no .png at all** for that entity - the only
platform PNG in the whole set is `spinning_platform.png`. The `m_thrown` path draws a
runtime body from `entities/<sprite>`, which would have found nothing here, so a
straight copy of that path would have produced an invisible floor. `buildSprites` reads
the SCENE and so cannot have made an entry either. `syncSprites` therefore synthesises
one `DrawnSprite` from the template sibling's when the body first appears: a
`DrawnSprite` with a null `body` and no crystal, static-portal or zone index is drawn
statically at `sprite.atPx`, which is precisely what a platform that never moves wants.
It is added AFTER that function's loop and by index, because pushing into `m_sprites`
while ranging over it would invalidate the range.

`unloadLevel` destroys it explicitly, as it already does for `Boss::Rock` bodies:
it belongs to no `Built`, so the loop over `built.entities` does not reach it, and a
retry would otherwise leave a platform standing in the next run of the level.

**One asymmetry, stated rather than left to be found.** The dropped platform gets a
sprite but no entry in `m_bodies`, so the `B` box view does not draw a box for it. It
is a debug toggle and never what a player sees, but every other body in the game has
one, and this does not.

GCC 13.3 and MSVC 14.50 agree: **0 failures in every Magic Portals suite** and no
warnings on either. `test_mp_darkdragon` 92 → **97**, reporting "it fell over 9.00 s and
dropped its key" and then "and a 64 x 32 platform at (160, 240), from the sibling the
level places". Every other suite is unchanged, and **128 of 128 levels still start and
play** on both.

## Step 38 - the HUD the original draws, and the three seconds every level opens with (built)

The port's in-level HUD was placed by eye from one of the owner's screenshots, and a
level simply appeared. Two measured specs now exist for both - the remake's
`out/parity/specs/ui1/static_hud.md` (57 settled levels of captures of the original at
1280x720) and `level_start_timeline.md` (every frame of a 14 s recording of 1-1) - and
this step builds to them. **Every number is data** (`games/magicportals/data/ui.json`,
each block citing its sources) and **all the arithmetic is `sim/Hud`**, pure, which the
layer only places quads by.

**THE DECODE CAME FIRST, AND IT SETTLED FOUR OF THE SPECS' OPEN QUESTIONS.** The
full-module reading of `android_game.bin` that step 11a describes has `ScreenPad`,
`FadeInController`, `UISprite`, `Game::addCurrentMedalSprite` and
`dismissCurrentMedalSprite`, `Game::showGameTitle`, `GameLayer` and
`PortalManager::update` in it. Where a number is decoded, `ui.json` takes the decode and
holds the captures against it. They agree nearly everywhere; where they do not, the
reason is below.

**Where the controls are.** Restart and pause are hd sprites drawn 32 x 32 units, flush
in the top-right corner and touching (123 of 123 frames at px (1100, 0) and (1190, 0)).
The walk pads are 64 x 64 units **wholly on screen, flush in the bottom corners** (39 of
41 levels exactly and the other two within a pixel, by a fit that lets a sprite hang off
the edge). **Both overturn step 36**, which ran the pads' discs off the screen at 0.30 of
the view and kept an inset on the top-right pair, each read by eye off one capture. Every
element is anchored to the corner it was measured from, because the view is 455.11 units
wide at 16:9 and follows the window's shape.

**How opaque.** `0x78FFFFFF` - alpha 120 of 255 - is ONE constant in four places: the
game layer's `menuButtonsCustomColor` (restart and pause), `ScreenPad.color` (the pads),
and the clear-portals button. The captures measured 0.48 +-0.02 (RGB565) on all four;
120/255 is 0.4706.

**THE PULSE IS EVERY LEVEL'S, WHERE THE SPEC SAID ONLY 1-1's.**
`ScreenPad::computeButtonColor` pulses the pads as a triangle of two 300 ms strides, from
120 up to 120 + variation: 14 strides (4.2 s) with a variation of 40, or in the tutorial
48 strides (14.4 s) with 90. The tutorial is **level0 by name**:
`MainCharacter::MainCharacter` passes `GetSceneFileName() == "scenes/level0.esc"`. The
spec measured 1-1 at 0.475..0.496 <-> 0.805..0.824, triangle over sine, period 0.609 s,
which is 120 <-> 210 at 600 ms. But it concluded that no other level pulses by comparing
t4.5 with t8.0, **both past the 4.2 s gate**. Its own opacity outputs put every other
level's t2.0 pads above their t4.5 and t8.0 ones (median 0.487/0.489 against 0.475/0.477,
max 0.515), and its W-K proxy agrees independently (t2.0 - t8.0 median +0.007/+0.005 over
49 levels; t4.5 - t8.0 exactly 0). The decode is taken. At t2.0 the measured amplitude is
smaller than 40 at a random phase would give; the library's t2.0 frames all sample one
narrow arc of the cycle, and nothing is tuned to it.

**AND THE "GHOST" IS A RING.** The spec fitted what grows out of 1-1's pads as a scaled
copy of the pad sprite, drawn behind it. `drawIndicatingRing` draws
`sprites/ring_sprite.png` - clear in the middle, bright only at its rim - centred on the
pad's corner, `200 units * max(bias, 0.01)` wide over 1200 ms at alpha `1 - bias`, from
the pads' UPDATE and so under them. The spec's timing survives (every second trough,
behind, corner-anchored). Its opacity numbers do not, and its right-pad growth of
155..181 units/s brackets the decoded 167 where its left-pad 116.5 does not.

**AND THE PADS SLIDE IN.** `ScreenPad`'s interpolators bring each pad in from 128 units
outside its corner over 700 ms, eased by `smoothEnd` (sin(v * pi / 2), every
Interpolator's filter). No still shows it: on the emulator, the load stall's frame delta
had finished the slide before the first lit frame.

**THE BLACK IS TWO BLACKS, WHICH IS WHY IT MEASURED 0.54 s WHERE THE CODE SAYS 700 ms.**
`FadeInController` draws a full-screen black at `1 - elapsed / 700`, and a level has two:
`BaseState::preLoop` adds one and `Game::preLoop` another. Stacked, `(t / 0.7)^2` of the
picture shows through. The timeline fitted a linear 0.54 s fade; the same frames fit the
square with a start steady at 0.19 +-0.03 s before its black frame, where the linear
fit's implied start drifts by 0.13 s across the ramp. The controllers draw in the order
they were added, so **the pads are drawn BETWEEN the two blacks**: under one, where the
world, restart, pause and the plaque are under both. That is exactly the timeline's
unexplained "pads brighter than under the overlay and darker than above it", and the
port draws its black as two quads, one each side of the pads.

**"Part N".** `Game::showGameTitle` draws `"Part " + (levelIndex + 1)` in
`Matura84_shadow.fnt` at `screenSize * (0.5, 0.8)`, centred on its box of summed advances
by line height, fading linearly over 3000 ms, at half a unit per font pixel. The spec's
0.4964 was fitted with a kerning pair (P-a +1) that **gs2d's `DrawBitmapText` never
applies** (BitmapFont.cpp:412; no kerning anywhere in it). Without it, the measured 248.49
px advance of "Part 1" is 177 font px at 1.404, against the decode's 1.40625. So the
engine's `BitmapFont`, which applies no kerning either, was already right and needed
nothing. The caption is **a quad per letter**, each with a texture transform that picks
its glyph out of the page, rather than `BuildText` behind a `MeshRegistry` key. Matura84
keeps "Part 1"'s P and r on page 0 and its a, t and 1 on page 2, so a quad per letter can
take each page as its own texture. And the whole layout stays a pure function that a
suite can pin with no registry at all.

**The plaque.** `addCurrentMedalSprite` runs only where `getScore` is not 0. It puts the
medal at `scale(40, 40)` and the plaque at that plus `(0, 28)`, both centred; the
captures put their top-left corners at (8.02, 8.02) and (8.02, 4.02), which is exactly
those centres. Both are `UISprite`s: they come in over 1000 ms by `smoothEnd`, are
dismissed once `getUiTime() > 2000`, and go out over 1000 ms as `1 - smoothEnd`. The
captures start that fade 2.095 s after the tap in the recording and about 2.03 s in the
library. The decoded 2000 ms is taken; it runs about 0.04 alpha under six of the seven
library t2.0 plaques the timeline dated by their captions. A fresh save shows no plaque, as the original's does.

**ONE CLOCK.** The timeline measured two, 0.70 s apart: the caption, plaque and pulse run
from the tap, the black from its first frame. That 0.70 s is the emulator's load stall,
since `Game::preLoop` starts every one of them. So the port runs them all from the tick a
level is loaded (`LevelAgeMs`), a retry included: the original's restart builds a fresh
Game state (`GameLayer::update`). *Superseded by step 40*: the black is on a second clock
after all, and the port now starts it 465 ms into the level's age.

**The clear-portals button** follows `PortalManager::update`:
- none where maxPortals is 0;
- removed while `portals` is empty, and shown otherwise - so exactly while a placed portal
  is alive;
- pressing it calls `killAll(true)`, which plays `playPortalKilledSound` and gives back a
  count of the golden score for every portal whose `hasTeleportedSomething` is still 0.
  `teleportToOther` sets that flag on both ends of a pair it carries something through.

`Portals::State::KillAll` and `Placed::teleported` are exactly that, and the button sits
flush top-left at 32 units.

**No text over a level.** The status, result and help lines the port laid over a level
being played are gone. Across 41 levels' settled frames, the only screen-fixed regions are
the two button corners and the two pads (static_hud.md section 6). A refused level, a
chapter's end and the menu screens still say what they say.

**Four things wrong in the port, fixed on the way.**
- **The HUD trailed the camera by a tick.** The controls were placed in `readInput`,
  before the camera moved that tick. They were then placed after `placeCamera` and
  interpolated as the camera is. *Superseded by step 39*, which draws the HUD on the
  screen and so needs no camera at all; `test_mp_layer` still walks level30 until the
  camera pans and the pause button never moves on the view.
- **A tap on a control asked the camera.** It is now tested in VIEW space, straight from
  the pointer's place in the viewport. A pad's hit area is `isPointInSphere` with
  `buttonRadius = scale(64)`: a quarter circle of 64 units about the corner, already
  larger than the drawn disc. A button's is `Button::isPointInButton`, its rectangle.
- **The pause button stepped a level it had just unloaded.** `openMenu`, called from
  `readInput`, left the tick to run `BeforeStep` and read the player's transform on an
  empty level. The tick now returns when a screen has gone up.
- **A weightless level drew walk pads.** `MainCharacter` neither updates nor draws them
  when `noGravity` is set, so the port hides them there. Decoded only: no capture of a
  weightless level has been measured.

**THE ENGINE BLENDS IN LINEAR LIGHT, AND THE ORIGINAL DID NOT** - and this step got the
consequence wrong. It drew the HUD as quads in the level and converted only the opening
black (`Hud::BlackInLinearLight`), on the argument that nothing else could be converted
and that translucent elements would merely read "somewhat brighter". Review measured the
opposite for opaque white (186 of 255 on a black screen) and a control contrast that
moved with the background. **Step 39 replaces all of it**: the HUD is drawn after the
tone map, in display values, and the conversion is gone.

**Captured** with a dev-only `--saves <dir>` in main.cpp. It points the medals directory
somewhere other than the user's own, so a capture can open a level with a medal recorded
without touching a player's save.
- **Which frame.** For each library frame, the one whose level age matches: the frame's
  label plus the offset its own caption alpha dates it by (2.385..2.445 s for t2.0).
- **Which levels.** 1-1, 1-5, 1-17 and 3-5, at t2.0, t4.5 and t8.0.
- **Alignment.** An edge alignment of each HUD region against the original puts restart,
  pause and both pads at **(0, 0) px in all twelve**. On the four t2.0 frames, where the
  caption and plaque are still up, the caption is within 1 px and the plaque at (0, 0).
- **The ring, seen.** Every library frame of 1-1 lands 45 ms into a ring stride, so the ring
  is a dot hidden under its pad in all three (6000 ms is five strides). The recording's
  frame at 3.094 s (stride phase 0.81) was compared with the port at age 2200 ms (0.83).
  Both show a faint rim about 225 px out from each bottom corner, well clear of the pads,
  where the decode says 227.
- **Two clocks, on purpose.** The library frames are dated by their caption (the tap
  clock), and the ring, pulse and plaque run on that clock. The fade check dates the
  recording by its black frame instead, 0.19 s after the fade's start.
- **What still differs** is the world behind them: its lighting is not ported, and its
  colour goes through the pipeline above. 1-1's pad pulse at t8.0 is also phase-sensitive,
  to the tens of milliseconds by which the two clocks' origins differ.

**Not built, and said so.**
- **The pause screen.**
- **The tutorial popups of 1-2 and 1-3.** In the original they hide the HUD; the port has
  no popups.
- **The pads' fade on game over.** `ScreenPad::draw` multiplies their alpha by 0.98 a
  frame; the port hides the controls through the finish and death beats instead.
- **The no-portal sign of 1-15, 2-07 and 2-28.** It is screen-fixed in the original but
  drawn by a level entity, which `followUp`s the camera's corner. The port still draws it
  where the level file puts it, off the level. *Built in step 39.*

MSVC 14.50 (Release, Ninja) only, this step: **35 of 35 Magic Portals suites pass**, and
the full build prints no warning.
- `test_mp_hud` is new, at **184** checks, and needs nothing from outside this repository.
- `test_mp_layer` goes from 217 to **277**. The new checks cover:
  - the HUD's rectangles and alphas, held against its quads;
  - a portal cleared by a tap on its button, with its count given back;
  - a medal earned, and the plaque it opens the retry with;
  - the tutorial's rings;
  - no pads in level1c.
- **Smart App Control.** It refused five freshly linked suites on the first run (ctest's
  `BAD_COMMAND`, "Permission denied"). Each passed once relinked, as step 36 recorded for
  `test_mp_turrets`.
- **Not run on GCC.**

## Step 39 - the HUD drawn after the tone map, and the sign pinned to the corner (built)

Two independent reviews of step 38 compared its captures with the original's and found
seven discrepancies. Each was re-measured before anything changed. Four were one engine
gap, one was a missing element, and two are declined below with the reason.

**THE ENGINE GAP: THE HUD WENT THROUGH THE BLOOM AND THE TONE MAP.** Every quad the
layer drew was scene geometry. The scene target is linear light, and
`bloom_composite.frag` adds the bloom, applies Reinhard and encodes once. For a HUD
that is wrong in three ways:
- **White is capped.** Linear 1.0 leaves Reinhard as 0.5 and encodes to **186**, and no
  finite radiance reaches 255. On 1-1's black opening frame the port's "Part 1" read 184.6
  where the original reads 253.
- **Glyphs bloom.** The bright pass's soft knee starts at half its threshold, so every
  letter carried a halo: 86 / 34 / 21 / 12 / 5 grey at 0.5-2 / 2-4 / 4-6 / 6-8 / 8-12 px.
- **The blend is in linear light.** A translucent control's contrast then depends on
  what is behind it.

The third is the one no choice of alpha can fix. For white and black sprite pixels at
alpha 120/255 over a background of display value b:

| background b | display-space contrast (the original) | through Reinhard (step 38) | linear, tone map off |
|---|---|---|---|
| 0.05 | **0.471** | 0.559 | 0.673 |
| 0.20 | **0.471** | 0.451 | 0.571 |
| 0.40 | **0.471** | 0.317 | 0.457 |

Only display-space blending is invariant to the background, and that invariance is
what the captures of the original show. Its measured gain barely moves across levels
with very different backgrounds. So turning the tone map off would not have helped:
it is worse than Reinhard over the dark stone most levels have. Pre-compensating the
colour cannot reach white at all, since it would need infinite radiance.

**Fixed in the engine, minimally, with a suite.**
- **`core/ScreenOverlay`.** An immediate-mode list of quads. Each quad has a rectangle
  in fractions of the image, a uv rectangle, a display-referred colour and a texture
  path. It is published in the registry context the way `WorldShapes` is.
- **`BloomPass::RecordOverlay`.** Opens a second render pass over the composited
  `R8G8B8A8Unorm` image. The pass loads the image instead of clearing it, starting
  from the layout the composite left.
- **`VulkanRenderer::DrawFrame`.** Resolves every quad's texture and material set,
  then draws the list with `screen_overlay.vert/.frag` and clears it.
  - No vertex buffer: six vertices from `gl_VertexIndex`.
  - Blending on, no depth.
  - Each texture is acquired UNORM (`srgb` false), so a texel is the byte in the file
    and what is written is a display value.
- **Order and capture.** Order of `Add` is order of drawing. `--screenshot` reads this
  same image, so captures show the overlay.
- **Why not `UIImageComponent`.** It is drawn in the swapchain's ImGui pass, which
  `SupersonicApp.cpp` does not read for a screenshot (it reads
  `offscreen.GetPresentedImage()`). A HUD there would have been invisible to every
  capture.
- **Shaders.** The two new `.spv` are compiled by the build and left in the tree beside
  their sources (`SHADER_JOBS`).
- **Suite.** `test_screenoverlay` is new, **28 checks**. It pins:
  - clip-space and texture-space corners;
  - 1280x720 pixel placement of the pause button;
  - two non-degenerate triangles;
  - order;
  - the ceiling;
  - the blend factors;
  - that the shader's own vertex table is the one `CornerOf` states.

  ARCHITECTURE.md section 5 and the README's suite table say the same.

**THE PORT'S HUD GOES THROUGH IT.** `MagicPortalsLayer::EmitHud` replaces every HUD
entity. Emission order is the original's order of drawing, as step 38 decoded it:
1. the no-portal sign (a level entity);
2. the tutorial's rings;
3. restart and pause;
4. the plaque, then its medal;
5. clear-portals;
6. the first black;
7. the pads;
8. the second black;
9. "Part N".

It runs once a FRAME from `OnUpdate`. The overlay is cleared every frame, so a HUD
emitted on the tick would vanish from frames with no tick. It reads the last tick's
`LevelAgeMs` and changes nothing.

With the HUD on the screen, four of step 38's parts are dead weight, and all four are
gone:
- the eight z constants;
- the black's overscan;
- the interpolated transforms;
- **`Hud::BlackInLinearLight`**.

In display space two stacked blacks multiply exactly as the original's did. Its old
validation had been taken under Reinhard. Re-measured now, on the world band of 1-1:

| frame | port brightness k | decode (t / 0.7)^2 per byte | original (rec11) |
|---|---|---|---|
| 350 ms into the black | **0.253** | 0.252 | 0.262 (n29) |
| 600 ms into the black | **0.741** | 0.738 | 0.753 (n36) |

**Evidence after, on the same measures the reviews used.** (`out/parity/ui1/r2/`, gitignored.)
- **Caption on the black frame.** Ink median 253 and p99 253, against the original's 253
  and 255. The ring profile outside the ink:

  | px from ink | 0.5-2 | 2-4 | 4-6 | 6-8 | 8-12 |
  |---|---|---|---|---|---|
  | port | 83.5 | 1.6 | 0.0 | 0.0 | 0.0 |
  | original | 83.9 | 1.7 | 0.1 | 0.1 | 0.1 |

  The halo is gone; what is left is the glyphs' own antialiasing.
- **Control contrast.** High-pass gain, static_hud's section 4 method, on t8.0 frames.
  Columns are restart / pause / left pad / right pad:

  | level | original | port now | step 38 |
  |---|---|---|---|
  | 1-05 | 0.476 0.476 0.477 0.476 | 0.474 0.473 0.467 0.469 | 0.471 0.465 0.327 0.406 |
  | 1-15 | 0.480 0.479 0.472 0.473 | 0.475 0.474 0.471 0.471 | - |
  | 1-17 | 0.478 0.487 0.472 0.476 | 0.475 0.474 0.471 0.472 | 0.471 0.464 0.332 0.366 |
  | 3-05 | 0.471 0.473 0.475 0.476 | 0.473 0.471 0.471 0.472 | 0.437 0.521 0.332 0.366 |

- **Plaque at alpha 1.** Pixels whose sprite value is in the 200-255, 150-200 and 0-50
  bands:
  - port frame 90: 231 / 177 / 26;
  - the sprite itself: 231 / 177 / 26;
  - the original at 2.928 s: 233 / 175 / 25;
  - step 38 (review): 183 / 135 / 42.
- **Plaque while it fades.** Apparent alpha at t2.0, regressed as the review did, for
  1-01 / 1-05 / 1-15 / 3-05:
  - original 0.404 / 0.490 / 0.443 / 0.413;
  - port 0.361 / 0.444 / 0.402 / 0.409;
  - step 38 read 0.465..0.679.

  It no longer lingers. What is left is **not explained**:
  - 1-01, 1-05 and 1-15 read 0.041..0.046 below the original, and 3-05 is within 0.004.
  - The difference is one-signed. At 2.4 s the plaque's alpha falls 1.27 per second, so
    0.04 is about 30 ms, two frames. On those three levels the original's plaque runs
    about two frames behind the clock its own caption dates the frame by.
  - A first run of this table had 3-05 at 0.473, 0.06 *above*. That was a capture error,
    not the port: its frame was dated 2.385 s, a base copied from 1-05, where 3-05's own
    caption alpha (0.189) dates it 2.434 s. Re-dated, re-captured, it reads 0.409.
  - Why 3-05 has no two-frame lag and the others do is not established. A candidate is
    the plaque's accumulated `getUiTime()` starting a frame or two after the caption's
    `GetTime()` origin, the same kind of split as the opening's (below). No capture here
    can tell that from frame-pacing noise.
- **The world is untouched.** Outside the HUD corners plus a 60 px margin, the t8.0
  captures of 1-01, 1-05, 1-17 and 3-05 are **byte-identical** before and after.

**THE NO-PORTAL SIGN, PINNED AS ITS SCRIPT PINS IT.** 1-15 and 2-07 draw
`entities/hd/no_portal_symbol_small.png` flush top-left at 64 units, at full opacity. The
decode says why a level entity does that:
- **The callback.** `ETHCallback_no_portal_sign` (MiscCallbacks.angelscript, bytes
  343129..343339) calls `followUp(thisEntity, GetCameraPos() + GetSize() * 0.5, 600, 100,
  true)` every frame.
- **The follow.** `followUp` (utilEntityEffect.angelscript, bytes 328407..329130) eases a
  `PositionInterpolator` over 600 ms by `smoothEnd`, on frame time. **It aims it again
  from where it has got to** whenever the target is not where the entity stands and
  more than 100 ms have passed. So it never runs a whole ease. It closes about 30 % of the
  distance every 117 ms, and a quarter of a percent is left at 2 s, which is where the
  library's t2.0 frames already find it.
- **Data and state.** `ui.json`'s new `hud.no_portal_sign` block carries those numbers;
  `Hud::Follow` is that state machine.
- **Drawing.** The layer takes the entity out of the level's sprites by
  `metadata/entity_name`, as `SeekEntity` finds it, and draws it first in the overlay.
  It uses the hd twin, which the captures match (0.977 against 0.818 for the 1x at
  twice its size); the converter copies only the 1x.

`locate.py` with the hd sprite, at scale 1.40625 in the top-left region:

| frame | port | original |
|---|---|---|
| 1-15 t2.0 | (0, 0) 180 px, 0.783 | 0.805 |
| 1-15 t4.5 | (0, 0) 180 px, 0.928 | 0.977 |
| 1-15 t8.0 | (0, 0) 180 px, 0.928 | 0.977 |
| 2-07 t8.0 | (0, 0) 180 px, 0.876 | 0.897 |

At t2.0 both are lower because the plaque is over the sign. The rest of the gap is the
unlit world seen through the sign's translucent parts. 2-28 (level27a) still has no
in-level capture of the original.

**DECLINED: THE HUD OVER THE TUTORIAL POPUPS OF 1-2 AND 1-3.** The difference is real: the
original hides restart, pause and both pads while its help popup is up. But the port
builds no popup. Hiding the controls there would leave two levels with no controls and
nothing to dismiss. It belongs with the popup, which is its own system and stays unbuilt.

**DECLINED, WITH THE MEASUREMENT: THE OPENING'S TWO CLOCKS.** The review saw the port's
pads slide in and its plaque fade in while the black lifts. At equal blackness the
original's caption is 0.15 dimmer. Re-measured on rec11's frames n29..n38, under the
stacked black: the caption's clock leads the black's by **0.46 s** (0.42..0.47, steady
across the ramp).

The decode explains that lead without a second clock in the design:
- `FadeInController` times itself from `GetTime()` at its construction in `preLoop`.
- `showGameTitle` starts the caption earlier in the same `preLoop`.
- The pads' slide and the plaque's `UISprite` run on accumulated frame time
  (`InterpolationTimer::update`, `GetLastFrameElapsedTime`). The first frame after a
  level load carries the whole load in its delta.

On the emulator that delta covered the 700 ms slide and most of the plaque's 1000 ms
fade-in before anything was drawn. That is why no capture can show either. The port
loads within a tick, so it plays the zero-load timeline its spec gives. The fade curve
itself matches (table above). The slide's one-tick lag the review measured was the
interpolated transform, which is gone with the overlay. *Overturned by step 40*, which
reads `GetTime()` down to the clock it reads and builds the lead.

**Captured** by `out/parity/ui1/capture_port_r2.sh`:
- **Library frames.** Each is dated by its own caption alpha: t2.0 is 2.448 s on 1-01,
  2.389 s on 1-05, 2.406 s on 1-17, 2.434 s on 3-05 and 2.417 s on 1-15. 2-07's caption
  has a fireball over it, so 2.40 s is taken. Step 38 and the first run of this step
  dated 3-05 at 2.385 s, a base copied from 1-05; it is re-captured at 2.434 s.
- **rec11 frames.** Dated by their black (n29, n36) and by their caption (n62).

All 21 were run with the validation layers active and logged no VUID.

| capture | offset px | edge IoU | HUD |
|---|---|---|---|
| 1-01 t2.0 / t4.5 / t8.0 | 0,0 / 0,0 / 0,0 | 0.43 / 0.41 / 0.41 | matches; pad pulse phase varies (tutorial pulse) |
| 1-05 t2.0 / t4.5 / t8.0 | 0,0 each | 0.43 / 0.42 / 0.41 | matches |
| 1-17 t2.0 / t4.5 / t8.0 | 0,0 each | 0.38 / 0.37 / 0.36 | matches |
| 3-05 t2.0 / t4.5 / t8.0 | 0,0 each | 0.42 / 0.37 / 0.37 | matches |
| 1-15 t2.0 / t4.5 / t8.0 | 0,0 each | 0.38 / 0.38 / 0.37 | matches, sign included |
| 2-07 t2.0 / t4.5 / t8.0 | 0,0 / 0,-15 / 0,0 | 0.24 / 0.17 / 0.18 | matches; the world does not (its darkness and lights are not ported). The t4.5 offset is the whole frame's: pause and restart alone still fit best at 0 px (mean abs 13.4 and 11.2 at 0, worse at every shift to 20 px) |

**What still differs is the world, not the HUD.** Lighting is not ported: the torch,
the lamps and the dark levels' shading are absent, and the world still goes through the
tone map. So a translucent control over the port's stone sits on a different
background, though with the original's contrast. The overlay makes the HUD
display-referred and nothing else.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning.
- **Magic Portals suites.** **35 of 35** pass, and **112 of 112** suites pass across
  the whole ctest set. The fix round's brief counts 34 `mp_` suites; the 35th is
  `test_mp_hud`, new in step 38 and not yet committed.
- **`test_screenoverlay`.** New, 28 checks.
- **`test_mp_hud`.** 184 → **179**, and that net is two moves, not a wash:
  - **20 checks went** with `BlackInLinearLight`: the test that walked the conversion
    across the black's alphas is gone because the function is gone.
  - **15 came in** for the sign's follow: the rules, the settled rectangle, the first
    ease, the re-aim past 100 ms, the chase at 2 s and 20 s, and a moved camera.
  - Nothing else in the suite changed, so the 20 is the 184 less the 164 left before the
    sign's 15.
- **`test_mp_layer`.** 277 → **294**. The HUD is asserted on the overlay list itself -
  rectangle, alpha, texture and order - and the sign is in level14 and not in level0.
- **Smart App Control.** It refused eight freshly linked suites and `MagicPortals.exe`
  (BAD_COMMAND, "blocked by your organization's Device Guard policy"). Each ran once
  relinked.

## Step 40 - the black that started after the load, and the popup that is its own system (built)

A third review compared step 39's captures with the original's and found five
discrepancies. **Four are one cause**: the port's opening black started with the level,
where rec11's starts later. **The fifth** is the HUD over the tutorial popups of 1-2 and
1-3. Each was re-measured first. The four are fixed by one number, and the popup stays
declined for a stronger reason than step 39 gave.

**WHAT THE REVIEW SAW.** Every lit frame of rec11 (n28 onwards) already has:
- the pads home in their corners;
- the plaque up;
- "Part 1" dimmer than the port's at the same blackness, by 0.15 of alpha (0.46 s of
  its fade).

The port instead slid its pads in and faded its plaque up while the picture came
through. It also kept the caption and plaque on screen about 0.5 s longer after the
black had gone. Step 39 had declined this as the emulator's load stall. That
explanation fit the captures, but nothing had checked it.

**THE DECODE, READ DOWN TO THE CLOCK EACH PART READS.**
- **The black reads wall time.** `FadeInController::FadeInController` and `::start`
  (bytes 37911..38429) take `startTime` from `GetTime()` after a `LoadSprite`, and
  `::draw` fades by `GetTime() - startTime`.
  - Ethanon's `GetTime` is the video's `GetElapsedTime`
    (ETHScriptWrapper.System.cpp:90-93).
  - On Android that is a live `clock_gettime(CLOCK_MONOTONIC)`
    (AndroidGLES2Video.cpp:66-70).
  - Of every function in the module, `GetTime()` is called only by `FadeInController`,
    portals, the earthquake, the score dashboard and a menu blink.
- **Everything else in the opening reads frame time.** Each adds the last frame's
  elapsed time:
  - the caption: Ethanon's `ETHTextDrawer::Draw`, ETHDrawable.cpp:64-66;
  - the plaque: `UISprite` through `InterpolationTimer::update`, and
    `GameStateController::m_uiTime`, which gates its dismissal;
  - the pads' slide and pulse: `ScreenPad::update`, instruction 170;
  - the sign: `followUp`'s interpolator.
- **A level's load is one frame.**
  - Ethanon loads the scene and then runs its `preLoop` inside one frame's update
    (ETHEngine.cpp:176-179 → ETHScriptWrapper.Scene.cpp:570-612, then
    `LoadSceneScripts` :660-668).
  - `Game::preLoop` builds its `FadeInController` at instruction 316, and
    `BaseState::preLoop` builds the other one, both after the scene is in.
  - gs2d charges the next frame the whole wall time since the last one, capped at
    1000 ms (`ComputeElapsedTimeF`, Application.cpp:36-50; android/main.cpp:180-181).

So the frame after a load hands **all** of it to the caption, the plaque, the pads and
the sign, but only the part after instruction 316 to the black. The black runs behind
the rest by however long the load took before the blacks were built. That is the
two-clock split the timeline measured, now with its mechanism named.

**MEASURED, THREE WAYS.** Each lit frame gives the level's age from its caption,
t = 3000 (1 - alpha), and the black's own age from the world's brightness,
700 sqrt(k). The difference is the lead:

| source | frames | median lead | range |
|---|---|---|---|
| level_start_timeline.md sections 1-2 | n28..n37 | 465 ms | 423..490 |
| review 2's table of the same frames | n28..n37 | 464 ms | 422..490 |
| step 40's own re-measure from the raw frames, with its own settled reference | n28..n37 | 473 ms | 435..506 |

The lead is steady across the ramp. n37, whose caption stalls for 84 ms, is the low
end. The black frame n27 is 24 ms into the level by its caption, and wholly black.

**WHAT THE PORT DOES.**
- **`ui.json`**: new `level_start.overlay.start_after_ms` = **465**. Its note cites the
  decode above and the three measurements. It also says what the number is: **that
  emulator's load, not a designed wait**. A device that loaded in no time would show 0,
  and setting it to 0 restores step 38's zero-load opening. It is kept because it is
  what the original shows.
- **`Hud::OverlayLayersAlpha`** times both blacks by the level's age less that number.
  They are wholly black before it, and the pads still sit between them.
  - **The hold's look is inferred, not decoded.** In the original neither black exists
    yet then, and no frame is drawn.
  - The port draws its held frames as rec11's black frame n27 looks, the one frame of
    the load that was drawn.
- **Nothing else moves.** The caption, the plaque, the pads' slide, pulse and rings, and
  the dismissal all stay on the level's age.
- **The sign is handed the hold in one piece** (`Hud::HandOver`). The sign's chase is
  the one clock in the opening that is not a pure function of age: it is re-aimed every
  100 ms. Fed sixty short frames it is still 13 units out at 700 ms. Fed the original's
  one long frame it is 3 units out.
  - So its frame time is held while the blacks are whole and given over on the first
    tick after.
  - **This is inferred.** There is no footage of 1-15's opening, and by 2 s the two
    feeds are within a pixel of each other: the library's t2.0 capture of 1-15 is
    byte-identical either way.
- **A retry opens the same way**, as its restart builds a fresh Game state.

**EVIDENCE AFTER.** The port was captured at the rec11 frames, each dated by its own
caption alpha (`out/parity/ui1/capture_port_r3.sh`). Both sides were measured by
`out/parity/ui1/r3/opening_check.py`:
- world brightness k;
- caption alpha on the glyph interiors;
- plaque alpha in its text band;
- each pad's disc band as a fraction of that side's own settled frames, median of the
  same 4.0..8.5 s window.

| rec11 frame | level age | world k, original / port | caption | plaque | left pad band | right pad band |
|---|---|---|---|---|---|---|
| n28 | 700 ms | 0.091 / 0.112 | 0.761 / 0.764 | 1.11 / 0.89 | 0.26 / 0.26 | 0.24 / 0.25 |
| n29 | 833 ms | 0.246 / 0.280 | 0.721 / 0.720 | 0.99 / 0.96 | 0.47 / 0.49 | 0.46 / 0.48 |
| n31 | 917 ms | 0.380 / 0.419 | 0.694 / 0.694 | 1.05 / 0.98 | 0.63 / 0.63 | 0.62 / 0.63 |
| n33 | 983 ms | 0.515 / 0.550 | 0.674 / 0.670 | 1.07 / 0.99 | 0.70 / 0.70 | 0.69 / 0.70 |
| n36 | 1050 ms | 0.728 / 0.706 | 0.649 / 0.646 | 1.05 / 1.00 | 0.79 / 0.78 | 0.78 / 0.77 |
| n38 | 1150 ms | 0.950 / 0.962 | 0.617 / 0.615 | 1.03 / 1.00 | 0.86 / 0.89 | 0.83 / 0.88 |
| n62 | 2000 ms | 1.000 / 1.000 | 0.331 / 0.330 | 1.00 / 0.99 | 1.11 / 1.09 | 1.08 / 1.08 |

- **Before.** At matched blackness review 2 measured the port's pad bands at
  0.06 / 0.18 / 0.40 / 0.56 against 0.26 / 0.47 / 0.63 / 0.69, and its caption 0.15
  brighter.
- **Now.** Both agree within 0.03 on every frame. The pads are 0 px from their corners
  from n28 on.
- **The caption and plaque end** 1.83 s after the black clears; rec11's end 1.78 s after.
- **What is left.**
  - **World k.** On this estimator it reads 0.02..0.04 brighter in the port on n28..n33
    and 0.02 darker on n36.
    - The estimator's own lead (473 ms) would bring the first to 0.014..0.022, but take
      n36 to 0.049 darker.
    - So no single lead explains the residual; it sits within the spread of the three
      estimates.
  - **Plaque at n28.** The original's 1.11 is divided by a k of 0.09, which multiplies
    its noise by eleven. The port's 0.89 is the decoded `smoothEnd` at 700 ms.
- **The library frames** (1-01, 1-05, 1-17, 3-05, 1-15, 2-07 at t2.0, t4.5 and t8.0)
  are **byte-identical** to step 39's captures. They are all past 1.165 s, where nothing
  changed, and compare as step 39 tabled them (offset 0, 0 on all but 2-07 t4.5).

**DECLINED AGAIN, WITH THE DECODE: THE HUD OVER THE POPUPS OF 1-2 AND 1-3.** The
difference is real. But the original has no rule that hides the HUD on those two levels.
- **What opens the popups.** `Game::managePopups` (bytes 115157..115478) opens
  `LevelHelp1Popup` for world 0, level index 1 and `LevelHelp2Popup` for index 2. It
  does so through `UILayerManager::openPopup` (bytes 78770..78899), which makes the
  popup the current UI layer.
- **Why the HUD goes.** Restart, pause and the plaque belong to the game layer, so they
  go with the swap: review 3 measured the plaque at 0.021 on 1-03 even with a gold
  medal saved. **"Part 2" and "Part 3" stay drawn over the popup** in both captures,
  because the caption is not a layer sprite.
- **What hiding it here would do.** Hiding the HUD by level name would encode a rule
  the original does not have. It would also leave two levels with no controls and
  nothing to dismiss.
- **What the unit of work is.** The popup:
  - `Popup` and its layer (bytes 71281..73437);
  - `LevelHelp1Popup` (constructor 205113..207734, draw 208104..208915);
  - `LevelHelp2Popup` (209049..211708, 212078..212768), each an animated page with a
    hand, blocks and a portal;
  - the continue button.

  Seven more popup classes and `HelpBlockController` serve the other levels. That is
  its own step, and the HUD will hide with its layer when it is built.

**Not done, and said so.**
- **Input during the hold is not gated.** Under the whole black the pads walk and a tap
  can open a portal. The original takes none during its load, which draws no frame.
  Nothing measured it.
- **No capture of the original's 1-15 opening exists**, so the sign's hand-over is held
  against the decode only.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning.
- **Magic Portals suites.** **35 of 35** pass, and **112 of 112** across the whole
  ctest set.
- **`test_mp_hud`: 179 → 255** (-1 + 29 + 1 + 1 + 2 + 44 = 76).
  - The black held and its squared fade from 465 ms: 20 checks, replacing the 21 that
    timed it from the first tick.
  - rec11 frame by frame, each dated by its caption: world k within 0.05, pads home and
    plaque up on n28..n36, and n27 whole (29). A black on the level's own clock misses
    these k by 0.25..0.9.
  - The pads between the blacks, and hidden while both are whole - marked inferred (+1).
  - The file's 465 (+1).
  - A negative `start_after_ms` refused by name (+2).
  - `HandOver` and the sign fed the hold in one piece against sixty short frames (44).
- **`test_mp_layer`: 294 → 304.**
  - At 450 ms both blacks are still whole (4).
  - level14's sign is unmoved under them and within 3.5 units of its corner by 700 ms (4).
  - A retry pressed during the hold hands nothing of the attempt before to the sign's
    chase: at 700 ms it is where a first attempt has it (2).
  - The settled-HUD check waits 1.2 s instead of 1.
- **Smart App Control.** It refused `test_mp_portal`, `test_mp_launchers` and
  `MagicPortals.exe` once each after relinking (BAD_COMMAND, "Permission denied"). Each
  ran once relinked again.

## Step 41 - the original's lighting, read from the levels and drawn by nothing yet (built)

The port draws no lighting: every sprite is its texture, tone-mapped, which is why the
arches of 1-1 are mid-grey where the original's are near-black and a white texel stops
near 186. The remake's `out/parity/specs/lighting/design_port.md` plans the whole of it
from a numerical fit of the original's pixels (`fit.md`, about 1/255 per block), in steps
that each change one thing. **This is its step G1, and it is only reading**: the levels
now carry the original's lighting, the port reads it strictly, and **not one pixel
changes**. The engine is not touched.

**THE DATA, REGENERATED AND PROVED UNCHANGED BUT FOR THE KEYS.** The remake's b572fec
taught its converter to write every lighting input a level file holds as quoted
`metadata/eth_*` strings, and `--copy-lighting` to copy the files they name. `out/` was
regenerated with that committed converter, from the remake's root:

```
tools/converter/.venv/Scripts/python -m ethanon2godot --world N --copy-textures --copy-lighting   (N = 0, 1, 2, 3)
```

The hashes of `out/levels`, `out/data/chapters.json` and `out/assets` were recorded and
the levels copied aside first (`out/parity/specs/lighting/work/g1/`). After:
- **All 128 `.tscn` changed, and all 128 are byte-identical to the file before once their
  `metadata/eth_*` lines are taken out** (`tscn_diff.py`). Every one of those lines is
  `metadata/eth_<key> = "<text>"`, and there are **11,234** of them: the converter's own
  report, summed over the four runs (3,042 + 2,277 + 3,708 + 2,207).
- **`chapters.json`**: md5-identical.
- **Every file that was under `out/assets`** - 279, the 66 entity textures among them -
  md5-identical. What is new: 730 lightmaps in 67 level directories under
  `assets/lightmaps/`, the 44 normal maps under `assets/entities/normalmaps/`, and
  `halo.bmp` and `spark_halo.bmp`. `portal_halo.png`, already there as a sprite, was
  copied over itself and is unchanged.
- **The Godot copy is out of date on purpose.** The remake's gitignored
  `game/assets/levels` is still byte-identical to the levels BEFORE this step. Nothing in
  this repository reads it.

| Key | Lines | Where the converter writes it |
|---|---|---|
| `eth_ambient`, `eth_light_intensity` | 128 each | the root, always |
| `eth_z` | 1,533 | any entity node off z 0; the same nodes as `z_index`, unrounded |
| `eth_emissive` | 2,140 | any entity node with a non-zero emissive |
| `eth_static` / `eth_apply_light` | 2,676 / 1,719 | only nodes that draw a sprite or own a light |
| `eth_normal` | 1,720 | the same; 27 distinct files |
| `eth_lightmap` | 730 | static, applyLight and a sprite; in 67 levels |
| `eth_light_range` | 72 | every light, default range included; 68 on static owners |
| `eth_light_offset` / `eth_light_color` | 71 / 72 | a light, when not the default |
| `eth_halo` | 71 | a light with a `<HaloBitmap>` |
| `eth_halo_offset` / `_size` / `_brightness` | 54 / 54 / 66 | a halo, when not the default |

**THE READER, `sim/Lighting`.** Renderer-free, beside `Sprites`, with the design's
section 3.5 interface: `Lighting::Read(scene, resRoot, out, error)` fills the scene's
ambient and intensity and a `Look` for **every** entity node (4,065 in the game), a node
with no key standing at the engine's defaults - depth 0, not static, no light, emissive
0, colour 1, a light's range 256 and halo 64 at brightness 1. Those are decoded
defaults, cited in the header, not a guess. The one exception is the root pair: every
level file has a `<SceneProperties>`, so a root without both is refused, never given
ambient 1 and intensity 2.

`Tscn` already reads any `metadata/*` string, so **its vocabulary does not grow and
`test_mp_tscn` is untouched**. The strictness about which `eth_` keys exist, where, and
what they may say moves up into `Lighting`. It refuses, naming the node's line:
- an `eth_` key it does not know;
- a scene key on an entity, or an entity key on the root, or any key below an entity
  node;
- a value that is not a quoted string of exactly the numbers the key takes, one space
  apart, all finite (no `inf`, no `nan`, no double space);
- a flag that is not `"0"` or `"1"`;
- a path that is not `res://` or names no file;
- a lightmap on a node that is not static, does not apply light, or draws no sprite;
- a lightmap whose size times two is not its sprite's;
- a light key without `eth_light_range`, a halo number without `eth_halo`, or a range
  that is not positive;
- a sprite-or-light key (`eth_static`, `eth_apply_light`, `eth_color`, `eth_normal`, the
  light's) on a node that neither draws a sprite nor owns a light.

**The design's list has neither the placement rules, the flag rule nor the halo rule;
they are the converter's own conventions, which landed after the design.** The design
put every entity key on sprite-or-light nodes only.
The converter writes `eth_z` and `eth_emissive` on every node, because the player
marker has no sprite and is where the port's player will take its lighting height -
and 2-09's `main_char` sits at z 2. So those two may stand anywhere, and the rest keep
the converter's gate. Before the reader was written, an independent parser of the emitted
files (`precheck.py`, beside `tscn_diff.py`) checked the gate, the lightmap's three
conditions and its size, and that every path names a file. It found 0 violations over
the 128 levels, and the reader then accepted all 128.

**The lightmap's size is checked against `Sprites::Find`'s**, not a second read of the
sprite's file, so it is the size the layer will stretch the lightmap over. All 730
lightmaps are exactly half their sprite on both axes, as the remake's
`data_inventory.md` found. **That factor of 2 holds against `sizePx` as it is today**:
1x art, where an image pixel is a level unit. A lightmap is a quarter of the hd sprite per
axis. So when plan_port's System 6 draws the hd tier, `sizePx` must stay in level units
(the design's "world units", with the tier's 0.5 folded in), or this check must change its
factor. Otherwise it refuses all 730.

**Not a pure function of the text**, in the same way `Sprites::Find` is not: it asks the
filesystem whether each path is a file, and reads the lightmaps' and sprites' headers.

**Nothing draws it.** The layer does not call `Lighting::Read`, and `Game::Data` does
not carry it. Reading it in the layer, the ambient and emissive, the lightmaps, the lights
and the halos are the design's steps G3 to G6, after the engine steps E0 to E3. The
design's `color` field turned out to be an instance property (the remake's
`docs/ethanon-formats.md` correction: `ETHEntity.cpp:236`); the key is the same and no
level carries one.

**GATES.**

| Gate | Required | Measured |
|---|---|---|
| `out/levels` after regeneration | differs only by `metadata/eth_*` lines | 128 of 128 identical once those lines are removed; 11,234 lines; no other byte |
| `out/data/chapters.json` | unchanged | md5 identical |
| entity textures | unchanged | 66 of 66 md5 identical (279 of 279 files under `out/assets`) |
| converter totals | 730 lightmaps, 72 lights / 68 static, 71 halos, 1,720 normals, 2,140 emissive, 1,533 z; 11,234 lines | all equal, and `test_mp_lighting` and `test_mp_start` read the same from the port's side |
| 1-01 capture, `level0 --window 1280x720 --fixed-step --frames 420` | byte-identical | `a5abafb357766442e22edd119e01952b` before and after |
| 2-26 capture, `level25a`, the same | byte-identical | `0e925ef51e310519076594c231afbd0d` before and after |
| `test_mp_levels` `metaStrings` | raised by exactly 11,234 | 5,700 → **16,934**; `grep -cE '^metadata/[^ ]+ = "'` over the files counts 5,700 before and 16,934 after |
| `metaVectors` / `metaNumbers` / outside joints | unchanged | 837 / 80 / 0 |

**The captures are against this step's own before, not the design's.** The design's
`port/1-01_level0_f420.png` and `2-26_level25a_f420.png` were taken before steps 38 to
40 put the HUD in the frame, so they no longer match today's port with or without this
step (md5 `eee7bdb7...` and `f1b9368f...`). The before-captures were taken first, twice,
with identical md5s, so the port is deterministic here. Then the data was regenerated,
the code added, and both were captured again. The logs differ only in the timings and
the file name.

**What the data change alone did**, run before any code: of 112 suites, only
`test_mp_levels` failed, and only on `metaStrings` (got 16,934). No other suite reads
metadata it does not know.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, with the four changed sources forced to recompile.
- **ctest.** **113 of 113** pass, 36 of them Magic Portals suites.
- **`test_mp_lighting`: new, 415 checks** (230 without the levels, which skip by
  themselves and never with 77).
  - Every key read off a hand-written scene with invented values, and every absence read
    as its default: a light with no sprite, a marker, a lit wall with no lightmap, a node
    with no key.
  - 31 refusals, each checked for its message and for the line of the node it names.
  - Over the 128 levels:
    - the totals, and 67 levels with lightmaps;
    - each lightmap in its own level's directory, named `add<id>` for its own node's
      instance id;
    - the 730 files on disk are exactly the 730 the levels name;
    - level0's ambient (0.35, 0.3, 0.35) at intensity 3, its 9 lightmaps, and its torch
      `light_ent_696` key by key;
    - lights at defaults, one each: 1-2's `portal_static` with no halo, 1-8's blue light
      at halo brightness 1, and 2-11's fire agent at range 256 with the default halo
      offset and size;
    - 4-22's file ambient 0.5 at 3.5;
    - 2-09's player marker at z 2, the only one off 0;
    - 2-01's `static_sphere`, the one normal map on a sprite that applies no light.
- **`test_mp_start`: 552 → 689.** Every level's lighting read, 128, plus the 9 totals.
  The 552 was not re-measured before the edit. It is derived: the measured 689, less the
  137 the new sweep adds by construction, is 552, which is what step 35 last recorded.
- **`test_mp_levels`**: 128 checks, the pin raised.
- **Unchanged:** `test_mp_tscn` 168, `test_mp_sprites` 155, `test_mp_layer` 304.
- **Smart App Control.** Two relinks, two refusals ("Not Run").
  - The first relink: `test_mp_ghost`, `test_mp_boss` and `test_mp_sprites` were refused.
  - The second relink (`Lighting.cpp` moved to its alphabetical place in the library's
    list): `test_mp_chapters`, `test_mp_fields`, `test_mp_fire`, `test_mp_bounce` and
    `test_mp_boss` were refused.
  - Each ran once relinked again. `MagicPortals.exe`, relinked both times, was never
    refused, and captured 1-01 and 2-26 at the same md5s after the second.

## Step 42 - the material descriptor sets given back, and the walk that ran out without them (built)

The lighting design's step E0 (the remake's `out/parity/specs/lighting/design_port.md`,
section 4.8). **An engine defect the lighting would trip, fixed before the lighting
exists.** The design gives every lightmapped sprite a material set of its own: 730
lightmaps in 67 levels, at most 21 in one (3-13). `TextureRegistry`'s pool held 512
sets and never took one back. **No pixel changes**, and nothing in the port draws
differently. The game gains only a DEV switch that proves the fix.

**THE DEFECT, REPRODUCED BEFORE ANYTHING WAS FIXED.** The pool had no
`eFreeDescriptorSet` flag. `Invalidate` and `ReplaceRGBA` erased sets from the cache
and never freed them. The capacity check read the cache's size, so after any drop it
undercounted the pool.
- The design predicted two symptoms: the fallback set at 512 cached, or a throw once
  drops had hidden the pool's use. The walk below hit the second.
- **`MagicPortals --visit-levels lightmapped --visit-passes 2`, on the unfixed engine,
  died at its 44th level (3-10)**: `fatal: vk::Device::allocateDescriptorSets:
  ErrorOutOfPoolMemory`, exit 1, 7.9 s in.
  - The cache held 65 sets at the time.
  - The pool held 512, by the log's arithmetic: 65 cached, plus the 444 lightmap sets
    of the 43 levels before (dropped, never freed), plus 3 of 3-10's 11. The fourth
    lightmap loaded, and its set was the 513th.
- **The `sets.empty()` throw after the allocation was dead code.** vulkan-hpp throws
  `OutOfPoolMemoryError` itself, so that exception went straight out of the frame.

**THE FIX (engine: `TextureRegistry`, the new `renderer/MaterialSetLedger.hpp`).**
- **The pool is freeable, and holds `MaterialSets::kMaxSets` = 1024**, the design's
  margin. A set is a few bytes of pool.
- **A dropped set is freed through `VulkanDevice::DeferDestroy`**, because a command
  buffer from the last two frames may still bind it. Both of the registry's reasons to
  drop a set use one function, `MaterialSets::TakeNaming`: a texture invalidated, and a
  texture's pixels replaced. It removes every set naming the id in any binding and
  returns them. It replaces two hand-written loops that erased and forgot. So a texture
  replaced once a frame no longer takes a set from the pool once a frame.
- **The cap is checked against the pool, not the cache.** `MaterialSets::Ledger` counts
  a set as live from its allocation until its free actually runs. A set out of the
  cache and still queued is exactly what the old check missed.
  - `MaterialSetsInPool()` reads that count.
  - `MaterialSetCount()` keeps its meaning, the cache's size, so nothing that reads it
    moves.
- **A pool that refuses degrades instead of throwing.** Full by the ledger, out of pool
  memory, or fragmented, it hands out the white fallback's set and logs it, as the old
  cap branch did. vulkan-hpp throws the last two, and a freeable pool can be
  fragmented. Any other error propagates.
- **The images are resolved before the set is allocated.** A throw between allocation
  and cache would leave a set the ledger counts and nothing gives back.
- **Shutdown.** `~VulkanRenderer` destroys the registry at `m_textureRegistry.reset()`,
  and the pool with it (which frees every set). Only later does it call
  `FlushDeferredDestroys`. A queued free holding the pool's handle would then free into
  a destroyed pool.
  - So the pool and its ledger live in a `shared_ptr` the registry owns, and a queued
    free holds a `weak_ptr`.
  - After the destructor it no longer locks, and the free does nothing.

**THE RECYCLED HANDLE (engine: `VulkanRenderer`).** Freeing sets makes one thing false
that `test_shadowcache` stated in so many words: "TextureRegistry hands back a set it
has never handed back before."
- `ShadowPassSignature` mixes a cut-out caster's material set **handle**, and
  `ShadowCache` keeps that signature across frames. Once sets are freed, the driver may
  give the same handle to another material. Same handle, same everything else,
  different holes: a cached pass that looks right.
- **The seed every pass signature starts from now mixes
  `TextureRegistry::Generation()`.** Every call that drops a set bumps it, so a reused
  handle always arrives at a later generation than any signature that saw it.
- The cost is one re-record of every pass per texture reload.
- The two other handle uses are the opaque and blended passes' batching keys, which
  live within one frame, so they are unaffected.
- The comment in `test_shadowcache` is corrected, and `ARCHITECTURE.md` says the same
  beside the signature.

**THE PROOF (game, DEV ONLY: `LevelVisit.{hpp,cpp}`, two flags in `main.cpp`).**
`--visit-levels <lightmapped|all|name,...>` and `--visit-passes <n>` push a second layer
beside `MagicPortalsLayer`. It is never pushed without the flag, and is built into the
executable only, never `MagicPortalsGame`, so no suite links it.
- **It stands in for what the lighting will do.** G3's unload does not exist yet, and E2
  has not added the lightmap binding.
  - For each level, it reads the lightmaps through `Lighting::Read`.
  - It opens the level with `PressMenu` (a `Level` button), waits 6 frames, then acquires
    each lightmap as a data texture with one material set of its own. That is the pool
    pressure the fourth binding will add.
  - It holds them for 2 frames, then invalidates them before opening the next level, as
    G3 will.
  - The sets are never bound, so nothing is drawn.
- **Its checks, any failure failing the run:**
  - at each release, the level's sets leave the cache at once and stay counted in the
    pool, because their free is deferred;
  - six frames on, the pool equals the cache;
  - n lightmaps add exactly n to both counts;
  - no acquisition comes back as the fallback;
  - after the last release, the pool equals the cache again;
  - then the last level's lightmaps are taken and dropped **on the frame the run
    quits**, so their frees are still queued when the renderer destroys the pool. That
    is the shutdown case above, and validation is what would catch a free into a
    destroyed pool.
- `main` returns failure if the walk did not finish or recorded a failure, before the
  existing validation-error check.

**On the fixed engine, the same command: exit 0 in 19.1 s, validation ACTIVE.**
- **The walk:** 134 visits (67 levels, twice) and 1,460 lightmap sets acquired and
  dropped. That is 1.43 times the new cap and 2.85 times the old.
- **Every release:** 134 of 134 left exactly the released sets waiting (at most 21, at
  3-13), with the pool unchanged at that moment.
- **Every visit:** 134 of 134 found the pool equal to the cache six frames later, and
  134 of 134 added exactly n to both.
- **The baseline:**
  - **Pass 1** climbed from 22 to 75, as the levels' own sprites and the HUD cached
    their images. Those sets are never dropped.
  - **Pass 2** stood at **75 at all 67 visits**.
  - The peak was **96**: 75 plus 3-13's 21.
- **After the last release:** 75 in the cache and 75 in the pool. The run quit with 5
  sets still waiting, and `Subsystem resources destroyed cleanly`.
- **The log:** no "exhausted", "out of memory", "fragmented" or "did not count" line,
  and not one line containing "error".

**WHAT NO SUITE COVERS, AND WHY.** No suite can construct a `TextureRegistry`: it needs a
device, a command pool and the pipeline's material layout. Step 20 recorded the same for
`MeshRegistry`.
- **Split out so the suites can reach it:** the ledger and `TakeNaming`, the part that
  decides. `test_materials` grew four cases, 210 to 235 checks:
  - a set dropped from the cache still fills the pool until it is given back;
  - giving back more than was taken is refused, not wrapped;
  - every binding is searched for a dead id, and what was taken is handed back in key
    order;
  - the cap is 1024.
- **Only a run can reach the rest:**
  - the `vkFreeDescriptorSets` call;
  - its deferral past the frames in flight;
  - the pool flag;
  - the fallback on a real refusal;
  - the weak reference at shutdown.
- **The deferral is proved** by the visit's release check (the pool unchanged on the
  frame of the drop) and by validation staying silent while dropped sets were still
  bound by the previous two frames' command buffers. The engine's own
  `SupersonicEngine --frames 120` self-check does that too: it invalidates
  `uv_grid.png` at frame 60 (the log says `texture=dropped`), and any set naming it now
  goes through the new free.
- **The shutdown guard is load-bearing, measured with it removed.** For one build, not
  kept, the queued free captured the raw device and pool handle and freed without
  locking. The same walk quit with its 5 frees queued and then **failed: exit 1, "20
  Vulkan validation error(s); failing the run"**. The first was
  `vkFreeDescriptorSets(): descriptorPool Invalid VkDescriptorPool Object`
  (`VUID-vkFreeDescriptorSets-descriptorPool-parameter`, then `-pDescriptorSets-00310`),
  printed during shutdown after the registry was gone.
  - The source was restored from a copy taken before the experiment. On the restored
    source, the build, the full ctest, the walk and the four captures were all redone,
    with the numbers in the table.
  - The guard rests on `m_setPool` being the only strong owner. The header says so.
- **Not proved: the degraded paths never ran.** Those are the out-of-memory and
  fragmented catches, and the ledger-full branch. The walk peaked at 96 of 1024.

**LEFT FOR LATER STEPS.** The layer does not invalidate anything on unload yet (G3), and
there is no fourth binding (E2). `--visit-levels` is the check to run again once both
exist, with the probe's own acquisition replaced by the layer's.

**GATES.**

| Gate | Required | Measured |
|---|---|---|
| visit walk, unfixed engine | reproduces the defect | fatal `ErrorOutOfPoolMemory` at visit 44 (3-10), 444 lightmap sets in, 65 cached; exit 1 |
| visit walk, fixed: exhaustion | none | 1,460 lightmap sets over 134 visits; peak 96 in pool of 1024; 0 fallback sets; exit 0 |
| visit walk: set count back to baseline after each unload | pool == cache at every visit | 134 / 134; pass-2 baseline 75 at every visit; after the last release 75 / 75 |
| visit walk: frees deferred, not immediate | pool unchanged on the release frame | 134 / 134 releases (5 to 21 sets waiting) |
| visit walk: validation | active and silent | `Vulkan validation layers: ACTIVE`; no validation error; `main` exit 0 |
| visit walk with the shutdown guard removed (experiment, not kept) | validation catches the free into a destroyed pool | exit 1, 20 validation errors, `VUID-vkFreeDescriptorSets-descriptorPool-parameter` |
| `MainScene.scene --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical | `1e24c2a30f6f22dc2bb01b6038bd1af9` before (twice), after (twice) and on the final source (twice); `Clean exit with validation active` |
| `WolfBrigade --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical | `d9e7b8fe5e8b0b2f162b0195e5d5ba31` before (twice), after (twice) and on the final source (twice) |
| 1-01 `level0 --window 1280x720 --fixed-step --frames 420` | byte-identical (not required by E0) | `a5abafb357766442e22edd119e01952b` before and after, step 41's md5 |
| 2-26 `level25a`, the same | byte-identical (not required by E0) | `0e925ef51e310519076594c231afbd0d` before and after, step 41's md5 |
| build | zero warnings | 0, with the 17 translation units that include a changed file recompiled |
| ctest | all pass; `test_materials`, `test_resourcesync` named | 113 / 113; `test_materials` 235, `test_resourcesync` 22 (unchanged), `test_shadowcache` 82 (unchanged), `test_mp_layer` 304 (unchanged) |

The MainScene captures without `--fixed-step` differ between two runs of the same
binary (`08b6075f...` and `cd0c9fd3...`). The design's command is `--frames 120
--screenshot`, so both games were captured with `--window 1280x720 --fixed-step` added,
which is what makes it a byte-identity gate.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, twice. Each time the changed engine header and sources, both
  tests and the two game files were touched, so all 17 dependent translation units
  recompiled. The second time was the final source, after the experiment was reverted.
- **ctest.** **113 of 113** pass, twice, the second on the final source. No suite was
  added.
- **Smart App Control.** Two full builds, so two rounds of refusals ("Not Run"), each
  resolved by deleting and relinking the refused executables.
  - **First build.** 13 suites were refused: `test_scenemanager`, `test_sat`,
    `test_wb_combat`, `test_mp_timed`, `test_mp_movers`, `test_mp_hinge`,
    `test_mp_zerog`, `test_mp_boss`, `test_serialize`, `test_mixer`, `test_materials`,
    `test_hierarchy` and `test_gltf`. On the first relink `test_sat` and
    `test_mp_zerog` were refused again, and both passed on the second.
  - **Final build.** 13 different suites were refused: `test_renderplan`,
    `test_wb_selection`, `test_husk_layer`, `test_mp_geometry`, `test_mp_play`,
    `test_mp_chapters`, `test_mp_camera`, `test_mp_timed`, `test_mp_sounds`,
    `test_mp_hud`, `test_shadowcache`, `test_packaging` and `test_mixer`. On the first
    relink `test_mp_sounds` and `test_mixer` were refused again, and both passed on
    the second.
  - After each, a full `ctest` ran 113 of 113 with none refused.
  - `MagicPortals.exe` was refused after 4 of its 11 links this step. Each was cleared
    by one or two relinks.
  - `WolfBrigade.exe` was refused twice. The first was on the first baseline capture,
    before any change, and it ran on the next attempt without a relink. The second was
    after the final build, cleared by one relink.

## Step 43 - the scene that says its numbers are display values, and the port that does not say it yet (built)

The lighting design's step E1 (the remake's `out/parity/specs/lighting/design_port.md`,
section 4.1, which is plan_port's System 1a). **An engine switch, off by default.** A
scene can now declare that its target holds display values rather than linear radiance.
Then colour textures are sampled as the bytes in the file, the bloom is not recorded,
and the composite only clamps. An optional 5/6/5 quantisation goes after it. **Nothing
turns it on**: the port opts in at G2. Wolf Brigade, MainScene and seven Magic Portals
levels capture byte-identical before and after.

**WHY A MODE AND NOT A TINT.** The original multiplies and blends on encoded 8-bit values.
The design's fit (`fit.md` section 4) scores the two readings against the original's pixels:
- no-lightmap sprites below full emissive, 1,359 blocks: blending in linear light **13.59**
  of 255, on encoded values **0.28**;
- lightmapped ones, 1,042 blocks: **11.90** against **0.92**.

Step 39 met the same wall from the HUD side: linear 1.0 leaves Reinhard at 0.5, which
encodes to 186. It went around it with a pass after the composite. The level art is scene
geometry, so the scene itself has to be able to say what its numbers are.

**THE SETTING (engine: `core/RenderSettings.hpp`).**
- `enum class SceneEncoding : uint8_t { LinearHdr, LinearNoToneMap, DisplayEncoded }`,
  default `LinearHdr`.
  - The composite reads it as the enum's value, so the order is part of the shader's
    contract. The header says so, and `test_screenoverlay` holds the shader to it.
- `enum class OutputQuantize : uint8_t { None, Rgb565 }`, default `None`.
- `decodesColourTextures()`: false only for `DisplayEncoded`. One place to ask, like
  `drawsSky()`.
- `SceneClearColor(const RenderSettings*)`: the clear, all cases in one function.
  - A null settings, or a sky under either linear mode, keeps **0.00023**. That is the
    radiance Reinhard and the encode bring back to 0.02, and the sky covers it anyway.
  - A flat colour is written verbatim, as before.
  - A `DisplayEncoded` scene is cleared to `backgroundColor` as it stands, sky or not. A
    radiance means nothing in a target of display values.
  - The design is silent on a `LinearNoToneMap` sky. It keeps the literal.

**WHERE IT IS READ.**
- **`RenderSystem::SyncResources`** asks for albedo with `srgb = decodesColourTextures()`.
  - It does so on both acquisitions: the entity's, and a mesh section's.
  - `ResourceSignature` takes the answer as a fifth, **undefaulted** parameter, mixed in
    as a byte of its own. Switching the mode changes no path and moves no generation, so
    without it every sprite would keep its sRGB copy.
  - The section loop is gated on the texture generation, not on the signature. So
    `GpuMesh` gains `sectionDecodesColour`, compared beside `sectionTextureGeneration`.
  - `TextureRegistry::Acquire` keys its cache `"srgb:"` or `"data:"` plus the path
    (`TextureRegistry.cpp:227`) and passes the flag to `UploadRGBA`, which picks
    `eR8G8B8A8Srgb` or `eR8G8B8A8Unorm` from it (`:181`, `:267`). Checked in the source,
    because the signature flipping would be worth nothing if the lookup then handed back
    the sRGB copy. Both uploads coexist. No capture isolates this half: the port
    experiment's brighter glows are explained by the missing tone map alone.
  - Normal and ORM maps stay data in every mode.
- **`SupersonicApp`** copies `encoding` and `quantize` into `BloomPass::Settings` every
  frame, beside the four numbers it already copied.
  - That block runs in game mode too: it is at the frame loop's level, not inside the
    editor's UI. The port experiment below shows the mode reaching the composite from a
    game layer.
- **`BloomPass`.**
  - `RunsBloomChain(settings)` is false for `DisplayEncoded`. `Record` then skips the
    bright and both blur passes and records the composite only.
  - `CompositeValue(settings)` packs the push constant: (intensity, or 0 when the chain
    is not recorded; exposure; the encoding's value; 1 to quantise). **With the defaults it
    is (intensity, exposure, 0, 0), exactly what the composite was always given.**
  - The design put the forced intensity 0 in `SupersonicApp`. It lives here instead, so
    every caller gets it and a suite can reach it.
- **`VulkanRenderer`** clears to `SceneClearColor`. `drawSky` still decides the sky pass.
- **`SceneSerializer`** writes `"Encoding": "LinearNoToneMap" | "DisplayEncoded"` and
  `"Quantize": "Rgb565"` **only when not the default**.
  - A scene that never chose saves to the bytes it saved before.
  - Any other word, and a missing key, reads as the default, as an unknown `Background`
    reads as the sky.
  - A scene loaded over a display-encoded one does not inherit it.
- **`InspectorPanel`**, not in the design's list, and small:
  - an encoding combo and a 16-bit checkbox;
  - the background picker stops its pow(1/2.2) / pow(2.2) round trip under
    `DisplayEncoded`, where the stored number is already the display value.

**THE COMPOSITE (`bloom_composite.frag`, regenerated `.spv`).** The design's whole file,
with two departures, both measured:
- **LinearHdr** keeps its two lines in their order: Reinhard, then pow(1/2.2).
- **LinearNoToneMap**: pow(clamp(colour, 0, 1), 1/2.2).
- **DisplayEncoded**: clamp(scene * exposure, 0, 1). **It does not read the bloom at all.**
  The design added `bloom * 0`. But in this mode binding 1 is not the bright image (next
  paragraph), and 0 times a non-finite texel is NaN.
- **Rgb565 writes the 8-bit reading of the level, as k/255.** The design wrote
  `floor(c * levels + 0.5) / levels` and left the driver to store it.
  - Measured on a MainScene copy, that stored level 20 of 31 (164.516 of 255) as **164**,
    on all 322 pixels at that level, and 0 pixels as 165. One green pixel came out 133
    for level 33 of 63 (133.57).
  - Now `floor(level * 255 / levels + 0.5) / 255`: **0 pixels off a level** in the same
    capture.
  - **Which 8-bit reading the original's captures use is not settled here.** Replicating
    the high bits into the low ones differs from `round(level * 255 / (2^bits - 1))` by
    one on 4 of the 32 five-bit levels and 10 of the 64 six-bit ones. G6 gates against the
    fit, and the shader comment names both.
- The `.spv` was rebuilt by the `Shaders` target with the SDK's glslc (step S0's
  configuration), and a direct `glslc bloom_composite.frag` produces the same bytes.

**THE BLOOM THAT IS NOT RECORDED LEAVES AN IMAGE WITH NO LAYOUT.** The bright image is
only ever in `eShaderReadOnlyOptimal` because the bright pass left it there, and the pass
starts from `eUndefined`. A composite that skips the pass cannot bind it. So `BloomPass`
allocates a second composite set, `m_compositeSceneOnlySet`, that names the scene image in
both bindings. The pool grew from 4 sets and 5 images to 5 and 7.
- **Proved load-bearing, for one build, not kept.** The skip branch bound the ordinary
  `m_compositeSet` instead. A `DisplayEncoded` MainScene then **failed: exit 1, "10 Vulkan
  validation error(s)", `VUID-vkCmdDraw-None-09600`**: "expects VkImage ... to be in layout
  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL--instead, current layout is
  VK_IMAGE_LAYOUT_UNDEFINED".
  - The source was restored from a copy taken before the experiment, and everything below
    was rebuilt and re-run on it.
- The composite pass is never skipped. It writes the image `--screenshot` reads, and the
  image `RecordOverlay` loads.

**THE SCREEN OVERLAY (step 39) IS UNTOUCHED.** It is still a second pass over the
composited `R8G8B8A8Unorm` image. It is loaded after the composite in every mode, with
its textures acquired UNORM regardless of the scene's encoding.
- **Under the defaults it is bit-exact.** All seven port captures below carry the HUD, and
  all seven are byte-identical.
- **Under `DisplayEncoded` it still draws, from a game layer.** For one build, not kept, the
  port forced the mode in `OnUpdate`.
  - 1-01 and 2-05 at frame 420 exited 0, `Vulkan validation layers: ACTIVE`, with no
    validation message.
  - The restart, pause and arrow buttons are drawn over the new composite.
  - The mode reached the composite from the game: pixels at 255 went from **0 to 2,344**
    (1-01) and **0 to 1,646** (2-05), the portals and the torch flame. Under Reinhard, 255
    is unreachable.
  - The HUD's own pixels are not a byte-identity check here: its controls are translucent,
    so they carry the new background. G2 measures them.
- **Quantisation does not reach the overlay**, which is drawn after it. The original's HUD
  went into the same 16-bit framebuffer, so G6 has to decide whether that matters. Nothing
  here pretends it does not.

**WHAT THE NEW MODES DRAW (MainScene copies, `work/e1/modes/`).** Five copies of
`MainScene.scene`, each with only its `Rendering` line edited, were captured
`--window 1280x720 --fixed-step --frames 120`. Each capture is the editor viewport,
744 x 336, and so is every MainScene capture in this step, the gate's included:
`SupersonicEngine` is the editor, where `--window` sizes the window and the offscreen
target follows the viewport (AGENTS.md). **All five exited 0 with
"Clean exit with validation active."**
- **Clear:**
  - Background `Color` (0.25, 0.5, 0.75) under `DisplayEncoded`: every one of the 744
    top-row pixels is **(64, 128, 191)**, the bytes of the authored colour.
  - The same scene under `LinearHdr`: **(123, 155, 173)**, which is Reinhard and the
    encode applied to it, predicted to the unit.
- **`Rgb565` over `DisplayEncoded`:**
  - 0 pixels off a 5/6/5 level, of 249,984.
  - The distinct values are R 25, G 41, B 27, against 124 / 119 / 180 unquantised.
  - Quantising the unquantised capture differs from it only by exactly one level, and only
    where the 8-bit value sits within half a step of a level boundary. That is a limit of
    reading an 8-bit capture, not a disagreement.
- **Whole frame:**

| Encoding | mean | max | share at 255 | mean abs vs default |
|---|---|---|---|---|
| `LinearHdr` (default) | 58.80 | 236 | 0.00 % | 0 |
| `LinearNoToneMap` | 60.40 | 255 | 0.53 % | 1.60 |
| `DisplayEncoded` | 21.37 | 255 | 0.52 % | 37.72 |
| `DisplayEncoded` + `Rgb565` | 20.75 | 255 | 0.52 % | 38.34 |

MainScene is a lit PBR scene, and its ambient and lights assume radiance, so
`DisplayEncoded` darkens it. That is expected, and ARCHITECTURE.md section 5 says it: a
display-encoded scene is expected to draw unlit.

**THE SUITES.**
- **`test_serialize` 419 -> 445** (floor 334 -> 400):
  - both words round-trip, and `LinearNoToneMap` on its own, so a reader that mapped every
    non-default word to one mode would be caught;
  - a default scene's file contains neither key;
  - an unknown word (`"Gamma24"`, `"Rgb444"`) loads as the default over a registry that
    held `DisplayEncoded` and `Rgb565`;
  - the clear, in all six combinations of background and encoding, and for null.
- **`test_resourcesync` 22 -> 26** (floor 19 -> 23):
  - decoded and undecoded are different signatures, and an unchanged mode still skips;
  - the four combinations of texture generation and mode are all distinct;
  - an entity with no material also re-resolves.
- **`test_screenoverlay` 28 -> 46** (floor 25 -> 40). It holds the overlay, and now the
  composite under it:
  - `CompositeValue` with the defaults is (0.55, 1, 0, 0);
  - `DisplayEncoded` at intensity 2 gives x = 0 and z = 2, and records no chain;
  - `Rgb565` gives w = 1;
  - `LinearNoToneMap` keeps the intensity and the chain, with z = 1;
  - it reads `bloom_composite.frag` and checks that each mode's number lands in the branch
    of the same index, and that 31 / 63 / 31 and `w > 0.5` are there.
  - **Mutation:** with the shader's `z < 1.5` edited to `z < 2.5`, and no rebuild (the suite
    reads the source at run time), it failed on exactly that check, 1 of 46. The file was
    restored byte for byte.
- **No suite constructs the pass.** `Record`'s skip, the second set, the driver's rounding
  and the clear on a real target need a device. They are proved by the captures and the
  experiment above.
- **Unchanged:** `test_materials` 235, `test_renderplan` 127, `test_sprite` 66,
  `test_draworder` 85, `test_shadowcache` 82, `test_mp_layer` 304, `test_mp_sprites` 155,
  and the 18 `test_wb_*`.

**NOT CONVERTED BY THE MODE, AND LEFT FOR LATER STEPS.**
- `grid.frag` still pre-linearises its colours, and the procedural sky and the PBR path's
  lights and fog assume radiance. A 2D display-encoded scene uses none of them.
  - A `DisplayEncoded` scene left on `Background::Sky` still records the sky pass, and is
    now cleared to `backgroundColor` beneath it. That pairing is incoherent and nothing
    gates it. G2 sets a black `Color` background, which records no sky.
- The float scene target's residual stays as design 4.1 states it. A translucent texel
  blended onto a destination already above 1 is not clipped per draw. The contingency is
  an 8-bit target, if a gate ever points there.
- The port does not opt in: that is G2, with `DisplayEncoded`, a black `Color` background
  and bloom 0.
  - Its HUD (steps 38 to 40) is drawn after the composite, so it keeps its look.
  - Any world quad the menus draw becomes UNORM and brighter, i.e. as authored.

**GATES.**

| Gate | Required | Measured |
|---|---|---|
| `SupersonicEngine --scene assets/scenes/MainScene.scene --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical before/after | `1e24c2a30f6f22dc2bb01b6038bd1af9` before (twice), after the first build, and on the final source (twice); step 42's md5; `Clean exit with validation active` |
| `WolfBrigade --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical before/after | `d9e7b8fe5e8b0b2f162b0195e5d5ba31` before (twice), after the first build, and on the final source (twice); step 42's md5 |
| port captures, `--window 1280x720 --fixed-step --frames 420` | unchanged until G2 opts in | before, after and final, all identical: 1-01 `a5abafb357766442e22edd119e01952b`, 1-09 `fca99769138c284b64d82f602014a1e2`, 1-13 `8b69fc8d3a0497a3db347eb24c2848bf`, 2-05 `7741d03fd24cc41323a10fa666b00818`, 2-26 `0e925ef51e310519076594c231afbd0d`, 3-05 `03ae1cbcc47d7630f7b225f9c803ec46`, 4-22 `a48cd2fcafd0d159404119fc9dc4b432`; validation ACTIVE, no message |
| screen overlay under the defaults | bit-exact | the seven port captures above carry the HUD, byte-identical |
| screen overlay under `DisplayEncoded` (experiment, not kept) | still drawn after the composite, validation silent | 1-01 and 2-05 exit 0, validation ACTIVE, no message; HUD drawn; pixels at 255 0 -> 2,344 and 0 -> 1,646 |
| `bloom_composite.spv` | regenerated from the GLSL | rebuilt by the `Shaders` target; a direct glslc compile is byte-identical to it |
| bloom skipped: a valid binding 1 | validation silent | 5 mode captures clean; with the bright-image set bound instead (experiment, not kept): exit 1, 10 errors, `VUID-vkCmdDraw-None-09600` |
| `DisplayEncoded` clear | authored colour verbatim | (64, 128, 191) for (0.25, 0.5, 0.75), 744 of 744 top-row pixels; `LinearHdr` (123, 155, 173) as predicted |
| `Rgb565` | every channel on a level | 0 of 249,984 pixels off a level (322 before the k/255 write) |
| build | zero warnings | 0, three full builds (146, 121 and 5 steps) and every relink |
| ctest | all pass; `test_serialize`, `test_resourcesync`, `test_materials`, `test_renderplan`, `test_sprite`, the 18 `test_wb_*` named | **113 / 113**; `test_serialize` 445, `test_resourcesync` 26, `test_screenoverlay` 46, `test_materials` 235, `test_renderplan` 127, `test_sprite` 66, all 18 `test_wb_*` pass |

`lightgate.py` was not run: the port captures are byte-identical to before, so every number
it would print is the one it printed before. The design gates the encoding at G2, with
`--variant engine_tier1x`.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, in any of three full builds.
  - The first: every changed file, and the composite shader compiled.
  - The second, on the final source: after the k/255 change and the first experiment's
    revert.
  - The third: after the port experiment's revert, which recompiled the layer and relinked
    `MagicPortals.exe` and `test_mp_layer`.
- **ctest.** 113 of 113 after the first build, on the final source, and once more after
  the last edits: the quantisation comment and the `tests/CMakeLists.txt` comment beside
  `test_screenoverlay`, which now names `bloom_composite.frag` too. That build reconfigured
  and had nothing to compile, with no warning and `Shader compiler:
  C:/VulkanSDK/1.4.357.0/Bin/glslc.exe` in its log.
- **Smart App Control.** Three rounds of refusals ("Not Run"), each resolved by deleting and
  relinking the refused executables until none was refused.
  - **First build.** 16 suites were refused: `test_sat`, `test_nav`, `test_wb_data`,
    `test_wb_waves`, `test_wb_combat`, `test_wb_snapshot`, `test_wb_audio`,
    `test_wb_selection`, `test_wb_hero`, `test_mp_tscn`, `test_mp_geometry`, `test_mp_play`,
    `test_mp_fire`, `test_input`, `test_layerstack` and `test_userdata`.
    - On the first relink `test_nav` and `test_wb_combat` were refused again.
    - On the second relink `test_wb_combat` was refused again. It ran on the third.
  - **Final build.** 39 suites were refused.
    - On the first relink 5 were refused again: `test_wb_economy`, `test_wb_selection`,
      `test_camera`, `test_cascades` and `test_gltf`.
    - Then 2 (`test_cascades`, `test_gltf`), then `test_cascades` twice. It ran on the fifth
      relink.
    - A full `ctest` then ran 113 of 113 with none refused, and again after the third build.
  - **`WolfBrigade.exe`** was refused once, on the first capture after the final build
    ("Permission denied", exit 126). It ran on the next attempt without a relink.
  - **`MagicPortals.exe`** was refused on every attempt after the port experiment's link
    (3 attempts at each of 2 levels) and after the revert's link (3 attempts at each of
    7 levels). Each time one delete-and-relink cleared it, and every capture in this record
    was retaken on the relinked binary. The PNGs of the refused runs were deleted first, so
    none of them could be mistaken for a new one.
  - The port experiment's first build did not compile (C2664: `insert_or_assign` wants an
    rvalue), and the two captures taken straight after it ran the previous binary. They were
    deleted, not measured.
- **The comment on the quantisation** was corrected after the last captures. It had claimed
  bit replication agrees on every level. The `Shaders` target recompiled the file, and the
  `.spv` is byte-identical to the one every capture above ran.

## Step 44 - the port says its numbers are display values, and the ground its skies left bare (built)

The lighting design's step G2 (the remake's `out/parity/specs/lighting/design_port.md`,
section 7.1, which is plan_port's System 1b with its V2 chosen). **The port opts in to the
switch step 43 built.** At attach the layer puts a `RenderSettings` in the registry's
context: `DisplayEncoded`, a flat black `Color` background and bloom 0. **No engine file
changes.** Nearly every level pixel moves (93 to 99 % of each captured frame); the HUD
does not; no ambient, emissive, lightmap or light is drawn yet (G3 onwards).

**Every gate the design lists for G2 passes. One clause it inherits does not, and is left
for the owner.** The design says G2 is plan_port's 1b, and 1b's non-regression clause
names fifteen levels, not section 7.0's seven. On 4-32 `edge_iou` falls by **0.0452**
against a tolerance of 0.02. The drop lies in two gaps this step stops hiding:
- the darkest level's scenery, still drawn at full brightness until G3;
- the sky, which the original pins to the camera and the port does not. That leaves black
  ground on **34 of the 128 levels**, up to half a frame.

Both are measured below, under the fifteen levels and the bare ground, and the decision is
under LEFT FOR THE OWNER. The first review found both. No source changed in answer; this
record did.

**WHAT THE LAYER SETS (`MagicPortalsLayer.cpp`, `SceneRendering` and `OnAttach`).**
- **`encoding = DisplayEncoded`.** The original multiplied and blended on the encoded bytes
  of an 8-bit framebuffer. The design's fit scores blending in linear light at **13.59**
  of 255 over 1,359 unlit blocks against **0.28** on encoded values, and **11.90** against
  **0.92** over 1,042 lightmapped ones (`fit.md` section 4). Every later lighting step
  multiplies into those same bytes, so this is the space the rest is defined in, not a look.
- **`background = Color`, `backgroundColor = (0, 0, 0)`**, and so no sky pass.
  - Ethanon clears to black: `video->SetBGColor(gs2d::constant::BLACK)` at start
    (`ETHEngine.cpp:114`), and black is `GLES2Video`'s own default (`GLES2Video.cpp:87`).
  - The original's script sets it once more: `LoadingScreen::preLoop` pushes
    `-16777216` (0xFF000000) and calls `SetBackgroundColor` (the remake's decoded listing,
    `out/parity/specs/ui3/work/motion/dec_loading.txt`). That listing is partial, so
    whether any other function calls it is not read here.
  - A display-encoded scene left on the sky is incoherent (step 43). Leaving the sky has a
    consequence of its own, measured below.
- **`bloomIntensity = 0`.** The original draws no bloom. `BloomPass::RunsBloomChain` already
  records no chain for this encoding, whatever the number says, so the zero only makes the
  scene's own settings say the same.
- **Left at their defaults:** `quantize None` (the 16-bit target is G6), exposure 1, and
  the bloom threshold and knee, which no recorded pass reads.
- **Where.** `OnAttach`, beside the clock, before the menu or a level is built, so the
  menu gets it and so does a start that fails.
  - `insert_or_assign`, not `emplace`: a `--scene` load may already have put a
    `RenderSettings` of its own there. The suite loads one first.
  - Nothing per level writes it, and `OnDetach` leaves it, as it leaves the clock.
- **Not in a data file.** The design's `lighting.json` (section 5.9) carries only `rgb565`
  among the scene settings, and that is G6's. These three are what the arithmetic is, not
  numbers to tune: a data file that said `LinearHdr` would put every later step's
  multiply back on radiance. The black's provenance is in the code comment and above.
- `MagicPortalsLayer.hpp`: the class comment gains the encoding, and the HUD's comment no
  longer says the scene blends in linear light.

**THE BEFORE WAS RETAKEN, NOT READ FROM THE DESIGN.** Section 7.0's "today" was captured
before steps 38 to 40 put the HUD in the frame, as step 41 found. Every gate capture
below was taken with the same command on step 43's build (before) and on this step's
(after).
- The seven section 7.0 levels at frame 420 reproduced step 43's md5s exactly (1-01
  `a5abafb3...` through 4-22 `a48cd2fc...`).
- Against `engine_tier1x`, the two classes this step gates came out exactly as section 7.0
  printed them (12.25 and 2.86): the HUD covers none of their blocks. Classes near the
  HUD corners did not (1-01 `LM_E=1` 8.82 against the design's 9.75), and neither did
  `edge_iou` (1-01 0.398 against 0.395). The table below uses this step's own before.
- Captures: `out/parity/specs/lighting/work/g2/{before,after,final}/`, by `capture.sh`;
  gates by `gates.sh` and `hud_g2.py`. `torch_g2.py` and `door_g2.py` are copies of
  plan_port's `torch.py` and `door.py` that read the port frames from there, and nothing
  else changed. `door.py` reads the original from `levels_v1`: its 2-01 t4.5 and t8.0 are
  md5-identical to `levels/`.
- **All 128 levels at frame 420, both builds, after the first review**:
  `work/g2/fix1/{before,after}/`, by `fix1/sweep.sh`.
  - The before binary is the reviewer's build of a `git archive` of step 43's commit. Its
    471 source, shader and CMake files were checked here against the commit: equal with
    carriage returns ignored, and every `.spv` byte-identical.
  - Every run exited 0, and every log says validation is ACTIVE with no VUID.
  - The sweep's frames agree with the other two sets wherever they overlap. Per build, 20
    are md5-identical to the reviewer's set and 8 to `before/` and `after/` above.
  - Scored by `fix1/census.py`, `ablate.py`, `attrib.py` and `camera_from_sky.py`, with
    results beside them. The final binary's frames are in `work/g2/fix1_final/`.

**THE COLOUR TRANSFER, ISOLATED (`lightgate --variant engine_tier1x`).** The design picked
the E = 1 classes without a lightmap because ambient and lightmaps cannot move them, so
they read the encoding alone.

| Level, class (blocks) | port vs model, before | bias before (R, G, B) | port vs model, after | bias after | port vs original, before -> after | floor (model vs original) |
|---|---|---|---|---|---|---|
| 1-09 `noLM_E=1` (519) | 12.25 | -9.8 -9.3 -12.2 | **0.29** | **-0.1 -0.1 -0.1** | 17.94 -> 9.01 | 8.94 |
| 2-26 `noLM_E=1` (1,161) | 2.86 | -0.1 -2.2 -3.6 | **0.91** | **-0.1 -0.1 -0.1** | 4.23 -> 2.16 | 1.92 |

- Both are within one unit of the model, with the same -0.1 bias on every channel. The
  -0.1 is the size the missing 5/6/5 predicts: `fit.md` scores `engine` 1.72 against
  `engine_no565` 1.83 on this class. It is left for G6.
- **1-09's port-vs-original 9.01 is the floor, not this step.** `engine_tier1x` is 8.94
  from the original on those blocks: it is the model drawn with the 1x art the port
  draws, and the original draws hd. That is plan_port's System 6.
- 2-26 is all `dark_sky.png`, and its port-vs-original 2.16 is within 0.24 of the floor.

**THE FLAMES.** Additive glows now clip at the top of the range instead of compressing.

| | before | after | original | gate |
|---|---|---|---|---|
| 1-1 torch, bright flame RGB (`torch_g2.py`, frames 270..420) | 196.6 157.5 127.1 | **255.0 238.3 145.2** | 253.2 252.0 159.1 | R >= 240, G >= 235 |
| 1-1 torch, peak grey | 194.7 | 255.0 | 255.0 | - |
| 2-01 door flames, p95 R / p95 G (`door_g2.py`, frames 300..420) | 168 / 71 | **255 / 48** | 247 / 36 | R >= 230, G <= 50 |

- **The torch's G passes, and is still 14 short of the original.** Over the brightest tenth
  of the flame's pixels, R is 255 on 100 % of them in the port. G is at 250 or more on
  **48 %** in the port against **89 %** in the original (p10 210 against 249). So the
  channel that clips is right, and the core where G also clips is smaller. The encoding
  cannot shrink a core. The candidates are the particles: the additive pipeline weights
  by alpha where Ethanon adds `One, One`, and the flame sits 13 units low (plan_port gaps
  J and I, System 2). Not measured further here.
- **The door's G passes by 2, and the HUD is not holding it up.** `door.py`'s box,
  x 996..1107, predates the HUD and overlaps the right pad's quad from x 1100. With the
  box cut at x 1099 the numbers do not move: p95 R 255 and G **48** after, 168 / 71
  before, 247 / 36 in the original. So the table keeps `door.py`'s own box, comparable
  with plan_port's baseline.
- Its red rows now run 377..643 px against the original's 404..642, and the mean red
  excess in the mask is 133.0 against 108.3: the dim tops of the flames now clear the
  script's red > 40 test. Not gated, and the same System 2 particles.
- **Both G margins are thin**: the torch's G by 3.3 and the door's by 2. The captures are
  deterministic, so neither passes by luck. But plan_port System 2's particle changes can
  move either way, so `torch.py` and `door.py` are to be run again there.

**WHOLE FRAME (`compare.py`, frame 420 against `levels/<W-LL>_t8.0.png`).** No level may
lose more than 0.02 of `edge_iou`.

| Level | edge_iou before -> after | change | mean_abs before -> after |
|---|---|---|---|
| 1-01 | 0.3979 -> 0.4375 | +0.0396 | 29.43 -> 26.23 |
| 1-09 | 0.3948 -> 0.4124 | +0.0176 | 27.64 -> 24.27 |
| 1-13 | 0.2112 -> 0.2217 | +0.0105 | 37.49 -> 38.26 |
| 2-05 | 0.2852 -> 0.2782 | **-0.0070** | 30.13 -> 32.22 |
| 2-26 | 0.3268 -> 0.3218 | **-0.0050** | 17.00 -> 18.43 |
| 3-05 | 0.3721 -> 0.3886 | +0.0165 | 20.55 -> 21.04 |
| 4-22 | 0.2502 -> 0.2536 | +0.0034 | 40.25 -> 39.53 |

- 1-13's offset is (0, -10) before and after, the camera difference section 7.0 records;
  `lightgate` still refuses it (exit 2).
- 4-22's phase offset reads (0, -1) after, (0, 0) before. `lightgate` accepts 1 px, and no
  4-22 blocks exist yet (G3 builds them).
- 2-05 is one of the levels where the black ground now shows (below). 2-26 has none, and
  its 0.005 is not attributed.

**PLAN_PORT'S FIFTEEN LEVELS: ONE FAILS.** The design's G2 gate names section 7.0's
levels, and passes. But the design also says G2 *is* plan_port's 1b. 1b's own
non-regression clause is "`edge_iou` on the 15 levels of section 0.3 does not drop by more
than 0.02". The first review ran it; this record did not, until now. Frame 420 of the
sweep, the same `compare.py` and the same `levels/`:

| Level | edge_iou before -> after | change | mean_abs before -> after | bare ground after |
|---|---|---|---|---|
| 1-07 | 0.3890 -> 0.4334 | +0.0444 | 31.04 -> 31.93 | 0 |
| 1-10 | 0.3989 -> 0.4518 | +0.0529 | 29.10 -> 27.55 | 128 px |
| 1-12 | 0.3595 -> 0.3864 | +0.0269 | 29.87 -> 31.43 | 0 |
| 1-17 | 0.3645 -> 0.3894 | +0.0249 | 25.01 -> 23.15 | 0 |
| 1-27 | 0.3636 -> 0.4112 | +0.0476 | 25.97 -> 24.79 | 0 |
| 2-01 | 0.2455 -> 0.2512 | +0.0057 | 34.34 -> 35.84 | 31,810 px |
| 3-23 | 0.3375 -> 0.3653 | +0.0278 | 27.62 -> 27.82 | 0 |
| 4-05 | 0.1605 -> 0.1643 | +0.0038 | 31.29 -> 32.39 | 0 |
| **4-32** | **0.2369 -> 0.1917** | **-0.0452 FAILS** | **37.65 -> 52.35** | **266,448 px (28.9 %)** |
| the seven above | as in the table above | worst -0.0070 | | 2-05 18,485 px |

- Offsets are (0, 0) before and after on every level except 4-05, which is (-80, 0) both.
  plan_port section 0.2 records that as 4-05's camera-start difference.
- **Where 4-32's 0.0452 went (`fix1/ablate.py`).** Pixels in a region were replaced by the
  original's own, in both port frames, and `compare.py`'s metric was scored again. What
  stays of the change lies outside that region. The regions:
  - **B**, the bare ground below: 266,448 px.
  - **D**, where the original draws near-black (grey < 20) outside B: 355,267 px. These are
    4-32's silhouettes. `properties_843` carries `darkest = "1"`, and the port draws those
    pixels at mean grey 64 (1.6 in the original), before this step and after it.

  | replaced | before | after | change |
  |---|---|---|---|
  | nothing | 0.2369 | 0.1917 | -0.0452 |
  | B | 0.2380 | 0.2066 | -0.0314 |
  | D | 0.6541 | 0.6316 | -0.0225 |
  | B and D | 0.6707 | 0.6611 | **-0.0096** |

  - With both replaced, what is left is within the tolerance, so the failure lies in those
    two regions.
  - Replacing D alone recovers more (0.023) than replacing B alone (0.014). Replacing D
    lifts both scores to about 0.65, so these are indicators, not an exact split.
  - The first review measured the same drop two other ways, and this record's
    `fix1/attrib.py` reproduces both:
    - B dilated by 3 px and left out of both edge maps: 0.2315 -> 0.1972;
    - B filled with the before frame's pixels: 0.2037.
  - Within 3 px of B the original has 7,930 edge pixels. The port has 4,164 there before,
    2,908 of them on the original's, and 1,631 after, 879 on the original's.
- **4-22, the other `darkest` level in the gate, does not fail.** It is 55 % silhouette
  too, drawn at grey 65, but it has no bare ground: its change is +0.0034, and -0.0046
  with D replaced.
  - On 4-32 the port's edges inside D went from 56,999 to 61,172, of about 92,000 kept
    per frame. On 4-22 they went down, 56,352 to 55,059.
  - Why D costs 4-32 more is not measured further. One candidate is that the edges the
    bare ground lost were spent inside D instead; the percentile threshold keeps the count.
- **What should close it, not yet shown.** D is what the design's G3 draws: ambient 0.01 on
  a `darkest` level, gated on 4-22 by E = 0 scenery at most 4/255. B needs the sky
  controller, below, which no step of either plan owns yet. Neither belongs to G2.
- **Beyond the fifteen, a screen, not a gate.** `levels/` holds a t8.0 frame for 81 of the
  128 levels, and only 4-32 falls by more than 0.02 among them. The median change
  is +0.017, from -0.0452 (4-32) to +0.0592 (1-23).
  - plan_port section 0.2 checked only 14 library frames for a menu caught in place of
    the level. 4-01 and 2-32, which it found wrong, are not among the 81.
  - The 17 library frames of levels with more than 2,000 bare pixels (below) were looked at
    beside the port's, and all are in-level. The other frames were not.

**THE HUD IS UNCHANGED, AND WHY IT CANNOT BE BYTE-IDENTICAL.** Step 43 said it: the
controls are translucent, so a control's pixel carries the world behind it. At level4
(1-05) frame 143 the md5 goes from `3f204d4a...` to `7f7b98ec...`, and 97.1 % of the
pixels outside the HUD corners changed. So the check is on what display-space blending
keeps invariant, with step 39's own measures, plus one frame where nothing is behind it
(`hud_g2.py`, medals from `out/parity/ui1/saves_gold`).
- **Nothing behind it: level0 frame 1**, both opening blacks whole. **Byte-identical**,
  `5c5cfed20f942d06da1f22fb5835c1dd` before and after, 0 pixels differing.
- **Opaque HUD: level0 frame 90**, the plaque and medal at alpha 1. 858,079 pixels of the
  frame changed. Inside the 39,510 texels the sprites' own alpha calls opaque (at least
  254/255 after a bilinear resize), 10 pixels changed, by 1, all on the mask's one-pixel
  edge. Inside the mask eroded by 2 px: **0 of 37,910**.
- **Translucent controls: high-pass gain** of restart, pause, left pad and right pad (step
  39's method). In display space it is the control's alpha whatever is behind it.

  | frame | before | after | original | largest change |
  |---|---|---|---|---|
  | 1-05 t8.0 (level4, 503) | 0.4738 0.4726 0.4666 0.4688 | 0.4737 0.4732 0.4664 0.4686 | 0.4762 0.4760 0.4766 0.4757 | 0.0006 |
  | 1-15 t8.0 (level14, 505) | 0.4751 0.4742 0.4707 0.4714 | 0.4755 0.4746 0.4709 0.4712 | 0.4797 0.4795 0.4719 0.4734 | 0.0004 |
  | 1-17 t8.0 (level16, 504) | 0.4751 0.4745 0.4707 0.4717 | 0.4755 0.4745 0.4708 0.4716 | 0.4778 0.4872 0.4724 0.4757 | 0.0004 |
  | 3-05 t8.0 (level4b, 506) | 0.4725 0.4711 0.4710 0.4717 | 0.4736 0.4716 0.4712 0.4715 | 0.4707 0.4734 0.4749 0.4761 | 0.0011 |
  | 1-05 t2.0 (level4, 143) | 0.4738 0.4726 0.4743 0.4764 | 0.4737 0.4732 0.4741 0.4763 | 0.4762 0.4760 0.4836 0.4844 | 0.0006 |

  The world's mean grey behind them moved 72.45 -> 73.62 at frame 143 and 91.90 -> 99.49
  at level0 frame 90; the gains moved by at most 0.0011.
- **The plaque while it fades**, 1-05 at t2.0 (frame 143 against 293, step 39's
  regression): apparent alpha **0.4437 -> 0.4448**.
- **The overlay list itself** is a function of `ui.json` and the level's age, and this step
  touches neither. `test_mp_layer`'s HUD checks (rectangles, alphas, textures, order) pass
  unchanged.

**WHAT ELSE MOVED, NOT GATED.**
- **The ground its skies leave bare.** Before this step the engine's procedural sky showed
  wherever no level sprite was drawn, at grey 89. Now the black shows there. Counted as
  pixels that are 0 after and at least 60 on every channel before, at frame 420 of all 128
  levels (`fix1/census.py`).
  - **34 of the 128 levels, 3,659,902 px in all.** 25 levels lose more than 1 % of the
    frame, and 14 lose more than 10 %.
  - An earlier version of this record said "over every capture taken" and listed only 2-01
    and 2-05. The captures then did not include the levels below.
- **Split by what the original draws on those pixels.** 19 of the 34 levels have a library
  frame; the mean is over the same pixels.

  | the original draws | levels | px | largest |
  |---|---|---|---|
  | **a bright sky**, now black | 6 | 373,854 | **1-21** 234,829 (25.5 %, mean 171 132 112, the sunset through the arches); **1-20** 122,794 (13.3 %, 224 175 122); 1-29 5,212; 1-25 4,712; 1-08 3,190; 1-19 3,117 |
  | **the dark blue-grey sky** | 10 | 1,322,324 | **2-02** 336,507 (36.5 %, 62 68 74); **4-32** 266,448 (28.9 %, 60 71 79); **4-20** 216,148 (23.4 %); **2-07** 199,507 (21.6 %); **2-09** 146,699 (15.9 %); 2-03 63,587; 2-29 41,093; 2-01 31,810; 2-05 18,485; 2-08 2,040 |
  | near-black too (every channel under 30) | 3 | 14,786 | 3-18 14,645 (19 23 27), 1-10 128, 1-23 13: black is right or close |
  | no library frame | 15 | 1,948,938 | **2-31** 473,224 (51.3 %); **2-27** 302,138 (32.8 %); **3-30** 218,256 (23.7 %); **2-32** 201,020 (21.8 %); **2-18** 164,633 (17.9 %); **4-31** 147,103 (16.0 %); **2-22** 93,708 (10.2 %); then 2-14, 2-30, 2-23, 2-17, 2-11, 4-27, 2-16, 1-02 |

  - **2-01 (level0a):** 31,810 px, x 1120..1279 and y 0..606. 95 % of them are at x 1171
    or more.
  - **2-05 (level4a):** 18,485 px. 4,667 are in the left 8 px column and 13,783 in the band
    y 602..629 above the floor; 35 are elsewhere, up to x 1099.
  - None in 1-01, 1-09, 1-13, 2-26, 3-05 or 4-22.
  - None in the 19 levels that carry `space_sky` (4-01 to 4-19). The original draws those
    with a different controller, `SpaceSky`.
- **Why: the original pins its sky to the camera, and the port draws it where the level
  put it.** Read from the original's compiled script, in the `tools/asbc` listing of
  `android_game.bin`. The excerpt is kept at `work/g2/fix1/sky_functions.txt`, with the
  full listing's md5.
  - **`Game::preLoop`** (`Game.angelscript`, bytes 112536..114414). A level without
    `Game.spaceBg` builds `StaticSky` with the name `'sky'`, then adds it with the other
    controllers (instructions 148..182 and 281..287).
  - **`StaticSky::StaticSky`** (`Sky.angelscript`, 153461..153922). It collects every
    entity named `sky` and every one named `sky.ent` into `m_skies`, then calls `scaleSky`.
  - **`StaticSky::scaleSky`** (154161..154942), for each sky:
    - it scales it by `GetScreenSize().y / GetSize().y`, so it is as tall as the screen;
    - it takes `m_width` from the scaled width;
    - it reads the entity's `scroll` float, which makes a scrolling sky.
  - **`StaticSky::position`** (154942..155688). A sky that does not scroll gets
    `originalPos = GetScreenSize() * 0.5 + (m_width * t, 0)`. Then
    `SetPositionXY(GetCameraPos() + originalPos + scrollPos)`.
  - **`StaticSky::update`** (155688..156307) calls `position` for every sky on every frame,
    and moves `scrollPos` along for a scrolling one.
  - So the position a level gives its sky is overwritten from the first frame. The sky is
    centred on the screen and as tall as it, which at 16:9 is as wide as it too: 455.1
    units against `dark_sky.png`'s 455.
  - The port places the one 455x256 quad where the level put it, and its camera moves off
    it. The camera offsets below are read from the frames (`fix1/camera_from_sky.py`): a
    display-encoded, unlit sky pixel is its texel, so the offset matching the most texels
    places the camera.
    - **Scrolled right, to the level's far edge:** 2-01 by 57 u (372,193 px matched), and
      4-32 and 1-20 by 313 u (192,196 and 115,544). Their `max` markers are 512 and 768 u
      wide.
    - **Scrolled down, to the level's bottom:** 2-02 by 126 u and 2-05 by 44 u, whose
      `max` markers are 382 and 300 u tall. That is 2-05's band: its sky ends 214 u down
      the view, at 602 px.
    - **2-05's left column** is the sky placed at (230.5, 130), 3 u right of the view's
      edge.
    - **1-21:** no pixel of the frame matches the sky (86 at best). Its `max` is (512, 512),
      so a view held at the bottom starts at y 256, where the sky ends. The arches frame
      nothing.
    - 1-01, as a check, reads (0, 0).
  - This corrects the explanation given here before: that the sky's size and the view's
    width were left, and belonged to plan_port System 6. System 6's content scale (gap D)
    is a separate question.
  - **No step of the lighting design or of plan_port owns this controller.** Before this
    step the grey sky hid it. This step shows it, and the decision is under LEFT FOR THE
    OWNER.
- **Placeholder boxes go dark.** The layer's boxes (bodies without art, markers, zones, a
  carranca's fireball) are lit PBR quads, and a display-encoded scene is expected to draw
  unlit (ARCHITECTURE.md section 5). A fireball on 2-01 went from (149, 116, 80) to
  (112, 55, 21), where the original draws its fire (148, 113, 90). They stand in for art
  not yet drawn.
- **Classes this step does not own**, against `engine_tier1x`, before -> after:
  - 1-01 `LM_E=1` 7.79 -> 3.15, 1-09 `LM_E=1` 7.73 -> 2.41. The rest is the missing
    lightmap (G4).
  - 2-05 `noLM_E<1` 66.59 -> 67.20, 2-26 `LM_E<1` 54.46 -> 58.27, 1-01 `LM_E<1` 34.78 ->
    34.58, 1-09 `LM_E<1` 43.12 -> 43.00. Below full emissive nothing multiplies by the
    ambient yet (G3). Reinhard had been darkening them, which is why two got worse.
  - The halo classes wait for G5.
- **The menu** draws world quads, not the overlay, so it is display-encoded too.
  - Frame 120 of the menu, with `--saves saves_gold`: mean 43.58 -> 42.12, max 188 -> 255,
    and 439 pixels at white where there were none.
  - The original's `screens/main_menu.png` is not the same frame. `compare.py` offsets
    (14, 31) before and (40, 0) after, `edge_iou` 0.08. So nothing is claimed for or
    against it.

**THE SUITE.** `test_mp_layer` **304 -> 366**; its floor (35) is unchanged.
- `TheSceneHoldsDisplayValues`, 62 checks. It starts at level0, at the menu and at level99
  (a start that fails), each on a registry that held no `RenderSettings`. Each time:
  - `DisplayEncoded`, so `decodesColourTextures()` is false;
  - `Color` and `!drawsSky()`;
  - the colour (0, 0, 0), and `SceneClearColor` returns it rather than the linear literal;
  - bloom 0, `quantize None`, exposure 1.
- Also after N to level1 and a retry, and over a context that already held a loaded
  scene's settings (bloom 2, background 0.5), which the layer replaces.
- **Mutation.** With the `insert_or_assign` line replaced by a no-op, the suite ran 326
  checks with **12 failures**: the four starts on an empty registry found no settings,
  and the loaded scene's settings failed 8 checks. The file was restored from a copy
  (`cmp` identical) and everything rebuilt.
- `test_mp_sprites` 155, unchanged.

**STEP 42'S WALK, RETAKEN ON THE NEW KEYS.** `DisplayEncoded` moves every albedo from the
registry's `srgb:` key to its `data:` key, and step 42 walked the pool only on the first.
Not one of the design's G2 gates, and the design's full visit check waits for G3; run
because it is one command. `MagicPortals --visit-levels lightmapped --visit-passes 2`:
**exit 0 in 19.2 s**, validation ACTIVE, 134 visits, 1,460 lightmap sets, peak **96** in
the pool, 0 failures, pass 2's baseline **75 at all 67 visits**, 75 cached and 75 in the
pool after the last release, `Subsystem resources destroyed cleanly`, and no line
containing "error". Those are step 42's numbers, one for one.

**LEFT FOR THE OWNER.** This step's implementation does not depend on the answers, but
whether it ships does.
- **plan_port's fifteen-level clause fails on 4-32** (-0.0452), and the failure lies in
  the silhouettes and the bare ground. The review offered two ways on: waive the clause on
  4-32 until G3 and the sky controller land, or gate 4-32 again at G3. Its advice is to
  hold the step until the owner picks one.
  - The measurements favour waiving: with both regions replaced, 4-32's change is
    -0.0096.
  - It is still the owner's call. The clause is plan_port's, and this record cannot loosen
    it.
- **Black ground on 34 levels until the sky follows the camera.** It covers 51 % of 2-31,
  37 % of 2-02, 29 % of 4-32 and 25 % of 1-21. On 1-21 and 1-20 the original's sunset
  through the arches is replaced by black.
  - The before hid it behind a grey no level draws.
  - Porting `StaticSky` is a system of its own:
    - it needs the `scroll` custom float, which is not read here;
    - it needs the tiling of several skies;
    - its gates must pin the sky in 2-26's `noLM_E=1` blocks, which are all sky.
  - Nothing in G2 needs it. It needs an owner, and a place in the order: before G3, or
    beside it.
- **Placeholder boxes darker** (above) until plan_port System 7's art replaces them. A
  playtest will see them.

**GATES.**

| Gate | Required | Measured |
|---|---|---|
| `lightgate --variant engine_tier1x`, 1-09 `noLM_E=1` | bMAE <= 2.0, abs(bias) <= 2.5 per channel | **0.29**, bias -0.1 / -0.1 / -0.1 (before 12.25, -9.8 / -9.3 / -12.2) |
| the same, 2-26 `noLM_E=1` | bMAE <= 2.0, abs(bias) <= 2.5 per channel | **0.91**, bias -0.1 / -0.1 / -0.1 (before 2.86, -0.1 / -2.2 / -3.6) |
| `torch.py`, bright flame | R >= 240, G >= 235 | **R 255.0, G 238.3** (before 196.6 / 157.5; original 253.2 / 252.0) |
| `door.py` on 2-01 | p95 R >= 230, p95 G <= 50 | **R 255, G 48** (before 168 / 71; original 247 / 36) |
| `compare.py` `edge_iou`, section 7.0 levels | none falls by more than 0.02 | worst **-0.0070** (2-05), then -0.0050 (2-26); five rose, by up to +0.0396 (1-01) |
| `compare.py` `edge_iou`, plan_port 0.3's fifteen levels (1b's own non-regression clause, not in the design's G2 list) | none falls by more than 0.02 | **FAILS on 4-32: -0.0452** (0.2369 -> 0.1917). The other fourteen pass, worst -0.0070 (2-05). With the bare ground and 4-32's silhouettes both replaced by the original's pixels: -0.0096. Left for the owner |
| HUD, nothing behind it (level0 frame 1) | byte-identical | `5c5cfed2...` before and after |
| HUD, opaque (level0 frame 90) | unchanged | 0 of 37,910 plaque and medal pixels 2 px inside their edge; 10 edge pixels by 1 |
| HUD, translucent (restart, pause, pads; five frames incl. level4 frame 143) | unchanged | high-pass gain within **0.0011**; plaque apparent alpha 0.4437 -> 0.4448 |
| `test_mp_layer` | the layer's `RenderSettings` are DisplayEncoded / black / 0 | 366 checks, 0 failures (mutation: 12 failures) |
| final binary, first round | the gate captures are what the last build draws | spot check: 1-01, 1-09, 2-26, 2-01 at frame 420, level4 frame 143 and level0 frame 1, retaken after that round's last rebuild, md5-identical to the after set |
| final binary, after the review's rebuild | the gate captures are what the last build draws | `work/g2/fix1_final/`, 38 frames: all 26 `capture.sh` frames (the HUD's eight and the menu included), the eight of the fifteen levels it does not take, 1-20 and 1-21, all md5-identical to the after sets; and MainScene and Wolf Brigade, md5-identical to step 43's. `gates.sh` and `compare.py` on them print every number above again. `hud_g2.py` was not run again: its frames are identical, so its numbers stand |
| MainScene and Wolf Brigade, `--fixed-step --frames 120` (no engine file changed) | byte-identical | `1e24c2a30f6f22dc2bb01b6038bd1af9` and `d9e7b8fe5e8b0b2f162b0195e5d5ba31`, step 43's |
| `--visit-levels lightmapped --visit-passes 2` (not a G2 gate) | step 42's walk still holds on `data:` keys | exit 0; 1,460 sets, peak 96, baseline 75; 0 failures |
| bare ground (not a gate) | counted | 34 of 128 levels, 3,659,902 px; 14 levels over 10 % of the frame; cause read from `StaticSky` (camera-pinned sky) |
| validation | silent | every capture's log says `Vulkan validation layers: ACTIVE`; the only `[Validation]` lines are the loader's two missing Steam layer manifests, as in step 43's captures; every run exited 0 |

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning.
  - The first build compiled the layer, `main.cpp` and `LevelVisit.cpp` (the header
    changed) and `test_mp_layer.cpp`.
  - The second, after a comment-only edit in the layer, compiled the layer again.
  - The third followed the mutation's revert. The mutation build itself printed C4551 on
    the no-op line it had put in, which is gone.
- **ctest.** **113 of 113** pass, none refused ("Not Run" 0): after the second build, and
  again after the third.
- **Smart App Control** refused nothing in the first round: `MagicPortals.exe` was
  relinked three times, and every capture exited 0.
- **After the review, no source changed.**
  - The four translation units were compiled again from scratch: the layer, `main.cpp`,
    `LevelVisit.cpp` and `test_mp_layer.cpp`. No warning.
  - Smart App Control then refused the new `test_mp_layer.exe`, so ctest showed it Not Run.
    It went on refusing through 19 relinks of the same object; each gave a new md5, and
    neither running it directly nor through ctest helped.
  - Compiling the object again before each link got through on the fifth such round. The rebuilt `MagicPortals.exe` was refused the same way, and ran after five
    more links, two with `main.cpp` recompiled.
  - Every such round printed no warning.
- **Then ctest: 113 of 113 pass, Not Run 0.** `test_mp_layer` run directly: 366 checks,
  0 failures. `ninja: no work to do` before it.
- **No other document's table moves.** No engine file changed and no suite was added, and
  neither README.md nor ARCHITECTURE.md lists `test_mp_layer`'s checks.

## Step 45 - every sprite at its ambient, the dark levels dark, and the lightmaps handed back (built)

The lighting design's step G3 (the remake's `out/parity/specs/lighting/design_port.md`,
section 7.1). **The port now draws every level sprite the way the original's first pass
does: its colour times min(1, ambient + emissive), per channel.** The ambient is the level
file's, or darkest's 0.01 where the level sets `darkest`, or a lit torch's (0.1, 0.1,
0.25); the emissive is the sprite's node's, or its `.ent`'s for what no level places. A
level's lightmaps are handed back to the texture registry when it goes. **No engine file
changes.** No lightmap, light or halo is drawn yet (G4, G5).

**Three gates fail, and none of them is the multiply. None is waived here.**
- 2-05's `noLM_E<1` bias is the carranca's fireball placeholder box, in the carranca's mouth
  on the one frame the gate reads. On six other frames from 330 to 510 the same 349 blocks
  pass.
- 1-09's `LM_E=1` bias is the art tier, on sprites this step multiplies by exactly one.
- Step 44's torch confirmation (G >= 235) now reads 232.1: the arches under the flame are
  dimmed, and their lightmap and halo are not drawn yet.

Each is a row of THE GATES, with its attribution and the step where it must pass again.
- **The first review** found all three, and two suite pins that could pass having checked
  nothing. The pins were fixed, and this record was corrected; no game or engine source
  changed in answer.
- **The second review** measured the same three failures and found two things more:
  - two sentences of this record said more than was measured (1-09's `LM_E=1` frames, and
    4-22's offset);
  - no suite pinned a timed crystal's fade, which this step moved into the tint.
  - Both sentences were reworded to what was measured, a `test_mp_layer` case was added
    (it fails when the fade is dropped), and 2-05's attribution gained the frames above.
  - Again no game or engine source changed.

**WHY IT IS ONE MULTIPLY.** The original's pass 1 is `vc = min(1, A + E) * C`, and the
texel times that, for every sprite whether or not it applies light and whatever its blend
(`ETHRenderEntity.cpp:113-117`, the remake's `engine_math.md` section 0). The design's fit
of the original's pixels holds it to **0.28** of 255 over 1,359 blocks of sprites without a
lightmap, against **45.30** for drawing them full bright (`fit.md` section 4), and finds the
fitted gain equal to the scene ambient to within 0.02 on every lightmapped entity (section
5). Since step 44 the scene holds display values, so the multiply lands on the bytes, as the
original's did.
- Over the 128 levels, **1,250 of the 2,883 level sprites** have min(1, A + E) below one on
  some channel, in 114 levels; **137** of them, in the twelve `darkest` levels, go to 0.01.
  The rest are emissive 1 and do not change.

**THE DATA (game: `data/lighting.json`, new; `art.json`, `torch.json`, `launchers.json`).**
- **`lighting.json`** holds only what this step reads, each with its decode:
  - `darkest_ambient` (0.01, 0.01, 0.01), moved from `art.json`'s `darkest` block:
    `SetAmbientLight(DARKEST_AMBIENT_LIGHT)`, built at `Game.angelscript` bytes
    306783..306841. It replaces the file's ambient: 4-22's file says 0.5.
  - `torch_lit_ambient` (0.1, 0.1, 0.25), moved from `torch.json`'s `ambient.lit`: what
    `ETHCallback_light_off`'s `delete` branch sets.
  - The design's section 5.9 file also carries the normal maps' green, a halo scale, script
    emissive overrides and `rgb565`. Those belong to G4 to G6 and are not in the file.
- **`art.json`**: each picture gains `emissive`, its `.ent`'s `<EmissiveColor>`, read from
  the extracted files: `portal.ent` (1, 1, 1), `projectile.ent` **(0.6, 0.6, 1)**,
  `dark_mage.ent` (1, 1, 1), `beholder.ent` (1, 1, 1), `beholder_spike.ent` (1, 1, 1). The
  `darkest` block keeps its decode and points to `lighting.json`.
- **`launchers.json`**: `rolling_stone.ent` gains `emissive` **(0.5, 0.5, 0.5)**, which a
  thrown stone and the beholder's rock (the same throwable) are drawn with.
- **`torch.json`**: the `ambient` block points to `lighting.json`, and `_not_built` no
  longer lists `SetAmbientLight`.
- **Strict, like the rest.** `Art::LoadRules` refuses a picture without `emissive`, or with
  one that is not three numbers none below zero; `Launchers::LoadRules` the same for a
  throwable. A default would draw the player at the ambient alone, which on a dark level is
  black, and nobody decoded that.

**THE AMBIENT (game sim: `sim/Lighting`, `sim/Game`).** Renderer-free, so the simulation's
suites reach it without a layer.
- `Lighting::Rules` and `LoadRules`: both lights required, each three numbers from 0 to 1.
- `Lighting::Ambient(rules, fileAmbient, darkest, torch)`: the original's events in their
  order, so the last one decides.
  - It starts at the file's ambient, or darkest's when the level sets `darkest`.
  - A torch lit now gives the torch's.
  - A torch put out gives darkest's on any level: `ETHCallback_fire_signal` sets
    `DARKEST_AMBIENT_LIGHT`, never the file's back.
  - **"Lit now" is any light's own `lit`, not `Torch::State::lit`**, which counts torches
    ever lit and never comes down. Read the counter and a torch put out stays at (0.1, 0.1,
    0.25); `test_mp_lighting` and `test_mp_torch` both pin that trap.
  - Every level places at most one torch (`torch.json`'s census), so "lit now, else put out
    before, else the start" is exactly the last event.
  - **One frame early.** The original sets the lit ambient on the frame after the shot, in
    the `delete` branch; the port has it from the tick the torch is shot.
- `Lighting::AmbientTerm(ambient, emissive)`: min(1, A + E) per channel.
- `Game::Data` carries `Lighting::Rules lighting`, read by `LoadData` beside `torch.json`.
  Nothing in `Game::Level` or the tick reads it: it is presentation, and the state hash is
  untouched. Minion sight, when it is built, compares the same `Lighting::Ambient`
  (`Game.hpp`'s `darkest` comment says so).

**THE LAYER (`MagicPortalsLayer.{hpp,cpp}`).**
- **Read once with the level's data.** `Lighting::Read(m_data.scene, m_paths.art, ...)`
  where `Game::LoadData` runs, so a retry reuses it. A level whose lighting will not read
  is played unlit, as a level whose art will not read is played as boxes, and says why
  (`LightingError()`, and a log line). Without the art it does not read: step 41's reader
  checks the sprite and lightmap files.
- **The layer keeps C apart (design section 5.2).** Each level sprite carries its node's
  colour and emissive; a timed crystal's fade multiplies its alpha; the beholder's (1, hp /
  max, hp / max) is kept as `m_beholderColour`. No sync function writes a sprite's
  `albedoColor` any more.
- **`syncLighting`**, the last thing `syncDrawables` does, on the tick and at a load, so a
  quad made this tick is coloured before it is drawn. It computes the ambient and hands
  every sprite to one function, `tint`: `albedoColor = (C.rgb * min(1, A + E), C.a)`,
  written only when it changes.
  - The level's sprites, the dark dragon's dropped platform (its template's look), the
    player, the placed portals' halos, the shot, the beholder, pictured spikes, and thrown
    stones and rocks, each with its own emissive.
  - **Added sprites are dimmed too**: the fit's model computes `vc` before the blend, and so
    does the original.
  - **Folded into the albedo colour**, which the engine's unlit path already multiplies.
    E2 gives the engine a 2D path that holds the ambient apart; moving it there is a change
    to `tint` alone.
- **Not coloured, each for a reason:**
  - **the particles**: Ethanon multiplies a system by min(1, luminance + ambient) only when
    it is alpha-blended (`ETHParticleManager.cpp:382-389`), and all 102 of the game's are
    added (`Particles.hpp`), so `updateEmitters` is unchanged by the rule;
  - **the boxes**, PBR placeholders that stand for things, which at 0.01 would vanish;
  - the menu, the medal screens and the HUD, which are not in a level.
- **Not done: the script's emissive (design section 5.8).** The 2-05 door and crates, and
  the player on `ignore_emissive` levels, are drawn with their files' emissive. The design
  keeps those as override rows in `lighting.json`, one of them a `_guess`; no step of section
  7.1 names them, and G3's text does not. So:
  - 4-22's player is drawn whole, where the original draws a black silhouette with red eyes;
  - its doorway frame (`window01.png`, emissive 0.7) is drawn at 0.71, where the original's
    reads 3.3, 3.9 and 4.6 of 255 over its 4 blocks: about 0.01 of the texel. That is a
    second level carrying `ignore_emissive = "1"` whose door renders at emissive 0, beside
    2-05's. **New evidence for the rule section 5.8 declined; not adopted here.**
  - 2-05's crates render at 0.15 of their texel, where the original's read about 0.33.
  - Neither 1-09 nor 1-13 carries `ignore_emissive` or `door_emissive`, so the player gate
    below is not touched by any of it.

**THE LIGHTMAPS HANDED BACK.** The design's section 6 names this step's half of the budget:
each lightmapped sprite will hold a texture and a material set of its own, 730 across the
game, and the registry keeps both until told.
- The layer holds the level's lightmap paths (`HeldLightmaps()`, sorted) from the moment it
  loads, and `unloadLevel` gives each to `TextureRegistry::Invalidate`, through the pointer
  the app publishes in the registry's context.
  - A suite's bare registry has no such pointer, so the call is skipped there and the list is
    still cleared. `LightmapsHandedBack()` counts the paths given back either way, which is
    how a suite sees a retry keep them.
- **A retry keeps them**: `loadLevel` passes `keepLightmaps` when it reloads the level it is
  showing, which draws the same lightmaps again at once, and a retry does not touch the disk
  (`level_manager.gd:5-13`). The next level, the menu, a chapter's end, a refused start and
  detaching all give them back.
- **Nothing is dropped yet.** Nothing acquires a lightmap until E2 and G4, and `Invalidate`
  on a path never acquired finds no key, drops nothing and bumps no generation
  (`TextureRegistry.cpp`, `Invalidate`). So it is harmless now, as the design says.
- **Detaching is safe**: `~SupersonicApp` clears the layers before it resets the renderer.

**THE SUITES.**
- **`test_mp_layer` 366 -> 418.**
  - `EverySpriteIsDrawnAtItsAmbient`, on level0:
    - the ambient (0.35, 0.3, 0.35);
    - both arches at it, and the torch's own sprite (emissive 0);
    - the three platforms, the door (emissive 0.7, so 1) and the four static portals'
      added halos whole;
    - a static body's box still the layer's grey;
    - the player whole;
    - nine lightmaps held, in level0's directory, sorted, `add696.png` among them;
    - a retry holding the same list, handing none back, and dimming the rebuilt arches again;
    - N holding level1's nine and none of level0's, with level0's nine handed back;
    - Escape to the grid holding none, 18 handed back, and dimming nothing.
  - `AShotIsDimmedAndADarkLevelIsDark`:
    - level1's shot at **(0.95, 0.9, 1)**;
    - level21c at ambient 0.01, its pilars, wall and platforms at 0.01, its three crystals
      and the lift's door whole, and no lightmap held.
    - **Only the shot waits on the original's extracted assets** (`projectile.png` is an
      `Art.hpp` image). level21c's scenery is the converter's art beside the levels, so its
      pins run wherever the levels do.
      - Before the review the whole test returned when the original's assets were missing,
        design pins included.
      - Shown by one build with level21c's `Paths::original` pointed at a directory that does
        not exist: **410 checks, 0 failures** (the suite's count before the second review's
        case). Against a normal run its log gains six lines,
        each naming that directory: five HUD images and the caption font.
      - The suite was put back byte for byte (md5 `306c8a13...` before and after) and built
        again (`work/g3/fix1/mutation_no_original_level21c.log`).
  - `WithoutTheArtTheLevelIsBoxes` also finds the lighting reported, nothing dimmed and
    nothing held.
  - `ATimedCrystalFadesInItsAlphaAlone` (8 checks, added after the second review), on level14
    (1-15):
    - Before this step `syncSprites` wrote a timed crystal's fade straight into its albedo.
      Now the fade is a factor on C's alpha, which `syncLighting` multiplies in.
    - With more than 2 s left, all five crystals are (1, 1, 1, 1): each is emissive 1 under
      the file's (0.25, 0.25, 0.4).
    - `crystal_861` goes at 12 s. Ticked into its last 2 s, to a tick whose fade (computed as
      `syncSprites` computes it, from that tick's `leftS`) is below 0.8, exactly one crystal
      is (1, 1, 1, fade) and four are whole.
    - **Mutation, one build, not kept:** `syncLighting` passing the colour without the fade
      makes both of the last two checks fail (418 checks, 2 failures). The layer was copied
      first and put back byte for byte (md5 `d23fb19e...` before and after), and everything
      was built again.
- **`test_mp_torch` 92** (11 check statements added, each run once). `theAmbientFollowsTheTorch`, on level25c:
  - that the level has a torch and a wall, checked before the test returns without them.
    Before the review it returned with no check (the 91 the first draft of this record
    gave);
  - darkest's 0.01 over the file's 0.5;
  - (0.1, 0.1, 0.25) once lit, and still after the wall has gone;
  - 0.01 again when the signal is shot, while `lit` stays at 1.
- **`test_mp_start` 689 -> 726.** Each of the twelve `darkest` levels starts at darkest's
  0.01, and its file says otherwise; level0 starts at its file's own.
- **`test_mp_lighting` 415 -> 444.** `TheScriptsAmbientIsReadAndFollowsTheTorch`, which
  runs without the levels:
  - `lighting.json`'s two lights;
  - `Ambient` through start, darkest, lit, put out on a darkest and on an ordinary level,
    and lit again;
  - `AmbientTerm` per channel and clipped;
  - seven refusals, each for its message.
- **`test_mp_sprites` 155 -> 166**: the five pictures' emissives pinned; a picture with no
  emissive, two numbers, or a negative one refused.
- **`test_mp_launchers` 95 -> 98**: rolling_stone.ent's 0.5, on each of the three levels.
- **Mutations, for one build each, not kept.** Three edits were put in by a script that
  copied the two files first, and the copies were put back byte for byte afterwards.
  - **All three at once**:
    - `Ambient` reading `torch.lit > 0`;
    - the `syncLighting` call taken out;
    - `loadLevel` never keeping the lightmaps.
  - Results:
    - **`test_mp_lighting` failed 2** (a torch put out, on a dark and on an ordinary level);
    - **`test_mp_torch` failed 1** ("out: 0.01 again");
    - **`test_mp_layer` failed 10** (level0's ambient, arches, torch sprite and retried arches;
      the shot; level21c's ambient and its four sceneries).
  - **Nothing failed for the retry.** The held list is rebuilt from the same level either way,
    so the list alone could not tell a kept lightmap from one handed back and taken again.
    `LightmapsHandedBack()` was added for that.
  - **The retry edit on its own**, against the new checks: **`test_mp_layer` failed 3** (0, 9
    and 18 handed back).
  - The source was restored, every changed file recompiled, and every number in this record
    retaken or checked on that build (below).

**THE GATES.** Captures: `out/parity/specs/lighting/work/g3/{before,after}/` by `capture.sh`;
gates by `gates.sh`, `gate_extra.py` and `player_alpha.py`; the review's attributions by
`fix1_extra.py`. After each review every frame was retaken on the final build
(`work/g3/fix1_final/`, then `work/g3/fix2_final/`) and every script run again on it: each
number in the Measured column below came out again to the digit printed. The Before column
is step 44's frames, not retaken.
- **The before is step 44's**, and was checked rather than assumed: 1-01 at frame 420
  captured on this step's starting build is `a904609a...`, step 44's `fix1_final` md5. The
  seven frames were copied from `work/g2/fix1_final/`.

**4-22's BLOCKS, BUILT (`work/g3/l422_blocks.py`).** The fit built none for 4-22. The
design's recipe, with the fit's own scripts unchanged:
- **The ambient first.** The fit's parser reads the file's `<Ambient>`, 0.5, and nothing of
  `darkest`. So `parse_esc` is wrapped to give 0.01 for level21c before anything renders:
  the camera search, the refine and every block's `vc` see the level as the original draws
  it.
- **The camera.**
  - `camfind2.py`: coarse (0.0, -0.09) at NCC 0.892, fine **(0.089, 0.0)** at 0.726.
    - The NCC is low because the frame is mostly black: 51.3 % of the original's pixels are
      at most 4 of 255 on every channel (`fix1_extra.py`). It is not the validation; the MAE
      minimum below is, as the design says.
  - `camrefine.py`'s check: the static-pixel MAE over its grid around the rounded camera,
    with the found camera added. The minimum is **0.922 at (0, 0)**; the found camera
    scores 0.975 and the next grid points 1.02 and 1.07. So 4-22's world camera is **(0, 0)**.
    The review re-ran it with integer offsets up to 2 units added: the same minimum, and
    2.9 to 3.25 at 2 units.
  - Step 44's (0, -1) is `compare.py`'s whole-frame offset, port against original, not the
    camera. `compare.py` reads (0, -1) on step 44's frame and (0, 0) on this step's.
    `lightgate`'s own offset search reads (0, 0) on both, over the same 630 `noLM_E<1`
    blocks.
- **The blocks** (`tmp/fit/L4-22_blocks.json`, written where `lightgate` reads, beside the
  fit's five; no other file there touched): **1,608**, of which **630** are `noLM_E<1` and
  978 `noLM_E=1`.
  - The 630 are seven entities at emissive 0 (126 pilar, 230 wall, 142 + 10 platform, 95
    block, 7 + 16 single stone), plus the doorway frame's 4 at emissive 0.7.
  - The 978 are the sky's 953 and the lift door's 25.
  - The original reads **0.0** on every emissive-0 block: 0.01 of any texel is below one
    5-bit step.
  - **So the port's 0.83 there, and the +0.8 bias below, are the 5/6/5 framebuffer**, which
    the port has not got until G6 (design decision 7). Against `engine_no565` the 630
    `noLM_E<1` blocks read 0.22 with a bias of +0.07 +0.08 +0.09, and the 626 emissive-0
    ones 0.22 against a model mean of 0.74 (`fix1_extra.py`).

| Gate | Required | Before | Measured |
|---|---|---|---|
| 2-05 `noLM_E<1` against `engine_tier1x` | bMAE <= 2.0 | 67.20 (293 blocks) | **1.62** (349 blocks) |
| the same | abs(bias) <= 1.0 per channel | +69.3 +66.3 +66.0 | **+2.4 +1.1 +0.4: FAILS on R and G** |
| the same, without the 13 blocks within 2 px of a box pixel (`gate_extra.py`: colour within 2 of (113, 55, 21), grown by 2 px) | (attribution, not the gate) | 68.09, +69.1 +67.3 +67.9 | **0.41, +0.1 +0.1 +0.1** (336 blocks). A plain colour mask (within 6, not grown) touches 9 blocks and leaves 340 at **0.42, +0.09 +0.05 +0.07** (`fix1_extra.py`) |
| the same, all 349 blocks, at frames 330, 360, 390, 450, 480 and 510 (`work/g3/fix2/phase/`) | (attribution, not the gate) | - | **0.42, +0.1 +0.1 +0.1** at every one of the six |
| 1-01 `LM_E<1` against `lm_omitted` | abs(bias) <= 2.5 | +42.8 +45.3 +46.9 | **+0.5 +0.6 +1.5** |
| 1-01 `LM_E=1` against `lm_omitted` | abs(bias) <= 2.5 | +0.2 +0.2 +0.2 | **+0.2 +0.2 +0.2** |
| 1-09 `LM_E<1` against `lm_omitted` | abs(bias) <= 2.5 | +46.8 +45.6 +43.8 | **+1.0 +0.6 +1.8** |
| 1-09 `LM_E=1` against `lm_omitted` | abs(bias) <= 2.5 | +3.2 +3.2 +2.8 | **+3.2 +3.2 +2.8: FAILS**, unchanged by construction |
| the same against `lm_omitted` drawn with 1x art | (attribution, not the gate) | 0.11, 0.0 0.0 0.0 | **0.11, 0.0 0.0 0.0** |
| 2-26 `LM_E<1` against `lm_omitted` | abs(bias) <= 2.5 | +81.6 +78.7 +76.1 | **+0.4 +0.3 +0.2** |
| 2-26 `LM_E=1` | abs(bias) <= 2.5 | no such blocks | no such blocks |
| player gain, 1-09 (`player_alpha.py`) | each channel in [0.95, 1.05] | 1.0001 0.9996 1.0000 | **1.0001 0.9996 1.0000** |
| player gain, 1-13 | each channel in [0.95, 1.05] | - | **0.9997 0.9998 0.9995** |
| 4-22, the emissive-0 scenery blocks, mean port value | <= 4 of 255 | 75.02 | **0.83** (626 blocks, largest block 1.39) |
| 4-22 `noLM_E<1` against `engine_tier1x` | bMAE <= 2.0 | 74.61 | **0.82**, bias +0.8 +0.8 +0.8 |
| the same against `engine_no565` | (attribution, not the gate) | - | **0.22**, bias +0.07 +0.08 +0.09: the +0.8 is the 5/6/5 framebuffer, G6's |
| `test_mp_layer`, `test_mp_torch`, `test_mp_start` pins | as the design lists | - | pass: 418, 92, 726 checks, 0 failures |
| `torch.py` bright flame, level0 frames 270..420 (step 44's G2 confirmation, re-read) | R >= 240, G >= 235 | 255.0, 238.3 | **255.0, 232.1: FAILS on G**. B 145.2 -> 127.5, peak grey 255.0 -> 243.0. Must pass again at G4 and G5 |
| `door.py` on 2-01 (step 44's G2 confirmation, re-read) | p95 R >= 230, p95 G <= 50 | 255, 48 | **255, 48** |
| `compare.py` `edge_iou`, section 7.0's seven levels (G2's clause, re-read) | none falls by more than 0.02 | step 44's | worst **-0.0054** (3-05); the other six rose |

**2-05's block count moved with the frame, not with the rule.** `lightgate` reads the
port's offset as **(0, 1)** on step 44's frame and **(0, 0)** on this step's (rerun on
`before/`: (0, 1), 293 blocks, 67.20). So 56 more of the fit's clean blocks fall inside the
compared frame, and the two columns are not the same block set.
- **4-22's did not move.** `lightgate` reads (0, 0) on both frames, 630 blocks both times,
  so its two columns are the same blocks. The (0, -1) -> (0, 0) above is `compare.py`'s,
  rerun on `before/` for this record.

**Three rows fail, and none is this step's multiply. None is waived here.**
- **2-05's bias is the carranca's fireball box.** At frame 420 a fireball sits in the right
  carranca's mouth, and the port draws it as a flat (113, 55, 21) box.
  - `fireball.ent` has no sprite, so the box is its picture (`syncTurrets`), and no lighting
    step draws it.
  - **The original draws no such thing there at t8.0.** Its mouth is dark
    (`work/g3/fix1/2-05_carranca_orig_vs_port.png`), and the carranca's 59 blocks read as
    the model does: original against `engine`, -0.13 -0.09 -0.15.
  - Every block the box touches is the carranca's (entity 827). Its bias against
    `engine_tier1x` is +13.6 +6.2 +1.8, near the box's own ratio.
  - The only other entity off by more than 0.5 against the original is the bar at entity
    910 (`bar4_contrast.png`): +3.7 +3.6 +3.4 on the 3 blocks at x 880 at the frame's top
    edge.
    - Against `engine_tier1x` it is -0.3 +0.1 +0.3. The 1x model itself sits +3.8 +3.4 +2.9
      above `engine` on those blocks, so that is the art tier (System 6), not this step.
  - Without the box's blocks the class is at 0.41 with +0.1 on every channel; with them bMAE
    passes and bias does not.
  - **Dimming the box was not done.** It would take those blocks from about 108 to about 1
    and pass the row for a cosmetic reason: the port would still draw a box where the
    original draws none. It would also dim a stand-in for an added particle system, which
    the original does not dim (design section 5.6).
  - **It is the frame, not the class** (second review, `work/g3/fix2/phase/`). 2-05 was
    captured on the same binary at frames 330, 360, 390, 450, 480 and 510, and `lightgate`
    run on each against `engine_tier1x`.
    - Every one reads offset (0, 0), all **349** blocks, **0.42**, bias **+0.1 +0.1 +0.1**:
      inside both of the row's limits with no block taken out. Entity 827 reads +0.1 0.0 +0.1
      against the original.
    - Frame 420 is the only one of the seven with the box: 1,980 px at x 990..1034, y
      529..572. On the other six no pixel of the frame is within 2 of (113, 55, 21), and that
      region reads 6.2/6.6/7.3, where the original's `t8.0` reads 6.1/6.4/6.6.
    - The frame at 420 is md5-identical to the gate's (`5345f959...`).
  - **The original also draws a fireball at that mouth, at another moment.** Bright orange
    pixels (R > 180, G > 100) in the library frames:
    - `2-05_h6.3.png`: x 597..996, reaching the mouth, whose box region reads 25/10/8;
    - `2-05_t8.0.png`: x 597..805 only, over the crates, and the mouth reads 6.1/6.4/6.6.
    - So the fixed pair, port frame 420 against `t8.0`, catches the right carranca's fire at
      two different moments of its cycle. The fit chose its blocks clear of particles on the
      original's frame, not on the port's.
    - A capture moment is game state, as `lightgate` already treats a camera offset.
  - **So System 2 is not expected to close this row alone** (reasoning, not measured: no port
    draws `fireball.ent`'s particles yet). Particles put where the box is on frame 420 would
    still cover those blocks, which the fit chose clear of anything on the original's frame.
    What is measured is the other six frames. The row closes when it is read at a moment where
    neither frame draws a fireball over entity 827, or with the blocks under a port-drawn
    moving object taken out. Either is a change to the design's gate, and that is the owner's.
- **1-09's `LM_E=1` bias is the art tier, and G3 cannot move it.** Those sprites are
  emissive 1, so min(1, A + E) is 1: this step multiplies them by exactly one.
  - Over their 290 blocks, **one pixel** differs between step 44's frame and this step's, by
    1 of 255, in block (608, 416) of entity 590 (`work/g3/fix2/lme1_diff.py`). The class's
    bias is +3.232 +3.186 +2.835 on both frames, to the thousandth.
  - `lm_omitted` exists only at the best tier (hd, fullhd), and the port draws 1x.
  - The design assumed a tier moves detail and not the block mean. On this class it moves
    the mean by 3: the fit's own section 4 has the engine at -1.4 -0.6 -1.0 against the
    original, and `engine_tier1x` at +0.9 +1.7 +1.0.
  - **Attributed by rendering `lm_omitted` with 1x art** (`work/g3/extra_variant.py`): the
    fit's `render.py`, at the camera in the fit's blocks file, into a copy of the file under
    `work/g3/fit_extra/`.
    - The renderer was first held to the file: `engine_tier1x` rendered the same way equals
      the stored prediction on every block of 1-01, 1-09 and 2-26, worst difference 0.0.
    - Against that variant the class is 0.11 with a bias of 0.0 on every channel, before and
      after.
  - The same variant puts every lightmapped class of the three levels within 0.8 of the
    model: 1-01 `LM_E<1` 0.59 (0.0 0.0 -0.2), `LM_E=1` 0.85 (+0.8 +0.7 +0.8); 1-09 `LM_E<1`
    0.63 (+0.2 0.0 +0.1); 2-26 `LM_E<1` 0.44 (+0.1 0.0 0.0).
  - Against the original it closes at plan_port's System 6.
- **Step 44's torch confirmation fails on G.** `torch.py` over level0's frames 270 to 420:
  - bright flame **(255.0, 232.1, 127.5)** against step 44's (255.0, 238.3, 145.2) and the
    original's (253.2, 252.0, 159.1). Peak grey 255.0 -> 243.0.
  - G2's confirmation asks G >= 235: it passed at step 44 and now reads 2.9 below.
  - **Cause:** the flame's particles are added over the arches and wall, which are
    emissive 0 and now drawn at 0.35. In the original those pixels also carry the lightmap
    and the halo, which put an orange glow under the flame.
  - In the flame's box 58,343 of 61,600 pixels changed. Among the 1,232 brightest before,
    866 to 965 did, depending on the frame, and their mean G fell from 186.7 to 168.5 at
    frame 270. No particle changed (`updateEmitters` is untouched).
  - **It stays a gate.** G4 (the lightmap under the arches) and G5 (the torch's halo) each
    re-read `torch.py`, and neither passes while G is below 235.
- **The owner's choice**, as step 44 left 4-32:
  - 2-05's bias: read the row at a frame where neither picture has a fireball over the
    carranca, or take out the blocks under port-drawn moving objects, or waive it now on the
    attribution above. System 2 alone is not expected to close it.
  - 1-09's `LM_E=1` bias: gate it again at System 6, or amend the design's G3 rows to
    compare against an `lm_omitted_tier1x` variant like `work/g3/fit_extra/`'s. The design's
    threshold for the "bias only" rows rests on an assumption this measurement contradicts,
    and the design is the owner's to change, not this record's.
  - The torch's G: G4 and G5 carry it; whether this step may land below it meanwhile.

**THE PLAYER'S GAIN (`work/g3/player_alpha.py`).**
- **T is what the port samples**: the original's `entities/magic_portals_hd.png`, 160 x 224,
  cells of 40 x 56. That is the file `art.json` names and the layer draws, not
  `entities/hd/`, which the original draws.
- A cell is drawn at 2.8125 px per texel and sampled bilinearly across the whole sheet.
- **Located by masked NCC** over all 16 cells: cell 4 on both levels, NCC 0.9992 and 0.9989,
  refined to 1/8 px (0.9997 and 1.0000).
- **The gain** is `sum(actual * T) / sum(T * T)` per channel over the opaque pixels, eroded
  by 3 px: 5,037 pixels on 1-09 and 5,028 on 1-13, residual MAE 0.4 to 0.6 and 0.24.
- **1-13's camera offset does not matter here**, since the player is located in the frame.
  So this is the one G3 gate 1-13 can take while `lightgate` refuses it.
- **The measure can see a gain.** The same 1-09 frame scaled by (0.3, 0.3, 0.4) reads
  0.3002, 0.2995 and 0.4000.
- The player is emissive 1, so the gate holds the fold to leaving it whole, which is what
  the original's (1.02, 0.98, 1.00) and (1.03, 1.00, 1.01) say.

**WHAT ELSE MOVED, NOT GATED.**
- **Classes this step does not own**, port against `engine_tier1x`, before -> after:
  - 1-01 `LM_E<1` 34.58 -> **9.59** (bias -17.9 -8.7 -2.0), 1-09 `LM_E<1` 43.00 -> **1.72**,
    2-26 `LM_E<1` 58.27 -> **20.46** (-36.8 -17.7 -6.9). The remainder is the missing
    lightmap: the design expected about 38/255 low on R at 2-26 until G4.
  - The E = 1 classes read the same to the hundredth: 1-01 `LM_E=1` 3.15, 1-09 `LM_E=1`
    2.41 and `noLM_E=1` 0.29, 2-26 `noLM_E=1` 0.91, 4-22 `noLM_E=1` 0.91.
  - The halo classes are darker, as the lightmap and halo they lack sat under a sprite now
    dimmed: 1-01 `LM_E<1_halo` 37.63 -> 42.22, 1-09 `LM_E<1_halo` 39.78 -> 40.92. G4 and G5.
- **Whole frame (`compare.py`, frame 420 against `levels/<W-LL>_t8.0.png`):**

| Level | edge_iou before -> after | mean_abs before -> after |
|---|---|---|
| 1-01 | 0.4375 -> 0.4450 | 26.23 -> 23.49 |
| 1-09 | 0.4124 -> 0.4220 | 24.27 -> 9.42 |
| 1-13 | 0.2217 -> 0.2307 | 38.26 -> 17.62 |
| 2-05 | 0.2782 -> 0.3235 | 32.22 -> 12.35 |
| 2-26 | 0.3218 -> 0.3322 | 18.43 -> 13.69 |
| 3-05 | 0.3886 -> 0.3832 | 21.04 -> 9.27 |
| 4-22 | 0.2536 -> **0.6088** | 39.53 -> **4.95** |

- **All 128 levels at frame 420** (`capture.sh after sweep`, scored by `screen.py` against step
  44's `work/g2/fix1/after`):
  - every run exited 0, validation ACTIVE, no VUID, and no level drawn unlit: all 128 levels'
    lighting reads;
  - **113 frames changed**. The 15 that did not are 4-03 to 4-18 but 4-14, whose placed
    sprites are all emissive 1. The one exception is 4-13's carranca, whose frame is unchanged,
    so it is not in view at frame 420;
  - the seven gate levels' sweep frames are md5-identical to `after/`.
- **Against the 81 library frames: no level loses more than 0.02 of `edge_iou`.** The change
  runs from -0.0178 (3-07) to +0.4883 (4-23), median +0.0069. `mean_abs` fell on 64 of the
  81 and rose on 3: 1-05 by 6.4, 1-18 by 1.1, 1-22 by 0.7.
  - 1-05 was looked at: its original lights a wall with a blue lamp's lightmap and halo, which
    the port does not draw yet, over a wall now dimmed to the ambient.
- **Step 44's open question, 4-32.** plan_port's fifteen-level clause failed at G2 on 4-32
  (0.2369 at step 43 -> 0.1917), and step 44 put about 0.023 of that in the silhouettes this
  step draws. **After this step 4-32 is at 0.4847**, +0.2478 on step 43 and +0.2930 on step 44.
  Its black ground (the sky controller) is still there.
  - The fifteen, step 44 -> now:
    - chapter 1: 1-07 0.4334 -> 0.4493, 1-10 0.4518 -> 0.4707, 1-12 0.3864 -> 0.3804,
      1-17 0.3894 -> 0.3823, 1-27 0.4112 -> 0.4210;
    - chapters 2 to 4: 2-01 0.2512 -> 0.3732, 3-23 0.3653 -> 0.3631, 4-05 0.1643 ->
      0.1643 (unchanged bytes), 4-32 0.1917 -> 0.4847;
    - and the seven in the table above.
  - The worst change among them is 1-17's -0.0071.
- **The torch's flame and `door.py`** are step 44's G2 confirmations, so they are rows of
  THE GATES above, the torch's failing on G. The first draft of this record kept them here,
  out of the table.
- **Unchanged bytes:**
  - the menu at frame 120 (`934232ed...`) and level0's frame 1, both blacks whole
    (`5c5cfed2...`), step 44's md5s: neither draws a level sprite;
  - MainScene and Wolf Brigade at `--fixed-step --frames 120`: `1e24c2a3...` and
    `d9e7b8fe...`, step 43's, with no engine file changed.

**STEP 42'S WALK, WITH THE LAYER HANDING BACK.** `MagicPortals --visit-levels lightmapped
--visit-passes 2`:
- **exit 0 in 18.8 s**, validation ACTIVE, 134 visits, 1,460 lightmap sets, peak **96**, 0
  failures;
- pass 2's baseline **75 at all 67 visits**, 75 cached and 75 in the pool after the last
  release, and no line containing "error".
- Step 42's numbers, one for one.
- The probe still acquires the lightmaps itself and releases them before it opens the next
  level, so the layer's own `Invalidate` finds them gone. What this shows is that the
  layer's hand-back on every unload breaks nothing in the walk, validation included.
- The design's full E0 check, with the probe's acquisition replaced by the layer's own,
  waits for E2 and G4.

**LEFT FOR LATER STEPS AND FOR THE OWNER.**
- The three failing rows above: the owner's call, with the step that closes each.
- The script's emissive (design section 5.8): the 4-22 player and door, the 2-05 door and
  crates. No step of section 7.1 builds it, and this record adds 4-22's door as evidence.
- The torch flame's G, a gate again at G4 and G5.
- Step 44's open items stand: the sky controller (black ground on 34 levels), the placeholder
  boxes, and 1-13's camera.
- **To be checked, not changed here:** `Game.cpp` hands `DarkDragon::Tick` `level.torch.lit > 0`
  as "the torch is lit". `lit` is cumulative, the trap `Lighting::Ambient` avoids by reading
  each light's own flag. Whether the dragon should arm on "lit now" rather than "ever lit"
  wants the script read; if it should, that is a state-hash change for a later step.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning.
  - The final build was taken after the mutations were restored, with every changed source,
    header and suite touched: 35 translation units compiled (`work/g3/build_final.log`).
  - Every build before it printed no warning either.
- **ctest: 113 of 113 pass, "Not Run" 0.** Resolved as follows.
  - **First build.** `test_mp_fire` and `test_mp_sounds` were refused ("Not Run"), and each
    ran after one delete and relink.
  - **The mutation build.** `test_mp_start`, then `test_mp_layer`, were refused. The latter ran
    after one relink.
  - **The final build.** `test_mp_portal`, `test_mp_camera`, `test_mp_timed`, `test_mp_torch`,
    `test_mp_zerog` and `test_mp_fields` were refused.
    - After one relink, `test_mp_timed`, `test_mp_torch` and `test_mp_zerog` were still
      refused; after two, `test_mp_torch`.
    - It ran on the sixth.
    - Every relink printed no warning, and a full `ctest` then ran 113 of 113 with none
      refused.
- **The final binary draws the gate frames.** `MagicPortals.exe`, relinked by the final build,
  was not refused.
  - The seven gate levels at frame 420, retaken on it, are md5-identical to `after/`
    (`work/g3/final/`).
  - **Then every frame `capture.sh` takes, retaken on it** (`work/g3/final2/`): all 21 are
    md5-identical to `after/`. That is the seven gate levels, level0's torch frames 270 to
    420, 2-01's door frames 300 to 420, the menu at 120 (`934232ed...`), level0's frame 1
    (`5c5cfed2...`), MainScene (`1e24c2a3...`) and Wolf Brigade (`d9e7b8fe...`). So the
    unchanged-bytes, torch and door numbers above hold on the final binary, measured rather
    than inferred.
  - Step 42's walk on it: exit 0, 134 visits, 1,460 sets, peak 96, pass 2 at 75 throughout, 0
    failures, validation ACTIVE, no line containing "error".
- **After the review.** Only `test_mp_layer.cpp` and `test_mp_torch.cpp` changed, and this
  record.
  - **The final build** touched every changed source, header and suite: 35 translation units
    compiled, no warning (`work/g3/build_fix1_final.log`), then `ninja: no work to do`.
  - **ctest first refused 13** ("Not Run"): `test_mp_tscn`, `_levels`, `_geometry`,
    `_scores`, `_start`, `_ghost`, `_bounce`, `_darkdragon`, `_keys`, `_shot`, `_boss`,
    `_sprites` and `_lighting`.
    - Each round deleted the refused executables and relinked them: 6 were still refused
      after the first, 2 after the second (`test_mp_levels`, `test_mp_lighting`), none after
      the third.
    - No relink printed a warning.
  - **Then a full ctest: 113 of 113 pass, Not Run 0.** Run directly: `test_mp_torch` 92,
    `test_mp_layer` 410 (before the second review's case), `test_mp_start` 726,
    `test_mp_lighting` 444, `test_mp_sprites` 166 and `test_mp_launchers` 98 checks, 0
    failures each.
  - **`MagicPortals.exe`, relinked by that build, was not refused.** All 21 `capture.sh`
    frames retaken on it (`work/g3/fix1_final/`) are md5-identical to `after/`, MainScene
    and Wolf Brigade included. `gates.sh`, `gate_extra.py`, `player_alpha.py` and
    `fix1_extra.py` on them print the numbers above.
  - Step 42's walk on it: exit 0, 134 visits, 1,460 sets, peak 96, pass 2's baseline 75 at
    all 67 visits, 75 and 75 after the last release, 0 failures, validation ACTIVE, no line
    containing "error".
- **After the second review.** Only `test_mp_layer.cpp` changed, and this record.
  - **Build.** The suite alone first: one translation unit compiled, `MagicPortals.exe`'s md5
    unchanged. Then the mutation above, and the layer put back byte for byte.
  - **The final build** touched every changed source, header and suite: 35 translation units
    compiled and 40 links, no line containing "warning" or "error"
    (`work/g3/fix2/build_fix2_touched.log`), then `ninja: no work to do`.
  - **ctest first refused 6** ("Not Run"): `test_mp_tscn`, `_scores`, `_statics`, `_movers`,
    `_hazards` and `_sprites`.
    - On the next run `test_mp_scores` ran and the other five were refused again. They were
      deleted and relinked once, with no warning, and none was refused after.
  - **Then a full ctest: 113 of 113 pass, Not Run 0** (`work/g3/fix2/ctest_final.log`). Run
    directly: `test_mp_layer` **418**, `test_mp_torch` 92, `test_mp_start` 726,
    `test_mp_lighting` 444, `test_mp_sprites` 166 and `test_mp_launchers` 98 checks, 0
    failures and no skip line in any.
  - **`MagicPortals.exe`, relinked by that build (md5 `52645a59...`), was not refused.**
    - All 21 `capture.sh` frames retaken on it (`work/g3/fix2_final/`) are md5-identical to
      `after/` and to `fix1_final/`, MainScene (`1e24c2a3...`) and Wolf Brigade (`d9e7b8fe...`)
      included.
    - `gates.sh`, `gate_extra.py`, `player_alpha.py` and `fix1_extra.py` on them write the
      same files as on `fix1_final/`, apart from the tag in a path and a final newline.
  - Step 42's walk on it: exit 0 in 19 s, 134 visits, 1,460 sets, peak 96, pass 2's baseline
    75 at all 67 visits, 75 and 75 after the last release, 0 failures, validation ACTIVE, no
    line containing "error".
- **No other document's table moves.** No engine file changed and no suite was added, and
  neither README.md nor ARCHITECTURE.md lists the Magic Portals suites' checks.
- **Neither repository was committed to.** The remake's working tree is clean: everything it
  gained is under the gitignored `out/parity/specs/lighting/work/g3/`, plus
  `tmp/fit/L4-22_blocks.json` with its mask and overlay.

## Step 46 - a fourth map in every material, a sprite's record in the fields the unlit path never read, and a third blend (built)

The lighting design's step E2 (the remake's `out/parity/specs/lighting/design_port.md`,
section 7.1: sections 4.2, 4.3, 4.5 and the base of 4.6). **Three engine capabilities, all
off by default**, that G4 will switch on for the lightmaps:
- an **additive overlay map**, the fourth binding of every material set, 1x1 black when a
  material names none;
- a **2D sprite path** in `shader.frag`, `clamp(texel * tint * ambient + overlay * strength,
  0, 1)`, whose numbers ride in the per-draw fields the unlit path never read, so the record
  stays 128 bytes;
- a **premultiplied blend**, a third blended pipeline beside mix and add.

**Nothing turns them on, and nothing drew differently.** Wolf Brigade, MainScene and every
port frame the gate takes are byte-identical before and after; so are all 128 levels at frame
420. What the new path draws was proved on a scene built for it, below. The game gains no
lighting; its only change is `LevelVisit.cpp` passing the fourth map the engine now asks for.

**THE OVERLAY (engine: `Components.hpp`, `VulkanPipeline`, `TextureRegistry`,
`MaterialSetLedger.hpp`, `RenderSystem`).**
- `MaterialComponent::overlayTexturePath`, and `RenderableComponent::overlayTextureID`,
  resolved in `SyncResources` beside the other three.
  - **Colour, decoded exactly when the albedo is** (`srgb = decodesColourTextures()`): a
    baked light is authored in its picture's space. In the port's `DisplayEncoded` scene it
    is the byte in the file, added to the encoded value, which is what the design's decisions
    1 and 3 take from the fit (`fit.md` sections 0 and 4).
  - An unnamed or unreadable overlay is **black**. A checkerboard added over a sprite would
    be a worse way to say a file is missing than the sprite unlit.
  - The path is mixed into `ResourceSignature`; its colour space is already there (step 43).
- `VulkanPipeline::kMaterialBindingCount` 3 -> 4, and `kOverlayBinding` = 3. The layout loop,
  the pool size, the cache key and the per-binding writes all follow the constant, as
  `TextureRegistry.cpp` said they would; the comment there now says it happened.
- **`TextureRegistry::GetBlackTexture`**, a 1x1 (0, 0, 0, 255) **data** texture.
  - Uploaded **after** the checkerboard, so ids 0, 1 and 2, which `RenderableComponent`'s
    literals name, did not move. Its own literal is 4.
  - **Protected from `Invalidate` and `ReplaceRGBA`** like every built-in. The port makes that
    urgent: a lightmap that fails to load caches black under its own path, and G3's unload
    invalidates that path every time the level goes.
  - `fallbackSet()`'s key names it. A brace-initialised array of four given three ids
    zero-fills the fourth, and id 0 is the white albedo: an overlay of white.
- **`AcquireMaterialSet(albedo, normal, orm, overlay)`**, not defaulted.
  - Each slot falls back to its **own** neutral, now in one pure function,
    `MaterialSets::ResolveKey`: checkerboard, flat normal, neutral ORM, black.
- **Who binds what for the fourth slot:**
  - the opaque and blended passes and the entity shadow caster: the entity's overlay;
  - **a mesh section**, and a section's shadow caster: **black**. A file's surface names
    no overlay (`MeshMaterial` has none), and the entity's maps are not what a
    multi-surface mesh draws with. The design is silent here;
  - the particles and the screen overlay: black.
- **Not on `MaterialAsset`** (`MaterialLibrary`, `MaterialSystem` untouched): an overlay is
  one surface's own bake, and a shared asset carrying one would paint it on every user,
  the argument `uvScale`'s comment already makes.
- **Beyond the design's list, and why.**
  - `AssetRepointer` repoints the overlay with the other maps; a scene saves it with a Guid.
  - **The asset watcher does not watch it, deliberately**, with the reason at the watch
    site. A 2D game names hundreds (730 lightmaps here), `Watch` never forgets a path,
    and every watched path is a stat on every frame. One pass of Python `os.stat` over the
    730 files in `out/assets/lightmaps` took **6.63, 5.43 and 4.55 ms** (warm, three runs):
    a third of a 60 Hz frame. So an edited overlay file is read again only when its path is
    invalidated, as a level unload does. Not measured with the engine's own
    `fs::last_write_time`.
  - `InspectorPanel`: an "Overlay Map" path field, and the blend combo gains its third
    word. Its two-entry table would have shown a premultiplied material as Alpha, and one
    click would have rewritten it.

**THE 2D RECORD (engine: `RenderSystem::ApplySprite2D`, `PushConstantData`, `Components.hpp`).**
`MaterialComponent::Sprite2DLight`, as design section 4.2 wrote it: `enabled`, `ambient`,
`height`, `lightMask`, `normalYDown`, `overlayStrength`. Meaningful only with `unlit`.
`ApplySprite2D` writes it over the fields the unlit path never read:

| Field | PBR / plain unlit | with `kSprite2D` |
|---|---|---|
| `albedoColor.rgb` | tint | tint x ambient |
| `albedoColor.a` | alpha factor | alpha factor |
| `material` | roughness, metallic, ao, cutoff | overlay strength, 0, 0, cutoff |
| `emissive` | emission, occlusion strength | tint without the ambient, lighting height |
| `flags` bits 1, 2, 3 | free | `kSprite2D`, `kNormalYDown`, `kPremultiplied` |
| `flags` bits 20..27 | free | light mask (`PackLightMask`, beside `PackUvSlot`) |

- **A static function the draw loop calls last**, after the material branch and after the
  surface-override branch, both of which write `albedoColor`, `material` and `emissive`.
  The design wrote it inline in `buildPushConstants`; out here a suite can read it.
- **Every `static_assert` on the record is unchanged**: 128 bytes, the same offsets.
- The mask sits above the UV slot's twelve bits and four bits short of the sign bit.
- **The height, the tint without ambient, the normal switch and the mask are carried and
  read by nothing.** They are for the light term (E3). `shader.frag` declares
  `FLAG_NORMAL_Y_DOWN` and `LIGHT_MASK_SHIFT` so the suite can hold them to the C++ now.
- **Outside the state hash.** `StateHash` reads no `MaterialComponent` (checked), so nothing
  here can move a replay.
- `MaterialComponent::unlit`'s comment promised a value above 1 reaches the bright pass. The
  2D path clamps, as a fixed-point target clamps one draw. The comment now says so.

**THE PREMULTIPLIED BLEND (engine: `VulkanPipeline`, `VulkanRenderer`, `RenderSystem`).**
- `MaterialComponent::BlendMode` gains `Premultiplied`.
- `VulkanPipelineOptions::additive` becomes `BlendEquation { Mix, Add, Premultiplied }`.
  `ColorBlendFor(Premultiplied)` is `One, OneMinusSrcAlpha` for colour and alpha; Mix and
  Add keep their factors, which the suites check.
- `VulkanRenderer` builds `m_premultipliedPipeline` beside the additive one, and
  `RenderSystem::Render` takes it.
- `TransparentDraw::additive` and `BlendRun::additive` become `BlendEquation blend`.
  `RenderSystem::EquationFor` is the one table from a material's blend to a pipeline's.
  Runs are still cut from the sorted list, now wherever the equation changes.
- A transparent `Premultiplied` material sets `kPremultiplied` on any path. Every exit of
  `shader.frag` then multiplies its colour by its alpha: the 2D path, the plain unlit exit
  and the PBR exit. An opaque material's blend is ignored, as the gather ignores it.

**THE SHADER (`shader.frag`, regenerated `frag.spv`).**
- Set 1 binding 3, `overlayMap`; the three switches and the mask shift.
- **`shadeSprite2D`**: the texel masked to 0 where its alpha is 0, times the vertex colour;
  `base = clamp(texel * albedoColor.rgb + overlay * material.x, 0, 1)`; premultiplied when
  asked. **The overlay is sampled through the material's UV transform**, the rule
  `MaterialComponent`'s comment states for every map. The remake's `data_inventory.md` finds no
  lightmapped sprite with a sprite cut other than (1, 1), so for the port the two readings are
  the same pixels.
- **The light loop is not here, and neither is binding 12.** The design allows it to land
  with an empty buffer or in E3. Declaring a storage buffer the layout has not got is a
  layout mismatch, and the buffer, the scene-set binding and the pool count are E3's
  (section 4.4). Only the base is built.
- **The plain unlit exit** computes `alpha` once and writes `vec4(albedo, alpha)`: the same
  two operations, in the same order, as before. The premultiply is under the flag.
- `frag.spv` was rebuilt by the `Shaders` target with the SDK's glslc (step S0), and a direct
  `glslc shader.frag` gives the same bytes.

**THE CODEC (`ComponentCodec`).**
- `"OverlayTexture"`, through `writeAssetRef`, **only when named**, so it gets a Guid.
- `"Sprite2D": { "Enabled", "Ambient", "Height", "LightMask", "NormalYDown",
  "OverlayStrength" }`, **only when the block differs from the default**, each field read
  with its own default. A block saying only `"Enabled": true` reads ambient and strength
  as 1, not 0.
- `"Blend"` has three words. Any other word reads as Alpha, as before for any word but
  `"Additive"`.
- A material that never used them saves to the bytes it saved before (checked in the suite).

**PROVED ON A SCENE BUILT FOR IT (`work/e2/experiment/`, `check.py`).** No suite can build the
pipelines, the fourth descriptor write, the black set or the shader, so a scene was written
that uses each. It was captured by `SupersonicEngine --scene sprite2d.scene --window 1280x720
--fixed-step --frames 120 --screenshot` (the editor viewport, 744 x 336).
- The scene: MainScene's camera, `DisplayEncoded`, a black `Color` background, and fourteen
  unlit quads. The two overlay images are generated 8 x 8 PNGs of one colour, under `out/`.
- **Exit 0, "Clean exit with validation active", no VUID.** That covers every set naming
  black, the overlay sets, and a premultiplied run between mixed ones.
- Each quad is read as the most common colour in a window at its centre. The editor grid draws
  thin lines over the lower row.

| Quad | Predicted (x 255) | Measured |
|---|---|---|
| plain unlit, tint (0.5, 0.25, 0.75) | 127.5 63.75 191.25 | **128 64 191** (750 of 750 px) |
| 2D, tint 0.5, ambient (0.5, 1, 0.25), **no overlay: the black set** | 63.75 127.5 31.88 | **64 128 32** |
| 2D, tint 0.5, ambient 0.5, overlay (40, 80, 120) | 103.75 143.75 183.75 | **104 144 184** |
| the same, overlay strength 0.5 | 83.75 103.75 123.75 | **84 104 124** |
| 2D, tint 1, ambient 1, overlay 200: **the clamp** | 255 255 255 | **255 255 255** |
| overlay named, `sprite2D` off: **not sampled** | 127.5 127.5 127.5 | **128 128 128** |
| **Premultiplied** (1, 0, 0, a 0.5) over an opaque 0.8 | 229.5 102 102 | **229 102 102** (404 of 540) |
| **Alpha**, the same | 229.5 102 102 | **229 102 102** (422 of 540) |
| Premultiplied 2D, a 0.5, ambient 0.5, overlay, over 0.8 | 185.75 205.75 225.75 | **186 206 226** |
| Alpha 2D, the same, over a backing that reads 0.4 | 134.75 154.75 174.75 | **135 155 175** |

- **A premultiplied draw that adds nothing equals the alpha draw** on the same destination,
  to the byte.
- **Found, not caused by this step, not investigated.** Some plain unlit opaque quads of tint 0.8
  in these scenes read **102, half of 204**. It happens with nothing new in the scene.
  - **Only in the lower row.** Every dark quad is at y = -0.6, across the ground plane the
    editor grid draws on. The upper row at y = 2.2 read its predicted value in every scene
    (`p_row_high.scene`: 204); the same row moved down (`p_row_z0.scene`) reads 102.
  - **But not by position alone.** One quad alone at x = -4 or at x = 2 is dark; of four in
    a row, the right two are, and the one at x = -4 is not.
  - **The first candidate is the grid.** ARCHITECTURE.md says `grid.frag`'s pre-linearised
    colours are not converted by `DisplayEncoded`. A grid alone does not explain the
    dependence on which quads are present.
  - **The pre-E2 engine draws the same bytes.** `SupersonicEngine` was built from a
    `git archive` of `e5cb31d` in the session scratchpad (`work/e2/before_build.bat`).
    Run from that tree, so it read its own shaders, it captured `v_onlyB.scene` (four
    such quads) and `p_one_left.scene` (one) **md5-identical** to this step's build: 74f1f35f... and
    31be1fe6....
  - **The sprite rows are outside it.** All six Q rows are in the upper row. F1 to F3's
    backings read 204, as predicted. Only F4's backing is dark, and its row above is read
    against the 0.4 that backing actually shows.
  - Whether the editor, the grid or the renderer does it is left open; no Magic Portals
    frame shows it.

**THE SUITES.**
- **`test_materials` 235 -> 326** (floor 235 -> 320). Ten cases added, and the ledger cases
  now take the layout's own key shape. The floor is six below the count because the new shader
  case returns early when `shader.frag` cannot be opened, skipping its six later checks. Its
  first check fails in that case, so the suite still fails:
  - the set has four bindings, the overlay third;
  - `ResolveKey`: every slot naming nothing gets its own neutral in binding order, an overlay
    past the end is black and leaves the other three alone, and the last id is a texture while
    one past it is not;
  - `TakeNaming` searches the fourth binding: a set naming a dead id only as its overlay is
    handed back;
  - `ColorBlendFor(Premultiplied)` is `One, OneMinusSrcAlpha` with alpha alike, and Mix and Add
    keep theirs;
  - the four switches, the UV slot and the mask share the flags word, each packing leaving the
    others and the sign bit alone;
  - **the shader's copies**: `FLAG_SPRITE2D`, `FLAG_NORMAL_Y_DOWN`, `FLAG_PREMULTIPLIED`,
    `LIGHT_MASK_SHIFT` and binding 3's `overlayMap`, read out of `shader.frag`;
  - `ApplySprite2D`:
    - an unlit material without `sprite2D` leaves every byte of its record (`memcmp`), and so
      does a lit one that sets it;
    - an enabled sprite writes tint x ambient, the tint alone with the height, the strength
      with the cutoff kept, the mask and the normal switch, and keeps the UV slot, the model,
      the skin and the probe;
  - `kPremultiplied` only on a transparent premultiplied material, lit or not, and no switch
    for the other two blends;
  - the overlay, the 2D block and `"Premultiplied"` round-trip; a plain material writes neither
    key and reads the defaults; a block with only `Enabled` reads ambient and strength 1;
  - an unknown blend word loads as Alpha.
  - **Mutation:** with `FLAG_PREMULTIPLIED = 1 << 4` in `shader.frag`, and no rebuild (the suite
    reads the source at run time), it failed on exactly that check, 1 of 326
    (`test_materials.cpp:1374`). The file was restored from a copy, md5 `6ba432a6...` before and
    after.
- **`test_draworder` 85 -> 104** (floor 61 -> 80). The blend cases now take the enum. Added:
  - a premultiplied sprite gathered between two mixed panes sorts between them and is its own
    run of three;
  - all three equations in one frame: runs cut at every change and nowhere else, covering every
    draw once;
  - `EquationFor`'s table, and that an unset draw and a default material mix;
  - `ColorBlendFor` without `blendEnable` is off whichever equation it names.
- **`test_resourcesync` 26 -> 32** (floor 23 -> 29). Gaining an overlay, swapping it, and an
  unchanged one are three answers. The same file as the ORM map and as the overlay is two. The
  overlay's colour space moves the signature, and a sprite's light settings, which select no
  texture, do not.
- **Unchanged:** `test_renderplan` 127, `test_serialize` 445, `test_sprite` 66,
  `test_screenoverlay` 46, `test_shadowcache` 82, `test_mp_layer` 418, and all 18 `test_wb_*`
  pass. `test_assetdatabase` reads 110; `AssetRepointer` changed and its suite did not, and its
  count before was not taken.
- **What no suite covers, and why.** No suite can build a `TextureRegistry` or a pipeline: they
  need a device (steps 20 and 42 say the same). Only a run reaches:
  - the fourth descriptor write, and the black texture's upload;
  - `isBuiltIn` refusing to free black;
  - the premultiplied pipeline's creation;
  - the shader's arithmetic.
  The scene above covers the draws; the refusal to free black is not exercised by anything.
- **Carried, not changed.** `AcquireMaterialSet` throws when a key names a texture whose image
  `Invalidate` has moved out. That was already true of the other three bindings. The overlay
  is meant for per-level lightmaps, so it is the slot most likely to meet it. It cannot today:
  `Invalidate` bumps the generation, so `SyncResources` re-resolves every entity before the
  next `Render`.

**GATES.** Captures by `work/e2/capture.sh` (a copy of G3's with its folder changed), before on
this step's starting build and after on its final one.
- **The before is G3's final**: all 21 before-frames are md5-identical to
  `work/g3/fix2_final/`.
- **The after is on the third build, and again on the fourth.** `after/` and the sweep were
  captured on the third build. Two comment-only edits followed (`Components.hpp`,
  `tests/CMakeLists.txt`), and the fourth build relinked every executable. All 21 gate frames
  were captured again on it, into `work/e2/final/`: **21 of 21 md5-identical** to `after/`.

| Gate | Required | Measured |
|---|---|---|
| `SupersonicEngine --scene assets/scenes/MainScene.scene --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical before/after | `1e24c2a30f6f22dc2bb01b6038bd1af9` before and after (steps 42 to 45's); `Clean exit with validation active` |
| `WolfBrigade --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical before/after | `d9e7b8fe5e8b0b2f162b0195e5d5ba31` before and after (steps 42 to 45's) |
| port gate levels, `--fixed-step --frames 420` | unchanged (nothing uses the overlay) | identical before and after: 1-01 `f62ca770...`, 1-09 `a34abfab...`, 1-13 `c17b4dc0...`, 2-05 `5345f959...`, 2-26 `b335d981...`, 3-05 `0bf26811...`, 4-22 `8916b072...` |
| the torch and door frames (level0 f270..390, level0a f300..420) | unchanged | 10 of 10 identical |
| screen overlay (step 39), nothing behind it: level0 frame 1 | bit-exact | `5c5cfed20f942d06da1f22fb5835c1dd` before and after |
| the menu, frame 120 | unchanged | `934232ed534adae966587863179378e9` before and after |
| scene encoding (step 43) under the port's `DisplayEncoded` | bit-exact | every port frame above is `DisplayEncoded` and identical; the HUD drawn over it identical |
| all 128 levels at frame 420 (not required) | unchanged | **128 / 128 md5-identical** to `work/g3/after/sweep/`; the seven gate levels' sweep frames equal this step's `after/` frames; every run exit 0, validation ACTIVE in all 128 logs, no VUID |
| validation | silent | the 19 port logs say `Vulkan validation layers: ACTIVE`, MainScene's `Clean exit with validation active` (Wolf Brigade's log prints neither, before or after); no `VUID` or "Validation Error" in any of the 21 logs, in `after/` or in `final/`; every run exit 0 |
| 2D path, overlay, clamp, premultiplied pipeline (experiment) | as predicted, validation silent | every quad within 0.5 of its prediction; premultiplied = alpha to the byte; exit 0, clean exit with validation |
| `--visit-levels lightmapped --visit-passes 2` (`LevelVisit.cpp` changed) | step 42's walk still holds | exit 0 in 19.1 s; 134 visits, 1,460 sets, peak **96**; pass 1 22 -> 75, pass 2 **75 at all 67 visits**; 75 / 75 after the last release; 5 waiting at quit; `Subsystem resources destroyed cleanly`; no line containing "error" |
| build | zero warnings | 0 in every build: the first (it stopped on `test_draworder`'s C2039, the suite not yet moved to the enum), the full one (170 steps), the floor's, the fourth after the comment edits (266 steps), and every relink |
| ctest | all pass; `test_materials`, `test_resourcesync`, `test_draworder`, `test_renderplan`, `test_serialize`, `test_sprite`, all `test_wb_*` named | **113 / 113**, Not Run 0, on the fourth build; 326, 32, 104, 127, 445, 66; the 18 `test_wb_*` pass |

`lightgate.py` was not run: every port frame is byte-identical to G3's, so every number it would
print is the one step 45 printed.

**LEFT FOR LATER STEPS AND FOR THE OWNER.**
- **E3** adds the light term: binding 12, `GatherLights2D`, the loop that reads the height, the
  tint without ambient, the mask and `kNormalYDown`.
- **G4** switches the port's G3 fold to `sprite2D.ambient`, names the lightmaps and makes the
  alpha sprites premultiplied. Design section 10's torch confirmation (G >= 235) must pass there.
- **Step 42's walk still acquires its own sets.** Run it again at G4, with the layer's
  acquisition in place of the probe's: the design's full E0 check.
- **The watcher gap is a choice to confirm.** An overlay is not hot-reloaded, to keep 730 stats
  a frame out of the port. A platform file-change backend would lift it.
- **Found, not caused here:** some plain unlit quads in the editor scene draw at half their tint,
  identically on the pre-E2 build. Uninvestigated; the editor grid is the first candidate.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning in any build.
  - The first compiled every changed engine file, `LevelVisit.cpp` and the shader (90
    translation units), and stopped on `test_draworder`, not yet moved to the enum.
  - The second built the rest: 170 steps, 51 translation units, and the three executables.
  - The third rebuilt `test_materials` after its floor was raised. It also recompiled
    `shader.frag`, touched by the mutation's restore, into the same `frag.spv`.
  - The fourth followed two comment-only edits after the sweep: `RenderableComponent`'s count
    of the ids `SyncResources` overwrites, and what `tests/CMakeLists.txt` says
    `test_materials` reads. `Components.hpp` is included everywhere, so it recompiled 140
    translation units and relinked every executable (266 steps).
- **ctest.** 113 of 113, Not Run 0, three times: after the second, third and fourth builds.
  The named suites' counts on the fourth are the ones above.
- **Smart App Control.** Three rounds of refusals ("Not Run"), each resolved by deleting and
  relinking the refused executables.
  - **Second build.** 18 suites were refused: `test_sat`, `test_convexhull`, `test_wb_combat`,
    `test_wb_audio`, `test_husk_scenarios`, `test_mp_levels`, `test_mp_zerog`,
    `test_mp_sprites`, `test_mp_sounds`, `test_assetdatabase`, `test_spotlight`,
    `test_shadowcache`, `test_replay`, `test_physics`, `test_scripts`, `test_hierarchy`,
    `test_blending` and `test_skeletal`.
    - All 95 others passed in that run.
    - On the first relink `test_convexhull`, `test_husk_scenarios`, `test_mp_zerog` and
      `test_skeletal` were refused again. All four passed on the second.
  - **Third build.** `test_materials` was refused, again after one relink, and ran on the
    second.
  - **Fourth build.** 33 suites were refused, all 80 others passed. Relinked, 5 were refused
    again (`test_husk_foundation`, `test_mp_darkdragon`, `test_mp_sprites`, `test_wb_controls`,
    `test_wb_waves`), then 2 (`test_mp_sprites`, `test_wb_waves`), then none.
    - `MagicPortals.exe` and `WolfBrigade.exe` were refused too: exit 126, "Permission
      denied", on the first `final/` capture. `WolfBrigade.exe` ran after one relink,
      `MagicPortals.exe` after two. `final/` was captured after that.
  - `SupersonicEngine.exe` was never refused.
  - The pre-E2 `SupersonicEngine.exe` built in the scratchpad was not refused either.
- **Neither repository was committed to.** The remake's working tree is clean: everything this
  step wrote is under the gitignored `out/parity/specs/lighting/work/e2/`. The pre-E2 tree and
  its build are in the session scratchpad.

## Step 47 - the lightmaps drawn, the ambient handed to the engine, and every mixed sprite premultiplied (built)

The lighting design's step G4 (the remake's `out/parity/specs/lighting/design_port.md`,
section 7.1, with sections 5.2 and 10). **The port now adds each static, light-applying
sprite's baked lightmap, the level's `add<id>.png`, over its colour times its ambient**,
through the engine's 2D sprite path that step 46 built. The ambient that step 45 folded
into the albedo colour now rides in the sprite's own record, and every mixed sprite of a
lit level is drawn premultiplied. **No engine file changes.** No light, normal map or halo is
drawn yet (E3, G5).

**Every gate passes.**
- On 1-01, 1-09 and 2-26, every halo-free class is within **0.85** of the fit's
  `engine_tier1x` model, the worst bias **+0.8**: the design asked 2.0 and 2.5.
- The classes without a lightmap read what they read at G3, to the hundredth.
- The torch confirmation that failed at G3 passes: bright flame G **244.8**, asked 235.
- The design's level walk now watches the layer's own lightmap sets, not a probe's:
  1,460 over two passes, peak 91 of 1024, pool equal to cache at every visit.

**WHAT CHANGED (game: `MagicPortalsLayer.{hpp,cpp}`).** Step 45 said moving the fold to the
engine would be a change to `tint()` alone, and it is, with one field to feed it.
- **`tint(quad, colour, emissive, lightmap)`**, the one place a level sprite's look is
  written, on a lit level:
  - `albedoColor = C`, the node's colour with a timed crystal's fade on its alpha;
  - `sprite2D.enabled`, `sprite2D.ambient = min(1, A + E)` (`Lighting::AmbientTerm`),
    strength left at 1;
  - `overlayTexturePath` = the sprite's lightmap, or empty;
  - a `BlendMode::Alpha` material made `Premultiplied`. `Additive` stays: no blendMode-1
    instance applies light (design section 4.5).
  - Each field written only when it changes, so a still level writes nothing a tick and
    `SyncResources`' signature does not churn.
  - **On a level whose lighting did not read**: the plain unlit path, `albedoColor = C`, no
    2D record, no overlay, `Alpha`. That is what step 45's `term = 1` drew.
- **`DrawnSprite::lightmap`**, set in `buildSprites` from the node's look when it is static
  and applies light (design section 3.1). `Lighting::Read` already refuses a lightmap
  anywhere else; the condition says what the engine does rather than what the file holds.
- **What names no lightmap:** the player, the placed portals' halos, the shot, the
  beholder, spikes and thrown stones, which no level file placed.
  - **The dark dragon's dropped platform does not take its template's.** A bake is the light
    that fell where the template stands, and the original reads one per entity already in
    the scene, by its id (`ETHScene.cpp:315-334`, `add<id>` at `ETHSpriteEntity.cpp:437`).
    The platform is added when the dragon dies, under an id no file names. The design is
    silent here. **Reasoning, and not exercised:** 4-32, the one level that drops it, is a
    `darkest` level with no lightmap at all.
- **No runtime bake is needed here.** Every level that places a torch is a `darkest` level
  (torch.json's census), and none of the ten ships a lightmap: not one `eth_lightmap` key in
  their ten files (counted this step). So G6's switch to runtime mode has no
  file lightmap to drop, and G4 draws files on every level.
- **Not done, and why:** section 5.2's `normalTexturePath`, `height`, `normalYDown` and
  `lightMask`. The engine reads none of them until the light loop (E3), and G4's text names
  only the ambient, the overlay and the blend. `lighting.json` gains nothing.
- Comments: the class header, `HeldLightmaps`, the unload, `syncLighting`'s block, and
  `sim/Lighting.hpp`'s header, which said the lightmaps were drawn by nothing.
- **ARCHITECTURE.md, section 4c**: the sentence on `--visit-levels` named the command and
  step 42's peak of 96. Run today the command peaks at 91, so it now says the walk watches
  the sprites' own sets, 1,460 over two passes, peaking at 91. The only other document
  touched.
- **A correction to step 45's suite comment:** `add696.png` is not "the torch's wall". It is
  the lightmap of `light_ent_696`'s own sprite, `torch_small.png` (the new case pins it).

**THE WALK WATCHES THE LAYER (game, DEV ONLY: `LevelVisit.{hpp,cpp}`, `main.cpp`'s help).**
Steps 42 to 46 had nothing drawing a lightmap, so `--visit-levels` acquired each level's
lightmaps itself and invalidated them before the layer did. **Run unchanged it would now
double count**: its invalidation would drop the probe's set and the sprite's, twice what it
expects. So it no longer acquires anything of its own.
- **Six frames after a level opens**, it checks:
  - the pool equals the cache (the check that matters, unchanged);
  - `HeldLightmaps()` equals the lightmaps the level file names;
  - each is the overlay of a sprite, loaded (not the black a failed file caches), and under
    the id the registry gives its path. Asking for a loaded path adds nothing, so
    `TextureRegistry::Size()` staying put says the sprites had loaded every one.
- **One thing it does take.** The renderer acquires a set only for a sprite it draws, and a
  lightmapped sprite off screen has none yet. For each lightmapped sprite, it asks for the
  set of **that sprite's own four maps**: the set the renderer takes when it comes into view.
  - None may come back as the exhausted pool's fallback.
  - Taking the undrawn ones must add exactly that many to both counts.
  - So each visit puts the level's whole pressure on the pool, as walking it through would.
- **When the next visit opens a level**, the layer's unload inside `PressMenu` must drop
  exactly the measured number of sets from the cache and leave the pool unchanged. Nothing
  renders inside `PressMenu`, so nothing else can drop one. A level opened over itself is a
  retry and must drop none.
- **At the end** it leaves for the level grid and checks the pool equals the cache. Then it
  opens the last level again and quits with its sets held. The layer's detach drops them
  after the last frame, so their frees are still queued when the renderer destroys the
  pool: step 42's shutdown case, now through the layer, with validation to catch it.

**THE SUITES.**
- **`test_mp_layer` 418 -> 463.**
  - **`ColoursOf` now reads what the engine packs**, not the material's field: a
    `PushConstantData` with the material's colour, through `RenderSystem::ApplySprite2D`. So
    every step 45 pin (level0's arches and torch, the shot's (0.95, 0.9, 1), level21c's 0.01,
    the crystal's fade) still holds the product C x min(1, A + E), whichever side multiplies.
  - `EverySpriteIsDrawnAtItsAmbient` adds that both arches are colour (1, 1, 1, 1) with the
    2D record on at (0.35, 0.3, 0.35): the product is the engine's.
  - `TheLevelsArtIsDrawn` (level8's 23 sprites) and `ThePlayerIsTheDarkMage` now pin
    `Premultiplied` where they pinned `Alpha`.
  - **`LightmapsAreDrawnOverTheirSprites`** (new), design section 7.1's pins:
    - level0: **9** materials name an overlay, exactly `HeldLightmaps()`;
    - each on the 2D path, strength 1, premultiplied;
    - four at the ambient (both arches, the wall, the torch) and five whole (emissive 1: the
      platforms, the bar, the stone);
    - **`add696.png` on `torch_small.png` at (288, 80) px**, which is `light_ent_696` at
      (288, 64) with its sprite's 16 px offset, and one torch sprite in all;
    - no halo, door or player among them, and the four static portals' halos still
      `Additive` with the 2D record on;
    - a retry draws the same nine;
    - N draws level1's nine, as held, none of level0's;
    - the grid draws none;
    - **level4a: none**, and every one of its sprites on the 2D path.
  - **A first draft of the new case was wrong, not the layer**: it expected all nine of
    level0's lightmapped sprites at the ambient. Five are emissive 1, and 5 checks failed
    naming them. The pin was rewritten per image before the after frames were captured.
- **`test_mp_lighting` 444**, unchanged: G4 adds nothing renderer-free.
- **Mutations, one build each, not kept.** The layer was copied first (md5 `3647f973...`) and
  put back byte for byte after each.
  - `tint` not writing the overlay: **9 failures** of 443 checks (the level0 and level1
    overlay pins; the per-overlay loop ran over nothing).
  - the ambient folded into `albedoColor` **as well as** handed to the engine: **10 failures**
    of 463 (the arches 0.1225, the torch, the retried arches, the shot at 0.9025, level21c's
    four sceneries at 0.0001, and both arch records).
  - `Alpha` never made `Premultiplied`: **11 failures** of 463 (level8's sprites, the player,
    and the nine overlays).
  - **`releaseLightmaps` not invalidating** (the walk's own check): the walk **failed, exit
    1, 134 failures**, the first "opening 1-2 (level1): the layer's unload of 9 lightmap
    set(s) moved the cache from 25 to 25". The pool peaked at **804**, over step 42's old
    cap of 512.

**THE GATES.** Captures by `work/g4/capture.sh` (a copy of E2's, folder changed), gates by
`gates.sh`, the sweep scored by `screen.py`, all under `out/parity/specs/lighting/work/g4/`.
- **The before is G3's final**, taken on this step's starting build: all 21 before-frames are
  md5-identical to `work/e2/final/`, which step 46 found identical to `work/g3/fix2_final/`.
  `WolfBrigade.exe` was refused (exit 126), again after one relink, and ran after the second.
- **The after was taken on the final build and again after the last rebuild**
  (`final2/`): 21 of 21 md5-identical. A first capture on the step's first build
  (`try1/`) gave the same bytes as `after/` for every level it took.
- The design's "today" figures for `LM_E<1` (34.78, 43.12, 54.46) are from before G2; the
  Before column is G3's, which is what this step changes.
- **Design section 10's second amendment does not apply here.** It replaces a best-tier
  variant with its 1x re-render; every G4 row is read against `engine_tier1x`, which is
  already the model drawn with 1x art. `work/g3/fit_extra/` is not used.

| Gate | Required | Before (G3) | Measured |
|---|---|---|---|
| 1-01 `LM_E<1` against `engine_tier1x` (67 blocks) | bMAE <= 2.0, abs(bias) <= 2.5 | 9.59, -17.9 -8.7 -2.0 | **0.45**, 0.0 +0.1 -0.1 |
| 1-01 `LM_E=1` (97) | the same | 3.15, -4.0 -1.4 +0.4 | **0.85**, +0.7 +0.7 +0.8 |
| 1-09 `LM_E<1` (603) | the same | 1.72, -2.4 -1.1 -0.1 | **0.66**, +0.1 +0.1 +0.1 |
| 1-09 `LM_E=1` (290) | the same | 2.41, -4.5 -2.2 -0.4 | **0.12**, 0.0 0.0 0.0 |
| 1-09 `noLM_E=1` (519) | the same, and within 0.3 of G3 | 0.29, -0.1 -0.1 -0.1 | **0.29**, -0.1 -0.1 -0.1 |
| 2-26 `LM_E<1` (372) | bMAE <= 2.0, abs(bias) <= 2.5 | 20.46, -36.8 -17.7 -6.9 | **0.24**, 0.0 0.0 0.0 |
| 2-26 `noLM_E=1` (1,161) | the same, and within 0.3 of G3 | 0.91, -0.1 -0.1 -0.1 | **0.91**, -0.1 -0.1 -0.1 |
| non-LM classes elsewhere, within 0.3 of G3 | (the same clause) | 2-05 `noLM_E<1` 1.62; 4-22 `noLM_E<1` 0.82, `noLM_E=1` 0.91 | **1.62; 0.82, 0.91**, biases to the tenth unchanged |
| 2-05 `noLM_E<1` at frames 330, 360, 390, 450, 480, 510 (design section 10's amended G3 row) | median bMAE <= 2.0, abs(bias) <= 1.0 | 0.42, +0.1 +0.1 +0.1 at each | **0.42, +0.1 +0.1 +0.1 at each**, 349 blocks, offset (0, 0) |
| `torch.py`, level0 frames 270..420 (design section 10: must pass again here) | bright flame R >= 240, G >= 235 | 255.0, 232.1 (B 127.5, peak grey 243.0) | **255.0, 244.8** (B 132.9, peak grey 244.0); the original 253.2, 252.0, 159.1 |
| `test_mp_layer`: level0 9 overlays, `add696.png` on `light_ent_696`; level4a none | pass | 418 checks | **463 checks, 0 failures** |
| `test_mp_lighting` | pass | 444 | **444, 0 failures** |
| E0's walk, `--visit-levels lightmapped --visit-passes 2`, the layer's own sets | exit 0; pool = cache after every unload; no exhaustion; validation silent | step 46: the probe's 1,460, peak 96 | **exit 0 in 19.6 s**, 134 visits, 0 failures (below) |
| `door.py` on 2-01 (step 44's confirmation) | p95 R >= 230, p95 G <= 50 | 255, 48 | **255, 48** |
| MainScene and Wolf Brigade, `--fixed-step --frames 120` | unchanged (no engine file changed) | `1e24c2a3...`, `d9e7b8fe...` | **the same**; MainScene "Clean exit with validation active" |
| the menu at 120, level0 frame 1 (the HUD over both blacks) | unchanged | `934232ed...`, `5c5cfed2...` | **the same**. Neither draws a level sprite |
| validation, 19 port captures and all 128 sweep runs | active, silent | - | "ACTIVE" in every port log, no `VUID` or "Validation Error" in any, every run exit 0 |

**The walk, measured** (`work/g4/visit_final.log`, on the final binary):
- 134 visits (67 levels, twice), **1,460 lightmap sets**: 1,100 already taken by the renderer
  at the measure, 360 taken for sprites not yet in view.
- **Every unload** dropped exactly the level's sets (at most 21, at 3-13) with the pool
  unchanged at that moment; **every visit** found the pool equal to the cache six frames on.
- **The baseline**, the pool without the level's lightmap sets: pass 1 climbed **16 -> 70**,
  pass 2 stood at **70 at all 67 visits**. Peak **91** (70 plus 3-13's 21).
  - Step 46's baseline was 75 with a peak of 96. The five fewer are sets the layer no longer
    takes: a lightmapped image drawn only with its lightmap never needs the set with a black
    overlay. Reasoning from the keys, not measured set by set.
- On the grid after the last release: 74 cached, 74 in the pool (70 plus the grid's own 4).
- Quit holding level27c's 5 sets for the layer's detach; `Subsystem resources destroyed
  cleanly`, no line containing "error".

**THE COLOUR CHANGE IS THE LIGHTMAP'S, AND THE REST IS A ROUNDING (attributed).**
- **Where there is no lightmap, the frames move by at most one level.** Frame 420 against G3:
  1-13 820 px, 2-05 285, 4-22 551 and 2-01 821 px changed, every one by 1 of 255.
- **That is the premultiplied blend alone.** One build, not kept (`attr_blend.sh`): this
  step's layer with the `Alpha -> Premultiplied` line taken out.
  - 1-13, 2-05 and 4-22 captured **md5-identical to G3's frames**. So the ambient's move into
    the engine is exact, as `ApplySprite2D` multiplies the same two floats the fold did.
  - Against this step's frames it differs by at most 1 of 255: 1-01 on 1,220 px, 2-26 on
    592, 1-13 820, 2-05 285, 4-22 551.
  - The source was restored to its md5, and everything after was rebuilt and recaptured.
- **Whole frame (`compare.py`, frame 420 against `levels/<W-LL>_t8.0.png`):**

| Level | mean_abs before -> after | edge_iou before -> after |
|---|---|---|
| **1-01** | **23.49 -> 15.86** | 0.4450 -> 0.4560 |
| **1-09** | **9.42 -> 8.21** | 0.4220 -> 0.4290 |
| 1-13 | 17.62 -> 17.62 | 0.2307 -> 0.2307 |
| 2-05 | 12.35 -> 12.35 | 0.3235 -> 0.3234 |
| 2-26 | 13.69 -> 10.43 | 0.3322 -> 0.4077 |
| 3-05 | 9.27 -> 8.74 | 0.3832 -> 0.3883 |
| 4-22 | 4.95 -> 4.95 | 0.6088 -> 0.6088 |

- G5's own gate is 1-01 at 15 or below (design section 7.1); 15.86 is not that gate, and
  halos are not drawn.

**WHAT ELSE MOVED, NOT GATED.**
- **Against the original**, the lightmapped classes: 1-01 `LM_E<1` 8.74 -> **3.01**, 1-09
  `LM_E<1` 2.78 -> **2.36**, 2-26 `LM_E<1` 20.64 -> **2.36**.
  - The design's thresholds for these are after plan_port System 6 (2.53, 2.0, 2.27). The
    port is within 0.66 of `engine_tier1x`, and `engine_tier1x` itself reads 2.95, 2.34 and
    2.36 against the original on those blocks. So what remains is the 1x art, not the add.
  - Against `engine` (the best tier): 1.96, 2.17, 1.92.
  - 1-09 `LM_E=1` reads 11.72 against the original, where `engine_tier1x` reads 11.72: the
    tier again, as step 45 found.
- **The halo classes** (G5's): 1-01 `LM_E<1_halo` 42.22 -> 13.21 against `engine_tier1x`,
  1-09 `LM_E<1_halo` 40.92 -> 10.29, both still dark (bias -24.4 and -18.9 on R): no halo is
  drawn. 1-01 `LM_E=1_halo` reads 0.11 against `engine_tier1x` and 20.58 against `engine`,
  a split this step did not look into.
- **All 128 levels at frame 420** (`capture.sh after sweep`, `screen.py` against step 46's
  `work/e2/after/sweep`):
  - every run exit 0, validation ACTIVE, no VUID, **no level drawn unlit**;
  - **all 67 lightmapped levels** changed by more than one level somewhere;
  - **the 61 without a lightmap** changed by at most 1 of 255, except **one pixel of 4-25 by
    2** (474 px changed in all). Not looked into; two premultiplied layers rounding the same
    way is the first guess;
  - against the 81 library frames: **no `edge_iou` fell by more than 0.02** (worst -0.0038,
    3-09; best +0.0755, 2-26); `mean_abs` fell on 36, rose on one (3-15, +0.02) and held on
    the rest.
  - **1-05, 34.74 -> 13.74**, the largest fall: step 45 put its rise (6.4) on the blue lamp's
    lightmap under a wall now dimmed, and this step draws that lightmap.

**LEFT FOR LATER STEPS AND FOR THE OWNER.**
- **E3 and G5**: the lights, the normal maps, the height, the mask and `normalYDown`; the
  halos; `lighting.json`'s green-down switch. The torch confirmation is G5's again.
- **G6**: RGB565 and the runtime bake. No level needs to drop a file lightmap for it.
- **Step 46's watcher gap stands**: an edited lightmap is read again only when its level
  unloads.
- **Step 46's `AcquireMaterialSet` throw** on a key naming an invalidated texture: the overlay
  now carries per-level lightmaps. `unloadLevel` invalidates them before it destroys the
  sprites, with no frame between; the walk's 134 unloads and the sweep ran with validation
  silent. Not a proof for every order of unload a later screen may add.
- Unchanged from step 45: the script's emissive (design section 5.8), the sky controller
  (black ground on 34 levels), the placeholder boxes, 1-13's camera.
- Not measured: frame time and draw calls with the overlays (design section 6 expects at most
  about 21 more draws on 3-13).

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning in any build.
  - The first built `MagicPortals` alone (the layer, `main.cpp`, `LevelVisit.cpp`) for the
    gate captures; the second rebuilt it with the new walk.
  - The full build after the suite: 31 translation units, 40 links. So was the final full
    build with every changed file touched, after the three suite mutations were restored.
  - The attribution and the walk mutation each built `MagicPortals` alone; each restore was
    followed by a build (1 translation unit, 3 links: `MagicPortalsGame`, `test_mp_layer`,
    `MagicPortals`).
- **ctest: 113 of 113 pass, Not Run 0** (`work/g4/ctest_final.log`), on the last build.
  - **Smart App Control**, resolved by deleting and relinking each time:
    - the final full build: `test_mp_zerog`, `test_mp_diamonds`, `test_mp_hud`,
      `test_mp_camera` and `test_mp_tscn` refused; `test_mp_tscn` again after one relink,
      none after two; then 113 of 113;
    - the build after the attribution: none refused, 113 of 113;
    - the build after the walk mutation: `test_mp_layer` refused, ran after one relink;
      then 113 of 113;
    - `test_mp_layer.exe` in the blend mutation and `MagicPortals.exe` in the walk mutation,
      each refused once (exit 126) and run after one relink;
    - `WolfBrigade.exe` at the before capture, as above.
  - Run directly on the final build: `test_mp_layer` **463**, `test_mp_lighting` 444,
    `test_mp_sprites` 166, `test_mp_torch` 92, `test_mp_start` 726 and `test_mp_levels` 128
    checks, 0 failures and no skip line in any.
- **The final binary draws the gate frames**: `final2/`, 21 of 21 md5-identical to `after/`,
  on which every gate above was read; and the walk above was run on it.
- **Neither repository was committed to.** The remake's working tree is clean: everything
  this step wrote is under the gitignored `out/parity/specs/lighting/work/g4/`.

## Step 48 - a light a 2D sprite can take, with a height of its own, and the loop that adds it (built)

The lighting design's step E3 (the remake's `out/parity/specs/lighting/design_port.md`,
section 7.1: section 4.4 and the light loop of section 4.6). **An engine capability, off
until something places a light**: a `Light2DComponent`, one storage buffer that carries
every one of them to the shader, and the loop in `shadeSprite2D` that adds one clamped term
per light to a 2D sprite whose mask shares a bit with it. It reads, at last, the four things
step 46 packed and nothing read: the height, the tint without ambient, the light mask and
`kNormalYDown`.

**Nothing places a light, and nothing drew differently.** Wolf Brigade, MainScene, every port
frame the gate takes, the HUD over both blacks and the menu are byte-identical before and
after. What the loop draws was proved on a scene built for it: 30,256 predicted pixels, none
more than 1 of 255 away. The game does not change: no signature it calls moved.

**THE COMPONENT (engine: `core/Components.hpp`).** `Light2DComponent`, as design section 4.4
wrote it: `color`, `intensity`, `range`, `height`, `layers`, `enabled`.
- **x and y are the entity's world position; the height is absolute** and not the
  transform's z. In a 2D scene z is draw order: the port's quads sit at slot depths, and a
  torch drawn in front of a wall is not nearer to it.
- Beside `LightComponent`, and deliberately not a `type` of it. Those lights are tied to the
  camera's depth slices, chosen by importance and given shadow slots, none of which means
  anything to a sprite under an orthographic camera.
- The arithmetic is written above the struct, so a reader finds the formula where the light
  is declared.

**THE GATHER (engine: `core/Light2D.{hpp,cpp}`, new, no Vulkan).** `Light2D::GatherLights2D`,
pure, the engine's shape for this (`ClusterGrid`, `GatherUvTransforms`):
- Position from `LightWorldPosition`: the world matrix once the hierarchy has resolved, the
  local transform before. A lamp parented to a character goes with it, which is the bug
  section 4c of ARCHITECTURE.md records `LightComponent` once had.
- `color x intensity` folded on the CPU.
- **A light whose folded colour is zero is not packed**, and is not counted as dropped. Ethanon
  leaves it out of its list the same way: `ETHEntityRenderingManager::AddLight`,
  `ETHEntityRenderingManager.cpp:161-172`, the test at `:163` (read in the remake's
  `reference/ethanon` clone this step, where it checks the colour before `lightIntensity` is
  applied; folded here, an intensity of 0 is the same black light).
- **Past `capacity`, dropped in the registry's order and counted.** The renderer logs the
  count once per change, as it does for the froxel list and the transforms.
- Nothing else is filtered. A range of 0 lights nothing; a negative range is squared by the
  shader and lights as its magnitude. The design lists four behaviours and a range rule is
  not one of them, so it is left below as an open item rather than invented here.

**THE RECORD.** `GpuLight2D`, std430, 32 bytes: `vec3 position` (z the height), `float range`,
`vec3 color`, `uint32 layers`, behind `GpuLight2DHeader`, 16 bytes: the count and three words
of padding, because a runtime array of a struct holding a vec3 starts at 16. Size and all four
offsets are `static_assert`ed. `kMaxLights2D` = 64; `kLight2DBufferBytes` = 16 + 32 x 64 =
2,064.

**THE BUFFER (engine: `VulkanPipeline`, `VulkanRenderer`).**
- Scene set binding **12**, storage buffer, fragment stage. Every `VulkanPipeline` builds the
  scene layout the same way, so all of them gained it together.
- `m_light2DBuffers`, one per frame in flight, allocated at capacity with the clustered-light
  buffers; the descriptor pool's storage-buffer count `MAX_FRAMES_IN_FLIGHT * 6u` -> `* 7u`,
  with its comment's list of bindings; a thirteenth `WriteDescriptorSet`.
- **Gathered and uploaded every frame, after the UV transforms and before the uniform
  buffer, a count of zero included.** `shadeSprite2D` reads the count for every 2D sprite
  fragment, lit or not, so a frame with no light has to say zero rather than leave a stale or
  unwritten number in front of it. The existing light upload's `if (!empty())` was not
  copied; the transforms' unconditional upload was.
- Header and records go up as one contiguous write from a scratch vector.
- **Proved in two builds, so a difference would have had an owner.** The first build had the
  component, the gather, the codec, binding 12, the pool, the write and the upload, and not
  the shader: all 21 gate frames byte-identical to before (`work/e3/stageA/`), validation
  silent. The second added the shader, and nothing else that draws (its other step was
  `test_materials`' reading of it).

**THE SHADER (`shader.frag`, regenerated `frag.spv`, 44,904 -> 49,020 bytes).** Design section
4.6's declarations and loop:
- `struct Light2D` and `Light2DBuffer` at binding 12, `MAX_LIGHTS_2D = 64u`, and
  `LIGHT_MASK_BITS = 0xFFu` beside `LIGHT_MASK_SHIFT`.
- `lit` starts at zero. When the mask is not zero and the count is not zero:
  - the normal map decoded and **not renormalised**, green flipped under `kNormalYDown`;
  - carried along the model's first two columns, normalised, so a rotated or mirrored sprite
    turns its normals with it, with z left as z (towards the viewer, where heights are);
  - `P = (fragWorldPos.xy, emissive.w)`, the tint `texel x emissive.rgb` (no ambient);
  - per light sharing a mask bit: skip at `d2 >= r2`, else
    `lit += clamp(tint * color * ((1 - d2 / r2) * dot(v, n) * inversesqrt(max(d2, 1e-12))), 0, 1)`.
- **The return is `base * alpha + lit` premultiplied and `base + lit` otherwise.** With `lit`
  exactly zero, those are the two expressions this path returned before, and the captures
  below say so to the byte. The light goes on at full weight over a partly transparent texel,
  as the original's separate `One, One` light pass does.
- The range test is not redundant with the clamp. Beyond the range and **behind** the surface,
  `1 - d2/r2` and the facing are both negative and their product is a positive light from
  nowhere. The suite now pins that case (mutation below).
- Not ported, as design section 4.6 states: the per-light cull, the scissor, and the vertical
  variant.
- The `Shaders` target compiled it with the SDK's glslc (step S0), a direct
  `glslc shader.frag` gives the same bytes (`60f19975...`), and `spirv-val` accepts it. A
  comment-only fix to the shader after the gate frames recompiled it to the same md5.

**THE CODEC (`ComponentCodec`).** `"Light2D": { "Color", "Intensity", "Range", "Height",
"Layers", "Enabled" }`, written when the entity has one, its own key rather than inside
`"Light"`, which a reader would turn into a PBR point light. Each field reads with the
component's own default; `Layers` is clamped to a byte, not wrapped. **Beyond the design's
list, and why:** ComponentCodec's header says adding a component means editing one place, and
scenes and prefabs save through it. Without it a Light2D placed in a scene would not survive
a save. The editor's play/stop snapshot (`PlayMode.cpp`) and undo history (`EditHistory.cpp`)
both go through `SceneSerializer`, so a Light2D survives those too (checked in the source, not
run). `InspectorPanel` was not touched: there is no Light2D section or Add Component entry.

**THE SUITES.**
- **`test_light2d` (new), 90 checks, floor 80.** Pure arithmetic and a registry, so it needs no
  working directory and cannot skip.
  - The gather: x and y from the transform, **the height and not the transform's z**, range,
    colour x intensity (3, 1.5, 0.3 from (1, 0.5, 0.1) x 3), layers; a parented light at its
    world matrix; a black light, a light at intensity 0 and a disabled one left out and not
    counted, a pure red one kept; 70 lights at capacity 64 give 64 and **6 dropped**, and at
    capacity 3 give 67 dropped, with a black one among them neither taking a slot nor counted;
    an empty registry gives 0 and 0; a stale output vector is cleared.
  - `Light2D::WorldNormal` and `Light2D::Contribution`, **the CPU transliteration kept beside
    the shader** (each line carries the GLSL it mirrors):
    - flat normal, light at height 6 over a receiver at 0: the add is
      `(1 - d^2/R^2) * (6/d)` at x = 0, 2, 5, 8, 12 and 18 with R 20; nearer is brighter; a
      receiver raised to the light's height takes nothing;
    - a texel facing image-right is lit from world +x (0.91 at 3 units, R 10) and **exactly
      0** from -x, and 0 from above;
    - the same sprite turned +90 degrees (`TransformComponent::getModelMatrix`, as the
      renderer builds the record): the model's first column is +y, it is lit from above at
      0.91, not from the right and not from below; turned -90 it is lit from below; a scaled
      quad's normal is not stretched;
    - mirrored (scale x -1): lit from -x, not +x, green unchanged;
    - `kNormalYDown`: a green-up texel faces down and is lit from below, not above; red
      untouched;
    - a half-length normal gives half the add (not renormalised);
    - `d >= R`: exactly 0 at d = R, just past it, far past it, diagonally past it, and **past
      it behind the surface**; just inside it is `1 - 4.99^2/25`; a light on the fragment
      divides by nothing;
    - the mask: a shared bit lights, no shared bit and a mask of 0 do not, bit 7 works;
    - per-channel clamp of tint x colour, and a black texel takes no light.
  - **The rotation is the engine frame's claim.** `engine_math.md` section 2.6 measured the
    original's rotated lightmaps against its drawing rotation (MAE 0.80; correlation -0.107
    for the opposite sense). That the port hands the engine its sprites' angles in that sense
    is the game's to show when it feeds them (G5); this step does not settle it.
- **`test_materials` 326 -> 336** (floor 320 -> 330, still six below the count). New case: the
  shader's `struct Light2D` is `GpuLight2D`'s four members in order; binding 12 is a count,
  three pads and the array; `MAX_LIGHTS_2D` is `kMaxLights2D`; `LIGHT_MASK_BITS` is
  `kLightMaskBits`; the loop reads the height from `emissive.w` and the tint from
  `emissive.rgb`. Read from the source at run time, comments stripped, spacing ignored.
- **`test_serialize` 445 -> 479** (floor 400 -> 460). The fully loaded entity carries a
  Light2D with every field off its default, so the prefab's key parity and value round trip
  cover it. A new case: a scene saves it under `"Light2D"` and not `"Light"`, once, only for
  the entity that has one, and reads it back field for field without growing a
  `LightComponent`; a block naming only `Range` and `Layers: 300` reads the other fields as
  defaults and the layers as 255; `Layers: -4` reads as 0.
- **Mutations, not kept.**
  - `MAX_LIGHTS_2D = 32u` in `shader.frag`, no rebuild: `test_materials` failed on exactly
    that check, **1 of 336** (`test_materials.cpp:1444`). Restored from a copy, md5
    `bdf2d3dd...` before and after.
  - The y-down flip removed from `Light2D::WorldNormal`: `test_light2d` **3 failures of 89**
    (`:310`, `:313`, `:314`, the y-down case).
  - The range test removed from `Contribution`. Before running it, reading the suite found
    no case the clamp alone would get wrong (every beyond-the-range light it had was in
    front of its surface), so the behind-and-beyond case was added first; that run of the old
    suite was not made. With the case, the mutation failed **1 of 90** (`:346`).
  - Light2D.cpp was restored from a copy (md5 `2e1d378f...`) after each, and everything was
    rebuilt.
- **Unchanged from step 47's counts:** `test_resourcesync` 32, `test_draworder` 104,
  `test_renderplan` 127, `test_sprite` 66, `test_screenoverlay` 46, `test_mp_layer` 463,
  `test_mp_lighting` 444; all 18 `test_wb_*` pass. Also run directly: `test_clustergrid` 201,
  `test_lightselection` 19 (no earlier count taken).
- **What no suite covers.** The binding, the pool, the write, the upload and the shader's own
  arithmetic need a device. The scene below covers them.

**PROVED ON A SCENE BUILT FOR IT (`work/e3/experiment/`: `make.py`, `check.py`).** Two
scenes, `SupersonicEngine --scene <scene> --window 1280x720 --fixed-step --frames 120
--screenshot` (the editor viewport, 744 x 336): DisplayEncoded, a black `Color` background,
bloom 0, an **orthographic** camera at the origin looking down -z (height 8), so a pixel's
world position is linear in its index and every pixel can be predicted, not only a centre.
Eight premultiplied 2D sprites of 1.6 units, white albedo, each with **its own light on its
own layer** so no light reaches another's sprite. The two normal maps are generated 8 x 8
PNGs of one colour, under `out/`. `check.py` predicts every pixel 3 px inside each quad from
the numbers in the scene and the maps' bytes (the flat map is the built-in (128, 128, 255)):
design section 4.6's loop in numpy, then `floor(v * 255 + 0.5)`.
- **Both exit 0, "Clean exit with validation active", no VUID.** That covers binding 12's
  write and the upload with eight lights and with the masks off.

| Quad (3,782 px each) | Light | max abs diff | mean predicted | mean measured | centre pixel, predicted / measured |
|---|---|---|---|---|---|
| Q1 flat, tint (0.8, 0.9, 1), ambient 0 | (1, 0.6, 0.3), 0.5 right, height 1, R 3 | 1 1 1 | 137.82 93.02 51.68 | 137.76 92.99 51.66 | 158 107 59 / 158 107 59 |
| Q2 image-right normal | white, 1.2 right, height 0 | 1 1 1 | 188.56 | 188.49 | 215 / 215 |
| Q3 the same | 1.2 **left** | 0 0 0 | 0 | 0 | 0 / 0 |
| Q4 turned +90 degrees | 1.2 **above** | 1 1 1 | 188.29 | 188.23 | 214 / 214 |
| Q5 turned +90 degrees | 1.2 right | 1 1 1 | 31.67 | 31.66 | 0 / 0 (lit only on the half below the light) |
| Q6 mirrored | 1.2 **left** | 1 1 1 | 188.55 | 188.49 | 213 / 213 |
| Q7 green-up map, `NormalYDown` | 1.2 **below** | 1 1 1 | 188.29 | 188.23 | 214 / 214 |
| Q8 alpha 0.5, ambient 0.5, flat | (0.4, 0.2, 0.1) x 2, height 1 above | 1 1 1 | 214.32 139.04 101.40 | 214.26 138.97 101.36 | 245 154 109 / 245 154 109 |

- **Every predicted pixel within 1 of 255, and 0 non-black pixels outside the quads**: a light
  with no sprite under it adds nothing.
- **Q8 is the premultiplied claim.** The light goes on at full weight over the half-transparent
  texel: 245, 154, 109 at the centre. Weighting it by alpha, `(base + lit) * alpha`, would read
  154, 109, 86.
- **With every `LightMask` 0** (`light2d_nomask.scene`, the same lights): Q1 to Q7 are black to
  the last pixel and Q8 is its base alone, **64 64 64** on all 3,782 pixels; max diff 0.
- On the final build both scenes captured again: `light2d.png` `cb913dd7...` and
  `light2d_nomask.png` `5883441e...`, md5-identical to the first.

**GATES.** Captures by `work/e3/capture.sh` (a copy of G4's with its folder changed), before
on this step's starting build (G4's final, commit `827ea6c`, which rebuilt with nothing to do).
**The before is G4's final**: all 21 before-frames md5-identical to `work/g4/final2/`, and the
menu, MainScene and Wolf Brigade md5s too.

| Gate | Required | Measured |
|---|---|---|
| `SupersonicEngine --scene assets/scenes/MainScene.scene --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical before/after | `1e24c2a30f6f22dc2bb01b6038bd1af9` before, stage A, after and final (steps 42 to 47's); `Clean exit with validation active` |
| `WolfBrigade --window 1280x720 --fixed-step --frames 120 --screenshot` | byte-identical before/after | `d9e7b8fe5e8b0b2f162b0195e5d5ba31` before, stage A, after and final |
| port captures, 1-01, 1-09, 1-13, 2-05, 2-26, 3-05, 4-22 at frame 420 | unchanged (no light emitted yet) | identical before, stage A, after and final: `a585e9b2...`, `6620c110...`, `521e8405...`, `2979daf4...`, `8f250214...`, `05edbf77...`, `a80eae25...` |
| the torch and door frames (level0 f270..390, level0a f300..420) | unchanged | 10 of 10 identical in stage A and final; in `after/` 8 of 10, the other two failed (below) and 6 reruns were identical |
| screen overlay / HUD, level0 frame 1 | bit-exact | `5c5cfed20f942d06da1f22fb5835c1dd` before, stage A, after and final |
| the menu, frame 120 | unchanged | `934232ed534adae966587863179378e9` before, stage A, after and final |
| scene encoding (step 43) and the 2D record (step 46) under the port | bit-exact | every port frame above is DisplayEncoded and drawn through `shadeSprite2D` with mask 0, and identical |
| all 128 levels at frame 420 (not required) | unchanged | **128 / 128 md5-identical** to `work/g4/after/sweep/` (`capture.sh final sweep`, on the final build); the seven gate levels' sweep frames equal this step's `before/` frames; every run exit 0, validation ACTIVE in all 128 logs, no VUID. The level sprites of every lit level draw through `shadeSprite2D` with mask 0 |
| validation | silent | "ACTIVE" in every port log of before, stage A, after and final; MainScene's "Clean exit with validation active"; no `VUID` or "Validation Error" in any log (Wolf Brigade's prints neither, as before) |
| light loop, binding 12, upload (experiment) | as predicted, validation silent | 30,256 pixels within 1 of 255, 0 stray; masks off: 0 of 255; exit 0, clean exit with validation, twice |
| `frag.spv` | regenerated from the GLSL | `Shaders` target; direct glslc identical (`60f19975...`); `spirv-val` clean |
| build | zero warnings | 0 in all four builds (269, 4, 5 and 129 steps) and every relink |
| ctest | all pass; `test_light2d`, `test_serialize`, `test_materials` named | **114 / 114**, Not Run 0; 90, 479, 336 |

`lightgate.py` was not run: every port frame is byte-identical to G4's, so every number it would
print is the one step 47 printed. The design gates the light itself at G5.

**TWO FAILED RUNS IN `after/`, NOT REPRODUCED.** In the `after/` round, `level0a` at frames 360
and 420 **exited 1 about three seconds in, with no PNG** and no message. **No gate number came
from either**: the md5 columns above are stage A's and final's, where all 21 ran.
- Each log stops right after "[AudioEngine] Loaded ... fireball.mp3", the last line before a
  good run's `[SelfCheck] Mid-run invalidate`, which `SupersonicApp` logs at frame
  `maxFrames / 2` (180 and 210 here). So both died in the first half of the run, after the
  level and its sounds had loaded; where exactly is not known.
- The run between them (f390) and those around them were normal, and the Windows Application
  log has no event for the executable.
- Re-run three times each on the same binary: **6 of 6 exit 0**, md5-identical to before
  (`9efeb31f...`, `d69eab9d...`).
- The `final/` round on the relinked binary took all 21, again identical.
- Not the shader: stage A (no shader change) and the reruns and final (with it) draw the same
  frames, and a shader problem does not exit on a timer. The cause is not known. It is recorded
  rather than folded into "Smart App Control", which refuses at launch with 126.

**LEFT FOR LATER STEPS AND FOR THE OWNER.**
- **G5** places the lights (section 5.3), gives the player and the shot theirs (5.4), sets the
  sprites' height, mask, normal map and `normalYDown` (5.2), and draws the halos. Its torch-pass
  gate is the first measurement of this loop against the original; it must also show the
  rotation sense of `engine_math.md` section 2.6 end to end.
- **A range of 0 or below** is not filtered (0 lights nothing, a negative range lights as its
  magnitude). Ethanon keeps a light only if `range > 0`, which the remake's converter already
  applies; the engine does not. The owner's call.
- **No Inspector section** for `Light2DComponent`; the codec saves it, nothing in the editor
  adds or edits one.
- **Not measured:** frame time with lights (design section 4.9 expects a loop of at most a
  handful on a small share of fragments). No light is emitted yet.
- **Stale counts, left alone:** README's badge and "Sixty-eight suites" and AGENTS.md's
  "Sixty-eight" predate this step (ctest runs 114). AGENTS.md's registrar grep still reads
  74: its `[a-z_]+` does not match `test_light2d`, whose name has a digit;
  `ComponentCodec.hpp` still says the codec names eighteen components.
- **The ui2-screens branch** has not seen binding 12. Nothing it calls changed signature, but a
  merge that touches `shader.frag`, `VulkanRenderer` or the scene layout meets this step there.
- Unchanged from step 47: the script's emissive, the sky controller, the placeholder boxes,
  1-13's camera, the watcher gap for overlays.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning in any build.
  - The first (269 steps): the component, the gather, the codec, the renderer and pipeline, the
    new suite and the serialize suite, with the shader untouched (stage A).
  - The second (4 steps): the shader and `test_materials`.
  - The third (5 steps): the suite floors.
  - The fourth (129 steps), after the mutations' restores, a member block moved in
    `VulkanRenderer.hpp` and a comment fixed in `shader.frag`: the core library, every
    executable, and the shader recompiled to the same `frag.spv`.
- **ctest: 114 of 114, Not Run 0** (`work/e3/ctest_final.log`), on the fourth build.
  - **Smart App Control**, resolved by deleting and relinking:
    - the second build: 28 suites refused, then 7, then 2, then `test_packaging` alone, then
      none;
    - the fourth: 8 refused (`test_decomposition`, `test_mp_play`, `test_mp_portal`,
      `test_mp_sounds`, `test_mp_torch`, `test_resourcesync`, `test_scripts`,
      `test_shadowcache`), all ran after one relink;
    - `MagicPortals.exe` refused after the first build and after the fourth, each run after one
      relink; `test_serialize` refused on a direct run after the first build (it ran in the
      ctest rounds); `test_materials` and `test_light2d` refused once each during the
      mutations, each run after one relink.
- **Neither repository was committed to.** The remake's working tree is clean: everything this
  step wrote is under the gitignored `out/parity/specs/lighting/work/e3/`.

## Step 49 - the level's lights placed, the player lit by the torch it passes, and the halos drawn (built)

The lighting design's step G5 (the remake's `out/parity/specs/lighting/design_port.md`,
section 7.1: sections 5.3 lights, 5.4 the player and the shot, 5.5 halos; plan_port System 8
folded in). **Step 48's engine loop now has lights to add.** Game-only: no file under `src/`,
`assets/shaders/` or the engine tests changed.

**The session that built this step ended before its verify and commit.** The implementation,
its gate captures and a first ctest were taken on 2026-09-15; the build, the full ctest and the
128-level sweep were retaken on 2026-09-16 on the same source, and the numbers below are those
runs. No independent verifier ran on this step.

**WHAT CHANGED.**
- **`data/art.json`, `sim/Art`.** Every picture states its .ent's `static` and `apply_light`
  (required, no default: a picture that said nothing would be unlit by a rule nobody read), its
  `<Normal>` when it names one, and its `<Light>` when it has one, with every number required
  and the `z` its height is measured from. The shot's light is projectile.ent's: range 70,
  offset (0, 0, -12), colour (0.6, 0.6, 1), halo.bmp, halo brightness 0.65, size 50.
- **`sim/Lighting`.** `ReceiverMask` and `LightLayer`: two layers, live and static. A sprite
  that applies light takes only the live lights when it is static (its static lights are in its
  lightmap; ETHEntitySpriteRenderer.cpp:70 skips that pass even where no file was baked) and
  both when it is not, so the player is lit by the torches it walks past. `ParticleRatio`,
  `LightColour` (x intensity, x the particle ratio for a live owner) and `HaloColour`
  (x haloBrightness x the particle ratio for every owner, not the intensity;
  ETHRenderEntity.cpp:354-388) are pure and pinned by the suites.
- **`data/lighting.json`.** `normal_map_green_down` true (hPixelLightDiff.ps decodes against
  a y-down world) and `halo_brightness_scale` 0.6, a `_guess` with its measurements beside it
  (below).
- **`MagicPortalsLayer`.** `buildLights` / `syncLights` / `unloadLights`: one
  `Light2DComponent` per level `<Light>` (72 in the 128 levels, every one owned by an entity
  that draws a sprite), placed at its owner every tick, its halo an additive quad just in front
  of the owner and behind its particles, coloured by the owner's live particles; the shot's
  light and halo made and unmade with the shot. Each level sprite now writes its height, normal
  map and light mask with its colour.
- **`main.cpp`.** `--light-masks-off`, DEV only: lights and halos drawn, no light reaching a
  sprite. It exists for the torch pass, which differences a capture with and without it.

**GATES** (captures in the remake's `work/g5/final/`, 1280x720, `--fixed-step`, frame 420).
- **Static receivers do not double-light (+-0.3 of step 47 against `engine_tier1x`): passes.**
  Every lightmapped class on 1-01, 1-09 and 2-26 reads the same to 0.01 before and after:
  1-01 LM_E<1 0.45, LM_E=1 0.85; 1-09 LM_E<1 0.66, LM_E=1 0.12; 2-26 LM_E<1 0.24.
- **Halo classes against the original (asked <= 6.0): two of three.** 1-01 LM_E<1_halo
  8.20 -> **5.83**; 1-09 noLM_E=1_halo 5.04 -> **4.11**; 1-09 LM_E<1_halo 8.55 -> **6.17,
  misses by 0.17**. No scale passes both LM_E<1_halo rows (0.5: 5.57 / 6.45; 0.65: 6.06 /
  6.08; 0.75: 6.70 / 5.99; 1: 8.94 / 6.64), and the fit's own model drawn with 1x art reads
  at least 5.84 on 1-09 at any brightness (2.97 with the original's hd art), so the rest is
  plan_port System 6's.
- **Orange R-B excess beside the 1-01 torch (asked within +-15 of 93/74/64/54 at
  60/90/120/150 u): misses at three of four.** Measured 46.2 / 55.7 / 49.9 / 36.1 (diff -47.2 /
  -18.0 / -14.0 / -17.8), from 20.7 / 48.7 / 48.9 / 36.1 before. Nearer the flame the original
  is far more orange than the formula's halo at any scale the halo classes allow; not settled.
- **compare.py mean_abs (1-01 asked <= 15): passes.** 1-01 15.86 -> **12.72**; 1-09 8.21 ->
  8.01; 2-26 10.43 -> 10.29; 3-05 8.74 -> 8.47; 1-13, 2-05, 4-22 unchanged (17.62, 12.35, 4.95).
- **The torch pass on the player (level0, a recorded right-walk replayed, frames 285-357, with
  and without `--light-masks-off`): three of four.** The red add follows the section 1 formula
  at scale 0.969 (asked 0.8-1.3), green at 1.000 (0.7-1.3), per-pixel error vs the prediction
  0.12-0.63; green add frame means at most 4.89 (asked <= 5; single pixels reach 47); nothing
  outside the player changed. **"Red rises monotonically with proximity" fails**: frame 345
  (154.8 u) reads 19.94 after 20.00 at 167.0 u, and the prediction dips there too (20.45 ->
  20.25), so the formula is not monotonic along this walk. This is the first end-to-end
  measurement of engine_math.md 2.6's rotation sense, and it agrees.
- **The portal shot on level1: passes.** Blue rises near the shot in every shot frame and more
  than red and green (the light is (0.6, 0.6, 1)); the zero-shot frame changes nothing outside
  the player.
- **Torch flame (design section 10.3, asked R >= 240, G >= 235): passes**, 255 / 255.
- **1-13** is still refused by the gate: the camera offset (0, -10) of step 44.

**SWEEP.** All 128 levels at frame 420 on the 2026-09-16 build (`work/g5/final/sweep/`): 128 exit 0,
validation active in every log and silent, no level drawn unlit or as boxes. The 13 logs with a
`GONE` sprite line are the same 13 as step 47's sweep.

**Tests.** test_mp_layer 463 -> 534, test_mp_lighting 444 -> 482, test_mp_sprites 209,
test_mp_torch 112 checks, 0 failures. **Build: no warnings** (MSVC 14.50, Release, Ninja; GCC
was not run). **ctest: 114 of 114, Not Run 0**, after one Smart App Control refusal
(`test_blending`) resolved by deleting and relinking.

**Left open.**
- The halo brightness: 0.6 is tuned to one frame's flame (10 of 12 particles live); over 20 s
  the flame is 0.7265 live, so the halo averages 0.44 of the formula where the fit found 0.5.
  With the orange-excess miss, for the owner and for plan_port System 6.
- The per-light 3D cull and vertical lights (`vPixelLight`, 10 `light_off` instances) are not
  ported; G6 (darkest levels at run time, RGB565) is next.
- The `ui2-screens` branch has not seen steps 48 or 49.
## Step 50 - the pause the original raises over a level, and the world that stands still under it (built)

*Merged into main on 2026-09-16. Steps 50-54 were built on branch `ui2-screens` as steps 45-49,
beside main's lighting steps of the same numbers, and renumbered at the merge; their commit messages
keep the branch's numbers. The merge adapted nothing in them but the numbers and the conflicts it
records in step 54's merge note below.*

The in-level pause control called `openMenu(Screen::Worlds)`: it unloaded the level and
put the chapter screen up, and Escape unloaded it for the level grid. The original does
neither. `GameState::showMenuPopup` (bytes 98935..99036) makes `CustomGameMenuLayer` the
current UI layer **over** the level and pauses game time. This step builds that screen to
the remake's merged spec, `out/parity/specs/ui2/spec.md` section 2 (its evidence is
`pause_finished.md` section 1 beside it), and holds it to that spec's acceptance list 6.1.
- **Data.** Every number is in `games/magicportals/data/ui.json`, in two new blocks, each
  citing its decode and its measurement:
  - `ui_layer`: the UISprite and UIButton primitives that the finished, lost and help
    screens will share;
  - `pause`: the screen itself.
- **Arithmetic.** It lives in two new pure files, `sim/UiLayer` (the primitives and the
  fraction-of-the-screen placement) and `sim/Pause` (the screen), so a suite pins every
  rectangle, alpha and text with no window.
- **Drawing.** The layer draws the pause through `core/ScreenOverlay` in display values, as
  step 39 draws the HUD. That is forced, not chosen: restart and pause are overlay quads,
  and the dim has to cover them (A-P6). A dim drawn in the scene would sit under them.
- **The spec's §1.2 D10** (the pause control unloads the level) is the delta this closes.
  Nothing of the finished, lost or help screens moved.

**THE DECODE, READ AGAIN WHERE THE SPEC STOPPED, AND WHAT IT CHANGED.**
- **Achievements starts from a different place.** Spec 2.4 gives its entrance offset as
  (+26.4, +18.1) u. `UIButton::UIButton` (bytes 76108..76681, instructions 22..64) starts
  a button at `pos + normalize(pos - GetScreenSize() * 0.5) * g_scale.scale(32)`, where
  `_pos` is `unnormalizePos(normPos)` (`UILayer::addButton`, bytes 62337..62682): the
  button's ANCHOR, not its centre.
  - For P5, P6 and P7 the origin is V2_HALF, so anchor and centre are the same point, and
    the spec's (-29.48, -12.44), (+29.48, -12.44) and (0, +32) come out either way.
  - Achievements is anchored at the screen's corner with origin (1, 1). From the anchor it
    starts **(+27.89, +15.69)** u out; the spec's number is the ray to its centre.
  - The switches, placed the same way, start (-27.89, +15.69) and (-26.39, +18.10).
  - **The port takes the decode.** A-P8 measures only P5, so no capture tells the two
    apart.
- **The switches, which the spec left undecoded (U12, and 2.4's "not decoded"), are in the
  remake's ui3 listing** (`out/parity/specs/ui3/work/static/dec_ui.txt`).
  - **Sound.** `SoundPanelLayer::SoundPanelLayer` (bytes 73444..73806) adds the sound switch
    through `addGlobalSoundSwitch`. `GlobalSoundSwitch::manageSoundSwitch` (bytes
    32340..32535) sets `SetGlobalVolume(1)` or `(0)`, then calls
    `GlobalVolumeManager::saveVolume`.
  - **Music.** `SoundPanelLayer::manageMusicSwitch` (bytes 74288..74908) runs every update.
    With the sound off and a music switch present, it calls `UIButton::dismiss` on it: 700 ms
    out, then removed. With the sound on and none present, it adds a fresh one: a fresh
    entrance.
  - **Both enter as buttons.** ui3's motion.md 1.2 has `UISwitch::UISwitch` calling
    `UIButton::UIButton`.
- **The pause is drawn under the level's blacks, not over them.** `UILayerManager` is a
  controller that `BaseState::preLoop` adds before the first `FadeInController` (step 38's
  `_between`), and every UI layer draws inside it. Spec 2.2's order stops at the texts only
  because the blacks are gone 1.165 s into a level.
- **Every alpha is a byte.**
  - A sprite's is `fTOu(getBias * a)` (`UISprite::update`, bytes 84559..84790), where `a`
    is 200 for the dim and 255 for a plaque.
  - A button's is `fTOu(unfilteredBias * 255)` (`UIButton::setColor`, bytes 31747..31953):
    linear. Only its position is eased.
  - "Part N" takes the resume button's colour with its rgb set to white
    (`GameMenuLayer::draw`, bytes 101852..102330). The golden number takes the first
    button's (`CustomGameMenuLayer::draw`, bytes 281012..281464). Both buttons are on the
    same clock.

**GAME TIME STOPS, AND THE APP DOES NOT KNOW IT HAS.** Not stepping the level is not
enough.
- **What the app runs anyway.** `SupersonicApp` runs `PhysicsSystem::Update` and
  `SpriteAnimationSystem::Update` before every `OnFixedUpdate`, paused or not, so a player
  paused mid-walk would slide on and a falling one would fall.
- **What the layer does about it.** When the pause opens it holds every entity with a
  `RigidBodyComponent`: its transform and its whole body state (velocity, sleep and all).
  It puts them back after each of those steps. It also stops the flipbooks that were
  playing and stops carrying the particles.
- **Where the tick stops.** The pause opens where the tick reads its input: after
  `AfterStep`, before `BeforeStep`. The tick it resumes on runs that `BeforeStep` half, with
  the walk the keys ask for (the pointer is on the button that resumed it). So the level
  takes up exactly where it stopped.
  - `test_mp_layer` walks level0 right for 40 ticks, pauses for 90 with the arrow held,
    resumes, and walks 30 more.
  - A second run walks the same 71 ticks straight through.
  - The two players stand within 1e-3 px of each other at the same level age.
- **Captured.**
  - Two frames of the fresh 1-1 pause, 2 s and 3 s after the tap, differ in **0 of 921,600
    pixels**.
  - The same level unpaused changes 231,632 pixels over those 3 s: the torch, the tutorial
    pulse, the particles.
  - The spec's popup measurement (world temporal std 0.001, section 0.3) says the original
    stands as still.
- **What stops with game time.** The pads are not drawn (`layOutControls`). Restart, pause
  and clear-portals stay drawn, under the dim at their own 120. The level-start plaque
  holds whatever alpha it had, since GameLayer is not updated.
- **Two clocks.** `LevelAgeMs` is game time and stands still.
  - **`LevelFrameMs`, new.** It is the level's age plus every millisecond a pause stood
    it still, and it goes on running.
  - **The caption reads it.** `ETHTextDrawer::Draw` adds the engine's own frame time.
  - **So do the two blacks.** `FadeInController` reads `GetTime()`.
  - **Neither clock stops for a pause in the original**, so a caption still fading when a
    pause opens goes on fading above it.
  - **INFERRED, not captured for a pause** (spec 2.2's last bullet, U3). Until a pause
    opens the two clocks are the same number, so nothing a level already did moved.

**INPUT, AS SPEC 2.5 MAPS IT.**
- **The pause control and Escape** open the pause where the pause control could be pressed:
  a level loaded, not in its door, not dying. `GameState::handleBackButton` (bytes
  98331..98935) shows the pause while GameLayer is current and resumes while GameMenuLayer
  is.
  - A refused level and a chapter's end still go to their grid on Escape.
  - During the 1400 ms finish and death beats Escape now does nothing, where it used to
    leave for the grid.
- **Resume** (`hideMenuPopup`): the whole layer goes on that tick, with no fade. Every
  element is reset (`UILayer::hide(true)`), so the next pause plays its whole entrance
  again.
- **Back to levels** closes the pause and presses the medal screen's `Kind::List`: the grid
  of this level's world, which is `createLevelSelectState`.
- **Skip** is there only on a level already finished (`score > 0`, any tier). It presses
  `Kind::Next`, `goToNextLevel`.
- **Achievements**, kept by the owner's ruling, does nothing visible. It makes the menu
  button's noise, and `PressPause` reports it led nowhere. The AchievementsPopup is U10's.
- **The sound switch.**
  - Off: nothing is played and no music runs. The music switch is dismissed and comes back
    with a fresh entrance.
  - The music switch stops the music.
  - Both hold for the session: `SoundOn()` and `MusicOn()`. **Not persisted**, where the
    original saves the volume.
- **The noises.** The level's buttons make `level_button`, through `PressMenu`
  (`sounds.json` already maps the exit-level and skip-level sounds to that entry). The
  others make `menu_button`. Opening the pause makes none, as the in-level restart makes
  none.
- **Under the dim nothing is pressed.** The tick reads only the pause's buttons while it is
  up (spec 0.5).

**DEV ONLY: `--press <what>@<tick>`** in `main.cpp`, beside `--saves`.
- **What it does.** It presses the pause control (`pause`) or one of the pause's buttons
  (`levels`, `resume`, `skip`, `achievements`, `sound`, `music`) on the layer's tick.
  `MagicPortalsLayer::ScheduleDevPress` takes the press where a tap of the same thing is
  taken, and the log says on which tick.
- **Why.** A `--fixed-step` run takes no input, and every acceptance capture has to open a
  pause, and some mute it and resume it, at a known moment. `--record/--replay` was not
  used.

**CAPTURED AND MEASURED.**
- **Scripts.** `out/parity/ui2/pause/capture_port.sh` and `measure.py` (gitignored), with
  the spec's own helpers (`specs/ui2/work/lib.py`, `fit_text.py`).
- **Frames.** 1280x720, `--fixed-step`, one tick a frame; `--press pause@480` and frame 660
  is 3 s after the tap, as the library's stills are. Fresh 1-1 is paused at 8.0 s, gold 1-4
  (a save with every level's medal gold) at 6.0 s.
- **The dim's twin.** The dim is regressed against the SAME port frame unpaused: frame
  T - 1, because the tick a pause opens stops before it syncs the drawables. That is a
  cleaner pair than the original's two stills 3 s apart, and the port's unported lighting
  cannot enter the tolerance.
- **A-P5's blocks.** The original's column mixes two sources. This script run on the
  original's two stills gives k 0.2149 but blocks 0.1978..0.2390, outside 0.205..0.225,
  because those stills are 3 s apart and the report excluded the torch and the near-blacks
  besides. The port's tight blocks come from the same-frame pair, not from luck.
- **The A-P9 twins.** A second pause opens at tick 700 of a run resumed at 660, so its
  world is regressed against that run's frame 699 (`repause_1-1_twin_699`). A first pass
  used a straight run's frame 661 and read 0.5907 on blocks 0.5858..0.5981; the matched
  pair reads A-P8's numbers exactly. The resumed frame 661 is 481 ticks of game time in,
  and it is pixel-identical to a straight run's frame 481 (`unpaused_1-1_481`); against the
  straight run's frame 661, 3 s further on, 130,215 px differ by more than 8.
- **The binary.** All ten captures were retaken on the final binary and are md5-identical;
  the two A-P9 twins were taken on it after.
  Every run logged `Vulkan validation layers: ACTIVE`, and nothing but the loader's two
  missing Steam manifests.
- **Pairs.** `compare.py`: fresh 1-1 `offset_px (0, 0)`, `edge_iou` 0.779, `mean_abs` 5.71;
  gold 1-4 (0, 0), 0.842, 5.83. Looked at: every pause element lands on the original's; the
  world behind differs by its lighting and torch, as step 44 left them.

| # | check | original | port | tolerance | pass |
|---|---|---|---|---|---|
| A-P1 | fresh 1-1: P3, P4, P7 absent (Pearson at their TL) | none drawn | -0.003, -0.129, 0.094 | < 0.5 | yes |
| A-P1 | P1, P2, P5, P6, P8, P9, P10 present; "Part 1"; "0" | present | all present (Pearson 0.99998..0.99999); "Part 1" and "0" fit | presence exact | yes |
| A-P2 | gold 1-4: P3, P4 `medal_gold_l`, P7 present; "Part 4"; "2" | present | present, Pearson 0.99998..0.99999 | presence exact | yes |
| A-P3 | P2 golden plaque TL | (934.0, 86.25), S 1.407, 0.9992 | **(934.0, 86.4)**, S 1.40625, 0.99999 | ±0.5 px; S ±0.003; >= 0.999 | yes |
| A-P3 | P3 current plaque TL (medal masked) | (166.0, 169.25), 1.406, 0.9995 | (166.0, 169.2), 1.40625, 0.99999 | same | yes |
| A-P3 | P4 medal TL | (166.0, 169.25), 1.4055, 0.9993 | (166.0, 169.2), 1.40625, 0.99998 | same | yes |
| A-P3 | P5 back to levels TL | 0.99929 at (447.6, 226.8) | (447.6, 226.8), 1.40625, 0.99999 | same | yes |
| A-P3 | P6 resume TL | (652.25, 226.75), 0.9994 | (652.4, 226.8), 1.40625, 0.99999 | same | yes |
| A-P3 | P7 skip TL | (550, 414), 0.9993 | (550.0, 414.0), 1.40625, 0.99999 | same | yes |
| A-P3 | P8 Achievements TL | (920, 630), 0.9993 | (920.0, 630.0), 1.40625, 0.99999 | same | yes |
| A-P3 | P9 sound TL | (0, 630), 0.9993 | (0.0, 630.0), 1.40625, 0.99998 | same | yes |
| A-P3 | P10 music TL | (115.25, 630), 0.9991 | (115.2, 630.0), 1.40625, 0.99998 | same | yes |
| A-P4 | "Part 1": font, scale, centre | Matura84_shadow (6.63; next 15.70), 1.405, (652.3, 165.1) | Matura84_shadow (3.82; next 15.47), 1.405, (652.8, 165.6) | ±0.01 font px/px; ±1.0 px of measured | yes (0.5, 0.5) |
| A-P4 | "Part 4" | Matura84_shadow (6.95), 1.405, (651.8, 165.1) | Matura84_shadow (3.96; next 15.02), 1.405, (652.8, 165.6) | same | yes, **at the edge** (1.0, 0.5) |
| A-P4 | golden "0" | Matura84_shadow (7.68), 1.40, (1052.1, 210.95) | Matura84_shadow (4.53; next 11.51), 1.40, (1052.1, 211.95) | same | yes, **at the edge** (0, 1.0) |
| A-P4 | golden "2" | Matura84_shadow (5.70), 1.40, (1051.6, 210.95) | Matura84_shadow (3.71; next 11.46), 1.40, (1052.1, 211.95) | same | yes, **at the edge** (0.5, 1.0) |
| A-P5 | world k, intercept, 80 px blocks | 0.2149, +0.37, 0.212..0.218 (the report's); this script: 0.2149, +0.374, blocks 0.1978..0.2390 (97) | fresh **0.2158**, +0.005, 0.2137..0.2183 (118 blocks); gold **0.2159**, -0.010, 0.2133..0.2186 (106) | 0.2157 ±0.01; ±1 grey; 0.205..0.225 | yes |
| A-P6 | restart / pause high-pass gain under the dim | 0.100 / 0.100; this script 0.0973 / 0.0982 | 0.1025 / 0.1021 (unpaused 0.4743 / 0.4727) | 0.10 ±0.02 | yes |
| A-P6 | pads | 0.010 / -0.025; this script 0.0098 / -0.025 | 0.012 / -0.023 | <= 0.03 | yes |
| A-P7 | muted: P9 `sound_mute` TL; P10 slot | (0, 630), 0.9992; empty | (0.0, 630.0), S 1.40625, 0.99998; music_on 0.061, music_off 0.045 | ±0.5 px; < 0.5 | yes |
| A-P8 | 350 ms: P5 alpha, P5 offset, world gain | decoded 0.50, 9.37 u, 0.590 | 0.4978, 9.32 u (on a 0.5 px search), **0.5924** | ±0.03; ±0.5 u; ±0.03 | yes |
| A-P9 | resume, the frame after | decoded: cut, pads drawn | resume and golden plaque Pearson -0.078 / -0.011; pads high-pass 0.727 / 0.724; identical to a straight run's frame 481 (0 px differ) | absent; pads drawn | yes |
| A-P9 | a second pause, 350 ms in | decoded: replays A-P8 | world **0.5924**, blocks 0.5910..0.5932, P5 alpha 0.4979, 9.32 u: A-P8's numbers | as A-P8 | yes |

- **The port lands on the decode, exactly.**
  - Every sprite fits best at its decoded top-left and at 1.40625.
  - Both texts fit best at their decoded centres.
  - The fits' own grids (0.5 px, 0.005 of scale) are why "Part N" reads 1.405 and the
    number 1.40.
- **The rasterisation offset the captures show is not reproduced** (spec U11). Every text
  of the original's sits about 0.5 px left of and 0.5..1.0 px above its decoded centre, and
  that leaves three A-P4 coordinates at exactly the tolerance's 1.0 px.
  - A-P4 allows it.
  - Its cause is not established, so nothing is tuned toward it.
- **The sprite fit needed a second pass.** Its first refined on a quarter-pixel grid and
  could not land on (447.6, 226.8) or (115.2, 630), so it scored 0.9996..0.9997 beside
  them. `measure.py` now seeds from the decoded placement and descends to 0.05 px.

**TESTS.**
- **`test_mp_hud`: 255 → 417**, pure, against the spec.
  - **What the file says:** the four primitive numbers, the dim's 200 against the three
    measured k, every sprite and font name, and the music switch at 0.09 of the width.
  - **Where the settled gold 1-4 pause puts P2..P10:** each at its decoded top-left to
    0.06 px and within 0.5 px of the measured one, 255 whole. P3 and P4 are told apart by
    their sizes. The dim is first; every sprite comes before every button. At 4:3 the x
    positions move with the width and the sizes stay.
  - **The state gates:** fresh against every tier, the medal by tier, "Part 1" / "Part 4",
    "0" / "2", and both text centres within A-P4's pixel of both measurements.
  - **The switches:** sound off, the music switch's dismissal and its return.
  - **The entrance:** all six start offsets, 32 u out; A-P8's three numbers; linear
    buttons against smoothEnd sprites; home at 700 ms; a button only ever comes in.
  - **What a tap is on,** where the button is on that tick, and never a music switch on its
    way out.
  - **`Hud::LayOutText`** centring on a written font.
  - **Four refusals, each by name.**
- **`test_mp_layer`: 366 → 436.**
  - `EscapeLeavesALevelForItsGrid` (3 checks) becomes `EscapePausesALevelAndResumesIt` (4).
  - **Four new cases:**
    - `ThePauseStopsTheLevelUnderIt`: the pause control opens it over the loaded level. A
      walking player stays put, velocity kept, through two seconds. The level's age stands
      still, the pause's and the frame clock run. No pads. Restart and pause sit under the
      dim at 120. The dim is 200 and whole-view. The golden plaque and the buttons are over
      it; no plaque and no skip on a fresh save. Glyph counts are checked, with the
      level-start caption still fading above it all. A tap on restart under the dim does
      nothing.
    - `ThePauseResumesWhereTheTapLeftIt`: the two runs above. A-P9 on the frame after.
      A second pause starts from nothing and is at A-P8's numbers 21 ticks in.
    - `ThePausesButtonsGoWhereTheOriginalsGo`: no skip on a fresh level. Achievements
      changes nothing. The sound switch, sound_mute, the music switch gone and back, and
      music_off. Back to levels opens world 0's grid of 16.
    - `SkipOnALevelAlreadyFinished`: level0 finished by walking and retried. The pause
      reads the medal just recorded and shows plaque, medal and skip (A-P2). Skip opens
      level1.
  - The baseline was counted by building HEAD's two test files against this source: 255,
    and 366 with the three Escape checks failing as they must.

**NOT BUILT, INFERRED, AND LEFT OPEN.**
- **INFERRED:** the caption and the blacks run on through a pause (U3), described above.
- **INFERRED:** Achievements is drawn on every level. `getAchievementsFromLevel` gates it,
  the port has no achievements table, and every captured level has one (U5).
- **Not built:** the AchievementsPopup (U10).
- **Not built:** the switches' saving (`GlobalVolumeManager::saveVolume`). The port's
  settings live for the session.
- **Not built:** the press bounce `Button::Button` sets up (`setBounce(300, ...)`), which
  the spec does not list.
- **Not built:** the main menu's own switches. ui3's M6 sits at 32 u, a different widget.
- **The dim is drawn as plain black** at alpha 200 rather than as `sprites/square.png`
  tinted black. The file is opaque white in every one of its 64x64 texels, so the two are
  the same arithmetic.
- **U5 is still open:** a bronze-only save, a level with no achievements, and `music_off`
  have no capture of the original. The port's `music_off` shows where the decode puts it.
- **The first frame of the music switch.** The original's music switch is added by the
  layer's first update, so its entrance may start one frame behind the rest. The port
  starts them together.
- **The finished and lost screens are not touched.** Escape during their 1400 ms beats is
  above.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, at `/W4` on the game, the sim library and both suites
  (`games/magicportals/CMakeLists.txt`, `sim/CMakeLists.txt` and `tests/CMakeLists.txt`
  each set it; `build.ninja` carries it on `Pause.cpp.obj`). The seven
  touched translation units were compiled again from scratch for the check, and printed
  none.
- **ctest.** **113 of 113** pass, "Not Run" 0, on the final source. No suite was added.
- **Smart App Control** refused freshly linked executables three times. Each was deleted
  and relinked until nothing was Not Run:
  - `test_json` on the first run;
  - after the final full relink, ten Magic Portals suites, then three, then one;
  - after a verbose recompile, eight, then one.
- **No other document's table moves.** README.md and ARCHITECTURE.md list neither suite's
  checks.

## Step 51 - the level finished and the game over, over a level that goes on running (built)

The medal screen and the lost screen were scene quads, sized by guessed fractions of the
view (buttons 41 u, "level finished" 41 u tall, the medal 77 u, "game over" 56 u), and the
level under both stopped ticking. The HUD vanished at once at the door and at a death, and
the golden-score plaque was drawn for gold alone. This step rebuilds both screens to the
remake's merged spec, `out/parity/specs/ui2/spec.md` sections 3 and 4 and deltas D1..D9
(section 1.2), and holds them to its acceptance lists 6.2 and 6.3. The evidence is
`pause_finished.md` sections 2..3 and `popups_gameover.md` section 4 beside it.
- **Data.** Every number is in `games/magicportals/data/ui.json`, in one new block,
  `level_end`, each entry citing its decode and its measurement:
  - `beats`: the 1400 ms `gameWonDelay` and `gameLostDelay`;
  - `hud`: the pads' `a <- uint(a * 0.98)` decay;
  - `finished`: F1..F12;
  - `lost`: L1..L4.
  - The three constants the layer carried (`kFinishDelayMs`, `kDeathDelayMs`,
    `kCounterStrideMs`) are gone into it.
- **Arithmetic.** It lives in a new pure file, `sim/LevelEnd`: both screens' pieces in the
  original's order of drawing, what a tap is on, `computeScore`, `ScoreCounter`, the pads'
  decay, a HUD button's dismiss, and the veil drawn as a clamped texture. It is built on step
  45's `sim/UiLayer` primitives. `UiLayer::Read` now holds the strict JSON readers that
  `sim/Pause` kept to itself, so both screens refuse a bad number with the same words.
- **Drawing.** Both screens go through `core/ScreenOverlay` in display values (D9), as the
  pause and the HUD do. Nothing of either is in the registry any more. `EmitHud` draws the
  screen after restart, pause and clear-portals and before the level's blacks, the walk pads
  and the caption (spec 3.4's order).
- **Closed:** D1..D9. D10..D12 were step 50's or are the popups'.

**THE DELTAS, AND HOW EACH IS CLOSED.**
- **D1..D5, sizes.**
  - The five buttons are 64 u.
  - `level_finished.png` is 256 x 64 u and the portals plaque 64 x 64 u.
  - The medal is 96 u: `scale(1.5)` of the 64 u one.
  - `game_over.png` is 128 x 128 u.
  - Every piece is placed as the pause's are: the anchor is `view * at_screen`, and the
    top-left is that less `size * origin`. The medal screen's pairs are already turned
    round, last-pushed-first.
- **D6, the golden plaque's gate, inverted.** `CMPIu score, 3; JNS` skips the plaque when the
  final score is 3 or more. So it is drawn for silver and bronze and never for gold, with its
  number at its anchor + (10, 1) u. The comment that argued the old gate ("a plaque claiming
  a medal nobody won") went with it.
- **D7, the level runs on.** `OnFixedUpdate` no longer hands the tick to `menuTick` while
  `Screen::Finished` or `Screen::Dead` is up.
  - **The screen goes first.** Each tick it advances its own clock and counters and reads a
    tap (`endScreenTick`). A button that leaves the level leaves it there.
  - **Then the level's own tick.** `AfterStep` and `stepLevel` run with a walk of 0: the
    camera, the no-portal sign, the flipbooks, the particles and the sounds all go on.
  - **Nothing is pressable from the door or the death on** (spec 3.5): no walk, no shot, no
    control, no pause. The beat used to read the keys and a tap, so a tap in the doorway
    fired a portal.
  - **Once either beat has begun, the other cannot.** The branches are now ordered finishing,
    dying, door, death. Reaching the exit on the same tick as a death still wins.
  - **The camera stays put.** In level0 the wall under the medal changes by 0.000 grey over
    1.6 s (A-F11), and the tick-level suite finds the screen 84 ticks after the door.
- **D8, the HUD as a level ends.** `layOutControls` lays out an ended level as well as a
  playing one. `ControlButton` carries its alpha, and `hideHud` is gone.
  - **At the door**, restart, pause and clear-portals are CUT on the door tick.
  - **At a death**, they are DISMISSED as UIButtons (`LevelEnd::HudDismissed`): 700 ms,
    alpha 120/255 x byte(1 - smoothEnd), sliding 32 u out along the ray from the screen
    centre through each one's ANCHOR, the corner its placement is measured from (step 50's
    rule). `Button::draw` (bytes 15237..15660) multiplies the custom colour into the
    UIButton's own.
  - **The pads**, either way, decay from the byte they were last drawn with
    (`Hud::PadAlphaByte`, new), `uint(a * 0.98)` a tick. The pulse and the tutorial ring stop.
  - **U1 is not resolved.** The decode predicts the dismiss both ways and the door measured a
    cut, so both measurements are followed, as the spec asks.
  - **U2, decided: per 60 Hz tick.** Under `--fixed-step` a tick is a drawn frame, and a
    60 fps device draws the same count.
- **D9.** Display-space throughout, as above.

**THE VEIL, AND THE SAMPLER THAT REPEATS.**
- **The texture.** `fade_edge.png` is 64 x 16 texels, black. Every row is the same alpha
  ramp, 255 at the left to 0 at the right. It is stretched 1920 px (F1) or 1152 px (L1)
  wide.
- **The problem.** The overlay binds the texture registry's sampler, which is `eRepeat`
  (`VulkanImage::CreateSampler`'s default). Magnified 30x, bilinear filtering would blend
  the opaque first texel with the clear last one across the leftmost 15 px. L1's right
  edge is on screen too, at 1152.
- **What the port does.** `LevelEnd::ClampedStrips` draws the stretch as three quads:
  - a half-texel strip at each end showing only that edge texel's centre;
  - between them, the texture from the first texel's centre to the last one's.
  - That is exactly a clamped linear sample, which is what the spec's decoded profile
    assumes (`np.interp` over texel centres).
- **No engine file changes.** A clamp sampler for overlay textures was the alternative; it
  would have touched the renderer, which another branch is changing now.

**THE COUNTER, AS SCORECOUNTER COUNTS.** `ScoreCounter::update` (bytes 39728..39922) adds
the frame's time to a Timer. Only once that reaches the stride, and while `current != end`,
does it reset the Timer to zero and step one. The remainder is dropped, which at the port's
60 Hz is nothing. `LevelEnd::Counter` is that, ticked from t0 by `endScreenTick`, for the
portals and for the crystals (F12's `n`). The medal is `computeScore` of the portal count
where it stands, every frame. `MedalFor` and the saved medal go through the same
`LevelEnd::ComputeScore`.

**WHAT ELSE CHANGED.**
- **The text lines.** The engine's own "1-1 cleared with 0 portals - gold" and "Click a
  button" lines are no longer written over a level under either screen. The original draws
  its own count there.
- **`MenuButtons()`** still lists Retry, Next and List for both screens, at their settled
  places in the level's pixels, so `PressMenu` and the suites work as before. A tap is
  tested against where each button is on its tick, entrance and all.
- **`Hud::LayOutTextFrom`**, new, lays text out from a top-left corner: F12 is `DrawText`,
  not centred. `LayOutText` now calls it.
- **Logged with the tick.** The door, the death and each screen becoming current are
  written to the log, so a capture can be dated.
- **`Pause::LoadRules`'s `units_per_font_px` refusal** now reads "missing, not a number, or
  not above zero", the shared reader's words. The refusal checks test only the key's name.

**DEV ONLY: `--hold <left|right>@<from>-<to>`** in `main.cpp`, beside `--press`.
- **What it does.** It holds a walk over a span of the layer's ticks
  (`MagicPortalsLayer::ScheduleDevHold`, read in `keyDirection`).
- **Why.** A `--fixed-step` run takes no input, and the finished and lost captures need the
  player walked into a door or a hazard at a known tick. `--record/--replay` was not used.
- **The runs:**
  - level0 held right from tick 1 reaches its door on tick 193, pads at 180; the medal is
    current on tick 277;
  - 2-10 (level9a) held right from tick 260 dies on tick 281, pads at 120; the lost screen
    is current on 365;
  - 2-32 (level31a), idle, dies on tick 287; its screen is current on 371.

**CAPTURED AND MEASURED.**
- **Scripts.** `out/parity/ui2/finished/capture_port.sh` and `measure.py` (gitignored), with
  the spec's own helpers (`specs/ui2/work/lib.py`, `fit_text.py`) and `tools/parity`'s
  `locate.py` and `compare.py`.
- **Frames.** 83 captures at 1280x720, `--fixed-step`, one tick a frame. Every run gets its
  own saves directory, because a finish writes a medal: fresh for level0, the all-gold save
  for 2-10 and 2-32.
  - Frame F is (F - t0) / 60 s after t0.
  - The settled finished frame is 457 (t0 + 3.0 s); the settled lost frames are 485 (2-10)
    and 491 (2-32), t0 + 2.0 s.
  - The recording's own instants (A-F9 at 0.69 s, A-F10) fall between ticks, so the port is
    read on the ticks either side and interpolated.
- **The veil's twin.** No same-frame twin exists: the world is running. The reference is the
  median of three pre-screen frames (253, 270, 276; 2-10: 350, 360, 364). The settled
  image is the median of three (457, 469, 481; 485, 497, 533). Columns are regressed over
  pixels whose temporal std is under 4 in both sets, the UI and the pad corners masked, as
  `dim_profile.py` did.
- **Alphas.** Each is a composite fit, `frame = base + alpha (sprite - base)`, over the
  sprite's opaque texels. The base is the reference dimmed by the veil as it stood that
  frame, the veil's own fraction fitted first. Button slides are the distance along each
  ray at which that fit is best.
- **The pads.** The world under a pad is static until t0, and the start alpha is the byte
  the port logged. Every later alpha then follows from `F_k - F_0 = (a_k - a_0) A (P - B)`.
  - level0's left pad sits over a static portal's animated glow and reads up to 0.079 off
    from tick 20 on. That cannot be the pad: both pads are drawn in one loop from one byte
    (`m_padEndByte`) at one alpha, so the port has no way to put them apart. The residual is
    the glow under the template, and the right pad, over still wall, is quoted.
  - 2-10's left pad is quoted for the same reason.
- **The binary.** Three captures (fin 457, 2-32 491, 2-10 302) were retaken on the final
  binary and are md5-identical. All 83 logs say `Vulkan validation layers: ACTIVE`, and
  none has an error or a warning.
- **Pairs** (`compare.py`), each looked at:
  - finished 1-1 `offset_px (0, 0)`, `edge_iou` 0.780, `mean_abs` 7.55: every piece lands on
    the original's, and the world differs by its unported lighting;
  - 2-32 (0, 0), 0.394, 25.23: the UI lands exactly; the port's level31a sky leaves the
    right half black, which is in the pre-screen frame too, the world's and not the UI's;
  - 2-10 (0, 0), 0.395, 11.91: the UI lands exactly. The original's frame (n190) still shows
    its pads, which at the emulator's 20 fps outlive t0 (spec U2).

| # | check | original | port | tolerance | pass |
|---|---|---|---|---|---|
| A-F1 | F2 level finished TL, S, Pearson | (234.0, 110.25), 1.406, 0.909 (this script: 0.915 at the decoded TL) | (233.87, 110.16), 1.40625, 0.987 | ±0.5 px; ±0.003; >= 0.90 | yes |
| A-F1 | F3 portals plaque (medal box masked) | (511.5, 414.0), 0.914 (this script: 0.877) | (511.6, 414.0), 1.40625, 0.998 | ±0.5 px; ±0.003; >= 0.91 | yes |
| A-F1 | F5 / F6 / F7 TL | (870, 90) / (870, 270) / (870, 450), 0.9993 | exactly those, 1.40625, 0.99999 each | ±0.5 px; >= 0.999 | yes |
| A-F1 | F8 medal TL, scale | (467.0, 261.25), 2.104 (this script: (466.65, 261.0), 2.109, 0.9995) | (466.6, 261.0), 2.10938, 0.99992 | ±1.0 px; 2.109 ±0.01 | yes |
| A-F2 | F9 "0": font, scale, centre | Matura128_shadow (rms 6.08), 1.405, (601.1, 373.9) | Matura128_shadow (2.87; next Matura64 14.49), 1.405, (601.6, 374.9) | ±1.0 px of the measured | yes, **at the edge** (0.5, 1.0) |
| A-F3 | F4 at its TL, F10 | absent | Pearson -0.020; no golden number | < 0.5 | yes |
| A-F4 | F1 k(x), 20 px columns, x <= 1195 | rms 0.0063 against the decode | rms **0.00106** over 60 columns; 1210 px reads 0.3839 against 0.3833 | rms <= 0.012 | yes |
| A-F4 | 10 px columns 1065..1195 | 0.0099 rms residual | every column within -0.0013..+0.0010 of the decode | ±0.02 each | yes |
| A-F5 | plateau opacities F2, F3, F5-F8 | 0.997..1.020 | composite F2 0.997, F3 0.992, buttons 0.9997; high-pass F8 1.005, buttons 0.999 | 1.00 ±0.02 | yes |
| A-F6 | restart / pause gain, player | 0.4705 -> -0.034 on the next frame | 0.473 / 0.472 on tick 192, **0.003 / 0.002 on the door frame 193**; 11,908 px change about the door 192 -> 193 against 583 after | <= 0.03 by the next frame | yes |
| A-F7 | pad alpha a tick at a time from 180 | rms 0.005 from 180 | right pad max \|diff\| **0.0013** over ticks 0..83; the suite: gone 99 ticks on | ±0.02 a frame; 81/99/106 ±1 | yes |
| A-F8 | t0 - door | 1.41..1.50 s | door tick 193, screen tick 277: 84 ticks, **1400 ms**; fitted T with t0 277 is 1.005 | 1400 ms ±1 tick | yes |
| A-F9 | at t0 + 0.1 / 0.35 / 0.69 / 1.0 s | model sin 0.156 / 0.522 / 0.884 / 1.0, lin 0.143 / 0.50 / 0.986 / 1.0 | veil 0.152 / 0.520 / 0.881 / 1.000; F2 0.152 / 0.520 / 0.881 / 0.997; F3 0.179 / 0.529 / 0.876 / 0.992; F5 0.140 / 0.497 / 0.983 / 1.000 | ±0.03 | yes |
| A-F9 | best-fit T | sin 0.985..0.995, lin 0.680..0.695 | veil 1.005, F2 1.01, F3 1.00; buttons linear 0.705 | 1.00 ±0.08; 0.70 ±0.03 | yes |
| A-F10 | slide at 0.022 / 0.257 / 0.429 / 0.527 / 0.656 s | 83.5 / 39 / 15.5 / 5 / 0 px | 85.5 / 41.0 / 16.0 / 6.6 / 0.5 px (the 32 u model at those instants: 85.6 / 40.9 / 16.1 / 6.7 / 0.4) | ±3 px each | yes |
| A-F10 | rms over the ramp against the 32 u model | 1.17..1.21 px | F5 0.149, F6 0.149, F7 0.171 px | <= 1.3 px | yes |
| A-F11 | 0.8 s mean abs diff, t0 + 1.6 -> 2.4 -> 3.2 s | flame 4.98 / 4.67, portal 4.31 / 8.47, wall 0.00 / 0.02 | door flame 6.55 / 7.08, left portal 2.92 / 2.04, mid portal 2.59 / 1.47; wall 0.000 / 0.000; buttons 0.000 / 0.000 | > 1 grey; < 0.1 | yes |
| A-F12 | counter and medal past golden + 2 | decode only | not captured; `test_mp_levelend` pins 6 portals against 1 at mid-stride, 0 1 2 3 4 5 6 6 6, gold -> silver -> bronze; `test_mp_layer` reads 0 1 2 2 at 50 / 150 / 250 / 500 ms on 1-9's two portals | exact at mid-stride | pinned, not captured |
| A-G1 | L2 / L3 / L4 centres, 2-32 settled | (640, 252) / (512, 432) / (768, 432); locate 0.9909 / 0.9959 / 0.9908 | exactly those (L2 fits (639.95, 252.0)); locate 0.9949 / 0.9953 / 0.9934; Pearson 0.988 / 0.99999 / 0.99999 | ±0.5 px; >= 0.985 | yes |
| A-G2 | L1 column gains, x 20..1140 | 0.29 / 0.34 / 0.576 / 0.68 / 0.769 / 0.826 / 0.925 / 0.979 / 0.997 | 0.296 / 0.344 / 0.568 / 0.658 / 0.734 / 0.846 / 0.921 / 0.960 / 0.998; every column within 0.001 of the decode | ±0.04 of the decode | yes |
| A-G3 | L1 / L2 sin T, L3 / L4 linear T, slide | 0.995; 0.700; rms 1.27 px; 78 px at 48 ms | 1.005 / 1.005; 0.700 / 0.705; slide rms 0.118 / 0.122 px; 80.0 px at 50 ms (tick 3; model 79.9) | ±0.08; ±0.03; <= 1.3 px; ±3 px | yes |
| A-G4 | restart / pause dismissal from the death tick | 0.475 -> 0 by 1 - smoothEnd, >= 28 u | pause 0.470 / 0.452 / 0.417 / 0.365 / 0.265 / 0.136 / 0.046 at ticks 0 / 1 / 3 / 6 / 12 / 21 / 30 (model within 0.0007); travel 0 / 1.24 / 3.56 / 7.11 / 13.87 / 22.58 / **28.8 u**; tick 39's alpha is one byte (0.0018), gone from tick 40 | ±0.03 a frame; >= 28 u | yes |
| A-G5 | pads from 120 | rms 0.0069; 0 after 81 | left pad rms **0.0003** (max 0.0006), 0.0004 at tick 83; the suite: gone by t0 | rms <= 0.01; 81 ±1 | yes |
| A-G6 | death -> lost screen | 1.438 s / 1.349 s | 2-10 ticks 281 -> 365, 2-32 287 -> 371: 84 ticks, **1400 ms** | 1400 ms ±1 tick | yes |
| A-G7 | the world under L1 after t0 + 1.0 s | the fire agent, the dragon move | 2-10 t0 + 2.0 -> 2.8 s: fire agent 4.85 grey; wall 0.000; UI 0.033 | > 1 grey | yes |

- **The port lands on the decode.** Every sprite fits best at its decoded top-left and
  scale; the two titles, F2 and L2, sit 0.05 px left of it on a 0.05 px search. Both veils
  follow the decoded profile to 0.0013.
- **The text offset U11 is not reproduced.** F9's centre sits at exactly A-F2's 1.0 px from
  the measured one, as step 50's texts did.
- **The door effect** (`red_suck_effect.ent`, `red_sparkles.ent`) that the original plays at
  the door is not in the port. It is a world effect, not this screen.

**TESTS.**
- **New suite `test_mp_levelend`: 226 checks,** pure, and it never skips.
  - **What the file says:** every number above, and D1..D6 by name.
  - **Where the settled finished screen puts F2, F3, F5..F8:** each at its decoded
    top-left to 0.06 px and within A-F1 of the measured one. F9 is at its decoded centre, and
    within A-F2's pixel give or take the 0.006 the decode itself sits off.
  - **What the play earns:** F4 and F10 drawn for a final score under 3 and never for gold;
    F11 and F12 from their corners, F12 not centred.
  - **The counter at 60 Hz:** mid-stride at every step, a step on the sixth tick, holding at
    its end, and the medal following it.
  - **The entrances:** A-F9's four instants, A-F10's five, all start offsets, and A-G3's
    first frame.
  - **The lost screen:** A-G1's centres.
  - **Taps:** where the button is on its tick.
  - **The veil's three strips:** 15 px end strips, and u = x / 1920 across the middle.
  - **The HUD:** the decays from 120 / 180 / 210 gone after 81 / 99 / 106, the anchors,
    and A-G4 a tick at a time.
  - **Six refusals, each by name.**
  - A first run caught the suite itself taking a piece out of a temporary list, which
    dangles. `Find` on a temporary is now deleted.
- **`test_mp_layer`: 436 -> 478.**
  - **`FinishingALevelShowsTheMedal`:** "3 button quads" becomes "no button quads; three
    button pictures in the frame", a tick into the entrance, since nothing is sent at alpha 0.
  - **`TheMedalScreenIsTheOriginals`:** the tagged-quad and material checks become frame
    checks. The veil is three `fade_edge.png` strips at 200/255 reaching 1.5 views; then the
    banner, the plaque, the buttons, the medal; one counter glyph; no restart or pause. After
    detach, nothing.
  - **`APlaqueForALevelWithAMedal`:** "nothing over the medal" becomes "nothing of GameLayer
    over it".
  - **`Level8FromTheSpawnWithTapsAndWalking`:** adds the counter on the screen's own clock
    and D6 on gold. It also adds F11 and F12 through the layer, the one path the pure suite
    cannot reach: 1-9's three crystals count to 3 by 500 ms, and the frame holds
    `crystal.png` (from `entities/hd`) after the medal and three `Matura84_shadow` glyphs,
    which on gold, with no golden number, can only be "3/3".
  - **`AFallOutOfTheLevelIsADeath`:** D7 with a body still falling, where the three captures
    all had bodies at rest. The lost screen is up 84 ticks on. 316 ticks later the body is
    94.8 u lower and the camera has moved 0.00009 px, the follow closing its last fraction
    on the x the test teleported the body to. `Camera::Clamp` holds the follow inside the
    bounds and the death is past them, so nothing pans under the screen. The original never
    filmed a fall death (U9), so there is nothing to fit it to.
  - **Two new cases:**
    - `TheDoorCutsTheHudAndTheLevelRunsOnUnderTheMedal`: the door tick has no restart or
      pause, and the pads are at their pulse byte. The byte decays exactly and is gone 99
      ticks on; the medal comes up 84 ticks on. A second under it is a second of the level's
      age. Escape and a stray tap do nothing, and a tap on restart replays.
    - `ADeathDismissesTheHudAndTheLostScreenComesIn`: whole and home on the death tick; at
      350 ms, 120/255 x (1 - sin(pi/4)) and moving out; gone at 700. The lost screen comes up
      84 ticks on, pads gone. Veil, title, restart, list, in that order at 180 and 0.9 views.
      The level runs on, and a tap on L3 replays.
- **`test_mp_hud`: 417**, unchanged.

**NOT BUILT, INFERRED, AND LEFT OPEN.**
- **INFERRED:** clear-portals is dismissed with restart and pause at a death, and cut with
  them at the door. The spec's recordings had no portal placed.
- **INFERRED:** the pads decay from the byte last drawn, and the door tick's own frame shows
  it undecayed. The ±1 frame of A-F7 and A-G5 covers the other reading.
- **Not built:** the door's suck-and-sparkle effect; the fall death's skull and the other
  death effects (U9).
- **Not built:** a scene `end_delay` override of the 1400 ms. No level places one.
- **Not built:** the level-start plaque at the end. It keeps its own timeline, and every
  captured end comes after it has gone.
- **Decode only:** silver, bronze, the live downgrade, F4/F10 and F11/F12 on a real play
  (U4). The crystal's size and F12's placement are unmeasured. F11/F12 are drawn through
  the layer in the suite (1-9) but were not captured.
- **Escape over either screen does nothing**, as before; `handleBackButton` for those layers
  was not read.
- **The finished screen's last 80 px** (x > 1200) were unmeasurable in the original. The
  port's 1210 px column reads the decoded 0.383.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, at `/W4` on the game, the sim library and every suite
  (`build.ninja` carries it on `LevelEnd.cpp.obj`). The nine touched translation units were
  compiled again from scratch for the check (`LevelEnd`, `UiLayer`, `Pause`, `Hud`, the
  layer, `main`, and the three suites), and printed none. After review, `test_mp_layer.cpp`
  gained seven checks and was compiled again, with no warning; a full build then had no
  work to do.
- **ctest.** **114 of 114** pass (113 and the new suite), "Not Run" 0, on the final source,
  after the review's checks too.
- **Smart App Control** refused freshly linked executables. Each was deleted and relinked
  until nothing was Not Run:
  - the game twice, once after a rebuild and once after the final recompile;
  - five suites on the first full run, which the next relink cleared;
  - after the final recompile, eleven, then three, then two.
- **No other document's table moves.** Only this plan lists the suites' checks.

## Step 52 - the tutorial and help popups, over a level whose game time stands still (built)

The port had no popups. 1-02 and 1-03 opened straight into play, and a help block was a
"?" in the world that did nothing when touched. Step 40 declined the HUD over the popups of
1-2 and 1-3 because the popup was its own system, and said the HUD would hide with its layer
when that was built. This step builds it, to the remake's merged spec,
`out/parity/specs/ui2/spec.md` section 5 (with sections 0 and 1), and holds it to its
acceptance list 6.4. The evidence is `popups_gameover.md` sections 1..3 and the recordings
under `rec/` beside it.
- **Data.** Every number is in `games/magicportals/data/ui.json`, in one new block, `popups`,
  each entry citing its decode and its measurement:
  - `dim`, `card`, `close_button`: H1..H3;
  - `highlight`: the close button's bounce and blink;
  - `level_start`: 1-02 and 1-03;
  - `help_block`: the entity, its box scale, the 12 of the move gate, and the 13 scenes;
  - `classes`: eight demonstrations, every waypoint, static and sheet of each.
- **Arithmetic.** It lives in a new pure file, `sim/Popup`: the rules and their refusals,
  the loops, a popup's clock and its close, its pieces in the original's order of drawing,
  the close button's state, and a help block's rectangle. Three small additions beside it:
  - `UiLayer::SpriteDismissAlphaByte`, a UISprite `ms` after `dismiss`;
  - `Hud::PlaqueAlphaFrom`, the plaque dismissed at a stated age; `PlaqueAlpha` is now
    that with the dismissal where an unstopped level has it, the same values;
  - `LevelEnd::HudEntered`, a GameLayer button `ms` into its UIButton entrance, the mirror
    of step 51's `HudDismissed`.
- **Drawing.** Through `core/ScreenOverlay` in display values, as the pause and the end
  screens are. `EmitHud` draws the popup after the pause's section and before the end
  screens, the level's blacks and the caption, so "Part N" lands over it (spec 5.4).
- **Closed:** step 40's declined item. Restart, pause and the plaque are not drawn under
  the two level-start popups, by the layer rule and not by a level name.

**WHICH LEVELS, AND WHEN.**
- **Level start.** `Game::managePopups` (bytes 115157..115478) is called from
  `Game::preLoop`, so on every load, a retry included. `loadLevel` opens `LevelHelp1Popup`
  on level1 and `LevelHelp2Popup` on level2 once the drawables are synced, when the
  original art and the HUD are ready; a run without the art opens none.
- **A retry stands at age 0.** The retry and skip keys load the level mid-tick, and that
  tick used to go on to `AfterStep` and a tick of age. A level that has just raised its
  popup now stops there, as one loaded from a menu does, and `test_mp_layer` pins age 0
  after the retry and after a tick more. The same stop guards the skip key landing on 1-02
  or 1-03 from the level before; that path is not tested or captured.
- **Help blocks.** `HelpBlockController::update` (bytes 226317..228622), skipped while game
  time is stopped:
  - the rectangle is the entity's 38 x 38 collision box x `scale(1.2)`, centred at its
    position less the camera (`Popup::HelpBlockRect`);
  - a touch down in it disables the level's touches, so that touch fires no portal;
  - the longest move is kept, and a release that moved less than 12 opens the scene's
    class. The 12 is read as window pixels (spec U8: its units are unmeasured). A move of
    14 px opens nothing in the suite;
  - the controls are asked first, as GameLayer's buttons are above the controller.
- **The 13 scenes** are the spec's table: 1-02, 1-03, 1-10, 1-11, 1-13, 1-19, 1-25, 1-32,
  2-03, 2-06, 3-09, 4-01, 4-02. Probed at tick 180, the blocks sit at (units of the view,
  455 wide): level9 (200, 152), level10 (190, 60), level12 (45, 102.7), level18 (222.1,
  84.7), level24 (295.1, 119.7), level2a (335.1, 77), level5a (32, 55); level31, level8b,
  level0c and level1c are off the view there.
- **4-02 is NOT BUILT.** `SpaceEasterEggHelpPopup`, with the owner's video-button ruling,
  has no content and, `tapScreenToClose` false, no way out. Spec U6 asks for an owner ruling,
  not a guess. `help_block.levels` gives level1c `popup: null`: its block takes the touch,
  fires no portal, logs "its popup is not built" and opens nothing.
- **Achievements 62 and 63**, which the controller dispatches before 4-01's and 4-02's
  popups, are not dispatched: the port has no achievement system.

**THE FRAMEWORK, AS DECODED.**
- **H1**, the dim: `eth_framework_square.png` over the screen in ARGB(150, 0, 0, 0), a
  UISprite, alpha byte(smoothEnd(t / 1000 ms) x 150). The file is opaque white, so the port
  draws the black plainly, as the pause's dim.
- **H2**, the card: `sprites/help_popup.png` (1x only), 340 x 256 u about the screen's
  centre, a UISprite on the same curve; top-left (161.875, 0) px at 720p.
- **H3**, the close button: `hd/popup_close_button.png`, 64 u at `PORTAL_HELP_POPUP_POS`
  (0.92, 0.5), a UIButton: alpha linear over 700 ms, sliding in 32 u along the ray from the
  screen's centre, eased by smoothEnd. Decoded centre 0.92 x 1280 = 1177.6 px.
- **Bounce and blink.** `setButtonHighlightEffects(button, 0.03, 150)` (bytes
  370863..371164), arguments pushed last-first:
  - bounce from A = (0.97, 1.03) to B = (1.03, 0.97), a 400 ms stride, smoothEnd, reversed
    on odd strides;
  - blink from brightness 0.95 to 1.0, a 150 ms stride, linear, into the rgb only;
  - both on the button's own elapsed time from its creation, and both stop at the close
    (`UIButton::update` calls `Button::update` only while not dismissed).
  - **Which end is A** was settled by the recording: sx is 1.03 near 2.02 and 2.80 s after
    the tap and 0.97 near 2.38 and 3.18 s, the odd and even multiples of 400 ms.
- **The demonstration.** Each class's `draw` calls `UILayer::draw` first, then its tracks
  and statics:
  - the tracks are WaypointSprites, each in its own colour, and not drawn at all once the
    close button is dismissed;
  - the statics are `drawScaledSprite` at `HelpPopup::computePos` = screen x (0.5 + at),
    tinted with the close button's colour with its rgb set white. So they come in with the
    button's linear alpha, without its blink, and are at alpha 0 from the close;
  - a sheet (the chara) steps its frames on its own FrameTimer, 150 ms.
- **Draw order: the decode, not spec 5.2's list.** Spec 5.2 lists H3 after the
  demonstration; `UILayer::draw` draws it before, and the port follows the decode. The
  button's opaque texels start near x 1130 px at its widest and the card's end near 1082,
  so the orders differ only where a sprite crosses the button: 1-02's chara walks out to
  x 1152 at y 309.6 as it fades to alpha 0, over the button's top edge. 2-03's fireball
  ends at x 1152 too, above and below the button.
- **Angles are counter-clockwise on the screen.** `GLES2Sprite::DrawOptimal` turns by
  `RotateZ(-angle)` (Ethanon `GLES2Sprite.cpp:268-270`). The arrow's 23 degrees pointing up
  and right between the two portals, and the anti-portal projectile's 200 degrees heading
  left, confirm the sign on the recordings.
- **The close.** A touch **down** anywhere (`tapScreenToClose`, instructions 45..47;
  `Popup::hasReceivedCloseCommand`, bytes 73196..73437) or the back key (GSK_BACK). The
  tracks go on that frame; the button dismisses over 700 ms, the dim and the card over
  1000 ms from whole (`UISprite::dismiss` resets its timer). The popup is gone once all
  have, 60 ticks, and game time resumes on that tick (`Popup::update`).
- **Pages.** One each. No class has paging code; the "arrow" is the close button.

**THE LOOPS, AS A FRAMETIMER STEPS THEM.** `WaypointManager` (bytes 40242..42676) adds the
frame's time and steps ONE waypoint at most a call when it reaches the stride, keeping the
remainder. A 0 ms stride is a jump that still takes its call. The bias is min(t, stride) /
max(stride, 1) through the waypoint's filter (linear, smoothEnd, smoothBeginning), and
position, alpha and angle all follow it; past the last it wraps. `Popup::Loop` is that, a
60 Hz tick a call. Every decoded loop is a whole number of ticks (5500, 6400, 3500, 2200,
4000, 6500 ms = 330, 384, 210, 132, 240, 390), and each class repeats exactly that many
ticks later and at no lag a tick either side.

**THE HUD UNDER IT, AND AFTER IT.**
- **Game time stops** (`GameTimeStopped()`: a pause or a popup). The world is held as the
  pause holds it: step 50's held state is now `m_frozen`, with `freezeWorld` and
  `thawWorld` shared by both. The emitters, the flipbooks and the level's age stop. The
  level's frame clock does not, so the caption goes on fading (spec 0.3).
- **The pads are not drawn** while game time is stopped, and nothing under the popup is
  pressable. Escape closes the popup and does not pause.
- **Over a help block's popup**, restart and pause stay drawn, frozen, under the dim.
- **Under a level-start popup** GameLayer has not had an update:
  - restart and pause now come in as UIButtons on the level's AGE
    (`LevelEnd::HudEntered`): 700 ms, linear alpha x their 120 byte, sliding 32 u in along
    the ray through each one's anchor. On every other level that entrance runs under the
    opening black (`overlay.start_after_ms` 700) and draws as before;
  - the pads slide in on the age, as they always did;
  - **the plaque**: `Game::dismissCurrentMedalSprite` dismisses it once the FRAME clock
    passes 2000 ms, which a popup does not stop, and `UISprite::dismiss` resets its timer,
    which only GameLayer's updates run. So after a popup of more than 2 s it is whole on the
    first update and fades out over 1000 ms (`tickPlaqueDismissal`,
    `Hud::PlaqueAlphaFrom`). The mechanism is INFERRED from the decode; the timings are
    A-H9's.
- **The same pixel a tick later.** On the popup-gone tick the port draws nothing of
  GameLayer yet, and its first update is the next tick. The original starts them on the
  gone frame (n265). One tick, inside A-H9.
- **A help block's popup** is raised mid-tick, after the step. The tick it goes away runs
  the rest of the tick, with the keys' walk, as the pause's resume does, so a level played
  through a popup is the level played without one (the suite compares the two runs).

**AN ENGINE CHANGE, AND WHAT IT COSTS THE MERGE.** The demonstrations turn sprites: the arrow
23 degrees, the wall 90, the dust -35..35, the rolling stone and the fireball through 180,
the projectile 200. A
`ScreenOverlay::Quad` is a min and a max, which cannot say a turn, so:
- **`core/ScreenOverlay`**: `Quad::basis`, a 2x2 in fractions applied to each corner's offset
  from the quad's centre (identity by default), and `ScreenOverlay::Rotation(radians,
  aspect)`, the turn in square pixels taken into fractions. `CornerOf` applies it.
- **`screen_overlay.vert`**: a fourth `vec4 basis` in the push block and the same branch;
  `screen_overlay_vert.spv` rebuilt (the `Shaders` target).
- **`VulkanPipeline.hpp`**: `ScreenOverlayPushConstants` gains `basis`, 48 -> 64 bytes.
  **`VulkanRenderer.cpp`**: one line sends it.
- **An unturned quad keeps the arithmetic it had, to the bit**: both sides branch on the
  identity.
- **This departs from step 51's "no engine file changes".** `ScreenOverlay.hpp/.cpp` and the
  shader are byte-identical to the main tree's today. `VulkanPipeline.hpp` and
  `VulkanRenderer.cpp` are being changed there by the lighting work, so the merge cost is
  those two files: one struct member and one line.
- **`test_screenoverlay` 46 -> 57**: a quad turned 90 degrees lands on its four pixels, 23
  degrees keeps every corner's distance, identity and the plain arithmetic are unchanged,
  the push block is 64 bytes, and the shader carries `vec4 basis` after `vec4 color` with
  `CornerOf`'s branch.

**DEV ONLY: `--tap <help|x,y>@<tick>`** in `main.cpp`, beside `--press` and `--hold`.
- **What it does.** A touch down on the tick and up on the next
  (`MagicPortalsLayer::ScheduleDevTap`, read in `touchThisTick`): on the level's first help
  block where the camera has it, or at pixel x, y of 1280 x 720.
- **Why.** The help popups open from a tap and close from a touch down, and a
  `--fixed-step` run takes no input.
- **Its warts.** A scheduled tap owns the pointer on its ticks, so it cannot press a HUD
  control. A `help@` tap with no block on the view is dropped with a warning.
- **The runs:** 1-13 tapped at 300: popup opened on 301; closed by 640,200@601, gone on
  661. 1-02 closed by 640,200@300: gone on 360.

**CAPTURED AND MEASURED.**
- **Scripts.** `out/parity/ui2/popups/capture_port.sh` and `measure.py` (gitignored), with
  the spec's `specs/ui2/work/lib.py` and `fit_text.py`, and `tools/parity`'s `compare.py`.
  `date_still.py` dated the library stills against a sweep of port frames.
- **Frames.** 324 captures at 1280x720, `--fixed-step`, one tick a frame, every run with its
  own saves (fresh; the all-gold save for 1-02's close).
  - 1-02 and 1-03 open in the load, so frame F is F / 60 s into the popup.
  - The library stills were dated by the demonstration: the hand whole at its fingertip,
    the arrow and left portal not in yet. The demo-region difference bottoms out over
    frames 86..102 on both levels; frame 96 (1.6 s) is used for every still row, its hand
    at (384.0, 396.0) against the original's (384.0, 395.9) and (384.0, 395.4).
  - A help block tapped at T opens on T + 1, and frame F is (F - T - 1) / 60 s in.
- **Masks and fits.**
  - A dim is `frame = k twin + c` over world pixels whose std across three unpopped frames
    (280, 290, 300) is under 2, the card, the button's travel, restart, pause and the pad
    corners masked.
  - An alpha is a composite fit, `frame = base + alpha (sprite - base)`, the base being
    the twin dimmed and carded as that frame has them. The close button's alpha is fitted
    with its brightness free, since the blink runs from its first tick.
  - Sprite centres and scales are a Pearson search against the template (`fit`). The card
    is fitted over its border band with the demonstration, caption and button masked.
    `locate.py`'s masked CCORR under-reads these sprites about 0.9x (spec 5.5's own
    finding), so it is not the size measurement here, as in steps 50 and 51.
- **Pairs** (`compare.py`), each looked at:
  - 1-02, frame 96: `offset_px (0, 0)`, `edge_iou` 0.636, `mean_abs` 7.49. The card, the
    hand, the stones, the portal, the button and "Part 2" land on the original's; the world
    around differs by the camera's start and the unported lighting, and the port shows the
    help block the original's camera does not;
  - 1-03, frame 96: (0, 0), 0.552, 9.55; the same;
  - 1-13, `rec/help_1-13_frames/0214_012234ms.png` (1.954 s after the fitted loop origin)
    against port frame 418: (0, 0), 0.551, 6.78. The stone on its wall, the card, the
    button, the dimmed restart and pause and the help block all coincide.
- **The binary.** All capture logs say `Vulkan validation layers: ACTIVE`, with no error
  and no warning. Six (1-02 frame 96, 1-02's close 300 and 361, 1-13's 420 and 662, 1-10's
  311) were retaken on the final binary, after the retry's stop and the dropped help tap,
  and are md5-identical. The second-loop frames for A-H12 were taken on it.

| # | check | original | port | tolerance | pass |
|---|---|---|---|---|---|
| A-H1 | H2 centre, scale (1-02 / 1-03 frame 96) | (640.1, 359.95), 2.8145, Pearson 0.996 | (640.0, 360.0), 2.8125, 0.99994 on both | ±1 px; 2.8125 ±0.005 | yes |
| A-H1 | H3 centre | (1177.6, 360.0) on both (sx, sy 1.391 / 1.419 and 1.408 / 1.404) | (1177.6, 360.0) on both, sx / sy 1.3632 / 1.4492: the bounce's A end at 1600 ms (1.3641 / 1.4484) | (1177.5, 360) ±1.5 px | yes |
| A-H2 | stones (384, 540), (896, 410.4); portal (896, 309.6) | (384.0, 540.0) 0.9987; (896.0, 410.4) 0.9988 on 1-03; (896.0, 309.65) 0.9987 | (384.0, 540.0) 0.99997; (896.0, 410.4) 0.99997; (896.0, 309.6) 0.9998. 1-02's right stone, under the portal's glow, fits (896.15, 410.9) at 0.797 in the original and 0.804 in the port | ±1.5 px | yes |
| A-H3 | HUD high-pass gains under the popup | restart / pause / pads / plaque within -0.008..0.011 | 1-02: 0.0009 / 0.002 / 0.005, 0.0004 / 0.0017; 1-03: 0.003 / 0.0026 / 0.0019, 0.0051 / 0.0022; the gold save the same | <= 0.03 | yes |
| A-H3 | "Part N" alpha at 1.6 s | 0.49 / 0.47 (the still's own age) | 0.46 / 0.46 on a 0.01 search; the model byte(1 - 1600 / 3000) = 0.467; the suite pins the quad at `CaptionAlpha(LevelFrameMs)` | ±0.03 | yes |
| A-H4 | world k under H1, 1-13 plateau | 0.399-0.406 | 0.4119 on each of frames 381..430 (intercept -0.025); 80 px blocks 0.4071..0.4156 (20) | 0.4118 ±0.02 | yes |
| A-H5 | restart / pause gain relative to the unpopped frame; pads | 0.412-0.420; pads absent | 0.4126 / 0.4124; pads -0.0006 / 0.0008 against 0.470 / 0.472 unpopped | 0.41 ±0.02; absent | yes |
| A-H6 | H1 / H2 fade in and out, best-fit sin T | in 1.075 / 1.010 / 0.990 s, out 1.045 / 1.025 / 0.980 s | in **1.005 s** (rms 0.0025; linear 0.73 at 0.033); out **0.990 s** (rms 0.002; linear 0.72 at 0.035) | 1.00 ±0.08 s | yes |
| A-H7 | H3 slide along +x, alpha | rms 0.97 px against 32 u; T 0.705 s | 80.0 / 70.0 / 51.0 / 34.0 / 19.5 / 9.0 / 2.5 / 0.0 px at 0.05 / 0.1 / 0.2 / 0.3 / 0.4 / 0.5 / 0.6 / 0.7 s (the model 79.9 / 70.0 / 51.0 / 33.9 / 19.6 / 8.9 / 2.3 / 0); rms **0.131 px**; alpha linear T **0.705 s** (rms 0.0034) | rms <= 1.3 px; 0.70 ±0.03 s | yes |
| A-H8 | bounce, stride, blink over 50 plateau ticks | sx, sy 0.97..1.03 antiphase (corr -0.993), stride 410 ms, blink min 0.957 | sx 0.9694..1.0305, sy the same, corr -1.000; stride **400 ms**; brightness 0.949..1.000 | 0.97..1.03 ±0.005; 400 ms ±5%; 0.95 ±0.02 | yes |
| A-H9 | help block: pads on the first frame after the popup | drawn, settled | gone on tick 661; left / right pad gain 0.470 / 0.472 on frame 661 (the unpopped 0.470 / 0.472), -0.0014 on 660 | drawn, no slide | yes |
| A-H9 | level start (1-02, gold save): from the popup-gone frame | restart / pause settled by 0.70 s; pads by 0.79 s; plaque whole, gone by ~0.89 s | gone on tick 360. Restart at home: 0.54 at 0.617 s, 0.94 at 0.667, 1.00 from 0.717. Left pad at home: 0.75 at 0.667, 0.93 at 0.717, then its pulse (0.85..1.07). Plaque: 0.004 on 360, **0.938** at 0.017 s, 0.513 at 0.317, 0.097 at 0.717, 0.024 at 0.867, 0.004 by 0.967 | 0.70 ±0.05 s; 0.70 ±0.1 s; gone by 1.0 ±0.12 s | yes (one tick late, see above) |
| A-H10 | a touch down on the card at (640, 200) | the fade starts on the touch-down frame; no portal | 1-02: "DEV tap down on tick 300", "popup closed on tick 300", gone on 360; the suite: `portalsUsed` 0 and no flight after the closing tap | the tick; unchanged | yes |
| A-H11 | demonstration gone on the close frame, 1-02 stone box | 85.0 / 38.1 -> 165.8 / 13.6; card 173.8 | 81.6 / 41.3 on frame 299 -> **165.5 / 13.9** on 300; card 173.0 | that frame | yes |
| A-H12 | each class's loop | 5.56 / 6.49 / 3.54 / 2.23 / 4.06 / 6.58 s, +1.1..1.5% on the emulator's clock | 1-02 / 1-03 / 1-13 / 1-25 / 2-03 / 1-10 repeat after 330 / 384 / 210 / 132 / 240 / 390 ticks = **5500 / 6400 / 3500 / 2200 / 4000 / 6500 ms**: the frame a loop in against the one two loops in differs by 0.000 grey over the card (the chara's band masked), and by 0.064..0.37 a tick either side (2-03 0.004 / 0.006, 1-10 0.020 / 0.016) | the decode ±0.5% | yes |
| A-H13 | tracks against the decode, phase fixed at creation | medians 0.7..2.1 px (hand, stone); fireball 8.8, projectile 13.5 against unrotated templates | hand 1-02 0.002 px (7 frames), 1-03 0.002 (11); stone 1-13 0.004 (12), 1-25 0.005 (19); fireball 0.000 (17), projectile 0.003 (4), turned templates. Every fit at Pearson >= 0.9 is within 0.6 px; the few below it (0.20..0.86) are the stone meeting its wall and the stone or fireball crossing a portal. One track a class was fitted on frames; the others (arrow, portals, dust, wall, ring, agent, chara) are pinned against the decode by `test_mp_popup` only | median <= 2 px; <= 15 px | yes |

**TESTS.**
- **New suite `test_mp_popup`: 156 checks,** pure, and it never skips.
  - **What the file says:** every framework number, the 13 scenes and their classes, each
    class's items, waypoints, loops and sheets, by name.
  - **Where the framework sits:** the card's top-left (161.875, 0), the button's decoded
    centre 1177.6 within A-H1's 1.5 px of the measured, the statics at A-H2's pixels, and
    the order of drawing.
  - **The entrance and the dismissal:** the dim and card on smoothEnd, the button linear
    with its slide, the tracks gone and the statics at 0 on the close frame, gone 60 ticks
    on.
  - **The bounce and the blink:** A at 0 ms, B at 400, the reversal, the blink's 150 ms,
    frozen at the close.
  - **The loops:** FrameTimer's one-step rule and the 0 ms jump; every class repeats at its
    loop in ticks and at no lag a tick either side, within 0.5% of the decode (A-H12).
  - **The tracks:** points of each class at chosen phases, turned sprites carried round
    their origin (A-H13).
  - **A help block's box**, and **GameLayer after a level-start popup**: `HudEntered`, the
    pads, `PlaqueAlphaFrom` whole on the first update and out over 1000 ms.
  - **Six refusals, each by name.**
- **`test_mp_layer`: 478 -> 536.**
  - **`CloseTheLevelStartPopup`**, a helper: level1 and level2 now open on a popup, so the
    cases that play them (`LevelsFollowInOrderAndRetryIsInstant`,
    `AFallOutOfTheLevelIsADeath`, `APortalAndAShotAreTheOriginals`,
    `EscapePausesALevelAndResumesIt`, `ALevelsEntitiesEmit`, `ParticlesGoWithTheirLevel`,
    `NoSpriteBlinksWhileWalking`) close it first; `WithoutTheOriginalThePortalIsABox` has
    no popup without the art; Escape closes a popup before it pauses; a retry raises it
    again.
  - **`ThePauseStopsTheLevelUnderIt`**: restart's alpha is 120/255 x its entrance byte.
  - **Two new cases:**
    - `TheTutorialPopupStopsALevelAsItLoads`: 1-02 raises `LevelHelp1Popup` in its load;
      96 ticks with the walk held leave age 0, the player at its spawn, the frame clock at
      1600 ms; no pad, restart or pause; dim, card, button, demonstration in that order;
      "Part 2" last at the caption byte; the arrow turned 23 degrees at 2.3 s. A touch
      down on the card closes it on its tick, the demonstration goes that frame, no portal,
      gone 60 ticks on; restart a tick into its entrance, home 700 ms after; the level
      then walks. A retry raises it from 0 at age 0.
    - `AHelpBlockOpensItsPopupAndTheLevelTakesUpWhereItStopped`: level12's block opens
      `LevelHelp15Popup` on the release; a move of 14 px opens nothing; the run through the
      popup ends where a straight run of the same play ends, the player within 1e-3.
- **`test_mp_hud`: 417**, unchanged. **`test_mp_levelend`: 226**, unchanged.

**NOT BUILT, INFERRED, AND LEFT OPEN.**
- **Not built:** 4-02's `SpaceEasterEggHelpPopup` (spec U6 wants an owner ruling); the
  achievements 62 and 63.
- **Decode only, not captured:** 3-09's `LevelHelp25Popup` with the chara sheet, 4-01's
  `RedKeyLocationHelpPopup` (spec U7: their blocks were never reached). The port opens both.
- **INFERRED:** the anti-portal popup's static chara at frame 0, and 3-09's sheet at frame
  0: nothing sets a frame.
- **INFERRED:** `m_ring`'s decoded ARGB(96, 0xA0, 0, 0) (spec U13), not checked against
  frames.
- **INFERRED:** the plaque's whole-then-out mechanism under a popup; its timings are A-H9's.
- **INFERRED:** a loop's origin is the popup's creation. The recordings' origins sit within
  the load stall (spec 5.4) and the port has no stall.
- **Not built:** the pressed close button's darkening; clear-portals' entrance (it keeps its
  flat 120, and no level starts with a portal placed).
- **The 12 of the move gate** is window pixels; its units are unmeasured (spec U8), as is
  whether the releasing tap walks.
- **Not fitted on frames:** the tracks other than the one A-H13 fits in each class (the
  arrow, the portals, the dust, the wall, the ring, the agent, the chara). The suite holds
  them to the decode's arithmetic.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, at `/W4` on the game, the sim library and every suite
  (`build.ninja` carries it on `Popup.cpp.obj`). After the last source change the build
  recompiled 27 translation units - every file this step touches and everything that
  includes `ScreenOverlay.hpp` or `VulkanPipeline.hpp` - and printed none. A second build had
  no work to do.
- **ctest.** **115 of 115** pass (114 and the new suite), "Not Run" 0, on the final source.
  Directly: `test_mp_popup` 156, `test_mp_layer` 536, `test_screenoverlay` 57,
  `test_mp_hud` 417, `test_mp_levelend` 226, 0 failures each.
- **Smart App Control** refused freshly linked executables. Each was deleted and relinked
  until nothing was Not Run:
  - the first full run after the final build had 22 BAD_COMMAND. Rerun, 20 were still
    refused and were relinked; after that 3, then 2, then 1 were refused and relinked, then
    none, and a full run passed all 115;
  - the game once after the final build (its captures failed "Permission denied" and were
    retaken), and once more mid-capture, where it ran again unrelinked.
- **No other document's table moves.** Only this plan lists the suites' checks.

## Step 53 - the front door: the loading screen, the main menu, and the black every menu state opens under (built)

The port opened straight onto a main menu of its own layout. TAP START was the only
button, 164 x 41 u. The title was a 512x512 texture stretched to 333 x 77 u. The
background was the 1x file squeezed into the view. The engine's text lines ran over all
of it. Every menu screen was a cut with no black, and a press acted on the down edge.
There was no loading screen and nothing moved. This step builds the remake's merged
spec `out/parity/specs/ui3/spec.md` sections 0.3-0.7 (what every menu state shares) and
section 2 with 2.1 (the main menu and the loading screen before it). It holds them to
that spec's acceptance lists 7.1 and 7.2. The evidence is `static.md` and `motion.md`
beside the spec, the recordings under its `rec/`, and the library stills
`out/parity/original/screens/main_menu*.png`.

**THE OWNER'S STANDING INSTRUCTION: MATCH THE ORIGINAL.** The design owner ruled on ui3
spec 8.2's four questions, and every later menu step is held to these answers:
- **R1**, chapter select pages TWO chapters per page, as the original does. For the
  chapter select step; nothing here.
- **R2**, device back (Escape) on the main menu QUITS the app, as `MainMenuLayer::update`
  does. Built here.
- **R3**, LOCKING as the original, from the port's save: a chapter needs 60% or more of
  the one before, and a level needs the one before it scored. The dev `--level` flag keeps
  opening any level directly. For the chapter select and grid steps; nothing here.
- **R4**, the loading screen as the original: the Asantee logo scene and its 1000 ms hold
  before the main menu. Built here.
- **P1-P4**, spec 8.1's port decisions, each taken as recommended:
  - P1, every per-frame rule once per 60 Hz tick;
  - P2, credits at 22.76 u/s;
  - P3, the dashboard's scroll bar at W - 7.5 u;
  - P4, a state's buttons and its black both start on the state's first frame. Only P4
    applies here.
- **Owner rulings carried:** the Facebook buttons (menu and credits), the free build's buy
  button and every like, store or video button are OMITTED. Achievements is KEPT. Its
  dashboard is the Credits and Achievements step's, and the pause's Achievements button
  opens it once that step builds it.

- **Data.** Every number is in `games/magicportals/data/ui.json`, in three new blocks, each
  citing its decode and its measurement:
  - `menu_state`: the black (`fade_ms` 700) and the press (`tint_byte` 204,
    `tile_travel_units` 48);
  - `main_menu`: M0-M6, TAP START's bounce and blink, and the title's bob. The music
    switch's x is `at_units_x` 32: units, not a fraction;
  - `loading`: the background, the character's walk, the portal, its halo and the vanish,
    the load (162 resources, 2 a frame, `hold_ms` 1000), the logo and the dots.
  - The entrance is `ui_layer.button`'s, unchanged.
- **Arithmetic.** Three new pure files:
  - `sim/MenuState`: the black's byte, Button's triangle, bounce and blink, its byte
    arithmetic, scaling about an origin, and the touch's largest travel;
  - `sim/MainMenu`: every piece in the original's order, what a point is inside, and which
    press acts first;
  - `sim/Loading`: the loop's frame arithmetic, the walk, the sheet frame, the dots, the
    vanish's places and the hold.
- **Drawing.**
  - **The main menu** goes through `core/ScreenOverlay` in display values (spec D21),
    background and all, with the state's black last. It puts nothing in the registry.
  - **The loading screen's scene** is quads in the level's space, as a level's art is: the
    background, the character, the portal's halo, the multiplied black halo and the
    particles. Its logo and dots go through the overlay.
  - **Chapter select and the grid** keep their world quads and gain their black over them.
  - **The new `EmitMenu`** draws it all from `OnUpdate`, beside `EmitHud`.
- **Closed:**
  - D1's black, on every menu state change;
  - D2 for the main menu;
  - D3 for the main menu's background;
  - D4, D5, D6, D7 (no text on any menu state), D8 (R2);
  - D18, on every menu screen;
  - D21 for the main menu;
  - D22's clocks: the layer keeps them per state and across the per-tick layout, never in
    a button.
  - Chapter select's and the grid's own deltas (D9-D17, D19, D20) are the later steps'.

**THE DECODE, READ AGAIN WHERE THE SPEC STOPPED.** From the listing the spec cites
(`asbc/all_functions.txt`):
- **`Button::update` (bytes 15892..16865) follows a touch in EVERY button it went down
  inside.**
  - A held touch rewrites `m_lastDownPos`. The release presses a button when that last
    held place is inside and the down was inside.
  - TAP START's rectangle lies wholly under the title's (y 385..565 px against -180..540),
    so a touch there holds both.
  - `PortalMainMenu::loop` acts on either with `createLevelSelectState`, so it makes no
    difference where it goes. `MainMenu::FirstActed` still orders them as the original
    runs them: the sound panel, TAP START, info, the title, Achievements.
- **`UIButton::setColor` (bytes 31747..31953)** writes the entrance alpha into `m_color`
  and keeps its rgb, which `Button::update` has just set to 0xFFCCCCCC or white.
  `Button::draw` (bytes 15237..15660) then multiplies that by the custom colour and the
  blink colour, and the alpha by the blink alpha, truncating each to a byte. So TAP START's
  alpha is `entrance x blink` and its grey `tint x blink`.
- **`setButtonHighlightEffects` (bytes 370863..371164) fixes the bounce stride at 400 ms.**
  Its second argument is the BLINK stride. TAP START passes 400, so the spec's numbers
  hold; the pause's close button passes 150, which step 52 already has.
- **`drawBlackRect` (bytes 314960..315187)** writes `iTOb(fTOu((1 - t/700) * 255))`,
  truncated, and `FadeInController::draw` stops at `elapsed >= 700`.
- **`MainMenuLayer::MainMenuLayer` (bytes 95602..96208) builds its `SoundPanelLayer`
  FIRST.**
  - So `UILayer::draw` draws the sound switch before TAP START, the title, info and
    Achievements, with the music switch (added by the panel's update) last.
  - Spec 2.3 puts the whole panel after M4. No capture separates the two, since no switch
    overlaps a button; the port takes the decode, as step 50 did.
- **`MainMenuLayer::update` (bytes 96444..96772)** tests `GetKeyState(14) == 1` and calls
  `Exit`. That is R2.
- **The noises.** TAP START and the title make `getButtonSoundName`, the port's
  `menu_button`. Info and Achievements make `getItemSelectButtonSoundName`, which
  `sounds.json` already maps with the level's own buttons: `level_button`.
- **`LoadingScreen::loop` (bytes 178170..179373).**
  - `BaseState::loop` draws the controllers first: the loading state's own black.
  - Only while loading, the loop then writes `m_swapStrings[loaded mod 26]` white and
    `'............'` at ARGB(40, 255, 255, 255), both Matura42 (Matura84_shadow on hd) at
    (0.5, 0.8) and half a unit per font pixel.
  - The logo comes after, every frame. So the logo and the dots are over the black, as
    launch_gold frame 73 shows them on black.
  - The 26 strings are 13 patterns, each twice: a run of three dots crossing ten columns.
- **`FrameTimer::set` (bytes 23254..23650)** resets to its first frame on the first call.
  After that it steps one frame each time its summed frame time reaches the stride, and
  never more than one a call.
- **`vanishEffect` (bytes 356384..356958).**
  - The suck effect goes at `portal + normalize(character - portal) * scale(32)`, turned
    `getAngle(direction) - 90` degrees. `getAngle` is `atan2(x, y)` in [0, 2 pi)
    (Ethanon `GameMath.h:711-715`), so 180 degrees here.
  - The sparkles go on the character.
  - `killPortal` (bytes 357278..357358) kills both of the portal's particle systems:
    nothing is released or renewed, and each particle lives out its life
    (`ETHParticleManager.cpp:211, 259, 378`).
  - A particle starts turned by its entity's angle (`ETHParticleManager.cpp:517`).
- **`black_halo.ent` is blendMode 4, `AM_MODULATE`** (gs2d `Video.h:60-68`), added at the
  portal's z less 4.
  - The engine has no multiply blend. The port writes `black_halo.bmp` once, beside the
    prisms, as black at an alpha of 255 less its grey. Drawn with the ordinary blend, that
    is the same arithmetic.
  - That file is the layer's only write outside `Paths::saveDir`, and only into a named
    `Paths::prisms` (the header says so). With none, nothing is written and the halo is
    left undrawn, so a layer built bare never writes into the working directory.
  - **The frames overrule the decoded order.** Ethanon's draw hash sorts z less 4 UNDER
    the portal's added halo. But launch_gold frame 183, once the spiral is killed, is a
    black blob with no trace of that halo, and frames 74-155 show the spiral light on dark.
    So the port draws the multiply over the added halo and under the particles. Why is not
    settled.

**ONE CLOCK: THE TICK'S.** This is P4's sibling, decided the way steps 40 and 45 decided it
for the level's blacks and the pause.
- The original times the black by `GetTime()`, and the bob by `GetTimeF()`, both wall
  clocks. It times buttons, blinks and the loading hold by summed frame time.
- The port runs all of it on the 60 Hz tick:
  - a menu state's first tick is its 0 ms, and a `--fixed-step` frame k ticks later is
    k / 60 s in;
  - the bob reads the layer's own clock from attach (`LayerClockMs`).
- The loading hold sums whole microseconds. 60 ticks are 1000 ms whether the tick is the
  double 1000/60 or the float the engine holds, and a first build that summed the float
  held one tick short.

**A STATE CHANGE WAITS FOR THE NEXT TICK; A PAGE DOES NOT.**
- **A release that changes state is done at the start of the next tick.** So are the
  loading hold and the back key on chapter select or the grid.
  - `SStateManager::setState` swaps at once, but the new scene draws from the next frame.
    The original's release frame is drawn in the old state, untinted, and the new black
    comes one frame on (spec 0.4, A-S5).
  - `PressMenu` and the new `PressMainMenu` stay immediate. The suites' presses are
    unchanged.
- **A page of the same grid is not a state**: no black, and its arrow acts on the release
  tick itself.
- **The press everywhere.** The main menu, and chapter select's and the grid's world
  quads, now:
  - tint the button a touch went down on at 204/255 while it is held inside;
  - act on the release with the down inside;
  - on a chapter or level tile, refuse a touch whose largest travel passed 48 u.

**THE LOADING SCREEN (R4).** `Screen::Loading` opens where the menu used to, when no level
is named. What it draws:
- `world_select_bg` centred;
- the character's hd sheet walking frames 8-11 from 0.35 to 0.65 of the width by
  `loaded / 162`;
- the portal: `portal_halo` added, the multiplied black halo, and `portal.ent`'s two
  particle systems through the level's own emitter code (which gains an entity angle and a
  kill);
- the dots and the logo.

The 81st tick hides the character, kills the portal's systems and adds the suck effect and
the sparkles. Then:
- tick 142 is the hold's 61st, and asks for the menu;
- tick 143 is the main menu's first, black.

Not drawn:
- the engine's own splash before it, which is Ethanon's and not the game script's;
- the loading state's black as the emulator showed it (spec U15, its first frames took
  165 ms or more). The port draws the decoded 700 ms of it over the scene, under the logo
  and the dots.

No music plays until the main menu's `playMenuMusic`.

**DEV ONLY**, in `main.cpp`:
- **`--tap <x>,<y>@<tick>-<release>`** holds a tap from its tick until its release tick, so
  a capture can show the press tint. `--tap x,y@tick` still releases on the next tick.
  Both reach the menu states now, through `touchThisTick`.
- **`--press back@<tick>`** is the back key on a menu state.
- `--record`/`--replay` were not used.

**CAPTURED AND MEASURED.**
- **Scripts.** `out/parity/ui3/frontdoor/capture_port.sh` and `measure.py` (gitignored),
  on the spec's own fits: `specs/ui3/work/static/lib.py`'s sprite Pearson, and
  `work/motion`'s fade regression, masked template tracks, ramp and bounce fits, re-run on
  the port's frames.
- **Frames.** 1280x720, `--fixed-step`, one tick a frame, each capture its own process:
  - the whole launch: loading frames and every tick of the menu's entrance (140..190);
  - one blink period tick by tick (239..288), then three more seconds at 5 ticks;
  - 12 s of the bob at 12 ticks, and 30 s at 60 ticks;
  - muted, with the sound switch tapped on 240;
  - releases on TAP START and on the title on 241;
  - TAP START held from 240 to 255, chapter select from 256, the back key on 400, the
    menu again from 401;
  - the same to chapter select, then its first tile tapped on 330 and released on 331
    (the level grid from 332), the back key on 450 and chapter select again from 451
    (330..380, 449..500).
- **The binary.** All 363 captures and the back-key run were retaken on the final source,
  after the loading screen's last change. The grid series' 103 were taken on it too. They
  came before the halo writer's guard on an empty prisms directory, which no capture reaches
  (`main.cpp` always names one). Every run logged `Vulkan validation layers:
  ACTIVE` and wrote its frame, and none logged a validation error.
- **Pairs.** `compare.py`, each looked at:
  - frame 239 against `main_menu.png`: `offset_px (0, 0)`, `edge_iou` 0.664, `mean_abs`
    6.70;
  - frame 263 against `main_menu_b.png`: (0, 0), 0.731, 5.20;
  - muted against `main_menu_sound_music_off.png`: (0, 0), 0.645, 8.23;
  - frame 60 against launch_gold 100: (0, 0), 0.634, 4.11;
  - frame 141 against launch_gold 183: (0, 0), 0.751, 1.80.
  - Every element lands on the original's. The residue is the omitted Facebook button,
    TAP START's blink phase, and the loading character's and particles' moment.

| # | check | original | port | tolerance | pass |
|---|---|---|---|---|---|
| A-S1 | loading -> menu (first black frame 143): linear T, rms; smoothEnd rms | T 0.698 median (0.680..0.755), rms 0.005..0.028 | **T 0.700** (t0 -0.002), rms 0.0013; smoothEnd 0.0317; t0 held at the first frame: T 0.698, rms 0.0016 | 0.70 +-0.03; rms <= 0.02 and under smoothEnd's | yes |
| A-S1 | gain at 0.10 / 0.35 / 0.60 s; first new frame | 0.143 / 0.50 / 0.857 (decoded); 0.000..0.064 | **0.145 / 0.503 / 0.859**; 0.000 (frame 143 all black) | +-0.03; <= 0.07 | yes |
| A-S1 | menu -> chapter select by the release (256) | as above | T 0.700, rms 0.0013; 0.145 / 0.502 / 0.859; 0.000; release frame 255 = its untouched twin (mean abs 0.000) | as above; old frame untinted | yes |
| A-S1 | chapter select -> menu by the back key (401) | as above | T 0.700, rms 0.0013; 0.145 / 0.503 / 0.859; 0.000; frame 400 = settled chapter select (0.000) | as above | yes |
| A-S1 | chapter select -> level grid by a tile's release (332) | as above | T 0.700, rms 0.0013; t0 held: T 0.698, rms 0.0014; 0.145 / 0.502 / 0.859; 0.000 (frame 331 is the old state at gain 0.968 of the grid's background) | as above | yes |
| A-S1 | level grid -> chapter select by the back key (451) | as above | T 0.700, rms 0.0012; t0 held: T 0.698, rms 0.0016; 0.145 / 0.502 / 0.859; 0.000 | as above | yes |
| A-S2 | title y, launch (143): rms, t0 first; left at 0.10 / 0.25 / 0.50 s | rms 0.17..0.85 (free t0); 69.97 / 42.12 / 8.91 | **rms 0.157** (45 frames, 2 dim frames off by > 4 px dropped); 70.32 / 42.36 / 8.93 | <= 0.9; +-3 px | yes |
| A-S2 | title y, after the back key (401) | same | rms 0.179; 70.37 / 42.37 / 8.91 | same | yes |
| A-S2 | TAP START y, both entrances | same | rms 0.043 / 0.045 from 0.23 s; 42.02 / 8.88 at 0.25 / 0.50 | same | yes, **0.10 s not trackable**: entrance 0.14 x blink 0.44, under a black that leaves 0.15 of the picture, is under 1% contrast; the 14 frames before 0.23 s were not found or matched elsewhere |
| A-S3 | M4 / M6 corner entrance: rms on the anchor ray, direction | 0.256 / 0.443 px | **0.075 / 0.057 px**; best angle 0.05 / 0.00 degrees off the ray; start (+78.44, +44.12) / (-75.30, +49.29) px decoded | <= 0.6 px; 1 degree | yes |
| A-S4 | M2 alpha, linear T | 0.700..0.735 | **0.700** (rms 0.0053; smoothEnd 0.0334) launch; 0.695 (0.0056) after the back key | 0.70 +-0.04 | yes |
| A-S5 | TAP START held 240..254, released on 255 | 0.795..0.815; new state next frame | **0.798..0.803** on all 15 held frames; 255 untinted (= twin); 256 chapter select, black | 0.80 +-0.02; nothing before the release; next tick | yes |
| A-S6 | a tile dragged 60 u / 40 u | decoded | `test_mp_layer` `ATileRefusesATouchThatTravelled`: 60 u no action, 40 u acts | exact | yes, on the port's chapter icons (its own layout until R1's step) |
| A-S7 | M0 background template rms | 2.31 | **0.349** on frame 239 (2.356 on `main_menu.png` by the same script) | <= 3 grey | yes |
| A-M1 | M3 / M4 / M5 / M6 TL, S, Pearson | exact, 0.9993 each | **(0, 0), (920, 630), (0, 630), (90, 630)**; S 1.40625; Pearson 0.99998 / 0.99999 / 0.99998 / 0.99998 | +-0.5 px; S +-0.003; >= 0.999 | yes |
| A-M2 | M2 TL x and y band; M1 centre | x 280; y in [-183.94, -176.06]; centre (640, 475.2) | **(280.0, -176.09)**, S 1.40625, 0.99999 (the bob at 3983 ms is +3.91 px); M1 centre (640.0, 475.2) at sx 0.99, sy 1.01 | x +-0.5; y in band; +-1 px | yes, y **at the band's edge**: the capture is at the bob's crest |
| A-M3 | muted: M5 `sound_mute`; M6 slot | (0, 630) 0.9992; empty | **(0, 630)**, S 1.40625, 0.99998; music_on -0.034, music_off -0.037 at (90, 630) | +-0.5 px; < 0.5 | yes |
| A-M4 | TAP START at rest (87 frames, 239..469) | sx 0.990..1.010, antiphase -0.887, stride 0.4075 s; alpha triangle, rgb in phase; min 0.2375 | **sx 0.990..1.010** (fit 0.990 -> 1.010, rms 0.0002), corr **-0.9996**, stride **0.400 s**, t0 0.000; alpha **0.248..1.000** (stride 0.400, t0 0); alpha/grey corr 0.997; brightness x alpha min **0.233** | +-0.003; 400 ms +-1%; +-0.03; 0.2375 +-0.03 | yes |
| A-M5 | the bob, 61 frames over 12 s | 3.141 s, 3.933 px, rms 0.067, x std 0.007 | **3.1415 s, 3.940 px, rms 0.008 px, x std 0.0002 px** | +-1%; +-0.15; <= 0.1; <= 0.2 | yes |
| A-M6 | 30 s, 31 frames: temporal std with M1, M2 and M2's bob band masked | 0 px > 6 (p99 1.40, max 3.10) | on the original's own masks (`work/motion/idlefit.py`: title x 330..950, y 60..460; TAP START x 430..850, y 380..540; the band y 34..59, x 461..791): **0 px > 6**, p99 0.42, max 5.18. The band alone holds 4,384 px > 6 (original 4,334). On `measure.py`'s wider masks: 0, p99 0.00, max 1.65 | 0 px | yes |
| A-M7 | every element on the same frame | same frame | frame 143 black to its last byte; on 144 every tracked element (title, TAP START, Achievements, music) matches; the corner fits' t0 is the first frame | exact | yes |
| A-M8 | release on TAP START / on the title on 241 | next frame | 241 = the untouched twin (0.000 each); 242 all black; 243 correlates **0.977** with chapter select and 0.129 with the menu | exact | yes |
| R2 | `--press back@300` on the main menu | quits | logged "quitting" on tick 300; the 900-frame run exited 0 early (5.6 s) | - | yes |

- **Sprites land on the decode, exactly.** Every button fits best at its decoded top-left
  and at 1.40625, with Pearson 0.99998..0.99999 where the original's own stills fit
  0.9993. The measured original sits on the same pixels.
- **The loading screen has no acceptance row** (spec 7). Its pairs were looked at
  frame by frame against launch_gold 100, 160 and 183: the walk, the spiral on the dark
  halo, the lilac suck to the portal's character side, and the black blob with its
  sparkles after.

**TESTS.**
- **`test_mp_frontdoor`, new: 194**, pure, against the spec.
  - The file's numbers.
  - The black's bytes and A-S1's three gains.
  - The triangle, TAP START's bounce (antiphase, smoothEnd) and blink (linear, the dip at
    the dim end, brightness x alpha 0.2375), and `Button::draw`'s bytes.
  - The 48 u travel, the largest while held.
  - M0-M6 at their stills' pixels, the draw order, and 4:3 (music stays 32 u in).
  - The bob's period and amplitude, on the layer's clock, moving the picture and not the
    rectangle.
  - All six entrance offsets along the anchor ray (A-S3's px), A-S2's three remaining
    offsets, and A-S4's 127.
  - The music switch going with the sound and coming back afresh.
  - What a point is inside, the overlap, which acts first, and the held tint.
  - The loading screen: 81 frames, the menu on 142 for either tick, the walk, FrameTimer's
    frames, the 26 strings, the vanish's places and angle, and the logo.
  - Eight refusals, each by name.
- **`test_mp_layer`: 536 -> 590.**
  - `TheMenuWalksToALevel` and `TheGridPagesThroughAWorld` tick through the loading screen
    first. The main menu's pictures are the overlay's, background first and title over
    TAP START, and no longer registry quads.
  - `TheSceneHoldsDisplayValues` starts on `Screen::Loading`.
  - `AClickOnTheMenuPressesWhatIsUnderIt` becomes `TheMainMenuActsOnTheRelease`:
    - down on TAP START holds it and the title;
    - 15 ticks held change nothing, at 0.80;
    - the release tick is still the menu, untinted;
    - chapter select comes on the next tick under a whole black, with the menu's noise;
    - a touch dragged off and let go presses nothing.
  - **Five new cases:**
    - `TheLoadingScreenLeadsToTheMenu`: its quads, the black first and the logo last with
      the dots between, the character gone on 81, the dots gone on 82, the menu on tick
      143.
    - `TheMenuStatesOpenUnderABlack`: 127/255 at 21 ticks, gone at 42. Chapter select and
      the grid each get their own black, and a page does not. The back key goes up a state
      on the tick after.
    - `TheBackKeyOnTheMainMenuQuits` (R2), which clears the process-wide latch after.
    - `TheMainMenusSwitchesAndCornerButtons`: the switches on the release tick, and A-M3
      in the layer. Info and Achievements make `level_button` and lead nowhere.
    - `ATileRefusesATouchThatTravelled` (A-S6).
- **`test_mp_hud` 417, `test_mp_levelend` 226, `test_mp_popup` 156**: unchanged.

- **A-S1 covers the port's five state changes.** loading -> menu, menu -> chapter select,
  chapter select -> menu, chapter select -> grid and grid -> chapter select were each fitted
  on frames. The spec's other transitions (credits, the dashboard, and chapter select's and
  the grid's own back buttons) are later steps', and use the same black.
- **TAP START's blink colour: the decode, against this step's own fit of the original.**
  - `measure.py`'s M1 composite fit reads the blink's dim end (alpha about 0.25) on both
    stills. The port's frame 239 fits grey **0.941** at alpha 0.248 (rms 0.61), which is the
    decode's colorA 0.95 at the start of a stride. `main_menu.png` fits grey **1.001** at
    alpha 0.246 (rms 3.68). That is motion.md's reading, which spec 8.4 set aside for the
    decode (the push order of `setButtonHighlightEffects`, instructions 22..40).
  - The decode is kept. At alpha 0.25, grey and alpha separate only weakly: the original's
    still fits with rms 3.68 where the port's clean frame fits with 0.61 and recovers alpha
    to 0.0001.
  - A-M4's tolerance holds either reading: brightness x alpha 0.233 (port) and 0.246
    (original) both lie in 0.2375 +-0.03.

**NOT BUILT, INFERRED, AND LEFT OPEN.**
- **Not built, and each is a later step's:**
  - info's credits screen (spec 3) and Achievements' dashboard (spec 4): both buttons make
    their noise and lead nowhere;
  - chapter select and the grid as the original lays them out (R1, R3, the pager);
  - the switches' saving, as step 50 left it.
- **INFERRED:** the black halo over the portal's added halo, against the decoded z (above).
- **Left open:** TAP START's blink colour at the dim end, the decode (0.95) against the
  original still's fit (1.00). A-M4 passes either way (above).
- **Not captured:** the loading state's own 700 ms black (U15); `music_off` on the main
  menu (U13); the main menu's back key on the original (U1).
- **Taken from the decode, as step 50 did:**
  - the music switch starts its entrance with the rest, where the original adds it on the
    panel's first update, a frame later. A-M7 asks for the same frame.
  - the sound switch drawn before TAP START (above).
- **TAP START's first 0.23 s** are not tracked on the port's frames (A-S2 above). The title,
  under the same arithmetic, is tracked from 0.05 s.
- **The black and the bob run on the tick**, not a wall clock (above). A port frame that
  took longer than a tick would show the original's black further on than the port's.

**MSVC 14.50 (Release, Ninja) only; GCC was not run.**
- **Build.** No warning, at `/W4` on the game, the sim library and every suite. One C4456
  (a shadowed `halo` in `buildMenu`) appeared on the way and was renamed. For the check, the
  step's headers and sources were touched and the build recompiled eight translation units:
  `MenuState`, `MainMenu`, `Loading`, `MagicPortalsLayer`, `LevelVisit`, `main` and both
  suites. It printed none.
- **ctest.** **116 of 116** pass (115 and the new suite), "Not Run" 0, on the final source.
  That includes the halo writer's guard, which recompiled `MagicPortalsLayer`, `LevelVisit`,
  `main` and `test_mp_layer` with no warning.
  Directly: `test_mp_frontdoor` 194, `test_mp_layer` 590, 0 failures each.
- **Smart App Control** refused freshly linked executables. Each was deleted and relinked
  until nothing was Not Run:
  - `test_mp_layer` once;
  - after the first full build, 6 suites, then 1;
  - after the loading screen's build, 3, then 1;
  - after the from-scratch recompile, 3, then none, and a full run passed all 116;
  - after the guard, `test_layerstack` (BAD_COMMAND), then none, and 116 again.
- **No other document's table moves.** Only this plan lists the suites' checks.

## Step 54 - the credits, the achievements dashboard, and the locking both are read from (built)

The remake's ui3 spec, sections 3 (credits) and 4 (the achievements dashboard), with the
owner's rulings of section 8.2 (R3: locking as the original, from the port's save) and the
port decisions P1-P3 of section 8.1 taken as recommended. **The main menu's info button now
opens the credits and its Achievements button the dashboard**; each back button and the back
key return to the main menu. Game-only: no file under `src/`, `assets/shaders/` or the engine
tests changed.

**The session that built this step ended before its verify and commit.** The code, the suite
and smoke captures were written on 2026-09-15; the suite was first compiled, fixed, and the
full ctest and the captures below taken on 2026-09-16. No independent verifier ran, and the
spec's section 7.3 and 7.4 acceptance rows were not each measured: see "Left open".

**WHAT CHANGED.**
- **`sim/Credits`** (pure): CreditsScreen and CreditsScreenLayer as `ui.json`'s `credits`
  block: the back button's place, entrance and bounce (0.97-1.03, stride 300 ms), the
  papyrus, and the one image of names that scrolls up it; the scroll steps once a tick (P1).
- **`sim/Dashboard`** (pure): ScoreDashboard and DashboardLayer as the `dashboard` block:
  the rows and chapter headers, the scroll with momentum x0.9 a tick, the top band x0.7 and
  the bottom band x0.3 (P1), the scroll bar (P3), the points plaque, the back button, and the
  start button a tap on a row raises.
- **`sim/Achievements`**: the 82 achievements as the remake's gitignored
  `out/data/achievements.json` holds them (`SUPERSONIC_MAGICPORTALS_ACHIEVEMENTS`), and what
  the port's medals unlock of them, re-derived as PortalMainMenu::
  checkPreviouslyUnlockAchievements does. **The content is Asantee's and is not in this
  repository**: it is extracted by the remake's `tools/asbc/achievements.py`, no suite embeds
  one, and without the file the dashboard draws no rows.
- **`sim/Locking`** (pure, ruling R3): isWorldUnlocked, isLevelUnlocked and
  computeWorldAccomplishment over the port's medals, as `ui.json`'s `locking` block. The
  dashboard is its first reader; chapter select and the level grid are to read the same
  functions. A named `--level` still opens any level.
- **`MagicPortalsLayer`**: the Credits and Achievements screens in the menu state, drawn by
  EmitMenu through the screen overlay. **`main.cpp`**: `--drag`, DEV only, a finger down,
  moved and released on given ticks, because a `--fixed-step` run has no pointer.
  **`data/sounds.json`**: the dashboard's sound events.

**MEASURED** (1280x720, `--fixed-step`, frame 420, the menu button tapped at tick 240;
captures in the remake's `out/parity/ui3/infoach/check/`).
- **Credits against `info.png`: compare.py mean_abs 6.79, edge IoU 0.549, offset (0, 0).**
  The frame differs by the omitted Facebook button (owner ruling) and by where the names have
  scrolled to at the moment each was taken.
- **Dashboard against `achievements.png` (gold medals on all 128 levels): mean_abs 2.89,
  edge IoU 0.840, offset (0, 0).** Rows, headers, icons, plaque, back button and scroll bar sit
  where the original draws them. The plaque reads 315 points where the original reads 325: the
  original's save holds an achievement the port's medals cannot unlock (below).
- **test_mp_info (new), 389 checks**: the credits' and the dashboard's layout constants and
  timelines, the glide (x0.9 a tick, 9x the last move) and the fling past the top (back within
  1 px in at most 55 ticks), the bands, the bar, row taps and the start button, and locking.
  Its first run failed one check of its own making: it asked the fling to peak above the 48 u
  it was released at, where the decoded order (decay, move, then band) lands the next tick at
  41.16; the check now asks for the release as the peak and the return from it. Three
  `CHECK_NEAR`s on doubles, which the harness compares as float and MSVC warned on, became
  `Near`. **test_mp_layer 592 checks**: its "info leads nowhere yet" and "nor does
  Achievements" became the screens they now open, and the back key's return.

**Build: no warnings** (MSVC 14.50, Release, Ninja; GCC was not run). **ctest: 117 of 117,
Not Run 0**, after Smart App Control refused 8 suites and then 2, each resolved by deleting
and relinking; `test_jobs` failed once on the busy machine and passed on the rerun (its
timing flake is a known open item).

**Left open.**
- The section 7.3 and 7.4 acceptance rows one by one against `info*.png`, `achievements.png`
  and `rec/` (the credits' scroll speed and drag/fling, the dashboard's recorded glide and
  fling paths, fonts and text positions): only the suites' model numbers and the two frames
  above were measured.
- Achievement unlock conditions beyond what medals decide are not decoded
  (`AchievementManager`); the 10-point difference above is one of them.
- Locked-tile denial, the credits' drag/fling and the dashboard's bottom band against
  recordings, silver-medal states (spec 8.3).
- Chapter select and the level grid (spec 5 and 6) are the next step.

**Merge note (2026-09-16: `ui2-screens` into main, after step 49).** The branch left main at step 44
(e534eff); main had gained steps 45-49 (lighting G3 to G5) meanwhile.
- **Conflicts, five files.** The planning doc: main's steps kept, the branch's renumbered 45-49 ->
  50-54 with their references to one another. `MagicPortalsLayer.hpp` and `tests/test_mp_layer.cpp`:
  both sides' includes, and both sides' `Emitter` fields (the lights' `sprite` and `slot`, the
  branch's `angleDeg` and `killed`). `MagicPortalsLayer.cpp` `OnUpdate`: the emitters stand still
  while game time is stopped, as the branch has it, and `syncLights` runs every frame after them, as
  main has it (it places lights from what the particles are, moving or not). `main.cpp`: both sides'
  DEV options, `--light-masks-off` inside the branch's argument loop.
- **Adapted, not conflicting.** Main's lighting cases walked out of a level to the grid with the
  back key and tapped on level1 at once; since steps 50-52 the back key opens the pause and 1-02
  raises its tutorial popup. `OutToTheGrid` goes the player's way (popup closed, back key, the
  pause's Levels button), used by the three cases that leave a level; the two shot cases close
  1-02's popup first. No other test changed. The E2 note that the branch would need the fourth
  `Acquire` argument did not apply: the branch adds no `Acquire` call. Step 48's scene binding 12
  and the branch's rotating overlay quads (step 52) merged without a conflict.
- **Measured on the merged build.** No warnings (MSVC; GCC was not run). **ctest 118 of 118**,
  Not Run 0, after Smart App Control refused 32, 11, 3, 2 and 1 suites and then `test_mp_layer`
  five relinks running (an identical rebuild relinks the same bytes; a clean relink got through).
  test_mp_layer 765 checks. **Byte-identical to step 49's build:** the seven gate levels at frame
  420, the ten torch and door frames, the HUD over level0 frame 1, MainScene and Wolf Brigade; the
  menu at frame 120 differs, as it must: it is the loading screen now. **Byte-identical to the
  branch's build:** the main menu at frame 400, the credits, the dashboard on a gold save, and the
  Play tap's next screen.

## Step 55 - the 16-bit surface, and the level a lit torch bakes at run time (built)

The lighting design's step G6 (the remake's `out/parity/specs/lighting/design_port.md`, section 7.1:
section 5.7 and `quantize Rgb565`), the last step of that plan. Game-only: the engine's
`RenderSettings::OutputQuantize::Rgb565` has existed since step 43 and is now asked for.

**WHAT CHANGED.**
- **`data/lighting.json`, `sim/Lighting`: `framebuffer_rgb565` true**, required and a bool, with its
  source (GL2JNIView.java:94, ConfigChooser.java:48-67: the original asks for a 5/6/5 surface and
  does not dither). `OnAttach` reads the port's lighting.json and sets the scene's quantise, so the
  menu has it as a level does. The screen overlay drawn after the composite (HUD, pause, menus) is
  not quantised; the original's was.
- **`Lighting::RuntimeBake(torch)`**: true from the first torch lit, for the rest of the level.
  Lighting one calls GenerateLightmaps, and putting it out calls it again with no static light
  left; nothing returns to the file's lightmaps (torch.json, design 5.7).
  **`ReceiverMask(isStatic, applyLight, runtimeBake)`**: baked at run time, a static sprite takes
  the static lights live too. `syncLighting` passes it, and draws no file lightmap while it holds
  (every torch level is `darkest` and ships none, so this drops nothing today).
- **`data/art.json` `torch_light`, `sim/Art`**: light_from_projectile.ent, which the shot that
  lights a torch adds: static, applyLight, `light01_normal.png`, a static light of range 512 at
  (0, -12, 24), colour (1, 0.5, 0.2), halo.bmp 300 at 0.7. Required like the shot's.
- **`MagicPortalsLayer::syncLights`**: that light and its halo at each torch while it is lit,
  made on the tick it is lit and destroyed when a signal puts it out. Its z is the torch's own
  `eth_z` (the callback's AddEntity position is not decoded: art.json's `_guess`). Its halo is at
  a particle share of 1: the entity's 12-particle flame and its sprite are not built.
- **`data/torch.json`**: `_not_built` no longer lists GenerateLightmaps or the light.

**GATES** (1280x720, `--fixed-step`, frame 420; the remake's `work/g6/final/`).
- **5/6/5 moves the port toward `engine` and away from `engine_no565` (2-05 `noLM_E<1`): passes.**
  Port vs `engine` 2.14, vs `engine_no565` 2.32 (vs the original 2.08; "<= 0.5 after System 6" is
  for later).
- **Every class against `engine_tier1x` moved closer**, the step-49 row -> this step:
  1-01 LM_E<1 0.45 -> 0.23, LM_E=1 0.85 -> 0.81; 1-09 LM_E<1 0.66 -> 0.29, noLM_E=1 0.29 -> 0.19,
  noLM_E=1_halo 2.39 -> 2.12; 2-26 LM_E<1 0.24 -> 0.19, noLM_E=1 0.91 -> 0.25; 4-22 noLM_E<1
  0.82 -> 0.00, noLM_E=1 0.91 -> 0.23. Against the original: 4-22 noLM_E<1 0.95 -> 0.13,
  noLM_E=1 2.18 -> 2.00; the halo rows of step 49 barely move (1-09 LM_E<1_halo 6.17 -> 6.16).
- **compare.py mean_abs against the original**, step 49 -> 55: 1-01 12.72 -> 12.72, 1-09 8.01 ->
  7.92, 1-13 17.62 -> 17.53, 2-05 12.35 -> 12.23, 2-26 10.29 -> 10.07, 3-05 8.47 -> 8.37, 4-22
  4.95 -> 4.50.
- **Torch flame** R 255, G 255 (asked >= 240, >= 235): passes.
- **The lit torch (`test_mp_layer` ALitTorchBakesTheLevelAtRunTime, on 4-22):** a tap at the
  torch lights it; one "Magic Portals Torch Light" on the static layer, range 512, colour in the
  ratio (1, 0.5, 0.2), 12 above the torch; every sprite that applies light then takes both layers;
  no overlay drawn; ambient (0.1, 0.1, 0.25). Before, the static walls took the live layer only.
- **Not taken:** a capture of 4-22 lit. The design compares it by eye against footage L1, which
  does not exist; one attempt with `--tap` missed the torch and was not repeated.
- **Unchanged:** MainScene, Wolf Brigade and the HUD over level0 frame 1 byte-identical to the
  merged build; the menu at frame 120 differs (its loading screen is drawn in the scene).

**SWEEP.** All 128 levels at frame 420 (`work/g6/final/sweep/`): 128 exit 0, validation active in every
log and silent, no level drawn unlit or as boxes. 14 logs carry a `GONE` sprite line against step
49's 13: the new one is 2-32, where the player dies on tick 287 and, since step 51, the lost screen
stands over a level that runs on while the dragon breaks a stone. Step 49's build retried at once
and logged no death (the log line came with 84b61ab); a rerun of 2-32 on this build is identical.

**Tests.** test_mp_lighting 482 -> 495 (the rule refused unless a bool; the runtime mask; RuntimeBake
through lighting, put out and after), test_mp_layer 765 -> 777 (the quantise at every start; the lit
torch above), test_mp_sprites 209 (its hand-written art.json files gain `torch_light`). **Build: no
warnings** (MSVC 14.50; GCC was not run). **ctest: 118 suites pass**: 116 in one ctest run, the two
it could not finish (`test_mp_sprites` failing before its fix, `test_mp_zerog` refused by Smart
App Control) run directly after. The owner asked for no more relink-and-retry loops: every refused
launch shows a Windows notification.

**Left open.** The overlay is not quantised. The torch's own sprite and flame (visuals System 7).
The torch light's z. The script's background lighten/darken (`lightenAllBackgroundEntitities`,
design 5.8).

## Step 56 - chapter select and the level grid, paged as the original pages them (built)

The remake's ui3 spec sections 0.5 (the pager), 5 (chapter select) and 6 (the level grid), with owner
rulings R1 (two chapters a page) and R3 (locking as the original) and port decision P1 (the pager's
per-frame rules once per 60 Hz tick). It closes the spec's deltas D9-D19: until now chapter select
showed all four chapters at once, nothing was locked, the grid's tiles were 0.217 of the height, a
page change was a cut, and both screens were world quads under the old layout. Game-only.

**WHAT CHANGED.**
- **`sim/Selector`** (new, pure): both LevelSelectors over one PageManager.
  - The board a state opens with, from the save: chapters open at >= 60% of the one before, levels
    open when the one before has a medal (`sim/Locking`), each chapter's percent, the warning where its
    boss level is scored and it is short, the last open chapter or level highlighted.
  - The pager: `SetPage` (offset = new - old), `Step` (x0.92 per getOffset call, twice with a
    neighbour and once without, snapped below 0.0005; the rubber band at a third; getGlobalOffset),
    the finger (the page follows it, a release past 0.2 swaps), the background's pan
    clamp(global, 0, 1) x (512 - W), and the forward arrow's fade in float truncated to a byte.
  - `Pieces`: W0-W9 and L0-L8 in the original's order, each at its entrance, bounce (I4-I6), press
    tint and the "requires 60%" sawtooth (I9); `HitAt`: a tile of the current page where it is drawn,
    or an arrow, none past the last page's forward.
- **`data/ui.json` `selector`**: every number with its decode and measurement.
- **`MagicPortalsLayer`**: `openMenu` builds the board and opens the pager on the opening page (the grid
  on the page of its last open level, sliding in under the black; chapter select on the chapter last
  played). `selectorInput` replaces `menuQuadInput`: the back key up a state from either page, a tile
  acting on release unless the finger travelled 48 u, an arrow turning the page on its release tick
  or, from page 1, going up a state. `EmitMenu` draws the pieces through the overlay, with the counter's
  frames by UV. `PressMenu` refuses a locked chapter or level (a named `--level` still opens any
  level). `buildMenu` puts nothing in the registry for these screens any more; `layOutMenu` lists every
  tile at rest on its page and the settled arrows, for `PressMenu` and the suites.

**MEASURED** (1280x720, `--fixed-step`, saves fresh and all-gold; TAP START tapped at tick 240, then
an icon or W8 at 330; the remake's `out/parity/ui3/select/first/`), compare.py against the stills:

| capture | still | mean_abs | edge IoU | offset |
|---|---|---|---|---|
| chapter select, fresh, page 1 | chapters_fresh_p1 | 1.72 | 0.824 | (0, 0) |
| chapter select, gold, page 1 | chapters_gold_p1 | 2.08 | 0.825 | (0, 0) |
| chapter select, gold, page 2 by W8 | chapters_gold_p2 | 1.93 | 0.849 | (0, 0) |
| grid, fresh, chapter 1 | levels_fresh_w1_open | 2.55 | 0.893 | (0, 0) |
| grid, gold, chapter 1, opening on page 2 | levels_gold_w1_p2 | 2.86 | 0.853 | (0, 0) |

**test_mp_select (new), 112 checks**, from the spec's own pixels: W1 (460, 18), W2 (403.75, 180) /
(640, 180) (chapter 1 bouncing about its centre), W3 (668.125, 270), W4 (431.875, 444.375), W5 centred
(538.75, 523.125), W6 (533.125, 573.75) at half alpha 300 ms in, W7/W8 about (64, 360) / (1216, 360), W9
(595, 648) digit and (640, 648) dot at ARGB(220,0,0,0); L3 (280 + 180c, 180r), L1 (-14.06, -126.56), L2
(0, -5.625), L4 centred, L5 (820, 540), L6 (381.25, 101.25), L7/L8 about (0, 360) / (1280, 360), L0 at
-160 px on page 2; the slide 0.92 x 0.8464^k new and 0.8464^(k+1) old, at rest by tick 44-47; the
fade 255, 244, 234, 224, 215, 206 and 0 on the 72nd frame, +12 a frame and 255 on the 22nd; a -0.087
drag snapping back, a -0.48 drag swapping to page 2 at +0.52, a +0.39 drag past page 1 drawn at 0.13.
**test_mp_layer 777 -> 783**: the grid lists all 32 tiles, sixteen a page; the forward arrow turns the
pager, a locked level refuses, back goes page 2 -> page 1 -> chapter select; the medal counted on the
board and placed from the drawn piece.

**Build: no warnings** (MSVC 14.50; GCC was not run). **ctest: 115 of 119 pass, 4 not run**: Smart App
Control refused test_mp_chapters, test_mp_movers, test_mp_minions and test_mp_zerog, which were not
relinked and run again at the owner's request (each refusal is a notification); none of their code
changed here. test_mp_select, test_mp_layer, test_mp_frontdoor (194), test_mp_info (389) and
test_mp_lighting (495) were run directly.

**Left open.** The acceptance rows of spec 7.5 and 7.6 that need footage: the slide against nav_gold
frame by frame, the arrows' entrance, the swipe paths. The denial path of a locked tile is omitted with
the store (ruling). Silver chapter medals are not captured (U3). The dot of the page counter fits worse
than the digit (U4).

## Step 57 - several frames of one capture run, and the last one still where it was (built)

The visuals plan's Step 0 (the remake's `out/parity/specs/visuals/plan_port.md`, section 3), which it puts
before every temporal gate: the walk (3a), the patrol (5a), the effects (7b/7c), the flicker (8a) and the
sway (10a) each want several frames of one run, and until now each frame cost a launch that loaded the level
again to reach a frame the last launch had already drawn. Engine-only: no file under `games/magicportals`
changes, and every game's main already hands unknown flags to `LaunchOptions`.

**WHAT CHANGED.**
- **`LaunchOptions`: `--screenshot-every <N>`**, `screenshotEvery` (0 = not given).
  - Refused (non-zero exit) without `--screenshot`, checked after the loop so the order the two are typed
    in does not matter, and for N below 1, a suffix or more than 1,000,000, as `--frames` is.
  - Without `--fixed-step` it is accepted with a warning in the new `warnings`: frame N is then not N/60 s
    of game time. The plan left refuse-or-warn open; warn, because real-clock frames are still pictures.
  - `CapturesFrame(frames)`: N > 0, a path, frames > 0 and a multiple of N.
    `ScreenshotPathForFrame(frames)`: the stem, `_f<frame>` unpadded, the extension, by
    `std::filesystem` so a dot in a directory is not an extension and the typed separator stays.
- **`SupersonicApp::Run`**: the stamped capture at the top of the loop, where `frame` is the count already
  drawn, and BEFORE the `--frames` exit test, which breaks (after it, frame 420 at N = 30 would be lost and
  13 files written). `writeScreenshot(path)` is now the one function both read back through, and on the last frame both
  read the same presented image in the same loop iteration, so the last stamped frame and
  `--screenshot`'s file are the same bytes. The constructor logs the parse warnings, so all four mains
  (engine, Magic Portals, Wolf Brigade, HUSK) get them without an edit.
- **Docs:** `Usage()`; AGENTS.md "Running without a person watching"; README Shipped; ARCHITECTURE 8c
  beside `--fixed-step`; `ScreenCapture.hpp`, whose "runs once, not per frame" was no longer true.

**GATES** (1280x720, `--fixed-step`; the remake's `out/parity/visuals/step0/`, `capture.sh`).
- **`level0 --frames 420 --screenshot X --screenshot-every 30` writes 14 stamped files plus X: passes.**
  `_f30`, `_f60` ... `_f420`, exit 0, validation active and silent.
- **Its `_f420` is byte-identical to X, to a plain `--frames 420 --screenshot` run and to the lighting
  baseline `work/g6/final/1-01_level0_f420.png`: passes** (all four md5 59c8b12a...).
- **Mid-run readbacks do not perturb the run:** its `_f270`, `_f300`, `_f330`, `_f360` and `_f390` are
  byte-identical to g6's five separate launches stopped at those frames (5 of 5).
- **Refusals:** no `--screenshot`: exit 1, "--screenshot-every needs --screenshot <path>..."; N = 0: exit 1,
  "wants a count of at least 1, got '0'"; neither writes a file.
- **Warning:** `--frames 30 --screenshot-every 10` without `--fixed-step`: exit 0, the warning is line 3 of
  the log, `_f10`, `_f20`, `_f30` and X written.
- **MainScene and Wolf Brigade unaffected:** plain frame-120 captures byte-identical to g6's. MainScene with
  `--screenshot-every 60` writes `_f60` and `_f120`, and `_f120` and X equal the plain capture.
- **Cost:** the 420-frame run took 10.9 s against 7.9 s plain, about 0.2 s a capture (the readback and PNG
  write); the same 14 frames as launches would take about 14 x 7.9 = 110 s. The profiler's worst frames in
  such a run are the readback's.

**Tests.** test_launchoptions 93 -> 138: read and not given; refused without `--screenshot` in either order;
0, -1, -30, "soon", "30f", "", "1.5", 1000001 and a bare flag refused, 1 accepted; the warning without the
step, none with it given after the flag, none for plain `--screenshot`; 420 frames at N = 30 capture exactly
14 (first 30, last 420, never frame 0), none without the flag or without a path; the names (`shots/run.png`
-> `shots/run_f30.png`, `captures.v2/run` -> `captures.v2/run_f60`, `a/run.tar.png` -> `a/run.tar_f90.png`,
a backslash path on Windows). **Build: no warnings** (MSVC 14.50; the three changed first-party files recompiled
alone at /W4 give none, ScreenCapture.cpp builds at /W0 as stb's host; GCC was not run). **ctest:** **93 of 119 pass, none fail, 26 not run**: Smart App Control
refused test_sat, test_raycast, test_convexhull, test_decomposition, test_draworder, test_nav, test_sprite,
test_uilayer, test_wb_gestures, test_wb_audio, test_wb_selection, test_husk_foundation, test_mp_tscn,
test_mp_turrets, test_mp_minions, test_mp_darkdragon, test_mp_launchers, test_mp_popup, test_mp_frontdoor,
test_environmentmap, test_spotlight, test_replay, test_mixer, test_undo, test_audio and test_hierarchy
(every exe relinked, since the change is in the core library). They were not relinked and run again: each
refusal is a notification on the owner's screen, and none of them calls the parser or the capture.
test_launchoptions was also run directly.

**Left open.**
- **Without `--frames` the flag is unbounded**, for the owner to decide: `CapturesFrame` does not read
  `maxFrames`, so an interactive run writes a PNG and stalls the queue every N frames until the window
  closes, where plain `--screenshot` writes nothing without `--frames`. Not refused: the spec names two
  refusals and one warning.
- **The plan's section 0 predates the lighting steps and this one.** 0.1 describes the per-frame launch
  this step removes and cites `SupersonicApp.cpp:1238` for the fixed step (1241 at 87886c6, 1247 now).
  0.2's numbers come from `original/levels_v1/`, which still exists; the current library is
  `original/levels/` (`_t8.0`, `_h6.3`), and 0.2's own re-point has not been done. 0.3's
  mean_abs (1-01 31.6, 1-09 30.4, 2-05 31.9, 3-05 23.4, 4-22 42.4) and its "3x too bright" premise are
  superseded by step 55's 12.72, 7.92, 12.23, 8.37, 4.50, so its edge_iou floors need a new baseline.
  Systems 1 (colour transfer) and 8 (halos) are largely steps 43-47 and 49.

## Step 58 - the sky the original pins to the camera, and the ground it covers (built)

Step 44's LEFT FOR THE OWNER: black ground on 34 of the 128 levels, up to half a frame, because the
original moves its sky to the camera every frame and the port drew it where the level put it. This step
ports that controller, `StaticSky` (`Sky.angelscript`). Game-only: no file under `src/` or `assets/shaders/`.
**Measured on captures of all 128 levels: the bare ground goes from 3,634,104 px on 34 levels to 1,593 px on
3, and those 1,593 are sprites drawing their own black; all eleven library levels step 44 names improve on
both mean_abs and edge_iou; every run exits 0 with validation active and silent.**

**WHAT THE ORIGINAL DOES** (the asbc listing, md5 `48fe65ce...`; byte ranges and instruction numbers in
`data/sky.json`'s `_source`).
- `Game::preLoop` builds `SpaceSky()` when `Game.spaceBg`, else `StaticSky(sceneMax.x, 'sky', true)`, and adds
  it after the camera, portal and earthquake controllers. `readProperties` sets `spaceBg` when the first
  `properties` carries `space_bg` at all (`CheckCustomData != 0`, not its value).
- The constructor collects the entities named exactly `sky` and `sky.ent`, runs `scaleSky`, and keeps
  `SeekEntity('satellite')` with its own position as `originalPos`.
- `scaleSky`, per sky t: `Scale(screen.y / GetSize().y)`; `m_width` = the scaled width; `m_scrollValue` =
  `GetFloat('scroll')`, `m_scroll` = it > 0 (one member each: the last sky's stand); `position(t)`;
  `scrollPos` = 0; for a scrolling sky `SetPivotAdjust(size x (1, 0))` at t = 0 and `(-1, 0)` at the last,
  with the size read before the scale, the second call replacing the first on a lone sky.
- `position(t)`: pitch = screen x 0.5, or for a scrolling strip size x 0.5 moved by a size at its ends;
  `SetPositionXY(GetCameraPos() + pitch + (m_width x t, 0) + scrollPos)`. `GetCameraPos()` is the corner,
  the video camera's position (`ETHScriptWrapper.Scene.cpp:350-356`); names match by `==`
  (`ETHBucketManager.cpp:488-503`); a pivot adjust is stored over the scale and multiplied back at the draw
  (`ETHEntity.cpp:1019-1022`; an unrotated sky is drawn by `GetScreenRect`, `ETHSpriteEntity.cpp:742-752`, less
  `ComputeAbsoluteOrigin`, `ETHEntity.cpp:270-297`; the rotated branch at 702-708 gives the same centre).
- `update`: `position(t)` for every sky, THEN `scrollPos.x += scaledUnitsPerSecond(-m_scrollValue)` (the frame
  capped at 200 ms, times the time manager's `m_factor`, 1 or 0 while paused: `STimeManager.angelscript`
  8045..8162, checked in the remake's `specs/ui3/work/motion/dec_state.txt`), back to 0 once
  `abs >= m_width`; the satellite at `GetCameraPos() + originalPos`.
- In the level's own units the scale is view height / picture height (`g_scale` = screen.y / 256 is applied to
  every entity first, main.angelscript's `preLoop`), and every sky picture is 256 u tall at every tier: 1.
  So the port changes where a sky is drawn and not its size. A sky entity is `ET_HORIZONTAL`, origin
  `EO_CENTER` (`ETHEntity.cpp:27-44`, `273-296`), and all 96 sky and satellite `Sprite2D`s carry no offset,
  so the entity's position is its picture's centre.

**MEASURED ON THE ORIGINAL** (the remake's `out/parity/visuals/sky/where_orig_sky.py`, which slides the tier
the original draws, fullhd at 512x256 u or 1x, over its library frame). **At t8.0 the best sky centre is
(640, 360) px of 1280x720, to within 1 px, on 16 of 16 levels**: 1-09, 1-20, 1-21, 2-01, 2-02, 2-05, 2-07,
2-09, 2-26, 2-29, 3-05, 4-20, 4-32 exactly (159,282 to 589,269 px agree), 1-01 at (640, 359), 2-03 at
(640, 361) and 1-13 at (641, 360) (101,052 px agree there, 100,890 at the centre; the same at h6.3;
`fix2/where_orig_1-13.txt`). The verifier's own metric (the lowest quarter of per-pixel differences)
agrees, within 1 px, on seven of them. Among them 1-01's level puts its sky 2 u right and 8 u up of that
centre, 3-05's 28.4 u right, and the port's cameras on 2-01, 2-05, 1-13 and 4-32 have moved 57 u right, 44 u
down, 88 u down and 313 u right.

**NO SKY SCROLLS, so the scroll branch is carried and tested but no level reaches it.**
- No sky carries a `scroll` variable: none of the 133 `.esc` files and none of the sky `.ent`s has one,
  and `GetFloat` answers 0 for a missing name (`ETHEntity.cpp:851-856`).
- The `scrolling_sky` = 1 on 37 levels' `properties` is another variable, and the string occurs nowhere in the
  listing's 1,473 functions (nor anywhere in `android_game.bin`, the verifier's byte search).
- The original's frames agree: on 3-05, a `scrolling_sky` level, the band y 0..199, x 150..599 is all sky
  (100% within 10 of `satellite_sky` pinned at the centre) and is byte-identical in h2.8 and h6.3 (one run,
  3.5 s apart) and in t4.5 and t8.0 of another. The same band of 3-06 is as still, but only 70.3% of it is sky.

**SPACE LEVELS: StaticSky does not run there, and SpaceSky is not ported.** The 19 `space_bg` levels
(4-01 to 4-19) take the other branch. SpaceSky (156379..158171) is outside step 44's excerpt and not small: it
places `planet_bg.ent`, `spiral`, `space_sky` and `eclipse.ent` about the camera, turns and scales the spiral
and runs the planet through `linearMotion`. `linearMotion` (`utilEntityEffect.angelscript`, 327527..328407)
and `scaleToSize` (329349..329538) are in the listing but not yet read. What the listing shows is recorded in
`sky.json`'s `space_sky._not_ported`. Step 44 counted no bare ground on those levels; their frames are
byte-identical to g6's.

**WHAT CHANGED.**
- **`data/sky.json`** (new): `static_sky` - `properties`, `space_bg`, `["sky", "sky.ent"]`, `satellite`,
  `scroll`, `screen_pitch` 0.5, `frame_cap_ms` 200 - with `_source` (with the Ethanon citations above),
  `_units` (with the fullhd tiers measured, below), `_census` and `_measured` (the 16 levels, the still band).
- **`sim/Sky.{hpp,cpp}`** (new, pure): `LoadRules` (refuses no names, a pitch or cap of 0); `Build` from the
  scene and the level's sprites (a space level or one with neither a sky nor a satellite does not run; a sky
  without a picture is an error); `PositionPx`, `DrawnCentrePx` (less the pivot), `DrawnSizePx`,
  `SatellitePx`; `Advance`, the scroll half of `update`.
- **`MagicPortalsLayer`**: `sky.json` read at attach with the port's other data. `buildSprites` builds the
  controller from the same sprites, after the no-portal sign is taken out, and marks the sky and satellite
  quads. It returns early when the art will not read, so a level drawn as boxes has a controller that does not
  run. `syncSprites` places them from `m_follow.centrePx` and `ViewPx()` on every tick, and `stepLevel` advances
  the scroll after `syncDrawables`, in `update`'s order. The quads carry `InterpolatedTransformComponent`: the
  camera is moved on the tick and drawn between ticks, and both interpolate by the same alpha. `SkyController()`
  is exposed for the suites.

**THE SUITES.**
- **`test_mp_sky` (new): 361 checks, 0 failures.** It pins:
  - the rules, and a pitch of 0 refused;
  - a still sky at the view's centre for three cameras, 1-01's move of (-1.944, +8) u, scale 1, width 455, and
    scale 2 under a 512-tall view;
  - `space_bg` "1" and "0" both building nothing, and `scrolling_sky` not scrolling;
  - `space_sky`, `sky_dark.ent` and a wall not being skies; a sky without a picture refused;
  - the satellite at the corner plus its place (the first named exactly `satellite`);
  - a still strip at +455 u a sky;
  - a scrolling strip's positions (682.5, 682.5, 682.5 u from the corner), pivots (+455, 0, -455) and drawn
    centres (227.5, 682.5, 1137.5); -1 u a tick at 60 u/s, -12 on a 1 s frame, the wrap after 455 or 456
    ticks and never a whole width off;
  - a lone scrolling sky drawn at 682.5; the last sky's scroll and width standing.
  - **The census, printed:** 128 levels, 95 with one sky (93 `sky`, 2 `sky.ent`), 19 space, 13 neither, 1
    satellite alone (3-15), 37 `scrolling_sky` keys, 0 scrolling, 0 skies at a scale other than 1, every sky
    at the view's centre. All 34 bare-ground levels have a StaticSky sky (checked by a script).
- **`test_mp_layer`: `TheSkyStaysOnTheViewAsTheCameraMoves` added; 790 checks, 0 failures.** level30 places its
  sky 0.94 u off the view's centre, so it fails on the first tick without this step. It walks right for 240
  ticks with four interpolated frames a tick: **camera 227.6 -> 324.0 px, the sky quad off the camera on 0 of
  240 ticks and 0 of 960 frames.** `TheLevelsArtIsDrawn`, `NoSpriteOnScreenIsCulled` and
  `NoSpriteBlinksWhileWalking` see the moved sky and pass.
- Run directly on the final source and data: `test_mp_sky` 361/0 (the relinked `505ada8e...`, and before the
  relink `8cfcf0eb...` against the final `sky.json`), `test_mp_layer` 790/0 (`4ae45192...`, not relinked by the
  last build). Earlier rounds, directly: test_mp_sprites 209/0, test_mp_lighting 495/0, test_mp_camera 15/0,
  test_mp_play 145/0, test_mp_start 726/0.
- **ctest, once, by the implementer: 93 of 119 pass, 0 fail, 26 not run** (refused by Smart App Control:
  test_launchoptions, test_convexhull, test_wb_buildings, test_wb_match, test_mp_tscn, test_mp_geometry,
  test_mp_minions, test_mp_ghost, test_mp_zerog, test_mp_dragon, test_mp_diamonds, test_mp_boss, test_mp_hud,
  test_mp_levelend, test_mp_info, test_mp_select, test_environmentmap, test_input, test_spotlight,
  test_lightselection, test_resourcesync, test_replay, test_camera, test_uicanvas, test_worldshapes and
  test_blending). Not rerun: it would launch the 26 refused exes again; the suites that link the sky,
  test_mp_sky and test_mp_layer, ran directly.

**BUILD: no warnings** (MSVC 14.50, Release, Ninja, /W4; GCC not run). The first build stopped on two faults of
this step, a broken string in the new layer test (C2001) and a discarded `[[nodiscard]]` `get_or_emplace`
(C4834, replaced by `all_of` then `emplace`); every build since printed no warning, and the verifier compiled
`Sky.cpp`, `MagicPortalsLayer.cpp`, `test_mp_sky.cpp` and `test_mp_layer.cpp` again with ninja's /W4 commands:
0 warnings. The last build relinked `MagicPortals.exe` and `test_mp_sky.exe` only.

**SMART APP CONTROL** (enforce mode, `VerifiedAndReputablePolicyState` = 1). Four links of `MagicPortals.exe`
were refused before any capture (`117e713b...`, `3fae84a5...`, `e0021483...`, `6b2d0d04...`). The fix round
deleted it and relinked once, with only a test comment and `sky.json` text changed since: **`5a435cad...`
ran**, and every capture below is that binary. A relink writes a new link time, so the file is new to the
policy; nobody relinked again.

**GATES.** Before: g6's frames (`work/g6/final/`, ad1e97c; 87886c6 and step 57 changed no level frame). After:
the remake's `out/parity/visuals/sky/after/` from `capture_sky.sh after`, `capture_sky.sh after sweep`
(128 levels at f420) and `gates_sky.sh after` (`gates.txt`); the attributions in `fix2/`.

| Gate | Required | Before (g6) | After (measured) |
|---|---|---|---|
| (1) bare ground, step 44's mask (`census_sky.py`), all 128 at f420 | the 34 levels lose their black where the original draws sky | 34 levels, 3,634,104 px of the 3,659,902-px mask | **3 levels, 1,593 px** (1-10 128, 1-23 13, 2-32 1,452): sprites' own black, below |
| (2) `compare.py` against t8.0, mean_abs / edge_iou | not worse | 2-02 34.16 / 0.3444; 4-32 30.08 / 0.4720; 4-20 19.95 / 0.3891; 2-07 24.87 / 0.2675; 2-09 24.54 / 0.2457; 1-21 50.08 / 0.3232; 1-20 44.40 / 0.4270; 2-01 12.43 / 0.3698; 2-05 12.23 / 0.3263; 2-03 12.73 / 0.3742; 2-29 12.94 / 0.3964 | **all eleven better on both**: 9.30 / 0.3795; 4.02 / 0.5524; 1.87 / 0.5366; 9.38 / 0.2695; 9.97 / 0.2528; 19.37 / 0.3605; 10.86 / 0.4738; 7.50 / 0.3710; 6.06 / 0.3416; 5.09 / 0.3810; 7.85 / 0.4003 |
| (3) `lightgate` 2-26 `noLM_E=1`; g6's other gates | not worse | 2-26 `noLM_E=1` port_vs_orig 1.92, port_vs_model 0.25 (both variants) | **1.95 and 0.29** (+0.03, +0.04: the 0.16 px re-sample, below); every other class of 1-01, 1-09, 2-05 and 4-22 within +-0.03; torch and door json identical. **Two numbers worse, both the 1x sky picture's tier (content scale and grade, System 6), not its place:** 1-01 mean_abs **12.72 -> 14.01** (unchanged pixels 11.05 -> 11.05; the fullhd picture in the same place scores 10.97); 1-01's halo rings further from the original (ring 60 R-B diff -47.2 -> -58.1, ring 70 -44.9 -> -51.3), reproduced by the bare picture's move within 0.6 on rings 50 to 150 |
| (4) zero-offset levels byte-identical, or every changed pixel explained | as required | port sky centres 1-01 (644, 336), 3-05 (720, 360), 1-09/2-26/4-22 at 227.5 u | **none byte-identical, as predicted**; the sky at (640, 360) exactly on all; changed pixels are the sky or what is drawn over it (below) |
| (5) a scrolling sky moves between two frames | as required | no level's sky scrolls | **not applicable**; `--screenshot-every 60` on 3-05 and 2-01: the sky at (640, 360) on f120 to f420 |
| (6) all 128 exit 0, validation active and silent | as required | g6 sweep: 128 exit 0 | **128 of 128 exit 0, "ACTIVE" in 128 logs, no VUID or Validation Error**; the 21 gate-set launches exit 0 |
| `test_mp_sky` | arithmetic and census pinned | - | **361 checks, 0 failures** |
| `test_mp_layer` | sky on the camera, ticks and frames | - | **790 checks, 0 failures**; 0 of 240 ticks and 0 of 960 frames off the camera |

**(1) THE BARE GROUND, level by level** (`after/census.json`, `fix2/after_detail.txt`).
- 31 of the 34 are clear: 3,658,309 of the mask's 3,659,902 px now show something.
- Every sky picture the port draws is opaque, 256 u tall and at least 455 u wide against the view's 455.11 u:
  0.111 u narrower, a 0.156 px strip each side, under half a pixel, so it covers every pixel centre. And it is
  the farthest thing back. So a pixel still exactly black is drawn black by a sprite in front of it:
  - 1-23: 13 px, one column (x 1185, y 589..601), the dark right edge of a stone pillar, dark in t8.0 too;
  - 1-10: 128 px, a strip 8 px tall (x 997..1169, y 450..457), the black top border of a stone block;
  - 2-32: 1,452 px inside a block lit nearly black (x 1018..1123, y 585..674), 0 in g6 as well.
- None of the port's 1x sky pictures has a (0, 0, 0) texel (`fix2/fullhd_padding.txt`). The fullhd tiers'
  black is padding the port does not draw (below). sky_purple (1-25) has 3 texels that a floor to RGB565 would
  make 0; 1-25 counts 0 bare px.
- **New black, 809 px on 35 levels**, pixels exactly 0 after and not in g6. Everywhere but 2-05 they were at most
  8 in g6, one RGB565 step from black under the moved sky. 2-05's 290 were at most 20, and the original has
  them at 0 (median and p90): mean |port - original| there 8.4 -> 0.3.

**(3) AND THE g6 GATES. Two gate numbers get worse, and both come from the 1x sky picture's tier (its content
scale and grade), the finding under THE VISUALS PLAN below: placed where the original draws it, the port's
picture is compared with a different picture.**
- `gates_sky.sh` now prints every lightgate class beside g6's json. `noLM_E=1` (sky blocks):
  - 2-26 +0.03 port_vs_orig, +0.04 port_vs_model; 4-22 +0.02, and +0.02 / +0.03 port_vs_model;
  - 1-09 port_vs_model +0.02 (tier1x) and +0.01 (engine); its halo class -0.01 to -0.02.
  - These skies moved 0.056 u = 0.16 px (the view's centre is 227.556 u, their levels say 227.5), which
    re-samples every sky texel: the whole change is at most 0.04 of 255. The lightmapped classes are 0.00.
- `compare.py` on g6's seven: 1-09 7.92 / 0.4255 -> 7.92 / 0.4253; 1-13 17.53 / 0.2317 -> **12.34 / 0.2398**;
  2-05 12.23 / 0.3263 -> **6.06 / 0.3416**; 2-26 10.07 / 0.4024 -> 10.08 / 0.4022; 3-05 8.37 / 0.4097 ->
  **4.85 / 0.4337**; 4-22 4.50 / 0.5965 -> 4.51 / 0.5963.
- 1-13's lightgate (engine_tier1x) is refused for its camera offset, rc 2, as it already was in g6
  (`work/g6/gates_final.txt:11`); not this step. Its `compare.py` improves 17.53 -> 12.34, above.
- **1-01 12.72 / 0.4546 -> 14.01 / 0.4581: the mean_abs rise is the 1x picture's tier (its content scale
  and grade), not the place**
  (`fix2/attrib_1-01.txt`). The pixels the move did not change score 11.05 before and after; the 295,930 it
  changed go 16.26 -> 20.27. Put the original's fullhd `sky.png` pinned at (640, 360) into the 206,540 px that
  show the port's sky and the frame scores **10.97** (3.86 on those pixels); the 1x picture back at the level's
  place scores 13.15.
- **Torch and door json: identical. HUD f1 and menu f120: byte-identical.**
- **The halo gate** (1-01, R-B by ring over the right arch) moves AWAY from the original, because the sky
  under the halo moved (`fix2/attrib_halo.txt`): port R-B ring 60 46.2 -> 35.3 (against the original 93.4:
  diff -47.2 -> -58.1), ring 70 50.4 -> 44.0 (-44.9 -> -51.3); diffs 50 -40.0 -> -42.7, 80 -31.0 -> -36.2, 90
  -17.9 -> -22.1, 120 -14.1 -> -17.8, 150 -17.9 -> -18.5; rings 30, 40, 100, 110, 130 and 140 0.0 to 1.5
  closer. `gates_sky.sh`'s "halo ok" is only the script's exit; the comparison with g6's `halo.txt` is by
  hand. The same rings over the bare 1x picture, level place -> pinned, move by the same amount to within 0.6
  on rings 50 to 150 (ring 60 -10.7 against -10.9), and within 1.4 on rings 30 and 40, where the halo itself
  is most of the pixel. The original's own background under them, the fullhd picture pinned, is 10 to 44
  higher in R-B on rings 50 to 150: System 6's picture tier again.

**(4) EVERY CHANGED PIXEL** (`fix2/changed_explained.txt`, `where_port_sharp.txt`).
- Where the sky is drawn, by the lowest quarter of per-pixel differences over +-4 px: exactly (640, 360) on
  1-01, 1-09, 1-13, 1-20, 1-21, 2-01, 2-02, 2-05, 2-26, 3-05, 4-22 and 4-32. The predicted moves:
  1-01 (-1.94, +8) u; 3-05 -28.4 u; 1-09, 2-26 and 4-22 +0.056 u; 2-05 (-2.9, +42) u; 1-13 (+0.06, +88) u.
- Changed px, and the share where the 1x sky shows within 10 in either frame (grown by 2 px): 1-01 295,930
  (69.6%), 1-09 11,576 (97.9%), 1-13 110,205 (91.3%), 2-05 472,396 (97.7%), 2-26 9,031 (89.2%), 3-05 411,734
  (95.3%), 4-22 4,860 (91.7%). The rest is sky under something drawn over it, looked at on 1-01, 1-13 and 3-05
  (`fix2/rest_sheet.png`): the portal halos and torch light in 1-01's windows, the window's soft edge and a
  crystal's glow on 1-13, pillar edges and the translucent HUD and control buttons on 3-05.
- **37 levels byte-identical:** the 19 space levels, the 13 with neither, and five that run the controller.
  Every one is placed pinned: "the sky is drawn where the level put it" is in 0 of the 128 sweep logs, and
  each log's sprite dump at load (1 world unit = 50 u) has the pinned position, read per log:
  - 3-32: `satellite_sky` at x 4.55111 = 227.56 u, not its level's 256, a 28.4 u move; 4-30: `dark_sky` at
    227.56 u; 3-13: `dark_sky` at 284.44 u, its camera's corner at load (56.89 u) plus 227.56. No sky pixel is
    visible in either frame of these three (the sky locate's best is at its +-4 px edge, scores 10 to 52
    against 1.8 to 6.6 where a sky shows): scenery covers them.
  - 3-27: `satellite_sky` at 256 u (5.12), its corner at load 28.44 u; g6 already drew it at (640, 360).
  - 3-15: its satellite at (140, 182) u, the corner (0, 0) plus its place, and not visible in either frame:
    the stone backdrop covers (394, 512) px (`fix2/3-15_satellite_crop.png`). So no frame shows the satellite
    path; `test_mp_sky` pins it.

**(5) STILL.** 3-05 and 2-01, `--screenshot-every 60`: sky at (640, 360) px on f120, f180, ... f420 (510,903
to 516,497 and 415,235 to 418,302 px agree). f60 is the level's fade-in (mean 28.3 against 46.0 at f120),
where the count within 10 does not find it; the picture shows it in place. 2-01's camera stands 57 u right
on f300 to f420: g6 drew its sky at (482, 360) on all five frames, the port now at (640, 360)
(`fix2/where_port_2-01_g6_vs_after.txt`). A camera moving under a pinned sky is `test_mp_layer`'s level30 walk.

**(6) AND THE LEVEL WALK.** `MagicPortals --visit-levels lightmapped --visit-passes 2`, not run since step 47:
- With no saves it exits 1: 392 failures, the first "opening 1-3 ... moved the cache from 36 to 36". 1-3 is
  locked. `PressMenu` does nothing for a locked level (`MagicPortalsLayer.cpp:1291`, step 54) and
  `LevelVisitLayer::open` does not look at its answer, so the walk measures a level that never opened.
- With `--saves` pointing at the remake's `out/parity/ui1/saves_gold`: **exit 0 in 20 s, 134 visits, 1,460
  lightmap sets, peak 101, baseline 80 at all 67 pass-2 visits, 0 failures**, validation active, "Subsystem
  resources destroyed cleanly" (`fix2/visit_saves_gold.log`). Step 47 had peak 91, baseline 70; steps 48 to 56
  are between, and the sky takes no lightmap set.

**THE ROUND-1 SIMULATION IS SUPERSEDED.** It predicted mean_abs falls and edge_iou on 1-20 falling to 0.3659;
the capture has 1-20 at 10.86 / 0.4738.

**THE VISUALS PLAN, where this step makes it stale** (the remake's `out/parity/specs/visuals/plan_port.md`).
- No row or system of it owns the camera-pinned sky. Gap D and System 6 treat the sky's width only.
- System 6's gate `sky4.py` on 1-01 (fit_a 0.925, fit_b 2.2 px) was measured on a sky drawn at the level's
  (229.5, 120). Pinned, it moves (-5.5, +22.5) px, so fit_b needs a new baseline before System 6 is gated.
- **Gap D and System 6 stand.** The fullhd tiers of sky, icy_sky, red_sky and sky_purple are 1024x512 texels
  with 57 black columns each side (`fix2/fullhd_padding.txt`), 455 u of picture inside 512 u; satellite_sky is
  512 u in both tiers. But the 1x file is not that picture at 1:1 on two of them (the verifier's
  `verify3/tier_geom.txt` and `tier_geom2.txt`): 1x `sky.png` is the whole fullhd image resampled to about
  554x277 and cropped (NCC 0.9990, against 0.8717 at 1:1), a content scale of 1.082 whose inverse, 0.924, is
  gap D's 0.9275 and `sky4.py`'s fit_a 0.925, and it is still 3.5 apart per pixel after a per-channel gain and
  offset, a grade. red_sky is about 1.047x (NCC 0.9751 against 0.9080) and 9.4 apart after that fit. icy_sky
  and sky_purple are 1:1 (NCC 1.0000 and 0.9970). That tier, content scale and grade, is 1-01's mean_abs and
  halo rings above, and it is on red_sky (1-20 and 1-21 among the library levels). An earlier reading of this
  step, that only the grade differs, compared the pictures at 1:1 only.

**LEFT FOR THE OWNER.**
- **SpaceSky**, for the 19 space levels, once `linearMotion` and `scaleToSize` are read.
- **System 6** stands as the plan writes it (the fullhd tier's content scale and grade on `sky.png`, and on
  `red_sky.png`); only `sky4.py`'s fit_b baseline needs retaking on the pinned sky.
- **The level walk without saves**: `LevelVisit` should unlock or check `PressMenu`'s answer. Not this step's
  system.
- The satellite (3-15 only) is behind scenery, so its pinning has no frame evidence.
