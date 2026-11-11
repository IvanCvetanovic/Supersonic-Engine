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

**Run of 11 November 2026 on the owner's PC, against `main` at `0bbefd7`, on branch
`local-verification`. It stopped at L1, by the owner's rule that a failure of L1 or L2 ends the
run: L2 to L11 were not run.** No engine file, test, tolerance or baseline was changed.

The machine: Windows 11 Pro 10.0.26300; AMD Ryzen 7 250 (16 logical cores), 13.7 GB RAM; GPU an
**integrated** AMD Radeon 780M, driver 32.0.31033.3; Visual Studio Build Tools **2026** 18.10.12217.157,
MSVC toolset 14.51.36231 (`cl` 19.51.36260.0), Windows SDK 10.0.26100.0; Vulkan SDK 1.4.357.0 (the
version CI pins); JDK 17.0.20; git 2.55.0, `core.autocrlf=true`.

### Surprises first

**1. L1 fails on MSVC under `/WX`, and the cause is not this branch.** Three `C4702 unreachable code`
warnings, promoted to errors by `SUPERSONIC_WERROR=ON`, stop `test_gltf` and `test_serialize` from
compiling in Debug:

```
tests\test_gltf.cpp(632,1): error C2220: the following warning is treated as an error [...\build\tests\test_gltf.vcxproj]
tests\test_gltf.cpp(632,1): warning C4702: unreachable code
tests\test_serialize.cpp(1376,1): error C2220: the following warning is treated as an error [...\build\tests\test_serialize.vcxproj]
tests\test_serialize.cpp(1376,1): warning C4702: unreachable code
tests\test_serialize.cpp(1388,1): error C2220: ...   /   warning C4702: unreachable code
```

| Site on `main` | Same site at the merge-base `4261485` | Written by | Shape |
|---|---|---|---|
| `tests/test_gltf.cpp:632` | `:632` | `7dd9814f`, 2026-08-27 | `for (const auto& submesh : scene.submeshes) { CHECK_MSG(...); break; }` |
| `tests/test_serialize.cpp:1376` | `:1356` | `b96d4dc1`, 2026-10-01 | `for (const entt::entity e : registry.view<BoxColliderComponent>()) { return ...; }` |
| `tests/test_serialize.cpp:1388` | `:1368` | `b96d4dc1` | the same, over `UITextFieldComponent` |

- **Pre-existing, shown rather than inferred.** Both commits are ancestors of `4261485`. I built
  `test_gltf` and `test_serialize` in Debug in a worktree at `4261485` with the same toolset and
  options: `test_gltf.cpp(632,1)`, `test_serialize.cpp(1356,1)` and `(1368,1)` fail identically.
  (The two targets given together stop at the first failure, so `test_serialize` was built on its own.)
  No diagnostic is in code the branch wrote, and none is in `src/`.
- **Why nobody saw it.** Where `SUPERSONIC_WERROR=ON` appears in the CI and the scripts, it is on
  Linux or macOS: `tools/check.sh` (lines 310, 393, 436) and CI's `arm64` job (`ci.yml:448`). CI's
  Windows leg configures with `-A x64` alone. I can find no MSVC `/WX` run before this one. In it the
  engine library and the editor, which `CMakeLists.txt:461-471` holds to `/W4 /permissive- /WX`, and 64
  of the 66 suites compiled clean; the plugin built, but is not held to `/WX` at all.
- **Not tried:** the Release configuration (C4702 is an optimiser-adjacent warning, so Release may
  differ), and any other MSVC version.
- **What I did not do, on purpose.** I did not edit the three loops, add `/wd4702`, or configure with
  `SUPERSONIC_WERROR=OFF` to get a green build: each is a weaker L1 or a change to the engine's
  tests, which the owner's rules reserve. The handoff's own line, "fix it in that code", conflicts
  with the owner's stop rule; I followed the rule. Decision for the owner, below.

**2. Commands in this document that do not run on this PC as written** (reported, not worked around
silently):

- L1 `-G "Visual Studio 17 2022"`: this PC has only Build Tools **2026**, and the `cmake` on `PATH` is
  Strawberry Perl's 3.29.2, which has no such generator. What I ran: Visual Studio's bundled CMake
  4.3.1-msvc1 (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`)
  with `-G "Visual Studio 18 2026" -A x64 -DSUPERSONIC_WERROR=ON`. It configured first time (8.2 s) with
  no warning and no CMake 4 policy workaround. `ci.yml:81-86` already says not to pin the generator.
- `pwsh` is not installed (Windows PowerShell 5.1.26100.9818 only). L2 would have to run as
  `powershell.exe -NoProfile -File tools/verify-replay.ps1 -Config Release`. Not run; from reading it
  nothing in the script needs PowerShell 7.
- `bash` on `PATH` is the WSL launcher (`C:\Windows\System32\bash.exe`). L11 needs Git Bash,
  `C:\Program Files\Git\bin\bash.exe`.
- L1 and "What was checked here" still say **65** entries. After the merge it is **67**: `ctest -N` on
  the configured tree lists 67 (`#66 test_device`, `#67 test_reporttext`), and
  `grep -cE '^add_engine_test\([a-z0-9_]+\)$' tests/CMakeLists.txt` gives 66.
- The existing `build/` was a Ninja tree (Strawberry CMake, `cl` 14.51, Release, validation ON), and a
  generator cannot be switched in place, so I moved it to `build-ninja-owner/`, built mine in `build/`
  (which `tools/verify-replay.ps1` expects), and at the end deleted mine and moved the owner's back,
  reading each `CMakeCache.txt`'s generator line before every step. It is as I found it.

**3. Smart App Control is enforced on this PC** (`HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy`,
`VerifiedAndReputablePolicyState` = 1). `AGENTS.md` does not mention it; `docs/planning/2026-09-10-migration-readiness.md:161`
says it "blocks freshly built test binaries at random - up to 23", and the games' `CLAUDE.md` files say
every refusal raises a notification. L1 launches 66 fresh executables per configuration, so a full L1
here can show `Not Run` entries that are the machine and not the code. I ran **no test executable at
all**, so I have no count of how many it would block.

**4. `gh repo view` reports `IvanCvetanovic/Supersonic-Engine` as PUBLIC**, while `AGENTS.md` and
`ci.yml:17-25` reason from it being private (the reason automatic runs are off). Check which is true;
if public, the minutes argument behind "CI spend" may be moot.

**5. L5's premise does not match the four repositories.** The handoff says the games compare
`StateHash`, replay files, screenshots and scene bytes. I found, by `git ls-files` in each repository
(not by trusting a summary): **0 tracked `.replay` files in all four**, and every mention of
`StateHash` outside the engine is in a document (Penumbra 2 files, Magic Portals 3, HUSK 1, Wolf
Brigade 0; `git grep -lI StateHash`). Tracked PNGs are Penumbra 237, Wolf Brigade 83, HUSK 112 and
Magic Portals 0; the reconnaissance found none of them used as a baseline (I did not open them).
What the games do guard with, from reading (not run):

| Game (repo, engine pin) | Regression guard that an engine change could trip |
|---|---|
| Penumbra (`Penumbra-and-the-Castle-of-Shadows-Enhanced`, `4261485`, the merge-base) | 17 suites via `tests/test_pn_all.exe` (hand-worked numbers); one test compares the 0.7.12 writer to the shipped scenes byte for byte |
| Magic Portals (`Magic-Portals-Enhanced`, `a2f4873`) | 94 ctest entries in one `test_mp.exe`; bit-hash goldens in `tests/golden/*.json`; 128 win routes that fail if physics moves; local, git-ignored `out/parity` sweeps |
| Wolf Brigade (`The-Wolf-Brigade`, `4bfcf67`) | hand-checked constants from the Godot harness; `test_wb_hud` records and replays a tap in process |
| HUSK (`Test-Game`, `4bfcf67`) | `test_husk_scenarios`: 68 golden `.hashes` files compared with the Rust sim's own per-tick hashes; `/fp:precise` is required on `HuskSim` |

Which "two games" the handoff means is not stated: the README lists three ports (Wolf Brigade, HUSK,
Magic Portals) and Penumbra is a fourth. Penumbra and Magic Portals are the ones that ship releases,
so they come first; all four pin the engine as a submodule. All four repositories, and their
submodules, are clean and untouched by this run (`git status` empty; pins unchanged). Practical notes
for whoever runs L5: every game builds with **Ninja + `cl`**, not a Visual Studio generator (Strawberry's
GCC wins on `PATH` unless `-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl` is given); `build-lv` is
**not** git-ignored in Penumbra, Magic Portals or Wolf Brigade (it is, under `supersonic/`, in HUSK), so
`git status` will list it; screenshot and replay paths must be absolute or they land inside the
engine submodule and dirty it; and Smart App Control (3) applies to every new executable.

**6. One check that passed, and is worth knowing.** The Debug build recompiled all **17 of 17**
shaders with this PC's `glslc` (shaderc v2026.3, SDK 1.4.357.0), and afterwards `git status` was empty
and `git diff --name-only -- assets/shaders` listed nothing: every blob came out byte-identical to the
committed `.spv`. That is the local equivalent of the shader half of L10 `quick`, on this machine's
`glslc`.

### Step by step

| Step | Result | What was and was not done |
|---|---|---|
| L1 | **Failed**, stopped | Configure: exit 0, no warning in the log. Debug build: 3.8 min, exit 1; 69 projects linked, including `SupersonicCore`, `SupersonicEngine`, `GameScripts` and 64 of the 66 suite executables; the two that failed are `test_gltf` and `test_serialize` (item 1). `ctest -N`: 67 entries. **Not run:** the Release build, and ctest in either configuration. |
| L2 | Not run | Stopped at L1 by the owner's rule. |
| L3 | Not run | Stopped at L1. |
| L4 | Not run | Stopped at L1. |
| L5 | Not run | Stopped at L1. Read-only reconnaissance only (item 5): nothing built, pinned or run in any game. |
| L6 | Not run | Stopped at L1. Also needs someone to see the statistics panel's reload count. |
| L7 | Not run, needs a person | No screen control in this session. |
| L8 | Not run, needs a person | No screen control in this session. |
| L9 | Not run | Stopped at L1. |
| L10 | Not run | Stopped at L1. `gh` is logged in with the `workflow` scope and the account is ADMIN on the repository, so this is reachable. `ci.yml:61-63` puts the leg in the concurrency group, so different legs do not cancel each other. |
| L11 | Not run | Stopped at L1. Read only: `git check-attr eol tools/git-hooks/pre-push` says `lf`, and `tools/git-hooks/pre-push`, `tools/install-hooks.sh` and `tools/check.sh` contain 0 CR bytes, with `core.autocrlf=true` on this PC. |

### Commands run (PowerShell unless noted)

```powershell
git switch -c local-verification
Move-Item build build-ninja-owner
$cm = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cm -B build -G "Visual Studio 18 2026" -A x64 -DSUPERSONIC_WERROR=ON
& $cm --build build --config Debug --parallel          # exit 1, the two targets above
& $ctest --test-dir build -C Debug -N                  # listing only: 67 entries, nothing launched
git worktree add ..\engine-base 4261485                # removed again afterwards
& $cm -B build -G "Visual Studio 18 2026" -A x64 -DSUPERSONIC_WERROR=ON     # in engine-base
& $cm --build build --config Debug --parallel --target test_gltf test_serialize   # exit 1 at test_gltf:632
& $cm --build build --config Debug --parallel --target test_serialize             # exit 1 at :1356 and :1368
```

### For the owner

- **Decide how `C4702` is to be handled, then rerun L1.** The three loops are tests only, and the
  idiom is "look at the first element": a `break`/`return` inside a `for` leaves its increment
  unreachable. The honest options are rewriting the three loops so no loop body ends unconditionally,
  accepting that `SUPERSONIC_WERROR` is not yet an MSVC gate and saying so in `AGENTS.md`, or a
  `/wd4702` for the suites, which is a suppression. I changed none of them.
- If you want L2 to L11 now with `SUPERSONIC_WERROR=OFF`, that is a deliberate weakening of L1's
  gate and your call; on this PC a Debug build is about 4 minutes. Say so and the run can continue
  from L1's suites, and then L2.
- Everything the handoff lists under "Not checked" is still not checked: MSVC at run time, a real
  GPU, the device suite, Windows hot reload, the editor, the games and CI. This run adds one fact to
  that list: **under MSVC `/W4 /WX` the engine library, the editor and 64 of the 66 suites compile
  without a warning**; the other two fail on code older than the branch.

---

## Outcome, second run (evening of 11 November 2026)

**The second run stopped at L1 as well, by the same rule, for a different reason. L1's warning
gate is now green in both configurations; one suite fails in Debug, and the cause is not the PC.
L2 to L11 were not run.** Nothing in the engine, the tests, a golden or a baseline was changed
by this run. It tested `313d3b7`, which is the first run's commit `349cae7` plus the owner's
`9c983cc` (the three `C4702` loops, and the Corrections block above); the engine and test sources
are those of `9c983cc`. The machine and toolchain are the first run's (see above).

### Surprises first

**1. `test_gltf` fails in Debug on MSVC with a modal CRT assertion, and the cause is in the
vendored `tinygltf`, reached by a test this branch added.**

```
Debug Assertion Failed!
Program: ...vet\Desktop\Supersonic-Engine\build\tests\Debug\test_gltf.exe
File: C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\include\vector
Line: 1949
Expression: vector subscript out of range
```

- **It blocks, it does not just fail.** The Debug CRT shows this as a dialog and the process waits
  for a click. Under ctest it sat on it for 465 s, until I found it (ctest's default per-test
  timeout is 1500 s). I posted the dialog's own `IDABORT` (what pressing Abort does); ctest then
  recorded `test_gltf (Failed)`. Anything that runs the Debug suites unattended on a desktop
  session can hang on this, once per occurrence; `ctest --timeout` bounds it.
- **Deterministic.** By hand, from the repository root, under a guard that records the dialog and
  aborts it: Debug exits **3** with the same text; Release exits **0** with `test_gltf: 350 checks,
  0 failures` (and Release ctest `Passed` it).
- **Which test.** The file `supersonic_wrapview.gltf` was left in `%TEMP%` by the aborted run, and
  the suite removes its files when each case finishes, so the case that died is
  `testAnImageViewThatWrapsPastItsBufferIsRefused` (`tests/test_gltf.cpp:802`). It was added on this
  branch, by `0604c99` (2026-11-05, "Review fixes: the hull sort is taken back…"), and feeds an image
  buffer view with `"byteOffset": 18446744073709551614` (2^64 − 2).
- **Where.** `third_party/tinygltf/tiny_gltf.h:6415`, in the loader's image-from-buffer-view branch:

  ```cpp
  bool ret = LoadImageData(&image, idx, err, warn, image.width, image.height,
                           &buffer.data[bufferView.byteOffset],
                           static_cast<int>(bufferView.byteLength), load_image_user_data);
  ```

  `buffer.data` is a `std::vector<unsigned char>` and the offset is not checked first. This runs
  before the engine's callback, so before the engine's own check
  (`src/core/GltfLoader.cpp:107-108`, which compares each term against what is left and is
  correct). The vendored file is untouched by the branch: `git log 4261485..HEAD --
  third_party/tinygltf` is empty.
- **Reproduced without the engine.** A short program (`tg_repro.cpp`: the vendored header, the
  same four defines, the same no-op image loader; it is in the scratch directory, not the
  repository) loading the test's exact file and a control that differs only in `byteOffset`:

  | Build | `byteOffset` 2^64 − 2 | `byteOffset` 0 |
  |---|---|---|
  | `/MDd` (Debug STL) | **assertion**, `vector` line 1949, exit 3 | loads, 1 image |
  | `/MD /O2` | loads, returns `true`, 1 image | loads, 1 image |

  The program, compiled with `cl /EHsc /std:c++17 /bigobj /I third_party\tinygltf` (plus `/MDd /Od` or
  `/MD /O2`), run on the test's own JSON (`tests/test_gltf.cpp:810-829`) and on the same JSON with
  `"byteOffset": 0` in `bufferViews[2]`:

  ```cpp
  #define TINYGLTF_IMPLEMENTATION
  #define TINYGLTF_NO_STB_IMAGE
  #define TINYGLTF_NO_STB_IMAGE_WRITE
  #define TINYGLTF_NO_EXTERNAL_IMAGE
  #include "tiny_gltf.h"
  #include <cstdio>
  int main(int, char** argv) {
      tinygltf::Model model; tinygltf::TinyGLTF loader;
      loader.SetImageLoader([](tinygltf::Image*, const int, std::string*, std::string*, int, int,
                               const unsigned char*, int, void*) { return true; }, nullptr);
      std::string err, warn;
      const bool ok = loader.LoadASCIIFromFile(&model, &err, &warn, argv[1]);
      std::printf("%s images=%zu err=[%s]\n", ok ? "true" : "false", model.images.size(), err.c_str());
  }
  ```

- **What this does and does not show.** It shows the Debug failure is the vendored library indexing
  its own vector out of range on a hostile input, not the engine's code and not this PC. It does
  **not** show the optimised case is harmless: there the expression forms a wild pointer that the
  engine's no-op callback never reads, which is still out-of-range indexing (undefined behaviour) in
  vendored code. The previous run could not reach this: `test_gltf` did not compile.
- **Decision for the owner, nothing applied.** The options I can see: guard the index in the vendored
  header (a change to vendored code); have the engine reject a bad buffer view before handing the
  file to tinygltf; or record that Debug MSVC cannot run this one case until one of those is done.
  Skipping the case, or changing the STL's checking level, would weaken the check and is not on the
  list.

**2. Smart App Control: 18 of the 134 ctest entries (3 in Debug, 15 in Release) did not start, and
nothing that started failed because of it.** Per your protocol each was rerun by hand, up to three
times (table below). Separately, several fresh Release suites took about 10 s under ctest where
Debug took one or two: `test_replay` 10.10 s against 1.23 s, `test_device` 10.28 s against 1.49 s,
and `test_gltf` 10.28 s in Release. I attribute that to the reputation check on a newly linked
executable; I did not isolate it. Release ctest took 282 s, Debug 222 s once the 465 s wait on the
dialog is taken out.

**3. The warning gate is green.** `SUPERSONIC_WERROR=ON`, `/W4 /permissive- /WX`, MSVC 19.51:
Debug and Release both exit 0 with no diagnostic of any kind in the log, the three rewritten loops
included.

### L1, numbers

| | Debug | Release |
|---|---|---|
| Configure | exit 0 in 7 s, no warning (shared) | |
| Build, `SUPERSONIC_WERROR=ON` | exit 0 in 2.9 min, 0 diagnostics | exit 0 in 5.8 min, 0 diagnostics |
| ctest entries | 67 | 67 |
| Passed under ctest | 63 | 52 |
| `Not Run` (`BAD_COMMAND`, `[unknown error]`) | 3 | 15 |
| of those, ran by hand and passed (attempt 1) | 3 | 9 |
| of those, never ran in 3 attempts | 0 | 6 |
| Failed | **1** (`test_gltf`, above) | 0 |
| Skipped | 0 | 0 |
| Ran and passed, in all | 66 of 67 | 61 of 67 |

- **Debug, by hand and passed:** `test_environmentmap` 81 checks, `test_camera` 56,
  `test_blending` 45, each `0 failures`. (Exit codes were not captured on this pass: the helper
  I used lost them. The summary line is the suite's own.)
- **Release, by hand, exit 0 each:** `test_launchoptions` 217 checks, `test_json` 92,
  `test_scenemanager` 45, `test_contacts` 72, `test_joints` 228, `test_imagepixels` 43,
  `test_gameruntime` 3283, `test_packaging` 32, `test_audio` 316; all `0 failures`.
- **Release, never ran** (three attempts each; every attempt: `This command cannot be run due to
  the error: An Application Control policy has blocked this file.`): `test_transform`, `test_sat`,
  `test_heightfield`, `test_draworder`, `test_camera`, `test_audiostreamkeeper`. This is the
  machine, and I did not relink, copy, unblock or otherwise route round it. Each of the six passed in
  Debug (`test_camera` by hand), so no suite is without a pass in some configuration on this PC.
- **`test_device` was `Passed`, not `Skipped`, in both configurations**, with
  `SUPERSONIC_REQUIRE_DEVICE` unset, on the integrated AMD Radeon 780M. `test_reporttext` was
  `Passed` in both (JDK 17 found: `SUPERSONIC_JAVAC` is in the cache).
- **`test_determinism` and `test_replay` passed in both configurations** (#34 and #35: Debug 3.52 s
  and 1.23 s, Release 0.56 s and 10.10 s). The pinned hash is `kCanonical = 6794834318059694172`
  (`tests/test_determinism.cpp:476`). That is the unit-level half of L2; the replay round trip
  itself was not run.
- `git status` was empty at the end, after both builds, both ctest runs and every run by hand: each
  build recompiled all 17 of 17 shaders with this PC's `glslc` (shaderc v2026.3, SDK 1.4.357.0), and
  `git diff --name-only -- assets/shaders` lists nothing, so every blob is unchanged.

### L10, CI (dispatched first, as asked, on `claude/beautiful-cray-qs85oy` at `9c983cc`)

- **`legs=arm64`, run 43062314027: success.** One job, `Suites on Linux arm64`, 7 m 22 s, runner
  label `ubuntu-24.04-arm`, GCC 13.3.0, `100% tests passed, 0 tests failed out of 67`; `test_determinism`
  (#34) and `test_replay` (#35) `Passed`; `test_device` (#66) `Skipped` (no Vulkan device on that
  runner). The job log never prints `uname -m`, so that the machine was arm64 rests on the label.
- **`legs=all`, run 43062298112:**

  | Job | Result | Time |
  |---|---|---|
  | Configure with tests and plugin OFF | success | 8 m 12 s |
  | Shaders compile from source | success | 20 s |
  | Documented counts match the build | success | 7 s |
  | Linux (GCC) | success | 11 m 27 s |
  | Validation layers (lavapipe) | success | 13 m 47 s |
  | ASan + UBSan (Linux) | success | 30 m 37 s |
  | Suites on Linux arm64 | skipped (not part of `all`, by design) | |
  | **Windows (MSVC)** | **PENDING when this was written** | |

  The Windows job runs the suites in Debug (`config: Debug` in the matrix). Its Build step finished
  at 16:54:38 UTC and its Test step was still running at 17:36 UTC, about 41 minutes later, when
  the Debug suites take a few minutes here. That is consistent with the same dialog sitting on the hosted desktop, but
  it is not yet evidence: the log cannot be read until the run ends. Its result is added below.

### Step by step

| Step | Result |
|---|---|
| L1 | **Failed in Debug** (`test_gltf`, item 1); Release: no failure, 6 suites never ran (machine). |
| L2 to L9, L11 | Not run: stopped at L1. L7 and L8 also need a person. |
| L10 | Run first, as asked: `arm64` success; `all` six jobs success, Windows pending (above). |
| L5 | Not run. |

### For the owner

- **To proceed to L2 to L11 with the Debug `test_gltf` failure recorded as an open finding and not
  as a pass, say so.** Left alone, the stop rule applies: a genuine suite failure in L1 is neither
  a `C4702` nor a warning, so it is outside what you authorised me to route round.
- Decide how Debug MSVC should treat the vendored `tinygltf` index (item 1). Nothing else in this run
  needs a decision.
- A `ctest --timeout 180` (or similar) in `AGENTS.md`'s MSVC Debug instructions would keep a CRT
  dialog from holding a run for 25 minutes.
