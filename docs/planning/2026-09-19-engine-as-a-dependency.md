# The engine as a dependency of a game's repository — 19 September 2026

The three ports built on this engine — Wolf Brigade, HUSK and Magic Portals —
leave `games/` for repositories of their own, each carrying the engine as a git
submodule pinned to a commit. The owner decided this on 19 September 2026, with
one condition: the move must not change what any game does, and every game's
output after the move is proven byte-identical to its output before.

Every game was written inside this tree, so every game's build took things from
the tree without naming them. Before the games can leave, the engine has to hand
those things over through targets and documented variables. That is step 1, and
it changes nothing a game draws or computes. Step 2 removes the games.

## What a game's build was taking from the tree

Three read-only surveys, one per game, read all four repositories at `80a4ed2`
and agreed on four things:

1. **Its targets would be defined twice.** The top-level `CMakeLists.txt` added
   `games/wolfbrigade/sim`, `games/wolfbrigade`, `games/husk` and
   `games/magicportals` unconditionally, and `tests/` defaulted ON and linked
   all of them. A game repository that did `add_subdirectory(engine)` would get
   a second `HuskSim`, `WolfBrigadeGame` or `MagicPortalsSim` beside its own,
   and every other game's targets with them, and the configure would fail. The
   editor executable, the script plugin and the engine's own suites would all
   have come along as well.
2. **Directory-scoped settings do not cross `add_subdirectory`.** C++20
   (`CMAKE_CXX_STANDARD`, lines 4-6) and `SUPERSONIC_PLATFORM_WINDOWS=1` reached
   the games only because `games/` is below the top-level directory.
   `SupersonicCore` carried no compile feature, so a game outside the tree would
   compile the engine's headers as MSVC's default C++14 and fail on the first
   `<numbers>`.
3. **The test rule and its harness were paths.** `add_engine_test` lived in
   `tests/CMakeLists.txt`, and the suites found `TestHarness.hpp` through
   `${SUPERSONIC_SRC}/tests` - a variable set in that one directory.
4. **Every game ran from the engine's root.** The renderer opens its seventeen
   `.spv` relative to the working directory, and so do the pipeline cache, the
   scene and prefab folders and the asset database's scan. A game is packaged
   only when `assets/shaders` sits beside its executable, which a build tree
   never has, so every capture the three ports have ever made was taken with
   the engine's root as the working directory. From a game's own repository
   the shaders are one directory down, in the submodule.

## Decisions

**The top level decides the defaults.** `SUPERSONIC_IS_TOP_LEVEL` is
`PROJECT_IS_TOP_LEVEL`, and `SUPERSONIC_BUILD_EDITOR` (new),
`SUPERSONIC_BUILD_SCRIPT_PLUGIN` and `SUPERSONIC_BUILD_TESTS` default to it; the
games are added only when it is true. The engine built for itself is exactly
what it was. `cmake_minimum_required` stays at 3.20 rather than rising to 3.21
to claim the variable: raising it switches on 3.21's policies across the whole
tree, CMP0126 among them (what `set(CACHE)` does to a variable of the same
name), in a change meant to alter nothing. `project()` sets the variable on any
CMake from 3.21, and an older one gets the source-directory comparison it
stands for.

**The `Shaders` target stays, and only the editor depends on it.** A game's
build therefore never rewrites the committed SPIR-V inside its submodule unless
it builds `Shaders` by name.

**`SupersonicCore` carries what the directory used to.** `cxx_std_20` and the
platform definition are PUBLIC on the library. In the engine's own build both
were already on every line, and the compile database shows no line changed by
them (below).

**The rule and the harness are a target and a function.**
`cmake/SupersonicTesting.cmake` defines `Supersonic::TestHarness`, an INTERFACE
target that puts `tests/` on the include path, and `supersonic_add_test(name)`,
the old body of `add_engine_test` with the harness linked instead of named by
path. It is included unconditionally, because a game turns the engine's suites
off and builds its own with it. `add_engine_test` remains as a one-line wrapper,
so the engine's suites are built by the same function and CI's suite count,
which matches `^add_engine_test\(`, still sees them. `SUPERSONIC_ENGINE_DIR`
(a cache entry, because a directory's variables do not reach its parent) names
the checkout's root for a suite that needs the engine's assets as its working
directory.

**A game settles where its files resolve from, once, by moving the working
directory.** `ChooseAssetRoot` takes the first directory holding
`assets/shaders` of: the executable's own (packaged), the working directory,
and `SUPERSONIC_ASSET_ROOT`, which the build bakes; failing all three it keeps
the working directory, and the run fails on its first `.spv` as it always did.
`SupersonicApp` asks it before anything is opened - before the scene folder is
created and the plugin fallback is looked up, which previously happened ahead
of the packaged-game anchoring - and only for a game; the editor is left alone.

- *Why move the directory rather than prefix paths.* `AssetRoot()` already *is*
  the working directory, and packaged games already anchor by moving it. The
  engine opens relative paths from many places, and a game passes its own.
  One move keeps them agreeing; a root applied per call site is a new rule at
  each one, and a missed one reads from the wrong tree silently.
- *Why the packaged folder outranks the baked root.* The root is an absolute
  path on the machine that built the game. A packaged copy of that game must
  ignore it.
- *Why the working directory outranks it.* So a run from the engine's root -
  every existing run - never consults the root at all.
- *Empty when the engine is built for itself.* CMake leaves
  `SUPERSONIC_ASSET_ROOT` empty at the top level, so the engine's own build
  bakes no path and its compile lines do not change, and sets it to the
  engine's checkout when it is a subproject. It is compiled into
  `ExecutablePath.cpp` alone.
- *What it costs.* Once a game has moved, a relative `--screenshot`, `--record`
  or `--replay` resolves from the engine's root, as it always has for a
  packaged game. Pass those as absolute paths. Rewriting them against the launch
  directory was considered and left out: it would change packaged games too.
- *Left as it was.* The plugin fallback (`build/Release/`, `build/Debug/`
  relative to the working directory) and `cache/pipeline_cache.bin` follow the
  working directory, like every other relative path. A game built with the
  engine as a subproject builds no plugin by default and uses no scripts.

## Step 1, as built

Build: MSVC 14.50, Ninja, Release, validation ON, `/W4`: zero warnings.

The compile database (`ninja -t compdb`) of the engine's own build, before and
after, has the same 1,020 lines. 898 are byte-identical. The other 122 are the
suites' translation units, and each differs only in one token: the harness
directory was spelt `-I<engine>\tests\..\tests` through `SUPERSONIC_SRC` and is
now `-I<engine>\tests` through the target. Nothing outside `tests/` changed:
the PUBLIC `cxx_std_20` and platform definition add nothing to a line that
already had them.

Checked on the executables built from it, each once, from the engine's root,
against the same runs of the executables from before the change:

| Check | Before | After |
|---|---|---|
| `AllPasses.scene`, 40 frames, `[Counts]` | 12 drawn (13), 1 culled, 4 blended, 12 draw calls (13), 4 pipeline binds, 8 mesh binds (9), 9 material binds, 3 particles (6), 47 shadow casters (48), 81 shadow culled (82), 2 skinned matrices | identical |
| `AllPasses.scene` screenshot | `160662a555970e7455ecb68eb7e3925e` | identical |
| Magic Portals `level0`, 1280x720, 420 fixed frames | `52549d7f8b69441aae499b33e83fb5f3` | identical |
| HUSK `first_light`, replay of the survey's recording | 60 of 60 ticks, 2 checkpoints; frames 60, 120, 180 `b832650a`, `fd99aa02`, `b82d402a` | identical |
| Wolf Brigade, replay of the survey's recording, empty `APPDATA` | 600 of 600 ticks, 11 checkpoints; frame 600 `c8af3b36`; autosave `8e155f7b` | identical |

The *before* column was taken on the old executables, and each matched the
game's survey baseline, so it is also the first proof that the HUSK and Wolf
Brigade recordings replay to the pictures their live runs drew.

`test_gameruntime`, which now holds the rule's five cases (19 checks, all on
made-up directories behind a predicate, so the filesystem is never touched):
69 checks, 0 failures. `test_packaging`, whose predicate the rule reuses: 32
checks, 0 failures.

A stand-in for a game repository - a scratch project outside the tree that
builds this checkout with `add_subdirectory`, sets no C++ standard of its own,
links one executable to `SupersonicCore` and builds one suite with
`supersonic_add_test` - configured and built with zero warnings. Its build
graph holds `SupersonicCore`, GLFW, its own two targets and the unbuilt
`Shaders`; no editor, plugin, engine suite or game, and no `.spv` was compiled.
Its suite reads `ConfiguredAssetRoot()` back as `SUPERSONIC_ENGINE_DIR` and
finds `assets/shaders` there: 5 checks, 0 failures. Its executable, declared a
game and launched from an empty directory, logged that its assets resolve from
the engine the build named, rendered 20 frames with validation on, wrote its
screenshot and exited 0 - and left that directory empty.

## Step 2, as built: the games removed

On its own branch, `games-out`, because the three game repositories pin their
submodule to it; `main` stays at step 1 until the owner moves it.

Removed, 536 files, every path the three surveys listed as a port's:
`games/wolfbrigade` (67), `games/husk` (246) and `games/magicportals` (151);
the 18 `test_wb_*` suites and `WolfBrigadeFixture.hpp`, their one helper; the
3 `test_husk_*` and 44 `test_mp_*` suites; and the six records about one game,
listed with where each went in this directory's README. From the files that
stay: the games block of the top-level `CMakeLists.txt` with its three options,
and the three registrars and 241 lines of `tests/CMakeLists.txt`.
`TestHarness.hpp` stays; it is the engine's, and the games reach it through
`Supersonic::TestHarness`.

**The suite count.** CI's documented-counts job added `add_engine_test` and
`add_port_test` lines, 56 + 18 = 74, against "sixty-eight" in the README, AGENTS
and CONTRIBUTING - failing before any of this. With `add_port_test` gone it
counts one registrar, and its pattern now allows digits: `test_light2d` never
matched, so the check had been one short of what ctest runs. 57 suites, which
is the number ctest runs and the number the documents now say.

**Pointers, not scrubbing.** The README says where each game lives (Repository
layout, and a line at the head of the Roadmap), and restates the Android
measurement it used to link into the Wolf Brigade plan for. AGENTS moves the
replay claim `test_wb_hud` carried to where that suite went. ARCHITECTURE says
where the spike record it cites for per-axis locks now is. What merely mentions
a game stays as it was: the Roadmap's entries, ARCHITECTURE's examples, the
comments in `src/` and one in `tests/test_audio.cpp` that names
`test_mp_sprites`.

Measured, in a new build directory outside the tree (MSVC 14.50, Ninja,
Release, validation ON): no compiler warning. The eleven vendored translation
units print MSVC's D9025 note, their `/W0` overriding the target's `/W4`, as
every clean build of this tree does. The `Shaders` target recompiled all
seventeen `.spv` into `assets/shaders`, byte-identical to the committed ones.
ctest, once: 57 of 57 passed, 6,771 checks, none blocked. `AllPasses.scene`
from the engine's root: `[Counts]` identical and screenshot `160662a5`, as
before step 1.
