# Capability matrix and gap list, 19 November 2026

A dated record, not a live document. It compares Supersonic Engine with Godot, Unity
and Unreal across 19 domains and ranks what is missing for the owner's three genres
(2D platformers, 3D strategy, 3D shooters). It is the input to the detailed plan.

**How it was made.** One multi-agent run (workflow `wf_3a1aa15b-4ff`): each of 19 domains
was surveyed against the code and the competitors, every survey was re-checked by an
independent agent (which changed 220 of 633 rows, added 76 and dropped 5), and the
synthesis below was reviewed by three reviewers (completeness, accuracy of engine claims,
honesty) and edited. The 633 rows are in
[2026-11-19-capability-matrix-rows.md](2026-11-19-capability-matrix-rows.md).

**What to trust, and what not.**
- **Claims about this engine** were checked against the code where the text says "seen";
  "(r)" means a reviewer cited it and it was not reopened. A search for an absent
  feature rests on the search terms it lists.
- **Every competitor claim is recollection**, not checked against release notes. The
  environment could not reach the competitors' documentation sites.
- **Every cost is a guess** (person-weeks of one developer with an AI assistant, about
  40 focused hours), scaled by the editor from the matrix. None is validated against
  this repository's history.
- **The synthesis saw about 120 of the 633 rows** (cut by value per cost). The expensive
  rows that decide strategy and shooters may be the unseen ones.

**Names used in the text.** `roadmap.md` is
[2026-11-19-frontier-rendering-roadmap.md](2026-11-19-frontier-rendering-roadmap.md);
`owner-decisions.md` is [2026-11-19-owner-decisions.md](2026-11-19-owner-decisions.md).
Section 7's decisions 1-16 were open when this was committed.

---

## Plain answers first

Your three open questions, in short. The full text is in section 7.

1. **Universal engine, with 2D platformers, 3D strategy and 3D shooters as the test.** The plan below uses those three as the yardstick. Everything else a universal engine needs is the long tail in section 5.
2. **"Deterministic"** means the same inputs give the same result, bit for bit, on every machine. Strategy games use it to send commands instead of unit positions, and it makes a recorded replay play back identically. Measured so far: x64 Windows with MSVC, x64 Linux with GCC, and arm64 Linux with GCC. Not yet run: macOS or Clang, or an Android device.
3. **"Speed suite"** is a small set of fixed benchmark scenes. It asserts counts (draw calls, allocations) and only records milliseconds. It is a smoke alarm that says when a change made the engine slower. It is not started.

**Reading conventions.**
- Genre digits like "333" or "121" are the matrix's 0-3 scores for 2D platformer, 3D strategy and 3D shooter, in that order. This is my reading of the digest, which does not state it.
- **UPG** means "does this need an UPGRADING.md entry".
- **ABI** is the binary interface between the engine and script plugins.
- **Codec** is the scene/prefab save-and-load code.
- **Lavapipe** is the cloud's software Vulkan device.
- **Lane** is a stream of work that can run in parallel with others.
- **Seen** means I opened that line this pass. **(r)** means a reviewer cited it and I did not reopen it.

---

## 1. What the matrix says

**Scope.** 633 rows in 19 domains. 19 of 19 domain surveys returned. That is not a claim that the capability space is covered; section 6 lists what has no home in the matrix.

Overall coverage (full = 1, partial = 0.5, unknown competitor cells excluded):

| Engine | Coverage | Source |
|---|---|---|
| Supersonic | 26% | the matrix, with code spot-checks (section 6) |
| Godot | 78% | unverified recollection |
| Unity | 87% | unverified recollection |
| Unreal | 89% | unverified recollection |

Supersonic as a share of Godot is 26/78 = 33%, and the same caveat applies. Every competitor number in this document is recollection (my knowledge ends around June 2026). I made no web checks. A reviewer reported that official-docs hosts were unreachable from the session.

**Why these numbers are crude.**
- Every row has the same weight. A VR row counts as much as the character controller.
- A partial scores 0.5 however partial it is. Most of the gap list is partials, and several of those (see section 2) are mostly built.
- The same capability is scored in several domains, so each missing feature is counted two to four times. RNG sits in three domains and time control in four. Names/tags, scene switching, save-game, HDR/bloom and fixed-timestep interpolation each sit in two or three. physics2d mostly re-scores the 3D solver under axis locks. A de-duplicated figure would be higher than 26%, by an amount I cannot compute from the digest.
- Row granularity was chosen by the surveyors.
- The independent re-check changed 220 of 633 rows (35%), added 76 and dropped 5. In tooling it changed 20 of 33 rows (61%). Rankings that differ by a few points are within noise.
- Competitor ceilings are low in some domains because of which rows were listed. ai_nav is 41/45/61. A low Godot score there does not mean the work is small. Godot ships navmesh and avoidance, which are the rows this owner needs.

| Domain | Rows | Supersonic | Godot | Unity | Unreal | Share of Godot |
|---|---|---|---|---|---|---|
| tooling | 33 | 32 | 56 | 84 | 92 | 57% |
| core_runtime | 32 | 41 | 81 | 92 | 92 | 51% |
| physics3d | 34 | 37 | 82 | 87 | 97 | 45% |
| physics2d | 35 | 39 | 90 | 91 | 81 | 43% |
| editor | 34 | 38 | 90 | 96 | 96 | 42% |
| render3d | 39 | 31 | 82 | 85 | 100 | 38% |
| platforms_xr | 33 | 32 | 88 | 97 | 91 | 36% |
| input | 31 | 29 | 82 | 92 | 92 | 35% |
| ai_nav | 33 | 14 | 41 | 45 | 61 | 34% |
| ui | 32 | 31 | 94 | 92 | 95 | 33% |
| scripting | 30 | 27 | 82 | 86 | 93 | 33% |
| services_ecosystem | 33 | 20 | 61 | 84 | 91 | 33% |
| render2d | 33 | 27 | 89 | 89 | 61 | 30% |
| assets_build | 34 | 26 | 87 | 94 | 91 | 30% |
| networking | 33 | 11 | 47 | 82 | 82 | 23% |
| audio | 31 | 19 | 84 | 94 | 97 | 23% |
| world | 35 | 13 | 69 | 84 | 92 | 19% |
| vfx | 33 | 15 | 84 | 94 | 97 | 18% |
| animation | 35 | 11 | 86 | 90 | 90 | 13% |

**Tiers, not ranks.**
- Upper tier (42-57% of Godot): tooling, core_runtime, physics3d, physics2d, editor. These are the parts the engine was built around.
- Middle tier (30-38%): render3d, platforms_xr, input, ai_nav, ui, scripting, services_ecosystem, render2d, assets_build.
- Lower tier (13-23%): networking, audio, world, vfx, animation.
- The gap to Godot in the lower tier runs from 36 points (networking) to 75 (animation).

**Scoring notes from the code.**
- physics2d "materials (friction, bounce, combine rules)" is scored full. The combine rules are fixed. Restitution is the max clamped to 0.99, and friction is the geometric mean clamped to 4 (`PhysicsSystem.cpp:97-109`, seen). A partial, or a rename to "friction and bounce", would be fairer.
- Joint limit, spring and motor are Hinge-only (r, `Components.hpp:1487-1513`).
- 2D shapes are 3D shapes under axis locks (`Components.hpp:1226-1236`, seen).
- Determinism is scored full in physics3d and physics2d but partial in core_runtime. It should be scored once, the same way everywhere. The evidence:
  - The pinned hash passes on x64 MSVC and x64 GCC. The determinism record says both hash identically after `DetMath`.
  - It passes on arm64 Linux GCC: CI run 43101171042 passed `test_determinism` (#34) and `test_replay` (#35), with the machine confirmed as aarch64 (`docs/planning/2026-11-11-local-verification-handoff.md`, around lines 940-946, seen).
  - Not run: macOS libm or Clang, an Android device, or a game's own `std::sin` inside its tick.
  - `README.md:1404` and `docs/planning/2026-09-10-cross-platform-determinism.md:118` still say arm64 has not been run. That predates the handoff and is stale.

**Ahead or at parity, honestly.**
- No domain is ahead on score, and none is at parity.
- At least two rows put Supersonic ahead of at least one competitor. Both are in core_runtime: cross-platform deterministic simulation, and input recording with state-hash desync detection. The files are `src/core/StateHash.cpp`, `InputRecording.cpp`, `DetMath.hpp` and `TimeTravelDebugger.cpp`.
- It is "at least two" because I saw only the gap list (about 120 of 633 rows). The full-scored determinism rows in physics3d and physics2d are probably ahead as well.
- The competitor side of this claim is unchecked. The owner's decisions record says "as far as is known none of Godot, Unity or Unreal offers it natively (not checked)" (`docs/planning/2026-11-19-owner-decisions.md` section 3, seen). The matrix says Unity partial. Soften the claim until it is verified.
- The two-laptop replay is a plan in that record (section 7), not a recorded result. I found no result recorded in it.
- Cheap-parity rows where a competitor has nothing (unchecked): bad-link simulation (Godot), 2D camera shake (Godot), 2D camera bounds (Unreal), and a grid navigation field (Unity, Unreal). The last is an architecture difference, not an advantage. `NavGrid` exists (item 24).

---

## 2. The gaps that matter most

**How ranked.** By how many of the owner's three genres a thing blocks, then by how many other items depend on it, then by cost.

**Weeks and what changed from the first draft.**
- The first draft's weeks were the matrix's per-row ranges. Most top-ranked rows were partials, and the first draft priced them as unbuilt.
- Every item below now has **Built** (what the code already does) and **Remainder**.
- Where I opened the code and found the bulk built, I scaled the weeks down. These are **editor estimates**, not measurements. The roadmap says none of its ranges are validated against this repo's history, and the same applies here.
- Where I did not open the code, the matrix range is kept and marked "not re-checked".
- The unit is one developer with an AI assistant, about 40 focused hours a week. The digest does not state a unit.
- 2D and 3D physics rows are costed separately in the matrix, but the 2D game runs on the 3D solver with axis locks. Where I say "shared", that is my judgement.

**UPG:**
- *Yes* means existing behaviour, a hash, a picture, the ABI or a file format changes.
- *Additive* means a new opt-in API or key with a default that keeps old results.
- *No* means nothing a game sees.
- AGENTS.md and UPGRADING.md rule 1 (line 54, seen) require the entry in the same commit as the change.

**Verify** uses the matrix's tags: cloud, local-pc, device-or-eyes. The cloud has lavapipe and no GPU. Anything that changes a picture runs on both owner laptops.

### 2A. Gameplay simulation

**1. Physics filtering, triggers, collision events (physics3d, physics2d, scripting).**
- **Built (seen).**
  - `layer` and `collidesWith` exist on box, sphere, capsule, hull and heightfield colliders (`Components.hpp:1343-1372, 1398-1400, 1602-1604, 1642-1644`).
  - `isTrigger` exists on each, and is serialised (`ComponentCodec.cpp:402-480`).
  - The broadphase requires both sides to agree (`PhysicsSystem.cpp:496`).
  - `Raycast`, `OverlapSphere` and `IsGrounded` take `layerMask` and `includeTriggers` (`PhysicsSystem.hpp:79-99`).
  - `ContactTracker` emits Enter, Stay and Exit with normal and `isTrigger` (`ContactTracker.hpp:45-65`).
  - ABI v7 exposes `contactCount` and `contactAt` (`ScriptPluginApi.h:239-240`).
- **Remainder.**
  - `layerMask` and `includeTriggers` on the ABI raycast and `isGrounded`. Neither appears in `ScriptPluginApi.h`.
  - Contact point and impulse. `ContactTracker::Event` carries only other, normal, `isTrigger` and phase.
  - Named layers or a layer matrix, and per-pair exceptions.
  - Kinematic-versus-static pairs. `PhysicsSystem.cpp:491` skips any pair where both inverse masses are 0, and `inverseMassOf` returns 0 for kinematic bodies (`:236-244`). So a static trigger never fires for a kinematic character.
- **Weeks:** 2-5 (was 7.5-15.5 row by row, 4-9 if shared).
- **UPG:** Yes.
  - The ABI revises (v14).
  - The pair set changes for scenes with kinematic bodies beside static ones, so hashes there can move.
  - The first draft's claim that "the codec gains layer fields" is withdrawn. They exist.
- **Verify:** cloud, with `tools/verify-replay.sh`.

**2. Character controller and platformer physics (physics2d, physics3d).**
- **Built.**
  - An `IsGrounded` query with `layerMask`.
  - Per-axis position and rotation locks that run 2D on the 3D solver (`Components.hpp:1226-1236`).
  - A kinematic flag.
- **Not found.** CharacterController, stepOffset, slopeLimit and maxSlope have no hits in src or plugins.
- **Remainder (matrix rows).**
  - Planar bodies and ground/wall/ceiling state.
  - Slopes.
  - Per-body gravity scale and velocity clamp.
  - Forces, impulses and torque for explosions.
  - 3D kinematic body driving.
- **Weeks:** 5.5-11.5, plus shape casts (P1).
- **Depends on:** item 1's kinematic-versus-static rule.
- **UPG:** Additive, with an opt-in flag so recordings do not move.
- **Verify:** cloud for correctness. Jump arcs and slope feel need the owner's hands.

**3. Query layer (physics2d, physics3d).**
- **Built.** `Raycast` returns a single `RayHit` (`PhysicsSystem.hpp:79`). `OverlapSphere` exists.
- **Remainder.**
  - Multi-hit raycast.
  - Overlap by box, capsule or any shape.
  - Exact-shape 2D raycast.
  - Shape casts (P1).
- **Weeks:** 2-5 (was 4.5-9). **UPG:** Additive. The ABI signature change rides with item 1's v14. **Verify:** cloud.

**4. Native seam and script API breadth (scripting).**
- **Built (names only, not opened).** `EngineLayer.hpp`, `LayerStack.hpp`, `test_layerstack.cpp`.
- **Remainder.** The matrix's native subsystem seam row is not re-checked. "Script API breadth" is the named dependency of about a dozen rows (audio API, spawning, activation, lookups, time control, UI events, persistent state). It is paid inside those rows, not here.
- **Weeks:** 1.5-3 (not re-checked). **UPG:** Yes (ABI).
- **Note.** The ABI is at v13. Versions 11, 12 and 13 each rescoped input to the tick (`ScriptPluginApi.h:22-35`, seen). Settle breadth before choosing an embedded language (decision 1).

**5. Entity lifecycle (world, scripting, vfx).**
- **Built (seen).** `spawnPrefab` and `destroyEntity` are in ABI v7 (`ScriptPluginApi.h:244-245`). `loadScene` is in ABI v8 (`:261`). `PrefabSerializer.cpp` exists.
- **Remainder.** Hierarchy-aware prefab write and read with parent remap, pooling, and entity activation (enable, disable or hide a subtree).
- **Weeks:** 1.5-4 (was 2.5-6).
- **UPG:** Yes only if the prefab format changes for hierarchy. Additive for activation.
- **Verify:** cloud.

**6. Identity, references, events (core_runtime, scripting).**
- **Built.** `TagComponent` (`Components.hpp:1183`, seen). `ContactTracker` events are polled by scripts.
- **Remainder.** Groups and lookup by name or tag from scripts, a lifecycle and signal system with a polling queue for scripts, and stable entity references (P2).
- **Weeks:** 2-4, plus P2.
- **UPG:** Yes if stable ids enter the scene format.
- **Verify:** cloud.

**7. Time control: pause, scale, hit-stop, unscaled time (core_runtime, scripting, physics2d, animation).**
- **Built.** Editor Pause and single-step only (`PlayMode.hpp:11-41`, seen).
- **Not found.** timeScale, SetPaused, hitStop and unscaled-time have no hits in game-callable code.
- **Design.** Four matrix rows, one design: scale the number of ticks per frame, not the delta, so the simulation stays deterministic. Reuse the `PlayMode` gate.
- **Weeks:** 3.3-7.5. **UPG:** Additive if scale 1 is recorded as absence, Yes if the replay line format changes. **Verify:** cloud, with verify-replay.

**8. Deterministic RNG and proof of determinism (core_runtime, scripting, networking).**
- **Built.** No RNG service exists.
- **The one RNG that does exist is a hazard** (`ParticleSystem.cpp:13-18`, seen).
  - It is a function-static `std::mt19937{12345u}` plus `std::uniform_real_distribution<float>`.
  - It is never reseeded, so a second Play continues from the first run's state.
  - `StateHash.cpp` has no particle or random term (searched: no hits), so it is hash-exempt.
  - `std::uniform_real_distribution` output is implementation-defined across standard libraries.
  - I searched src and plugins for mt19937, rand, uniform_*, minstd, pcg, xorshift, splitmix and Random. This is the only one.
- **Remainder.** A seeded stream service with its state in `StateHash` and snapshots, using its own distribution code. The particle system moves onto it.
- **Weeks:** 0.5-1.5.
- **Proof of determinism** costs "0-0" in the matrix, which means verification only.
  - Measured legs are listed in section 1. The open legs are macOS or Clang, an Android device, and a game's own libm calls inside a tick.
  - The two laptops are both x86-64 (Ryzen 7 250 and i7-12700H, `owner-decisions.md` section 7). They add CPU-model coverage, not a new architecture. The matrix tag "cloud" for this row is wrong for the open legs, which need a Mac or an Android device.
- **UPG:** Yes, the hash moves.

**9. Window, cursor, display (platforms_xr, input).**
- **Built (seen).**
  - `CursorMode::Normal`, `Hidden` and `Locked` exist (`Input.hpp:195-210`).
  - Locked maps to GLFW's disabled cursor, with unscaled motion (`InputPolling.cpp:31-41, 122`).
  - ABI v9 has `setCursorMode`, `cursorMode` and `mouseDelta` (`ScriptPluginApi.h:90-103`).
  - `CameraSystem.cpp:63-70` treats a locked pointer as the look.
  - A sample `MouseLookScript` is registered (`SampleScripts.cpp:189, 275`).
  - **Mouse capture is done.** The first draft listed it as the shooter's first blocker, and that was wrong.
- **Remainder.**
  - Pointer confinement as a fourth mode. `Input.hpp:195-201` and `InputPolling.cpp:31-33` document that GLFW's captured-cursor mode was deliberately not mapped to Locked.
  - Present mode and frame limiter. `VulkanSwapchain.cpp:64-72` returns Mailbox if offered, else Fifo, with no user choice.
  - Window and resolution management, and multi-touch (verify only). Multi-touch contacts are (r) in `Input.hpp:148-160`.
- **Weeks:** 1.3-3.1 (was 1.6-3.8, with the 0.3-0.7 capture row removed). **UPG:** Additive. **Verify:** local-pc, because it needs a real display.

### 2B. Input, audio, UI

**10. Audio voice, spatial and listener core (audio).**
- **Built.**
  - Inverse-distance attenuation with `referenceDistance` 1.5 and `maxDistance` 40 (`Components.hpp:1647-1662`, seen).
  - Pan from the listener's right vector (`AudioSystem.cpp:153`, seen).
  - An `AudioListenerComponent{isPrimary}` exists (`Components.hpp:1664`). The listener pose comes from the first `CameraComponent`, not from that entity (`AudioSystem.cpp:85-96`, seen).
- **Remainder.** Listener from an entity, spatial blend, rolloff curves and voice management (matrix rows).
- **Weeks:** 1.5-4 (was 3-7.5).
- **UPG:** Yes if the default attenuation or listener source changes what existing games hear.
- **Verify:** cloud for mixing. Whether it sounds right needs ears. AGENTS.md says ALSA headers decide real audio or a no-op, and the cloud has no way to listen.

**11. Gameplay audio API, transport and formats (audio, assets_build).**
- **Built.** `Mp3Decode.cpp`, `StbVorbisImplementation.cpp`, `DrMp3Implementation.cpp` and `AudioMixer.cpp`. The ABI has no audio calls (`play` at `ScriptPluginApi.h:144` is animation).
- **Remainder.**
  - Play, stop and volume from scripts and events.
  - Pause, seek, fades and finished events, which need a sample-accurate voice clock in the mixer.
  - Formats beyond the current decoders (the assets_build audio-formats row is the same work).
- **Weeks:** 4.5-9 (not re-checked beyond the ABI). **UPG:** Additive.
- **Depends on:** item 10 and the ABI.

**12. Gamepad and input processing (input).**
- **Built (seen).**
  - The `Pad` namespace (`Input.hpp:55`).
  - `BindActionPadButton` (`:238`) and `BindAxisPad` with scale (`:242`).
  - `IsGamepadConnected` (`:460`).
  - A fixed `kStickDeadzone` of 0.18 (`:582`).
  - `platform/Gamepads.cpp`.
  - The first pad only (comment at `InputPolling.cpp:212`).
  - Scripts read through named actions and axes (`ScriptPluginApi.h:81`).
- **Not found.** rumble, haptic.
- **Remainder.** Rumble, configurable deadzone and curves, more than one pad, an authored input-map asset with editor UI, and rebinding.
- **Weeks:** 3-6 (was 5-9). **UPG:** Likely, if action value types change the recording format. **Verify:** processing maths in cloud, a real pad on the owner's PC.

**13. UI layout and presentation (ui).**
- **Built (seen, `Components.hpp`).**
  - `UIAnchor` on every UI component.
  - Reference-height scaling (`UICanvas.cpp:86` comment).
  - `UIImageComponent` with atlas UVs and tint (`:2292`).
  - `UIStackComponent` (`:2155`), `UIOrderComponent` (`:2182`) and `UIPanelComponent` (`:2354`).
  - `UITextComponent` with align, size, wrap and world-space (`:2203-2270`).
- **Remainder.**
  - Font choice (BitmapFont only).
  - Truncation.
  - DPI.
  - Images from asset paths. The texture is an `ImTextureID` integer (r, `:2299`).
- **Weeks:** 3.5-8 (was 7.5-15). **UPG:** Yes, if the UI file format or layout rules change.
- **Verify:** cloud for layout maths. Fonts need eyes.

**14. UI widgets and integration (ui, services_ecosystem).**
- **Built (seen).**
  - `UIButtonComponent` (`:2417`) and `UITextFieldComponent` (`:2475`).
  - World-anchored shapes and text (`:2108, 2209`).
  - ABI `setText`, `setVisible`, `wasClicked` and `isHovered` (`ScriptPluginApi.h:155-162`). Clicks resolve on the tick since v13.
  - `UIInput.hpp:62` hands each tick "the clicks nothing has consumed yet", so some UI-versus-game arbitration exists. How much is missing is unverified.
- **Remainder.** Toggle and skin states, focus navigation (pad and keyboard), user-adjustable UI scale, and the unverified arbitration remainder. Properties and events from scripts overlap item 4.
- **Weeks:** 3.5-7 (was 6-13). Items 13 and 14 together: 7-15 (was 13.5-28). **UPG:** Additive.

### 2C. Presentation

**15. 2D rendering basics (render2d).**
- **Built.** `sortKey` on renderables is a per-renderable integer (`Components.hpp:1180`, `RenderSystem.cpp:174, 977-986`). Tilemap bake and draw, `Light2D`, sprite animation.
- **Remainder.** A layer or y-sort policy, batching, nine-slice, pixel-art import flags, billboards.
- **Weeks:** 3.3-7.3. **UPG:** Additive if the new order is opt-in, Yes if the default order moves pictures. **Verify:** cloud for logic, eyes for the pixel-art look.

**16. 2D camera (render2d, physics2d).**
- **Built.** Orthographic camera (`Components.hpp:266-336`). `InterpolatedCameraComponent`.
- **Not found.** Follow, dead zone, look-ahead, bounds and shake, in `CameraSystem.cpp` and `Components.hpp`.
- **Weeks:** 2-5. The physics2d camera row is the same work. **UPG:** Additive. **Verify:** eyes, because it is feel.

**17. Hierarchy hints and interpolation residual (core_runtime, physics).**
- **Built (seen).**
  - Render interpolation exists. `InterpolatedTransformComponent` is opt-in (`Components.hpp:612`), with a camera counterpart (`:643`).
  - Rotation uses a per-axis shortest-arc Euler lerp (`InterpolationSystem.cpp:48-51, 150`).
  - The source comment says converting to quaternions and back "would have to invent an Euler decomposition".
- **Remainder.** Static hints on the transform hierarchy (1.5-3 in the matrix) and the half-turn-per-tick limit of a shortest-arc lerp.
- **Quaternion rotation** is no longer an item. See decision 3.
- **Weeks:** 1.5-3 (was 2-4.5). **UPG:** No.

**18. Animation gameplay set (animation).**
- **Built.** Two-clip cross-fade (`AnimatorComponent.blendDuration`, `Components.hpp:1679`, seen).
- **Not found.** Sockets or bone attachment, IK, root motion, tween or easing, animation events. `AnimationClip` holds name, duration and channels only (`Skeleton.hpp:92-96`).
- **Remainder.** Sockets (1-2), tween and easing (1-2, plus an ABI revision).
- **Weeks:** 2-4 (was 3-6). **UPG:** Yes if animation joins the hash. **Verify:** cloud for maths, eyes for the look.
- Animation scores 11%. About 30 rows are not in the digest.

**19. Scene switching with persistent state (world, core_runtime, scripting).**
- **Built.** `loadScene` in the ABI (v8, `ScriptPluginApi.h:261`). `SceneManager.cpp`.
- **Remainder.**
  - State carried across loads, and async and additive loading (P4).
  - A replay is scoped to ONE scene (`README.md:1352`, seen). The hash excludes `registry.ctx()` (`:1362`). Multi-scene replay and hashing of persisted state need a decision.
- **Weeks:** 2.5-5. **UPG:** Yes, because scene-manager semantics change.

**20. Save-game and player settings (core_runtime, services_ecosystem).**
- **Built.** A per-user data directory resolver (`ExecutablePath.hpp:164`, `test_userdata.cpp`).
- **Remainder.**
  - Save-slot serialisation, which depends on stable references (P2) and scene switching (item 19).
  - Settings persistence for video, audio and controls.
  - UI sliders, toggles and dropdowns.
  - An audio master gain.
- **Weeks:** 3-6. **UPG:** Additive.

**21. Post-stack, HDR, bloom, AA, IBL (render3d, vfx).**
- **Built.** `BloomPass.cpp` with `bloom_bright`, `bloom_blur` and `bloom_composite` shaders (seen). The roadmap lists a 4x MSAA RGBA16F scene target and CPU-convolved IBL in `EnvironmentMap.cpp` (r).
- **Remainder.** Anti-aliasing beyond MSAA (FXAA or SMAA), an asynchronous IBL path, and a tone-map and encode review.
- **Prerequisites,** from `roadmap.md` (seen):
  - F3, a trusted shader build path (1.5-3 weeks, PC).
  - F8, the GPU-pass harness (0.5-1, cloud).
  - F9, the post-stack refactor (1.2-2.5).
  - F13, the fixture suite (1-2, cloud).
  - Together 4.2-8.5 weeks. They sit inside roadmap path A, so do not add them again.
- **Weeks:** 7.7-15.5 including those prerequisites.
- **UPG:** Yes. Pictures and `.spv` blobs move. **Verify:** both laptops and eyes.
- **The large rendering lane** is `roadmap.md`, not re-derived:
  - Path A: about 52-105 weeks.
  - Path B: 27-59, or 15-34 on top of A.
  - Path C: 60-118, or 38-73 on top of A and B.
  - Union: about 105-212.
  - All of these are guesses.

**22. Debug draw and collider visualisation (tooling, physics, world).**
- **Built (seen).**
  - `WorldShapes` is an immediate-mode, depth-tested line renderer in world units, cleared each frame.
  - It provides `AddLine`, `AddCircle`, `AddGroundRect` and `AddBox`.
  - It has a Vulkan line pipeline with its own shaders (`VulkanRenderer.cpp:592-625, 2112-2137`) and is registered in the registry context (`SupersonicApp.cpp:551`).
  - Its first consumer is `NavSystem::Draw` (`NavSystem.cpp:156`).
  - The first draft priced a debug-draw API from zero.
- **Not found.** Nothing emits colliders, joints or contacts into `WorldShapes`. Its users are `NavSystem`, `SupersonicApp` and the renderer only.
- **Remainder.**
  - 22a: emit colliders, joints and contacts into `WorldShapes`, add sphere and capsule shapes, an editor toggle and an ABI call (1-2 weeks).
  - 22b: in-viewport collider and trigger gizmos and 2D collider editing (2-4).
- **Weeks:** 3-6 (was 7.5-14 raw, 4-8 shared). **UPG:** No. **Verify:** cloud for the draw list, eyes for the look.

**23. Editor placement tools (editor, world).**
- **Remainder (r).** Gizmos call `ImGuizmo::Manipulate` with no snap array (`InspectorPanel.cpp:2153-2159`). Viewport navigation, grid, rotation and surface snapping, and pivot modes remain.
- **Weeks:** 3.5-7.5. **Depends on:** `PhysicsSystem::Raycast` for surface snap, and a selection model. **UPG:** No. **Verify:** local-pc and eyes.

### 2D. Genre-specific

**24. Navigation (ai_nav).**
- **Built (seen).**
  - `NavGrid.hpp` is an integer-cost grid with a multi-source flow field (`FlowAt`, `:135`). Cost 0 is a wall, so walls and weighted cells exist.
  - `NavSystem::StampColliders` stamps colliders into it (`NavSystem.hpp:45`).
  - `NavSystem::Draw` visualises it.
  - The header explains why a flow field and not A* per agent.
  - It also says a navmesh is not vendored on determinism grounds. `README.md` and the roadmap refuse Detour for the same reason (r).
- **Remainder.** A* for single paths, path smoothing, and the hidden rows (crowd steering, formations, section 4).
- **Weeks:** 1-2 for A* (was 2-4 for A* plus the grid). **UPG:** Additive.

**25. Terrain (world).**
- **Built.** `HeightfieldColliderComponent` exists (`Components.hpp:1623`), and so do `Heightfield.cpp` and `HeightfieldCache.cpp`. The terrain primitive is a fixed 64x64 generated surface (`TerrainGenerator.hpp:19-20`).
- **Remainder.** The heightmap data asset, which is roadmap Q19 at 2-4 weeks (seen). The collider needs bit-exact decode and a cache key that hashes heights. Splat layers (Q20) are another 3-6.
- **Weeks:** 2.5-5.5 (was 0.5-1.5). **UPG:** Additive.

**26. Networking (networking).**
- **Built.** There is no networking code. I searched for sockaddr, WSAStartup, winsock2, getaddrinfo, recvfrom, sendto, enet_host_create, lockstep and rollback. The only "lockstep" hits are camera comments. There is no no-GPU server mode: the replay check needs a Vulkan device (AGENTS.md).
- **Remainder.** Network time and input buffering (1-2), bad-link simulation (1-2), and a transport with reliable ordered messaging (P3, editor guess 4-8).
- **Beyond that, hidden.** Lockstep input exchange and desync handling, rollback with a full-state snapshot including scripts and RNG, prediction and reconciliation, lag compensation, a dedicated server, session and lobby. The owner's decisions record names lockstep for strategy and rollback for platformers (section 3).
- **Weeks:** 6-12 visible. **UPG:** Additive. **Verify:** cloud on loopback with simulated bad links. Real internet or NAT needs the owner.

**27. Feedback and juice (vfx, audio).**
- **Remainder.**
  - Effect spawning and lifecycle with pooling (0.5-1.5).
  - Random and variation containers (1.5-3).
  - Audio from animation events (1-2).
  - Emitter shapes, bursts and play control (P6).
  - Particles are CPU-simulated.
- **Weeks:** 3-6.5. **UPG:** Additive.
- **Depends on:** items 11 and 18.

### 2E. Tooling, pipeline, ship

**28. Performance guardrails (tooling).**
- **Built.** `PerfOverlay.cpp`, `FramePerf.cpp`, `Profiler.hpp` (CPU zones). `tools/bench` has two benchmarks. GPU timestamps are absent (r: `Profiler.hpp:14-15`).
- **Remainder.**
  - GPU pass timing (0.8-2).
  - Physics performance statistics.
  - GPU capture labels. The debug-utils extension is enabled only for the validation messenger (r, `VulkanContext.cpp`).
  - The speed suite itself (section 7).
- **Weeks:** 2-6 (was 3.3-8). **UPG:** No. **Verify:** counts in cloud, GPU timings on both laptops.

**29. Console, logging, assertions (editor, tooling).**
- **Built (seen).**
  - `Log.hpp`: levels, categories, a bounded ring buffer with a dropped count, console and file sinks, and elapsed timestamps.
  - A Console panel (`EditorLayer.cpp:1112-1118`).
- **Remainder.** Log rotation and retention, and assertion and ensure macros (r: none found).
- **Weeks:** 1-2.5 (was 2.3-5.8). **UPG:** No. **Verify:** local-pc for the editor console.

**30. Hot reload of gameplay code (scripting, tooling).**
- **Built.** `HotReloadEngine.cpp`, `tests/test_hotreload.cpp` and `hotreload_stub.cpp`.
- **Remainder.** Per the matrix (not re-checked). A known Windows issue is a non-ANSI project folder (r, README).
- **Weeks:** 2-5. **Verify:** the load logic is largely cloud-testable. Windows-specific behaviour needs MSVC on the owner's PC. **UPG:** Maybe, if the plugin ABI moves.

**31. Asset hot reload (assets_build, editor).**
- **Built.** `AssetWatcher.cpp`.
- **Weeks:** 1.5-3. **Verify:** cloud. **UPG:** No.

**32. Physics project settings (physics2d).**
- **Built.** `PhysicsSettings.hpp` exists. It already carries `sortedContactExits` per the `ContactTracker` comment.
- **Remainder.** Iterations, slop, sleep, tick and default material.
- **Weeks:** 1-1.5. **UPG:** Additive.

**33. Export and notices (assets_build, services_ecosystem).**
- **Remainder.** Command-line and CI export (1-3), and third-party notices in shipped games (0.5-1). They share nothing and are tracked separately. Export presets (P5) are uncosted.
- **Weeks:** 1.5-4. **UPG:** Additive.
- **Verify, split.**
  - Manifest and preset format: cloud.
  - A Windows package: MSVC on the owner's PC.
  - Android and iOS packages: the games' own repositories (AGENTS.md "Other platforms").
  - GitHub Actions minutes are used up, so verification is by local dispatch.

**34. Audio debugging and profiling (audio).**
- **Weeks:** 1-2. **Depends on:** voice management and mixer buses. **Verify:** cloud. **UPG:** No.

### 2F. Prerequisites that were uncosted

These gate top-ranked items. The bands are **editor guesses** unless noted.

| Id | What | Gates | Band |
|---|---|---|---|
| P1 | Shape casts (sphere, capsule, box sweeps). No hits for shapecast, spherecast, capsulecast, boxcast, sweepTest or ShapeSweep. | 2, 3 | 2-4 |
| P2 | Stable entity ids and references (the keystone for 5, 6, 19, 20) | 5, 6, 19, 20 | 2-4 |
| P3 | Network transport and reliable ordered messaging | 26 | 4-8 (inside item 26) |
| P4 | Async and additive scene loading | 19 | 1.5-3 |
| P5 | Export presets | 33 | 1-2 |
| P6 | Emitter shapes, bursts, play control | 27 | 1-2.5 |
| P7 | Heightmap terrain asset | 25 | roadmap Q19, 2-4 (inside item 25) |

### 2G. Rows the digest did not show, checked against the code

I searched src, plugins and CMake with the terms below. An absence here rests on that term list, not on a full read.

- **Tilemap collision.** Absent, by design. `README.md:1333`: "it has no collision… the solver is a 3D one, and no 2D game here has asked to walk on tiles". `PhysicsSystem` and `NavSystem` do not reference tilemap. This is a platformer blocker that the first draft omitted. Size: medium.
- **Tile authoring and import.** tmx, ldtk and autotile: no hits.
- **One-way platforms, moving-platform carry, parallax, pixel-perfect or integer-scale camera.** one-way, parallax, pixel-perfect, moving platform: no relevant hits. The one "one-way" hit is a comment in `PhysicsSystem.cpp`.
- **3D character controller.** No hits (see item 2).
- **Swept or continuous collision.** `ARCHITECTURE.md:2234`: "No swept collision detection." Mitigated by speculative contacts only.
- **Multi-camera and render-to-texture** (view model, minimap, split-screen, portals). RenderTexture, renderTarget, cullingMask and splitscreen: no relevant hits. Rendering a second view needs a split of `DrawFrame` (r, roadmap).
- **Crowd steering, avoidance, formations, behaviour AI, fog of war.** steering, boid, RVO, ORCA, formation, behaviour tree, blackboard and fog of war: no hits.
- **Embedded scripting language.** lua, luajit, wasm, wasmtime, quickjs, angelscript as whole words: no hits. The owner's decisions record says "the likely answer is an embedded scripting layer on top of C++ (Lua, C# or WASM)" (`owner-decisions.md:180-181`, seen). That is a capability gap with no item until now. Size: large.
- **IK, root motion, sockets, animation events, tween.** None found (see item 18).
- **Decals.** No component. Only comments mention them.
- **Localisation.** localis, localiz, i18n and StringTable: no hits.
- **Compressed textures.** ASTC, BC7, KTX, ETC2 and basisu: no hits. The games run on Android and Apple (README, AGENTS.md).
- **Time scale, camera shake, rumble.** None found (items 7, 16, 12).

---

## Totals

| Part | Person-weeks |
|---|---|
| The 34 items, revised | about 87-188 |
| Uncosted prerequisites P1, P2, P4, P5, P6 (editor guesses) | 7.5-15.5 |
| **Visible total (raw, not de-shared)** | **about 94-203** |

- The first draft's 110-230 was the sum of unrevised matrix ranges.
- The visible total **excludes**:
  - the rendering lane beyond item 21 (roadmap, above);
  - the hidden rows in 2G and section 4;
  - owner sessions, which the roadmap says set the elapsed time for pictures and GPU work.
- Item 21's F3, F8, F9 and F13 sit inside roadmap path A. Do not add the two figures.
- Treat the total as order of magnitude.

---

## 3. Sequencing

**Format batching.**
- Several items touch the scene, prefab or UI codec: layers and ABI (1), prefab hierarchy (5), stable ids (6), UI (13).
- Group these into the same wave so the parallel lanes do not collide in `ComponentCodec.cpp` and `SceneSerializer.cpp`.
- UPGRADING.md rule 1 still requires an entry in the same commit as each change. So: one entry per commit, plus an optional wave summary.
- Quaternion rotation is not scheduled (decision 3).

All wave sums below are raw and reconcile to the totals above.

**Wave 0, foundations: 17-37 weeks.**
- **Lane A (cloud):**
  - native seam (4);
  - identity and events (6);
  - stable entity references (P2);
  - time control (7);
  - RNG (8);
  - debug draw emission (22a).
- **Lane B (owner PC):**
  - window, cursor and present mode (9);
  - F3, the trusted shader build path (PC);
  - GPU timers (28).
- **Cloud, not PC:** F8 and F13 are cloud in `roadmap.md` (lines 248 and 271), so they sit in lane A. Only F3 needs the owner's PC.
- **Unlocks:** the performance alarm, and every row that says it depends on script API breadth.
- **No longer unlocked here:** mouse look, which already exists.

**Wave 1, gameplay physics and entities: 16-35 weeks, cloud.**
- Lane A:
  - layers, triggers and events (1);
  - shape casts (P1);
  - controller (2);
  - queries (3);
  - settings (32).
- Lane B:
  - prefab hierarchy and activation (5);
  - collider gizmos (22b);
  - tilemap collision (2G).
- **Depends on:** wave 0 lane A.

**Wave 2, audio, UI and input: 16-34 weeks.**
- Audio (10, 11): cloud.
- UI (13, then 14): cloud, with eyes for fonts.
- Input (12): needs the owner's PC for a real pad.

**Wave 3, feel and presentation: 17-38 weeks.**
- Lane A: 2D rendering (15), 2D camera (16), hierarchy hints (17).
- Lane B: animation (18), juice (27) and emitter control (P6).
- Lane C (both laptops): the remainder of item 21 after F3, F8 and F13.
- **Depends on:** wave 0 lane B and wave 1.

**Wave 4, game structure: 12-25 weeks.**
- Scene switching (19), async and additive loading (P4), save and settings (20), editor placement tools (23), asset hot reload (31).
- **Depends on:** stable references (P2), UI widgets (14) and audio (10).

**Wave 5, strategy and shooter specifics: 10-20 weeks visible.**
- Navigation (24), terrain (25), networking (26).
- Plus the hidden rows in 2G and section 4, which are not costed.

**Lane T, any time: 6.5-15.5 weeks.**
- Console and logging (29), hot reload (30), export and export presets (33, P5), audio debug (34).

**Constraints.**
- The owner's PC and laptops are one shared resource. Items tagged local-pc or device-or-eyes (9, 12, 21, 23, 28 GPU part, 30 on Windows) compete for the same sessions.
- Shader blobs are built only there, batched per wave (owner-decisions section 7).
- Owner sessions, not coding, set the elapsed time for waves 0, 2 and 3.

---

## 4. Genre view

**Test-bed games.** The live games are Penumbra and Magic Portals. Wolf Brigade and HUSK are test beds (`owner-decisions.md` section 7). Magic Portals is the one live 2D consumer: a 2D game on the 3D solver with axis locks. I did not look at the games' repositories, so what their code already proves is unchecked.

**Visible cost per genre** (revised, raw, items only):

| Genre | Visible weeks | Items counted |
|---|---|---|
| 2D platformer | 39-85 | 2, 1, 15, 16, 17, 10, 11, 13, 14, 12, 7, 5, 20, tween |
| 3D strategy | 28-59, or 34-72 with audio (10, 11) | 8, 13, 14, 5, confinement part of 9, 24, 25, 26, 23, 19, 20 |
| 3D shooter | 44-94 | 2, 1, 3, 10, 11, 18, 27, 21, 12, 26, 13, 14, plus the roadmap lane |

**These ranges overlap, so visible cost does not pick a genre.** The hidden rows below decide strategy and shooter, and I cannot cost them. The first draft's "platformer has the shortest verified path" is withdrawn. Nothing was verified, and the digest favours cheap rows.

**2D platformer**
- **Built.**
  - Tilemap rendering with no collision.
  - Sprite animation and `Light2D`.
  - An orthographic camera with interpolation.
  - Per-axis locks on the 3D solver.
  - Layers, triggers and contact events.
  - Pad bindings, UI components, replay.
- **Blockers, in order:**
  1. Tilemap collision, and one-way and moving platforms (2G).
  2. Kinematic-versus-static pairs (item 1) and the controller (item 2).
  3. 2D sorting and rendering basics (15).
  4. 2D camera (16).
  5. Audio core and API (10, 11).
  6. UI gaps (13, 14).
  7. Pad extras and rumble (12).
  8. Time control for hit-stop (7).
  9. Prefab and spawn (5).
  10. Save and settings (20).
  11. Tween (18).
- **Removed from the first draft's list:** render interpolation, which is built.

**3D strategy**
- **Built.**
  - Bit-exact simulation, with state hash, recording and replay, including the pointer.
  - `NavGrid` flow field with weights, stamping and a visualiser.
  - `WorldShapes` for range rings and selection indicators.
  - A heightfield collider. Instancing is done per the roadmap (r).
- **Blockers, in order:**
  1. RNG (8).
  2. UI foundation, widgets and input arbitration (13, 14).
  3. Prefab and spawn at scale (5).
  4. Pointer confinement (9).
  5. A* (24).
  6. The terrain asset (25).
  7. Networking time, input buffering and transport (26).
  8. Editor snapping (23).
  9. Scene switching and save (19, 20).
- **Hidden rows that probably decide the genre,** all absent in the code check: crowd steering and avoidance, formations, unit AI primitives, fog of war, and a selection and command layer.
  - The games implement their own selection in game code. Whether the engine should own one is a design decision.
- **Scale limits (r, from `roadmap.md` and `ARCHITECTURE.md:2259-2290`).**
  - The solver has no persistent broadphase (seen).
  - A skinned entity past the joint-palette cap draws unskinned (the fallback `paletteBase = -1` is seen, `AnimationSystem.cpp:522`). The 1,024 figure is not re-read.
  - The owner's decisions name parallel simulation and an opt-in Fast mode as open (section 3).

**3D shooter**
- **Built.**
  - Mouse look with a locked pointer, and a sample script.
  - A 3D raycast with layer masks.
  - Layers, triggers and contact events.
  - Hull colliders and joints.
  - Two-clip cross-fade and skinning.
  - HDR bloom and MSAA.
- **Blockers, in order:**
  1. The 3D controller and kinematic driving (2).
  2. Kinematic-versus-static pairs, ABI layer masks and contact data (1).
  3. Query breadth and shape casts (3, P1).
  4. Audio (10, 11).
  5. Sockets (18).
  6. Effect lifecycle (27).
  7. AA and IBL, then the roadmap lane (21).
  8. Pad and rumble (12).
  9. Networking prediction (26).
  10. HUD (13, 14).
- **Hidden rows,** all absent in the code check:
  - swept or continuous collision for projectiles;
  - multi-camera for a view model;
  - decals;
  - IK and root motion;
  - textured or ribbon particles for muzzle flash and tracers;
  - recoil and camera shake.
- **I did not map** the roadmap's rendering items (AO, decals, LOD, occlusion) to the genres. Which of Path A's 52-105 weeks a shooter or strategy game can skip is undecided.

**Which game first.** The data does not decide it (see above). What would settle it:
1. the full 633 rows, or every row that scores 3 for any of your genres, re-ranked by genre value instead of value-per-cost;
2. a cost band for the hidden rows;
3. your own preference between rendering depth (shooter) and navigation and networking depth (strategy).

---

## 5. The universal long tail

**After the genre needs are met:**
- Work down the matrix in the order "full in all three competitors, low cost". I cannot rank rows below the digest's cut.
- Keep the replay and determinism strength as the engine's identity. Be exact about it: measured on x64 Windows, x64 Linux and arm64 Linux, not yet on macOS, Clang or Android.
- Keep lit 2D and per-game feature tiers (`roadmap.md` section 5). Its claims against Godot and Unity are marked unchecked there.
- Gaps that belong to no listed domain, and are absent in the code check: cinematics and timeline, localisation, accessibility, multi-camera and render-to-texture, behaviour AI, compressed textures, mesh LOD and optimisation, an embedded scripting language, visual scripting, mod support.
- XR and further platform work. platforms_xr is at 32%.
- Native audio, UI and animation depth beyond the first pass: more formats, widgets, graph features.

**What to deliberately not chase, with reasons:**
- **Nanite-class geometry** (roadmap guess 60-130 weeks), **Lumen-class GI** (50-100+) and **virtual shadow maps** (12-30). The engine has no compute, no bindless and no frame graph, so these are effectively a second renderer.
- **Voxel or SDF GI, mesh shaders, a node-based VFX graph and SSGI** are poor fits (roadmap 16-30, 10-18, 8-20 and 10-16 weeks).
- **DLSS and XeSS in the engine.** They are proprietary SDKs, and the engine is MIT-0 (per the roadmap). At most they are out-of-tree plugins after reading the redistribution terms.
- **Console access.** SDKs are under NDA and need certification and a platform account. I cannot code or verify that from here.
- **A navmesh library.** It conflicts with the determinism decision. The repo has already refused Detour on that ground. If wanted, a determinism proof comes first (decision 8).
- **Ecosystem** (asset store, community, marketplace) cannot be coded. The mitigation is open import formats and permissive licences.
- **Content** (sample games, templates, art, the roadmap's F14 3D showcase scene) is work but not engine code.
- **Full Forward+ or URP parity.** The roadmap puts it at A plus 31-62 weeks, or plus 57-110 with GI. All guesses.

---

## 6. What this analysis could not establish

- **Competitor claims are recollection.** No per-row confidence flag reached me, and I made no web checks. The owner's decisions doc and the roadmap's own lists of what Godot 4.7 and Unity 6 ship are also unchecked.
- **I saw about 120 of 633 rows**, cut by value-per-cost. Rows below the cut are invisible. High-cost rows rank low by ratio, so the expensive rows that decide strategy and shooters may be exactly the unseen ones. The hidden-row list in 2G is from a code search, not from the matrix.
- **Code verification covers the items listed, not the whole matrix.**
  - I opened the lines marked "seen" in section 2.
  - Items 4, 11, 13 (beyond component structs), 14, 20, 30-34, and the matrix rows below the digest, are not re-checked.
  - Items with (r) came from reviewers.
  - Where the matrix and the code disagree, the code wins in this document. The matrix cells themselves were not edited.
- **Costs are guesses.**
  - The revised ranges are editor estimates scaled from the matrix, not measurements.
  - The roadmap says none of its ranges are validated against this repo's history.
  - Sub-week digits are bookkeeping.
  - "0-0" rows mean verification only, not zero effort.
  - The P-band numbers are guesses.
- **Stale documents.** Dated planning documents, README roadmap sentences and `ARCHITECTURE.md` text can lag the code. The 22 August gap audit is stale on joints and hulls, mouse capture, cross-fade and UI images. `README.md:1404` is stale on arm64. Treat them as unverified wherever the code could answer.
- **Error rate.** The re-check changed 35% of rows. Some unchecked errors probably remain.
- **Not looked at.** The games' own repositories, so what Penumbra, Wolf Brigade, HUSK and Magic Portals already prove about selection, commands or 2D needs is unchecked. Penumbra's genre is not in this repo.
- **Verification tags.** The cross-platform determinism tag "cloud" is wrong for the unmeasured legs (section 2, item 8).

---

## 7. Decisions for the owner

**The two terms.**
- **Deterministic** means the same inputs give the same result, bit for bit, on every machine.
  - In a strategy game, each player's computer can run the whole game from just the commands.
  - It is also what makes a recorded replay play back identically.
  - The rule the engine commits to: no simulation result may depend on thread timing, the wall clock, uninitialised memory or a platform's maths library. Rendering is outside it (`owner-decisions.md` section 3).
  - Measured on x64 Windows, x64 Linux and arm64 Linux. macOS, Clang and an Android device have not been run.
- **Speed suite** is a small set of fixed scenes (for example thousands of cubes, a thousand physics bodies, a few hundred animated characters, the lit 2D path). It asserts counts (draws, entities walked, allocations per frame) and only records milliseconds, with a baseline per laptop. It is a smoke alarm while features are added (`owner-decisions.md` section 4). `tools/bench` holds two benchmarks today (`physics_bench`, `dedupe_bench`). The suite is not started.

**Decisions, with a recommendation each.**

1. **What is the main gameplay surface?** This is a three-way choice, not a binary one.
   - (a) Native C++ only.
   - (b) The C++ plugin ABI.
   - (c) An embedded language.
   - The decisions record already says an embedded layer (Lua, C# or WASM) is the likely answer to the scripting gap (`:180-181`). No code exists for it.
   - A VM would need its state in the hash, no clock and no libm.
   - The ABI reached v13 after three input rescopings.
   - Recommendation: native first for strategy. Narrow the ABI work to items 4 and 11-14. Settle the ABI before starting an embedded language.
2. **2D physics: one solver with a good controller, or a separate 2D solver?** The matrix costs them separately. Recommendation: keep one, revisit if platformer feel or performance demands it.
3. **Quaternion rotation.** Withdrawn from the plan. Interpolation already exists and handles Euler wrap, and the Euler choice was deliberate. A quaternion change would move scenes, prefabs, hashes and goldens, and needs a determinism migration, not a row. Take it only if a game asks, and cost it then.
4. **Hash and format changes.** Batch them per wave, with one UPGRADING.md entry per commit and a wave summary.
5. **Hours per week, and owner sessions.** The roadmap asks the same. Owner sessions set the elapsed time for waves 0, 2 and 3.
6. **Which game first.** No recommendation from this data (section 4).
7. **Standing approval for batched shader-blob sessions** (`frag.spv` changes), as agreed once for `1b99158`. Without it, item 21 stalls.
8. **Third-party libraries.** FLAC or Opus decoders and a font stack are acceptable if MIT-0 compatible and listed in the notices (item 33). A navmesh library needs a determinism proof first.
9. **A weighted target in place of the unweighted percentage.** For example, every row that scores 3 for any of your three genres reaches "full".
10. **Check competitor claims against the official Godot 4.7.x and Unity 6.x release notes?** Until then the competitor columns are recollection.
11. **What counts as "professional".** A shipped PC game, a store build, a phone build? It decides whether items 21, 28 and 33 are wave-critical or long tail.
12. **Which network model per genre** (lockstep, rollback, authoritative server with prediction). It sets what item 26 costs and whether platformers need networking.
13. **Fast mode versus deterministic parallel simulation** (open in `owner-decisions.md` section 3). It governs unit counts for strategy and physics for shooters.
14. **How game UI is drawn.** The ImGui pass draws every UI component (AGENTS.md), so fonts, effects and mobile behaviour are bounded by ImGui. Decide whether that bound is acceptable.
15. **Mobile asset pipeline.** No compressed texture formats exist. The games run on Android and Apple.
16. **Is an in-editor tile or level editor in scope** for platformers and strategy, and is the platformer pixel-art or HD-2D?

---

## What changed after review

**Fixed (verified against the code unless marked).**
- **Items built but priced as unbuilt, now re-costed from the remainder only.**
  - Item 1: layers, masks, triggers, contact events, query filters and the codec fields exist. 7.5-15.5 became 2-5 (editor estimate). The false UPG note about codec fields is gone.
  - Item 9: mouse capture is built. It is removed from the shooter's blocker list and from "wave 0 unlocks mouse look". Confinement, present mode and frame limiter remain.
  - Item 17: render interpolation exists. Quaternion rotation was removed as an item (decision 3).
  - Items 5, 10, 12, 13, 14, 15, 18, 19, 22, 24, 25, 28, 29: reduced or re-scoped. Each now has Built and Remainder lines.
  - Item 22: `WorldShapes` is the existing debug-line renderer. The remainder is emission, not a debug-draw API.
  - Item 24: the grid, weights, flow field and stamping exist. The remainder is A* and the hidden rows.
- **Items re-costed upward.** Item 25 (terrain, now with the heightmap asset, roadmap Q19) and item 26 (networking transport, P3) were underpriced.
- **Platformer-first recommendation withdrawn.** It rested on a cheap-biased cut and the word "verified" was unwarranted. Section 4 now has a per-genre table, and the three visible ranges overlap.
- **Totals and waves.**
  - The headline is relabelled "visible items only", at about 94-203 with prerequisites.
  - Wave sums are raw and reconcile to the items.
  - The F3, F8 and F13 double-count between wave 0 and item 21 is removed.
  - Wave 0 is 17-37 (the first draft said 11-24).
- **Determinism.**
  - The measured arm64 Linux pass (CI run 43101171042) is recorded. The stale "arm64 not run" lines are called out.
  - The "two rows ahead" claim is now "at least two".
  - Owner-decisions section 7 is quoted as a plan, not a result.
  - The open legs are named (macOS or Clang, Android, a game's own libm).
  - The scoring inconsistency across physics3d, physics2d and core_runtime is flagged.
  - The hidden, non-reseeded, hash-exempt particle RNG is named in item 8.
- **Competitor claims.**
  - Labelled unverified recollection beside the headline numbers.
  - The "smaller job" inference about ai_nav and tooling is removed.
  - The domain table is presented as tiers.
- **Missing capabilities added** (all checked with search terms listed in 2G):
  - tilemap collision;
  - tile import;
  - one-way and moving platforms;
  - parallax and pixel-perfect camera;
  - the 3D controller;
  - swept collision;
  - multi-camera and render-to-texture;
  - crowd steering and behaviour AI;
  - fog of war;
  - an embedded scripting language;
  - IK, root motion, sockets, animation events and tween;
  - decals;
  - localisation;
  - compressed textures.
- **Prerequisites promoted** to a table (P1-P7) with editor-guess bands, flagged as guesses.
- **Smaller fixes.**
  - Wave 0's F8 and F13 moved to the cloud lane (`roadmap.md` tags).
  - Item 33's verify tag is split.
  - Item 30's verify tag is corrected.
  - The "50 to 75" gap is corrected to "36 to 75".
  - "None missing" is now "19 of 19 surveys returned".
  - The quaternion decision is removed from wave 0.
  - The batching-per-wave rule is reconciled with UPGRADING.md rule 1.
  - Decisions 12-16 are added, and decision 1 is now a three-way choice.
  - A legend and glossary are added, and the plain answers moved to the top.
  - The scoring notes on physics materials and joints are added.
  - The duplicate-counting caveat is added.

**Rejected or only partly taken, and why.**
- **"Ask the matrix owner for the remainder text and the full 633 rows."** I cannot ask from here. I did code reads instead and turned the request into a decision and an open item (section 4, "Which game first").
- **A value-only ordering beside value/cost.** I have no value data.
- **Mapping every roadmap rendering row to the three genres.** Not done. It is stated as open in section 4.
- **Splitting every compound item.** I split where cost or format risk differs (9, 17, 22, 33, and P1-P7). Items 4, 12, 14, 21 and 26 stay compound, with their sub-rows named inside Built and Remainder, to avoid a 60-item list.
- **Using the games' repos for selection and command evidence.** Out of scope for this task. I noted it as unchecked.
- **The Windows CI caveat** (Debug leg only, `test_device` skipped). Not material to any ranking here.
- **The matrix's own evidence text** (for example the "impulse has zero hits" term) was not in my input, so I could not correct it. My own search found no camera shake or rumble terms. The one hit set for "vibrat" is unrelated joint comments.

**Still unverified.**
- All competitor cells.
- The value-ranked order of the 633 rows, and every row below the digest's cut.
- The code state of items 4, 11, 20, 30-34 and the rest of the matrix.
- Everything marked (r).
- The exact size of the UI-versus-game input arbitration remainder.
- The size of all hidden rows, and the editor-guess bands (P1-P7 and the revised ranges).
- Whether the owner's two laptops, the games' repositories or a Mac would change any ranking.
- macOS, Clang and Android determinism.