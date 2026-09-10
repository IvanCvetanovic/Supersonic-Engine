#include "Map.hpp"

#include <algorithm>
#include <cstdlib>

namespace husk {

bool pointInPoly(const std::vector<Vec2>& poly, Vec2 p) {
    if (poly.size() < 3) return false;
    bool inside = false;
    size_t j = poly.size() - 1;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i];
        const Vec2 b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

MapDef MapDef::m0TestMap() {
    MapDef def;
    def.half = 50.0f;
    def.obstacles = {
        // central wall, gaps at x in [-12,-6] and [8,14]
        Rect2::make(-30.0f, -1.5f, -12.0f, 1.5f),
        Rect2::make(-6.0f, -1.5f, 8.0f, 1.5f),
        Rect2::make(14.0f, -1.5f, 30.0f, 1.5f),
        // scattered blocks
        Rect2::make(20.0f, 15.0f, 28.0f, 23.0f),
        Rect2::make(-35.0f, 20.0f, -25.0f, 28.0f),
        Rect2::make(15.0f, -30.0f, 25.0f, -22.0f),
        Rect2::make(-20.0f, -25.0f, -12.0f, -17.0f),
        Rect2::make(-2.0f, 12.0f, 2.0f, 16.0f),
    };
    return def;
}

NavGrid NavGrid::fromDef(const MapDef& def) {
    NavGrid g;
    g.cell = 1.0f;
    g.dim = asU32((def.half * 2.0f) / g.cell);
    g.origin = Vec2::splat(-def.half);
    const size_t n = static_cast<size_t>(g.dim) * g.dim;
    g.blocked.assign(n, 0);
    g.level.assign(n, 0);
    g.ramp.assign(n, 0);
    for (uint32_t cy = 0; cy < g.dim; ++cy) {
        for (uint32_t cx = 0; cx < g.dim; ++cx) {
            const size_t i = static_cast<size_t>(cy) * g.dim + cx;
            const Vec2 center =
                g.origin + Vec2(static_cast<float>(cx) + 0.5f, static_cast<float>(cy) + 0.5f) * g.cell;
            if (std::any_of(def.obstacles.begin(), def.obstacles.end(),
                            [&](const Rect2& r) { return r.contains(center); })) {
                g.blocked[i] = 1;
            }
            for (const auto& [rect, l] : def.plateaus) {
                if (rect.contains(center)) g.level[i] = std::max(g.level[i], l);
            }
            for (const auto& [poly, l] : def.plateauPolys) {
                if (pointInPoly(poly, center)) g.level[i] = std::max(g.level[i], l);
            }
            if (std::any_of(def.ramps.begin(), def.ramps.end(),
                            [&](const Rect2& r) { return r.contains(center); })) {
                g.ramp[i] = 1;
            }
        }
    }
    return g;
}

bool NavGrid::stepOk(uint32_t from, uint32_t to) const {
    if (blocked[to]) return false;
    return level[from] == level[to] || ramp[from] || ramp[to];
}

bool NavGrid::stepOkWorld(Vec2 from, Vec2 to) const {
    if (isBlockedWorld(to)) return false;
    return stepOk(cellAt(from), cellAt(to));
}

uint32_t NavGrid::cellAt(Vec2 p) const {
    const Vec2 local = (p - origin) / cell;
    const int32_t hi = static_cast<int32_t>(dim) - 1;
    const uint32_t cx = static_cast<uint32_t>(std::clamp(asI32(local.x), 0, hi));
    const uint32_t cy = static_cast<uint32_t>(std::clamp(asI32(local.y), 0, hi));
    return idx(cx, cy);
}

Vec2 NavGrid::cellCenter(uint32_t i) const {
    const auto [cx, cy] = coords(i);
    return origin + Vec2(static_cast<float>(cx) + 0.5f, static_cast<float>(cy) + 0.5f) * cell;
}

uint8_t NavGrid::levelAt(Vec2 p) const {
    return level[cellAt(p)];
}

CellCoords NavGrid::cellCoords(Vec2 p) const {
    const Vec2 local = (p - origin) / cell;
    const int32_t hi = static_cast<int32_t>(dim) - 1;
    return {std::clamp(asI32(local.x), 0, hi), std::clamp(asI32(local.y), 0, hi)};
}

bool NavGrid::losClear(Vec2 from, Vec2 to) const {
    const auto [x0, y0] = cellCoords(from);
    const auto [x1, y1] = cellCoords(to);
    const uint8_t eye = level[idx(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0))];
    const uint32_t tidx = idx(static_cast<uint32_t>(x1), static_cast<uint32_t>(y1));
    if (level[tidx] > eye && !ramp[tidx]) return false; // can't see onto higher ground
    if (x0 == x1 && y0 == y1) return true;
    const int32_t dx = std::abs(x1 - x0);
    const int32_t dy = -std::abs(y1 - y0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    int32_t x = x0;
    int32_t y = y0;
    for (;;) {
        const int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
        if (x == x1 && y == y1) return true;
        const uint32_t i = idx(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        if (level[i] > eye && !ramp[i]) return false; // higher ground occludes
    }
}

bool NavGrid::isBlockedWorld(Vec2 p) const {
    const Vec2 local = (p - origin) / cell;
    if (local.x < 0.0f || local.y < 0.0f) return true;
    const int32_t cx = asI32(local.x);
    const int32_t cy = asI32(local.y);
    if (cx >= static_cast<int32_t>(dim) || cy >= static_cast<int32_t>(dim)) return true;
    return blocked[static_cast<uint32_t>(cy) * dim + static_cast<uint32_t>(cx)] != 0;
}

std::pair<Vec2, CellCoords> NavGrid::snapFootprint(Vec2 center, uint32_t w, uint32_t h) const {
    const Vec2 local = (center - origin) / cell;
    const float wf = static_cast<float>(w);
    const float hf = static_cast<float>(h);
    const int32_t cx = std::clamp(asI32(std::round(local.x - wf * 0.5f)), 0,
                                  static_cast<int32_t>(dim) - static_cast<int32_t>(w));
    const int32_t cy = std::clamp(asI32(std::round(local.y - hf * 0.5f)), 0,
                                  static_cast<int32_t>(dim) - static_cast<int32_t>(h));
    const Vec2 snapped =
        origin + Vec2(static_cast<float>(cx) + wf * 0.5f, static_cast<float>(cy) + hf * 0.5f) * cell;
    return {snapped, {cx, cy}};
}

std::vector<uint32_t> NavGrid::footprintCells(CellCoords min, uint32_t w, uint32_t h) const {
    std::vector<uint32_t> cells;
    cells.reserve(static_cast<size_t>(w) * h);
    for (int32_t dy = 0; dy < static_cast<int32_t>(h); ++dy) {
        for (int32_t dx = 0; dx < static_cast<int32_t>(w); ++dx) {
            const int32_t x = min.first + dx;
            const int32_t y = min.second + dy;
            if (x >= 0 && y >= 0 && x < static_cast<int32_t>(dim) && y < static_cast<int32_t>(dim)) {
                cells.push_back(idx(static_cast<uint32_t>(x), static_cast<uint32_t>(y)));
            }
        }
    }
    return cells;
}

bool NavGrid::cellsFree(const std::vector<uint32_t>& cells) const {
    return std::all_of(cells.begin(), cells.end(), [&](uint32_t c) { return !isBlockedIdx(c); });
}

void NavGrid::setBlocked(const std::vector<uint32_t>& cells, bool value) {
    for (uint32_t c : cells) blocked[c] = value ? 1 : 0;
}

uint32_t NavGrid::nearestOpen(uint32_t i) const {
    if (!isBlockedIdx(i)) return i;
    const auto [ucx, ucy] = coords(i);
    const int32_t cx = static_cast<int32_t>(ucx);
    const int32_t cy = static_cast<int32_t>(ucy);
    const int32_t d = static_cast<int32_t>(dim);
    for (int32_t r = 1; r < d; ++r) {
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int32_t nx = cx + dx;
                const int32_t ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= d || ny >= d) continue;
                const uint32_t n = idx(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                if (!isBlockedIdx(n)) return n;
            }
        }
    }
    return i;
}

} // namespace husk
