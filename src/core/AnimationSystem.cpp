#include "core/AnimationSystem.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"

namespace Supersonic {

namespace {

// Index of the last key at or before `time`, plus the fraction to the next one.
// Times outside the channel clamp to its ends rather than extrapolating.
size_t findKey(const std::vector<float>& times, float time, float& outT) {
    outT = 0.0f;
    if (times.size() < 2) return 0;

    if (time <= times.front()) return 0;
    if (time >= times.back()) return times.size() - 1;

    // Binary search: a channel can hold thousands of keys and this runs per
    // channel per frame.
    const auto upper = std::upper_bound(times.begin(), times.end(), time);
    const size_t next = static_cast<size_t>(upper - times.begin());
    const size_t current = next - 1;

    const float span = times[next] - times[current];
    outT = span > 1e-9f ? (time - times[current]) / span : 0.0f;
    return current;
}

// glTF cubic spline: p(t) = (2t^3-3t^2+1)p0 + (t^3-2t^2+t)m0 + (-2t^3+3t^2)p1 + (t^3-t^2)m1,
// with the tangents scaled by the key interval.
glm::vec4 cubicSpline(const glm::vec4& v0, const glm::vec4& outTangent0,
                      const glm::vec4& inTangent1, const glm::vec4& v1,
                      float t, float deltaTime) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0
         + deltaTime * (t3 - 2.0f * t2 + t) * outTangent0
         + (-2.0f * t3 + 3.0f * t2) * v1
         + deltaTime * (t3 - t2) * inTangent1;
}

glm::vec4 sampleChannel(const AnimChannel& channel, float time) {
    if (channel.times.empty() || channel.values.empty()) return glm::vec4(0.0f);

    float t = 0.0f;
    const size_t key = findKey(channel.times, time, t);

    if (channel.interpolation == AnimInterpolation::CubicSpline) {
        // Three values per key: in-tangent, value, out-tangent.
        const size_t base = key * 3;
        if (base + 1 >= channel.values.size()) return channel.values.back();
        if (key + 1 >= channel.times.size() || base + 4 >= channel.values.size()) {
            return channel.values[base + 1];
        }
        const float deltaTime = channel.times[key + 1] - channel.times[key];
        return cubicSpline(channel.values[base + 1], channel.values[base + 2],
                           channel.values[base + 3], channel.values[base + 4], t, deltaTime);
    }

    if (key + 1 >= channel.times.size() || key + 1 >= channel.values.size()) {
        return channel.values[std::min(key, channel.values.size() - 1)];
    }

    // STEP holds the previous key's value until the next one, which is what
    // makes it useful for things that must not blend, like a visibility flip.
    if (channel.interpolation == AnimInterpolation::Step) return channel.values[key];

    return glm::mix(channel.values[key], channel.values[key + 1], t);
}

glm::quat sampleRotation(const AnimChannel& channel, float time) {
    if (channel.times.empty() || channel.values.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    float t = 0.0f;
    const size_t key = findKey(channel.times, time, t);

    // glTF stores quaternions xyzw; glm::quat is constructed wxyz.
    const auto toQuat = [](const glm::vec4& v) { return glm::quat(v.w, v.x, v.y, v.z); };

    if (channel.interpolation == AnimInterpolation::CubicSpline) {
        return glm::normalize(toQuat(sampleChannel(channel, time)));
    }

    if (key + 1 >= channel.times.size() || key + 1 >= channel.values.size()) {
        return glm::normalize(toQuat(channel.values[std::min(key, channel.values.size() - 1)]));
    }
    if (channel.interpolation == AnimInterpolation::Step) {
        return glm::normalize(toQuat(channel.values[key]));
    }

    // Spherical, not linear: lerping quaternions and renormalising takes the
    // chord rather than the arc, and the joint visibly slows in the middle of
    // every large rotation.
    return glm::normalize(glm::slerp(toQuat(channel.values[key]),
                                     toQuat(channel.values[key + 1]), t));
}

} // namespace

void AnimationSystem::SamplePose(const Skeleton& skeleton, const AnimationClip& clip, float time,
                                 std::vector<JointPose>& outPose) {
    const size_t jointCount = skeleton.joints.size();

    // Start from the rest pose: a clip is only required to drive the channels it
    // mentions, and anything it leaves alone keeps its authored transform.
    outPose.resize(jointCount);
    for (size_t i = 0; i < jointCount; ++i) {
        outPose[i].translation = skeleton.joints[i].restTranslation;
        outPose[i].rotation = skeleton.joints[i].restRotation;
        outPose[i].scale = skeleton.joints[i].restScale;
    }

    for (const auto& channel : clip.channels) {
        if (channel.joint < 0 || static_cast<size_t>(channel.joint) >= jointCount) continue;
        const auto joint = static_cast<size_t>(channel.joint);

        switch (channel.path) {
            case AnimPath::Translation:
                outPose[joint].translation = glm::vec3(sampleChannel(channel, time));
                break;
            case AnimPath::Scale:
                outPose[joint].scale = glm::vec3(sampleChannel(channel, time));
                break;
            case AnimPath::Rotation:
                outPose[joint].rotation = sampleRotation(channel, time);
                break;
        }
    }
}

void AnimationSystem::BlendPoses(const std::vector<JointPose>& from,
                                 const std::vector<JointPose>& to,
                                 float weight,
                                 std::vector<JointPose>& outPose) {
    const float t = std::clamp(weight, 0.0f, 1.0f);
    const size_t count = std::min(from.size(), to.size());

    outPose.resize(count);
    for (size_t i = 0; i < count; ++i) {
        outPose[i].translation = glm::mix(from[i].translation, to[i].translation, t);
        outPose[i].scale = glm::mix(from[i].scale, to[i].scale, t);

        // glm::slerp already flips the target when the two quaternions face
        // opposite ways, which matters because q and -q are the same
        // orientation and slerping between them the long way sends a limb
        // swinging through the body on a transition nobody should notice.
        // test_blending pins that behaviour rather than trusting it silently.
        outPose[i].rotation = glm::normalize(glm::slerp(from[i].rotation, to[i].rotation, t));
    }
}

void AnimationSystem::PoseToLocals(const std::vector<JointPose>& pose,
                                   std::vector<glm::mat4>& outLocals) {
    outLocals.resize(pose.size());
    for (size_t i = 0; i < pose.size(); ++i) {
        outLocals[i] = glm::translate(glm::mat4(1.0f), pose[i].translation)
                     * glm::mat4_cast(pose[i].rotation)
                     * glm::scale(glm::mat4(1.0f), pose[i].scale);
    }
}

void AnimationSystem::SampleClip(const Skeleton& skeleton, const AnimationClip& clip, float time,
                                 std::vector<glm::mat4>& outLocals) {
    std::vector<JointPose> pose;
    SamplePose(skeleton, clip, time, pose);
    PoseToLocals(pose, outLocals);
}

void AnimationSystem::ComposePose(const Skeleton& skeleton, const std::vector<glm::mat4>& locals,
                                  std::vector<glm::mat4>& outMatrices) {
    const size_t jointCount = skeleton.joints.size();
    outMatrices.resize(jointCount);

    // Model-space transform per joint, before the inverse bind is applied.
    std::vector<glm::mat4> globals(jointCount, glm::mat4(1.0f));

    for (size_t i = 0; i < jointCount; ++i) {
        const Joint& joint = skeleton.joints[i];
        const glm::mat4 local = i < locals.size() ? locals[i] : glm::mat4(1.0f);

        // One forward pass with no recursion, which is only correct because the
        // loader reorders joints parent-before-child. A file listing a child
        // first would otherwise read an uninitialised parent here.
        const glm::mat4 parentGlobal = joint.parent >= 0
                                     ? globals[static_cast<size_t>(joint.parent)]
                                     : glm::mat4(1.0f);

        globals[i] = parentGlobal * joint.preTransform * local;

        // MODEL space, deliberately excluding the entity's world transform: the
        // vertex shader computes model * skin * position, so folding the world
        // matrix in here would apply it twice.
        outMatrices[i] = globals[i] * joint.inverseBind;
    }
}

void AnimationSystem::SyncSkeletons(entt::registry& registry, AnimationLibrary& library) {
    for (auto entity : registry.view<AnimatorComponent, MeshComponent>()) {
        const auto& mesh = registry.get<MeshComponent>(entity);

        const uint32_t skeletonID = library.Acquire(mesh.filePath);
        if (skeletonID == AnimationLibrary::kInvalidSkeleton) {
            if (registry.all_of<SkinnedMeshComponent>(entity)) {
                registry.remove<SkinnedMeshComponent>(entity);
            }
            continue;
        }

        auto& skin = registry.get_or_emplace<SkinnedMeshComponent>(entity);
        if (skin.skeletonID != skeletonID) {
            skin.skeletonID = skeletonID;
            skin.jointMatrices.clear();
            // A different mesh has different bind bounds.
            skin.bindBoundsCaptured = false;
        }

        const Skeleton* skeleton = library.GetSkeleton(skeletonID);
        if (skeleton && skin.jointMatrices.size() != skeleton->joints.size()) {
            skin.jointMatrices.assign(skeleton->joints.size(), glm::mat4(1.0f));
        }
    }
}

namespace {

// One clip's clock, wrapped or clamped. Shared by the playing clip and the
// outgoing one during a transition, which is what keeps the two in step.
float advanceClock(float time, float delta, float duration, bool loop) {
    float advanced = time + delta;
    if (loop) {
        advanced = std::fmod(advanced, duration);
        // fmod keeps the sign of the dividend, so playing backwards would
        // otherwise walk off into negative time and clamp to the first key.
        if (advanced < 0.0f) advanced += duration;
        return advanced;
    }
    return std::clamp(advanced, 0.0f, duration);
}

// Starts a cross-fade when the requested clip differs from the one playing.
void BeginTransitionIfClipChanged(AnimatorComponent& animator) {
    if (animator.clipName == animator.activeClip) return;

    const std::string outgoing = animator.activeClip;
    animator.activeClip = animator.clipName;

    // Adoption, not a change: an empty outgoing clip means nobody switched
    // away from anything. It happens on the first frame, and after every load,
    // Play, Stop and undo - activeClip is derived and so is not serialised,
    // while time is. Resetting the clock here would throw away the restored
    // pose, which is the whole reason time is persisted at all.
    if (outgoing.empty()) {
        animator.blendRemaining = 0.0f;
        animator.blendTotal = 0.0f;
        animator.blendFromClip.clear();
        return;
    }

    // A real switch with blending turned off. The new clip starts from the
    // beginning, as it always did.
    if (animator.blendDuration <= 0.0f) {
        animator.blendRemaining = 0.0f;
        animator.blendTotal = 0.0f;
        animator.blendFromClip.clear();
        animator.time = 0.0f;
        return;
    }

    // Interrupting a transition blends from wherever it had got to, so
    // hammering a key does not snap back to the pose the first fade started
    // from. Approximated by fading from the clip that was winning, which is
    // the incoming one - the pose on screen is already mostly that.
    animator.blendFromClip = outgoing;
    animator.blendFromTime = animator.time;
    animator.blendTotal = animator.blendDuration;
    animator.blendRemaining = animator.blendDuration;
    animator.time = 0.0f;
}

} // namespace

void AnimationSystem::Advance(entt::registry& registry, const AnimationLibrary& library,
                              float deltaTime) {
    if (deltaTime == 0.0f) return;

    for (auto entity : registry.view<AnimatorComponent, SkinnedMeshComponent>()) {
        auto& animator = registry.get<AnimatorComponent>(entity);
        const auto& skin = registry.get<SkinnedMeshComponent>(entity);

        // Noticed here rather than pushed by whoever assigns the clip, so a
        // change transitions the same way whether it came from the inspector,
        // a script or a scene load.
        BeginTransitionIfClipChanged(animator);

        if (!animator.playing) continue;

        // The outgoing clip keeps running for the length of the fade. Freezing
        // it instead makes a character stop dead and slide into the new
        // animation, which reads worse than no blending at all.
        if (animator.blendRemaining > 0.0f) {
            if (const AnimationClip* previous =
                    library.FindClip(skin.skeletonID, animator.blendFromClip);
                previous && previous->duration > 0.0f) {
                animator.blendFromTime = advanceClock(animator.blendFromTime,
                                                      deltaTime * animator.speed,
                                                      previous->duration, animator.loop);
            }
            // Real time, not scaled by speed: a transition is a presentation
            // choice measured in seconds, and tying it to playback rate makes a
            // slowed-down animation take proportionally longer to blend in.
            animator.blendRemaining = std::max(0.0f, animator.blendRemaining - std::abs(deltaTime));
        }

        const AnimationClip* clip = library.FindClip(skin.skeletonID, animator.clipName);
        if (!clip || clip->duration <= 0.0f) continue;

        animator.time = advanceClock(animator.time, deltaTime * animator.speed,
                                     clip->duration, animator.loop);
    }
}

void AnimationSystem::EvaluatePoses(entt::registry& registry, const AnimationLibrary& library) {
    // Reused across entities rather than allocated per entity per frame.
    std::vector<glm::mat4> locals;
    std::vector<JointPose> pose;
    std::vector<JointPose> previousPose;
    std::vector<JointPose> blended;

    for (auto entity : registry.view<AnimatorComponent, SkinnedMeshComponent>()) {
        auto& animator = registry.get<AnimatorComponent>(entity);
        auto& skin = registry.get<SkinnedMeshComponent>(entity);

        const Skeleton* skeleton = library.GetSkeleton(skin.skeletonID);
        if (!skeleton || skeleton->empty()) continue;

        const AnimationClip* clip = library.FindClip(skin.skeletonID, animator.clipName);
        if (!clip) {
            if (!animator.warnedMissing && !animator.clipName.empty()) {
                std::cerr << "[AnimationSystem] No clip named '" << animator.clipName
                          << "'; the mesh stays in bind pose." << std::endl;
                animator.warnedMissing = true;
            }
            // Bind pose, which is what the identity palette produces.
            skin.jointMatrices.assign(skeleton->joints.size(), glm::mat4(1.0f));
            continue;
        }
        animator.warnedMissing = false;

        SamplePose(*skeleton, *clip, animator.time, pose);

        if (animator.blendRemaining > 0.0f && animator.blendTotal > 0.0f) {
            if (const AnimationClip* previous =
                    library.FindClip(skin.skeletonID, animator.blendFromClip)) {
                SamplePose(*skeleton, *previous, animator.blendFromTime, previousPose);

                // Weight runs 0 to 1 as the remaining time runs down, so the
                // incoming clip arrives at full strength exactly when the
                // transition ends.
                const float weight = 1.0f - (animator.blendRemaining / animator.blendTotal);
                BlendPoses(previousPose, pose, weight, blended);
                pose.swap(blended);
            }
        }

        PoseToLocals(pose, locals);
        ComposePose(*skeleton, locals, skin.jointMatrices);

        // Widen the render bounds to the posed extent.
        //
        // Culling reads RenderableComponent's local bounds, which SyncResources
        // refreshes from the STATIC mesh - the bind pose. An arm raised above
        // that box makes the whole character disappear the moment the bind-pose
        // AABB leaves the frustum, which is the exact failure the frustum tests
        // exist to prevent, arriving through a different door.
        if (auto* renderable = registry.try_get<RenderableComponent>(entity)) {
            // Captured once, and every union goes against the captured value
            // rather than the widened one - so calling this twice in a frame, or
            // in any order relative to SyncResources, gives the same answer
            // instead of inflating a little more each time.
            if (!skin.bindBoundsCaptured) {
                skin.bindBoundsMin = renderable->localBoundsMin;
                skin.bindBoundsMax = renderable->localBoundsMax;
                skin.bindBoundsCaptured = true;
            }

            const glm::vec3 bindMin = skin.bindBoundsMin;
            const glm::vec3 bindMax = skin.bindBoundsMax;

            glm::vec3 posedMin(std::numeric_limits<float>::max());
            glm::vec3 posedMax(std::numeric_limits<float>::lowest());

            for (const glm::mat4& matrix : skin.jointMatrices) {
                // Conservative: the bind box carried by each joint. Looser than
                // a true posed hull, and loose is the safe direction - an
                // over-large bound costs a draw call, an under-large one loses
                // the character.
                const glm::vec3 centre = (bindMin + bindMax) * 0.5f;
                const glm::vec3 extent = (bindMax - bindMin) * 0.5f;

                const glm::vec3 worldCentre = glm::vec3(matrix * glm::vec4(centre, 1.0f));
                const glm::mat3 basis(matrix);
                const glm::vec3 worldExtent(
                    std::abs(basis[0].x) * extent.x + std::abs(basis[1].x) * extent.y + std::abs(basis[2].x) * extent.z,
                    std::abs(basis[0].y) * extent.x + std::abs(basis[1].y) * extent.y + std::abs(basis[2].y) * extent.z,
                    std::abs(basis[0].z) * extent.x + std::abs(basis[1].z) * extent.y + std::abs(basis[2].z) * extent.z);

                posedMin = glm::min(posedMin, worldCentre - worldExtent);
                posedMax = glm::max(posedMax, worldCentre + worldExtent);
            }

            if (posedMax.x >= posedMin.x) {
                renderable->localBoundsMin = glm::min(bindMin, posedMin);
                renderable->localBoundsMax = glm::max(bindMax, posedMax);
            }
        }
    }
}

uint32_t AnimationSystem::GatherPalettes(entt::registry& registry,
                                         std::vector<glm::mat4>& outPalette,
                                         uint32_t capacity) {
    outPalette.clear();
    uint32_t written = 0;

    for (auto entity : registry.view<SkinnedMeshComponent>()) {
        auto& skin = registry.get<SkinnedMeshComponent>(entity);
        const auto count = static_cast<uint32_t>(skin.jointMatrices.size());

        if (count == 0 || written + count > capacity) {
            // -1 draws unskinned. The alternative is reading past the end of the
            // buffer, and robustBufferAccess is not enabled on this device, so
            // that is a device loss rather than a zeroed read.
            skin.paletteBase = -1;
            continue;
        }

        skin.paletteBase = static_cast<int32_t>(written);
        outPalette.insert(outPalette.end(), skin.jointMatrices.begin(), skin.jointMatrices.end());
        written += count;
    }
    return written;
}

} // namespace Supersonic
