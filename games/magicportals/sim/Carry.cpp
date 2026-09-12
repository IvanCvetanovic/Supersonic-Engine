#include "sim/Carry.hpp"

#include "sim/Units.hpp"

#include "core/Components.hpp"

#include <algorithm>

namespace MagicPortals::Carry {

namespace {

using Supersonic::TransformComponent;

glm::dvec2 PxOf(entt::registry& registry, entt::entity body) {
    return Units::ToPixels(registry.get<TransformComponent>(body).position);
}

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
}

bool Standing(entt::registry& registry, entt::entity body) {
    return body != entt::null && registry.valid(body) && registry.all_of<TransformComponent>(body);
}

} // namespace

bool Drop(Carried& carried, entt::registry& registry) {
    if (carried.owner == entt::null) return false;
    if (Standing(registry, carried.owner)) return false;
    carried.owner = entt::null;
    return true;
}

bool Acquire(Carried& carried, entt::registry& registry, const std::vector<entt::entity>& carriers, double rangePx) {
    const double range2 = rangePx * rangePx;
    for (const entt::entity carrier : carriers) {
        if (!Standing(registry, carrier)) continue;
        if (Squared(PxOf(registry, carrier) - carried.atPx) >= range2) continue;
        carried.owner = carrier;
        carried.fromPx = carried.atPx;
        carried.toPx = carried.atPx;
        carried.sinceAimMs = 0.0;
        return true;
    }
    return false;
}

void Trail(Carried& carried, entt::registry& registry, const Rules& rules, double ms) {
    const glm::dvec2 ownerPx = PxOf(registry, carried.owner);

    // forceFollowUpPosition: both ends of the interpolation, and the position,
    // are slammed onto one point.
    if (Squared(ownerPx - carried.atPx) > rules.leashPx * rules.leashPx) {
        carried.atPx = ownerPx;
        carried.fromPx = ownerPx;
        carried.toPx = ownerPx;
        carried.sinceAimMs = 0.0;
        return;
    }

    carried.sinceAimMs += ms;
    if (carried.sinceAimMs > rules.reaimMs) {
        carried.fromPx = carried.atPx;
        carried.toPx = ownerPx;
        carried.sinceAimMs = 0.0;
    }
    const double t = std::min(1.0, carried.sinceAimMs / rules.strideMs);
    carried.atPx = carried.fromPx + (carried.toPx - carried.fromPx) * t;
}

} // namespace MagicPortals::Carry
