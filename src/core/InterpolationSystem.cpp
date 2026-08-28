#include "core/InterpolationSystem.hpp"

#include "core/Components.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace {

constexpr float kTau = 6.28318530717958647692f;

// The shortest way round from `from` to `to`, in radians.
//
// Rotations are stored as EULER ANGLES here, so a straight lerp is wrong at the
// wrap: a turret at 179 degrees turning to -179 has moved two degrees, and a
// naive lerp takes it three hundred and fifty-eight the other way. That is not
// a subtle artefact - it is a tank turret spinning all the way round between
// two ticks, once per lap, for one frame.
//
// Per axis, and honestly less than a slerp: three independent shortest arcs are
// not the same path a quaternion would take between two orientations. It is the
// right trade for a component that stores Euler angles - converting to
// quaternions and back would have to invent an Euler decomposition on the way
// out, and there are several, none of which is the one the author typed.
//
// The limit worth knowing: a rotation of MORE than half a turn within one tick
// interpolates the short way, which is the wrong way. Anything spinning that
// fast is a frame from aliasing regardless.
float shortestAngleDelta(float from, float to) {
    float delta = std::fmod(to - from, kTau);
    if (delta > kTau * 0.5f) delta -= kTau;
    if (delta < -kTau * 0.5f) delta += kTau;
    return delta;
}

glm::vec3 lerpEuler(const glm::vec3& from, const glm::vec3& to, float alpha) {
    return glm::vec3(from.x + shortestAngleDelta(from.x, to.x) * alpha,
                     from.y + shortestAngleDelta(from.y, to.y) * alpha,
                     from.z + shortestAngleDelta(from.z, to.z) * alpha);
}

} // namespace

void InterpolationSystem::BeginTick(entt::registry& registry) {
    for (auto entity : registry.view<InterpolatedTransformComponent, TransformComponent>()) {
        auto& interp = registry.get<InterpolatedTransformComponent>(entity);
        auto& transform = registry.get<TransformComponent>(entity);

        // Put the simulated value back over whatever the last frame drew, so
        // this tick reads the world rather than the picture of it. Skipped on
        // the first tick an entity ever runs, when there is nothing to restore
        // and the transform IS the authority.
        if (interp.captured) {
            transform.position = interp.currentPosition;
            transform.rotation = interp.currentRotation;
            transform.scale = interp.currentScale;
        }

        interp.previousPosition = transform.position;
        interp.previousRotation = transform.rotation;
        interp.previousScale = transform.scale;
    }
}

void InterpolationSystem::EndTick(entt::registry& registry) {
    for (auto entity : registry.view<InterpolatedTransformComponent, TransformComponent>()) {
        auto& interp = registry.get<InterpolatedTransformComponent>(entity);
        const auto& transform = registry.get<TransformComponent>(entity);

        interp.currentPosition = transform.position;
        interp.currentRotation = transform.rotation;
        interp.currentScale = transform.scale;

        // On the very first tick the two ends are the same, so the frame drawn
        // after it sits still rather than sliding in from wherever the
        // uninitialised previous value happened to be - which for an entity
        // spawned far from the origin is a visible streak across the map.
        if (!interp.captured) {
            interp.previousPosition = transform.position;
            interp.previousRotation = transform.rotation;
            interp.previousScale = transform.scale;
            interp.captured = true;
        }
    }
}

void InterpolationSystem::Apply(entt::registry& registry, float alpha) {
    const float t = std::clamp(alpha, 0.0f, 1.0f);

    for (auto entity : registry.view<InterpolatedTransformComponent, TransformComponent>()) {
        const auto& interp = registry.get<InterpolatedTransformComponent>(entity);
        if (!interp.captured) continue;

        auto& transform = registry.get<TransformComponent>(entity);
        transform.position = interp.previousPosition
                           + (interp.currentPosition - interp.previousPosition) * t;
        transform.rotation = lerpEuler(interp.previousRotation, interp.currentRotation, t);
        transform.scale = interp.previousScale
                        + (interp.currentScale - interp.previousScale) * t;
    }
}

} // namespace Supersonic
