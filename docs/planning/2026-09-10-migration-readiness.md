# Can the three games start moving?

> **Record, 10 September 2026.** Written against Supersonic `engine-roadmap` at
> `4a4c5fe` plus the fixes made the same day, The Wolf Brigade at `50741d1`, HUSK
> (`Desktop/Test-Game`) at `721e19e`, and Magic Portals Remake at `79af319`. The
> three game surveys were READS of the trees, not runs; every engine claim below
> was checked in `src/` and names the file it rests on. Written on the laptop,
> the first time this engine had been built anywhere but the desktop - which is
> what the last section is about.

## The finding that decides the order

**Nothing in the engine blocks starting any of the three.** Every game can take
its first step - a sim library under `games/<name>/` with its own suites, or a
one-layer spike - on the engine exactly as it stands. What each one is missing
from the engine only bites later, when its view is being built or its port is
being finished, and every item is small next to the port work in front of it.

The second finding matters more for Wolf Brigade than for the engine: **the port
in `games/wolfbrigade/` is of a game that no longer exists.** Its `data/*.json`
match the game byte for byte only at commits `7997e5d`..`ebf3d27` (9-10 July).
The game has 34 commits since, and they are the ones that made it a different
game: real CC0 art with baked frame animation, a parallax backdrop, a
data-driven campaign, a possessed hero with Cleave and Dash, squads, respawn,
priests, a Storehouse, farms and a supply cap - and Endless mode removed. The
port plan's opening premise, that the cheapest moment to move is "while the
visual language is still nineteen coloured rectangles", stopped being true on
10 July.

---

## The Wolf Brigade

**Verdict: can start; the engine is ready for the art that landed, the port is
not.** GDScript under `scripts/` went from 4,619 to 7,112 lines since the
snapshot, and `WolfBrigadeLayer.cpp` draws flat-colour quads only - it never
names a texture.

What survives: the wave director (Endless is 59 references to delete), the
economy, the unit state machine as a skeleton, the save design, the gesture
machine and build placement. What is new: `abilities.json` and `levels.json`
(levels merge overrides over world, economy and waves), HeroControl, Squads,
CapturePoint, a 2D move band for workers where the port has `x` only, two new
unit states, four building types, fifteen new save fields. Every number pinned
in `tests/test_wb_*.cpp` is suspect until it is re-derived at HEAD, and the game
gained three sources of randomness the snapshot did not have (spawn row, camera
shake, floating-number spread). Six new headless harnesses - hero, squads,
casters, respawn, levels, difficulty - can serve as oracles, with the same rule
as before: the stepping is part of the specification.

Engine gaps, none of which blocks starting:

| | Gap | Evidence | Size |
|---|---|---|---|
| 1 | Nothing has ever drawn a textured, alpha-blended sprite and read the pixels back. `test_sprite` checks cell arithmetic, not pixels | `tests/test_sprite.cpp` | 1-2 days, and do it first |
| 2 | A flipbook cannot be mirrored: the animation system rewrites the UV transform every tick, and a negative scale is back-face culled in the opaque pass | `SpriteAnimationSystem.cpp:109-110` | hours - a `flipX` on the component |
| 3 | UI buttons know one pointer; the touch scheme holds the joystick with one finger and presses an ability with another | `UICanvas` pointer; `Input.hpp` has 8 contacts | 2-4 days, touch only |
| 4 | World-space shapes are ImGui, so they draw over sprites; the selection ellipse sits under the unit | `UIShapeComponent` | none - a ring quad with a lower `sortKey` |
| 5 | Audio has per-voice volume only, no buses | `AudioMixer.hpp` | none - the port already folds master and sfx dB into each voice |

One consequence to design in rather than discover: the blended pass sorts by
depth, the opaque pass does not, so a building fading to 0.55 drawn over opaque
units paints over the ones in front of it. Every sprite goes through the blended
pass, with `sortKey` from y.

## HUSK (`Test-Game`)

**Verdict: nothing blocks step one, and step one is still the sim.** Port the
22-system tick chain as a `HuskSim` library with `test_husk_*` suites and hold
it to the Rust per-tick FNV-1a hashes bit for bit - the strongest oracle any of
the three games offers.

Of the ten blockers the 19 August read ranked, eight are closed and two are
partial. Two headers are out of date in the pessimistic direction:
**interpolation shipped** (`SimulationClock::alpha`, `InterpolatedTransformComponent`)
and **the glTF importer is enough for this model library** - parts still merge
into one mesh, but every material keeps its own draw range and its name, and
`SurfaceOverridesComponent` recolours by that name, which is exactly HUSK's
team-colour contract. Unskinned animation is one joint per animated node, so a
unit is ONE entity, not the six the old estimate assumed.

Gaps for the view phase, not for step one:

| | Gap | Evidence | Size |
|---|---|---|---|
| 1 | 1,024 palette matrices per frame; past that a unit silently draws in its rest pose. HUSK's rigs make that ~146 animated units against a 200-unit perf scene | `VulkanRenderer.hpp:251` | S - grow it, and warn on overflow |
| 2 | Game text cannot choose a font; it draws with the editor's | `UISystem.cpp`, `UITextComponent` | S-M |
| 3 | Multi-material models never batch across units - a batch only merges identical index ranges | `RenderSystem.cpp` batching | S |
| 4 | No colour grading or tonemapper choice | `RenderSettings.hpp` | S-M, look only |

Two determinism notes for whoever ports the sim. Pin `-ffp-contract=off` and
`/fp:precise` on the sim target; the engine sets neither. And the engine's
`--record`/`--replay` does not fit HUSK: its state hash includes transforms a
HUSK camera moves by frame time, so keep HUSK's own tick-stamped order log as
its replay format and feed its hash through `StateHash`'s contributor seam.

## Magic Portals Remake

**First assessment. Verdict: can start with a spike; one decision is needed
early.** A 2D side-view physics puzzle-platformer, Android-first. The original's
AngelScript is not interpreted - it is read as reference and reimplemented - so
the missing scripting language is not a blocker. The physics is lighter than it
sounds: explosions destroy or ignite in a radius and push nothing, there is no
fracture and no ropes, and hinges with limits and a motor are a better fit here
than in Godot, whose `PinJoint2D` drops the original's motor torque.

| | Gap | Evidence | Size |
|---|---|---|---|
| S1 | **No per-axis lock.** A 2D body must stay in the plane and rotate about z only; `freezeRotation` is all or nothing. It does not stop the spike - measuring the drift without it is the spike's first job, and that number decides whether this is needed before the port or during it | `Components.hpp:833`, `PhysicsSystem.cpp:239` | 3-5 days |
| S2 | Polygon colliders from data - hulls come from meshes only | `ConvexHullCache.cpp` | none - 13 distinct polygons become 13 extruded OBJ prisms |
| F1 | A kinematic body displaces a rider rather than carrying it (29 movers) | `ARCHITECTURE.md` §7m | 2-4 days |
| F2 | Triggers never fire against kinematic bodies - the broadphase skips pairs with no mass on either side | `ARCHITECTURE.md` §7k | none if actors are dynamic; 0.5-1 day otherwise |
| F3 | No additive blending (99 nodes use it) | `VulkanPipeline.cpp` blend state | ~1 day |
| F4 | 46 MP3s; the decoder is WAV only | `AudioClip.cpp` | none - transcode in the converter |
| F5 | Android | README, Platforms | the same 2-4 months as everyone |

The spike: `games/magicportals/`, one converted level (level30 - it has rigid
bodies and movers; level0 has neither), at 50 px per metre so the solver's
metre-tuned constants land where the original's Box2D did. Measure out-of-plane
drift over 3,600 ticks, landing on polygon geometry, hinge limits and motor
speed, riders on a mover, trigger events for the player, and velocity through a
portal.

*Running since 10 September: `2026-09-10-magic-portals-spike.md`. Correction
from it: no level enables a hinge motor, so the motor half of the hinge argument
above is moot for this data. What matters is the limits, which all ten hinges
set.*

---

## What the first build on a second machine found

The laptop: AMD Radeon 780M, Visual Studio 2026 Build Tools (MSVC 14.50), no
Vulkan SDK, Smart App Control enforced; and WSL Ubuntu 24.04 with GCC 13.3,
GCC 14 and clang, glibc 2.39. Both toolchains now build with no warnings from
the project's own code. MSVC passes all 68 suites; the Linux build passes 67,
and the one it fails is the determinism constant - which is left failing on
purpose, because moving it is a decision rather than a fix (below).

- **The Linux build did not compile.** GCC 13.3 - Ubuntu 24.04's compiler -
  hits an internal compiler error on the second `RawInputState{}` in
  `Input::ClearBindings`, most likely since `RawInputState` gained its contact
  array on 26 August. Worked around with one temporary and a copy.
- **A fixture only worked on MSVC.** `ScratchData` copied the shipped data with
  `copy_options::overwrite_existing` alone; the standard copies a directory's
  contents only with `none` or `recursive`, and libstdc++ holds it to that. Under
  GCC every authored-data case ran against one file, which is two failed suites
  and a segfault in `test_wb_match`.
- **Two floors counted checks their own comments exclude.** `test_audio` and
  `test_wb_audio` say their device-dependent checks are not in the floor; the
  floors were exactly the with-device counts, so any machine without a sound
  card failed both. Now 47 and 260, verified with and without a device.
- **Warnings accumulated while CI was off,** all introduced 17-30 August:
  seven MSVC `/W4` sites; under GCC four in engine code and sixteen lines in the
  port's tests, fifteen of them one comment ending in a shell continuation. Six
  more are a GCC 13 false positive (`-Wdangling-reference`) in one file, which is
  exempted rather than rewritten.
- **`APPDATA` was read through the ANSI code page**, which mangles a profile
  folder with a character outside it. Read wide now, with a regression test that
  fails against the old code.
- **Smart App Control blocks freshly built test binaries at random** - up to 23
  of 68 were refused on one run, a different set after every rebuild. A
  Windows-only verification loop is not reliable on this machine; the WSL build
  is.

- **Determinism does not cross from Windows to Linux.** The README listed this
  as the one experiment never run. `test_determinism` pins four seconds of its
  fixture scene to `8818694387102185031`, verified under MSVC and GCC 15.2 -
  both on the UCRT - and MSVC 14.50 on this laptop still gives it. Every glibc
  build disagrees, and agrees with every other: GCC 13.3, GCC 14.2 and clang 18.1
  on glibc 2.39 all give `7854318744396420989`. Two compiler families on each
  side, split by nothing but the C runtime. The fixture is built in code rather
  than read from disk, so line endings and paths are ruled out. The functions
  the test's own comment names - `asin` in the Euler conversion, `pow` in the
  damping curve - are the obvious suspects; which one diverges first has not
  been measured. What it means: a replay recorded on Windows does not reproduce
  on Linux, and lockstep between the two would desync. HUSK's sim, which per its
  survey has no trig and two `sqrt` calls, is far less exposed than the engine's
  own physics. The constant is left as it is, and the Linux run fails on it.
- **The renderer is validation-clean on Linux, and the frame differs by one
  bind.** `AllPasses.scene`, 60 fixed-step frames, on lavapipe under
  `VK_LAYER_KHRONOS_validation`: no messages, clean exit. Drawables, draw calls,
  pipeline, mesh and skinned counts match the Windows run; material binds are 9
  on Windows and 8 on Linux. `4a4c5fe` with none of the day's changes also
  gives 8 on Linux, so the difference is the platform, not those changes.
  Material keys are only ever compared for equality, so the likeliest cause is
  the divergence above shifting the blended pass's depth order after 60 frames
  of play - likely, not measured. `verify-replay` passes on the Linux build too:
  180 of 180 ticks, and a corrupted checkpoint caught at tick 180.

## Decisions for the owner

1. **Which game first.** Recommended: re-sync Wolf Brigade first - it is the
   only one with a port in the tree, the method is proven, and the delta is the
   smallest - then HUSK's sim, which needs no engine work and has the best
   oracle, then Magic Portals after its spike has decided S1.
2. **Whether to build the small gaps now or when a port asks.** This project's
   rule has been the second, and it has been right each time: the layer seam,
   the pathfinding decision and the sprite work all came out better for having
   a real caller. The cheapest of them - `flipX`, the palette overflow warning -
   are hours each if you would rather have them in hand.
3. **What the determinism claim is.** Scope it to one C runtime - true today,
   and enough for bug reports and regression replays on one platform - or make
   it cross-platform, which means the physics stops calling the platform's
   `asin` and `pow`: its own implementations, or a rotation representation that
   does not need them. The 28 August plan said to find this out before building
   anything on top of the physics. It is now found, and the Linux build stays
   red on it until this is decided - not until a second constant is added.
