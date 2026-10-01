// Regression tests for skeletal animation.
//
// Run against assets/models/bender.gltf, a fixture authored to be awkward in the
// three ways this code most easily gets wrong:
//
//   - skin.joints lists the CHILD before its parent, so a loader that evaluates
//     in file order reads an uninitialised parent and poses garbage;
//   - JOINTS_0 is UNSIGNED_BYTE, which is never float - copying the float-only
//     guard the UV path uses leaves every index and weight at zero, and every
//     vertex collapses onto the origin with nothing logged anywhere;
//   - the skinned mesh node carries a non-identity transform, which the glTF
//     spec says must be IGNORED; baking it in double-transforms the mesh.
//
// bender_u16.gltf is the same rig with normalised UNSIGNED_SHORT weights.

#include "TestHarness.hpp"
#include "core/AnimationLibrary.hpp"
#include "core/AnimationSystem.hpp"
#include "core/Components.hpp"
#include "core/GltfLoader.hpp"

#include <glm/gtc/epsilon.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace Supersonic;

static const std::string kFixture = "assets/models/bender.gltf";
static const std::string kFixtureU16 = "assets/models/bender_u16.gltf";

static void testVertexLayoutIsWhatThePipelineDeclares() {
    // The static_asserts in Components.hpp cover the offsets; this covers the
    // attribute descriptions the pipeline is actually built from, which are what
    // a stale shader would silently disagree with.
    const auto attributes = Vertex::getAttributeDescriptions();
    CHECK_EQ(attributes.size(), size_t{7});

    CHECK_EQ(attributes[5].location, 5u);
    CHECK_MSG(attributes[5].format == vk::Format::eR8G8B8A8Uint,
              "joint indices must be an integer format, so the shader declares uvec4");
    CHECK_EQ(attributes[5].offset, static_cast<uint32_t>(offsetof(Vertex, jointIndices)));

    CHECK_EQ(attributes[6].location, 6u);
    CHECK_MSG(attributes[6].format == vk::Format::eR32G32B32A32Sfloat, "weights are float");

    CHECK_EQ(Vertex::getBindingDescription().stride, static_cast<uint32_t>(sizeof(Vertex)));
}

static void testFixtureLoads() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK_MSG(scene.ok, scene.error);
    CHECK_EQ(scene.submeshes.size(), size_t{1});
    CHECK_EQ(scene.skeletons.size(), size_t{1});
    CHECK_MSG(!scene.clips.empty(), "the fixture's animation must be imported");
    CHECK_EQ(scene.submeshes.front().skinIndex, 0);
}

static void testJointsAreReorderedParentBeforeChild() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.skeletons.empty()) return;

    const Skeleton& skeleton = scene.skeletons.front();
    CHECK_EQ(skeleton.joints.size(), size_t{2});

    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const int32_t parent = skeleton.joints[i].parent;
        CHECK_MSG(parent < static_cast<int32_t>(i),
                  "joint " + std::to_string(i) + " must come after its parent, or a single "
                  "forward pass reads an uninitialised matrix");
    }

    // The fixture lists Upper (the child) first, so a loader that did not
    // reorder would leave "Upper" at index 0.
    CHECK_MSG(skeleton.joints[0].name == "Lower",
              "the root must be first after reordering, got '" + skeleton.joints[0].name + "'");
    CHECK_MSG(skeleton.joints[1].name == "Upper", "and the child second");
    CHECK_EQ(skeleton.joints[0].parent, -1);
    CHECK_EQ(skeleton.joints[1].parent, 0);
}

static void testJointIndicesAndWeightsAreRead() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.submeshes.empty()) return;

    const auto& vertices = scene.submeshes.front().mesh.vertices;
    CHECK_EQ(vertices.size(), size_t{12});

    float totalWeight = 0.0f;
    bool sawUpperJoint = false;
    for (const auto& v : vertices) {
        const float sum = v.jointWeights.x + v.jointWeights.y + v.jointWeights.z + v.jointWeights.w;
        totalWeight += sum;
        CHECK_MSG(std::fabs(sum - 1.0f) < 1e-3f, "weights must be renormalised to one");
        if (v.jointIndices.y == 1 && v.jointWeights.y > 0.0f) sawUpperJoint = true;
    }
    CHECK_MSG(totalWeight > 11.0f, "weights must not all be zero - that is the float-only-guard bug");
    CHECK_MSG(sawUpperJoint, "the second joint must actually influence something");

    // The middle ring blends the two joints half and half.
    CHECK_NEAR(vertices[4].jointWeights.x, 0.5f);
    CHECK_NEAR(vertices[4].jointWeights.y, 0.5f);
}

static void testNormalisedShortWeightsMatchFloatOnes() {
    const GltfLoader::Scene asFloat = GltfLoader::Load(kFixture);
    const GltfLoader::Scene asShort = GltfLoader::Load(kFixtureU16);
    CHECK(asFloat.ok);
    CHECK_MSG(asShort.ok, asShort.error);
    if (asFloat.submeshes.empty() || asShort.submeshes.empty()) return;

    const auto& a = asFloat.submeshes.front().mesh.vertices;
    const auto& b = asShort.submeshes.front().mesh.vertices;
    CHECK_EQ(a.size(), b.size());

    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        // Reading normalised shorts as raw floats would be off by orders of
        // magnitude, not by a rounding error.
        CHECK_MSG(std::fabs(a[i].jointWeights.x - b[i].jointWeights.x) < 1e-3f,
                  "normalised short weights must decode to the same values as float ones");
        CHECK_MSG(std::fabs(a[i].jointWeights.y - b[i].jointWeights.y) < 1e-3f, "same for the second influence");
    }
}

static void testSkinnedPrimitiveIgnoresItsNodeTransform() {
    // The fixture's mesh node sits at (5, 3, -2). The spec says a skinned mesh
    // node's own transform is ignored, so the vertices must still be around the
    // origin - baking it in would put them 5 units out and pose them relative to
    // a skeleton that is not there.
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.submeshes.empty()) return;

    const MeshData& mesh = scene.submeshes.front().mesh;
    CHECK_MSG(mesh.boundsMin.x > -1.0f && mesh.boundsMax.x < 1.0f,
              "a skinned primitive must not be baked into its node's world space");
    CHECK_MSG(mesh.boundsMax.y > 1.9f && mesh.boundsMax.y < 2.1f, "and must keep its own height");
}

static void testAllThreeInterpolationModesImport() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.clips.empty()) return;

    const AnimationClip& clip = scene.clips.front();
    CHECK_MSG(clip.name == "Bend", "clip name must survive import, got '" + clip.name + "'");
    CHECK_NEAR(clip.duration, 2.0f);
    CHECK_EQ(clip.channels.size(), size_t{3});

    bool linear = false, step = false, spline = false;
    for (const auto& channel : clip.channels) {
        if (channel.interpolation == AnimInterpolation::Linear) linear = true;
        if (channel.interpolation == AnimInterpolation::Step) step = true;
        if (channel.interpolation == AnimInterpolation::CubicSpline) spline = true;

        CHECK_MSG(channel.joint >= 0 && channel.joint < 2,
                  "every channel must be remapped onto a joint of this skin");
    }
    CHECK_MSG(linear, "LINEAR channel missing");
    CHECK_MSG(step, "STEP channel missing");
    CHECK_MSG(spline, "CUBICSPLINE channel missing");
}

static void testStepInterpolationHolds() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.clips.empty() || scene.skeletons.empty()) return;

    const Skeleton& skeleton = scene.skeletons.front();
    std::vector<glm::mat4> locals;

    // The STEP channel translates the root to y = 0.25 at t = 1 and back at
    // t = 2. Halfway between the keys it must still read the EARLIER value, not
    // an interpolated one.
    AnimationSystem::SampleClip(skeleton, scene.clips.front(), 1.5f, locals);
    CHECK_EQ(locals.size(), size_t{2});
    CHECK_NEAR(locals[0][3][1], 0.25f);

    AnimationSystem::SampleClip(skeleton, scene.clips.front(), 0.5f, locals);
    CHECK_MSG(std::fabs(locals[0][3][1]) < 1e-4f, "before the first key a STEP channel holds the first value");
}

static void testRotationInterpolatesSmoothly() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.clips.empty() || scene.skeletons.empty()) return;

    const Skeleton& skeleton = scene.skeletons.front();
    std::vector<glm::mat4> atZero, atHalf, atOne;

    AnimationSystem::SampleClip(skeleton, scene.clips.front(), 0.0f, atZero);
    AnimationSystem::SampleClip(skeleton, scene.clips.front(), 0.5f, atHalf);
    AnimationSystem::SampleClip(skeleton, scene.clips.front(), 1.0f, atOne);

    // The child joint rotates 0 -> 50 -> 0 degrees about Z. For a Z rotation the
    // first basis column is (cos, sin, 0), so its y component is sin of the
    // angle - once the column is normalised. Normalising is not optional here:
    // the CUBICSPLINE channel scales this same joint, and comparing the raw
    // element would be testing rotation and scale multiplied together.
    const auto rotationSine = [](const glm::mat4& m) {
        const glm::vec3 column(m[0]);
        const float length = glm::length(column);
        return length > 1e-6f ? column.y / length : 0.0f;
    };

    const float zero = rotationSine(atZero[1]);
    const float half = rotationSine(atHalf[1]);
    const float one = rotationSine(atOne[1]);

    CHECK_MSG(std::fabs(zero) < 1e-3f, "the first key is an identity rotation");
    CHECK_MSG(half > zero + 0.05f, "the pose must actually move between keys");
    CHECK_MSG(one > half, "and keep moving to the second key");
    CHECK_NEAR(one, std::sin(glm::radians(50.0f)));

    // And the spline channel really is driving the scale at that key: 1.3 on X
    // and Z, 1.0 on Y.
    CHECK_NEAR(glm::length(glm::vec3(atOne[1][0])), 1.3f);
    CHECK_NEAR(glm::length(glm::vec3(atOne[1][1])), 1.0f);
}

static void testPoseCompositionInheritsFromTheParent() {
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.clips.empty() || scene.skeletons.empty()) return;

    const Skeleton& skeleton = scene.skeletons.front();
    std::vector<glm::mat4> locals, matrices;

    // At t = 1 the root is translated 0.25 up by the STEP channel. The child sits
    // a unit above it, so with the inverse bind applied its matrix must carry the
    // parent's translation - that is the whole point of composing parent-first.
    AnimationSystem::SampleClip(skeleton, scene.clips.front(), 1.0f, locals);
    AnimationSystem::ComposePose(skeleton, locals, matrices);

    CHECK_EQ(matrices.size(), size_t{2});
    CHECK_NEAR(matrices[0][3][1], 0.25f);

    const glm::vec4 posed = matrices[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    CHECK_MSG(posed.y > 1.0f, "the child joint must inherit the parent's rise");
}

static void testBindPoseIsTheIdentityAtRest() {
    // A rig with no animation applied must reproduce its bind pose exactly, or
    // every mesh is subtly deformed before anything has moved.
    const GltfLoader::Scene scene = GltfLoader::Load(kFixture);
    CHECK(scene.ok);
    if (scene.skeletons.empty()) return;

    const Skeleton& skeleton = scene.skeletons.front();
    std::vector<glm::mat4> locals(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        locals[i] = glm::translate(glm::mat4(1.0f), skeleton.joints[i].restTranslation)
                  * glm::mat4_cast(skeleton.joints[i].restRotation)
                  * glm::scale(glm::mat4(1.0f), skeleton.joints[i].restScale);
    }

    std::vector<glm::mat4> matrices;
    AnimationSystem::ComposePose(skeleton, locals, matrices);

    for (size_t j = 0; j < matrices.size(); ++j) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                const float expected = (column == row) ? 1.0f : 0.0f;
                if (std::fabs(matrices[j][column][row] - expected) > 1e-4f) {
                    CHECK_MSG(false, "joint " + std::to_string(j) + " is not identity at bind pose");
                    return;
                }
            }
        }
    }
    CHECK(true);
}

static void testLibraryCachesHitsAndMisses() {
    AnimationLibrary library;

    const uint32_t first = library.Acquire(kFixture);
    CHECK_MSG(first != AnimationLibrary::kInvalidSkeleton, "the fixture has a rig");
    CHECK_EQ(library.Acquire(kFixture), first);
    CHECK_EQ(library.Size(), size_t{1});

    // A rigless file must be remembered as a miss, or it is re-parsed from disk
    // every frame for as long as it is in the scene.
    const uint32_t missing = library.Acquire("assets/models/monument.gltf");
    CHECK_EQ(missing, AnimationLibrary::kInvalidSkeleton);
    CHECK_EQ(library.Acquire("assets/models/monument.gltf"), AnimationLibrary::kInvalidSkeleton);
    CHECK_EQ(library.Size(), size_t{1});

    CHECK_MSG(library.FindClip(first, "Bend") != nullptr, "named lookup must find the clip");
    CHECK_MSG(library.FindClip(first, "") != nullptr, "an empty name falls back to the first clip");
    CHECK_MSG(library.FindClip(first, "NoSuchClip") == nullptr,
              "a mistyped name must leave the mesh in bind pose, not play something else");
}

static entt::entity makeAnimated(entt::registry& registry, AnimationLibrary& library) {
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Bender");
    registry.emplace<TransformComponent>(entity);
    auto& mesh = registry.emplace<MeshComponent>(entity);
    mesh.filePath = kFixture;
    registry.emplace<AnimatorComponent>(entity);
    registry.emplace<RenderableComponent>(entity);

    AnimationSystem::SyncSkeletons(registry, library);
    return entity;
}

static void testSyncSkeletonsResolvesTheRig() {
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimated(registry, library);

    CHECK_MSG(registry.all_of<SkinnedMeshComponent>(entity), "an animated mesh must gain a rig");
    const auto& skin = registry.get<SkinnedMeshComponent>(entity);
    CHECK_EQ(skin.jointMatrices.size(), size_t{2});

    // A mesh with no rig must lose the derived component rather than keep a
    // stale one.
    registry.get<MeshComponent>(entity).filePath = "assets/models/monument.gltf";
    AnimationSystem::SyncSkeletons(registry, library);
    CHECK_MSG(!registry.all_of<SkinnedMeshComponent>(entity),
              "switching to a rigless mesh must drop the skin");
}

static void testClockLoopsAndRunsBackwards() {
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimated(registry, library);

    auto& animator = registry.get<AnimatorComponent>(entity);
    animator.clipName = "Bend";
    animator.time = 1.9f;

    AnimationSystem::Advance(registry, library, 0.5f);
    CHECK_MSG(animator.time < 1.0f, "the clock must wrap at the end of the clip");
    CHECK_MSG(animator.time >= 0.0f, "and stay positive");

    // fmod keeps the sign of the dividend, so a negative speed walks off into
    // negative time unless the wrap adds the duration back.
    animator.time = 0.1f;
    animator.speed = -1.0f;
    AnimationSystem::Advance(registry, library, 0.5f);
    CHECK_MSG(animator.time > 1.0f, "playing backwards must wrap to the end, not clamp to zero");

    animator.loop = false;
    animator.speed = 1.0f;
    animator.time = 1.9f;
    AnimationSystem::Advance(registry, library, 5.0f);
    CHECK_NEAR(animator.time, 2.0f);
}

static void testPoseBoundsCoverTheAnimation() {
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimated(registry, library);

    auto& renderable = registry.get<RenderableComponent>(entity);
    renderable.localBoundsMin = glm::vec3(-0.35f, 0.0f, -0.35f);
    renderable.localBoundsMax = glm::vec3(0.35f, 2.0f, 0.35f);
    const glm::vec3 bindMax = renderable.localBoundsMax;

    auto& animator = registry.get<AnimatorComponent>(entity);
    animator.clipName = "Bend";
    animator.time = 1.0f;   // fully bent and raised
    AnimationSystem::EvaluatePoses(registry, library);

    CHECK_MSG(renderable.localBoundsMax.x > bindMax.x || renderable.localBoundsMin.x < -0.35f,
              "a bent pose reaches outside the bind-pose box, and the bounds must follow - "
              "otherwise the character vanishes mid-animation");
    CHECK_MSG(renderable.localBoundsMax.y >= bindMax.y, "the bounds must never shrink below bind pose");
}

static void testPoseBoundsNeverShrinkBelowTheBindBox() {
    // The claim the line above tries to make, moved somewhere it can fail.
    //
    // `localBoundsMax = glm::max(bindMax, posedMax)` is the implementation, so
    // asserting `localBoundsMax.y >= bindMax.y` against the box the same call
    // just read is true whatever the pose did - it cannot fail, and a check that
    // cannot fail is a check that is not being made. That one is left where it
    // is because it reads as an intention; this is the version with a way to go
    // wrong in it.
    //
    // A bind box far LARGER than anything the pose reaches. The union has to
    // keep it. An implementation that assigned the posed extent instead of
    // taking the maximum would shrink it to roughly the rig's own size, and
    // every character in the project would start being culled the moment its
    // authored bounds mattered - which is the failure the union exists for.
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimated(registry, library);

    auto& renderable = registry.get<RenderableComponent>(entity);
    renderable.localBoundsMin = glm::vec3(-50.0f);
    renderable.localBoundsMax = glm::vec3(50.0f);

    auto& animator = registry.get<AnimatorComponent>(entity);
    animator.clipName = "Bend";
    animator.time = 1.0f;
    AnimationSystem::EvaluatePoses(registry, library);

    CHECK_MSG(renderable.localBoundsMax.x >= 50.0f && renderable.localBoundsMax.y >= 50.0f &&
                  renderable.localBoundsMax.z >= 50.0f,
              "a pose smaller than the authored box must not shrink it");
    CHECK_MSG(renderable.localBoundsMin.x <= -50.0f && renderable.localBoundsMin.y <= -50.0f &&
                  renderable.localBoundsMin.z <= -50.0f,
              "at either end");
}

static void testPoseBoundsDoNotAccumulate() {
    // The pose bounds are the union of the bind box carried by each joint. Union
    // that into the CURRENT bounds and the result feeds back into itself, growing
    // a little every frame until the pick box swallows the space around the
    // character. Safe today only because SyncResources happens to reset the
    // bounds each frame - so the invariant is pinned here rather than left to
    // that ordering.
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimated(registry, library);

    auto& renderable = registry.get<RenderableComponent>(entity);
    renderable.localBoundsMin = glm::vec3(-0.35f, 0.0f, -0.35f);
    renderable.localBoundsMax = glm::vec3(0.35f, 2.0f, 0.35f);

    auto& animator = registry.get<AnimatorComponent>(entity);
    animator.clipName = "Bend";
    animator.time = 1.0f;

    AnimationSystem::EvaluatePoses(registry, library);
    const glm::vec3 firstMin = renderable.localBoundsMin;
    const glm::vec3 firstMax = renderable.localBoundsMax;

    for (int i = 0; i < 100; ++i) {
        AnimationSystem::EvaluatePoses(registry, library);
    }

    CHECK_MSG(glm::all(glm::epsilonEqual(renderable.localBoundsMin, firstMin, 1e-5f)),
              "evaluating the same pose again must not widen the bounds");
    CHECK_MSG(glm::all(glm::epsilonEqual(renderable.localBoundsMax, firstMax, 1e-5f)),
              "and the maximum must be just as stable");
}

static void testPaletteGatherPacksAndBoundsCheck() {
    entt::registry registry;
    AnimationLibrary library;
    makeAnimated(registry, library);
    makeAnimated(registry, library);

    std::vector<glm::mat4> palette;
    const uint32_t written = AnimationSystem::GatherPalettes(registry, palette, 1024);

    CHECK_EQ(written, 4u);   // two entities, two joints each
    CHECK_EQ(palette.size(), size_t{4});

    std::vector<int32_t> bases;
    for (auto entity : registry.view<SkinnedMeshComponent>()) {
        bases.push_back(registry.get<SkinnedMeshComponent>(entity).paletteBase);
    }
    CHECK_EQ(bases.size(), size_t{2});
    CHECK_MSG(bases[0] != bases[1], "each entity must get its own slice");

    // Too small to hold either entity: both must opt out rather than write past
    // the end of the buffer, which on a device without robustBufferAccess is a
    // device loss rather than a zeroed read.
    const uint32_t tiny = AnimationSystem::GatherPalettes(registry, palette, 1);
    CHECK_EQ(tiny, 0u);
    for (auto entity : registry.view<SkinnedMeshComponent>()) {
        CHECK_EQ(registry.get<SkinnedMeshComponent>(entity).paletteBase, -1);
    }
}

static void testMissingFileIsHandled() {
    AnimationLibrary library;
    CHECK_EQ(library.Acquire("assets/models/does_not_exist.gltf"), AnimationLibrary::kInvalidSkeleton);
    CHECK_EQ(library.Acquire(""), AnimationLibrary::kInvalidSkeleton);
    CHECK_MSG(library.GetSkeleton(AnimationLibrary::kInvalidSkeleton) == nullptr,
              "an invalid id must not index the entry list");
}

// --- Hot reloading a rig ----------------------------------------------------
//
// A rig is re-exported constantly while a character is being made, and the
// library cached the first parse for the life of the session - so the animator
// saved, alt-tabbed, and saw the skeleton from ten minutes ago.
//
// The fixtures name their buffer by a RELATIVE uri, so a copy has to bring the
// .bin along or the loader reads a rig with no data and every case below turns
// into "the file did not load", which passes for the wrong reason.

static const std::string kReloadDir = "test_animreload_tmp";
static const std::string kReloadRig = kReloadDir + "/rig.gltf";

static void installRig(const std::string& gltf, const std::string& bin) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(kReloadDir, ec);
    fs::copy_file(gltf, kReloadRig, fs::copy_options::overwrite_existing, ec);
    fs::copy_file(bin, kReloadDir + "/" + fs::path(bin).filename().string(),
                  fs::copy_options::overwrite_existing, ec);
}

static void clearReloadDir() {
    std::error_code ec;
    std::filesystem::remove_all(kReloadDir, ec);
}

static void testReloadKeepsTheIdAndBumpsTheGeneration() {
    clearReloadDir();
    installRig(kFixture, "assets/models/bender.bin");

    AnimationLibrary library;
    const uint32_t id = library.Acquire(kReloadRig);
    CHECK_MSG(id != AnimationLibrary::kInvalidSkeleton, "the copied rig must load");
    const uint32_t before = library.GenerationOf(id);

    // Re-exported. Same rig, same file name - which is the whole point: an id
    // cannot tell this apart from nothing having happened.
    installRig(kFixture, "assets/models/bender.bin");
    CHECK_MSG(library.Reload(kReloadRig), "a valid rig must reload");

    // Every SkinnedMeshComponent in the scene is holding this number.
    CHECK_EQ(library.Acquire(kReloadRig), id);
    CHECK_MSG(library.GenerationOf(id) > before,
              "the generation must move, or nothing derived from the rig rebuilds");

    clearReloadDir();
}

static void testReloadingARiglessFileClearsTheCachedMiss() {
    // The headline case. A mesh exported without its skin is cached as a miss
    // so it is not re-parsed every frame; adding the skin and exporting again
    // then does nothing at all until the editor restarts.
    clearReloadDir();
    installRig("assets/models/monument.gltf", "assets/models/monument.bin");

    AnimationLibrary library;
    CHECK_EQ(library.Acquire(kReloadRig), AnimationLibrary::kInvalidSkeleton);

    installRig(kFixture, "assets/models/bender.bin");
    CHECK_MSG(library.Reload(kReloadRig), "a file that has gained a rig must reload");

    const uint32_t id = library.Acquire(kReloadRig);
    CHECK_MSG(id != AnimationLibrary::kInvalidSkeleton, "the rig must now resolve");
    CHECK_MSG(library.FindClip(id, "Bend") != nullptr, "and its clips must be there");

    clearReloadDir();
}

static void testAReloadThatLosesTheRigKeepsTheOldOne() {
    // A .glb caught half-written parses as a file with no skin. Dropping the
    // rig there would put the character into bind pose and leave it there,
    // because the next poll sees a file that has stopped changing.
    clearReloadDir();
    installRig(kFixture, "assets/models/bender.bin");

    AnimationLibrary library;
    const uint32_t id = library.Acquire(kReloadRig);
    const size_t joints = library.GetSkeleton(id) ? library.GetSkeleton(id)->joints.size() : 0;
    CHECK_MSG(joints > 0, "the rig must have joints to begin with");

    installRig("assets/models/monument.gltf", "assets/models/monument.bin");
    CHECK_MSG(!library.Reload(kReloadRig), "losing the skin must report failure");
    CHECK_EQ(library.GetSkeleton(id) ? library.GetSkeleton(id)->joints.size() : 0, joints);
    CHECK_MSG(library.FindClip(id, "Bend") != nullptr, "and the clips must survive too");

    clearReloadDir();
}

static void testReloadingARigNobodyAcquiredDoesNothing() {
    // Reload is driven by a watcher that also reports textures, meshes and
    // materials, so it is handed paths of every kind.
    AnimationLibrary library;
    CHECK_MSG(!library.Reload(kFixture), "reloading an unknown path must be a no-op");
    CHECK_EQ(library.Size(), size_t{0});
}

static void testAReloadedRigRebuildsWhatWasDerivedFromIt() {
    clearReloadDir();
    installRig(kFixture, "assets/models/bender.bin");

    AnimationLibrary library;
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Bender");
    registry.emplace<TransformComponent>(entity);
    registry.emplace<MeshComponent>(entity).filePath = kReloadRig;
    registry.emplace<AnimatorComponent>(entity);

    AnimationSystem::SyncSkeletons(registry, library);
    auto& skin = registry.get<SkinnedMeshComponent>(entity);
    const uint32_t idBefore = skin.skeletonID;

    // Captured once and then trusted, which is what makes it dangerous: the
    // bind box belongs to the rig that was loaded when it was captured.
    skin.bindBoundsCaptured = true;

    installRig(kFixture, "assets/models/bender.bin");
    CHECK_MSG(library.Reload(kReloadRig), "the rig must reload");

    AnimationSystem::SyncSkeletons(registry, library);
    auto& after = registry.get<SkinnedMeshComponent>(entity);

    CHECK_EQ(after.skeletonID, idBefore);
    CHECK_MSG(!after.bindBoundsCaptured,
              "a re-exported rig must invalidate the bind bounds, which the id alone cannot say");
    CHECK_MSG(after.skeletonGeneration == library.GenerationOf(after.skeletonID),
              "the resolved rig must record the generation it was built from");

    clearReloadDir();
}

// --- a model that can animate should arrive animating ---------------------
//
// Nothing added an AnimatorComponent, so importing a rig produced an entity
// that COULD animate and did not - and the two look identical: the model is
// there, it is correct, it stands still. SyncSkeletons only ever looked at
// entities that already had an animator, so an imported character never
// reached it.

static void testAModelWithClipsIsGivenSomethingToPlayThemWith() {
    entt::registry registry;
    AnimationLibrary library;

    const entt::entity entity = registry.create();
    registry.emplace<MeshComponent>(entity).filePath = kFixture;

    CHECK_MSG(!registry.all_of<AnimatorComponent>(entity), "it starts with no animator");

    AnimationSystem::SyncSkeletons(registry, library);

    CHECK_MSG(registry.all_of<AnimatorComponent>(entity), "and is given one");
    if (!registry.all_of<AnimatorComponent>(entity)) return;

    // Attached AND aimed at something. An animator with no clip name is a
    // component that changes nothing, which is the same standing still it was
    // supposed to fix.
    const auto& animator = registry.get<AnimatorComponent>(entity);
    CHECK_MSG(!animator.clipName.empty(), "and it names a clip rather than sitting idle");

    const uint32_t id = library.Acquire(kFixture);
    const std::vector<AnimationClip>* clips = library.GetClips(id);
    CHECK_MSG(clips != nullptr && !clips->empty(), "the fixture really does have clips");
    if (clips && !clips->empty()) {
        CHECK_MSG(animator.clipName == clips->front().name, animator.clipName);
    }
}

static void testAMeshWithNoRigIsLeftAlone() {
    // The regression this could most easily cause: every static prop in the
    // project acquiring an animator, a clip it cannot play, and a per-frame
    // warning about it.
    entt::registry registry;
    AnimationLibrary library;

    const entt::entity crate = registry.create();
    registry.emplace<MeshComponent>(crate).primitiveType = "Cube";   // no file at all

    const entt::entity missing = registry.create();
    registry.emplace<MeshComponent>(missing).filePath = "assets/models/definitely_missing_71.gltf";

    AnimationSystem::SyncSkeletons(registry, library);

    CHECK_MSG(!registry.all_of<AnimatorComponent>(crate), "a primitive gets no animator");
    CHECK_MSG(!registry.all_of<AnimatorComponent>(missing), "and neither does a broken path");
}

static void testTheClipNameIsAStartingPointAndNotAnAuthority() {
    // The difference between a working feature and an annoying one. Attaching
    // is a standing rule, so it runs every frame - but the NAME is set only at
    // the moment of attachment, or a user clearing it in the inspector would
    // watch it come straight back.
    entt::registry registry;
    AnimationLibrary library;

    const entt::entity entity = registry.create();
    registry.emplace<MeshComponent>(entity).filePath = kFixture;

    AnimationSystem::SyncSkeletons(registry, library);
    CHECK_MSG(registry.all_of<AnimatorComponent>(entity), "attached on the first pass");
    if (!registry.all_of<AnimatorComponent>(entity)) return;

    registry.get<AnimatorComponent>(entity).clipName.clear();
    AnimationSystem::SyncSkeletons(registry, library);

    CHECK_MSG(registry.get<AnimatorComponent>(entity).clipName.empty(),
              "a cleared clip name stays cleared");

    // And a name the user chose is not overwritten either.
    registry.get<AnimatorComponent>(entity).clipName = "SomethingTheUserPicked";
    AnimationSystem::SyncSkeletons(registry, library);
    CHECK_MSG(registry.get<AnimatorComponent>(entity).clipName == "SomethingTheUserPicked",
              "and a chosen one is left alone");
}

// A clock that is not a number must sample somewhere that exists.
//
// findKey's two range checks (`time <= first`, `time >= last`) are both false for
// NaN, so a NaN time fell through to a binary search whose comparison is false for
// every key - and upper_bound answered "past the end", which the next line then
// read as a key. One float past the vector. A time gets to NaN honestly: a looping
// clip's fmod(infinity, duration) is NaN, and Speed or Time can be 1e999 in a scene.
static void testANonNumberTimeSamplesTheFirstKeyAndReadsNothingElse() {
    Skeleton skeleton;
    skeleton.joints.resize(1);

    AnimChannel channel;
    channel.joint = 0;
    channel.path = AnimPath::Translation;
    channel.interpolation = AnimInterpolation::Linear;
    channel.times = {0.0f, 1.0f, 2.0f};
    channel.values = {glm::vec4(0, 0, 0, 0), glm::vec4(10, 0, 0, 0), glm::vec4(20, 0, 0, 0)};

    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 2.0f;
    clip.channels.push_back(channel);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    std::vector<JointPose> pose;
    AnimationSystem::SamplePose(skeleton, clip, nan, pose);
    CHECK_EQ(static_cast<int>(pose.size()), 1);
    if (pose.size() == 1) {
        CHECK_MSG(std::isfinite(pose[0].translation.x), "a NaN time does not produce a NaN pose");
        CHECK_NEAR(pose[0].translation.x, 0.0f);
    }

    // The two infinities already clamp to the ends, and must keep doing so.
    AnimationSystem::SamplePose(skeleton, clip, -inf, pose);
    if (pose.size() == 1) CHECK_NEAR(pose[0].translation.x, 0.0f);
    AnimationSystem::SamplePose(skeleton, clip, inf, pose);
    if (pose.size() == 1) CHECK_NEAR(pose[0].translation.x, 20.0f);

    // And the middle of a clip is what it was: this is a guard on the edge, not a
    // change to the interpolation.
    AnimationSystem::SamplePose(skeleton, clip, 0.5f, pose);
    if (pose.size() == 1) CHECK_NEAR(pose[0].translation.x, 5.0f);
    AnimationSystem::SamplePose(skeleton, clip, 1.5f, pose);
    if (pose.size() == 1) CHECK_NEAR(pose[0].translation.x, 15.0f);
}

static void runTests() {
    testAModelWithClipsIsGivenSomethingToPlayThemWith();
    testAMeshWithNoRigIsLeftAlone();
    testTheClipNameIsAStartingPointAndNotAnAuthority();
    testVertexLayoutIsWhatThePipelineDeclares();
    testFixtureLoads();
    testJointsAreReorderedParentBeforeChild();
    testJointIndicesAndWeightsAreRead();
    testNormalisedShortWeightsMatchFloatOnes();
    testSkinnedPrimitiveIgnoresItsNodeTransform();
    testAllThreeInterpolationModesImport();
    testStepInterpolationHolds();
    testRotationInterpolatesSmoothly();
    testPoseCompositionInheritsFromTheParent();
    testBindPoseIsTheIdentityAtRest();
    testLibraryCachesHitsAndMisses();
    testSyncSkeletonsResolvesTheRig();
    testClockLoopsAndRunsBackwards();
    testANonNumberTimeSamplesTheFirstKeyAndReadsNothingElse();
    testPoseBoundsCoverTheAnimation();
    testPoseBoundsNeverShrinkBelowTheBindBox();
    testPoseBoundsDoNotAccumulate();
    testPaletteGatherPacksAndBoundsCheck();
    testMissingFileIsHandled();
    testReloadKeepsTheIdAndBumpsTheGeneration();
    testReloadingARiglessFileClearsTheCachedMiss();
    testAReloadThatLosesTheRigKeepsTheOldOne();
    testReloadingARigNobodyAcquiredDoesNothing();
    testAReloadedRigRebuildsWhatWasDerivedFromIt();
    clearReloadDir();
}

TEST_MAIN("test_skeletal", 150)
