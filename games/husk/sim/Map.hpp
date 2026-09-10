#pragma once

// The map definition and the navigation grid derived from it (src/sim/map.rs).
//
// Sim positions are on the ground plane: `.x` is world X and `.y` is world Z.
// The grid is square cells of 1 world unit; each cell is blocked or open, has a
// terrain tier, and may be a ramp. Stepping between tiers is only legal across
// a ramp.

#include "Vec2.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace husk {

struct Rect2 {
    Vec2 min;
    Vec2 max;

    static constexpr Rect2 make(float minX, float minY, float maxX, float maxY) {
        return {{minX, minY}, {maxX, maxY}};
    }

    bool contains(Vec2 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
};

// Even-odd ray cast. Any winding; fewer than three vertices is never inside.
bool pointInPoly(const std::vector<Vec2>& poly, Vec2 p);

struct MapDef {
    float half = 50.0f; // the world spans [-half, half] on both axes
    std::vector<Rect2> obstacles;
    std::vector<Rect2> roads; // client-only striping
    std::vector<std::pair<Rect2, uint8_t>> plateaus;
    std::vector<std::pair<std::vector<Vec2>, uint8_t>> plateauPolys;
    std::vector<Rect2> ramps;
    std::vector<std::array<float, 4>> bumps; // (x, y, radius, height), client-only

    // The 100x100 M0 test map: a wall across the middle with two gaps, and
    // scattered blocks.
    static MapDef m0TestMap();
};

using CellCoords = std::pair<int32_t, int32_t>;

struct NavGrid {
    uint32_t dim = 0;
    float cell = 1.0f;
    Vec2 origin;
    std::vector<uint8_t> blocked; // bool per cell
    std::vector<uint8_t> level;
    std::vector<uint8_t> ramp; // bool per cell

    static NavGrid fromDef(const MapDef& def);

    // Same tier, or either side a ramp; the destination must be open.
    bool stepOk(uint32_t from, uint32_t to) const;
    // In bounds, open, and not across a cliff edge.
    bool stepOkWorld(Vec2 from, Vec2 to) const;

    float halfExtent() const { return static_cast<float>(dim) * cell * 0.5f; }
    uint32_t idx(uint32_t cx, uint32_t cy) const { return cy * dim + cx; }
    std::pair<uint32_t, uint32_t> coords(uint32_t i) const { return {i % dim, i / dim}; }

    // The cell containing a world position, clamped onto the grid.
    uint32_t cellAt(Vec2 p) const;
    Vec2 cellCenter(uint32_t i) const;
    uint8_t levelAt(Vec2 p) const;
    CellCoords cellCoords(Vec2 p) const;

    // Combat line of sight: only terrain HEIGHT occludes, by integer Bresenham.
    bool losClear(Vec2 from, Vec2 to) const;

    bool isBlockedIdx(uint32_t i) const { return blocked[i] != 0; }
    // Blocked cell, or outside the map.
    bool isBlockedWorld(Vec2 p) const;

    // Snap a desired centre so a (w, h)-cell footprint aligns to the grid.
    std::pair<Vec2, CellCoords> snapFootprint(Vec2 center, uint32_t w, uint32_t h) const;
    std::vector<uint32_t> footprintCells(CellCoords min, uint32_t w, uint32_t h) const;
    bool cellsFree(const std::vector<uint32_t>& cells) const;
    void setBlocked(const std::vector<uint32_t>& cells, bool value);

    // The nearest open cell, the cell itself if open; a ring scan in fixed
    // order.
    uint32_t nearestOpen(uint32_t i) const;
};

} // namespace husk
