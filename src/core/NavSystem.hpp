#pragma once

#include <entt/entt.hpp>

#include "core/NavGrid.hpp"

namespace Supersonic {

class WorldShapes;

// The two things a nav grid needs from the world: what is in the way, and a
// way to look at it.
//
// Separate from NavGrid because neither of them is a search. Stamping walks a
// registry and Draw walks a shape list; the grid itself is arithmetic over
// integers, and keeping it that way is what lets the part that goes quietly
// wrong be checked without a scene.
//
// DELIBERATELY NOT A COMPONENT, and that is the decision this file exists to
// record. A nav grid could be authored on an entity, serialised, shown in the
// inspector - about five sites per field, on the engine's own reckoning - and
// nothing in either game in front of this engine would read it. Wolf Brigade
// is a lane RTS whose units walk lanes; HUSK brings nine hundred lines of its
// own navigation. So a game OWNS a NavGrid, the way it owns its match state,
// and puts it in the oracle through StateHash::RegisterContributor. If a scene
// ever needs to author one, that is the commit that adds the component, and it
// will have a caller to shape it.
class NavSystem {
public:
    // Marks every cell a collider covers as blocked.
    //
    // Boxes and spheres, because those are the shapes a level is blocked out
    // with. Capsules and hulls are not stamped: a capsule is a character - a
    // thing that MOVES, and blocking the grid under it would wall units in
    // behind each other - and a hull is a mesh whose footprint is a projection
    // problem rather than a lookup. Both are refusals with a reason rather
    // than gaps, and the reason is the same one: a nav grid describes the
    // level, not its inhabitants.
    //
    // TRIGGERS ARE SKIPPED. A trigger volume exists to be walked into.
    //
    // Additive: it blocks cells and never clears one, so several calls compose
    // and the caller decides when to start again with Fill. Returns how many
    // cells it blocked that were not already blocked.
    static uint32_t StampColliders(const entt::registry& registry, const NavBounds& bounds,
                                   NavGrid& grid);

    // How much of a collider's footprint a cell must contain before the cell
    // counts as blocked... is a question this deliberately does not ask. A cell
    // whose CENTRE is inside the footprint is blocked, which is the cheap and
    // predictable rule; anything finer needs an agent radius to be meaningful,
    // and no caller has one yet.
    //
    // The consequence is written down rather than hidden: a wall thinner than
    // a cell can fall between two centres and not block anything. Choose a
    // cell size smaller than the thinnest wall in the level.
    static bool CellIsInsideBox(const NavBounds& bounds, uint32_t x, uint32_t y,
                                const glm::vec3& worldCenter, const glm::vec3& halfExtents);

    // Draws the grid into the world, for looking at rather than for shipping.
    //
    // Blocked cells get a cross, the outline gets a box, and where a field has
    // been built each passable cell gets a short line pointing the way it
    // flows. Lines only, because that is all WorldShapes has and all any of
    // this needs - see its header for why a filled shape is a different and
    // much larger problem.
    //
    // `stride` draws every nth cell, because a two-hundred-square grid is
    // forty thousand crosses and a viewport full of noise.
    static void Draw(const NavGrid& grid, const NavBounds& bounds, WorldShapes& shapes,
                     uint32_t stride = 1);
};

} // namespace Supersonic
