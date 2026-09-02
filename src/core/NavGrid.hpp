#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// A grid of integer movement costs, and the flow field that solves it.
//
// WHY A FLOW FIELD AND NOT A* PER AGENT. The shape of game this engine has in
// front of it is an RTS: many units, few destinations. A* answers "how does
// THIS unit get THERE" and costs a search per unit; a flow field answers "from
// anywhere, which way is the goal" and costs one search for the whole army. At
// two hundred units heading for one rally point that is two hundred searches
// against one. The plan this engine is built to says nav stops after the grid
// unless a game asks for more, and this is the half that serves the many.
//
// WHY INTEGERS, EVERYWHERE. This engine's differentiator is that a simulation
// reproduces bit for bit, and a cost field summed in floats does not: addition
// is not associative in floating point, so a frontier expanded in a different
// order gives a different total, and two machines that disagree about a
// distance disagree about where a unit walks. Integers make the arithmetic
// exact and the tie-breaks decidable, which is the whole reason a navmesh is
// not being vendored - see the roadmap, which refuses Detour on exactly this
// ground.
//
// NO WORLD, NO REGISTRY, NO COMPONENTS. This is a grid of numbers. Where it
// sits in the world is NavBounds below, which is a separate and separately
// tested thing, and what stamps obstacles into it is the caller's business.
// The reason is the one the sprite and tilemap systems already gave: the
// arithmetic is where this goes wrong, it goes wrong quietly, and it is only
// checkable if it can be reached without a device or a scene.
class NavGrid {
public:
    // What a cell costs to ENTER, as a multiplier on the step. One is ordinary
    // ground; higher is mud, water, a road nobody wants to leave.
    using Cost = uint16_t;

    // ZERO IS A WALL, and it is a sentinel rather than a very high price. A
    // cost of zero would otherwise mean "free", which is the opposite of what
    // anybody typing it into a tile means - and "very expensive" is a thing a
    // path can still be forced through, while a wall must not be.
    static constexpr Cost kBlocked = 0;
    static constexpr Cost kNormal = 1;

    // What a step over one cell edge costs before the destination's own cost
    // multiplies it. Ten and fourteen, because 14/10 is 1.4 and the true ratio
    // is 1.41421 - the classic integer approximation, wrong by three parts in a
    // thousand, and exact in a way that sqrt(2.0f) is not.
    static constexpr uint32_t kStraightStep = 10;
    static constexpr uint32_t kDiagonalStep = 14;

    // No path from here. Distinct from a very large distance so a caller can
    // tell "the goal is far" from "the goal cannot be reached at all", which
    // are different things to a unit deciding whether to walk or to give up.
    static constexpr uint32_t kUnreachable = 0xFFFFFFFFu;

    // The largest number that is a distance rather than the sentinel. The
    // search refuses a step that would carry a total past it, which is what
    // keeps an unsigned sum from wrapping into a small number the field would
    // then believe.
    static constexpr uint32_t kMaxDistance = kUnreachable - 1;

    struct Cell {
        uint32_t x{0};
        uint32_t y{0};

        bool operator==(const Cell& other) const { return x == other.x && y == other.y; }
    };

    // Cells past this are refused. A cell is a uint16 cost and a uint32
    // distance, six bytes, so a grid at the cap is SIX megabytes and a
    // Dijkstra over it is a million pushes - already past what a frame can
    // absorb, and well past what any game here has asked for. A bigger world
    // is several grids, or a coarser cell.
    static constexpr size_t kMaxCells = 1u << 20;

    // Sizes the grid and sets every cell to ordinary ground. Refuses a grid
    // past the cap or with a zero side, leaving the grid as it was.
    bool Resize(uint32_t width, uint32_t height);

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    size_t CellCount() const { return static_cast<size_t>(m_width) * m_height; }
    bool Contains(uint32_t x, uint32_t y) const { return x < m_width && y < m_height; }

    // Blocked for anything outside the grid, so a caller walking neighbours
    // needs no bounds check of its own - and so an agent at the edge of the
    // world does not step off it.
    Cost CostAt(uint32_t x, uint32_t y) const;
    bool Blocked(uint32_t x, uint32_t y) const { return CostAt(x, y) == kBlocked; }

    // False for a cell outside the grid. The grid is not resized to fit.
    bool SetCost(uint32_t x, uint32_t y, Cost cost);
    void Fill(Cost cost);

    // ---- The field ------------------------------------------------------

    // Integer Dijkstra outward from every goal at once, filling the distance
    // field. Returns how many cells were reached.
    //
    // MULTI-SOURCE BY DESIGN. One field for "the nearest of these four
    // barracks" costs exactly one search, and is what a game asking "go home"
    // actually means. A goal outside the grid, or on a blocked cell, is
    // ignored - a wall is not a destination, and silently seeding one would
    // hand back a field that flows into geometry.
    //
    // DETERMINISTIC, and that is a property of the queue rather than of the
    // costs. Cells are expanded in bucket order, and inside one bucket in the
    // order they were pushed, so two runs over the same grid expand the same
    // cells in the same sequence. A std::priority_queue would not: it says
    // nothing about equal elements, and equal distances are the RULE on a
    // uniform grid rather than the exception - the same defect this engine
    // already fixed twice, once for blended draws and once for particles.
    uint32_t Build(const std::vector<Cell>& goals);

    // What the LAST SEARCH found, which is not the same as what the grid says
    // now. `kUnreachable` for a cell outside the grid and for one the search
    // could not reach - which included every blocked cell AT THE TIME IT RAN.
    // A cell blocked since then keeps the distance it had, because nothing
    // has re-searched; FieldIsCurrent is how a caller knows to, and reading a
    // stale field is a caller's decision rather than a hidden one.
    uint32_t DistanceAt(uint32_t x, uint32_t y) const;

    // WHICH WAY TO WALK, as the offset of the neighbour to step onto. (0, 0)
    // means stay: the cell is a goal, is blocked, is unreachable, or has no
    // neighbour closer than itself.
    //
    // Derived from the distance field on demand rather than stored. A stored
    // direction is a second copy of the answer that can disagree with the
    // first - and the cost of deriving it is eight reads, against a megabyte
    // of grid that would otherwise be walked twice to fill it.
    glm::ivec2 FlowAt(uint32_t x, uint32_t y) const;

    // Whether Build has been run against the costs as they are now. Any write
    // to a cost clears it, so a field can never be read as current after the
    // wall it routes around has moved.
    bool FieldIsCurrent() const { return m_fieldBuilt; }

    // A number standing for the whole grid - size, costs, the goals the field
    // was built from, and whether that field is still current.
    //
    // For StateHash::RegisterContributor, which is how a game whose state is
    // not in components gets that state into the oracle. A grid IS simulation
    // state the moment a tick writes it: a wall knocked down on one tick is
    // read by every unit on the next, and a replay that did not see the change
    // would diverge silently.
    //
    // The DISTANCES are not hashed, because they are a pure function of the
    // three things that are. That is only true while the field is current,
    // which is exactly why the fourth thing is in there: a wall built and then
    // searched, and a wall built after the search, have identical costs and
    // identical goals and send their units different ways.
    uint64_t Hash() const;

private:
    uint32_t m_width{0};
    uint32_t m_height{0};
    std::vector<Cost> m_costs;
    std::vector<uint32_t> m_distances;
    std::vector<Cell> m_goals;
    bool m_fieldBuilt{false};
};

// Where a grid sits in the world.
//
// Separate from the grid on purpose, and it is the same split the tilemap
// makes between its bake and its cell arithmetic: one of these is a search and
// the other is a coordinate change, they fail in completely different ways, and
// only the second one needs to know which way up the world is.
struct NavBounds {
    // The world position of the OUTER CORNER of cell (0, 0) - not its middle.
    // A corner is what a caller has when it says "the map starts here", and a
    // centre is what it has to derive; putting the derivation in the wrong
    // place puts every cell half a cell out, which reads as an off-by-one in
    // the pathing rather than in the setup.
    glm::vec3 origin{0.0f};

    // World units per cell, both axes. One number, because a non-square cell
    // makes the ten-and-fourteen step costs wrong by whatever the ratio is,
    // and a nav grid that lies about its diagonals routes units into walls to
    // save a distance that was never there.
    float cellSize{1.0f};

    // WHICH PLANE THE GRID LIES IN. The 3D lane walks on XZ, with y up; the 2D
    // lane is authored in XY, and its ground is the screen. Both are real here
    // now, so this is a choice rather than an assumption - and an assumption
    // is what would silently put a 2D game's nav grid edge-on to its own world.
    enum class Plane { XZ, XY };
    Plane plane{Plane::XZ};

    // The world position of the MIDDLE of a cell, which is where a unit
    // walking to it is walking to.
    glm::vec3 CellCenter(uint32_t x, uint32_t y) const;

    // Which cell a world position is in. False when it is outside the grid the
    // caller describes; the size is passed in rather than stored, because the
    // grid owns its own size and two copies of a number are one too many.
    bool CellAt(const glm::vec3& world, uint32_t width, uint32_t height,
                uint32_t& outX, uint32_t& outY) const;

    // The two world corners of the whole grid, for a debug overlay and for a
    // caller deciding whether a thing is inside it at all.
    void WorldBounds(uint32_t width, uint32_t height, glm::vec3& outMin, glm::vec3& outMax) const;
};

} // namespace Supersonic
