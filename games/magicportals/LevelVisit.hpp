#pragma once

// DEV ONLY: --visit-levels. Not part of the game, and never pushed unless that
// flag is given.
//
// It walks levels in one process and checks that the engine hands material
// descriptor sets back to its pool when a level lets go of its textures. That
// cannot be tested any other way: no suite can construct a TextureRegistry,
// because it needs a device (the remake's design_port.md, step E0).
//
// What it watches. Since step 47 (the lighting design's G4) every lightmapped
// sprite draws its lightmap as the overlay of its own material, so it holds a
// descriptor set of its own - 730 lightmaps across the game - and the layer
// hands a level's lightmaps back when the level unloads (step 45). Steps 42 to
// 46 had nothing drawing a lightmap, so this used to acquire them itself; now it
// acquires nothing of its own and watches the layer's.
//
// One thing it does take. The renderer acquires a set only for a sprite it
// draws, so a lightmapped sprite the camera has not reached has none yet. For
// each one, this asks the registry for the set of that sprite's own four maps -
// the set the renderer takes when the sprite comes into view, not another - so
// every visit puts the level's whole pressure on the pool, as a player who walks
// the level through would.
//
// The checks, per visit:
//  - six frames after the level opens, the sets in the pool equal the sets in
//    the cache: whatever the last level dropped has been freed;
//  - the layer holds exactly the lightmaps the level file names, each is the
//    overlay of a sprite, loaded (not the black a failed file caches) and under
//    the id the registry gives its path;
//  - each such sprite's set is its own, not the exhausted pool's fallback, and
//    taking the ones not yet drawn adds exactly those to both counts;
// and when the layer unloads the level (the next visit opening it):
//  - the level's lightmap sets leave the cache at once, exactly that many, and
//    stay counted in the pool, because their free is deferred.
// At the end it leaves for the level grid and checks the pool equals the cache
// again, then opens the last level once more and quits with its sets held, so
// the layer's detach drops them after the last frame and their frees are still
// queued when the renderer destroys the pool: the shutdown case, which
// validation (failing the run) is what would catch.
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
        std::size_t lightmapSets = 0;       // sets naming a lightmap, over the whole run
        std::size_t drawnFirst = 0;         // of those, already taken by the renderer at the measure
        std::size_t peakInPool = 0;         // the most sets the pool held at once
        std::vector<std::size_t> baselines; // the pool at each visit, without its lightmap sets
        std::size_t finalInPool = 0;        // on the level grid, after the last release
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
        std::vector<std::string> lightmaps; // on disk, as Lighting resolves them, sorted
    };

    enum class Phase { Walking, Leaving, Returning, Done };

    void fail(std::string why);
    // Presses `button` on the game, and checks what its unload did to the pool:
    // `expectDropped` sets out of the cache at once, the pool unchanged.
    void pressAndCheckRelease(entt::registry& registry, MagicPortalsLayer::MenuButton button,
                              const std::string& what, std::size_t expectDropped);
    void open(entt::registry& registry, const Visit& visit);
    // False when there is no registry to measure, which ends the walk.
    bool measure(entt::registry& registry, const Visit& visit, bool counted);

    MagicPortalsLayer& m_game;
    MagicPortalsLayer::Paths m_paths;
    Options m_options;
    Result& m_result;

    std::vector<Visit> m_plan;  // one pass
    int m_visit = -1;           // across all passes
    int m_age = 0;              // frames since the phase's last press
    Phase m_phase = Phase::Walking;
    std::size_t m_heldSets = 0; // sets naming the shown level's lightmaps, as last measured
};

} // namespace MagicPortals
