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

void AnimationSystem::SampleClip(const Skeleton& skeleton, const AnimationClip& clip, float time,
                                 std::vector<glm::mat4>& outLocals) {
    const size_t jointCount = skeleton.joints.size();

    // Start from the rest pose: a clip is only required to drive the channels it
    // mentions, and anything it leaves alone keeps its authored transform.
    std::vector<glm::vec3> translations(jointCount);
    std::vector<glm::quat> rotations(jointCount);
    std::vector<glm::vec3> scales(jointCount);
    for (size_t i = 0; i < jointCount; ++i) {
        translations[i] = skeleton.joints[i].restTranslation;
        rotations[i] = skeleton.joints[i].restRotation;
        scales[i] = skeleton.joints[i].restScale;
    }

    for (const auto& channel : clip.channels) {
        if (channel.joint < 0 || static_cast<size_t>(channel.joint) >= jointCount) continue;
        const auto joint = static_cast<size_t>(channel.joint);

        switch (channel.path) {
            case AnimPath::Translation:
                translations[joint] = glm::vec3(sampleChannel(channel, time));
                break;
            case AnimPath::Scale:
                scales[joint] = glm::vec3(sampleChannel(channel, time));
                break;
            case AnimPath::Rotation:
                rotations[joint] = sampleRotation(channel, time);
                break;
        }
    }

    outLocals.resize(jointCount);
    for (size_t i = 0; i < jointCount; ++i) {
        outLocals[i] = glm::translate(glm::mat4(1.0f), translations[i])
                     * glm::mat4_cast(rotations[i])
                     * glm::scale(glm::mat4(1.0f), scales[i]);
    }
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

void AnimationSystem::Advance(entt::registry& registry, const AnimationLibrary& library,
                              float deltaTime) {
    if (deltaTime == 0.0f) return;

    for (auto entity : registry.view<AnimatorComponent, SkinnedMeshComponent>()) {
        auto& animator = registry.get<AnimatorComponent>(entity);
        if (!animator.playing) continue;

        const auto& skin = registry.get<SkinnedMeshComponent>(entity);
        const AnimationClip* clip = library.FindClip(skin.skeletonID, animator.clipName);
        if (!clip || clip->duration <= 0.0f) continue;

        animator.time += deltaTime * animator.speed;

        if (animator.loop) {
            animator.time = std::fmod(animator.time, clip->duration);
            // fmod keeps the sign of the dividend, so playing backwards would
            // otherwise walk off into negative time and clamp to the first key.
            if (animator.time < 0.0f) animator.time += clip->duration;
        } else {
            animator.time = std::clamp(animator.time, 0.0f, clip->duration);
        }
    }
}

void AnimationSystem::EvaluatePoses(entt::registry& registry, const AnimationLibrary& library) {
    std::vector<glm::mat4> locals;

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

        SampleClip(*skeleton, *clip, animator.time, locals);
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
