#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/EngineLayer.hpp"

#include "sim/GameData.hpp"
#include "sim/Match.hpp"
#include "sim/Progression.hpp"

namespace WolfBrigade {

// The game, drawing the match it actually runs.
//
// This was a spike. It built a hardcoded lane of coloured quads and moved them
// with a sine wave, to answer three questions the port plan's estimates rested
// on: whether a game can live outside the engine at all, what the frame costs at
// Wolf Brigade's real drawable count, and whether a side-on lane reads on
// screen. All three came back yes, and the numbers are in
// docs/planning/2026-08-25-wolf-brigade-port.md.
//
// What it did NOT do was run the game. Four thousand lines of ported
// simulation, verified against twenty-two of the original's harnesses, sat in a
// library that only the test suites linked - so the thing on screen and the
// thing that had been proven correct were two different programs that had never
// met. This is the meeting.
//
// The layer owns the match and nothing else owns any of it: GameData is loaded
// once, a Profile and a Match are constructed over it, and OnFixedUpdate steps
// the match on the engine's tick and then makes the picture agree with it. The
// engine knows none of those types, which is the whole argument EngineLayer.hpp
// makes.
class WolfBrigadeLayer final : public Supersonic::EngineLayer {
public:
    const char* Name() const override { return "WolfBrigade"; }

    void OnAttach(entt::registry& registry) override;
    void OnDetach(entt::registry& registry) override;

    // The match steps on the TICK, not the frame.
    //
    // Match::Step takes a delta and the original ran at a fixed rate, so
    // driving it from the frame delta would make the game's speed a function of
    // the display's - the exact bug the engine's own clock work exists to end.
    // The scene authors 30 Hz, which is what the tick rate is for.
    void OnFixedUpdate(entt::registry& registry, float fixedDelta) override;

private:
    // One reusable drawable.
    //
    // Pooled rather than created and destroyed per tick. A match spawns and
    // kills units constantly, and rebuilding the entities each tick would churn
    // the registry, invalidate every interpolation, and move the state hash for
    // reasons that are about drawing rather than about the game.
    struct Quad {
        entt::entity entity{entt::null};
        bool live{false};
    };

    // Takes the next free quad from the pool, growing it if it has run out, and
    // parks it where the caller says.
    entt::entity claim(entt::registry& registry, std::size_t& cursor,
                       const glm::vec2& simPosition, const glm::vec2& simSize,
                       const glm::vec3& colour, int32_t layer);

    // Everything past `used` is hidden rather than destroyed, for the reason
    // the pool exists.
    void retire(entt::registry& registry, std::size_t used);

    std::unique_ptr<GameData> m_data;
    std::unique_ptr<Profile> m_profile;
    std::unique_ptr<Match> m_match;

    std::vector<Quad> m_pool;

    // The camera, kept so the view can follow the lane.
    entt::entity m_camera{entt::null};

    bool m_booted{false};
};

} // namespace WolfBrigade
