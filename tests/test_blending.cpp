// Regression tests for animation cross-fading.
//
// The engine played one clip at a time and switched between them in a single
// frame, so a character going from idle to run jumped between two unrelated
// poses. Blending is what removes that, and the way it is wrong is specific:
// interpolating the composed matrices instead of the transforms, or taking the
// long way round a rotation, both produce something that animates smoothly and
// looks broken.

#include "TestHarness.hpp"
#include "core/AnimationSystem.hpp"
#include "core/AnimationLibrary.hpp"
#include "core/Components.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace Supersonic;

namespace {

JointPose poseAt(const glm::vec3& translation, const glm::quat& rotation,
                 const glm::vec3& scale = glm::vec3(1.0f)) {
    JointPose pose;
    pose.translation = translation;
    pose.rotation = rotation;
    pose.scale = scale;
    return pose;
}

glm::quat aboutY(float degrees) {
    return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
}

// Angle between two orientations, in degrees.
float angleBetween(const glm::quat& a, const glm::quat& b) {
    const float d = std::abs(glm::dot(glm::normalize(a), glm::normalize(b)));
    return glm::degrees(2.0f * std::acos(std::min(1.0f, d)));
}

} // namespace

static void testWeightZeroIsTheOutgoingPose() {
    const std::vector<JointPose> from{ poseAt(glm::vec3(1.0f, 2.0f, 3.0f), aboutY(0.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(9.0f, 9.0f, 9.0f), aboutY(90.0f)) };

    std::vector<JointPose> out;
    AnimationSystem::BlendPoses(from, to, 0.0f, out);

    CHECK_EQ(out.size(), size_t{1});
    CHECK_NEAR(out[0].translation.x, 1.0f);
    CHECK_MSG(angleBetween(out[0].rotation, aboutY(0.0f)) < 0.01f,
              "at weight 0 the pose must be exactly the outgoing one");
}

static void testWeightOneIsTheIncomingPose() {
    const std::vector<JointPose> from{ poseAt(glm::vec3(1.0f), aboutY(0.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(5.0f), aboutY(90.0f)) };

    std::vector<JointPose> out;
    AnimationSystem::BlendPoses(from, to, 1.0f, out);

    CHECK_NEAR(out[0].translation.x, 5.0f);
    CHECK_MSG(angleBetween(out[0].rotation, aboutY(90.0f)) < 0.01f,
              "at weight 1 the transition must have completed");
}

static void testHalfwayIsHalfway() {
    const std::vector<JointPose> from{ poseAt(glm::vec3(0.0f), aboutY(0.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(10.0f, 0.0f, 0.0f), aboutY(90.0f)) };

    std::vector<JointPose> out;
    AnimationSystem::BlendPoses(from, to, 0.5f, out);

    CHECK_NEAR(out[0].translation.x, 5.0f);
    CHECK_MSG(std::abs(angleBetween(out[0].rotation, aboutY(45.0f))) < 0.5f,
              "a half-blended rotation must be the half angle, not the average "
              "of two matrices");
}

static void testRotationStaysAUnitQuaternion() {
    // A blended rotation that is not normalised scales the joint as it turns,
    // so a limb swells and shrinks through every transition.
    const std::vector<JointPose> from{ poseAt(glm::vec3(0.0f), aboutY(0.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(0.0f), aboutY(170.0f)) };

    for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
        std::vector<JointPose> out;
        AnimationSystem::BlendPoses(from, to, t, out);
        CHECK_MSG(test::nearly(glm::length(out[0].rotation), 1.0f, 1e-3f),
                  "rotation drifted off unit length at t=" + std::to_string(t));
    }
}

static void testBlendTakesTheShortestPath() {
    // q and -q are the same orientation. Interpolating between them naively
    // travels 350 degrees to get 10 degrees away, which on a character is a
    // limb swinging through the body during a transition nobody should notice.
    //
    // glm::slerp handles this itself; this pins it, so replacing slerp with a
    // component-wise lerp - or a GLM version that stops doing it - fails here
    // rather than in someone's animation.
    const glm::quat a = aboutY(0.0f);
    const glm::quat b = -aboutY(10.0f);   // same orientation as +10 degrees

    const std::vector<JointPose> from{ poseAt(glm::vec3(0.0f), a) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(0.0f), b) };

    std::vector<JointPose> out;
    AnimationSystem::BlendPoses(from, to, 0.5f, out);

    const float travelled = angleBetween(a, out[0].rotation);
    CHECK_MSG(travelled < 45.0f,
              "halfway should be about 5 degrees from the start, not most of the "
              "way round; travelled " + std::to_string(travelled));
}

static void testWeightIsClamped() {
    const std::vector<JointPose> from{ poseAt(glm::vec3(0.0f), aboutY(0.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(10.0f, 0.0f, 0.0f), aboutY(90.0f)) };

    std::vector<JointPose> under;
    AnimationSystem::BlendPoses(from, to, -2.0f, under);
    CHECK_MSG(test::nearly(under[0].translation.x, 0.0f),
              "a negative weight must not extrapolate backwards past the pose");

    std::vector<JointPose> over;
    AnimationSystem::BlendPoses(from, to, 3.0f, over);
    CHECK_MSG(test::nearly(over[0].translation.x, 10.0f),
              "a weight over 1 must not overshoot the target pose");
}

static void testMismatchedPoseLengthsDoNotReadPastTheEnd() {
    // Two clips on the same rig always agree, but a rig swapped underneath a
    // running transition would not - and reading past the end of the shorter
    // one is a crash rather than a wrong pose.
    const std::vector<JointPose> from{ poseAt(glm::vec3(0.0f), aboutY(0.0f)),
                                       poseAt(glm::vec3(1.0f), aboutY(0.0f)),
                                       poseAt(glm::vec3(2.0f), aboutY(0.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(5.0f), aboutY(90.0f)) };

    std::vector<JointPose> out;
    AnimationSystem::BlendPoses(from, to, 0.5f, out);
    CHECK_EQ(out.size(), size_t{1});
}

static void testScaleBlendsToo() {
    const std::vector<JointPose> from{ poseAt(glm::vec3(0.0f), aboutY(0.0f), glm::vec3(1.0f)) };
    const std::vector<JointPose> to{ poseAt(glm::vec3(0.0f), aboutY(0.0f), glm::vec3(3.0f)) };

    std::vector<JointPose> out;
    AnimationSystem::BlendPoses(from, to, 0.5f, out);
    CHECK_NEAR(out[0].scale.x, 2.0f);
}

static void testPoseToLocalsMatchesAKnownTransform() {
    // The composition order has to stay translate * rotate * scale: swapping
    // scale and rotate shears anything non-uniformly scaled, which is invisible
    // on the uniform rigs and obvious on everything else.
    std::vector<JointPose> pose{ poseAt(glm::vec3(2.0f, 0.0f, 0.0f), aboutY(0.0f),
                                        glm::vec3(3.0f, 1.0f, 1.0f)) };

    std::vector<glm::mat4> locals;
    AnimationSystem::PoseToLocals(pose, locals);

    CHECK_EQ(locals.size(), size_t{1});

    const glm::vec4 origin = locals[0] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK_NEAR(origin.x, 2.0f);

    const glm::vec4 unitX = locals[0] * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    CHECK_MSG(test::nearly(unitX.x, 5.0f),
              "scale must apply before translation, not after: got " +
                  std::to_string(unitX.x));
}

// --- whole-system transitions ----------------------------------------------
//
// Everything above tests the blend arithmetic in isolation. These drive the
// path a game actually takes: change the clip on an animator, run the clock,
// and look at what comes out of the joint palette.

namespace {

constexpr const char* kFixture = "assets/models/bender.gltf";

// An entity with a rig and an animator, as SyncSkeletons would leave it.
entt::entity makeAnimatedEntity(entt::registry& registry, AnimationLibrary& library,
                                const std::string& clip) {
    const auto entity = registry.create();

    auto& animator = registry.emplace<AnimatorComponent>(entity);
    animator.clipName = clip;
    animator.loop = true;
    animator.playing = true;

    auto& skin = registry.emplace<SkinnedMeshComponent>(entity);
    skin.skeletonID = library.Acquire(kFixture);
    return entity;
}

// The rotation the animated joint ends up with, which is where a transition is
// visible. Joint 1 is "Upper", the one both clips drive.
glm::quat animatedJointRotation(const entt::registry& registry, entt::entity entity) {
    const auto& skin = registry.get<SkinnedMeshComponent>(entity);
    if (skin.jointMatrices.size() < 2) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return glm::quat_cast(glm::mat3(skin.jointMatrices[1]));
}

} // namespace

static void testFixtureHasTwoClipsToBlendBetween() {
    // Without a second clip there is nothing to transition to, and every test
    // below would pass by doing nothing.
    AnimationLibrary library;
    const uint32_t rig = library.Acquire(kFixture);
    CHECK_MSG(rig != AnimationLibrary::kInvalidSkeleton, "the fixture has a rig");

    const auto* clips = library.GetClips(rig);
    CHECK(clips != nullptr);
    if (clips) {
        CHECK_MSG(clips->size() >= 2,
                  "the fixture needs two clips: got " + std::to_string(clips->size()));
    }
    CHECK_MSG(library.FindClip(rig, "Bend") != nullptr, "Bend must load");
    CHECK_MSG(library.FindClip(rig, "Twist") != nullptr, "Twist must load");
}

static void testChangingClipStartsATransition() {
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimatedEntity(registry, library, "Bend");

    // The first Advance adopts the starting clip; there is nothing to fade from.
    AnimationSystem::Advance(registry, library, 0.016f);
    CHECK_MSG(registry.get<AnimatorComponent>(entity).blendRemaining == 0.0f,
              "the first clip must not fade in from nothing");

    registry.get<AnimatorComponent>(entity).clipName = "Twist";
    AnimationSystem::Advance(registry, library, 0.016f);

    const auto& animator = registry.get<AnimatorComponent>(entity);
    CHECK_MSG(animator.blendRemaining > 0.0f, "changing the clip must start a fade");
    CHECK_MSG(animator.blendFromClip == "Bend", "the fade must come from the previous clip");
}

static void testTransitionFinishesAfterItsDuration() {
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimatedEntity(registry, library, "Bend");
    registry.get<AnimatorComponent>(entity).blendDuration = 0.25f;

    AnimationSystem::Advance(registry, library, 0.016f);
    registry.get<AnimatorComponent>(entity).clipName = "Twist";
    AnimationSystem::Advance(registry, library, 0.016f);
    CHECK(registry.get<AnimatorComponent>(entity).blendRemaining > 0.0f);

    // Well past the fade.
    for (int i = 0; i < 40; ++i) AnimationSystem::Advance(registry, library, 0.016f);

    CHECK_MSG(registry.get<AnimatorComponent>(entity).blendRemaining == 0.0f,
              "a transition must end, or the outgoing clip is mixed in forever");
}

static void testMidTransitionPoseIsBetweenTheTwoClips() {
    // The point of the whole feature: partway through, the rig is in neither
    // clip. Before blending it snapped straight to the new one.
    AnimationLibrary library;

    entt::registry pureBend;
    const auto bendEntity = makeAnimatedEntity(pureBend, library, "Bend");
    entt::registry pureTwist;
    const auto twistEntity = makeAnimatedEntity(pureTwist, library, "Twist");
    entt::registry blending;
    const auto blendEntity = makeAnimatedEntity(blending, library, "Bend");
    blending.get<AnimatorComponent>(blendEntity).blendDuration = 1.0f;

    // Settle all three at the same point in Bend.
    for (auto* r : { &pureBend, &pureTwist, &blending }) {
        AnimationSystem::Advance(*r, library, 0.3f);
    }

    // Switch one of them, then run exactly half the fade.
    blending.get<AnimatorComponent>(blendEntity).clipName = "Twist";
    AnimationSystem::Advance(blending, library, 0.0f);   // notices the change
    AnimationSystem::Advance(blending, library, 0.5f);   // half of blendDuration

    // Put the pure-Twist rig at the same clip time the blended one reached, so
    // the comparison is against the pose it is fading toward at this instant.
    pureTwist.get<AnimatorComponent>(twistEntity).time =
        blending.get<AnimatorComponent>(blendEntity).time;

    AnimationSystem::EvaluatePoses(pureBend, library);
    AnimationSystem::EvaluatePoses(pureTwist, library);
    AnimationSystem::EvaluatePoses(blending, library);

    const glm::quat bend = animatedJointRotation(pureBend, bendEntity);
    const glm::quat twist = animatedJointRotation(pureTwist, twistEntity);
    const glm::quat mixed = animatedJointRotation(blending, blendEntity);

    const float apart = angleBetween(bend, twist);
    CHECK_MSG(apart > 5.0f,
              "the two clips must differ at this instant, or the test proves "
              "nothing: " + std::to_string(apart) + " degrees");

    if (apart > 5.0f) {
        const float fromBend = angleBetween(bend, mixed);
        const float fromTwist = angleBetween(twist, mixed);
        CHECK_MSG(fromBend > 0.5f && fromTwist > 0.5f,
                  "halfway through a fade the pose must lie between the clips, "
                  "not snapped to either: " + std::to_string(fromBend) +
                  " from Bend, " + std::to_string(fromTwist) + " from Twist");
    }
}

static void testZeroDurationSnaps() {
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimatedEntity(registry, library, "Bend");
    registry.get<AnimatorComponent>(entity).blendDuration = 0.0f;

    AnimationSystem::Advance(registry, library, 0.1f);
    registry.get<AnimatorComponent>(entity).clipName = "Twist";
    AnimationSystem::Advance(registry, library, 0.016f);

    CHECK_MSG(registry.get<AnimatorComponent>(entity).blendRemaining == 0.0f,
              "a zero-length transition must snap, not fade for one frame");
}

static void testRepeatedlyRequestingTheSameClipDoesNotRestartIt() {
    // The natural way to write "play run while moving" is to ask every frame.
    // If that restarted the clip, the character would freeze on frame zero.
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimatedEntity(registry, library, "Bend");

    for (int i = 0; i < 10; ++i) {
        registry.get<AnimatorComponent>(entity).clipName = "Bend";
        AnimationSystem::Advance(registry, library, 0.05f);
    }

    const auto& animator = registry.get<AnimatorComponent>(entity);
    CHECK_MSG(animator.time > 0.1f,
              "the clock must keep running: got " + std::to_string(animator.time));
    CHECK_MSG(animator.blendRemaining == 0.0f,
              "asking for the clip already playing is not a transition");
}

static void testTransitionSurvivesAMissingOutgoingClip() {
    // A clip can vanish - a rig swapped, a name mistyped after the fade began.
    // The pose must fall back to the incoming clip rather than reading a null.
    entt::registry registry;
    AnimationLibrary library;
    const auto entity = makeAnimatedEntity(registry, library, "Bend");

    AnimationSystem::Advance(registry, library, 0.1f);
    registry.get<AnimatorComponent>(entity).clipName = "Twist";
    AnimationSystem::Advance(registry, library, 0.016f);

    registry.get<AnimatorComponent>(entity).blendFromClip = "NoSuchClip";
    AnimationSystem::EvaluatePoses(registry, library);

    const auto& skin = registry.get<SkinnedMeshComponent>(entity);
    CHECK_MSG(!skin.jointMatrices.empty(),
              "a missing outgoing clip must still produce a pose");
}

static void runTests() {
    testWeightZeroIsTheOutgoingPose();
    testWeightOneIsTheIncomingPose();
    testHalfwayIsHalfway();
    testRotationStaysAUnitQuaternion();
    testBlendTakesTheShortestPath();
    testWeightIsClamped();
    testMismatchedPoseLengthsDoNotReadPastTheEnd();
    testScaleBlendsToo();
    testPoseToLocalsMatchesAKnownTransform();

    testFixtureHasTwoClipsToBlendBetween();
    testChangingClipStartsATransition();
    testTransitionFinishesAfterItsDuration();
    testMidTransitionPoseIsBetweenTheTwoClips();
    testZeroDurationSnaps();
    testRepeatedlyRequestingTheSameClipDoesNotRestartIt();
    testTransitionSurvivesAMissingOutgoingClip();
}

TEST_MAIN("test_blending")
