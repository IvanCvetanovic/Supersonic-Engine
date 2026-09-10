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
