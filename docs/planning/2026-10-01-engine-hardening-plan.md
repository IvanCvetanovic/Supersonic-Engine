# Hardening the engine without moving anything a game sees — 1 October 2026

The three ports (Wolf Brigade, HUSK, Magic Portals Remake) build this engine as a
pinned submodule, and each holds byte-exact baselines: screenshot hashes, a state
hash, replay files. The question asked was whether anything in the engine can be
improved without disturbing them. It can, and most of it is not gameplay code.

This began as a plan. Phase 0 and Phase 1 have since been carried out, on the
branch `claude/friendly-bohr-jofghe`; the [Outcome](#outcome) section at the end
says what was done, where it differs from the plan, and what was not checked.
Phases 2 and 3 have not been started. The tables below are left as they were
written, so a row may describe a fix as intended where the Outcome says how it
was actually made.

## How this was found

Three read-only audits ran in parallel, and a baseline build was taken first.

**Measured on this checkout** (GCC 13.3, Release, Linux, no GPU):

- All 58 suites configure and compile. 57 pass; `test_jobs` fails intermittently
  under CPU load, at `tests/test_jobs.cpp:139`. Reproduced here 3 times in 5 with
  four busy loops running. A separate run measured 93/500 pinned to two cores,
  which is what a two-vCPU CI runner is.
- 17 compiler warnings, all `-Wmissing-field-initializers` from EnTT instantiating
  `emplace<MeshComponent>` and `emplace<ScriptComponent>`, plus one in vendored
  `stb_image.h`. The README's "zero warnings" is an MSVC `/W4` claim.
- The README badge says 57 suites; `tests/CMakeLists.txt` registers 58. The prose
  in the README, `AGENTS.md` and `CONTRIBUTING.md` all say 58.

**Confirmed by reading the code** (each one re-read at the cited line, not taken
on the audit's word): `UIImageStore::Update` always throws; scene, prefab,
material and `.meta` saves open the file with truncation before writing;
`ComponentCodec` guards about 35 float writes with `jsonSafe` and the audit
counted about 100 more that write non-finite values raw (that count is the
audit's); no `setprecision` anywhere in `src/`; `SpriteAnimationSystem`
computes `columns * rows` in `uint32_t` and loops on `1 / framesPerSecond`;
`findKey` reads one past the end for a NaN time; `ContactTracker` emits Exit
events in `unordered_set` order; `EnvironmentProbe::Load` frees the old cube
images before any `waitIdle`; CI's sanitizer leg sets `detect_leaks=0` and no
`halt_on_error` for UBSan, and the workflow has no `timeout-minutes`.

**Taken from the audits, not yet re-verified:** everything else. Each item below
lands with a test that fails first, which is the re-verification.

**What this could not check.** The engine cannot be run here (no GPU, no ICD), so
the pixel and replay gates below need the owner's machine or the lavapipe CI leg.
And the three game repositories were not readable from this session, so what a
game uses was reconstructed from the pre-move tree (`git show 4bfcf67^:games/...`)
and from public code search. Treat it as a lower bound.

## The rule that decides what is safe

A change is safe if, for every input a shipped game can produce today, the output
is identical: same simulation state, same state hash, same pixels, same bytes on
disk. A change that only alters inputs that already crashed, hung or were
unreadable is safe. Everything else needs the owner's decision.

**Frozen** — do not touch without a decision:

- Tick order, `kFixedPhysicsStep`, the substep rule, the five-ticks-per-frame cap.
- The `StateHash` fields and fold; `kCanonical = 6794834318059694172`; `DetMath`
  bits and the `-ffp-contract=off` / `/fp:precise` flags.
- The replay file format (magic, version 2, delta encoding, float bits in hex).
- Scene, prefab, `.meta`, `.material` and `game.manifest` keys: add-only, never
  renamed or re-unit'd. Json parser semantics (60 files in the pre-move games
  include `Json.hpp`).
- The committed `assets/shaders/*.spv`, and any change that moves a pixel.
- CMake: the target names `SupersonicCore`, `Supersonic::TestHarness`, `Shaders`;
  `supersonic_add_test`; the PUBLIC include roots, definitions and flags; the
  cache variables `SUPERSONIC_ENGINE_DIR`, `SUPERSONIC_ASSET_ROOT` and the three
  `SUPERSONIC_BUILD_*` options; the `ChooseAssetRoot` order.
- Signatures in the game-facing headers (`Components.hpp`, `Input.hpp`,
  `PhysicsSystem.hpp`, `GameRuntime.hpp`, `LaunchOptions.hpp`, `UISystem.hpp`,
  `AudioEngine.hpp` and the registry-context services). Note that
  `SupersonicApp.hpp` includes `EditorLayer.hpp`, `VulkanRenderer.hpp` and
  `Window.hpp`, so a signature change in those recompiles every game even though
  none uses it.

**Free to change:** the editor, `GamePackager`, `HotReloadEngine` and the script
plugin ABI (no game used them), renderer internals that are pixel-identical,
logging, profiling, tests, docs, and anything additive and opt-in.

**One more safety net.** A game pins a commit. Nothing here reaches a game until
that game bumps its submodule, and the bump is where each game's own baselines
run. This plan makes the engine side as provable as it can be before that.

## The gates

Every item names the gates it must pass.

| Gate | What | Where it runs |
|---|---|---|
| **G1** | All suites pass in Release with no new warnings | Here, and anywhere |
| **G2** | The same suites clean under ASan + UBSan + LSan (clean on all 58 today, measured) | Here, and anywhere |
| **G3** | `test_determinism` still pins `kCanonical`; `tools/verify-replay.ps1` passes, including its corrupted-checkpoint step | The owner's Windows machine; a bash port is item 0.9 |
| **G4** | `--frames 120 --fixed-step --screenshot` on the demo scene is byte-identical before and after, and `[Counts]` match | Needs a GPU: the owner's machine, or lavapipe |
| **G5** | The test that fails before the fix and passes after | Here |

## Phase 0 — Nothing in `src/` changes behaviour (size S unless noted)

Gates: G1, G2. These cannot move a game.

| # | Item | Detail |
|---|---|---|
| 0.1 | **Fix the stale facts** | README badge 57 → 58. Seven suites have no row in the README table: `test_bitmapfont`, `test_clustergrid`, `test_detmath`, `test_draworder`, `test_uilayer`, `test_userdata`, `test_worldshapes`. The "19,200 lines / 73 translation units" figures are old (an audit counted 104 `.cpp` units and 39.6k lines). `tools/` is missing from the layout block. |
| 0.2 | **Deflake `test_jobs`** | Make each job wait, bounded to about 200 ms, on an atomic until a second thread id has arrived, instead of trusting the scheduler. The comment above it claims it cannot fail spuriously; it does. A 20% false red on a dispatch-only CI that is expensive to run is the cost. |
| 0.3 | **Warnings to zero on GCC and Clang** | Spell out the trailing members at the `MeshComponent` / `ScriptComponent` emplace sites (`SupersonicApp.cpp` ×6, `InspectorPanel.cpp`, `SceneHierarchyPanel.cpp` ×2, two tests). Move the `stb_image` implementation to its own translation unit listed in `VENDORED_SOURCES`, as `stb_vorbis` already is. Delete the dead `kept` in `MeshRegistry.cpp`. Guard the VMA nullability suppression on Clang rather than only Apple and Android. Then add `SUPERSONIC_WERROR`, applied to the core and the suites only. |
| 0.4 | **CI that can fail** | UBSan has no `halt_on_error`, so undefined behaviour exits 0; LSan is off. Both are clean today, so turning them on costs nothing. Add `timeout-minutes` (a hung suite can burn six hours of the allowance that caused automatic runs to be switched off) and `permissions: contents: read`. Add `workflow_dispatch` inputs so the one-minute `doc-facts` and `shader-check` jobs can run without paying for Windows and ASan. Stays dispatch-only. |
| 0.5 | **Docs that contradict each other** | `AGENTS.md` says Android and iOS "do not produce runnable apps yet" while the README and `ARCHITECTURE.md` describe working backends. `ARCHITECTURE.md` says validation is requested on Debug builds (CI uses it in Release) and audio is a no-op outside Windows and Linux (CoreAudio exists). `AGENTS.md` omits the Linux apt packages, `-DGLFW_BUILD_WAYLAND=OFF`, and `SUPERSONIC_ENABLE_VALIDATION` / `_BUILD_EDITOR` / `_MOLTENVK_LIBRARY` / `_ASSET_ROOT`. State the compilers actually tested (MSVC, GCC 13, Clang 18) instead of "GCC 11+, Clang 13+". |
| 0.6 | **Plain `cmake -B build` works on Linux** | It dies with `Failed to find wayland-scanner` without the Wayland toolchain. Default `GLFW_BUILD_WAYLAND` off when `wayland-scanner` is not found. Changes nothing for a machine that configures today. |
| 0.7 | **Repository hygiene** | Delete the committed `VmaSample_Release_vs2022.exe` and its two `.spv` (`*.exe` is already in `.gitignore`). Add `/test_*_tmp*` to `.gitignore`: nine suites write scratch files into the source tree and only `cache/` is ignored. Fix the three version cells in `THIRD_PARTY_LICENSES.md` that read "—", and list or delete the unused ImGui `misc/fonts`. The 71 MB of unused vendored trees is optional and needs a check that nothing's `CMakeLists.txt` reaches for `deps/`. |
| 0.8 | **Faster tests** | About 6.6 s of the 7 s total run is the `letTheClockMove()` helper, a `sleep_for(1100ms)` that waits for mtimes to differ, in `test_assetwatcher` and `test_materials` (the 6.6 s is an audit figure; the two suites took 4.4 s and 2.2 s in this run's ctest). Set `last_write_time` explicitly instead. |
| 0.9 | **`tools/check.sh`** (M) | `tools/` holds only a PowerShell script. A bash runner with the steps CI already has (`docs`, `shaders`, `build`, `options`, `asan`, plus `tsan` on the six threaded suites, clean today, and `tidy`) so the checks that must not wait for a dispatch can run locally on Linux. Have `ci.yml` call the same steps so they cannot drift again. Includes a bash port of `verify-replay`. |
| 0.10 | **Plugin lands where the engine looks** | `plugins/CMakeLists.txt` puts `GameScripts` in `<build>/Release/` while the single-config executable is at `<build>/SupersonicEngine`, so hot reload only works by the fallback path. Output beside the executable. Editor-only; games do not build the plugin. Check no test hard-codes the old path. |

## Phase 1 — Defects fixed for inputs that already fail (size S–M)

Gates: G1, G2, G5 for each, and G3 for anything near the simulation. For every
item the property is the same: **any input that works today produces identical
output**. Each lands with a test that fails first.

| # | Item | Where | Why it is safe |
|---|---|---|---|
| 1.1 | **`UIImageStore::Update` always throws** | `UIImageStore.cpp:83`, `VulkanImage.cpp:257-269`. `TransitionLayout` supports only Undefined→TransferDst and TransferDst→ShaderRead; `Update` asks for ShaderRead→TransferDst. Add that branch. Nothing constructs a `UIImageStore` in any suite; add one (needs a device, so lavapipe or manual). | It is published through the registry context as the way to update a UI image (fog of war), and every call kills the process. The path never succeeded. |
| 1.2 | **Saves truncate in place** | `SceneSerializer.cpp:459`, `PrefabSerializer.cpp:30`, `MaterialLibrary.cpp:239`, `AssetDatabase.cpp:192`. Serialise to a string, write `<path>.tmp`, rename; `PipelineCache.cpp:112-130` already does this. A truncated `.meta` re-mints its GUID and orphans every scene reference to it. | Identical bytes on success. Check that the temp name is one `AssetDatabase`'s scan and `AssetWatcher` ignore. |
| 1.3 | **Non-finite floats make a scene unreadable** | `ComponentCodec.cpp`, `SceneSerializer.cpp`. One `num_put` facet imbued on the stream, writing non-finite values with `jsonSafe`'s existing policy, so no call-site churn at ~100 sites. Finite values delegate to the standard facet and are formatted exactly as now. | Affects only values that could never be read back. |
| 1.4 | **GPU use-after-free on environment change** | `EnvironmentProbe.cpp:176`: `Load` replaces the cube images immediately; `DrawFrame` has waited only this slot's fence and the `waitIdle` is after the destruction. Move the old image into `DeferDestroy`, as `MeshRegistry.cpp:327` does. Reproduce first with the validation layers. | Only the order of a free. The hazard is read from the code; whether it trips validation is not yet shown. |
| 1.5 | **glTF reads past the buffer on a crafted file** | `GltfLoader.cpp` 331-414, 512, 845-852, 1004, and `accessorData` 207-229. Validate accessor indices, `offset + (count-1)*stride + elementSize` against the buffer, component and type for position, normal, joints and weights; cap counts before `resize`; drop whole triangles when an index narrows; cap `visitNode` recursion. | Valid files take the same path. Invalid ones log and skip the primitive. |
| 1.6 | **Allocations sized by file data** | Replay `ticks N` (`InputRecording.cpp:756`, a 40-byte file can declare 4e9 ticks); Radiance HDR width×height (`EnvironmentMap.cpp:310`); heightfield width×depth (`TerrainGenerator.cpp:117`); `maxParticles` (`ParticleSystem.cpp:40`); BitmapFont page id (`BitmapFont.cpp:135`). Cap and fail the load. | Caps go well above anything shipped. **Check each cap against the three games' scenes before choosing it** — that is the one thing here this session could not do. |
| 1.7 | **Sprite animation hang and divide-by-zero** | `SpriteAnimationSystem.cpp:17-18, 55-59`; `TilemapSystem.cpp:89`. `columns * rows` in `uint64_t`, treat 0 as degenerate; guard `secondsPerFrame` that is zero or denormal. | `frame` and `elapsed` are hashed, so the subtraction loop stays as it is for every sane value, and a test pins that. |
| 1.8 | **NaN animation time reads out of bounds** | `AnimationSystem.cpp:24-33`: `if (!(time > times.front())) return 0;` is identical for every non-NaN. | |
| 1.9 | **NaN breaks three `std::sort` comparators** | `PhysicsSystem.cpp:407-410`, `RenderSystem.cpp:159-176, 268-279`. Order NaN last. | Finite data sorts as before; the broadphase test and `kCanonical` pin it. |
| 1.10 | **An oversize texture is fatal and leaks** | `TextureRegistry.cpp:265-311`. Treat it as a load failure, use the fallback texture, free the pixels with a deleter. | |
| 1.11 | **A 32-bit integer WAV plays as noise** | `AudioClip.cpp:418-429`, `AudioMixer.cpp:57`. Convert int32 on load; reject float with bits ≠ 32; check `tellg`; make the mixer clamp NaN-safe. | A game shipping such a file hears noise today. |
| 1.12 | **Hot reload retries a doomed load every second frame** | `HotReloadEngine.cpp:98-130`. Retry only copy and lock failures; for a missing symbol or version mismatch wait for the next write time. | Editor only. |
| 1.13 | **Small ones** | `BloomPass` shader load has no file or size checks and leaks on a throw; a minimised window cannot be closed (`VulkanRenderer.cpp:185`); `GamePackager.cpp:63` uses the throwing `is_regular_file`; `VulkanImage` ctor leaks if `createImageView` throws; camera pitch ±90 and zero-area triangles produce NaN; out-of-range `static_cast<int>(double)` from JSON is undefined behaviour. | Each only changes a case that already failed. |
| 1.14 | **Suites for what has none** (M) | `HotReloadEngine`, `ParticleSystem`, `PipelineCache`, `Log`, `ThumbnailCache`, `ScreenCapture`'s PNG write, `SafeArea`. Additive. | |

## Phase 2 — Faster, bit-identical (size S–M)

Gates: G1, G3, G4. These are only worth doing where the output can be proven
identical, and several of them are a trap if done the obvious way.

| # | Item | Note |
|---|---|---|
| 2.1 | `ClusterGrid::Assign` early-out when there are no local lights | It does 3,456 clusters × 2 `std::pow` every frame, and the default scene has one sun. The output is the zeroed clusters either way. |
| 2.2 | `syncProbes` skips the per-renderable work when no probe exists | O(renderables × probes) every frame, even with zero probes. |
| 2.3 | `UISystem` builds `childrenOf` lazily; layer check before `ClipScope` | Built every frame, before the early return that makes it unused. |
| 2.4 | `PhysicsSystem` per-step waste | `reserve` on `bodies` and `proxies`; skip the `byEntity` map with no joints; carry the `RigidBodyComponent` pointer instead of two `try_get` per constraint per iteration. **Trap:** the identity-parent `glm::inverse(glm::mat3(parentWorld))` has `-0.0` off-diagonals and `StateHash` is bitwise, so use a precomputed constant *of that exact result*, never `mat3(1)`. |
| 2.5 | `ConvexHull::deduplicate` is quadratic | A hash grid with cell ≥ 2e-6 and a 27-neighbour check keeps the same set in the same first-seen order, because the 1e-6 tolerance is unchanged. A first hull collider from a 50k-triangle mesh stalls `PhysicsSystem::Update` for seconds. Needs a randomised equivalence test. |
| 2.6 | Per-frame scratch in `RenderSystem` and `AnimationSystem` | Fresh vectors every frame; `ComposePose` allocates per animated entity although its sibling reuses its buffers; `FindClip` is a linear string scan up to four times per animator per frame. |
| 2.7 | `ContactTracker`: swap instead of copy | `m_previous = m_current` copies a hash set every tick and `m_current` is read only in `Update` and `Clear`. The *sorting* of Exit events is a different matter; see 3.2. |
| 2.8 | `AssetWatcher` polls a slice per frame | The code admits 730 files cost 4.6–6.6 ms. Changes only reload latency, in the editor. |

## Phase 3 — Needs the owner's decision

Not started on anyone's say-so but the owner's. Each is a real change to
something a game can observe or to something every game inherits.

| # | Item | What it changes, and why it is held |
|---|---|---|
| 3.1 | **Float text round-trips** | Floats are written with iostream's default six digits, so Play → Stop → Play does not restart from the authored bits, undo rounds every transform, and a smaller edit is not seen as a change (the 28 August determinism audit admits it). The fix, `%.6g` when it round-trips and `%.9g` otherwise, leaves nice values as text but **changes the saved bytes of any scene with a longer float**, and the state hash an editor Play starts from. Byte-pinned fixtures and recorded replays would notice. |
| 3.2 | **Sort `ContactTracker` Exit events** | Their order is `unordered_set` iteration order, which differs between libstdc++, libc++ and MSVC, and scripts read events by index, so it is a cross-platform determinism hole. But sorting changes the order a script sees *today*, on the platform each game was recorded on. |
| 3.3 | **Don't force `CMAKE_BUILD_TYPE=Debug` into the parent's cache** | `CMakeLists.txt:47-50` does it even as a subproject. Correct to fix, but it changes the default build type of any game that relied on it. Each game has to be told. |
| 3.4 | **Subproject hygiene** | `ENTT_INSTALL OFF` (a parent's `cmake --install` gets a stray EnTT today); `if(NOT TARGET ...)` around the vendored `add_subdirectory` calls; `GLFW` options not `CACHE FORCE`; in-source-build guard; `/machine:x64` hard-coded in `GenerateVulkanImportLib.cmake`. Touches what a game inherits. Real `install()` / `export()` is not recommended: the contract is `add_subdirectory`. |
| 3.5 | **`supersonic_add_test` gets a `TIMEOUT` and `LABELS`** | Games inherit this function. A default that is too tight kills a long replay suite, so it would have to be generous and overridable. |
| 3.6 | **Precompiled headers** | A full Release build is 1,285 CPU-seconds (about 5.5 minutes on four cores), and `Components.hpp` alone takes about 4 s to parse per translation unit. `target_precompile_headers` on the core should cut a large share (an estimate, not measured). It changes compile lines, which the 19 September record set the bar against; prove the test binaries behave identically. Not unity builds: `kEpsilon`, `kPi` and `kFaceBias` are file-local in several files. |
| 3.7 | **Batch the GPU upload drains** | Each transition, copy and mip generation submits and calls `queue.waitIdle`, so a texture drains the queue three times and a mesh twice, mid-frame. One command buffer and a fence per upload is the fix, and it is synchronisation code that is hard to prove here. Needs G4. |
| 3.8 | **Replay files with unsorted checkpoints** | `CheckpointAt` breaks early assuming ascending order, so such a file silently skips verification. Verifying it would turn a silent pass into a possible failure. |
| 3.9 | **Json `\u` surrogate pairs; `ScriptRegistry` built-in override restore; skip the second `UpdateWorldTransforms` in packaged games** | Each is correct and each changes behaviour a game could depend on. |
| 3.10 | **Android and iOS** | The build scripts say "no GLFW backend", which is stale; they would instead fail because the editor builds by default and nothing defines `SupersonicMain`. Either delete the scripts and point at Penumbra's, or default the editor off on those platforms. A dispatch-only cross-compile job would let engine changes see breaks, which only Penumbra's CI does today. |
| 3.11 | **`assets/audio/ambient.wav` provenance** | `THIRD_PARTY_LICENSES.md` still says to confirm it. Only the owner can. |

## Left alone on purpose

- **Behaviour the code admits is limited** (waking travels one link per step;
  capsule and hull ray queries use the enclosing box; one joint per entity;
  particles composite wrongly behind transparent panes). Fixing any of them
  moves the simulation or the picture.
- **What the 27 August roadmap already rejected**: a scripting language, a 2D
  engine, pathfinding, networking, GPU particles.
- **Turning CI on automatically.** The owner chose dispatch-only because the
  repository is private and runs are charged. Item 0.9 is the answer to that, not
  a reversal.
- **A whole-tree reformat to `.editorconfig`.** 824 lines are over 100 columns;
  reformatting them would bury `git blame` for no behavioural gain.

## The order

1. **Phase 0**, in the order 0.2, 0.1, 0.4, 0.3, then the rest. Smallest risk, and
   0.2 stops a false red from hiding a real one.
2. **Phase 1**, highest value first: 1.1, 1.2, 1.4, 1.5, 1.6, 1.3, then the rest.
3. **Phase 2**, only with G3 and G4 available.
4. **Phase 3**, one decision at a time.

A rough size, as an estimate and not a measurement: Phase 0 is two to three days,
Phase 1 about a week, Phase 2 about a week. Phase 3 depends on the answers.

## Outcome

Phases 0 and 1 are done; Phases 2 and 3 were not authorised and are untouched.
Every change is its own commit with the reasoning in the message.

### What the numbers are now

| | Before | After |
|---|---|---|
| Suites | 58 (57 pass, `test_jobs` flaky) | 62, all pass, none flaky |
| Suite run time | about 7 s | about 0.6 s |
| GCC 13 warnings | 17–23 | 0, and held by `-DSUPERSONIC_WERROR=ON` |
| Clang 18 warnings | not measured before; the first full build found one dead variable, and VMA's nullability noise was already known | 0 under `SUPERSONIC_WERROR=ON` |
| ASan + UBSan + LSan | clean on 58, with UBSan not halting and LSan off in CI | clean on all 62, and CI now fails on either |

Not run anywhere: MSVC, macOS, and anything that needs a GPU or a window (see
"Not checked").

### Phase 0

All ten items are in. 0.9 is the one that differs: `tools/check.sh` has the steps
`docs shaders build options asan tsan tidy all`, and `ci.yml` calls the same code,
but the **bash port of `verify-replay` was not written**; G3 still means running
`tools/verify-replay.ps1` on Windows. `check.sh docs` now also fails when a suite
has no README row or a `tests/test_*.cpp` is not registered, which is the drift 0.1
fixed by hand. The `tsan` step runs seven suites, not the six the table counts:
`test_log` starts threads and is in the list.

### Phase 1

All of 1.1–1.14 are in, each with a test written to fail first wherever the code
can be run here. Where it differs from the table:

- **1.5** The plan said to cap `visitNode` recursion. A cap of 1024 still
  overflowed the stack under ASan, so the walk is iterative with an explicit stack
  and has no depth limit at all.
- **1.6** The caps are `kMaxRecordedTicks` 2^22, `kMaxHeightfieldSide` 32768 with
  at most 2^26 cells, `kMaxParticlesPerEmitter` 2^20 (clamped, not refused), 256
  bitmap-font pages, and a Radiance image of at most 32768 a side and 2^28
  texels. They were chosen well above anything in this repository. **They have not been checked
  against the three games' scenes**, which is what the plan said must be done
  first. Do that at the next submodule bump.
- **1.13** "Camera pitch ±90 produces NaN" did not reproduce: a probe showed only a
  non-finite angle does. That case is fixed and tested, and the commit says so.
  The zero-area triangle and the out-of-range `static_cast<int>(double)` cases are
  fixed. The latter goes through `asU32` / `asI32` / `asIndex`, which keep
  x86's result for every in-range value and define the rest.
- **1.2** Saves go through one `AtomicFile` (`<path>.tmp`, then rename). The
  asset database's scans match on the `.meta` suffix and a list of known
  extensions, and `.tmp` is neither. If the temporary cannot be created the save
  falls back to the old in-place write rather than newly failing.
- **1.3** Done as planned, with a facet imbued by an RAII guard so the stream's
  locale is restored on every exit.
- **1.14** Added `test_particles`, `test_hotreload`, `test_log` and
  `test_platform` (the safe area), plus the PNG round trip in `test_imagepixels`.
  **`PipelineCache` and `ThumbnailCache` still have no suite**: both need a
  device.

The last ASan + UBSan + LSan pass over all 62 suites found one failure, and it was
a test's, not the engine's: `test_hotreload` took "some shared library" from
`dladdr(&printf)`, which under ASan is libasan itself, and loading a copy of the
sanitizer runtime aborts the process. It failed 20 times in 20 there and passed in
the ordinary build. It now uses `std::cos` (libm), and the mutation check still
fails it at the same line.

Extras that came out of the work and are in the tree: `SUPERSONIC_WERROR`, the
`StbImageImplementation.cpp` translation unit, `{}` initialisers on the
`MeshComponent` / `ScriptComponent` string and map members (which is what fixed the
emplace warnings without touching the call sites), and a differential test of
`SpriteAnimationSystem` against a verbatim copy of the old code.

### Not checked

- **G3 and G4.** `test_determinism` passes and its `kCanonical` hash is unchanged,
  but `verify-replay.ps1` and the `--frames 120 --fixed-step --screenshot`
  comparison need the owner's machine. Run both before bumping any game.
- **Device paths.** 1.1 (`UIImageStore::Update`), 1.4 (the environment probe's
  deferred destroy), 1.10 (the oversize-texture fallback) and the device parts of
  1.13 were fixed from reading the code and the pure helpers they call
  (`BarrierFor`, `FitsTheDevice`) were tested. Nothing here ran them against a
  driver or the validation layers.
- **Windows and macOS.** The MSVC branches of the CMake changes and
  `StbImageImplementation.cpp`'s warning pragma were not compiled.
- **Linux CI.** Nothing was dispatched; the repository's workflow is dispatch-only.
- **Optional parts of 0.7.** The 71 MB of unused vendored trees was left in place.

### What is left for the owner

- **Phase 2** (bit-identical speedups) needs G3 and G4 on a machine with a GPU.
- **Phase 3** is the list of decisions above and still waits on answers.
- Merge the branch so the README notice (free to use, as is, no responsibility, built
  with Claude Code) shows on the repository's front page. The repository's
  "About" line is a GitHub setting and was not changed.
