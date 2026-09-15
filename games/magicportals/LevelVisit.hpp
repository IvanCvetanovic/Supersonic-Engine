#pragma once

// DEV ONLY: --visit-levels. Not part of the game, and never pushed unless that
// flag is given.
//
// It walks levels in one process and checks that the engine hands material
// descriptor sets back to its pool when a level lets go of its textures. That
// cannot be tested any other way: no suite can construct a TextureRegistry,
// because it needs a device (the remake's design_port.md, step E0).
//
// What it stands in for. The lighting design gives every lightmapped sprite a
// descriptor set of its own - 730 lightmaps across the game, against a pool
// that used to hold 512 sets and never took one back - and has the layer drop a
// level's lightmaps when the level unloads (its G3). The layer does that since
// step 45 (MagicPortalsLayer::HeldLightmaps); the fourth binding does not exist
// yet (E2), so nothing acquires a lightmap for the layer to drop. So for each
// level visited this acquires the level's lightmaps as data textures and one
// material set per lightmap, which is the pool pressure the fourth binding will
// put on it, and invalidates those paths when it moves on, as the layer's unload
// does - before the layer's own, which then finds them gone. The sets are never
// bound: nothing it does is drawn.
//
// The checks:
//  - on release, the level's sets leave the cache at once and stay counted in
//    the pool, because their free is deferred;
//  - six frames later, when the renderer has collected them, the sets in the
//    pool equal the sets in the cache: nothing dropped is still held;
//  - acquiring a level's n lightmaps adds exactly n to both;
//  - no acquisition came back as the exhausted pool's fallback;
//  - after the last release, the pool again equals the cache.
// A failure is recorded, and main turns it into a failing exit.

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/EngineLayer.hpp"
#include "MagicPortalsLayer.hpp"

namespace MagicPortals {

class LevelVisitLayer final : public Supersonic::EngineLayer {
public:
    struct Options {
        // "lightmapped" (every level whose file names a lightmap), "all", or
        // level names separated by commas, as chapters.json names them.
        std::string levels;
        int passes = 1;
    };

    // Owned by main, because the layer stack is torn down before Run returns.
    struct Result {
        bool finished = false;
        std::vector<std::string> failures;
        int visits = 0;
        std::size_t lightmapSets = 0;       // acquired over the whole run
        std::size_t peakInPool = 0;         // the most sets the pool held at once
        std::vector<std::size_t> baselines; // the pool at each visit, before its lightmaps
        std::size_t finalInPool = 0;
    };

    LevelVisitLayer(MagicPortalsLayer& game, MagicPortalsLayer::Paths paths, Options options, Result& result);

    const char* Name() const override { return "Magic Portals level visit (DEV)"; }

    void OnAttach(entt::registry& registry) override;
    void OnUpdate(entt::registry& registry, float deltaTime) override;

private:
    struct Visit {
        int level = -1;                     // in chapters.levels
        std::string name;
        std::string label;                  // W-LL
        std::vector<std::string> lightmaps; // on disk, as Lighting resolves them
    };

    void fail(std::string why);
    void release(entt::registry& registry);
    void measureAndAcquire(entt::registry& registry);
    void finish(entt::registry& registry);

    MagicPortalsLayer& m_game;
    MagicPortalsLayer::Paths m_paths;
    Options m_options;
    Result& m_result;

    std::vector<Visit> m_plan;  // one pass
    int m_visit = -1;           // across all passes
    int m_age = 0;              // frames since the visit began
    bool m_done = false;
    bool m_closing = false;
    std::vector<std::string> m_held; // lightmap paths this layer holds now
    std::size_t m_heldSets = 0;      // and the material sets it acquired for them
};

} // namespace MagicPortals
