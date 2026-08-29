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

### Test

```bash
ctest --test-dir build -C Debug --output-on-failure
```

Sixty-one suites. Rather than repeat the list here - the copy that used to
live in this file had fallen thirteen entries behind - see the table in
[README.md](README.md#testing), or read it from the build, which is where CI
gets it:

```bash
# Both registrars. add_port_test wraps add_engine_test, so the thirteen Wolf
# Brigade suites run like any other and a count of the first pattern alone
# reports 48 against a ctest run of 61.
grep -cE '^add_(engine|port)_test\([a-z_]+\)$' tests/CMakeLists.txt
```

They cover the maths conventions, transform parenting and play/stop snapshots,
frustum and cascade fitting, viewport picking, mesh generation and OBJ parsing,
glTF import and skinning, animation blending, scene and prefab persistence,
material assets, editor undo/redo, physics, spot and point shadow setup, the UI
canvas and its input routing, the audio mixer and WAV decoder, the job system,
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
non-zero.

Note that a plain `--frames` run sits in edit mode, so it exercises only the
half of the engine that draws: no physics steps, no script runs, no particle
moves. Passing `--scene` enters Play as well as loading the file, so a check
that wants the simulation running has to pass one. That coupling is a side
effect of the condition packaged games use rather than a designed flag, so do
not rely on it without reading `SupersonicApp.cpp` first. `--screenshot` captures the composited image - the same one the
viewport shows, already tone-mapped and encoded - so a rendering change can be
looked at rather than only reasoned about.

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

> A replay only tests what the scene actually reads. **`MainScene` reads no
> input inside a tick**, so replaying it reproduces whatever it is fed — change
> a recorded mouse delta and the hash is identical. That is a property of the
> scene, not of the feature; `test_replay` carries the claim that recorded input
> drives state.

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
