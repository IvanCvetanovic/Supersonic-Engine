#pragma once

// The portal shot's geometry: what a segment in the plane meets first.
//
// The owner, 11 September: tapping fires a shot from the character toward the
// tap, and the portal opens where the shot arrives. A wall on the way stops it
// and no portal opens. So does a crate or a stone: "it would hit them before
// reaching the tapped location". A failed shot costs nothing, "since it would
// never appear". The original's script names the parts: portal_launch,
// computeProjectileOrigin, projectileDestiny, computePortalFinalPos,
// destroyOnStaticHit. Portals flies the shot; this is the test it flies by.
//
// The test is the port's own and exact in the plane, as Trigger's is, because
// the engine's queries see a box or a hull only by its bounds. It reads box
// and sphere colliders, turned with their entity, and the polygon LevelBuilder
// records for each hull (LevelBuilder::PlanePolygon). Every solid body stops a
// shot, whether static, moving or loose. A trigger never does, and neither does
// the character the shot leaves from. A capsule, which only the character has,
// is not read.

#include "sim/Trigger.hpp"

#include <optional>
#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Shot {

// The port's shot.json.
struct Rules {
    double speedPx = 0.0; // _guess
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Hit {
    entt::entity body = entt::null;
    double along = 0.0; // how far along the segment, from 0 to 1
};

// The first solid body the segment from `fromPx` to `toPx` meets, in the
// remake's pixels, other than `ignore`. None when it meets nothing. A segment
// that starts inside a body meets it at once.
std::optional<Hit> FirstBody(entt::registry& registry, const glm::dvec2& fromPx, const glm::dvec2& toPx,
                             entt::entity ignore);

// How far along the segment it first enters the box, or none.
std::optional<double> Enters(const glm::dvec2& fromPx, const glm::dvec2& toPx, const Trigger::Box& box);

} // namespace MagicPortals::Shot
