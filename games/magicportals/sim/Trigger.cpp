#include "sim/Trigger.hpp"

#include "core/Components.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MagicPortals::Trigger {

using Supersonic::BoxColliderComponent;
using Supersonic::CapsuleColliderComponent;
using Supersonic::RigidBodyComponent;
using Supersonic::SphereColliderComponent;
using Supersonic::TransformComponent;

namespace {

glm::vec2 Rotate(const glm::vec2& v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return glm::vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// From a point to the box; 0 inside it.
float DistanceToBox(const glm::vec2& point, const Box& box) {
    return glm::length(glm::max(glm::abs(point - box.centre) - box.half, glm::vec2(0.0f)));
}

float DistanceToSegment(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float lengthSquared = glm::dot(ab, ab);
    const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return glm::length(point - (a + ab * t));
}

// Whether the segment passes through the box: Liang-Barsky clipping.
bool SegmentCrossesBox(const glm::vec2& a, const glm::vec2& b, const Box& box) {
    const glm::vec2 lo = box.centre - box.half;
    const glm::vec2 hi = box.centre + box.half;
    const glm::vec2 d = b - a;
    float enter = 0.0f;
    float leave = 1.0f;
    for (int axis = 0; axis < 2; ++axis) {
        if (d[axis] == 0.0f) {
            if (a[axis] < lo[axis] || a[axis] > hi[axis]) return false;
            continue;
        }
        float t0 = (lo[axis] - a[axis]) / d[axis];
        float t1 = (hi[axis] - a[axis]) / d[axis];
        if (t0 > t1) std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave) return false;
    }
    return true;
}

// A capsule is every point within `radius` of its core segment, so it overlaps the
// box when the segment comes within `radius` of it. Unless the two cross, the
// nearest pair is an end of the segment against the box, or a corner of the box
// against the segment.
bool CapsuleOverlaps(const glm::vec2& a, const glm::vec2& b, float radius, const Box& box) {
    if (SegmentCrossesBox(a, b, box)) return true;
    float nearest = std::min(DistanceToBox(a, box), DistanceToBox(b, box));
    for (const glm::vec2 sign : {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f),
                                 glm::vec2(-1.0f, 1.0f)}) {
        nearest = std::min(nearest, DistanceToSegment(box.centre + box.half * sign, a, b));
    }
    return nearest < radius;
}

// A box turned by `angle` against the axis-aligned one. They are apart exactly
// when one of the four edge normals separates them.
bool TurnedBoxOverlaps(const glm::vec2& centre, const glm::vec2& half, float angle, const Box& box) {
    const glm::vec2 u = Rotate(glm::vec2(1.0f, 0.0f), angle);
    const glm::vec2 v = Rotate(glm::vec2(0.0f, 1.0f), angle);
    const glm::vec2 apart = centre - box.centre;
    for (const glm::vec2 axis : {glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f), u, v}) {
        const float turnedReach = half.x * std::fabs(glm::dot(u, axis)) + half.y * std::fabs(glm::dot(v, axis));
        const float boxReach = box.half.x * std::fabs(axis.x) + box.half.y * std::fabs(axis.y);
        if (std::fabs(glm::dot(apart, axis)) >= turnedReach + boxReach) return false;
    }
    return true;
}

bool VectorMeta(const Tscn::Node& node, const char* key, glm::dvec2& out, std::string& error) {
    const Tscn::Value* value = node.Meta(key);
    if (value == nullptr) return true;
    if (value->kind != Tscn::Value::Kind::Vector2) {
        error = node.name + ": metadata/" + key + " is not a Vector2";
        return false;
    }
    out = glm::dvec2(value->numbers[0], value->numbers[1]);
    return true;
}

} // namespace

bool FromNode(const Tscn::Node& node, Box& out, std::string& error) {
    const Tscn::Value* rotation = node.Find("rotation");
    double angle = 0.0;
    if (rotation != nullptr && rotation->AsNumber(angle) && angle != 0.0) {
        error = node.name + " is rotated, and its trigger would be turned with it";
        return false;
    }
    glm::dvec2 at(0.0);
    if (const Tscn::Value* position = node.Find("position");
        position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
        at = glm::dvec2(position->numbers[0], position->numbers[1]);
    }
    glm::dvec2 size(kFallbackSizePx);
    glm::dvec2 offset(0.0);
    if (!VectorMeta(node, "trigger_size", size, error) || !VectorMeta(node, "trigger_offset", offset, error)) {
        return false;
    }
    const glm::vec3 centre = Units::ToWorld(at.x + offset.x, at.y + offset.y);
    out.centre = glm::vec2(centre.x, centre.y);
    out.half = glm::vec2(Units::ToMetres(size.x * 0.5), Units::ToMetres(size.y * 0.5));
    return true;
}

bool Overlaps(entt::registry& registry, entt::entity entity, const Box& box) {
    const auto* transform = registry.try_get<TransformComponent>(entity);
    if (transform == nullptr) return false;
    const glm::vec2 origin(transform->position.x, transform->position.y);
    const float angle = transform->rotation.z;

    if (const auto* collider = registry.try_get<BoxColliderComponent>(entity)) {
        const glm::vec2 centre = origin + Rotate(glm::vec2(collider->center.x, collider->center.y), angle);
        return TurnedBoxOverlaps(centre, glm::vec2(collider->size.x, collider->size.y) * 0.5f, angle, box);
    }
    if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
        const glm::vec2 centre = origin + Rotate(glm::vec2(sphere->center.x, sphere->center.y), angle);
        return DistanceToBox(centre, box) < sphere->radius;
    }
    if (const auto* capsule = registry.try_get<CapsuleColliderComponent>(entity)) {
        // Its height is the whole capsule, caps included, so the core runs
        // height/2 - radius either side of the centre.
        const float reach = std::max(capsule->height * 0.5f - capsule->radius, 0.0f);
        const glm::vec2 centre = origin + Rotate(glm::vec2(capsule->center.x, capsule->center.y), angle);
        const glm::vec2 along = Rotate(glm::vec2(0.0f, reach), angle);
        return CapsuleOverlaps(centre - along, centre + along, capsule->radius, box);
    }
    return false;
}

int DynamicBodiesIn(entt::registry& registry, const Box& box) {
    int count = 0;
    for (const entt::entity entity : registry.view<RigidBodyComponent>()) {
        if (registry.get<RigidBodyComponent>(entity).isKinematic) continue;
        if (Overlaps(registry, entity, box)) ++count;
    }
    return count;
}

} // namespace MagicPortals::Trigger
