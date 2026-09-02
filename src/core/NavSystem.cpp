#include "core/NavSystem.hpp"

#include <algorithm>
#include <cmath>

#include "core/Components.hpp"
#include "core/WorldShapes.hpp"

namespace Supersonic {

namespace {

// The world-space axis-aligned box a collider occupies, from the entity's
// world matrix.
//
// AXIS ALIGNED, and that is a choice with a cost. A rotated crate blocks the
// cells its upright bounding box covers, which is more cells than the crate
// actually fills - so a unit walks a little further around it than it strictly
// must. The alternative is a per-cell test against an oriented box, which is
// the separating axis theorem the physics narrowphase already carries, run
// once per cell per collider. Cheaper to be slightly conservative, and being
// conservative is the SAFE direction: a nav grid that blocks too little sends
// units into walls, and one that blocks too much sends them around.
void WorldAabb(const glm::mat4& world, const glm::vec3& localCenter,
               const glm::vec3& halfExtents, glm::vec3& outCenter, glm::vec3& outHalf) {
    outCenter = glm::vec3(world * glm::vec4(localCenter, 1.0f));

    // The extent of a transformed box along each world axis is the sum of the
    // absolute contributions of the three local axes - the same arithmetic
    // Frustum::TransformAABB does, and for the same reason.
    const glm::vec3 axisX = glm::vec3(world[0]) * halfExtents.x;
    const glm::vec3 axisY = glm::vec3(world[1]) * halfExtents.y;
    const glm::vec3 axisZ = glm::vec3(world[2]) * halfExtents.z;

    outHalf = glm::vec3(std::fabs(axisX.x) + std::fabs(axisY.x) + std::fabs(axisZ.x),
                        std::fabs(axisX.y) + std::fabs(axisY.y) + std::fabs(axisZ.y),
                        std::fabs(axisX.z) + std::fabs(axisY.z) + std::fabs(axisZ.z));
}

// The rectangle of cells a world box can possibly touch, clamped to the grid.
// Returns false when it touches none, which is the ordinary case for most
// colliders in a level against any one grid.
bool CellRange(const NavBounds& bounds, const NavGrid& grid, const glm::vec3& center,
               const glm::vec3& half, uint32_t& minX, uint32_t& minY, uint32_t& maxX,
               uint32_t& maxY) {
    if (bounds.cellSize <= 0.0f) return false;

    // A grid that was never sized, or whose Resize was refused. Width() - 1 is
    // unsigned, so on an empty grid it is four billion and the clamp below
    // would bound the walk to the COLLIDER rather than to the grid - which is
    // a read off the end of an empty vector.
    if (grid.Width() == 0 || grid.Height() == 0) return false;

    const glm::vec3 local = center - bounds.origin;
    const float across = local.x;
    const float along = bounds.plane == NavBounds::Plane::XY ? -local.y : local.z;
    const float halfAcross = half.x;
    const float halfAlong = bounds.plane == NavBounds::Plane::XY ? half.y : half.z;

    const float loAcross = (across - halfAcross) / bounds.cellSize;
    const float hiAcross = (across + halfAcross) / bounds.cellSize;
    const float loAlong = (along - halfAlong) / bounds.cellSize;
    const float hiAlong = (along + halfAlong) / bounds.cellSize;

    if (hiAcross < 0.0f || hiAlong < 0.0f) return false;
    if (loAcross >= static_cast<float>(grid.Width())) return false;
    if (loAlong >= static_cast<float>(grid.Height())) return false;

    // Clamped in FLOAT before the cast. Flooring a negative number and casting
    // it to unsigned gives four billion, which passes every bounds check there
    // is and then reads off the end of the grid.
    minX = static_cast<uint32_t>(std::max(0.0f, std::floor(loAcross)));
    minY = static_cast<uint32_t>(std::max(0.0f, std::floor(loAlong)));
    maxX = static_cast<uint32_t>(std::min(static_cast<float>(grid.Width() - 1),
                                          std::floor(hiAcross)));
    maxY = static_cast<uint32_t>(std::min(static_cast<float>(grid.Height() - 1),
                                          std::floor(hiAlong)));
    return true;
}

} // namespace

bool NavSystem::CellIsInsideBox(const NavBounds& bounds, uint32_t x, uint32_t y,
                                const glm::vec3& worldCenter, const glm::vec3& halfExtents) {
    const glm::vec3 cell = bounds.CellCenter(x, y);

    // Only the two axes the grid lies in. The third is the one a nav grid has
    // no opinion about: a bridge over a road and the road under it occupy the
    // same cells, and a grid that tested height would block the road for a
    // bridge nothing walks on - or, worse, would need to know how tall a unit
    // is, which is where a grid stops being a grid and becomes a voxel world.
    const glm::vec3 delta = cell - worldCenter;
    if (bounds.plane == NavBounds::Plane::XY) {
        return std::fabs(delta.x) <= halfExtents.x && std::fabs(delta.y) <= halfExtents.y;
    }
    return std::fabs(delta.x) <= halfExtents.x && std::fabs(delta.z) <= halfExtents.z;
}

uint32_t NavSystem::StampColliders(const entt::registry& registry, const NavBounds& bounds,
                                   NavGrid& grid) {
    uint32_t blocked = 0;

    const auto stamp = [&](const glm::vec3& center, const glm::vec3& half) {
        uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
        if (!CellRange(bounds, grid, center, half, minX, minY, maxX, maxY)) return;

        for (uint32_t y = minY; y <= maxY; ++y) {
            for (uint32_t x = minX; x <= maxX; ++x) {
                if (grid.Blocked(x, y)) continue;
                if (!CellIsInsideBox(bounds, x, y, center, half)) continue;
                grid.SetCost(x, y, NavGrid::kBlocked);
                ++blocked;
            }
        }
    };

    for (auto [entity, world, box] :
         registry.view<const WorldTransformComponent, const BoxColliderComponent>().each()) {
        if (box.isTrigger) continue;
        glm::vec3 center, half;
        WorldAabb(world.matrix, box.center, box.size * 0.5f, center, half);
        stamp(center, half);
    }

    for (auto [entity, world, sphere] :
         registry.view<const WorldTransformComponent, const SphereColliderComponent>().each()) {
        if (sphere.isTrigger) continue;

        // A sphere stamped as the BOX AROUND IT, deliberately: the cells a
        // sphere covers and the cells its bounding box covers differ only at
        // four corners, and a nav grid that is a little conservative sends a
        // unit around rather than through - which is the direction to be wrong
        // in.
        //
        // The radius is SCALED and the rotation DROPPED, which is the whole
        // difference between this and the box path above. Running a sphere
        // through the same corner-sum treats it as a local cube and then takes
        // the bounding box of that cube once it has been rotated, so a marker
        // turned forty-five degrees would block seventy per cent more ground
        // than the same marker square-on. That is a footprint that depends on
        // which way an entity is facing, for a shape with no facing.
        //
        // The longest of the three columns is the scale that grows the sphere
        // most, which is the conservative reading of a non-uniform scale - and
        // a sphere under one is not a sphere anyway.
        const glm::vec3 center = glm::vec3(world.matrix * glm::vec4(sphere.center, 1.0f));
        const float scale = std::max({ glm::length(glm::vec3(world.matrix[0])),
                                       glm::length(glm::vec3(world.matrix[1])),
                                       glm::length(glm::vec3(world.matrix[2])) });
        stamp(center, glm::vec3(sphere.radius * scale));
    }

    return blocked;
}

void NavSystem::Draw(const NavGrid& grid, const NavBounds& bounds, WorldShapes& shapes,
                     uint32_t stride) {
    if (grid.Width() == 0 || grid.Height() == 0) return;
    if (stride == 0) stride = 1;

    constexpr glm::vec4 kOutline(0.35f, 0.75f, 1.0f, 0.9f);
    constexpr glm::vec4 kWall(1.0f, 0.35f, 0.3f, 0.9f);
    constexpr glm::vec4 kFlow(0.4f, 1.0f, 0.55f, 0.9f);

    // Open ground that no goal can reach - walled off, or behind a corner the
    // strict diagonal rule refuses. Dimmer than a wall, because it is not one.
    constexpr glm::vec4 kStranded(0.85f, 0.7f, 0.25f, 0.75f);

    glm::vec3 min, max;
    bounds.WorldBounds(grid.Width(), grid.Height(), min, max);

    if (bounds.plane == NavBounds::Plane::XY) {
        // AddGroundRect is an XZ primitive and says so: it reads x and z from
        // the corners and flattens every one of them to a single height. On
        // the XY plane both corners share a z, so it would emit the bottom
        // edge, the bottom edge again backwards, and two segments of zero
        // length - one line where a rectangle was asked for, with the grid's
        // whole vertical extent thrown away. It would still report four lines,
        // which is why a count is not a picture.
        const glm::vec3 a(min.x, min.y, min.z);
        const glm::vec3 b(max.x, min.y, min.z);
        const glm::vec3 c(max.x, max.y, min.z);
        const glm::vec3 d(min.x, max.y, min.z);
        shapes.AddLine(a, b, kOutline);
        shapes.AddLine(b, c, kOutline);
        shapes.AddLine(c, d, kOutline);
        shapes.AddLine(d, a, kOutline);
    } else {
        shapes.AddGroundRect(min, max, kOutline);
    }

    const float half = bounds.cellSize * 0.4f;

    for (uint32_t y = 0; y < grid.Height(); y += stride) {
        for (uint32_t x = 0; x < grid.Width(); x += stride) {
            const glm::vec3 centre = bounds.CellCenter(x, y);

            // The two in-plane axes as world vectors, so one loop draws both
            // the ground case and the flat 2D one without asking which it is
            // in more than once.
            const glm::vec3 across(1.0f, 0.0f, 0.0f);
            const glm::vec3 along = bounds.plane == NavBounds::Plane::XY
                                        ? glm::vec3(0.0f, -1.0f, 0.0f)
                                        : glm::vec3(0.0f, 0.0f, 1.0f);

            if (grid.Blocked(x, y)) {
                shapes.AddLine(centre - across * half - along * half,
                               centre + across * half + along * half, kWall);
                shapes.AddLine(centre - across * half + along * half,
                               centre + across * half - along * half, kWall);
                continue;
            }

            if (!grid.FieldIsCurrent()) continue;

            const glm::vec3 normal = bounds.plane == NavBounds::Plane::XY
                                         ? glm::vec3(0.0f, 0.0f, 1.0f)
                                         : glm::vec3(0.0f, 1.0f, 0.0f);

            const glm::ivec2 flow = grid.FlowAt(x, y);
            if (flow.x == 0 && flow.y == 0) {
                // A goal, or a cell nothing reaches, and they are told apart
                // by the distance rather than by the flow - so both are drawn,
                // differently. An earlier version said that and then drew only
                // the goal, which left a cut-off region looking exactly like
                // ground the stride had skipped: the one question a nav
                // overlay is opened to answer, silently unanswerable.
                if (grid.DistanceAt(x, y) == 0) {
                    shapes.AddCircle(centre, half, kFlow, 12, normal);
                } else {
                    shapes.AddCircle(centre, half * 0.5f, kStranded, 8, normal);
                }
                continue;
            }

            // FROM THE CENTRE, not through it. A segment drawn symmetrically
            // about the cell centre is the same two points for a flow of
            // (1, 0) and one of (-1, 0), so the overlay could show which axis
            // a cell flowed along and never which way - which is the only
            // thing anybody looks at a flow field to see.
            const glm::vec3 direction =
                glm::normalize(across * static_cast<float>(flow.x) +
                               along * static_cast<float>(flow.y));
            shapes.AddLine(centre, centre + direction * half, kFlow);
        }
    }
}

} // namespace Supersonic
