# Re-syncing the Wolf Brigade port

The port in `games/wolfbrigade/` was verified, slice by slice, against the
game's own `tools/verify_*.gd` harnesses. It was verified against the game as
it stood at `ebf3d27` (10 July 2026). The port record is
[2026-08-25-wolf-brigade-port.md](2026-08-25-wolf-brigade-port.md), and its
method is unchanged here: port the GDScript a harness exercises, port the
harness into a C++ test, and require the test to reproduce the numbers the
Godot harness prints.

**The owner approved this on 10 September**, after HUSK: "After that you may
proceed with wolf brigade and finall magic portals remake."

## What moved

| | port base | target |
|---|---|---|
| game commit | `ebf3d27` (10 Jul) | `50741d1` (12 Jul) |
| commits between | | 36 |
| files changed | | 102, +6,637 / −600 |
| headless harnesses | 25 | 29 (26 in `verify_all.bat`) |

**The 36 commits turned it into a different game.**
- Real CC0 art with baked frame animation, and a dusk parallax backdrop.
- A data-driven campaign: `levels.json`, with per-level overrides of world,
  economy and waves, and capture points.
- Endless mode removed.
- Direction B, hero-first: buildings auto-train and the village runs itself.
- The restructure (`b6c73fb`):
  - the player permanently possesses a hero, and selection is gone from the
    player's world;
  - proximity panels replace building selection;
  - garrison and warband squads, with formation and a leash;
  - respawn at the Town Hall or a buildable Waystone, and defeat when none is
    left;
  - priests as healers, and a shelter bell.
- Cleave and Dash as data-driven abilities (`abilities.json`).
- A Storehouse, farms and a supply cap, and passive regen.

Of the 102 files, the ones the simulation port mirrors (`scripts/core`,
`scripts/entities`, `scripts/systems`, `data`) account for 2,763 lines added
and 291 removed. `unit.gd` alone is 727 of them. Five harnesses are new:
- `verify_hero`
- `verify_squads`
- `verify_casters`
- `verify_respawn`
- `verify_levels`

`verify_endless` is gone, and most of the rest grew.

## Baseline: the oracle is alive on this machine

On 10 September the game's `tools\verify_all.bat` at `50741d1` ran on the
laptop's standalone Godot 4.7 (`5b4e0cb0f`). Result: **26 passed, 0 failed.**
Every number below is one this machine's Godot printed, not one read off the
source.

The port's data was `ebf3d27`'s, byte for byte apart from line endings:
`difficulty.json` and `meta.json` are unchanged since, eight files changed,
and two are new.

## How the work is kept from breaking the engine's branch

Copying `50741d1`'s data turns most of the fifteen `test_wb_*` suites red until
their slices are re-ported, which takes many commits. So the re-sync runs on
its own branch, **`wb-resync`**, cut from `engine-roadmap` after the
cross-platform determinism commit (`a6472b8`). It merges back only when every
Wolf Brigade suite is green against the new oracle. `engine-roadmap` stays
green the whole time.

## Slice 0: data

**Copied:** all twelve files from `50741d1`, including the new `levels.json`
and `abilities.json`.

**`GameData`** follows `data_loader.gd`:
- `levels` and `abilities` join the file table, making twelve.
- `world`, `economy` and `waves` are served as the **merge of the base file and
  the active level's overrides**: top-level keys replace and arrays go whole.
  The merge always starts from the pristine base, so levels never stack, and an
  unknown id means the default level.
- `LoadAll` applies the default level, as `load_all` does.
- `Ability()` filters `_`-prefixed keys, so the file's `_comment` is never
  read as an ability.

**`DataValidator`** loses the endless-mode check and gains the three the
campaign added:
- `levels.order` names real levels;
- a capture point's income bonus pays a real resource, and an unknown bonus
  kind fails loud;
- `economy.starting_hero` is a real unit.

**`test_wb_data`** now reproduces `verify_data`'s output at `50741d1`: twelve
files with key counts world 18, units 7, buildings 8, waves 3, economy 12,
upgrades 7, difficulty 3, audio 5, meta 2, fx 11, levels 3 and abilities 3,
and 0 issues. It adds one deliberate typo per new check, and pins the level
merge on a scratch file: override, no stacking, arrays whole, and the fallback
for an unknown id.

**What the new data broke, and what it did not.** With slice 0 in, eight of
the other fourteen suites still pass untouched: buildings, combat, economy,
gestures, persistence, progression, snapshot and worker. They run on authored
fixtures rather than the shipped data. Six go red (GCC, 10 September), and
every failure traces to the data rather than to broken code:

| suite | failures | cause |
|---|---:|---|
| `test_wb_match` | 33 | `world.ground_y` is 590, not 800. Resource nodes went from 12 to 22 |
| `test_wb_audio` | 5 | 18 sfx, not 11. The `attack` tone is 2,205 samples, not 2,425. The shipped board no longer has a researching building where the case looks |
| `test_wb_selection` | 5 | The hall probe at y = 720 now misses the hall, which stands on the new ground line |
| `test_wb_waves` | 3 | The failing case is Endless mode, which the game removed |
| `test_wb_placement` | 1 | Ground line, as for match |
| `test_wb_hud` | 1 | A tap on empty ground no longer moves the selection. Move orders are band-clamped since `2d38d9e` |

None of these gets fixed by editing the expected number. Each belongs to a
slice below and returns to green when that slice reproduces what its harness
prints at `50741d1`.

## The survey, and the order it gives

A read-only survey mapped every new and changed harness to the game
functions it exercises, with the values it asserts. It covered every changed
simulation file and the port file each maps to. Its dependency order is the
plan. Each slice names the harnesses that pin it:

1. **Data, and Endless out.** `GameData` levels, abilities and merge;
   `DataValidator`; `WaveDirector` without the generator; `GameState`, where
   the mode becomes the level; `Progression`, where `mode` becomes
   `control_scheme`. Pins: `verify_data`, `verify_levels` (the data half) and
   `verify_waves` (unchanged).
2. **Stats structs.** New unit fields: `supply`, `hp_regen`, `controllable`,
   `heal_amount`, `abilities`, `can_follow`, `can_build`. New building fields:
   `auto_train_default`, `supply`, `hp_regen`, `hero_respawn`.
3. **The band and 2D movement.** `ground_y` 590 with a 280 px-deep lane:
   - moves clamped to the band;
   - 2D approach for gather, deliver, build, flee and melee;
   - parking, and `command_gather`;
   - the placement row gap (115 px).

   Pins: `verify_units`, the cross-row parts of `verify_economy` and
   `verify_combat`, and `verify_buildings` placement.
4. **Lane queries.** `enemies_within`, `nearest_wounded_ally`.
5. **Supply and production.** Supply, auto-train with a reserve,
   research taking time, rally points, recruit squad, passive regen, and
   `unit_trained`'s four arguments. Pins: `verify_buildings`' new checks and
   `verify_casters` D.
6. **Capture points.** A new `CapturePoint`, and army damage through
   `_effective_damage`. Pins: `verify_economy` capture and army cases, and
   `verify_levels` capture config.
7. **Healers, squads, the hero under control.** New states `CONTROLLED` and
   `HEALING`, warband and garrison, the leash, the shelter bell,
   `HeroControl`. Pins: `verify_casters`, `verify_squads`, and `verify_hero`
   B and F.
8. **Abilities.** Cleave and Dash, with i-frames. Pins: `verify_hero` G.
9. **Save.** The new unit, building and game-state fields, capture points,
   and the resource-node sprite fallback. Pins: `verify_snapshot`, and the
   save parts of the others.
10. **Match (`main.gd`).** Level applied, capture points spawned, the
    starting hero possessed, the respawn flow, the Town Hall row at 640.4.
    Pins: `verify_respawn` (the only new harness needing a real boot),
    `verify_snapshot`, and `verify_meta`'s award wiring.
11. **Presentation-adjacent.** `AudioTones` (the noise wave, new tones,
    `falloff_linear`), `GestureMachine`'s hero mode, and a control-scheme
    resolver. Pins: `verify_audio`, `verify_hero` A and D.

**Left out, by the port's own rule** (`Match.hpp` names camera and UI as
presentation): camera follow (`verify_hero` C), the joystick and on-screen
buttons (E), the level-select screen, `verify_fx`, and the menu clicks in
`verify_input`.

## Slice 1: Endless out, the level in

**`WaveDirector` loses the generator.** Gone: its configuration, the per-step
cap, the endless-only victory rule, and `endless_index` in the save. Kept, as
the original keeps them, are the per-spawn health and damage multipliers on
queued entries. A save written while Endless existed still loads, because its
extra key is not read.

**`GameState`: the mode becomes the level.**
- `SetLevel` / `Level()` hold the choice as made. `CurrentLevel()` resolves an
  unset or unknown one to the data default, as `current_level()` does.
- `workers_sheltered` (the shelter bell) and `hero_down_x` join the save in the
  original's shapes (`[]` or `[x]`).
- `Reset` clears both, and keeps the difficulty and the level.

**`Progression`'s `mode` becomes `control_scheme`,** under the original's key,
as `save.gd` changed.

**The layer:**
- No mode radio. The difficulty row stays; the level is chosen on a campaign
  screen this port has not built.
- No Endless line on the HUD.
- `applySavedRules` applies the run's level to the data before the boot,
  where `main._ready` applies it.

**`GameData::ApplyLevel` assigns each section in place** rather than clearing
and rebuilding them. A reference to `data.World()` held across a level change
therefore stays valid.

**The Endless tests are gone.** In their place are three cases:
- a waves file still carrying an `endless` block is ignored;
- a restored run keeps its level and runs its schedule to the fifth wave, then
  reports no next wave;
- an unknown level resolves to level 1.

The snapshot suite now sets the shelter bell and the hero's death spot before
it captures. The digest therefore covers both new fields, which is exactly
what the game's own review found `verify_snapshot` had missed.

**Result (GCC, 10 September):** waves 78/0 (was 3 failures), economy 59/0,
snapshot 157/0, persistence 178/0 and progression 130/0. The five suites
still red belong to the band, audio and match slices.

**Traps, written down before they bite:**
- **State numbers.** `CONTROLLED` must be 8 and `HEALING` 9. They come after
  `DEAD` so that a saved state number keeps its meaning.
- **Formation slots are keyed on `get_instance_id()`.** `verify_squads`
  therefore asserts them loosely (x > 2000, within 300 of the hero), and the
  port must not pin exact slot coordinates either.
- **A freshly spawned unit's row is `randf()`.** Its y is not deterministic
  in the original, so nothing here may assert one.
- **The capture point's scan accumulator resets to zero.** It does not
  subtract the tick, so at 0.1 s steps it scans every 0.2 s. The port has to
  copy that exactly.
- **The capture army-damage registry is global** in the original. The port's
  copy must be per-match, or reset between tests, or one test's banner
  changes another's combat numbers.
- **Research is paid when it is queued, not when it completes.**
- **The `attack` tone is now noise:**
  `fposmod(sin(i·12.9898)·43758.5453, 1)·2−1`, mixed 0.65 with 0.35 of a sine.

## Slice 2: the stat fields

**`UnitStats`** reads the seven fields the hero-first game added to
`units.json`: `can_build`, `supply` (default 1), `hp_regen`, `controllable`,
`heal_amount`, `abilities` (in order) and `can_follow`.

**`BuildingStats`** reads four: `auto_train_default`, `supply` (default 0),
`hp_regen` and `hero_respawn`. `sprite` stays with the layer.

**`heal_amount` joins the saved stats block, and becomes upgradable.** The
original added it to `_SAVE_INT_FIELDS` with the priest. Its upgrades set any
stat by name (`stats.set(field, cur + delta)`), so a heal upgrade needs no code
there. The port's explicit `ApplyDelta` table gains the line. Without it, the
snapshot suite's rule that every saved field is upgradable fails, and it
should.

**Pinned in `test_wb_data`**, from the shipped rows:
- The priest heals 8, deals no damage, is trained at the temple, and is not
  controllable.
- The hero is controllable, casts Cleave then Dash, and costs 2 supply.
- Only soldier, archer and priest can follow.
- The Town Hall and the Waystone respawn the hero; the barracks does not.
- The hall auto-trains workers and the temple auto-trains priests.
- Supply: the hall gives 8 and a farm 4.

**Result (GCC, 10 September):** data 126/0, snapshot 160/0, and nothing
regressed. The five red suites are the same five, for the same reasons.

## Slice 3: the band, 2D movement and parking

**The band.** `world.json` sets `ground_y` 590 and `lane.depth` 280, so the
walkable band is [590, 870]. A unit reads it through `GameState::Data()`, where
the original's static `_clamp_to_band` reads `DataLoader`. A level override
would therefore reach it too.

**What became 2D, and what did not.**
- Move orders take both axes, with y clamped onto the band.
- A worker walks to the actual tree, deposit, site and flee destination, and
  melee closes on the target's actual position. All of it goes through one
  `ApproachTo`, from `_approach`.
- Every scan stays x-only: nearest node, deposit, site, enemy unit and enemy
  building. So do ranged attacks and a raider's march. `verify_units` pins the
  lane's half of this, and the port now does too.

**Parking.**
- A move order parks a worker: once it arrives, it holds instead of seeking
  work.
- A gather, build or attack order unparks it. `CommandGather` is new.
- `parked` is saved. A save without it loads as unparked.

**Build assist is a data flag.** `economy.auto_assist_build` ships on. When it
is off, an idle worker ignores unfinished sites and only an explicit build
order resumes one. The off case reads a scratch `economy.json`, because the
shipped data is shared.

**Placement** takes the pointer's row, clamped onto the band. A site is valid
unless a player building overlaps it in x *and* stands on a row closer than
`lane.building_row_gap` (115). `IsValidAt(x, y)` stays a pure predicate,
because the harness probes y = 950, which is below the band.

**Commands** gains the original's work-before-walking branches:
- A right-click on an unfinished site builds it, and on a tree gathers it. So
  does a tap with workers in hand.
- A selection without workers falls through to its usual handling.
- Group moves stagger their rows by half a spacing.
- `Selection::ResourceAt` is new.
- Rally points wait for slice 5, with production.

**Numbers reproduced** (Godot at `50741d1`, re-run 10 September):

| harness case | the original printed | port |
|---|---|---|
| `verify_units` FSM | y clamped onto the band (590.0) | same |
| `verify_units` lane | sky click to the band top; in-band moves change rows; nearest enemy by x | same |
| `verify_economy` 2d | closest 32 to the tree, 106 to the deposit, +20 banked | same |
| `verify_economy` 2e | parked, tree still 100; 90/100 after a gather order | same |
| `verify_combat` 4b | closest 91 ≤ 100, dy 64, damaged | same |
| `verify_buildings` placement | same row invalid, y 950 valid, y 860 invalid | same |
| `verify_buildings` 6 | assist on: joins and completes; off: ignores; `command_build` assigns | same |

**Three fixtures had to change:**
- `test_wb_worker`'s move case asserted that an order's y is dropped. It now
  asserts the clamp, as the harness does.
- Its long-step case reached idle through a move order. A parked worker never
  seeks work, so the case could no longer tell one tick from forty. It now
  reaches idle from a flee.
- `test_wb_snapshot` put a worker on the farther tree with a move order and let
  it pick the tree up. It now sends a gather order.

**The HUD case had been passing vacuously.** Slice 0 blamed the band clamp,
but the real cause is the fixture:
1. A tap 120 px along the unit's row lands on the third starting worker and
   selects it.
2. At `ebf3d27` the first worker then walked to the tree at 1900 on its own,
   so "it moved" held anyway.
3. At `50741d1` a tree stands at 1650, inside its gather range, so the worker
   stays put.

The case now taps a row down, asserts that the target is empty ground, and
asserts that the unit ends up parked.

**Found for slice 5:** `verify_combat`'s soldier now ends on **38**/60, not 36.
The fight is on one row, so the 2D approach is not the cause. Passive regen is:
`hp_regen`, with `hp_regen_delay_s` 4. `test_wb_combat` still pins 36, and
moves to 38 when regen lands. *(Slice 5 found that 38 is only one side of a
race in the harness. The deterministic answer is 45; see there.)*

**Result (GCC, 10 September):** worker 90/0, combat 60/0, buildings 97/0,
placement 91/0, hud 129/0 and snapshot 160/0. Still red:
- audio 5 (slice 11);
- match 33 (slice 10);
- selection 5: the hall probes at y = 720, which wait for the Town Hall's row
  at 640.4 (slice 10).

## Slice 4: the lane's two new queries

`Lane` gains the two scans the hero-first game added. Both are 1-D, like every
combat query:
- **`EnemiesWithin`** is every living enemy with |dx| within range, in
  registration order. It is the hero's Cleave list.
- **`NearestWoundedAlly`** is the nearest other living unit of the caller's
  own faction that is below max hp. It is the priest's target scan. The caller
  is excluded, and the later-registered unit wins a tie, as in `NearestEnemy`.

`Unit::SetHp` is a raw write, as the original's public `hp` field takes one.
The harnesses wound and restore units through it. Healing proper arrives with
slice 7.

**Reproduced:** `verify_casters` B's four lines at `50741d1`: the nearest
wounded ally is picked, a full ally and an enemy are skipped, the range gate
holds, the widened range finds the far ally, and a wounded priest never
targets itself. The Cleave list is pinned on its inclusive edge, a corpse, the
caller's own side and registration order.

**Result (GCC, 10 September):** combat 69/0. Nothing else moved.

## Slice 5: supply and production

**Supply is derived, never stored**, as `game_state.gd` derives it:
- The cap is what the player's complete buildings provide: the Town Hall 8,
  and 4 for each farm.
- The use is every living player unit plus every unit waiting in a player
  building's queue. A queued unit reserves its space.
- The original counts off its scene-tree groups. `World` gains the same two
  lists, `PlayerBuildings()` and `PlayerUnits()`, and `Supply::Cap`, `Used` and
  `HasRoomFor` count off them.
- `EnqueueTraining` refuses a full town. The bar asks first, and neither greys
  out silently nor takes the money: a full town's button reads "no space -
  build a Farm".

**Buildings feed their own queues.**
- Auto-train is seeded from `auto_train_default`, filtered to what the building
  trains.
- It runs on a ~2 Hz clock that resets to zero rather than carrying the
  remainder, as the original's does.
- It queues one unit at a time, and only into an empty queue, so a manual order
  always goes next.
- It is round-robin over the enabled ids.
- It pays the normal price but never dips into `economy.auto_train_reserve`.
- It is the player's only, and the toggles are saved.

**Research takes time.**
- It is paid when it is queued on the building, and landed when it finishes by
  `Upgrades::CompleteResearch`. That marks it, raises the standing buildings,
  and is idempotent.
- The building announces `upgradeResearched` itself.
- The bar pays and queues instead of researching instantly, and shows
  "Researching..." while it runs.

**Rally points and recruit squads.** `unitTrained` carries the original's four
arguments. `Match` orders a trained unit to its building's rally point. The
squad is carried, unused, until squads exist (slice 7). Both are saved.

**Passive regen**, for units and buildings: after `hp_regen_delay_s` of peace,
`hp_regen` a second, accumulated in whole points, and a hit pauses it.
`UnitStats::hpRegen` is a double, unlike its neighbours. It feeds an
accumulator, and 0.8f crosses each whole point on a different step than the
original's 64-bit 0.8 does.

**Save:** a building's `auto_train`, `recruit_squad`, `research_queue`,
`research_progress` and `rally` (`[]` for none), in the original's shapes. A
save without `auto_train` keeps the data's seed.

**Reproduced** (`verify_buildings` at `50741d1`, re-run 10 September):
- **3**: no rally emits INF; a trained unit carries the rally; the rally
  round-trips through a save.
- **3b**: research is not done after 1 s, is done once its time elapses, and
  the queue empties.
- **3c**: the cap is hall plus farm (12); a living unit and a queued soldier
  use their space; a full town refuses; the storehouse is a deposit point.
- **3e**: all eleven auto-train lines, including the reserve floor at exactly
  cost plus reserve, the round-robin, and the supply cap.
- **3d**: a worker regenerates "20 -> 23", not inside the delay, and a hit
  pauses it.

**The soldier's hit points are a race in the oracle.** `verify_combat` prints
45 on some runs and 38 on others, on the same machine and project.
- A probe ran the harness's fight in a scratch copy of the game, with the
  soldier's hit points logged per step.
- The original staggers each unit's first thinking tick by `randf()`. When the
  draw lets the raider's fifth blow land at 5.1 s, a step before the soldier's
  killing blow, the soldier ends on 30 + 8 = 38. Otherwise it ends on 36 + 9 =
  45.
- It printed 38 in two runs of seven.
- With the stagger zeroed, as the port runs, the original's own code gives 45
  on every run, on exactly the port's hit timeline. `test_wb_combat` pins 45.
- The harness asserts only "hp < max", so it passes either way.

**Fixtures that had to change:**
- Every manual training case gains a hall for supply and turns its barracks'
  production off, exactly as the original's harness clears `auto_train` for
  the manual path.
- The snapshot suite's training boards gain a hall.
- `test_wb_hud`'s drain can no longer empty the treasury by clicking. Supply
  stops it at five workers, 50 wood short. It now buys two through the bar and
  spends the rest directly, and counts only a button greyed on cost, not for
  want of space.
- "A winner stops swinging" asserted that the winner's hit points stay equal.
  They now rise, so it asserts they never drop.

**What auto-production did to `test_wb_match`:** eleven new failures. The Town
Hall now trains workers from the first half-second of a boot, which moves the
wood, unit-count and gathering baselines. They belong to slice 10's re-pin of
the match against `main.gd`, which auto-trains the same way.

**Not yet in the bar:** the per-building auto-train toggles, the recruit-squad
switch and rally placement. They are presentation, and the simulation calls
under them exist.

**Result (GCC, 10 September):** buildings 137/0, combat 69/0 (the soldier on
45), worker 94/0, hud 131/0 and snapshot 160/0. Still red:
- audio 5 (slice 11);
- selection 5 (slice 10);
- match 44 (slice 10): slice 0's 33, plus the eleven auto-production moved.

## Slice 6: capture points

**`CapturePoint`** ports `capture_point.gd`:
- **A tug of war on presence.** Presence is scanned at about 8 Hz and measured
  along the lane. The scan clock resets to zero rather than subtracting the
  tick (the trap recorded above), so at 0.1 s steps it scans every other step.
- **Progress** runs toward +1 (the player) or −1 (the enemy), and freezes while
  both sides stand in it. The holder changes only at the extremes, or to
  neutral when the tug crosses 0. Every change is announced on
  `captureChanged`.
- **Two bonus kinds.** An income point trickles its resource while the player
  holds it. An army_damage point multiplies the player's unit damage. The
  legacy flat `{resource, rate}` form reads as income.
- **`ToSave` and `FromSave`** carry the tug and the holder. They are wired into
  the snapshot in slice 9.

**The army bonus is per run.** The original keeps a static registry on the
class, which outlives the scene, so one harness's banner changes the next
one's combat. The port keeps the product on `GameState`, in the order the
points were taken.
- A point deregisters itself when it is destroyed, as the original's
  `_exit_tree` does, and `Match` destroys its points with the board.
- `Unit::EffectiveDamage()` is damage times the product, rounded, never under
  1, and for player units only. It feeds melee blows and arrows alike, as
  `_effective_damage` does.

**`Match`** spawns the level's points on both boot paths, as level furniture,
on the band's middle row: 590 + 280 / 2 = 730. It ticks them first, because
`main.tscn` lists CapturePoints before Resources, Buildings, Units and
Projectiles.

**Reproduced** at `50741d1`:
- `verify_economy` 8: player presence captures the point, the tug reaches +1,
  "held point trickles wood (+4)", the save round trip, and the enemy taking
  it back.
- `verify_economy` 9: the four banner lines.
- `verify_levels`' six capture-config lines.

**Added by the port:**
- a contested point freezes;
- a flip passes through neutral;
- two banners multiply;
- a destroyed point takes its bonus with it;
- a second run never sees the first run's banner;
- the shipped War Banner turns a soldier's 8 into 10 and leaves a raider's 6,
  and arrows carry it too.

**Not yet:** the layer does not draw the points, and a Continue does not
restore their tug. That is slice 9.

**Result (GCC, 10 September):** economy 79/0, combat 74/0 and data 131/0, and
nothing regressed. Still red, and unchanged: audio 5, selection 5 and match 44.

## Slice 7a: squads, the leash, priests and the shelter bell

Slice 7 is split in two. 7a is everything a unit decides for itself; 7b is
the hero under the player's hand (`CONTROLLED` and `HeroControl`). 7a's cases
set the world's hero directly, as the harnesses set `HeroControl.hero`, so
7b is not a prerequisite.

**The unit** gains the army layer of `unit.gd`:
- **States.** `CONTROLLED` = 8 and `HEALING` = 9 are appended after `DEAD`,
  where the original appends them, because the ordinal is a save format;
  `test_wb_snapshot` pins both. `FromSave` now takes states up to 9 and maps
  `CONTROLLED` to `IDLE`. That is two edits, and both are needed: widening
  the range alone would restore a unit that thinks nothing.
- **The defender.** The garrison holds, acquires in aggro while idle, and
  walks back to its post (`_return_home`, not within 60 px, no post means
  hold). The warband keeps station on the hero, engages on the march, and
  breaks off a fight past the leash (500 px, lane `|dx|` from the hero).
- **The scan is leash-aware.** A warband unit past the leash sees nothing, so
  the break-off cannot flip-flop at the tick rate.
- **An AI march stays combat-aware; a player's order does not.** The two
  are one `MOVING` state, told apart by `_ai_march`, which `CommandMoveTo`
  clears.
- **The priest.** Healing outranks following. The cast shares the attack
  cooldown; `ReceiveHeal` clamps, announces what actually came back on the
  new `EventBus::healed`, and does not touch the regen clock. A priest gives
  up a chase past its leash, measured from the hero in the warband and from
  its post in the garrison. `CommandAttack` is a no-op for a healer.
- **The shelter bell.** A worker's tick checks `WorkersSheltered()` first,
  runs for the nearest drop-off, and stands there until the bell rings off.
- **Facing** is now simulation state, because the warband's slots hang
  behind the hero's facing. It turns where the original calls `_face`: on
  every step, and toward the tree, the site, the target and the patient.
  Enemies start facing left.

**Formation slots are not reproduced.** The original fans the warband out by
`get_instance_id()`. The port puts the same arithmetic on a formation key,
the unit's index on the board. No test pins a slot, and neither does the
harness: it asserts "marches to the hero's side" and "closed most of the
gap".

**The world** answers two new questions: `Hero()`, per world rather than
the original's static, and `NearestWoundedAlly`, off the lane.

**The match** gives a trained follower its building's squad (a worker stays
in the garrison) and a post at its rally point, else where it appeared, and
drops the hero with the board.

**The save** carries `squad`, `home` (`[]` for none) and `ref_heal`. Snapshot
counts `ref_heal` among the references it reports, and the digests in
`test_wb_match` and `test_wb_snapshot` blank it along with the other refs,
because a raw sid changes between a capture and its recapture.

**`Squads.hpp`** ports the horns: `RallyAll`, `SendHome` and `Counts`, over
living `can_follow` player units.

**Reproduced** at `50741d1`, in a new suite, `test_wb_squads`:
- `verify_squads`: all 30 lines, A to E;
- `verify_casters` C, D and E: 16 lines. A is `test_wb_data`'s and B is
  `test_wb_combat`'s.

**Found while porting.** My first mid-march case started the follower 1000 px
from the hero. That is past the leash, so the leash-aware scan saw nothing
and it did not engage. The harness walks the follower to the hero first. The
port was right and the test was wrong; the case now asserts both halves.

**Added by the port:**
- the horns skip a worker, the hero and the dead;
- a player's move order interrupting a march home does not fight;
- no post, near the post, a warband with no hero, and a dead hero all hold;
- with no post and no hero a priest has no leash;
- zero heals and corpses are no-ops;
- a heal does not pause passive regen (the healed soldier stays exactly the
  heal ahead of an unhealed one);
- a heal target nobody rebuilt is counted as unresolved;
- state 9 loads as `HEALING`, 8 as `IDLE` and 10 as `IDLE`;
- a save from before squads loads as a garrison with no post;
- a trained unit's squad and post, through a real `Match`;
- the sheltered worker refuses a tree beside it, and ringing the bell off
  sends it back to gather.

**Not yet:** the heal sound (the original plays it from the priest; the layer
has no listener until slice 11), and a hero in the match (7b possesses one,
slice 10 spawns it at boot).

**Result (GCC, 10 September):** squads 99/0, and nothing regressed. Still
red, and unchanged: audio 5, selection 5 and match 44.

## Slice 7b: the hero under the player's hand

**The unit's `CONTROLLED` state** ports `set_controlled`, `set_control_dir`,
`_step_controlled`, `_move_clamped` and the manual strike:
- **Taking control drops the fight in hand.** Letting go returns to `IDLE`,
  so the AI re-acquires on its next tick. A dead unit refuses, and asking for
  the state it is already in does nothing.
- **The AI does not think at all** while controlled. That guard landed with
  7a, at the top of `TickAi`.
- **Steering** is clamped to unit length, dividing by the length the way
  Godot's `normalized` does. Movement is 2D at `move_speed`, with x held
  inside the world and y inside the band.
- **The strike** probes the lane at `attack_range + 80` and holds to
  `attack_range` plus the target's half-width. It faces the click, or the
  target in reach, spends the cooldown, and is heard even on a whiff. Damage
  lands only in reach, and a frozen board takes no strike. A ranged unit
  under control fires an arrow instead.
- **Nothing here is saved.** The steering is input, and 7a's load already
  maps `CONTROLLED` to `IDLE`.

**`HeroControl`** ports `hero_control.gd`, minus the camera. Possession
refuses the null, the dead and the uncontrollable. The stick outranks the
keyboard while held. The hero's death ends control and emits `heroLost`
with where it fell. The original's two node signals are members.

**The match** owns one, answers `World::Hero()` from it, and makes it forget
the hero when the board is cleared. Nothing possesses a hero at boot yet:
slice 10 spawns and possesses the starting hero and runs the respawn.

**Reproduced** at `50741d1`, in a new suite, `test_wb_hero`:
- `verify_hero` B (13 lines) and F (13 lines);
- A's two data lines.

The harness zeroes `_attack_cd` by hand between strikes. The port steps a
second instead, which under control and with no steering does nothing else.

**Added by the port:**
- a diagonal is normalised and a half deflection is kept;
- the world's left edge holds;
- taking control drops the fight in hand;
- a strike turns to a threat behind. The harness's hero already faces
  right, so its "strike faces its target" holds before the strike runs;
- a whiff is heard;
- a frozen board takes no strikes;
- every intent is a no-op with nobody possessed;
- the attack button and the click reach the unit through `HeroControl`;
- a `Match`'s warband follows the possessed hero, and a fallen hero leads
  nobody.

**Not yet:** abilities (slice 8), the respawn flow (slice 10), and input
routing and the camera (slice 11 and presentation).

**Result (GCC, 10 September):** hero 52/0, and nothing regressed. Still red,
and unchanged: audio 5, selection 5 and match 44.

## Slice 8: abilities

**`Unit::UseAbility`** ports `use_ability`, `_cast_aoe_damage` and
`_cast_dash`:
- **Casting is refused** unless the unit is under control. It is also refused
  for a slot out of range, on a frozen board, while that ability cools down,
  and for an id `abilities.json` does not define. A cast starts the
  ability's cooldown and announces `abilityUsed` with the id. The sound and
  the screen shake are the listener's, looked up by that id.
- **Cleave** hits every living enemy within its radius along the lane, on
  both sides, for `damage_mult` times the effective damage, rounded. A held
  banner therefore strengthens it. The world answers a new
  `EnemiesWithin` off the lane, which every fixture now implements.
- **Dash** flies `distance` at `speed` along the steering, or along the
  facing when standing. It overrides the steering until spent, stays inside
  the world and the band, and grants `invuln_s` of i-frames. Letting go of
  control drops a dash in flight; only the i-frames run on.
- **I-frames.** `TakeDamage` returns before anything else while they last:
  a dodged hit neither hurts nor pauses regen.
- **Cooldowns** tick in every state and ride the save as `ability_cds`. A
  dash in flight and its i-frames do not, as in the original.

**`HeroControl`** routes `UseAbility(slot)` and reports each slot's cooldown
for the buttons.

**Reproduced** at `50741d1`: `verify_hero` G's fifteen simulation lines,
from "hero has two ability slots" to "dash clamps to the band", in
`test_wb_hero`. The Q/E routing and the on-screen buttons are input and
presentation, and belong with slice 11 and the layer.

**Added by the port:**
- nothing is hit and nothing is announced when a cast is refused;
- a cleave's damage is pinned (22 × 1.6 rounds to 35), and so is its
  cooldown after half a second (7.5);
- a banner makes one cleave kill a raider that a plain one leaves on 5;
- the dash lands within a hundredth of 260 px, not just the harness's two;
- a standing dash follows the facing;
- releasing control ends a dash in flight;
- a dodged hit does not pause regen;
- an undefined ability and a frozen board cast nothing;
- `HeroControl` casts and reports by slot, and answers 0 out of range or
  with nobody possessed.

**Result (GCC, 10 September):** hero 89/0, and nothing regressed. Still red,
and unchanged: audio 5, selection 5 and match 44. The build also carried
slice 9's resource-node sprite, which left one extra red each in snapshot and
match; with `sprite` stripped from their digests, the captures matched line
for line, so this slice is clean and the difference is slice 9's to close.

## Slice 9: the save

Most of the restructure's save fields arrived with the slices that brought
them:
- the unit's squad, post and heal target (7a) and ability cooldowns (8);
- the building's auto-train, recruit squad, research and rally (5);
- the run's shelter bell and pending respawn (1).

This slice adds the two the snapshot itself had to learn.

**Capture points.** Capture writes each point's tug and holder as
`capture_points`, index-aligned to the level's list. Restore puts them back
into the points the boot already spawned. The Match hands those over through
a new `RestoreSink::CapturePointsToRestore`. It is empty by default, so a sink
with no level furniture restores as before. A save without the field
restores with neutral points rather than being refused: there is no version
bump, which is how the original added it too.

**Resource-node art.** A node carries its `sprite` from the economy file at
spawn and through the save. A save written before art has none, and the
restore takes today's from the economy file (`ResourceNode::SpriteFor`). A
saved sprite is an override and wins. The fallback lives in the restore
rather than in `FromSave`, because only the restore has the data.

**Found while porting.** The first build with the fallback turned two round
trips red: a node spawned with no sprite came back with one. The original's
nodes always spawn with the economy's sprite. The Match now does too, and so
does the snapshot suite's hand-built board.

**Reproduced** at `50741d1`: `verify_snapshot` 11, "a pre-art save re-adopts
today's sprites (22/22 textured)", through a real boot in `test_wb_match`.
The rest of `verify_snapshot`'s numbers (8 units, 220 wood, 3 or 4
gathering) come from a real boot of HEAD's board, and they are slice 10's.

**Added by the port:**
- the fallback and the override on a hand-built board;
- a resource with no art stays flat;
- the two level_1 camps' tug and holder, and the War Banner's bonus, survive
  a restore;
- a save without capture points restores them neutral.

**Result (GCC, 10 September):** snapshot 168/0 and the three new match
tests green, with nothing regressed. Still red, and unchanged: audio 5,
selection 5 and match 44, all of them slices 10 and 11.

## Slice 10: the match boot, the hero and the respawn

**The row policy comes first, because the rest depends on it.** HEAD's
`spawn_unit` ignores the caller's y. It deals every fresh spawn a random row
in the band, `_ground_y + randf() * _lane_depth`, and does the same for every
resource node. The port does not draw. A fresh unit and a node stand on the
ground line, as before. There are three reasons:
- **A draw would break replay.** It is the one thing that makes a replay
  diverge from the run it replays, which is why the AI stagger is zero
  (`Unit.hpp`). An RNG in the sim becomes save state, and a Continue that
  failed to restore it would diverge from a session that never suspended.
- **The row is presentation.** HEAD's comment says the scatter exists so
  y-sorting draws near units in front of far ones. Its one effect on the
  sim, the length of a 2D walk, follows from a drawing decision.
- **The oracle cannot hold it steady.** Over seven runs of HEAD's board
  (four of `verify_snapshot`, three of a probe of it), the gathering count
  was 4 four times and 3 three times. The unit count, the wood and the hall's
  hit points never moved.

A hashed row, like HEAD's decor scatter, was the tempting middle ground. It
is rejected: it is deterministic, but it reproduces nothing, and every 2D
walk would become a port artefact with no oracle behind it. The port
reproduces the decision and declines the draw, the same rule slice 3 (the
band) and 7a (formation slots) already follow.

What the port does reproduce is the Town Hall's row. A pre-placed building
stands at `groundY + laneDepth * building_row` = 590 + 280 × 0.18 = 640.4.
That is a formula, not a draw. Placed buildings keep the pointer's row, as
before.

**The starting hero.** `economy.starting_hero` spawns at
`player_spawn_x + 180` = 1860 and is possessed at boot, so the player is the
hero from the first frame. If the id is empty or has no row in `units.json`,
the boot spawns nobody.

**The respawn.** When HeroControl's `heroLost` fires:
1. The Match marks the respawn pending and records `hero_down_x`.
2. A sim-time timer runs for `hero_respawn_delay_s` (10 seconds).
3. The Match takes the complete, living `hero_respawn` building nearest the
   death x: the Town Hall or a Waystone.
4. The hero spawns at that building's x plus half its body plus its
   `spawn_offset`, and is possessed. If no such building is standing, the
   run is lost.

The timer is an accumulator the Match steps after the director. Godot runs
scene-tree timers after the nodes, and that order is argued from the engine,
not measured. On a restore, the Match finds the living controllable unit,
possesses it and clears any stale mark. A save with no such unit re-arms the
full delay at the saved death x.

**The selection reds were not about the row.** `verify_buildings` builds its
own hall at (1500, 800). The port's suite reused the boot's hall, which the
new ground line moved from under its probes. The suite now builds the
harness's hall.

**The ledger, from a probe of HEAD's board.** The probe runs
`verify_snapshot`'s boot and drive, logs every birth and every change in
resources, and was run three times:
- **Boot:** 240 wood (300 × 0.8 on hard) and 80 food; three workers; the
  hero, possessed, at 1860; the hall at (1500, 640.4); 22 nodes.
- **Step 3:** the Town Hall auto-trains a worker for 50 wood. It is born at
  step 27, at x 1600 (1500 + 60 + 40).
- **Steps 40 and 80:** the barracks' two soldiers, at 2395.
- **Wood:** three deliveries of ten, so 240 − 50 + 30 = 220.
- **Units:** 8 = three workers, the auto-trained one, the hero, the raider
  and two soldiers.
- **Gathering:** 4, 4 and 3.

**Not ported here:** `verify_respawn` C and D, the proximity panel and the
build menu's slow-motion, belong to the bottom bar, which is UI. The camera's
glide to the respawn site is presentation.

**Reproduced** at `50741d1`:
- `verify_respawn` A, B and E. The harness passes 11/0; C and D are the
  bottom bar's.
- `verify_snapshot`'s 8 units, 220 wood and 3-or-4 gathering, through a real
  boot, with the auto-trained worker born at 1600.

**Added by the port:**
- the nearest site wins, and an unfinished or ruined one does not count;
- no site standing loses the run;
- a run that is over brings nobody back and arms nothing;
- a restore with a living hero clears a stale mark;
- an empty or unknown `starting_hero` spawns nobody.

**Found while porting.** The placement suite counted every non-idle unit as
a busy worker. The boot's hero is Controlled, not Idle, so it now counts
workers only.

**Result (GCC, 10 September):** match 317/0 (was 44 red), selection 93/0
(was 5) and placement 92/0, and nothing else moved. The only red left is
audio 5, which is slice 11.

## Slice 11a: the eighteen sounds

Slice 11 was three unrelated things in one line: audio, the hero-mode
pointer and the control scheme. They share no code and no oracle, so it is
two slices. This one is the audio, and it closes the last red.

**The measurement.** A second probe runs inside a copy of the game project
and calls the game's own `Audio.stream_for` for every sfx in `audio.json`,
printing the suite's row: samples, bytes, three landmarks, the peak, and the
sum and sum of squares over the whole buffer. The ten rows that did not
change reproduced the 27 August table to the byte. `attack` changed: it is
now a 50 ms noise tone of 2,205 samples. The seven new sounds are gather,
hammer, capture, gather_food, heal, cleave and dash.

**The noise wave.** Each sample hashes its index through
`fposmod(sin(i * 12.9898) * 43758.5453, 1.0)` and mixes 0.65 of that with
0.35 of a sine at the spec's frequency. `fposmod` is a helper rather than a
bare `fmod`: the hash is negative about half the time, and Godot's
correction is the whole of the branch. All five noise rows matched Godot's
Windows build to the byte under GCC and glibc, sums included. That was not a
given, because the hash asks two C libraries to agree about the sine of
arguments up to about 355,000 radians, amplified before a truncation.

**The truncation trap moved.** It had been pinned on `attack` at 55 ms,
which is now a whole 2,205 samples. Fourteen of the eighteen durations
divide evenly. The four that do not are shoot and gather_food at 45 ms,
hammer at 55 ms and gather at 75 ms, and those are the ones pinned now.

**`falloff_linear`** is a pure function, `Audio::FalloffLinear`, pinned at
seven distances against the game's own function. The guard that keeps
`silent_px` at least a pixel past `full_px` is unreachable at shipped values,
so it has a scratch-data case.

**The layer.** It registered a fixed list of the original's eleven
constants, so the seven new sounds were synthesised and never reached the
mixer. It now registers every id the data names, as `stream_for` resolves
any. Three new edges come from signals that already existed:
- `captureChanged` plays capture;
- `healed` plays heal, throttled like the combat sounds, as the original
  throttles it;
- `abilityUsed` plays the ability's own `sfx` from `abilities.json`.

**Not ported, and written into the layer's header:**
- **Gather:** it fires on a sprite's impact frame, and there is no frame
  clock to hang it on.
- **Hammer:** it is a per-frame build tick throttled to 380 ms, a per-frame
  emission for a cosmetic.
- **Positional falloff:** it needs the camera's centre in sim space.

**Found while porting:**
- The research test failed because it looked for a researching building on
  the boot board. The restructure moved research from the Town Hall to the
  Armory and the Storehouse, so the test now places an Armory.
- Research also takes time now. The announcement is asserted when the
  research lands, and asserted absent at the click.
- The unknown-waveform case had used "noise" as its made-up name.

**Result (GCC, 10 September):** audio 419/0 with a device and 393/0
without one (was 5 red); the suite's floor is now 393. **Every one of the 17
suites is green.**

## Slice 11b: the control scheme and hero-mode routing

**`ControlScheme`** is header-only, from `control_scheme.gd`. `Resolve`
works like this:
- An explicit desktop or touch preference wins.
- Anything else is detected: AUTO, an empty value, or a value a hand-edited
  save invented.
- Detection gives touch on Android and iOS and desktop everywhere else, from
  the engine's platform macros, which stand in for Godot's
  `OS.has_feature("mobile")`.

`Label` spells out what AUTO detected, for example "Auto (PC)". The
preference itself was already the Profile's `control_scheme`.

**`HeroInput`** is the hero-mode half of `input_controller.gd`. It is a
stateless router that sits above `GestureMachine` rather than inside it. The
original's `_handle_hero_input` is a three-branch dispatch that remembers
nothing, and the gesture machine's value is being a touch state machine
testable without a viewport. Folding a mouse and a keyboard into it would
spend that for nothing. The routing:
- **Placement outranks hero mode.** A primary click confirms, and a
  secondary click or Escape cancels.
- **In hero mode:** Q and E cast slots 0 and 1, and Escape backs out. A
  primary click strikes, on the desktop scheme only. Everything else is
  inert, including right clicks, touches and releases.
- **Outside hero mode:** events pass through to the gesture path, except
  Escape, which deselects.
- **The scheme is cached when hero mode is entered,** as the original caches
  it.
- **`SteerFromKeys`** is `_keyboard_dir` plus the one line that shapes it for
  the hero: a diagonal is normalised only when longer than one, dividing by
  the length in single precision the way Godot does.

**Reproduced** at `50741d1`, in the new `test_wb_controls`:
- `verify_hero` A's four scheme lines;
- all six of D's routing lines;
- G's "Q outside hero mode is inert" and "hero mode: Q -> slot 0, E -> slot
  1".

The harness passes 72/0. "After hero mode, a tap is a context tap again" is
followed through the real `GestureMachine` rather than asserted of a
pass-through flag.

**Added by the port:**
- placement's secondary click and Escape both cancel, and a touch during
  placement is the gesture path's;
- outside hero mode, only Escape is answered;
- the scheme is taken at entry;
- keyboard steering: one key is exactly one, opposite keys cancel, and a
  diagonal is exactly 1/√2 per axis.

**Not yet:** the layer does not route keys or clicks to the hero at all.
There is no WASD steering, no strike on click and no Q/E. So the router is
pinned, but it is not yet wired into the layer. That wiring belongs to the
presentation work, along with the camera follow and the on-screen joystick
(`verify_hero` C and E).

**Result (GCC, 10 September):** controls 48/0, and nothing else moved. All
**18 suites are green**, so `wb-resync` is ready to merge into
`engine-roadmap`. That branch has not moved since `a6472b8`, so the merge
is a fast-forward.
