# Local verification handoff — 11 November 2026

**Read this first if you are the agent asked to verify this branch on the owner's PC.**
It says what was changed, what has and has not been checked, and what to run — in
order — with the result each step should give. The branch is
`claude/beautiful-cray-qs85oy`; it starts at `4261485` (the merge-base with `main`) and
is about twenty commits long. Nothing on it has been merged, no pull request is open, and no game
repository has been touched.

Like the other records in this directory it is a dated snapshot: do not edit the
sections above [Outcome](#outcome) to match what you find. Append to Outcome.

> **Update, later the same day.** The branch has since been merged into `main` (merge
> commit `ea5ed07`), so wherever this document says "the branch", use `main`; the checklist
> is otherwise unchanged. The merge brought in two commits that landed on `main` in the
> meantime — a hinge-limit fix in `Joints.cpp` and an Android audio-stream keeper with two
> new suites — and the merged tree went through the same gates again (Release `-Werror`,
> ASan/UBSan/LSan, replay round trip, frame repeatability, the device suite: all green;
> the pictures, the physics checksums and the replay's pinned checkpoint hash identical to
> before the merge). So expect **67** ctest entries (66 suites + `test_reporttext`), not 65.
> The audio keeper's own behaviour on a phone or emulator was not exercised here — only its
> pure-logic suites ran.
>
> Do your work on a new branch, `local-verification`, and push the Outcome to that — not to
> `main`. The owner merges it.

> **Corrections after the first local run (same day).** The first run stopped at L1 and was
> right to. What it found, and what changes for the next run:
>
> - **L1's `C4702` is fixed.** Three test loops whose body always exits (`test_gltf.cpp`,
>   twice in `test_serialize.cpp`) made MSVC `/W4 /WX` fail; they predate the branch
>   (September and October commits) and no code in `src/` is involved. They are rewritten
>   on branch `claude/beautiful-cray-qs85oy` without a suppression. **Merge that branch
>   into `local-verification` first**, then rerun L1 with `SUPERSONIC_WERROR=ON`, **Debug
>   and Release** (C4702 is optimiser-adjacent, so Release may report more). A further
>   `C4702` on a loop whose body always exits, in test code: fix it the same way and say so.
>   Anything else — a different warning, or anything in `src/` — record it and stop. The
>   earlier line "fix it in that code" applies to code this branch wrote.
> - **Commands that did not run on that PC.** `-G "Visual Studio 17 2022"` is an example, not
>   a requirement: use the generator the installed Visual Studio offers (`cmake -B build`
>   picks the newest), with Visual Studio's bundled CMake if the one on `PATH` lacks it.
>   `pwsh` may be absent: run `powershell.exe -NoProfile -File tools/verify-replay.ps1
>   -Config Release`, and if the script needs PowerShell 7, say what failed. `bash` on `PATH`
>   may be the WSL launcher: for L11 use Git Bash, `"C:\Program Files\Git\bin\bash.exe"`.
>   Wherever this document says 65 ctest entries, it is **67**.
> - **L5 is not about replay files.** None of the four games tracks a `.replay`, and none
>   compares `StateHash` outside a document. Each guards with its own suites and goldens
>   (Penumbra: 17 suites, one byte-comparing its scene writer to the shipped scenes; Magic
>   Portals: ~94 entries with `tests/golden/*.json` and 128 win routes; Wolf Brigade: its
>   Godot-derived constants and an in-process replay; HUSK: 68 golden `.hashes` files, and
>   `/fp:precise` on `HuskSim`). So L5 is: **for each game, run its own suites at its current
>   engine pin first** (that is its baseline, and it separates what the engine did from what
>   the PC does), **then with this engine**, and report any difference in a golden, a hash,
>   a byte comparison or a route. Never update a golden. Penumbra and Magic Portals ship
>   releases, so they go first, then Wolf Brigade and HUSK. They build with Ninja and `cl`
>   (pass `-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl`; a GCC earlier on `PATH` wins
>   otherwise), `build-lv` is not git-ignored in three of them, and screenshot paths must be
>   absolute or they land inside the engine submodule. Work on scratch branches; push nothing.
> - **Smart App Control** may block freshly built executables at random, one notification
>   each. A `Not Run` test (exit 126) is the machine, not the code: rerun that executable by
>   hand up to three times and record each as ran-and-passed, ran-and-failed (a finding), or
>   never ran. Do not turn Smart App Control off. L10 runs the suites on a hosted Windows
>   runner with none of this, which is why it can be run first, in parallel with the rest.
> - **The repository is public** (GitHub says so; `AGENTS.md` and `ci.yml` still reason from
>   it being private). Hosted Actions minutes are free for a public repository, so the
>   "CI spend" concern does not apply to L10 while it stays public. Whether it should be
>   public is the owner's call, not yours.

## Rules

1. **A failing check is a finding, not an obstacle.** Do not weaken a test, widen a
   tolerance, delete an assertion, mark something Skipped or re-record a baseline to get
   green. Find the cause, say what it is, and stop at anything you cannot explain.
2. **Do not change what a shipped game can observe.** The two games pin this engine and
   compare `StateHash`, replay files, screenshots and scene/prefab/`.meta`/manifest bytes.
   Scene keys are add-only. Every behaviour change on this branch that could move a game
   is an opt-in, default off, written to a file only when on and hashed only when on
   (see [Owner decisions](#owner-decisions)). If a check shows a game moved, that is the
   most important thing you will find.
3. **Commits are the owner's:** author and committer `IvanCvetanovic
   <icvetanovic99@gmail.com>`, no `Co-Authored-By` or session trailers. Do not open a pull
   request, bump a game's engine pin, or push to any other branch unless the owner says so.
4. Some steps need a person at the screen (the editor). Say which you could not do.

## What changed

About 95 files, +5.4k/−0.3k lines. By commit; the subject lines are the specifics.

| Area | Commits | In one line |
|---|---|---|
| Determinism tooling | `ab42512` | `tools/verify-replay.sh`: the record / replay / corrupt-a-checkpoint round trip on Linux and macOS, headless |
| Simulation fixes | `0b557cf` | a NaN body infects nothing, a joint holds a tonne, a NaN audio parameter is a number — all on inputs that already failed |
| Speed, same bits | `fb4fc42` `9eacca6` `32a0778` `81508c6` | physics, renderer, UI, animation, scripts: per-step and per-frame waste removed with identical output (physics checksums and pictures compared to the original engine) |
| Assets | `baab55e` | a material cannot write a number its reader refuses; a prefab/material from a newer build is refused; a BOM is not a parse error; directory walks cannot throw |
| Editor | `ba765ce` `ea42cb4` `0604c99` `727a9cb` | a save during Play cannot write the simulation over the file; the inspector no longer writes an untouched rotation back; the content browser sorts and filters; and the other everyday gaps `ba765ce` lists |
| Build | `a5f3191` | precompiled headers for the engine's own translation units (`SUPERSONIC_PRECOMPILE_HEADERS`, on) |
| Determinism opt-ins | `d960cbe` `0604c99` | exact editor snapshots (`SerializeSnapshot`), `StableContactOrder`, `SortedContactExits`, `RegisterExtendedCoverage` — all off unless asked for |
| Hot reload | `aac5643` `0604c99` | a copy never written into a mapped file; the new plugin really loads (unique shadow names) |
| Component facility | `2e12b5c` `20f42e6` `57b33b6` | `RegisterDescribedComponent<C>(…)` gives a game component its codec and its inspector section from one field list; the codec holds a game's writer to the engine's NaN / locale / precision guarantees |
| Device suite | `2e12b5c` `57b33b6` | `test_device`: the renderer's device code on a real Vulkan device (uploads read back, mip chain, replace, pipeline cache, deferred destroy) |
| Tooling | `2e12b5c` `a0ac94c` `57b33b6` | pre-push hook, `check.sh frames` / `device`, `verify-frames.sh`, benchmarks (`SUPERSONIC_BUILD_BENCHMARKS`), a docs path lint, an arm64 CI leg |

Nothing under `assets/` changed — no shader, blob, scene, prefab or manifest.

## What was checked here, and how far that goes

The environment: Linux x86_64, GCC 13.3, Ubuntu 24.04, 4 cores, **no GPU** — Mesa's
lavapipe (llvmpipe, LLVM 20.1.2, Vulkan 1.4.318) under Xvfb, and the Khronos validation
layers. Software only.

| Check | Result |
|---|---|
| Release, `-Werror` (`tools/check.sh build`) | zero warnings; **65 of 65** ctest entries pass (64 suites + `test_reporttext`) |
| ASan + UBSan + LSan (`tools/check.sh asan`) | **65 of 65**, `test_device` executing under the sanitizers (34 checks) |
| Replay round trip (`tools/verify-replay.sh`) | 180 of 180 ticks reproduce; a corrupted checkpoint is caught at tick 180 |
| Frame repeatability (`tools/verify-frames.sh`) | same picture and counts twice; a later frame differs |
| Pictures vs the **original** engine | `AllPasses` (40 frames, 1280×720) and `MainScene` (120 frames) are byte-identical: md5 `11a10ad1b6c68960ac75198c72a1fd71` and `5d846b4efa116f1cdfaec32c87b829e2`. These are lavapipe's pixels; a real GPU's differ |
| Physics bench vs the original engine | checksums identical (`983ab687…`, `ea11ec86…`, `a52c5fcc…`); world queries 7.7 → 2.7 ms/step on this machine |
| Validation + synchronization validation | 0 messages for both scenes, the editor, and `test_device`. The layer *was* loaded: the loader names it 8 times in the validation build and 0 in the plain one |
| Docs gate (`tools/check.sh docs`) | counts, README table, registrations and the paths the docs name agree; 0 failures in 40 consecutive runs |

Each new test was shown to fail without the fix it guards (mutation checks), not only to
pass. Two independent read-only reviews of the new code found 19 confirmed defects and some weak
tests; all were fixed, and are in `57b33b6`.

### Not checked — and why that is your list

- **MSVC, at all.** Nothing on this branch has been compiled by MSVC. Windows is the
  primary target. Likeliest to bite: `/W4 /WX` on `core/ComponentFields.hpp` (fold
  expressions, a lambda parameter unused for a zero-field component), `std::_Exit` and
  `_dupenv_s` in `tests/test_device.cpp`, `<locale>` use in the new tests.
- **A real GPU.** Every picture, the device suite, and the validation run are software.
- **Non-x86.** The pinned `StateHash` has never run on arm64 (`kCanonical` in
  `test_determinism`).
- **Android, macOS, iOS, and any Clang build.** Nothing from this branch was built with
  Clang. GCC 11 and Clang 13, the stated floors, have never been built.
- **The ImGui widgets.** What the inspector offers is tested through a `FieldVisitor`;
  the widgets themselves, the popups, the content browser, and the editor generally have
  not been looked at by a person.
- **Hot reload on Windows** (`LoadLibraryA`, mapped files, unique shadow names).
- **The shipped games.** Not built against this branch.
- **The shader byte-compare.** No `glslc` here — but no shader or blob changed.

## The checklist

Do these in order; each assumes the one before passed. `$ENGINE` is the repository root;
`$BASE` is a second checkout at `4261485`:

```powershell
git worktree add ..\engine-base 4261485
```

### L1 — Windows build and every suite (MSVC)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DSUPERSONIC_WERROR=ON
cmake --build build --config Debug --parallel
cmake --build build --config Release --parallel
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

**Expect:** zero warnings; 65 entries pass. `test_device` must be **Passed**, not Skipped,
with a GPU and the Vulkan SDK installed; `test_reporttext` is Skipped without a JDK.
**If an MSVC warning appears in this branch's code, fix it in that code** (it is a real
finding) — do not suppress it.

### L2 — Determinism on MSVC

```powershell
pwsh tools/verify-replay.ps1 -Config Release
```

**Expect:** the replay reproduces and the corrupted checkpoint is caught at the right
tick. `test_determinism` and `test_replay` are the unit-level half and are in L1. If the
canonical hash in `test_determinism` differs on MSVC, **stop**: that is the central claim
of the engine.

### L3 — The device suite and validation, on the real GPU

```powershell
$env:SUPERSONIC_REQUIRE_DEVICE = "1"      # a Skipped here would hide a broken machine
ctest --test-dir build -C Release -R test_device -V
$env:VK_LAYER_VALIDATE_SYNC = "1"
.\build\Debug\SupersonicEngine.exe --hidden --fixed-step --frames 120 --scene assets/scenes/AllPasses.scene
.\build\Debug\SupersonicEngine.exe --hidden --fixed-step --frames 120 --scene assets/scenes/MainScene.scene
```

**Expect:** `test_device` passes (34 checks; it also asserts zero validation errors in a
validation build); both runs exit 0. A validation error exits non-zero. Confirm the layer
is really loaded before reading silence as clean: `$env:VK_LOADER_DEBUG = "layer"` and look
for `VK_LAYER_KHRONOS_validation`.
**Known soft spots to watch:** the pipeline-cache check assumes a driver returns at least
the 32-byte header for an empty cache (the spec says it must); a driver that returns zero
bytes would fail "Save writes the cache to disk" — report it, don't relax it.

### L4 — Pictures: this branch against the original, on the same GPU

Compare the two builds to **each other**, on this GPU; do not compare to the md5s above.

```powershell
# From the root of each checkout, built Release; $tag is "base" or "branch".
$allpasses = @('--hidden','--fixed-step','--frames','40','--window','1280x720',
               '--scene','assets/scenes/AllPasses.scene','--screenshot',"${tag}_allpasses.png")
$main      = @('--hidden','--fixed-step','--frames','120','--window','1280x720',
               '--scene','assets/scenes/MainScene.scene','--screenshot',"${tag}_main.png")
& .\build\Release\SupersonicEngine.exe @allpasses
& .\build\Release\SupersonicEngine.exe @main
Get-FileHash "${tag}_*.png"
```

**Expect:** base and branch byte-identical for each scene. First run the *base twice*: if it
differs from itself, the GPU or driver is not repeatable and this step proves nothing —
say so. If base and branch differ, find which pass (the `[Counts]` block and the tail of
`9eacca6` say what the renderer commit changed) and report; do not re-baseline.

### L5 — Each shipped game, against this branch

In each game's repository, on a scratch branch: point the engine submodule at
the tip of `claude/beautiful-cray-qs85oy`, build, and run the game's own suites, replay verification
and screenshot baselines. **Expect: no differences of any kind.** A diff in a `StateHash`,
a replay file, a screenshot or a saved scene is a finding about this branch — report which
game and which artefact, and do not update the baseline. Do not merge the scratch branch.

### L6 — Hot reload on Windows

Run the editor, then `cmake --build build --config Debug --target GameScripts`. **Expect:**
the statistics panel's reload count rises and the new code runs; no leftover
`GameScripts*` copies accumulate beside the DLL after a few rebuilds; a plugin that cannot
load is tried once and left alone (`test_hotreload` covers the logic; this covers the OS).

### L7 — The editor, by hand

Open `assets/scenes/MainScene.scene` in the editor and check, saying what you saw:

- **Inspector rotation:** click a transform's Rotation without changing it; the entity's
  rotation (and the undo stack) must not change. Then change it; it must.
- **Save during Play:** press Play, press Save. The file on disk must be the edit-mode
  scene, not the simulation; Stop must restore every transform exactly.
- **Remove Component / Add Component** on a few components, and Undo/Redo after each.
- **Content browser:** entries are sorted; the filter box narrows them; an unreadable folder
  does not break it.
- **Typed numbers:** type `1e40` into a float field; it must not become an infinity.

### L8 — A described component, end to end

No shipped component uses the facility yet, so make a throwaway: in a scratch copy, register
one before the first scene loads —

```cpp
struct Probe { int hits{3}; float gain{0.5f}; std::string label{"probe"}; glm::vec3 where{0, 1, 0}; };
using ComponentFields::Field;
(void)RegisterDescribedComponent<Probe>("Probe",
    Field("hits", &Probe::hits), Field("gain", &Probe::gain),
    Field("label", &Probe::label), Field("where", &Probe::where));
```

— then in the editor: Add Component → Probe; edit each field; save, reopen (values survive);
Play, Stop (restored); Undo; Remove. Type a 600-character label (it must not be cut at 511).
Delete the scratch afterwards.

### L9 — Benchmarks

```powershell
cmake -B build-bench -DSUPERSONIC_BUILD_BENCHMARKS=ON -DSUPERSONIC_BUILD_TESTS=OFF -DSUPERSONIC_BUILD_EDITOR=OFF
cmake --build build-bench --config Release --target physics_bench dedupe_bench
.\build-bench\tools\bench\Release\physics_bench.exe
```

Build `physics_bench` against `$BASE` as well: copy `tools/bench` into it and add the block
the branch adds to the top-level `CMakeLists.txt` (`option(SUPERSONIC_BUILD_BENCHMARKS …
OFF)` and `if (SUPERSONIC_BUILD_BENCHMARKS) add_subdirectory(tools/bench) endif()`). The
checksums depend on the machine and compiler, so compare base to branch on the same one,
never to the Linux numbers above. **Expect:** the three checksums are **identical** between base and branch;
the timings are lower or equal. Different checksums mean a change was not bit-identical —
report which scene.

### L10 — CI

Actions → CI → Run workflow:

- `quick`: shader byte-compare and the docs check. Needs the pinned SDK; this is the
  recompile that could not run here.
- `all`: builds, the lavapipe validation job (it now *finds* the ICD rather than naming
  one, and requires the device suite), sanitizers, options-off.
- `arm64`: one job. The first time the pinned determinism hash is run off x86. If the
  runner label is not available to the account, say so; do not change the label.

**Expect:** `all` and `quick` run the same jobs they always did.

### L11 — The hook

```powershell
bash tools/install-hooks.sh        # refuses if it would displace a hook already in use
git push --dry-run origin HEAD     # a branch deletion or a tag must not run the checks
bash tools/install-hooks.sh --remove
```

The hook file must have LF endings in a Windows checkout (`git check-attr eol
tools/git-hooks/pre-push` says `lf`); with CRLF it would block every push.

## Owner decisions

Not yours to make; surface them.

- **The opt-in switches.** `StableContactOrder` and `SortedContactExits` (the Physics block
  of a scene) and `StateHash::RegisterExtendedCoverage()` remove platform-dependent
  behaviour (hull contact order differs between standard libraries) but change what a
  tick computes or what the hash covers. Turning one on for a shipped game moves its
  baselines. Whether any game, or only new ones, should use them is the owner's.
- **Bumping a game's engine pin,** and merging this branch.
- **`platform/` scripts:** repair or remove (item 3.10 of the hardening plan).
- **The compiler floor.** GCC 11 and Clang 13 are stated and untested: test them or raise
  the stated floor.
- **CI spend.** The arm64 leg, and the extra steps on the validation job, cost minutes the
  repository is careful with. Automatic runs are off by decision.

## Things that look like gaps and are deliberate

- `SerializeSnapshot` (nine digits) is for the editor's Play/Stop and Undo only. A game's
  own saves and snapshots use `SerializeToString` / `Serialize`, whose bytes are unchanged
  and pinned by test.
- `RegisterDescribedComponent` supports `bool`, `int`, `float`, `std::string`, `vec2/3/4`,
  nothing else; a tag is a compile error; there is no unregister (a hot-reloaded module that
  registered from inside itself would leave a stale entry — today no module does, the script
  API is a C ABI). The engine's own components keep their hand-written code.
- The inspector's `Vec3` control (`Theme::DrawVec3Control`) still does not clamp typed
  input; the facility's float, vec2 and vec4 widgets do. It predates this branch and
  position fields are used everywhere, so it is left for a decision.
- A NUL inside a described string is cut at the NUL by the first edit in the inspector
  (ImGui's text box is a C string). The writer and reader round-trip it.
- `tools/verify-frames.sh` proves repeatability on one machine; it is deliberately not a
  golden-picture check, which goes stale the day a driver changes.
- Performance numbers are one machine, one compiler.

## How to report

Append an **Outcome** section below, then commit it as the owner. Per step: passed,
failed, or not run (and why), with the exact numbers — counts, checksums, hashes, the line
that failed — and the first lines of any failure output. Put surprises first. Do not
summarise a failure as "environment" without showing why.

## Outcome

*(To be written by the local verification run.)*
