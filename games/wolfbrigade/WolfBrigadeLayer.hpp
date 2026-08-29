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

    // The match this layer is running, or null before OnAttach and after
    // OnDetach.
    //
    // Exposed for the tests, and const so that stays true: the HUD's whole
    // claim is that what is on screen is what the simulation says, and a test
    // can only check that by asking the simulation the same question. Handing
    // out a mutable match would let a test arrange the answer it wanted, which
    // is the one thing that would make these checks worthless.
    const Match* CurrentMatch() const { return m_match.get(); }

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

    // The heads-up display: two resource counters, the wave counter and a
    // Pause button.
    //
    // Built ONCE, in OnAttach, and afterwards only written to. That is not a
    // style preference - it is the one hard constraint the UI puts on a caller.
    // A button's `pressed` flag lives on its component and a click is the
    // transition off it, so a screen rebuilt from the simulation every tick
    // hands the release to a component that was never pressed and no button
    // ever fires. `test_uiinput` pins it. The set of elements here never
    // changes, so building once is also just the simple thing; the bottom bar,
    // whose buttons depend on what is selected, is where this will actually
    // take thought.
    void buildHud(entt::registry& registry);

    // This tick's numbers into the labels built above. Writing text is not
    // rebuilding: the entities, and so the click state, survive.
    void updateHud(entt::registry& registry);

    struct Hud {
        entt::entity wood{entt::null};
        entt::entity food{entt::null};
        entt::entity wave{entt::null};
        entt::entity pause{entt::null};
    };
    Hud m_hud;

    // The game's own pause, which is not the engine's. The tick still runs -
    // the layer simply stops stepping the match - so the UI stays live and the
    // button that got us here can get us back.
    bool m_paused{false};

    std::unique_ptr<GameData> m_data;
    std::unique_ptr<Profile> m_profile;
    std::unique_ptr<Match> m_match;

    std::vector<Quad> m_pool;

    // The camera, kept so the view can follow the lane.
    entt::entity m_camera{entt::null};

    bool m_booted{false};
};

} // namespace WolfBrigade
