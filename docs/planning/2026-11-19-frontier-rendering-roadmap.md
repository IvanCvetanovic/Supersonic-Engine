> **Written before two decisions of 19 November** ([owner-decisions](2026-11-19-owner-decisions.md)):
> that a local agent on the owner's PC does the work tagged PC (so this document's
> "owner sessions" overstate what the owner must attend), and that breaking changes are
> allowed and recorded in [UPGRADING.md](../../UPGRADING.md) (so its opt-in framing is a
> choice, not a constraint). Its competitor feature lists are recollection, unchecked.
> The costs are guesses. It is the rendering lane of
> [the capability matrix](2026-11-19-capability-matrix.md).

# Supersonic Engine: roadmap toward frontier rendering (edited after review)

Prepared 19 November 2026 from nine area surveys, an independent verification of each, a structural-prerequisites report, and three adversarial reviews. I re-checked the reviewers' repo claims that carry a decision. Where a reviewer's claim rests on web search or news coverage that I could not reach, it is labelled unchecked below.

**Unit.** Efforts are person-weeks: forty focused hours of one developer working with an AI assistant, including tests, add-only opt-in plumbing, docs and review. They are not calendar weeks. They do not include waiting for the owner's PC or phone, which is counted separately as owner sessions (section 1). The history has parallel agent sessions: 7 merge commits, some from `claude/*` branches. The repo's calendar speed therefore overstates a single stream.

**Precision.** No range in this document is validated against this repo's history, and nothing was run on a real GPU. Sub-week digits (0.3, 1.2) are bookkeeping from summing verifier ranges, not precision. Headline totals are rounded to whole weeks.

---

## 1. Honest framing

**Bottom line.** "Frontier" is three different purchases with very different prices.

| Meaning | What it covers | Standalone | On top of the others |
|---|---|---|---|
| **A. The look layer of Godot 4 Forward+ / Unity URP on desktop 3D** (not full parity, see below) | Tone-map choice, grading, AO, soft shadows, probes, DoF, exposure, fog, sky | **about 52–105 weeks** | n/a |
| **B. Standing out in lit 2D/2.5D and on phones** | The niche the engine already has | **about 27–59** | 15–34 on top of A |
| **C. A solo-developer frontier slice** | TAA and temporal upscaling, volumetric fog and atmosphere, ray-query shadows, 2D radiance-cascade GI, HDR output, as opt-in desktop tiers | **about 60–118** | 38–73 on top of A and B |

The union of A, B and C is **roughly 105–212 person-weeks** (arithmetic checked: 104.9–212.0). I do not know the owner's weekly hours. At full time that is two to four years, so choose paths, not everything.

Each path is a **dependency closure**. It includes every prerequisite item that its own members name in the dependency column. An earlier draft priced C from its headline items alone and was about 30% low. C's closure adds F0, F3, F5, F6, F7, F8, F9, F10, F12, F13, F14, P1, P3, Q12, Q29 and Q45, about 21–43 weeks.

**Full Forward+/URP feature parity is not Path A.** Both engines ship temporal AA and upscaling, SSR, volumetric fog and some form of GI. All three of those engines' feature lists below are recollection or reviewer web search, unchecked.
- Adding R1, R2, R4, R5 and Q40 costs 26–52 weeks. Their unpriced prerequisites Q29 and P12 add 5–10.
- SDFGI-class GI and lightmaps would add roughly 26–48 more (voxel/SDF GI 16–30, R9 10–18).
- That puts the whole thing at about A plus 31–62, or plus 57–110 with GI. These are guesses.

**Owner sessions** (a sitting on the owner's PC or phone: a pinned-compiler blob build, a real-GPU timing or byte-diff, a tablet run, or a look judgement).
- Counted as one slot per C\*, PC, Ph or E tag in the Verify column, with C\* slots batched two to three shader items per blob session.
- Rough floor: **A about 45–75, B about 20–35, C on top about 20–40.**
- Items whose result is tuned by eye or by device number (P4, P7, Q3, Q9, Q10, R1, R2, R5, R6) should be read at the high end of their week range or above. The repo's own device-gated record is `DynamicResolution`: three commits in eight days after the first tablet numbers (`1b99158` 8 Oct, `5dbfd3a` 9 Oct, `5781518` 16 Oct).
- Elapsed time will be set by these sessions, not by coding.

Most of the A/B/C cost is not the effects. It is plumbing that does not exist yet:
- sampleable depth and a prepass;
- a post stack;
- a shader build the cloud can trust;
- fixture scenes that make "unchanged" provable;
- a descriptor budget (13 of 16 fragment samplers are already used);
- a per-draw data path;
- GPU timers.

**Out of reach for one developer, and why.** The figures here are **unvalidated order-of-magnitude guesses**. They assume this engine as it is: no compute, no bindless, no frame graph, no history to calibrate against.
- **Nanite-class virtualised geometry**, guessed at 60–130 weeks. It needs a cluster DAG, streaming, compute rasterisation and a visibility buffer. In a 4x-MSAA forward renderer with per-set materials that is effectively a second renderer. No hosted game has the triangle counts.
  - A reviewer cites a solo meshlet prototype in another engine taking "several months", but that engine already had compute and bindless (unchecked). A static toy here may be well below my figure. A production-quality one is not a solo target.
- **Lumen-class GI.** The cost is surface-cache and distance-field infrastructure, not hardware: Epic documents software tracing as the default and hardware RT as optional (reviewer, unchecked). Guess 50–100+ weeks.
- **ReSTIR and RT GI**, which need ray-query hardware. Their correctness *can* be exercised on lavapipe, which exposes ray query and acceleration structures. Real-GPU timing cannot. A reviewer reports another engine's solo ReSTIR DI/GI and world-cache work took roughly half a year with compute and bindless in place (unchecked). The R8b price below is the more defensible number.
- **Neural radiance caches** (Müller et al. 2021). AMD's FidelityFX SDK reportedly bundles a radiance-caching sample and a denoiser (2026 coverage, unchecked). That weakens the "you would have to write the denoiser" argument but not the infrastructure one.
- **Virtual shadow maps**, 12–30 weeks. They need stored scene depth, compute, page tables and GPU-driven draws. This engine records one `drawIndexed` per caster on the CPU.
- **Voxel or SDF GI** (Crassin et al. 2011) at 16–30, **mesh shaders** at 10–18, **a node-based VFX graph** at 8–20, and **SSGI** at 10–16. All are poor fits; see the table.
- **DLSS as an engine feature.** DLSS is NVIDIA-RTX-only and proprietary. **XeSS** is a separate case: a reviewer reports a DP4a path that runs on non-Intel GPUs, and Vulkan super resolution is supported (Intel page not opened, unchecked). Both SDKs are proprietary, and the engine is MIT-0. At most they are out-of-tree plugins, and redistribution terms must be read before anything lands in the repo.

**Not as hard as they sound.**
- GTAO, once a depth prepass exists.
- Tone-map operators, grading LUT, vignette, grain and dither. Each is shader-local, under 2 weeks.
- FSR1, which is spatial, MIT-licensed and runs after the composite.
- Specular anti-aliasing, and fog applied to the sky.
- PCSS for the directional light. Cascades are orthographic with a constant depth range, so the blocker maths is simple.
- Baked probes, which reuse the existing probe path.
- Radiance cascades in 2D. They need only fragment passes at low resolution and are deterministic and noise-free.
- Tile light propagation, which needs no shader at all.
- Exposing shadow distance and split lambda. The maths already exists (`ShadowCascades::Build` takes both, with defaults 60.0 and 0.85). `VulkanRenderer.cpp:1673–1674` simply does not pass them.
- Instancing. It is already done.

**Harder than they sound.**
- *Anything that reads scene depth.* The engine discards it (MSAA D32, `storeOp DontCare`). Resolving it needs `createRenderPass2`, which is not used anywhere and is not core on the Vulkan 1.1 floor some games accept. Blended draws live inside the same scene pass, so they cannot read depth resolved after it.
- *Anything in `shader.frag`.* The cloud has no pinned shader compiler. Blobs must come from the owner's PC or CI and are byte-compared. Every edit moves `frag.spv` bytes even when pictures are identical.
  - That file is on the hardening plan's frozen list. The owner has agreed to a `frag.spv` change before (`1b99158`), so it needs the owner's go each time, not a standing bar.
- *Anything needing another sampled texture in the PBR shader.* 13 of the 16 guaranteed samplers are used. Several planned items (P7, Q1, Q3, Q7, Q11, Q38, R5) cannot all fit in the shared layout (see F12).
- *Anything temporal.* There is no jitter, velocity, previous transform or previous skin palette. The scene pass is created once. A velocity attachment means a second render pass and pipeline set (Q29). Tuning ghosting is done by eye on real hardware.
- *Ray queries.* The shader call is trivial. The acceleration-structure manager, device-address and feature plumbing, a SPIR-V 1.4+ target the CMake shader job does not pass, compute skinning for animated meshes and denoising are not. That is why R8a/R8b below cost 5–10 and 6–12.
- *HDR output.* The swapchain accepts only UNORM formats in sRGB-nonlinear, and every UI element is ImGui drawn into that same pass. HDR needs a colour-space extension, a float or 10-bit composite and a UI conversion stage (R15).
- *Judging the look.* Only the owner can.

**Limits of this plan.**
- The code was read, not run on real hardware. No performance number exists for any new effect. The only phone measurement is one Adreno 640 tablet.
- **Calibration.** The git clone is shallow (123 commits from 2026-09-17), so no feature's duration can be measured exactly. Bounding datapoints:
  - The 22 August gap audit calls IBL "a week-plus". The 28 August doc calls instancing "3–4 weeks, not now".
  - Yet the shallow root commit `494f6e2` (17 Sep) already contains `ClusterGrid`, `EnvironmentMap`/`EnvironmentProbe` and `PointShadow`. Those features landed in at most about three weeks, alongside determinism work and game ports.
  - Three opt-in 2D light features landed within about 22 hours: `f30df7c` (27 Sep 22:23, +1127/−118), `4507d99` (28 Sep 14:17, +514/−20), `6cb4036` (28 Sep 20:37, +896/−20).
  - `1b99158` (8 Oct) landed sprite-only pipelines and the dynamic-resolution controller as one commit: 16 files, +629/−6, with tests and a deliberate `frag.spv` change.
- **How I read those datapoints.** CPU-verifiable, oracle-backed work is days. Anything gated on a real GPU, a phone or the owner's eyes is multiple owner sessions. The earlier draft's "budget 2–3x" rule came from the replay-oracle audit (`2026-08-28-competing-with-godot.md:122`, scoped to "anything touching the replay oracle"). It is not a rendering overrun record and is dropped as a multiplier.
- Comparisons with Unity, Unreal and Godot are my recollection (knowledge cutoff June 2026) plus reviewer web searches I could not repeat (godotengine.org is not reachable from the sandbox). They are unchecked. In particular I have not seen Godot 4.7's release notes, though the README mentions 4.7.
- That AO, soft shadows, tone map and grade, fog and probes matter more than materials for a Unity-like or Unreal-like look is **my judgement, not a finding**. It drives the Tier 1 ordering.
- None of the four hosted games consumes most of Tiers 2–3. HUSK is the only game exercising the PBR path. So build one **3D showcase scene** (F14). Without a consumer, no effect can be judged.

---

## 2. Where the engine is today

**Frame structure.**
- `DrawFrame` (`VulkanRenderer.cpp:1422`, about 910 lines) records up to 25 passes: 18 depth passes (4 cascades, 2×6 point faces, 2 spots, skipped by a signature cache), one scene pass, four bloom/composite passes, an overlay and the ImGui swapchain pass.
- The scene pass is one subpass with 4x MSAA RGBA16F colour resolved in-pass (so the resolve averages linear HDR before Reinhard) and an MSAA D32 depth image with `storeOp DontCare` and attachment-only usage (`VulkanOffscreen.cpp:155–164, 252`).
- There is no prepass, normal, velocity, jitter or previous matrix.
- Every scene pipeline is built against that one render pass. The pass is created once and `SetOffscreenRenderPass` runs once (`SupersonicApp.cpp:411`). A viewport resize rebuilds images, the framebuffer and the bloom pipelines, not the scene pipelines. An extra attachment therefore means a second render pass and pipeline set.
- `DrawFrame` is one function: per-frame buffers (instances, palette, lights, the UBO) are written once per frame, and cascades, the cluster grid and the shadow-cache seed are fit to one camera. Rendering a second view needs a split (Q6).
- The picture reaches the screen as an RGBA8 composite shown by `ImGui::Image`, even in game mode.
- The swapchain accepts only B8G8R8A8/R8G8B8A8 UNORM in sRGB-nonlinear (`VulkanSwapchain.cpp:55–64`, verified). Scene and UI are both display-referred.

**Device and toolchain floor.**
- Vulkan 1.2 is the default. 1.1 is a per-game opt-in.
- Only `samplerAnisotropy` and the swapchain extension are enabled, and no feature structs are chained.
- There is no compute pipeline, no storage images and no 3D images (`VulkanImage.cpp:57` hard-codes 2D).
- Every pass uses v1 `createRenderPass`.
- There are no debug labels, no GPU timestamps and no shader hot reload. There is exactly one specialisation constant (`SPRITES_ONLY`, `shader.frag:710`).
- Apple is a supported target: macOS runs a game on CI's `macos-15` runner (README:294, verified). iOS builds and starts. A reviewer reports that the iOS simulator stops at the first draw because it cannot draw with a non-zero base instance, and that MoltenVK has no ray tracing (README wording not found by my grep, unchecked).

**Per-draw record and sampler budget.**
- The 128-byte record is an element of a storage buffer (binding 11, 65,536 per frame, `gl_InstanceIndex`), not a push constant, for scene draws. The "last thing that fits" comments are stale.
- The 128 limit survives only in `static_assert`s, a push-range default and two GLSL struct copies. Widening costs about 0.5–1 week. The shadow and particle paths still push.
- The fragment stage uses 13 of the 16 guaranteed samplers (8 scene, 5 material), pinned by `test_materials.cpp:1594` (verified: `kSamplersPerSceneSet + kMaterialBindingCount <= 16`).
- Running count from the draft's own per-item figures, not recounted line by line:
  - P7's AO texture takes the count to 14, and Q11's depth read to 15.
  - Compare-sampler twins of the shadow samplers (Q1) would add four more, so Q1 needs a layout split.
  - Q3 needs both raw-depth and compare reads, Q7 adds 3D probe volumes, Q38 two LUTs and R5 a froxel texture.
  - Taken together, Path A's items do not fit the shared layout. Real drivers report more than 16, but the Android floor is the constraint.

**Direct lighting and materials.**
- One GGX lobe (separable Schlick-GGX), Lambert, F0 hard-coded at 0.04 with no F90 (`shader.frag:814`), no specular anti-aliasing (no screen-space derivatives), and one UV set.
- The glTF importer reads one `KHR_materials` extension (`emissive_strength`) and is silent about the rest.
- Textures are uncompressed RGBA8. Lights are directional, point and spot only, with no size.

**Ambient and GI.**
- Ambient is a two-colour hemisphere, or a CPU-convolved IBL from a Radiance `.hdr`. The convolution is "seconds of work" and runs synchronously inside `DrawFrame`, so IBL cannot be driven per frame.
- Ambient is `(diffuse + specular) × authored AO`. It is never occluded or bounced. A roofed room is lit like open sky.
- There are two environment slots, chosen per object by whether its bounds centre is inside a box. There is no parallax correction. Probes are static authored `.hdr` files. Emissive lights nothing else.

**Shadows.**
- Four stable cascades at 2048². Filtering is a hand-written 3×3 PCF on a non-compare sampler, with a device-dependent filter (linear versus nearest raw-depth blend).
- Cascade distance (default 60) and split lambda (default 0.85) exist in `ShadowCascades` but are not exposed. There is no fade, strength or bias control, so the 60-unit cut is a default argument, not a limit of the algorithm. The "manifest key" for resolution is a code-level `GameManifest` field.
- There is one shadowed directional, 2 point casters and 2 spot casters, and 120 MiB allocated always.
- **Probable defect** (by reading, not run): `gatherLights` starts `shadowDirection` at (0,1,0) (`VulkanRenderer.cpp:1125`) and only replaces it from a light with `castsShadow` (`:1225`). A lone directional with `castsShadow=false` therefore keeps overhead cascades, while the shader shadows light index 0 (per a reviewer's reading of `shader.frag:911–919`, not re-checked). The fix is in C++ only.
- One animating skinned character re-records all 18 passes every frame. The cache is seeded with a byte-serial FNV-1a over the whole joint palette (`RenderSystem::MixSignature`, verified), a cost that scales with the cap.

**Post and atmosphere.**
- Tone map is per-channel Reinhard followed by `pow(1/2.2)`. Linear 1.0 leaves as 186/255, and the encode disagrees with hardware sRGB decode.
- Bloom is one half-res Gaussian. Exposure is a scalar that also scales DisplayEncoded scenes. There is no dither.
- The composite's push range is `sizeof(BloomPass::Params)` (`BloomPass.cpp:300–303`, verified), currently one vec4 with all four lanes used. It can be widened without a UBO. `test_screenoverlay` pins exactly two `params.value.z <` matches in the shader.
- Fog is exp² and the sky never reads it. The sky is a gradient plus sun disc.
- There is no time uniform, and no delta time reaches `DrawFrame`.

**Geometry and the CPU/GPU premise.**
- Instancing is done: 10,000 cubes become two draws and Scene Record is 1.59 ms at 10k drawables. The planning doc's "No instancing" is stale.
- Shadow casters are not batched (one push plus one draw per caster per pass).
- The joint palette caps at 1,024 matrices. Past the cap an entity silently draws unskinned. HUSK's design ceiling is about 3,000 drawables.
- The "CPU-bound, GPU idle" premise (4.68 ms for 10,000 cubes) was measured in a 755×389 viewport, before instancing. The 1080p recheck has no recorded number. Treat "extra passes are free on desktop" as unconfirmed.
- **The tablet, quoted from the README.** A Xiaomi Pad 5 (Adreno 640, 2560×1600) drew a lit 2D menu at 8.8 fps, GPU saturated.
  - `SPRITES_ONLY` alone took the menu from 117 to 75 ms (about 8.5 to 13 fps) and a level from 33 to 25 ms.
  - Dynamic resolution on top gave menu 61.7, level 2 61.7 and level 1 76 fps, from 8.8, 27 and 27.
  - ALU versus bandwidth was never separated. MSAA made no difference.

**VFX, 2D and the games.**
- Particles are CPU-simulated lit PBR cubes in one instanced draw after all blended meshes.
- They use a process-global `mt19937{12345}` consumed in EnTT order. Both md5 reference scenes contain a default emitter, so any change to RNG draw order moves them.
- 2D lighting is Ethanon-faithful: up to 64 point lights and game-supplied shadow strips (256 cap). No engine system produces occluder shadows.
- Terrain is a fixed 64×64 sine primitive.
- There are four hosted games.
  - **Wolf Brigade** is a Godot-ported side-on 2D lane RTS, Android-primary, orthographic and unlit. The repo does not say it uses DisplayEncoded.
  - **Magic Portals** is an Ethanon remake.
  - **Penumbra** is a separate Ethanon port that boots on Android and Apple. Its genre is not in the repo.
  - **HUSK** is a 3D RTS with 19 animated untextured models, two directional lights and no point lights.
- Magic Portals, Penumbra and HUSK already ship game-side particles and effects. "The games need textured particles or 2D shadows" is an inference, not a repo fact. The engine's own 28 August doc rates "Tilemaps + Light2D occluders" demand as "Zero across 44 Wolf Brigade scenes".

**Verification and toolchain.**
- The md5 gate is AllPasses (40 frames, 1280×720) `11a10ad1b6c68960ac75198c72a1fd71` and MainScene (120 frames) `5d846b4efa116f1cdfaec32c87b829e2`, on lavapipe. Each has one directional, two point lights and an empty `Hdri` (AllPasses verified).
- **Coverage gap.** Neither scene has an environment, probe, spot light, AO, non-casting sun, cut-out, 2D lit sprite or Light2D. Both have a default particle emitter. So the md5 gate proves nothing for IBL, probes, spots, the 2D lit path or cutouts, which several planned `shader.frag` edits touch. F13 closes this.
- The cloud's lavapipe (Mesa 25.2.8, Vulkan 1.4.318) is a superset: ray query, acceleration structures, mesh shaders, descriptor indexing, timestamps (64 valid bits). It offers only 1x and 4x MSAA. It validates the ON path and cannot catch a missing-feature crash on a phone.
- There is no `glslc` in the cloud. `libshaderc.so.1` exists and apt lists `glslc` 2023.8, but that is not the pinned compiler. CI byte-compares 17 blobs against shaderc v2026.3 (SDK 1.4.357.0). CI is dispatch-only, so a "CI job that uploads artifacts" is as manual as the owner's PC.
- The owner's PC rebuilt 17 of 17 blobs identically and built with MSVC `/W4 /WX` (handoff, third run). That device run reports an integrated Radeon 780M. The 28 August planning doc names an RTX 3060 Laptop. Confirm which GPU does verification (section 8).
- No render-target device test exists in the repo.
- The working Android and Apple builds live in the games' repositories (AGENTS.md). Every phone check therefore means bumping an engine pin in a game repo and building there.
- Rendering is outside `StateHash`.

**Where sources disagreed, and what I used.**
1. *Is the 128-byte record a wall?* The prerequisites report says yes. Four verifiers say it is a storage-buffer element widenable for about 0.5–1 week. I used the verifiers.
2. *Cost totals.* Every verifier raised its survey's totals, and I used the verifier ranges (GI 12–20, shadows 8–16, screen-space 8–15, AA 8–16 plus 7–13, post Tier B 9–18.5, infrastructure core 6.5–15.5, froxel fog 5–9). The review pass then re-costed F3, F10, F7, F9, Q6, R8 and R15 (see the last section).
3. *Prepass.* Estimates ran 1–2 to 2–4 weeks. The cheap route was refuted by review. I use 2.5–4.5.
4. *SSR* 6–12 over 4–8. *DoF* 2–4 over 1.5–3.
5. *Soft particles.* Particles are lit cubes, so the feature as specified is wrong. Replaced by billboard particles (P10).
6. *SPIR-V scanner.* Dropped. The descriptor-pool bugs were C++ pool-versus-layout counts, already fixed at source. I kept a 0.1–0.2 week cross-check of `kSamplersPerSceneSet` against the layout.
7. *Hot reload.* The survey assumed the viewport-resize path rebuilds scene pipelines. It does not, so the cost went up.

---

## 3. Staged plan

**Verification codes.**
- **C** means correctness is checkable in the cloud (lavapipe, validation plus sync validation with `VK_LAYER_VALIDATE_SYNC=1`, CPU twins, md5s).
- **C\*** is the same, but every shader edit needs blobs built by the pinned compiler on the owner's PC or CI first. A scratch `glslc` or `libshaderc` shim can compile for lavapipe iteration only. Never commit scratch blobs, and only trust a scratch build after F3's equivalence run.
- **PC** means it needs the owner's real GPU for timing, driver behaviour, or the byte-diff (the md5s are lavapipe pixels, a same-machine oracle).
- **Ph** means the Android tablet or a phone, via a game repo.
- **E** means the owner's eyes.

**Dependency spine.**
- F3 gates every shader item. F13 gates the first edit to `shader.frag` or `scene_ubo.glsl`.
- F6 gates P3, P4, P5, P6, Q16, Q24 and R1.
- F7 gates P3, P4, P6, P7 and any bound-when-off texture, plus Q16 and Q37.
- F9 gates P2, P12, Q8, Q9, Q28, Q30, R1 and R10.
- F10 gates P7, Q10, Q11, Q13, Q15, Q39, R1 and R4.
- F12 gates P7, Q1, Q3, Q7, Q11, Q38, R5 and R8b.
- F11 gates 3D images, Q7, R2, R5, R8b and R14.
- Q29 gates R1, R2 and R8b.

### Tier 0: foundations

Do not do all of Tier 0 first.
- The **always-first set** is F0, F1, F2, F3, F8 and F13, about **5–10 weeks**.
- **F6 and F7** (about 2–5.5) come before any Tier 1 `shader.frag` item.
- The rest are just-in-time.

**Tier 0 total F0–F14: about 16–34 weeks** (16.3–34.2). F11 is conditional, adding 1.5–3.

- **F0 Pre-flight** (0.5–1.2 wk; owner PC; PC).
  - Capture fixed-step baselines (`--fixed-step --screenshot-every`) of each game's key scenes at their engine pin.
  - Re-run the 1080p and 1440p check and record Frame Wait. This decides whether "extra passes are free" survives.
  - Decide where blobs get built. Write down the recipe for a phone run: which game branch bumps the engine pin, how the APK is built and installed. A minimal in-repo Android sample is not planned, because the Android build lives in the games' repositories.
- **F1 Debug labels and object names** (0.4–1 wk; C).
  - Wrap every pass in `VK_EXT_debug_utils` labels so RenderDoc, Nsight and AGI show names.
  - The extension is requested only with validation today. It must be requested when offered and null-checked for 1.1 Android.
- **F2 GPU timestamps and editor GPU panel** (0.8–2 wk; C for plumbing, PC and Ph for numbers).
  - Query pool per frame in flight, read two frames late, written between passes only (tilers).
  - Surface the results as GPU zones, a panel column and `--frames` output.
  - Optionally feed `DynamicResolution`, keeping its reproducible-run gating. Handle `timestampValidBits == 0`.
- **F3 A shader build path you can trust** (1.5–3 wk; PC).
  - Pick one route for committable `.spv`: the owner's PC with SDK 1.4.357.0, or a CI dispatch that uploads artifacts.
  - Extend `SHADER_JOBS` and `tools/check.sh` for `.comp`, include paths and defines for vendored headers, and per-shader `--target-env` for ray-query.
  - Guard against an unpinned compiler dirtying committed blobs. CMake writes `.spv` into `assets/shaders`.
  - **Scratch-versus-pinned equivalence:** compile the unmodified shaders with the scratch tool, run lavapipe, and require the existing md5s to reproduce. Only then is scratch iteration trustworthy for C\* items.
  - **Batching rule:** items that edit `shader.frag` cannot merge independently, so budget one owner blob session per two or three of them.
- **F4 Shader hot reload** (1.5–3.5 wk; C for swap plumbing with prebuilt blobs, PC for compiling; needs F1, F3).
  - Needs a new runtime path that rebuilds pipelines without touching registries. Hoist the descriptor-set layouts out of `VulkanPipeline` (`TextureRegistry` stores the layout handle), rebuild about ten scene, sky, grid, shadow and shape pipelines plus BloomPass's three, defer-destroy the old ones, and reuse `AssetWatcher`.
  - The hoist overlaps F12 and Q29, but no saving is credited. The same path serves debug views (Q34).
- **F5 Capability layer and forced-caps override** (1.2–2.5 wk; C with forced caps, Ph for real devices).
  - A `GetPhysicalDeviceFeatures2` chain, a `Caps` struct and tiers (Mobile, Desktop, RT, **Apple/MoltenVK**) that a game's main can pin.
  - A forced-caps override so the cloud exercises fallback paths. Never enable a feature by default.
  - Array or 3D images and ray tracing are marked unavailable or untested on the Apple tier.
- **F6 Per-draw and material data path with one planned UBO growth** (1.2–2.5 wk; C\* and PC byte-diff).
  - Widen the record or add a parallel buffer, and add a material-parameter table (slot 0 = off, the `UvTransform` pattern) with an add-only `MaterialComponent::Shading`.
  - **Grow the UBO once, before P3, P4, P5 and P6,** because `scene_ubo.glsl` has no spare vec4 and each growth rebuilds six blobs. Proposal: at least 4 spare vec4 (time, frame index, shading switch, fog, shadow settings, GI and atmosphere parameters) plus one storage buffer for ambient volumes. Check the proposal against the P3/P4/P5/P6 lane needs before the blob session.
  - Re-measure the 10,000-cube CPU frame. Delta-time plumbing into `DrawFrame` is paid by the first item that needs it (Q9, Q12, Q14, Q24).
- **F7 Specialisation-constant tiers** (1–3 wk; C\* for correctness, Ph for the gain).
  - Generalise `SPRITES_ONLY` into a few named, default-false constants (2D-lit-only, lit-no-IBL, mobile 3D).
  - Every later `shader.frag` addition that costs ALU or a fetch gets its own named constant, so OFF compiles to today's instructions.
  - Pipelines are built lazily across the four blend variants. This is the one measured mobile win.
  - Calibrated against `1b99158`: a +12-line shader edit plus a +8-line pipeline option. Device measurement is separate owner time.
- **F8 GPU-pass device-test harness** (0.5–1 wk; C).
  - Offscreen target creation, pipeline from `.spv`, draw, layout transitions, and readers for D32, R8 and RGBA16F.
  - It is the first test of its kind in the repo. `test_device` only reads back an uploaded texture.
- **F9 Post-stack refactor: descriptors and pass order** (1.2–2.5 wk; C; needs F8; run before P2 and P12).
  - Turn BloomPass into an ordered pass list with enable flags, a shared helper, resize handling, DisplayEncoded bypass and extra descriptors (LUT, exposure 1×1, depth).
  - Parameters do **not** need a UBO: widen `BloomPass::Params` directly (push range up to 64 B). P1 does this.
  - Keep "one sRGB encode, at the end". Default records the same four passes.
- **F10 Camera depth prepass** (2.5–4.5 wk; C for depth readback against analytic values, PC and Ph for cost; needs F3, F8).
  - A 1-sample sampleable D32 pass before the scene pass, recorded only when a consumer exists, with a 1×1 D32 cleared to 1.0 bound otherwise.
  - The shadow pipelines cannot be reused as they are. They cull front faces and apply constant 0.6 / slope 1.1 depth bias (`VulkanRenderer.cpp:523–549`, verified), so their depth is not the camera's. Needed:
    - new back-face, no-bias pipelines;
    - a **cutout prepass fragment blob** that samples with `texture()` like the main pass (`shadow_cutout.frag` deliberately uses `textureLod`, verified), which is why F3 is a dependency;
    - an **instanced replay** of `PlanPass` batches, which needs `recordPass` split into write-records and issue-draws.
  - The earlier "reuse `RenderDepthOnly`, no new blob, 1–2 weeks" route is deleted. It costs one push and one draw per caster, which is the per-draw cost instancing removed, and it would let AO and DoF see foliage the main pass discards.
  - Prepass depth is not bit-equal to the main pass, so consumers need a bias and cannot use EQUAL early-Z. Blended draws and particles cannot read it. Keep it off on phones. Depth resolve of the MSAA depth is a different, costlier route (`createRenderPass2`).
  - Depth only. A normal output is separate (R4).
- **F11 Compute pipeline, storage resources and 3D images** (1.5–3 wk; conditional; C for correctness, PC for timing).
  - `ComputePipeline`, storage image and buffer usage, a 3D image type, and `.comp` in the shader list and `check.sh`.
  - Ship it only with its first consumer. Keep all consumers render-only.
- **F12 Descriptor budget and per-tier layout variants** (2–3.5 wk; C\* and Ph; new, review blocker; needs F3).
  - Put optional inputs (AO, depth, LUT volumes, froxels, TLAS) in an optional set with a dummy layout when off, and pack the shadow resources into fewer descriptors (split image from sampler where compare twins are needed). Keep the legacy layout and path for 1.1 devices and phones.
  - Track a **running sampler count per path** against 16. Path A as listed does not fit the shared layout without this item.
  - The AO texture is not bound on phones unless enabled, so rule 7 can hold.
- **F13 Pre-change fixture suite and md5 gate** (1–2 wk; C; new; before the first `shader.frag` or `scene_ubo.glsl` edit).
  - Small fixture scenes for: IBL, a probe box, a spot with shadow, a non-casting sun, a cutout, a skinned caster, and Light2D with sprite, stand-up, gloss and 2D shadows.
  - Record their lavapipe md5s at the **pre-change commit** and add them to `tools/check.sh` beside AllPasses and MainScene.
- **F14 3D showcase scene and assets** (1–2 wk; PC and E; new, split out of F0).
  - A roofed interior plus an outdoor area, textured glTF, skinned characters, a spot light, `castsShadow` variants. Nothing like it is in the repo.
  - Cost includes asset sourcing and licence review. The engine is MIT-0, so prefer procedural or owner-authored assets.
  - A named prerequisite of P4–P7 and every E-tagged Tier 2 item.

### Tier 1: best visual payoff per week

**Tier 1 total: about 17–36 weeks** (16.8–35.8). 1A (3D look) is 12–24. 1B (2D, mobile, content) is 5–12.

Suggested 1A order after Tier 0's first set and F6/F7: P1, P5, P3, P4, P6, then P2 (after F9), P7 (after F10 and F12), P8.

*1A: the 3D look*

- **P1 Composite v2: tone-map operators and finishing** (1.1–2.2 wk; needs F3, F13; C\* and E).
  - ACES fit, AgX and Khronos PBR Neutral as a separate composite pipeline variant. An exact-sRGB encode is opt-in, because it changes every picture.
  - Vignette, grain, chromatic aberration and dither. Dither is off when RGB565 is on. Noise is a pure function of pixel position.
  - Widen `BloomPass::Params` directly. Do not widen `SceneEncoding`. Verify with CPU mirrors (±1–2/255) and a banding ramp level count.
- **P2 Colour-grading LUT plus parametric grade** (1–2 wk; needs P1, F9; C\* and E).
  - A 32³ strip LUT sampled with two 2D taps after the tone map, plus lift/gamma/gain. A scene key with a GUID like `HdriGuid`.
  - It is the one item that also reaches the 2D games' look, because it works in DisplayEncoded.
  - It is a separate composite pipeline variant, so OFF executes today's instructions by construction. BloomPass has no `TextureRegistry` reference and is rebuilt on resize, so descriptors are rewritten on each rebuild.
- **P3 Fog v2** (1–1.8 wk; needs F3, F6, F7, F13; C\*, Ph and E).
  - Exponential height fog, fog on the sky (which fixes the horizon seam), a sun in-scatter lobe, and optional fog colour from ambient.
  - Behind a named specialisation constant, since `shader.frag` is register-heavy on Adreno.
  - Acceptance includes a tablet before/after with the constant off. The oracle is exact: transmittance `exp(-σd)` and the height closed form against numeric integration.
- **P4 Shading-model switch** (1.2–2.8 wk; needs F3, F6, F7, F13, F14; C\*, Ph and E).
  - Geometric specular anti-aliasing (Kaplanyan et al. 2016; Tokuyoshi and Kaplanyan 2019, 2021), Toksvig for normal maps, height-correlated Smith visibility (Heitz 2014), and multiple-scatter energy compensation (Kulla and Conty 2017; Fdez-Agüera 2019).
  - Derivatives go right after the albedo fetch, before the cutout `discard`. Every fragment pays for them, so they sit behind a specialisation constant and the tablet before/after is part of acceptance.
  - The white-furnace test is exact only to the Karis fit's few percent, so test against a CPU twin of the same fit. Measure highlight variance on a rotating normal-mapped sphere.
- **P5 Shadow settings block plus the `castsShadow` fix** (1.8–3.8 wk; needs F3, F6, F13; C\* and E).
  - **Plumbing only:** expose shadow distance (clamped to far plane) and split lambda, which the maths already supports.
  - **Shader work:** fade band, strength and bias scale. **Per-kind resolution tier:** a code-level `GameManifest` field. A scene-level tier needs a live rebuild of up to 120 MiB (+0.5–1 week).
  - **The `castsShadow` fix is C++ only** (in `gatherLights`). Reproduce the defect first. It changes the picture of any scene whose first directional has `castsShadow=false`, so ship it behind an opt-in key that keeps today's behaviour as the default until each game's picture is checked at its pin.
  - Extract the gather decision into a pure function so it is testable.
  - New keys are written only when non-default. Scene keys live in `SceneSerializer.cpp`, light keys in `ComponentCodec.cpp` (whose writer needs restructuring), and Penumbra byte-compares its writer.
  - A spot-light fixture comes from F13.
- **P6 Ambient volumes plus specular occlusion and multi-bounce AO** (2–3.5 wk; needs F6, F7, F13, F14; C\* and E).
  - A box component with six-colour ambient cube, feather and priority, up to 8 in the UBO, addressed by world position. It fixes the roofed-room fault.
  - Add specular occlusion (Lagarde and de Rousiers 2014) and a multi-bounce fit (Jimenez et al. 2016). The survey said 1.5–3 weeks. I added 0.5 for a fixture scene and the six-blob rebuild.
- **P7 GTAO on the ambient term only** (2–4 wk; needs F10, P6, F12, F7; C\*, PC, Ph and E).
  - Half-res R8 from prepass depth, depth-aware blur, sampled at `gl_FragCoord` in the ambient term only (never direct light or emissive).
  - Spatial noise only (interleaved gradient noise of pixel coordinates), with no temporal accumulation.
  - Needs an orthographic branch and a depth-aware upsample to hide MSAA fringing. Normals are rebuilt from depth and ignore normal maps.
  - OFF means the AO sampler is not in the layout (F12) and the fetch is compiled out (F7). Acceptance includes a tablet check that OFF costs what it cost before.
  - Reference: Jimenez et al. 2016; Bavoil et al. 2008; XeGTAO (MIT, reviewer-checked).
- **P8 Cheap material wins** (1.5–3.7 wk; needs F3, F13; C for importer, C\* for shader, E).
  - Warn once per unsupported `KHR_materials_*`. Map `KHR_materials_unlit` onto the unlit flag. Read `ior` and `specular` as factors (F0 and F90, default 0.04).
  - Glass specular that does not fade with alpha. This must also select the Premultiplied pipeline and change the multiply at `shader.frag:1040`.
  - Depth-biased mesh decals for flat quads (terrain-conforming ones need new CPU projection geometry).

*1B: cross-cutting, 2D and mobile*

- **P9 Palette cap lift, loud overflow, and per-caster shadow signature** (0.7–2 wk; C). This merges the old Q4.
  - Raise 1,024 matrices (about 1 MB at 16,384), log once, and add a Stats counter. Check device `maxStorageBufferRange`.
  - **Sign the palette per caster:** digest once per skinned caster per frame, not byte-wise per pass, and empty passes must stop depending on the palette.
  - Lifting the cap without this is a regression. The byte-serial hash over a 1 MiB palette is, by a reviewer's estimate (unmeasured), about 1.2–1.4 ms per frame. It also fixes the "one animating skinned character re-records 18 passes" cost. Add a count test.
  - Real risk for HUSK at its stated ceiling.
- **P10 Textured billboard particles** (1.5–3.5 wk; C and E).
  - An opt-in Sprite mode of the existing emitter, default staying the lit cube. Camera-facing or flat quads, with Alpha, Additive or Premultiplied blending, unlit with an HDR emissive multiplier so bloom works.
  - Hidden work:
    - There is no per-frame vertex stream on the main pipelines. `MeshRegistry::Replace` is a blocking copy that bumps generation, and WorldShapes has its own pipeline.
    - Particles are not entities, so they need a route into the sorted blended list. Without it a puff cannot sit between sprite layers.
    - A per-emitter texture breaks the single-batch loop.
  - Keep the default RNG draw order. Three of four games already do particles game-side, so this serves new content.
- **P12 Output stage plus FSR1** (3–6.5 wk; needs F3, F9; C\*, Ph, E).
  - A display-resolution final pass with nearest or integer, FSR1 EASU and RCAS filters, and render scale above 1. The overlay is drawn after it so UI is not softened.
  - A nearest filter alone is a 1–3 day sampler swap at BloomPass's output sampler with the ImGui texture re-registered. Sharp-bilinear needs a shader.
  - It matches the one measured pain (tablet, dynamic resolution). Vendor the MIT headers with their licence, and extend the compile rule for include paths and defines.
  - EASU cost on Adreno is unmeasured. Qualcomm's spatial SGSR 1 (BSD-3-Clause, reviewer-checked) is a lighter alternative. SGSR 2 is temporal.

### Tier 2: menu, chosen by game and platform

**Tier 2 whole menu: about 87–173 weeks. Nobody should do it all.** Sub-totals: shadows and indirect light 15–28, camera/post/atmosphere 11–23, materials and terrain 8–15, 2D/VFX/content 12–28, engine and scale 20–38, on demand 22–41. Q4 is retired (merged into P9).

*Shadows and indirect light*
- **Q1 Hardware-compare PCF plus rotated-disk kernel** (2.5–5 wk; needs P5, F3, F12; C\*, Ph, E). Replaces nine manual taps and the device-dependent filter. Compare twins of four shadow samplers reach 17, so it needs a layout split into image plus sampler descriptors for bindings 1, 3 and 4. It is not "two routine bindings". The legacy path stays default.
- **Q2 Normal offset and bias parity for spot and point** (0.7–2 wk; needs P5; C\*, E). The factor functions need the normal passed in, and `GpuLight` has no spare scalar.
- **Q3 PCSS contact-hardening** (3–6 wk; needs Q1, F12; C\*, PC, E). Directional first (Fernando 2005). It needs raw-depth blocker reads and compare taps together, so it hits the sampler limit in full. Cascade padding must grow and seam penumbra widths must be clamped. Point-light PCSS adds 1–1.5.
- **Q5 Box-projected reflection probes** (0.5–1 wk; needs F3; C\*). The ray-versus-AABB oracle is exact. Use a six-colour cube fixture (Lagarde and Zanuttini 2012).
- **Q6 Editor-baked probe capture, including the view split** (4–7 wk; needs F3; C for six-wall captures, PC).
  - Includes about 1.5–3 weeks to split `DrawFrame` into "prepare and record scene for camera X into target Y" without acquire or present. The single per-frame instance buffer would otherwise be clobbered, and cascades, cluster grid and shadow-cache seed are fit to one camera.
  - Then six-face capture into a square MSAA-compatible target, cube to equirect, a new Radiance writer, and the editor capture and probe-assignment workflow.
  - The same "render a view" abstraction would help R1, R6 and Q35, but no saving is credited. Commit baked `.hdr`; never regenerate in CI.
- **Q7 Baked light-probe volume** (4–6.5 wk; needs Q6, P6, F11, F12; C\* and E). SH L1 or ambient-cube grid in 3D textures sampled trilinearly at world position, baked from Q6 captures (Ramamoorthi and Hanrahan 2001). It needs first 3D image support and a new asset kind with hot reload. Probes leak through thin walls, so use per-room volumes plus a visibility term.

*Camera, post and atmosphere*
- **Q8 Pyramid bloom** (1–2 wk; needs F9; C\* and E). Karis-weighted, resolution-independent, legacy chain stays default (Jimenez 2014).
- **Q9 GPU-only auto-exposure** (1.2–2.5 wk; needs F9, P1; C\* and E). Log-average chain from about 256×144, ping-pong adaptation in EV, no CPU readback, fixed delta under `--fixed-step`, seeded first frame. Needs delta time passed into `DrawFrame`.
- **Q10 Depth of field** (2–4 wk; needs F9, F10; C\*, PC, E). CoC from depth, half-res near and far gather before bloom (Abadie 2018; Jimenez 2014). New `CameraComponent` keys. Perspective only; off on phones.
- **Q11 Contact shadows for the sun** (1–2 wk; needs F10, F12; C\* and E). 8–16 step march, bias and thickness tuning, MSAA silhouette handling. It adds a depth sampler to `shader.frag`.
- **Q12 Analytic sky, time of day and a 2D cloud layer** (2–4 wk; needs P1, P3; C\* and E). Preetham or Hosek-Wilkie evaluated on the CPU into the existing hemisphere ambient (not IBL), plus a ray-plane cloud layer. New `Background` value appended. Needs a time uniform. Perspective only.
- **Q13 Sun light shafts** (1.5–3 wk; needs F9, F10; C\* and E). Half-res shadow-map raymarch (Mitchell 2007), or a radial blur on phones.
- **Q14 Stylised water** (1.5–3 wk; needs a time uniform; C\* and E). Own pipeline, scrolling normals or Gerstner waves, Fresnel reflection of sky and IBL, no refraction. Depth-fade foam needs F10.
- **Q15 Camera-only motion blur** (1–2 wk; needs F10; C\* and E). Previous drawn view-projection, reset on cuts.

*Materials and terrain*
- **Q16 Clearcoat** (1–2 wk; needs F6, F7; C\* and E). Second GGX lobe plus one extra IBL fetch. glTF sample-asset fixtures are not in the repo.
- **Q17 Sphere and tube lights** (0.8–1.6 wk; needs F3; C\* and E). Representative-point method (Karis 2013), radius in the unused point-light attenuation lane. Likely parity with Godot 4.7's reported area light (unchecked).
- **Q18 Alpha-to-coverage for cutouts** (0.8–1.8 wk; needs F3; C\* and E). The pipeline flag alone gives a biased half edge because the shader discards first. A good result needs the discard removed and alpha remapped with `fwidth` (Golus 2017). A second opaque pipeline breaks `PlanPass` keying. Lavapipe here has 1x and 4x only, and coverage patterns differ per vendor.
- **Q19 Heightmap terrain** (2–4 wk; C). Image import and chunked meshes. The matching collider is simulation-affecting, so it needs integer bit-exact decode and a cache key that hashes heights.
- **Q20 Terrain splat layers** (3–6 wk; needs Q19; C\* and E). A separate pipeline and descriptor layout with array textures, plus the lighting code factored into an include. Reference: Andersson 2007; Heitz and Neyret 2018.

*2D, VFX and content*
- **Q21 Emitter authoring depth plus seeded RNG** (1.2–3 wk; C). Shapes, bursts, curves, drag, world versus local space, sub-emitters, ground collision. Per-emitter seed, with 0 keeping the legacy global stream. Curves need a widget the inspector lacks. Fix the false statement at `ARCHITECTURE.md:3371`.
- **Q22 Ribbons and trails** (1.5–3.5 wk; needs P10's vertex-stream work; C and E). No per-vertex alpha exists, so fades use a gradient texture.
- **Q23 Light2D shapes** (1.5–3 wk; needs F3; C\*, PC, E). Cone, cookie, directional and falloff via flag bits and a parallel buffer (the 32-byte stride is pinned). Re-verify the four games' goldens. A cookie fetch is probably too heavy for Adreno.
- **Q24 Time uniform plus sprite effects** (1.5–3.5 wk; needs F6; C\* and E). Dissolve and SDF outline. True neighbour-fetch outlines cost too much on the tablet.
- **Q25 Tile light propagation** (1–2.5 wk; C; no shader). CPU BFS into the tile vertex colour the shader already multiplies. Integer, so replay-safe. Static lights 1–1.5 weeks. Moving emitters restage the whole vertex buffer (up to about 21 MB) and need chunking.
- **Q26 Pixel-art presentation** (0.8–2 wk; C). Integer-scaled low-res target, nearest present sampler, snapped ortho camera. There is no evidence a pixel-art game is hosted.
- **Q27 Sprite fill-rate pair** (1.9–4 wk; C, Ph). Transparent-texel early-out plus tight sprite meshes. The early-out is bit-identical only on the three blended pipelines. On the opaque pipeline it changes alpha-0 pixels. Gate both on a half-day tablet measurement: empty the light loop, unbind overlay and gloss, crop sprites.
- **Q28 Game-authored post-effect hook** (1.5–3 wk; needs F9; C and PC). A game-supplied fullscreen fragment shader with scene colour bound, SPIR-V validation, and fallback to no effect on a failed load. It is the Godot-style capability the 2D games lack.
- **Q45 (was P11) 2D occluder shadows derived from existing data** (1.5–3.5 wk; C and E; no GLSL).
  - A `LightOccluder2D` component plus a system that extrudes silhouette edges into the existing `Light2DShadowsComponent` strips (opacity 1, no mask).
  - Polygons come from `ConvexHullColliderComponent` (`Components.hpp:1592`) or a sprite alpha silhouette, with a numeric inspector. An in-viewport polygon editor does not exist in the repo and is a first-of-its-kind tool: +1–2 weeks if wanted (2.5–5.5 total).
  - Opt in per light to avoid colliding with the games' own writers. The 256-strip global cap needs a per-light budget and range culling. The CPU twin `Light2D::ShadowKeep` is the oracle.
  - Re-rated as **parity with zero internal demand** (see section 5), so it is no longer Tier 1. R10 depends on it.

*Engine and scale*
- **Q29 Scene-pass variants and runtime AA switch** (2–3.5 wk; C). Promoted to a dependency of R1, R2 and R8b. `VulkanPipeline` owns the descriptor layouts, `TextureRegistry` is built from them and the render pass is set once, so a velocity attachment needs a second render pass and about ten pipelines across four blend variants. The AA switch is its first consumer. Lavapipe offers 1x and 4x only, so 2x is untestable in the cloud.
- **Q30 SMAA or FXAA for the 1x path** (1.5–3 wk; needs F9; C\* and E). FXAA is 1–2 weeks; SMAA (Jimenez et al. 2012) needs area and search tables. Skip on the tablet, where MSAA is nearly free.
- **Q31 MultiMesh component** (3–5 wk; C). Bulk instances with chunked bounds, writing records directly. Unpriced: serialising big arrays, PlayMode snapshots, picking, parenting, and the 4,095 UV-slot cap for atlas cells. If a tick writes it, it must opt into `StateHash`.
- **Q32 Shadow-caster batching** (2–4 wk; C). `PlanPass` for depth passes, with three `RenderDepthOnly` call sites. `shadow_cutout.frag` needs an instance index, and shadow records across 18 passes can exceed the 65,536-record buffer. The prize is at most about 0.25 ms at 1,000 casters. Measure first.
- **Q33 Mesh LOD with meshoptimizer** (2.5–4.5 wk; C). Only if dense models appear.
- **Q34 Debug views** (1.5–3 wk; needs F4; C\*). Albedo, normal, roughness, AO, cluster heat map, cascade tint. Needs the runtime pipeline-rebuild path and a composite bypass. Wireframe needs a polygon-mode option and an optional device feature.
- **Q35 Frame graph (lite)** (5–10 wk; PC). Only after at least three hand-built passes exist. Keep `VkRenderPass`. Wrapping new passes only is cheaper (3–6). Migrating the existing eight passes plus a dynamic-rendering decision is 5–10 (O'Donnell 2017).
- **Q36 Mobile bandwidth path** (2–5 wk; Ph). Lazily allocated MSAA (no allocation path exists, and Adreno may offer none), an opt-in RGBA8 scene target, and direct composite into the swapchain pass. Split it into three items.

*On demand or structural*
- **Q37 Extra lobes** (3.5–7.3 wk; needs F6, F7; C\* and E). Sheen, anisotropy, thin translucency, iridescence (Estevez and Kulla 2017; Belcour and Barla 2017). Capability only; no hosted game needs them.
- **Q38 LTC area lights** (3–5.5 wk; needs Q17, F12; C\* and PC). Heitz et al. 2016. Two LUT samplers, and the light struct grows from 64 to 96 bytes. Check LUT licences. Parity with Godot 4.7's reported area light (unchecked).
- **Q39 Transmission and refraction** (3.5–6 wk; needs F10; PC and Ph). A pass split with opaque colour copy and mips. MSAA colour must be stored, which is hostile to tilers.
- **Q40 Decals** (3–6 wk; C\* and E). Clustered-forward (no depth, opaque PBR only, new storage binding) or depth-projected (needs F10 and a pass split).
- **Q41 Dithered translucent shadows and two-sided casters** (1.5–3 wk; needs Q1; C\*). Enderton et al. 2010; McGuire and Enderton 2011. The mode must be mixed into `ShadowPassSignature`.
- **Q42 Distortion buffer** (2–4 wk; needs P10; C\* and PC).
- **Q43 UV-slot dedupe and atlas packer** (2.5–5 wk; C). Dedupe is bit-identical and 0.3–0.8 weeks. The packer waits on a measured bind count.
- **Q44 Impostor or model-to-sprite baking** (2.5–4.5 wk; C and E). 3D units become lit sprites through the Light2D normal path. A 2.5D-niche idea.

### Tier 3: frontier and moonshots

**Tier 3 menu: about 84–160 weeks** (excluding R11's optional +2–4 and a second R3 backend). **The credible slice (R1, R2, R5, R6, R8a, R10, R15, plus F11 and P12) is about 38–76 for its own items, and 60–118 standalone with its prerequisites (section 1).**

- **R1 Temporal plumbing and TAA** (7–13 wk; needs F6, F9, F10, P12, Q29; C, PC, E). Halton jitter on the uploaded UBO copy only (index from the absolute frame counter), velocity target with previous model matrices and previous skin palettes, display-resolution history reset on resize and dynamic-resolution steps (Karis 2014; Pedersen 2016; Yang et al. 2020). It normally replaces MSAA, and blended draws need masks. Most of the schedule is tuning ghosting by eye.
- **R2 Temporal upscaling, FSR2/3 or TSR class** (5–12 wk; needs R1, P12, F11, Q29; PC and E). Integrate FidelityFX source (older versions MIT as recalled; the current SDK reportedly has exceptions, unchecked) or write a TSR-style pass. FSR 4 is not assumed open. Before costing, check what shader language and compile route that source needs against F3. Lavapipe may lack its 16-bit and subgroup requirements. Tablet cost is unmeasured and likely high.
- **R3 DLSS and XeSS out-of-tree plugins** (3–6 wk for the first backend, +1.5–3 for the second; needs R1; PC).
  - DLSS needs an RTX GPU.
  - XeSS super resolution reportedly runs through a DP4a path on non-Intel GPUs (unchecked), so it may be testable on the owner's machine whichever GPU it is.
  - Both SDKs are proprietary. Read redistribution terms before putting even a plugin in an MIT-0 repo.
- **R4 SSR** (6–12 wk; needs F10, F9; C, PC, E). Priced with depth-derived normals, which ignore normal maps. A real normal target adds 1–2 and needs Q29-style pass variants. The probe's specular is already in the forward colour, so a post pass double-counts it unless a separate specular target or history is added. Do it only for glossy floors (McGuire and Mara 2014).
- **R5 Froxel volumetric fog and lit shafts** (5–9 wk; needs F11, F12, P3, and R1 or a coarse non-temporal version; PC). Wronski 2014; Hillaire 2015. It needs a new scene-set binding, duplicates the cluster and cascade lookups, and is unmeasured on the Pad 5.
- **R6 Hillaire LUT atmosphere** (4–7 wk; needs P1, P3, Q12; C\*, PC, E). The reference uses compute for multi-scatter, so the fragment-pass port is not mechanical. A sun-unit decision changes lighting for any scene that opts in (Hillaire 2020; Bruneton and Neyret 2008).
- **R7 Raymarched volumetric clouds** (5–9 wk; needs R5, R6, R1; PC). Not recommended; keep the 2D cloud layer (Schneider and Vos 2015).
- **R8a Ray-query sun and spot shadows** (5–10 wk; needs F5, F3 with a SPIR-V 1.4+ target; C for correctness since lavapipe exposes ray query, PC with an RT GPU, E).
  - Hard shadows only, from a static or rigid-body TLAS. Skinned characters are not in the TLAS.
  - Needs device-address and acceleration-structure features, a BLAS/TLAS manager and a ray-query path in the existing fragment shader (no RT pipeline).
  - Soft or stochastic shadows need R1 for denoising. It is a second implementation of shadowing to maintain. Heitz et al. 2018.
- **R8b DDGI** (+6–12 wk on top of R8a; needs R8a, F11, F12, Q29; C, PC, E). Probe update, irradiance and visibility atlases, relocation and sampling in `shader.frag` (more samplers). Animated meshes need compute skinning, a separate 2.5–4.5, not included. Majercik et al. 2019 and 2021.
- **R9 Baked lightmaps for static 3D** (10–18 wk; needs Q7, therefore Q6; C and E). Unwrapper, CPU path tracer with BVH, denoise, atlas, a second vertex binding, `TEXCOORD_1` import and Android compression. Not for generated worlds.
- **R10 2D radiance-cascade GI** (5–9 wk; needs Q45, F9; C, PC, E). Fragment passes at low resolution, deterministic, noise-free (Sannikov 2023; Rong and Tan 2006 for jump flooding). A separate desktop mode, never a replacement for Ethanon-exact lighting. It needs a rule for DisplayEncoded. The tablet is fragment-bound and this is fill-heavy, so it is desktop-only unless measured.
- **R11 GPU culling and indirect draws** (6–10 wk, plus 2–4 for a retained draw cache; needs F11, F2 evidence that CPU recording is the limit; C and PC). Order-preserving compaction (prefix sum, not atomics), the CPU path kept as default, and not drawable on the iOS simulator (reviewer-reported).
- **R12 Hi-Z occlusion** (+6–10 wk on top of R11; needs R11, a stored or resolved depth, `createRenderPass2`; PC). The draft's "10–18 cumulative" disagreed with its own sum and is dropped.
- **R13 Bindless materials** (4–7 wk; C and PC). Two material paths forever (1.1 devices; iOS Tier 1 GPUs disable Metal argument buffers).
- **R14 GPU compute particles** (4–10 wk; needs F11, P10; C and PC). The CPU path already draws 20,000 particles in one call.
- **R15 HDR output** (3–6 wk; needs P1, P12 or F9, F5; PC with an HDR monitor, E). The swapchain is UNORM sRGB-nonlinear and all UI is ImGui drawn into it, so this needs a colour-space extension (not requested anywhere per a reviewer, unchecked), metadata, a float or 10-bit composite and a UI conversion stage (render the UI to an intermediate sRGB target or convert in a final pass). Game mode only; the editor stays SDR. Godot 4.7 reportedly ships HDR output (unchecked), so this is catch-up.

**Not recommended** (priced for the record, all unvalidated guesses): SSGI (10–16 wk), voxel or SDF GI (16–30), VFX graph (8–20), mesh shaders (10–18), virtual shadow maps (12–30), Nanite-like (60–130), Lumen-class (50–100+), compute skinning (2.5–4.5), per-object motion blur (3–5 on top of R1), async compute, standalone dynamic-rendering migration.

### Totals

| Bundle | Contents | Person-weeks |
|---|---|---|
| Tier 0 | F0–F14 (F11 conditional, +1.5–3) | 16–34 |
| Tier 1 | P1–P10, P12 (1A: 12–24, 1B: 5–12) | 17–36 |
| Tier 2 | whole menu, not to be done in full | 87–173 |
| Tier 3 | menu | 84–160 |
| **Path A** | the look layer: all of Tier 0 incl. F11, P1–P8, Q1, Q2, Q3, Q5–Q12, Q16 | **52–105** |
| **Path B** | 2D/2.5D and mobile: F0–F3, F5–F9, F13, P1, P2, P9, P10, P12, Q21–Q24, Q26–Q28 | **27–59** (15–34 on top of A) |
| **Path C** | frontier slice with prerequisites: R1, R2, R5, R6, R8a, R10, R15, F11, P12 (38.5–75.5), plus F0, F3, F5–F10, F12–F14, P1, P3, Q12, Q29, Q45 (21–43) | **60–118** standalone; 38–73 on top of A and B |
| **Union A+B+C** | | **about 105–212** |

Path A keeps F4 (hot reload, 1.5–3.5), which could be dropped to save that much. Optional adds: Q45 is not in Path B unless a game needs occluder shadows. R8b adds 6–12 to C.

---

## 4. Summary table

"Pay" is 1–5 and is **my judgement**, not a measurement. It means how much the item moves the picture of the named consumer: the 3D showcase or HUSK-class scene for 3D items, the tablet-class 2D niche for 2D items. 5 means "changes the whole picture if that is the target", not "a hosted game needs it". No hosted game consumes R5–R9. "Verify" uses the codes from section 3. Weeks are as in section 3.

| ID | Item | Tier | Pay | Weeks | Depends on | Verify |
|---|---|---|---|---|---|---|
| F0 | Pre-flight, baselines, device recipe | 0 | 1 | 0.5–1.2 | owner PC | PC |
| F1 | Debug labels | 0 | 1 | 0.4–1 | – | C |
| F2 | GPU timestamps and panel | 0 | 1 | 0.8–2 | F1 | C, PC, Ph |
| F3 | Trusted shader build path | 0 | 1 | 1.5–3 | owner PC or CI | PC |
| F4 | Shader hot reload | 0 | 1 | 1.5–3.5 | F1, F3 | C, PC |
| F5 | Capability layer, forced caps | 0 | 1 | 1.2–2.5 | – | C, Ph |
| F6 | Draw/material path, one UBO growth | 0 | 1 | 1.2–2.5 | F3 | C\*, PC |
| F7 | Specialisation-constant tiers | 0 | 2 | 1–3 | F3, F2 | C\*, Ph |
| F8 | GPU-pass device-test harness | 0 | 1 | 0.5–1 | – | C |
| F9 | Post-stack refactor | 0 | 1 | 1.2–2.5 | F8 | C |
| F10 | Camera depth prepass | 0 | 1 | 2.5–4.5 | F3, F8 | C, PC, Ph |
| F11 | Compute, storage, 3D images | 0 | 1 | 1.5–3 | first consumer | C, PC |
| F12 | Descriptor budget, layout variants | 0 | 1 | 2–3.5 | F3 | C\*, Ph |
| F13 | Pre-change fixture suite, md5 gate | 0 | 1 | 1–2 | – | C |
| F14 | 3D showcase scene and assets | 0 | 1 | 1–2 | – | PC, E |
| P1 | Tone-map operators and finishing | 1 | 4 | 1.1–2.2 | F3, F13 | C\*, E |
| P2 | Grading LUT | 1 | 4 | 1–2 | P1, F9 | C\*, E |
| P3 | Fog v2 | 1 | 4 | 1–1.8 | F3, F6, F7, F13 | C\*, Ph, E |
| P4 | Specular AA, visibility, energy | 1 | 3 | 1.2–2.8 | F3, F6, F7, F13, F14 | C\*, Ph, E |
| P5 | Shadow settings, `castsShadow` fix | 1 | 3 | 1.8–3.8 | F3, F6, F13 | C\*, E |
| P6 | Ambient volumes, specular occlusion | 1 | 3 | 2–3.5 | F6, F7, F13, F14 | C\*, E |
| P7 | GTAO on ambient | 1 | 4 | 2–4 | F10, P6, F12, F7 | C\*, PC, Ph, E |
| P8 | Importer honesty, glass, mesh decals | 1 | 3 | 1.5–3.7 | F3, F13 | C, C\*, E |
| P9 | Palette cap lift, loud overflow, per-caster signature | 1 | 2 | 0.7–2 | – | C |
| P10 | Textured billboard particles | 1 | 4 | 1.5–3.5 | – | C, E |
| P12 | Output stage and FSR1 | 1 | 3 | 3–6.5 | F3, F9 | C\*, Ph, E |
| Q1 | Compare PCF, rotated kernel | 2 | 3 | 2.5–5 | P5, F12 | C\*, Ph, E |
| Q2 | Spot/point normal offset | 2 | 2 | 0.7–2 | P5 | C\*, E |
| Q3 | PCSS | 2 | 4 | 3–6 | Q1, F12 | C\*, PC, E |
| Q5 | Box-projected probes | 2 | 3 | 0.5–1 | F3 | C\* |
| Q6 | Baked probe capture, view split | 2 | 3 | 4–7 | F3 | C, PC |
| Q7 | Baked light-probe volume | 2 | 4 | 4–6.5 | Q6, P6, F11, F12 | C\*, E |
| Q8 | Pyramid bloom | 2 | 3 | 1–2 | F9 | C\*, E |
| Q9 | Auto-exposure | 2 | 4 | 1.2–2.5 | F9, P1 | C\*, E |
| Q10 | Depth of field | 2 | 4 | 2–4 | F9, F10 | C\*, PC, E |
| Q11 | Contact shadows | 2 | 3 | 1–2 | F10, F12 | C\*, E |
| Q12 | Analytic sky, time of day, clouds | 2 | 4 | 2–4 | P1, P3 | C\*, E |
| Q13 | Sun light shafts | 2 | 3 | 1.5–3 | F9, F10 | C\*, E |
| Q14 | Stylised water | 2 | 3 | 1.5–3 | time uniform | C\*, E |
| Q15 | Camera motion blur | 2 | 2 | 1–2 | F10 | C\*, E |
| Q16 | Clearcoat | 2 | 3 | 1–2 | F6, F7 | C\*, E |
| Q17 | Sphere/tube lights | 2 | 2 | 0.8–1.6 | F3 | C\*, E |
| Q18 | Alpha-to-coverage cutouts | 2 | 3 | 0.8–1.8 | F3 | C\*, E |
| Q19 | Heightmap terrain | 2 | 3 | 2–4 | – | C |
| Q20 | Terrain splat layers | 2 | 4 | 3–6 | Q19 | C\*, E |
| Q21 | Emitter depth, seeded RNG | 2 | 3 | 1.2–3 | – | C |
| Q22 | Ribbons and trails | 2 | 3 | 1.5–3.5 | P10 | C, E |
| Q23 | Light2D shapes | 2 | 3 | 1.5–3 | F3 | C\*, PC, E |
| Q24 | Time uniform, sprite effects | 2 | 3 | 1.5–3.5 | F6 | C\*, E |
| Q25 | Tile light propagation | 2 | 3 | 1–2.5 | – | C |
| Q26 | Pixel-art presentation | 2 | 3 | 0.8–2 | – | C |
| Q27 | Sprite fill-rate pair | 2 | 1 | 1.9–4 | tablet measurement | C, Ph |
| Q28 | Game-authored post-effect hook | 2 | 3 | 1.5–3 | F9 | C, PC |
| Q29 | Scene-pass variants, runtime AA switch | 2 | 2 | 2–3.5 | – | C |
| Q30 | SMAA or FXAA (1x) | 2 | 3 | 1.5–3 | F9 | C\*, E |
| Q31 | MultiMesh | 2 | 2 | 3–5 | – | C |
| Q32 | Shadow-caster batching | 2 | 1 | 2–4 | measurement | C |
| Q33 | Mesh LOD | 2 | 1 | 2.5–4.5 | – | C |
| Q34 | Debug views | 2 | 2 | 1.5–3 | F4 | C\* |
| Q35 | Frame graph (lite) | 2 | 1 | 5–10 | three passes exist | PC |
| Q36 | Mobile bandwidth path | 2 | 1 | 2–5 | F2 | Ph |
| Q37 | Sheen, anisotropy, translucency, iridescence | 2 | 2 | 3.5–7.3 | F6, F7 | C\*, E |
| Q38 | LTC area lights | 2 | 3 | 3–5.5 | Q17, F12 | C\*, PC |
| Q39 | Transmission and refraction | 2 | 4 | 3.5–6 | F10 | PC, Ph |
| Q40 | Decals | 2 | 3 | 3–6 | F10 (depth-projected) | C\*, E |
| Q41 | Dithered translucent shadows | 2 | 2 | 1.5–3 | Q1 | C\* |
| Q42 | Distortion buffer | 2 | 3 | 2–4 | P10 | C\*, PC |
| Q43 | UV-slot dedupe, atlas packer | 2 | 1 | 2.5–5 | measurement | C |
| Q44 | Impostor baking | 2 | 3 | 2.5–4.5 | – | C, E |
| Q45 | 2D occluder shadows (was P11) | 2 | 2 | 1.5–3.5 | – | C, E |
| R1 | Temporal plumbing and TAA | 3 | 4 | 7–13 | F6, F9, F10, P12, Q29 | C, PC, E |
| R2 | Temporal upscaling | 3 | 4 | 5–12 | R1, P12, F11, Q29 | PC, E |
| R3 | DLSS/XeSS plugins | 3 | 4 | 3–6 (+1.5–3) | R1 | PC |
| R4 | SSR | 3 | 3 | 6–12 | F10, F9 | C, PC, E |
| R5 | Froxel volumetric fog | 3 | 5 | 5–9 | F11, F12, P3, R1 | PC |
| R6 | LUT atmosphere | 3 | 5 | 4–7 | P1, P3, Q12 | C\*, PC, E |
| R7 | Volumetric clouds | 3 | 5 | 5–9 | R5, R6, R1 | PC |
| R8a | Ray-query sun/spot shadows | 3 | 4 | 5–10 | F5, F3 | C, PC, E |
| R8b | DDGI | 3 | 5 | +6–12 | R8a, F11, F12, Q29 | C, PC, E |
| R9 | Baked lightmaps | 3 | 5 | 10–18 | Q7 | C, E |
| R10 | 2D radiance-cascade GI | 3 | 4 | 5–9 | Q45, F9 | C, PC, E |
| R11 | GPU culling and indirect | 3 | 1 | 6–10 (+2–4) | F11, F2 | C, PC |
| R12 | Hi-Z occlusion | 3 | 1 | +6–10 | R11 | PC |
| R13 | Bindless materials | 3 | 1 | 4–7 | – | C, PC |
| R14 | GPU compute particles | 3 | 2 | 4–10 | F11, P10 | C, PC |
| R15 | HDR output | 3 | 4 | 3–6 | P1, P12 or F9, F5 | PC, E |
| – | SSGI | moonshot | 3 | 10–16 | – | not recommended |
| – | Voxel or SDF GI | moonshot | 4 | 16–30 | F11 | not recommended |
| – | VFX graph | moonshot | 3 | 8–20 | R14 | not recommended |
| – | Mesh shaders | moonshot | 1 | 10–18 | – | not recommended |
| – | Virtual shadow maps | moonshot | 5 | 12–30 | R11 | not recommended |
| – | Nanite-like | moonshot | 3 | 60–130 | everything | not recommended |
| – | Lumen-class | moonshot | 5 | 50–100+ | everything | not recommended |

Q4 is retired (merged into P9). P11 became Q45. All week ranges and Pay values are unvalidated.

---

## 5. Where this engine could genuinely stand out

Copying PBR features makes it a smaller Godot. Standing out means lit 2D/2.5D on phones, with deterministic reproducibility. Every competitor comparison below is unchecked recollection (cutoff June 2026) or a reviewer's web search that I could not repeat. Re-baseline against the current Godot 4.7.x and Unity 6.x release notes before deciding anything on them.

1. **Ethanon-exact lit 2D.**
   - What exists: normal-mapped sprites, "standing" vertical sprites, a Blinn highlight with a fake eye, per-light clamping, up to 64 lights, game-supplied shadow strips, and CPU twins pinned by tests.
   - **Unchecked claim:** that Godot 4 and Unity URP 2D lack a "standing sprite" mode and Ethanon-exact per-light clamping. I believe so but have not verified it.
2. **Cheap lit 2D on a mid-range Android tablet.**
   - Measured on one device (README): menu 8.8 to 61.7 fps, level 2 27 to 61.7, level 1 27 to 76, from sprite-only pipelines plus dynamic resolution. `SPRITES_ONLY` alone is menu 117 to 75 ms, about 8.5 to 13 fps. Dynamic resolution supplies the rest.
   - With F7, P12 and Q27 this becomes systematic.
   - **Unchecked claim:** how Godot Mobile or Unity URP 2D do on the same Xiaomi Pad 5 scene. That has not been measured.
3. **Deterministic input replay with a hashed-state divergence check.**
   - Bit-hashed simulation, a text delta-encoded replay file with checkpoint hashes, divergence reported with tick and both hashes, `--fixed-step --screenshot-every`, and `tools/verify-frames.sh`.
   - **Narrowed claim.** Fixed-step offline capture is not unique: Godot has Movie Maker mode (`--write-movie`), a reviewer confirmed it and I recall it. Unity Recorder and Unreal's Movie Render Queue also cover frame-by-frame capture (my recollection, unchecked). The only piece I cannot find an equivalent for is input replay with state-hash divergence detection, and that too is unchecked. "Trailer capture" is dropped as a differentiator.
   - **Caveat:** rendering is outside the hash and the md5s are per-machine, so reproducibility holds on one driver, not across GPUs. Any temporal effect must use a frame-indexed jitter.
4. **Deterministic 2D GI (R10).**
   - Radiance cascades are noise-free with no history. As far as I know neither Godot 4.7 nor Unity 6.x ships built-in 2D GI.
   - Third-party radiance-cascade packages exist: a reviewer reports a paid Unity Asset Store package dated July 2026, Godot demos and a DiGRA 2026 benchmark (unchecked). So the pitch is a *built-in, deterministic* option, not a first.
   - It must be a separate mode because it cannot reproduce the per-light-clamped arithmetic that four games' tests pin.
5. **Tile-grid light propagation (Q25).** Integer propagation into tile vertex colour needs no shader and is replay-safe. I know of no built-in equivalent in the other engines (unchecked).
6. **3D units as lit 2.5D sprites (Q44).** It converts a skinned-geometry cost into a sprite fill cost, but sprite fill is the measured mobile bottleneck, so pair it with Q27.
7. **Per-game tiers.** Sprite-only, lit-no-IBL and mobile-3D pipelines let a small game pay only for what it draws. The proof point is 117 to 75 ms on the Adreno tablet, a single-device measurement.

**Parity, not stand-out.**
- Engine-generated 2D occluder shadows (Q45). Godot has `LightOccluder2D` (including a sprite-outline helper) and Unity URP 2D has Shadow Caster 2D (my recollection, a reviewer's search agrees, unchecked). Internal demand is documented as zero across 44 Wolf Brigade scenes.
- HDR output (R15) and area lights (Q17, Q38). Godot 4.7 reportedly ships both (secondary coverage, unchecked by me).

What will not stand out: matching Unreal-class systems, whatever they cost.

---

## 6. Rules that keep existing games safe, and how verification is split

1. **Default off means an identical command stream.**
   - New keys are add-only and written only when non-default. The pattern is `SceneSerializer.cpp:146–162`. Each key needs a `test_serialize` round trip. Penumbra byte-compares its writer to shipped scenes.
   - Package-level options (resolution tier, AA mode, `spritesOnlyScenePipelines`, `sceneSamples`) are `GameManifest` fields set by a game's main, not manifest text.
2. **New shader behaviour goes in new files, new pipelines or specialisation constants.**
   - The 17 existing blobs stay untouched where possible. CI byte-compares them.
   - Any edit to `shader.frag` or `scene_ubo.glsl` changes `frag.spv` and `vert.spv` bytes even when pictures are identical. A UBO change rebuilds six blobs. These are on the frozen list. Get the owner's explicit go (given once for `1b99158`, not standing), and batch edits: one UBO growth in F6 and one blob session per two or three `shader.frag` items.
3. **A new attachment means a new render pass and pipeline set** (Q29). Never alter the offscreen pass.
4. **Proof ladder, in order.**
   - (a) CI byte-compare of untouched blobs.
   - (b) Lavapipe md5 of AllPasses and MainScene identical with the feature off. These two scenes do not exercise IBL, probes, spots, non-casting suns, cutouts or the 2D lit path.
   - (c) **F13 fixtures, old build versus new build with the feature off:** zero differing pixels, md5s recorded at the pre-change commit. Then new build ON versus OFF: differences confined to the expected region (precedents: IBL 0 of 293,695 pixels; probe fixture 10,490 inside and 0 outside).
   - (d) A CPU twin test of the formula.
   - (e) Validation plus sync validation clean.
   - (f) The owner repeats the byte-diff on the real GPU and with MSVC. Adding code to a shared shader function can change driver contraction of unchanged arithmetic. The md5s are lavapipe pixels on one Mesa build, not a portable pin.
   - (g) Each game's own picture goldens at its pin (the games track 237, 83 and 112 PNGs; whether those are baselines was not checked).
5. **Determinism.**
   - Rendering stays outside `StateHash`.
   - No GPU readback feeds the simulation, and render code never uses the simulation RNG.
   - Particles keep today's global RNG order unless a seed is set.
   - Jitter and noise come from the frame counter or pixel position, never a clock.
   - History starts empty at frame 0 and resets on resize, dynamic-resolution steps, Play/Stop, scene load and camera cut.
   - Any component the simulation writes (MultiMesh, a terrain collider) must opt into the hash, with a mutation test.
6. **2D and DisplayEncoded scenes.** Never tone-map, TAA or SSAO them. Only display-space effects (LUT, vignette, grain) apply. Do not widen `SceneEncoding`. Exposure already scales DisplayEncoded.
7. **Android, Apple and Vulkan 1.1 devices keep today's path.**
   - Enable features only per tier via F5, including an Apple/MoltenVK tier. Items that add array or 3D images or ray tracing are unavailable or untested there.
   - Gate every full-screen pass.
   - **OFF must cost what it cost before, not only look the same.** Any bound-when-off texture or extra fetch in the PBR shader needs a specialisation constant (F7) or an optional descriptor set (F12). A tablet before/after check is part of acceptance. A 1×1 dummy texture on every device is not enough.
   - Effects are default-off on phones. `DynamicResolution` stays inactive for captures, fixed-step runs and replays.
8. **The shadow cache is a hidden contract.** Any new shadow-affecting state must be mixed into `ShadowPassSignature`. A still screenshot will not reveal a cache bug.
9. **Docs gate.** Each new suite needs `tests/CMakeLists.txt` registration, a README test-table row, the AGENTS.md suite count (66) and an `ARCHITECTURE.md` paragraph. New shaders must be in `SHADER_JOBS`.

**Verification split.**
- **Cloud box:** CPU twins; device-labelled suites and validation (once F8 exists); the md5 gate and F13 differentials; fixed-step repeatability; forced-caps fallbacks; plumbing checks (timestamps, ray-query structures; descriptor limits are not checked on lavapipe, which reports generous limits).
- **Owner's PC:** pinned-compiler blobs; real-GPU timing and byte-diff; MSVC; HDR monitor.
- **Tablet or phone, via a game repo:** every fragment-cost claim.
- **Penumbra's macOS runner:** the Apple leg. The iOS simulator is reported unable to draw the existing instanced batches.
- **Owner's eyes:** every "does it look right".

---

## 7. The first four things to do

Each is small enough to finish and prove, and in this order. Total **about 6–12 weeks** (5.7–12.4).

**1. Pre-flight, labels, GPU timestamps, and the 1080p measurement (F0, F1, F2; 1.7–4.2 weeks).**
- *Prove it:*
  - Validation and sync validation report 0 messages.
  - The AllPasses and MainScene md5s are unchanged.
  - GPU zones appear in the `--frames` output on the owner's PC.
  - One paragraph records GPU ms per pass at 1080p for AllPasses and a HUSK scene (and the tablet, if available).
- *Decision gate:* if the desktop GPU is idle at 1080p, depth-based effects are affordable. If not, do F7 tiers before anything else.

**2. A shader build path you can trust, and the fixture gate (F3, F13; 2.5–5 weeks).**
- Pick the blob route, run the scratch-versus-pinned equivalence on lavapipe, and record the pre-change md5s of the IBL, probe, spot, non-casting-sun, cutout, skinned and Light2D fixtures.
- *Prove it:* the existing md5s reproduce with a scratch-compiled build, and `tools/check.sh` fails if a fixture md5 moves.

**3. Reproduce and fix `castsShadow=false` (0.4–1 week; no shader, so it does not wait on F3).**
- Build the fixture (single sun with `castsShadow=false` plus a spot). Extract the gather decision into a pure function. Screenshot before and after.
- The fix is in `gatherLights` and changes the picture of affected scenes, so ship it behind an opt-in key that preserves today's behaviour. Decide per game whether it becomes the default.
- *Prove it:* a CPU test of the pure function, the non-casting-sun fixture md5 unchanged with the key off, and AllPasses and MainScene md5s unchanged (both have casting suns, so they cannot show a regression in the affected case).

**4. Composite v2 (P1; 1.1–2.2 weeks).**
- Tone-map operators (opt-in exact sRGB encode) and finishing, as a separate pipeline variant, default off, with keys written only when non-default. Widen `BloomPass::Params` directly.
- *Prove it:*
  - Default blobs byte-identical in CI.
  - md5s unchanged.
  - Curve mirrors within ±1–2/255, and a banding ramp.
  - `test_screenoverlay` still passes.
  - The owner picks the look on HUSK.

**Alongside, because they cost hours:**
- Warn on unsupported `KHR_materials_*`.
- Palette overflow log (the logging half of P9).
- Correct `ARCHITECTURE.md:3371`, which wrongly says `ParticleSystem` holds no static state.
- Fix the stale 128-byte comments.
- Cross-check `kSamplersPerSceneSet` against the layout (0.1–0.2 weeks).

Next, before any Tier 1 `shader.frag` item: F6 and F7 (one owner blob session for the UBO growth), then P5's shader half, P3, P4 and P6 in batches.

---

## 8. Open questions only the owner can answer

1. **Which picture is the target?** HUSK, a new 3D showcase, or the 2D games? Frontier features have no consumer in the four games as documented. Penumbra's genre and Wolf Brigade's encoding are not in the repo.
2. **How much time, in weekly hours, and how many owner sessions can you give?** The ranges are person-weeks of focused work. Elapsed time depends on blob builds, GPU and phone runs and look judgements.
3. **Will others use the engine?** That raises the value of hot reload, debug views, the capability layer and docs, and lowers the value of single-game hacks.
4. **Which GPU does verification use?** The 28 August doc names an RTX 3060 Laptop. The 11 November handoff's device run reports an integrated Radeon 780M. It decides whether DLSS, XeSS, ray tracing and volumetric tuning can happen locally.
5. **Where do blobs get built?** The owner's PC each time, or a dispatched CI job that uploads artifacts. And will the owner approve changing the frozen `frag.spv` in batched sessions, as was done for `1b99158`?
6. **Is a two-path engine acceptable?** A desktop tier with extra passes alongside today's path for phones and 1.1 devices. Is it acceptable to drop MSAA when TAA is on?
7. **Who judges acceptance of effects?** TAA ghosting, AO halos and fog tuning are decided by looking. Does the owner want a reference-image or metric gate for the ON paths beyond md5 equality of the OFF path?
8. **Should the Vulkan floor stay at 1.1-compatible?** It rules out dynamic rendering and depth resolve without a fallback.
9. **Full git history.** If the owner can consult it, the multi-pass items (F9, F10, R1) can be re-calibrated against how long clustered lighting, cascades and IBL really took.
10. **Phone runs.** Is a documented game-repo branch recipe enough, or should the engine carry a minimal Android sample for its own device checks?
11. **Competitor baseline.** Can the owner confirm the current Godot 4.7.x and Unity 6.x feature lists (HDR output, area lights, 2D occluders, 2D GI) from the official notes? Sections 1 and 5 depend on them and I could not.

---

## 9. Citations

Confidence is mine. "Web-checked" means a verifier confirmed the reference exists as cited. "Unchecked" means judged only from memory. A flag means it should not be treated as settled. Competitor feature claims elsewhere in this document are **not citations and remain unchecked recollections or reviewer search findings**.

*Lighting, GI, AO*
- Mitchell, McTaggart, Green, "Shading in Valve's Source Engine", SIGGRAPH 2006 course "Advanced Real-Time Rendering in 3D Graphics and Games" (ambient cube). High, unchecked.
- Lagarde and de Rousiers, "Moving Frostbite to Physically Based Rendering 3.0", SIGGRAPH 2014 course notes. High, unchecked.
- Jimenez, Wu, Pesce, Jarabo, "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016 course "Physically Based Shading in Theory and Practice" (GTAO, multi-bounce, specular occlusion). High, unchecked.
- Bavoil, Sainz, Dimitrov, "Image-Space Horizon-Based Ambient Occlusion", SIGGRAPH 2008 Talks. Medium, unchecked.
- Intel GameTechDev XeGTAO (2021). Medium. A reviewer web-checked the repository page: MIT licence, copyright 2021 Intel. Re-confirm before porting.
- Lagarde and Zanuttini, "Local Image-based Lighting With Parallax-corrected Cubemap", SIGGRAPH 2012 Talks. High, web-checked (the indexed title uses "cubemaps").
- Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course. High, unchecked.
- Ramamoorthi and Hanrahan, "An Efficient Representation for Irradiance Environment Maps", SIGGRAPH 2001. High, unchecked.
- Majercik, Guertin, Nowrouzezahrai, McGuire, DDGI, JCGT 8(2), 2019. High, unchecked.
- Majercik, Marrs, Spjut, McGuire, "Scaling Probe-Based Real-Time Dynamic Global Illumination for Production", JCGT 10(2), 2021. High, web-checked.
- Sannikov, "Radiance Cascades", informal draft, 2023. Medium, web-checked as existing. **Flagged:** not peer-reviewed. Do not confuse with the unrelated arXiv 2408.14425. The "used in Path of Exile 2" claim is secondary-source only.
- "Holographic Radiance Cascades for 2D Global Illumination", arXiv 2505.02041 (2025). Medium-low, **unchecked**: the identifier comes from a reviewer's search and I did not verify it.
- Rong and Tan, "Jump Flooding in GPU with Applications to Voronoi Diagram and Distance Transform", I3D 2006. High, unchecked.
- Crassin et al., "Interactive Indirect Illumination Using Voxel Cone Tracing", Computer Graphics Forum 30(7), 2011. High, unchecked.
- Wright, Narkowicz, Kelly, "Lumen: Real-time Global Illumination in Unreal Engine 5", SIGGRAPH 2022 Advances course. Medium (exact title uncertain), unchecked.
- Bitterli et al., "Spatiotemporal Reservoir Resampling for Real-time Ray Tracing with Dynamic Direct Lighting", ACM TOG 39(4), SIGGRAPH 2020. High, unchecked. **This is ReSTIR for direct lighting**, not GI.
- Müller, Rousselle, Novák, Keller, "Real-time Neural Radiance Caching for Path Tracing", ACM TOG 40(4), SIGGRAPH 2021. High, unchecked.

*Shadows*
- Fernando, "Percentage-Closer Soft Shadows", SIGGRAPH 2005 Sketches. High, unchecked.
- Enderton et al., "Stochastic Transparency", I3D 2010; McGuire and Enderton, "Colored Stochastic Shadow Maps", I3D 2011. High, unchecked.
- Heitz, Hill, McGuire, "Combining Analytic Direct Illumination and Stochastic Shadows", I3D 2018. High, web-checked.
- Karis, Stubbe, Wihlidal, "A Deep Dive into Nanite Virtualized Geometry", SIGGRAPH 2021 Advances course. High, web-checked.

*Anti-aliasing, temporal, upscaling*
- Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014 Advances course. High, unchecked.
- Pedersen, "Temporal Reprojection Anti-Aliasing in INSIDE", GDC 2016. High, unchecked (title from memory; an earlier note dropping this citation is withdrawn).
- Yang, Liu, Salvi, "A Survey of Temporal Antialiasing Techniques", Computer Graphics Forum 39(2), 2020. High, unchecked.
- Jimenez et al., "SMAA: Enhanced Subpixel Morphological Antialiasing", Computer Graphics Forum 31(2), Eurographics 2012. High, web-checked.
- Lottes, "FXAA", NVIDIA white paper, 2009. Medium (date from secondary sources only).
- AMD GPUOpen FidelityFX Super Resolution 1.0 (EASU and RCAS), June 2021, MIT. High, web-checked. FSR2/3-era FidelityFX source: MIT as recalled. A reviewer reports the current SDK is MIT with exceptions for a handful of files, and that FSR 4's core is not confirmed open. Unchecked, so check the licence of the exact version.
- Qualcomm Snapdragon Game Super Resolution: SGSR 1 (spatial) announced April 2023, SGSR 2 (temporal) open-sourced October 2024, BSD-3-Clause. Reviewer-checked, medium.
- Kaplanyan, Hill, Patney, Lefohn, "Filtering Distributions of Normals for Shading Antialiasing", HPG 2016. High, unchecked.
- Tokuyoshi and Kaplanyan, "Improved Geometric Specular Antialiasing", I3D 2019 (web-confirmed), and "Stable Geometric Specular Antialiasing with Projected-Space NDF Filtering", JCGT 10(2), 2021 (web-confirmed). High.
- Golus, "Anti-aliased Alpha Test: The Esoteric Alpha To Coverage", blog article, 2017. Medium-high, **unchecked** (not peer-reviewed). It is the source of the `fwidth` alpha remap in Q18. Wyman and McGuire, "Hashed Alpha Testing", I3D 2017, High, unchecked, is the stochastic-threshold technique and is related but is not that remap.
- McGuire and Mara, "Efficient GPU Screen-Space Ray Tracing", JCGT 3(4), 2014. High, unchecked.

*Materials*
- Heitz, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs", JCGT 3(2), 2014. High, unchecked.
- Kulla and Conty, "Revisiting Physically Based Shading at Imageworks", SIGGRAPH 2017 course. High, unchecked.
- Fdez-Agüera, "A Multiple-Scattering Microfacet Model for Real-time Image-based Lighting", JCGT 8(1), 2019. High, web-confirmed. **Flagged:** the survey gave the author as "Fdez-Aguera, S."; the real author is Carmelo J. Fdez-Agüera.
- Estevez and Kulla, "Production Friendly Microfacet Sheen BRDF", SIGGRAPH 2017. High, unchecked.
- Belcour and Barla, "A Practical Extension to Microfacet Theory for the Modeling of Varying Iridescence", ACM TOG 36(4), 2017. High, unchecked.
- Heitz, Dupuy, Hill, Neubelt, "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines", ACM TOG 35(4), 2016. High, unchecked. LUT licences not checked.
- Karis 2013 (listed above) for the representative-point method.
- Khronos glTF 2.0 `KHR_materials_*` extension specifications. High. `KHR_materials_anisotropy` is marked ratified on its spec page (reviewer-checked); no ratification date is stated here.
- Khronos "PBR Neutral Tone Mapper", 2024. Medium, web-confirmed.

*Post and atmosphere*
- Reinhard et al., "Photographic Tone Reproduction for Digital Images", SIGGRAPH 2002. High, unchecked.
- Hable, "Uncharted 2: HDR Lighting", GDC 2010. High, unchecked.
- Fry, "High Dynamic Range Color Grading and Display in Frostbite", GDC 2017. High, unchecked.
- Narkowicz, "ACES Filmic Tone Mapping Curve", 2016 blog post. Medium, unchecked. Sobotka, AgX display transform, 2022–23. Medium, unchecked.
- Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014 Advances course. High, unchecked.
- Abadie, "A Life of a Bokeh", SIGGRAPH 2018. High, unchecked.
- Preetham, Shirley, Smits, "A Practical Analytic Model for Daylight", SIGGRAPH 1999. High, unchecked.
- Hosek and Wilkie, "An Analytic Model for Full Spectral Sky-Dome Radiance", ACM TOG 31(4), SIGGRAPH 2012. High, unchecked.
- Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020. High, unchecked.
- Bruneton and Neyret, "Precomputed Atmospheric Scattering", EGSR 2008. High, unchecked.
- Wronski, "Volumetric Fog: Unified Compute Shader Based Solution to Atmospheric Scattering", SIGGRAPH 2014. High, unchecked.
- Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015. High, unchecked.
- Mitchell, "Volumetric Light Scattering as a Post-Process", GPU Gems 3, ch. 13, 2007. High, unchecked.
- Schneider and Vos, "The Real-Time Volumetric Cloudscapes of Horizon: Zero Dawn", SIGGRAPH 2015. High, unchecked.

*Geometry, infrastructure, terrain*
- O'Donnell, "FrameGraph: Extensible Rendering Architecture in Frostbite", GDC 2017. High, web-checked.
- Andersson, "Terrain Rendering in Frostbite Using Procedural Shader Splatting", SIGGRAPH 2007 course. High, unchecked.
- Heitz and Neyret, "High-Performance By-Example Noise using a Histogram-Preserving Blending Operator", HPG 2018. High, unchecked.
- Kapoulkine, meshoptimizer (software, MIT licence). High.

**Dropped as unreliable:** Epic's "Virtual Shadow Maps" documentation entry (low confidence, no URL). The claims about Unity 6, Unreal and Godot feature sets are not citations and remain unchecked.

---

## What changed after review

Three reviewers raised one blocker and 21 major issues, grouped below, plus about two dozen minor ones. I checked each repo claim that carries a decision against the code. Items marked unchecked rest on a reviewer's web search or news coverage that I could not repeat.

**Blocker**
- **No way to stay inside the 16-sampler floor** (R1). *Fixed.* Added F12, a Tier 0 item for the descriptor budget and per-tier layout variants (2–3.5 weeks). P7, Q1, Q3, Q7, Q11, Q38, R5 and R8b now depend on it. I added a running sampler count and rewrote rule 7 so OFF must cost what it cost before. The 13 of 16 base was verified against `test_materials.cpp:1594`. The later per-item counts are the draft's own figures and are labelled as such.

**Major issues**
- **Path C and Path A omit their prerequisites** (all three reviewers). *Fixed.* Every path is now a dependency closure. C is 60–118 standalone and 38–73 on top of A and B (it was 42–87). A is 52–105 and includes F11, F12, F13 and F14. The union is about 105–212. The false "silently needs P12 and F6" sentence is gone.
- **Calibration anchors mis-scoped** (R1). *Fixed.* The replay-oracle "2–3x" is no longer used as a rendering multiplier, which I checked in the planning doc. I defined the unit and added owner-session counts (A 45–75, B 20–35, C on top 20–40). I cited verified datapoints: the 22 August and 28 August docs against root commit `494f6e2`, the three 2D light commits within about 22 hours, and the DynamicResolution sequence.
- **F7 should gate the Tier 1 shader items** (R1, R2). *Fixed.* F7 is a dependency of P3, P4, P6 and P7, with a named specialisation constant and a tablet before/after in each item's acceptance. The branch-not-taken cost is an inference from the README's 117 to 75 ms, not a measurement.
- **F6 must precede the items needing UBO space** (R1). *Fixed.* F6 gates P3, P4, P5 and P6. It now specifies a reserved block (at least 4 vec4 and one storage buffer) as a proposal to be checked.
- **P9 hides a dependency on Q4** (R1). *Fixed.* Q4 is merged into P9 (0.7–2 weeks) and Tier 1. I verified that the signature hash is byte-serial FNV-1a. The 1.2–1.4 ms figure is the reviewer's estimate, unmeasured, and says so.
- **F10's cheap route does not work** (R1, R2). *Fixed.* I verified front-face culling plus bias in the shadow pipeline, and `textureLod` in `shadow_cutout.frag`. The route is deleted. F10 is now 2.5–4.5 weeks with back-face no-bias pipelines, a cutout prepass blob and instanced replay. R4's normals are priced separately (+1–2, or depth-derived normals stated).
- **R1/R2/R8 hide a scene-pass and pipeline-set change** (R1). *Fixed.* Q29 is promoted to a dependency of R1, R2 and R8b. F11 is added to R2. R8 states whether compute skinning is included (it is not, +2.5–4.5). FidelityFX's shader toolchain is flagged to be checked against F3 before R2 is costed.
- **The first three things skipped F3** (R1, R2). *Fixed.* Section 7 is now four steps: F0/F1/F2, then F3 plus F13, then the `castsShadow` fix, then P1. The total is 6–12 weeks (it was 2.6–6).
- **P11's polygon editor is unpriced** (R1) and **occluder shadows are parity, not a differentiator** (R3). *Fixed.* P11 became Q45 in Tier 2, rated payoff 2. It derives occluders from collider hulls or sprite silhouettes. The editor is +1–2 and optional. Section 5 moves it to "parity". I verified the repo's "Zero across 44 Wolf Brigade scenes" line. The Godot and Unity capabilities are my recollection plus a reviewer search, unchecked.
- **Ray-query shadows filed under "not as hard" yet priced at 10–24 weeks** (R2, R3). *Fixed.* Removed from "not as hard" and added to "harder". R8 is split into R8a (5–10) and R8b (+6–12). Path C uses R8a only.
- **The md5 gate cannot show "unchanged" for IBL, probes, spots, cutouts, 2D-lit or non-casting suns** (R2). *Fixed.* I verified AllPasses has an empty `Hdri` and only a directional plus two points. Added F13 (fixture scenes with pre-change md5s in `check.sh`) and restated rung (c) as old build versus new build with the feature off.
- **R15 HDR output ignores the UNORM swapchain and ImGui UI** (R2). *Fixed.* I verified the swapchain format selection. R15 is now 3–6 weeks and depends on P12 or F9 and on F5. The UI conversion stage is spelled out.
- **Tablet numbers misquoted** (R3). *Fixed.* I verified against the README: menu 8.8 to 61.7, level 2 27 to 61.7, level 1 27 to 76, and `SPRITES_ONLY` alone 117 to 75 ms. The "2 to 63" figure was wrong.
- **"Parity with Godot/URP" overstated** (R3). *Fixed.* Path A is renamed "the look layer". Full Forward+/URP parity is priced separately as A plus roughly 31–62 weeks, and more with GI. The competitor feature lists remain unchecked.
- **Lumen/ReSTIR bullet wrong** (R3). *Fixed.* I removed "needs RT hardware" from Lumen and fixed the contradiction with "cannot be judged in the cloud" (lavapipe exposes ray query). Bitterli 2020 is relabelled as direct lighting, the NRC paper is cited separately, and the FidelityFX radiance-caching point is noted as unchecked.
- **Out-of-reach cost figures are bare guesses** (R3). *Partly accepted.* All of them are now labelled unvalidated, with their assumptions stated. The Bevy precedents are mentioned as unchecked. I did **not** lower the numbers, because those precedents had compute and bindless in place and this engine does not, and I cannot derive a better figure.
- **DLSS/XeSS "vendor-locked and unverifiable"** (R3). *Fixed.* Split R3 into DLSS (RTX only) and XeSS (reportedly a DP4a path, Vulkan SR). Redistribution terms must be read in both cases. The XeSS claim is unchecked.

**Rejected or only partly accepted**
- **Round every per-item range to whole weeks** (R3). *Rejected.* Sums would drift and become unreproducible. I stated that the decimals are bookkeeping, rounded the headline totals, defined Pay as judgement, and flagged the AO-first ordering as judgement.
- **Re-baseline to Godot 4.7.x and Unity 6.x** (R3). *Accepted as a warning, not as fact.* I could not reach the official notes. R15, Q17 and Q38 are marked "likely parity (unchecked)", and the re-baseline is an open question for the owner.
- **Engine-owned Android device harness** (R1). *Partly accepted.* F0 now includes a documented game-repo recipe. A minimal in-repo sample is not planned, since AGENTS.md puts the Android and Apple builds in the games' repos. It is an open question (section 8, item 10).
- **Minimum tag changes.** Accepted all of them (E added to R4, P3, Q2, Q15, Q18, Q23, R6; PC to R10).

**Smaller fixes**
- F3 now prices the scratch-versus-pinned md5 equivalence and the blob batching rule.
- F9 is re-scoped to descriptors and pass order and runs before P2 and P12. `BloomPass::Params` is widened directly (the push range is `sizeof(Params)`, verified).
- F7 is repriced (1–3) against `1b99158` (+12 shader lines, +629/−6 total, verified), and the "frozen" wording now says the owner's go is needed each time.
- The 3D showcase scene is split from F0 into F14 (1–2 weeks, with asset licensing). The Apple/MoltenVK tier is added to F5, rule 7 and the verification split.
- Q6 is re-costed at 4–7 weeks for the `DrawFrame` view split.
- P5 treats shadow distance and lambda as plumbing, since they already exist in `ShadowCascades` (verified).
- The `castsShadow` fix is confirmed C++-only and ships behind an opt-in key.
- Q18's `fwidth` remap is re-attributed to Golus 2017.
- Section 9 resolves the Pedersen contradiction and the XeGTAO, SGSR and `KHR_materials_anisotropy` notes. It adds Preetham 1999, Hosek-Wilkie 2012 and the NRC paper.
- My own correction: the draft's R12 "10–18 cumulative" contradicted its own sum of R11 + R12 and is dropped.

**Still unchecked after review:**
- every competitor capability claim, including Godot 4.7 and Unity 6.x features, Movie Maker mode, 2D occluders, HDR and area lights, and the third-party radiance-cascade packages;
- the XeSS DP4a and Vulkan claims;
- the FidelityFX current-SDK licence exceptions;
- the arXiv 2505.02041 identifier;
- the iOS simulator base-instance limit;
- the 1.2–1.4 ms palette-hash estimate;
- every citation marked "unchecked" in section 9.