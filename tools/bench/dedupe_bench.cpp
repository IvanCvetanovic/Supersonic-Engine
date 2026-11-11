// Convex-hull point deduplication: the grid against the all-pairs scan it replaced.
//
// A mesh lists each vertex about once per face that uses it, so the scan - quadratic
// in the points that survive - was seconds on a dense mesh. The grid is linear and
// keeps the same points in the same order (test_convexhull proves the equivalence on
// randomised inputs; this only measures it). The all-pairs column is skipped above
// 20000 points because that is where it stops being worth waiting for.
//
//     cmake -B build -DCMAKE_BUILD_TYPE=Release -DSUPERSONIC_BUILD_BENCHMARKS=ON
//     cmake --build build --target dedupe_bench && ./build/tools/bench/dedupe_bench
#include "core/ConvexHull.hpp"
#include <chrono>
#include <cstdio>
#include <random>
using namespace Supersonic;
static std::vector<glm::vec3> allPairs(const std::vector<glm::vec3>& points) {
    std::vector<glm::vec3> unique; unique.reserve(points.size());
    for (const auto& p : points) {
        bool seen=false;
        for (const auto& k : unique) if (glm::dot(k-p,k-p) < 1e-12f) { seen=true; break; }
        if(!seen) unique.push_back(p);
    }
    return unique;
}
int main() {
    std::mt19937 rng(1); std::uniform_real_distribution<float> u(-10.f,10.f);
    for (size_t n : {5000u, 20000u, 150000u}) {
        // a mesh: every vertex appears ~3 times (once per face)
        std::vector<glm::vec3> base; for (size_t i=0;i<n/3;++i) base.push_back({u(rng),u(rng),u(rng)});
        std::vector<glm::vec3> pts; for (int r=0;r<3;++r) for (auto& b: base) pts.push_back(b);
        auto t0=std::chrono::steady_clock::now(); auto a=DeduplicatePoints(pts);
        auto t1=std::chrono::steady_clock::now(); 
        double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
        double refMs=-1; size_t rn=0;
        if (n<=20000) { auto r0=std::chrono::steady_clock::now(); auto r=allPairs(pts); auto r1=std::chrono::steady_clock::now(); refMs=std::chrono::duration<double,std::milli>(r1-r0).count(); rn=r.size(); }
        std::printf("%7zu points: grid %8.2f ms (%zu kept)", n, ms, a.size());
        if (refMs>=0) std::printf(", all-pairs %9.2f ms (%zu kept)", refMs, rn);
        std::printf("\n");
    }
}
