# AGENTS.md — Build & Run

Exact commands for building, testing and running **Supersonic Engine**.

## Requirements

- **Compiler**: C++20 (MSVC 2022, GCC 11+, Clang 13+).
- **CMake**: 3.20+.
- **Graphics**: a Vulkan-capable driver.
- **Vulkan SDK**: strongly recommended, and effectively required for development.
  Without it you get no validation layers and no shader compiler — see below.

### Why the SDK matters

Without `VK_LAYER_KHRONOS_validation`, invalid Vulkan usage does not produce an
error message. It produces an access violation, or nothing at all until the code
runs on someone else's driver. Two showstopper bugs in this repository survived
six commits for exactly that reason.

The SDK also supplies `glslc`. Without it CMake cannot rebuild the shaders and
falls back to the committed `.spv` blobs, so edits to the GLSL are silently
ignored. CMake prints a warning when this happens.

The build **works** without the SDK: it falls back to the vendored Vulkan
headers plus the loader that ships with the driver, generating an import library
from the loader's export table at configure time.

## Build

### Configure

```bash
# Single-config generators (Ninja, Makefiles) - build type chosen here.
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```powershell
# Visual Studio is a MULTI-CONFIG generator: it ignores CMAKE_BUILD_TYPE.
# Choose the configuration at build time with --config, not here.
cmake -B build -G "Visual Studio 17 2022" -A x64
```

### Compile

```bash
cmake --build build --config Debug      # --config is required for VS/Xcode
cmake --build build --config Release    # and ignored by Ninja/Makefiles
cmake --build build --parallel
```

> Passing `-DCMAKE_BUILD_TYPE=Release` to the Visual Studio generator and then
> building `--config Debug` produces a Debug binary. CMake prints a note about
> this at configure time.

### As a subproject of a game

A game builds the engine with `add_subdirectory(engine)` and gets
`SupersonicCore`, `Supersonic::TestHarness` and `supersonic_add_test` - no
editor, plugin or engine suites, which default ON only when the engine is the
top-level project. The contract and a minimal `CMakeLists.txt` are in
[README.md](README.md#building-a-game-against-the-engine). Two things to keep
true when changing the engine's CMake: nothing a game needs may live only in a
directory-scoped setting (a subproject's parent inherits none of them - carry
it on `SupersonicCore`'s interface instead), and nothing may be added that a
subproject build would define twice.

### Test

```bash
ctest --test-dir build -C Debug --output-on-failure
```

Fifty-eight suites. Rather than repeat the list here - the copy that used to
live in this file had fallen thirteen entries behind - see the table in
[README.md](README.md#testing), or read it from the build, which is where CI
gets it:

```bash
# One registrar since the game ports left for their own repositories, and
# digits in the pattern: without them test_light2d is not counted.
grep -cE '^add_engine_test\([a-z0-9_]+\)$' tests/CMakeLists.txt
```

They cover the maths conventions, transform parenting and play/stop snapshots,
frustum and cascade fitting, viewport picking, mesh generation and OBJ parsing,
glTF import and skinning, animation blending, scene and prefab persistence,
material assets, editor undo/redo, physics, spot and point shadow setup, the UI
canvas and its input routing, the audio mixer and decoders, the job system,
the script registry, packaged-game manifests and command-line parsing. Disable
with `-DSUPERSONIC_BUILD_TESTS=OFF`.

## Run

**Asset paths are relative to the working directory. Launch from the project
root**, or shaders, scenes and audio will not resolve.

```bash
# Linux / macOS
./build/SupersonicEngine                          # from the project root

# Windows
.\build\Debug\SupersonicEngine.exe                # from the project root
```

Visual Studio's `VS_DEBUGGER_WORKING_DIRECTORY` is set to the project root, so
pressing F5 works without any extra setup.

### Running without a person watching

```bash
# Render 120 frames and exit. Non-zero on a validation error or a crash.
./build/Debug/SupersonicEngine.exe --frames 120

# Same, against a chosen scene, writing a PNG of the last frame.
./build/Debug/SupersonicEngine.exe --frames 120     --scene assets/scenes/MainScene.scene --screenshot shot.png
```

`--frames` is what makes any automated check of the engine possible: the run
exits by itself, and an ERROR-severity validation message makes it exit
non-zero. So does a capture it was asked for and did not write: every stamped
and final PNG of a `--frames` run is counted (`LaunchOptions::CapturesOwed`),
and a run that wrote fewer fails rather than leave an older file at the path to
pass for this run's. Without `--frames` nothing is owed, since whoever closes
the window picks the last frame.

Note that a plain `--frames` run sits in edit mode, so it exercises only the
half of the engine that draws: no physics steps, no script runs, no particle
moves. Passing `--scene` enters Play as well as loading the file, so a check
that wants the simulation running has to pass one. That coupling is a side
effect of the condition packaged games use rather than a designed flag, so do
not rely on it without reading `SupersonicApp.cpp` first. `--screenshot` captures the composited image - the same one the
viewport shows, already tone-mapped and encoded - so a rendering change can be
looked at rather than only reasoned about.

```bash
# Every 30th frame of one reproducible run: shot_f30.png ... shot_f420.png,
# fourteen files, and the last frame still written to shot.png itself.
./build/Debug/SupersonicEngine.exe --frames 420 --fixed-step     --scene assets/scenes/MainScene.scene --screenshot shot.png --screenshot-every 30
```

`--screenshot-every <N>` is for checking anything that moves. Each extra frame
used to cost a whole launch that loads everything again to reach a frame the
last launch had already drawn; one run now writes them all.

- The stamped name is the path's stem, `_f<frame>`, then its extension; the
  frame is the count of frames drawn, not zero-padded.
- `<path>` is still written at the end of a `--frames` run, read back from
  the same presented image as the last stamped frame in the same loop
  iteration, so the two are byte-identical and a script comparing `<path>`
  keeps working.
- Refused, with a non-zero exit, without `--screenshot` or with N below 1.
- It does not need `--frames`, and without it keeps writing every N frames
  until the window closes - pass `--frames` unless that is what you want.
- Without `--fixed-step` it runs but logs a warning: frame N is then not a
  fixed game time, and the frames will not reproduce.
- Every capture stalls the queue, so the profiler's worst frames and the wall
  time of such a run are the readback's, not the scene's.

```bash
# The window as it is shown, UI included: HUD text, panels, buttons - and in
# the editor, the editor. Beside --screenshot or on its own.
./build/Debug/SupersonicEngine.exe --frames 120 --screenshot shot.png     --screenshot-ui shot_ui.png
```

`--screenshot` reads the composited scene, which the ImGui pass then draws
every UI component over, so no label or panel has ever been in one.
`--screenshot-ui <path>` reads the swapchain image back after that pass
instead, at the window's size.

- It is written when `--screenshot` is: after the last frame of a `--frames`
  run, and with `--screenshot-every N` at every Nth frame as
  `<stem>_f<frame><ext>`. Either path is enough for `--screenshot-every`; the
  two may not name the same file.
- The copy is recorded into the frame itself, because a swapchain image cannot
  be read once presented; a frame that is not drawn (a swapchain rebuilt
  mid-run) logs an error rather than writing an older one, and the run then
  exits non-zero.
- It needs a surface that offers transfer-source swapchain images. One that
  does not logs an error and writes no UI file; the run and `--screenshot`
  go on, and a `--frames` run exits non-zero at the end for the missing file.
- BGRA swapchains are swizzled, and the alpha is written as 255.

```bash
# Open at a chosen size. In game mode this is the render resolution too.
./build/Debug/SupersonicEngine.exe --frames 300 --window 1920x1080
```

`--window <W>x<H>` overrides whatever `game.manifest` asked for, for one run.
It exists so a measurement taken at one resolution can be retaken at another
without editing a literal and rebuilding — which is how the "the GPU is idle,
this is a CPU-bound engine" premise was finally checked at 1080p instead of in
a 755x389 editor viewport. In the editor the flag sizes the window and the
offscreen target still follows the viewport; **in game mode the offscreen
target follows the window**, so there the flag really is the render resolution.

```bash
# Open covering the monitor, at the mode it is already in; or in a window,
# whatever the manifest says.
./build/Debug/SupersonicEngine.exe --fullscreen
./build/Debug/SupersonicEngine.exe --frames 300 --windowed --window 1280x720
```

`--fullscreen` and `--windowed` override a manifest's `Fullscreen` key for one
run, and are refused together. `--windowed` is the one a script wants: a
headless capture of a game that ships fullscreen would otherwise cover the desk
it runs on, and render at the monitor's resolution rather than the one asked
for. A game changes the mode itself, while it runs, through `WindowControl`
(`ARCHITECTURE.md` §8b).

```bash
# The same run with no window on screen at all: created hidden, never shown,
# never focused. The PNG is written as before.
./build/Debug/SupersonicEngine.exe --hidden --frames 300 --fixed-step     --window 1920x1080 --screenshot shot.png
```

`--hidden` is for a capture on a desk somebody is using. Nothing in it shows,
focuses or resizes the window: the manifest's `Fullscreen` key and its fitted
window are skipped, every `WindowControl` request a game makes is dropped (a
restore or a maximise would show it), and a fatal error goes to stderr and the
log rather than to a message box. It is refused beside `--fullscreen`, and
warns without `--frames`, since there is no window to close.

### Recording a run and playing it back

```bash
# Play, and write down everything each tick was handed.
./build/Debug/SupersonicEngine.exe --scene assets/scenes/MainScene.scene     --record session.replay

# Feed it back and check it against the hashes it stored. Non-zero, with the
# tick number and both hashes, if it does not reproduce.
./build/Debug/SupersonicEngine.exe --frames 180 --fixed-step     --scene assets/scenes/MainScene.scene --replay session.replay
```

`--record` and `--replay` are refused together: a run recording the input it is
being fed writes a file that agrees with itself by construction.

The file is text and delta-encoded — three seconds of the demo scene is about
2 KB — so it can be read, diffed and hand-edited. `ARCHITECTURE.md` §8e explains
the two rules that make it work: levels persist between ticks and edges do not,
and floats are written as their bits.

> **This is about the DEMO SCENE, not about the engine.** Since the pointer
> joined a recorded tick, a session played with a mouse — Wolf Brigade's taps,
> drags and orders included — records and replays. The suite that carries that
> claim end to end, `test_wb_hud` (a tap recorded in one run selects the same
> unit in the next, through the file, with the devices left at rest), moved with
> the port to The-Wolf-Brigade repository's `supersonic/tests`; here,
> `test_replay` holds the engine's half, the pointer in the file included. What
> is still scoped is the SCREENSHOT-level check below.
>
> A replay only tests what the scene actually reads. **`MainScene` reads no
> input inside a tick**, so replaying it reproduces whatever it is fed — change
> a recorded mouse delta and the hash is identical. That is a property of the
> scene, not of the feature; `test_replay` carries the claim that recorded input
> drives state.

### Checking the determinism claim before a push

```powershell
pwsh tools/verify-replay.ps1
```

Records a session, replays it, then **corrupts one checkpoint and requires the
replay to fail at that tick**. The third step is the point: a harness that never
diverges and one that cannot detect divergence produce identical output on the
first two, and this project has shipped that shape of mistake before.

It lives here rather than in CI because CI is dispatch-only — the repository is
private, so automatic runs are charged. Run it before pushing anything that
touches the tick loop, the state hash, or the recording format. It takes under a
minute and needs no runner.

### With validation layers, if the SDK is not installed system-wide

```powershell
$env:VK_LAYER_PATH = "C:\path\to\VulkanSDK\Bin"
.\build\Debug\SupersonicEngine.exe
```

Add `$env:VK_LAYER_VALIDATE_SYNC = "1"` to enable synchronization validation,
which catches the frame-overlap hazards a plain validation run does not.

## Hot-reloading scripts

The engine loads `GameScripts` from the build output directory and watches it.

```bash
# with the engine already running:
cmake --build build --config Debug --target GameScripts
```

Edit `plugins/sample_scripts/SampleScripts.cpp`, rebuild that target, and the
new code is swapped in without a restart. The statistics panel shows the reload
count; assign scripts by name in the Inspector.

Disable the plugin with `-DSUPERSONIC_BUILD_SCRIPT_PLUGIN=OFF`; the engine runs with
built-in scripts only.

## Shaders

CMake compiles `assets/shaders/*.vert|frag` to SPIR-V automatically when a
compiler is available. To do it by hand:

```bash
cmake --build build --target Shaders
```

This replaces `compile_shaders.bat`, which is gone. The script was Windows-only
and named its shaders in a list of its own, which had drifted to four of the
twelve the build actually declares — so running it regenerated `shader` and `grid`
and silently left `shadow`, `fullscreen` and the three `bloom` blobs stale. The
CMake target is generated from `SHADER_JOBS`, the same list CI reads, so it
cannot fall behind the build in the first place.

CI compiles every declared shader and compares the result byte for byte against
the committed `.spv`, so a stale blob fails the build rather than shipping.

## Clean

```bash
cmake --build build --target clean
rm -rf build            # or: Remove-Item -Recurse -Force build
```

## Other platforms

Android and iOS scripts exist under `platform/` but **do not produce runnable
apps yet** — the engine creates its window and surface through GLFW, which has
no backend on either. Each script explains what is missing. See the platform
table in `ARCHITECTURE.md`.
