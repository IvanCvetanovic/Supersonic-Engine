#include "core/NavGrid.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace Supersonic {

namespace {

// The eight neighbours, in a fixed order, straight ones first.
//
// The ORDER IS PART OF THE ANSWER, not a detail of the loop. Two neighbours
// can be exactly the same distance away - which on a uniform grid is the rule
// rather than the exception - and FlowAt breaks that tie by taking the first
// it meets. A different order is a different, equally correct field, and a
// field that changed between runs would send a unit down one side of an
// obstacle here and the other side there.
//
// Straight before diagonal, which decides a tie between the two - and it is
// worth being exact about when that tie happens, because it is rarer than it
// looks. FlowAt compares the neighbours' own distances, and a straight and a
// diagonal predecessor of the same cell never hold the same one: reaching a
// cell of cost c costs ten from the straight neighbour and fourteen from the
// diagonal, so a diagonal that is equally optimal sits four times c LOWER and
// wins on its own merits. Both are shortest paths, so either is a correct
// answer.
//
// The order decides an EXACT tie - two neighbours at the same distance, which
// on a uniform grid is the ordinary case: two goals either side of a cell, two
// ways round an obstacle of equal length. What matters there is not which one
// is chosen but that the same one is chosen on every machine.
struct Neighbour {
    int32_t dx;
    int32_t dy;
    uint32_t step;
};

constexpr Neighbour kNeighbours[8] = {
    { 0, -1, NavGrid::kStraightStep },
    { -1, 0, NavGrid::kStraightStep },
    { 1, 0, NavGrid::kStraightStep },
    { 0, 1, NavGrid::kStraightStep },
    { -1, -1, NavGrid::kDiagonalStep },
    { 1, -1, NavGrid::kDiagonalStep },
    { -1, 1, NavGrid::kDiagonalStep },
    { 1, 1, NavGrid::kDiagonalStep },
};

} // namespace

bool NavGrid::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (static_cast<size_t>(width) * height > kMaxCells) return false;

    m_width = width;
    m_height = height;
    m_costs.assign(CellCount(), kNormal);
    m_distances.assign(CellCount(), kUnreachable);
    m_goals.clear();
    m_fieldBuilt = false;
    return true;
}

NavGrid::Cost NavGrid::CostAt(uint32_t x, uint32_t y) const {
    if (!Contains(x, y)) return kBlocked;
    return m_costs[static_cast<size_t>(y) * m_width + x];
}

bool NavGrid::SetCost(uint32_t x, uint32_t y, Cost cost) {
    if (!Contains(x, y)) return false;
    m_costs[static_cast<size_t>(y) * m_width + x] = cost;

    // THE FIELD IS NOW STALE, and saying so is what stops it being read as
    // current. The alternative - rebuilding here - would rebuild once per
    // stamped wall while a level loaded, which is the search run a thousand
    // times to answer a question nobody had asked yet.
    m_fieldBuilt = false;
    return true;
}

void NavGrid::Fill(Cost cost) {
    m_costs.assign(CellCount(), cost);
    m_fieldBuilt = false;
}

uint32_t NavGrid::Build(const std::vector<Cell>& goals) {
    m_distances.assign(CellCount(), kUnreachable);
    m_goals.clear();
    m_fieldBuilt = true;

    if (CellCount() == 0) return 0;

    // A BUCKET PER DISTANCE, IN AN ORDERED MAP, and the ordering is the whole
    // reason rather than the speed.
    //
    // Distances are drained smallest first and each bucket in the order things
    // were pushed into it, so the sequence in which cells are expanded is a
    // function of the grid alone. A std::priority_queue would not be: it says
    // nothing about the order of equal elements, and on a uniform grid equal
    // distances are the ORDINARY case rather than the exception - which is the
    // same defect this engine has already fixed twice, in the blended draw
    // sort and in the particle sort, both times because one float through
    // std::sort is not a total order.
    //
    // Dial's flat array of buckets is the faster structure and was the first
    // version of this. It sizes its window on the largest single step, which
    // is fourteen times the most expensive cell - so a grid with one cell of
    // cost 65535 in it allocates nine hundred thousand empty vectors, and the
    // structure's cost depends on a number the caller typed rather than on the
    // size of the search. A map costs a log per push and cannot be surprised.
    std::map<uint32_t, std::vector<uint32_t>> frontier;
    uint32_t reached = 0;

    const auto push = [&frontier](uint32_t index, uint32_t distance) {
        frontier[distance].push_back(index);
    };

    for (const Cell& goal : goals) {
        if (!Contains(goal.x, goal.y)) continue;

        // A WALL IS NOT A DESTINATION. Seeding one would flood a field out of
        // a cell nothing can stand in, and every unit in the level would walk
        // confidently into it.
        if (Blocked(goal.x, goal.y)) continue;

        const size_t index = static_cast<size_t>(goal.y) * m_width + goal.x;
        if (m_distances[index] == 0) continue;   // named twice

        m_distances[index] = 0;
        m_goals.push_back(goal);
        push(static_cast<uint32_t>(index), 0);
        ++reached;
    }

    if (m_goals.empty()) return 0;

    while (!frontier.empty()) {
        const uint32_t distance = frontier.begin()->first;

        // Taken OUT of the map before it is walked. Expanding a cell pushes
        // into the map, which invalidates nothing here - every push lands at a
        // strictly greater distance, since the cheapest step is ten - but
        // holding a reference into a container being written to is a hazard
        // that does not need to be reasoned about twice.
        std::vector<uint32_t> bucket = std::move(frontier.begin()->second);
        frontier.erase(frontier.begin());

        for (const uint32_t index : bucket) {
            // STALE ENTRY. A cell is pushed every time a shorter route to it
            // is found, so the same cell can sit in several buckets; the one
            // that matters is the one whose distance still agrees with the
            // field. Cheaper than finding and removing the old entry, and it
            // is the standard way a bucket queue stays a queue.
            if (m_distances[index] != distance) continue;

            const uint32_t x = static_cast<uint32_t>(index % m_width);
            const uint32_t y = static_cast<uint32_t>(index / m_width);

            for (const Neighbour& neighbour : kNeighbours) {
                const int64_t nx = static_cast<int64_t>(x) + neighbour.dx;
                const int64_t ny = static_cast<int64_t>(y) + neighbour.dy;
                if (nx < 0 || ny < 0) continue;

                const auto ux = static_cast<uint32_t>(nx);
                const auto uy = static_cast<uint32_t>(ny);
                const Cost cost = CostAt(ux, uy);
                if (cost == kBlocked) continue;

                // NO CUTTING A CORNER. A diagonal step between two walls
                // passes through the seam where they meet, which is not a gap
                // - a unit taking it walks through the join of two buildings.
                // Refused when EITHER orthogonal neighbour is blocked rather
                // than only when both are, because a unit has width and the
                // one that squeezes past a single wall's corner clips it.
                if (neighbour.dx != 0 && neighbour.dy != 0) {
                    if (Blocked(static_cast<uint32_t>(static_cast<int64_t>(x) + neighbour.dx), y)) {
                        continue;
                    }
                    if (Blocked(x, static_cast<uint32_t>(static_cast<int64_t>(y) + neighbour.dy))) {
                        continue;
                    }
                }

                // Fourteen times the dearest cell is 917,490, which fits;
                // the RUNNING TOTAL is what does not. A path may cross a
                // million cells, so a distance can reach nine hundred billion
                // - and an unsigned sum that wraps produces a small number,
                // which then passes the "is this shorter" test below, gets
                // stored, and gets pushed into a bucket BELOW the one being
                // drained. The field would be wrong and the queue's ordering
                // claim would be false, both silently.
                //
                // Refused rather than saturated: a cell whose true distance
                // does not fit is unreachable in every sense a game cares
                // about, and clamping it to the largest representable value
                // would make a wall of a place and then route through it.
                const uint32_t stepCost = neighbour.step * cost;
                if (distance > kMaxDistance - stepCost) continue;
                const uint32_t candidate = distance + stepCost;

                const size_t neighbourIndex = static_cast<size_t>(uy) * m_width + ux;
                if (candidate >= m_distances[neighbourIndex]) continue;

                if (m_distances[neighbourIndex] == kUnreachable) ++reached;
                m_distances[neighbourIndex] = candidate;
                push(static_cast<uint32_t>(neighbourIndex), candidate);
            }
        }
    }

    return reached;
}

uint32_t NavGrid::DistanceAt(uint32_t x, uint32_t y) const {
    if (!Contains(x, y)) return kUnreachable;
    return m_distances[static_cast<size_t>(y) * m_width + x];
}

glm::ivec2 NavGrid::FlowAt(uint32_t x, uint32_t y) const {
    const uint32_t here = DistanceAt(x, y);
    if (here == kUnreachable || here == 0) return glm::ivec2(0, 0);

    glm::ivec2 best(0, 0);
    uint32_t bestDistance = here;

    for (const Neighbour& neighbour : kNeighbours) {
        const int64_t nx = static_cast<int64_t>(x) + neighbour.dx;
        const int64_t ny = static_cast<int64_t>(y) + neighbour.dy;
        if (nx < 0 || ny < 0) continue;

        const auto ux = static_cast<uint32_t>(nx);
        const auto uy = static_cast<uint32_t>(ny);
        if (Blocked(ux, uy)) continue;

        // The same corner rule the search used. Without it here the field
        // would route around a corner and the flow would send a unit through
        // it - the two would be answering different questions about the same
        // grid, which is the shape of bug that only shows up as a unit
        // occasionally walking into a building.
        if (neighbour.dx != 0 && neighbour.dy != 0) {
            if (Blocked(static_cast<uint32_t>(static_cast<int64_t>(x) + neighbour.dx), y)) continue;
            if (Blocked(x, static_cast<uint32_t>(static_cast<int64_t>(y) + neighbour.dy))) continue;
        }

        const uint32_t there = DistanceAt(ux, uy);
        if (there == kUnreachable) continue;

        // STRICTLY closer, so a tie leaves the earlier neighbour in place and
        // the fixed order above decides. Equal-or-closer would let the last
        // neighbour win every tie, which is the diagonal - and a field of
        // diagonal steps across open ground is a unit walking a staircase to
        // a point straight ahead of it.
        if (there < bestDistance) {
            bestDistance = there;
            best = glm::ivec2(neighbour.dx, neighbour.dy);
        }
    }

    return best;
}

uint64_t NavGrid::Hash() const {
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](const void* data, size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < bytes; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ull;
        }
    };

    mix(&m_width, sizeof(m_width));
    mix(&m_height, sizeof(m_height));
    if (!m_costs.empty()) mix(m_costs.data(), m_costs.size() * sizeof(Cost));

    // The goals, in the order they were accepted. Two fields over one grid
    // with the goals named in a different order are the same field, but they
    // are not the same STATE: a game that stores its rally points in a
    // different order on two machines has already diverged somewhere else,
    // and an oracle that hid it would be hiding the cause.
    const auto goalCount = static_cast<uint32_t>(m_goals.size());
    mix(&goalCount, sizeof(goalCount));
    for (const Cell& goal : m_goals) {
        mix(&goal.x, sizeof(goal.x));
        mix(&goal.y, sizeof(goal.y));
    }

    // AND WHETHER THE FIELD IS CURRENT, which is not bookkeeping here. Two
    // grids with the same costs and the same goals answer FlowAt differently
    // when one of them has had a wall built since its last search: a wall
    // built and then searched routes around it, and a wall built after the
    // search does not. Without this the oracle would call those two worlds
    // identical while the units in them walked different ways.
    const unsigned char current = m_fieldBuilt ? 1u : 0u;
    mix(&current, 1);

    return hash == 0 ? 1ull : hash;
}

// ---------------------------------------------------------------------------

glm::vec3 NavBounds::CellCenter(uint32_t x, uint32_t y) const {
    const float across = (static_cast<float>(x) + 0.5f) * cellSize;
    const float along = (static_cast<float>(y) + 0.5f) * cellSize;

    if (plane == Plane::XY) {
        // ROW ZERO AT THE TOP, counting DOWN, exactly as a tilemap does - the
        // 2D lane is authored that way throughout and a nav grid that
        // disagreed would be upside down against the map it navigates.
        return origin + glm::vec3(across, -along, 0.0f);
    }
    return origin + glm::vec3(across, 0.0f, along);
}

bool NavBounds::CellAt(const glm::vec3& world, uint32_t width, uint32_t height,
                       uint32_t& outX, uint32_t& outY) const {
    if (cellSize <= 0.0f) return false;

    const glm::vec3 local = world - origin;
    const float across = local.x;
    const float along = plane == Plane::XY ? -local.y : local.z;

    // Written against the extent rather than against a floored index: floor of
    // a slightly negative number is -1, and a cast of that to unsigned is a
    // very large column that passes every bounds check there is.
    if (!(across >= 0.0f) || !(along >= 0.0f)) return false;

    const float cellX = across / cellSize;
    const float cellY = along / cellSize;
    if (cellX >= static_cast<float>(width) || cellY >= static_cast<float>(height)) return false;

    outX = static_cast<uint32_t>(cellX);
    outY = static_cast<uint32_t>(cellY);
    return true;
}

void NavBounds::WorldBounds(uint32_t width, uint32_t height, glm::vec3& outMin,
                            glm::vec3& outMax) const {
    const float w = static_cast<float>(width) * cellSize;
    const float h = static_cast<float>(height) * cellSize;

    if (plane == Plane::XY) {
        outMin = origin + glm::vec3(0.0f, -h, 0.0f);
        outMax = origin + glm::vec3(w, 0.0f, 0.0f);
        return;
    }
    outMin = origin;
    outMax = origin + glm::vec3(w, 0.0f, h);
}

} // namespace Supersonic
