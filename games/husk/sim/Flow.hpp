#pragma once

// Flow-field pathfinding (src/sim/flow.rs): per goal cell, a Dijkstra
// integration field over the nav grid, then a per-cell descent direction.
// Fields are cached per goal and shared by every unit ordered there.

#include "Map.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace husk {

inline constexpr uint32_t kUnreachable = UINT32_MAX;

struct FlowField {
    uint32_t goal = 0;
    std::vector<uint32_t> cost; // kUnreachable where no path exists
    std::vector<Vec2> dir;      // zero at the goal, in blocked cells and where unreachable

    static FlowField compute(const NavGrid& grid, uint32_t goal);
};

// Keyed by goal cell; std::map so any iteration is in key order, as the
// game's BTreeMap is.
using FlowFields = std::map<uint32_t, FlowField>;

} // namespace husk
