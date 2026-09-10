#include "Flow.hpp"

#include <functional>
#include <queue>
#include <utility>

namespace husk {

namespace {

struct Neighbor {
    int32_t dx;
    int32_t dy;
    uint32_t step; // cost x10
};

// 8-connected. Diagonals only when both orthogonal cells are steppable (no
// corner cutting); the same rule picks descent directions.
constexpr Neighbor kNeighbors[8] = {
    {1, 0, 10}, {-1, 0, 10}, {0, 1, 10}, {0, -1, 10},
    {1, 1, 14}, {1, -1, 14}, {-1, 1, 14}, {-1, -1, 14},
};

// std::f32::consts::FRAC_1_SQRT_2, rounded to f32 at compile time as Rust's
// constant is.
constexpr float kDiag = 0.707106781186547524400844362104849039f;

constexpr Vec2 kDirs[8] = {
    {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
    {kDiag, kDiag}, {kDiag, -kDiag}, {-kDiag, kDiag}, {-kDiag, -kDiag},
};

} // namespace

FlowField FlowField::compute(const NavGrid& grid, uint32_t goal) {
    const size_t n = static_cast<size_t>(grid.dim) * grid.dim;
    const int32_t dim = static_cast<int32_t>(grid.dim);
    FlowField f;
    f.goal = goal;
    f.cost.assign(n, kUnreachable);

    // Dijkstra from the goal outward. The game pops a BinaryHeap of
    // Reverse((cost, idx)); a min-queue of the same pairs pops the same
    // sequence, and final costs do not depend on pop order anyway.
    using Entry = std::pair<uint32_t, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
    f.cost[goal] = 0;
    heap.push({0, goal});
    while (!heap.empty()) {
        const auto [c, i] = heap.top();
        heap.pop();
        if (c > f.cost[i]) continue;
        const auto [ucx, ucy] = grid.coords(i);
        const int32_t cx = static_cast<int32_t>(ucx);
        const int32_t cy = static_cast<int32_t>(ucy);
        for (size_t k = 0; k < 8; ++k) {
            const int32_t nx = cx + kNeighbors[k].dx;
            const int32_t ny = cy + kNeighbors[k].dy;
            if (nx < 0 || ny < 0 || nx >= dim || ny >= dim) continue;
            const uint32_t ni = grid.idx(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
            // tier-aware: blocked cells and bare cliff edges both stop expansion
            if (!grid.stepOk(i, ni)) continue;
            if (k >= 4) {
                const uint32_t a = grid.idx(static_cast<uint32_t>(cx), static_cast<uint32_t>(ny));
                const uint32_t b = grid.idx(static_cast<uint32_t>(nx), static_cast<uint32_t>(cy));
                if (!grid.stepOk(i, a) || !grid.stepOk(i, b)) continue;
            }
            const uint32_t nc = c + kNeighbors[k].step;
            if (nc < f.cost[ni]) {
                f.cost[ni] = nc;
                heap.push({nc, ni});
            }
        }
    }

    // Descend the gradient. Strict < resolves ties to the first neighbour in
    // fixed order.
    f.dir.assign(n, kZero);
    for (uint32_t i = 0; i < static_cast<uint32_t>(n); ++i) {
        const uint32_t c = f.cost[i];
        if (c == 0 || c == kUnreachable || grid.isBlockedIdx(i)) continue;
        const auto [ucx, ucy] = grid.coords(i);
        const int32_t cx = static_cast<int32_t>(ucx);
        const int32_t cy = static_cast<int32_t>(ucy);
        uint32_t best = c;
        Vec2 bestDir = kZero;
        for (size_t k = 0; k < 8; ++k) {
            const int32_t nx = cx + kNeighbors[k].dx;
            const int32_t ny = cy + kNeighbors[k].dy;
            if (nx < 0 || ny < 0 || nx >= dim || ny >= dim) continue;
            const uint32_t ni = grid.idx(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
            if (!grid.stepOk(i, ni)) continue;
            if (k >= 4) {
                const uint32_t a = grid.idx(static_cast<uint32_t>(cx), static_cast<uint32_t>(ny));
                const uint32_t b = grid.idx(static_cast<uint32_t>(nx), static_cast<uint32_t>(cy));
                if (!grid.stepOk(i, a) || !grid.stepOk(i, b)) continue;
            }
            if (f.cost[ni] < best) {
                best = f.cost[ni];
                bestDir = kDirs[k];
            }
        }
        f.dir[i] = bestDir;
    }
    return f;
}

} // namespace husk
