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
