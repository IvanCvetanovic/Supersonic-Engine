#pragma once

// Level 2-32's dragon: the game's third boss, and the only one that cannot be
// killed.
//
// The beholder (Boss.hpp) is a fight, the ghost (Ghost.hpp) is a fight, and this
// is a CHASE. There is no hp, no damage arm and no death arm in
// ETHCallback_dragon, and the node carries no <Body> - so nothing can hurt the
// dragon and the dragon cannot touch the player. Level 2-32 is won by reaching
// its exit while the dragon strafes it and the claw eats the floor behind.
//
// THREE PARTS, and the third is the one that shapes the level:
//   - THE FLIGHT. It enters off the left edge at (-164, -84), advances 45 px a
//     second, sinks toward a cruising height, and once past the `take_off` marker
//     climbs away and stops taking part.
//   - THE FIREBALLS. Only while it has NOT taken off, only every 4 seconds, and
//     only when nothing solid stands between it and the player. Those are
//     Turrets::Fireball - addFireball is the carranca's own function - so Game
//     pushes them into the level's list and Turrets::Tick flies and judges them,
//     as it does a fire diamond's. The original states that gate as "the closest
//     contact is the character"; the port states it as "no body before the
//     endpoint", because the player is a capsule and Shot::FirstBody does not
//     walk capsules. Same gate, and dragon.json says why it is phrased the other
//     way round.
//   - THE CLAW. It destroys the level's platforms at the CAMERA's left edge.
//
// WHY THIS MODULE OWNS A CAMERA. The claw's every number is measured from
// GetCameraPos(), and level31a is the one level in all 128 that sets
// `auto_camera` - to "dragon.ent". AutoCameraController::update is a hard lock to
// the followed entity's x, clamped to the level, with y pinned to 0. So the
// camera IS this boss's own position, and the module derives it rather than
// having one threaded through Game::Tick. dragon.json carries the decode, and the
// evidence that GetCameraPos() is the view's TOP-LEFT corner rather than its
// centre - which is what puts the claw's margins at the left edge of the screen.
//
// The view's WIDTH is the one quantity from outside, and only the clamp uses it:
// it does not bite until x > 2177, past take_off at 2160. It defaults from
// dragon.json so the sim stands up with no window, and the layer overwrites it.
//
// NOT BUILT, and recorded in data/dragon.json: the sounds, the animation frames
// and particles, the claw's easing curves (nothing reads the claw's position),
// and the knight - which is a cutscene that runs only after the level is won.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Dragon {

// The port's dragon.json.
struct Rules {
    // The flight (ETHCallback_dragon).
    std::string entityName;      // dragon.ent
    std::string takeOffName;     // take_off
    std::string knightSpawnName; // dragon_knight_spawn
    double advancePxS = 0.0;     // 45
    double climbPxS = 0.0;       // -60, once past take_off
    double sinkPxS = 0.0;        // 25, while above its cruising height
    double hoverCeilingPx = 0.0; // 48
    double bobAmplitudePx = 0.0; // 13
    double bobFrequency = 0.0;   // 0.1

    // The fireballs.
    double fireIntervalMs = 0.0; // 4000
    double muzzlePx = 0.0;       // 95

    // The claw (ETHCallback_dragon_claw).
    std::string clawName;   // dragon_claw.ent
    double scanMs = 0.0;    // 250: the sweep's own clock
    double armPx = 0.0;     // 94: re-arm the swing and remember the height
    double crushPx = 0.0;   // 64: destroy
    double leadPx = 0.0;    // -100: how far left of the camera it hangs
    double liftPx = 0.0;    // 72
    double clawBobAmplitudePx = 0.0; // 13
    double clawBobFrequency = 0.0;   // 0.06
    std::vector<double> holdMs;      // six, summing to 1500
    std::vector<glm::dvec2> pathPx;  // six, the last two identical
    std::vector<double> anglesDeg;   // six; drawn only
    std::vector<std::string> crushes; // the four names isDestroyableByClaw accepts

    // The camera, and the only number from outside the sim.
    double viewWidthPx = 0.0; // 455 at 16:9, _inferred as view.json is
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// Whether a boss_spawn node is THIS boss. TWO entity names answer true, which is
// the difference from Boss::Plays and Ghost::Plays: level31a places `dragon.ent`
// and `dragon_knight_spawn`, both under boss_spawn, and a port that claimed only
// the first would leave the level reading as not played while playing perfectly.
// dragon.json says on what ground the knight is claimed.
bool Plays(const Rules& rules, const Tscn::Node& node);

// A platform the claw may take: one of the four names, as the level placed it.
// Scenery, every one - none of the four is in entity_roles.json - so nothing but
// LevelBuilder::Built holds it, which is what makes it safe to destroy.
struct Crushable {
    std::string name;
    entt::entity entity = entt::null;
    glm::dvec2 atPx{0.0};
    bool gone = false;
};

// WaypointManager as this one callback uses it: six points, non-repeating,
// parked on the second-to-last so that the idle pose is motionless.
struct Claw {
    int waypoint = 0;
    double inWaypointMs = 0.0;
    double scanMs = 0.0;
    double heightPx = 0.0; // the y of the last platform that armed it

    // Parked at getNumWaypoints() - 2, which is where the callback starts it.
    void Park(const Rules& rules);
    void Reset();
    void Advance(const Rules& rules, double ms);
    bool IsLastFrame(const Rules& rules) const;

    // Where it is drawn, given the camera's top-left. Presentation: no gameplay
    // reads this, which is why the easing curves are recorded and not applied.
    glm::dvec2 AtPx(const Rules& rules, const glm::dvec2& cameraLeftPx) const;
    double AngleDeg(const Rules& rules) const;
};

struct State {
    Rules rules;

    bool present = false; // the level places a dragon.ent
    std::string name;
    glm::dvec2 atPx{0.0};      // where it is DRAWN, which is what the camera follows
    glm::dvec2 virtualPx{0.0}; // and the position the flight integrates
    bool hasTakeOff = false;
    glm::dvec2 takeOffPx{0.0};
    bool tookOff = false;
    double elapsedMs = 0.0;

    bool hasClaw = false;
    Claw claw;
    std::vector<Crushable> crushables;

    double boundsX = 0.0;     // the level's `max`, for the camera's clamp
    double viewWidthPx = 0.0; // seeded from the rules; the layer may overwrite it

    int fired = 0;
    int crushed = 0;

    // The camera's TOP-LEFT corner, in the remake's pixels: a hard lock to the
    // dragon's x, clamped into the level, with y at 0. AutoCameraController.
    glm::dvec2 CameraLeftPx() const;

    // What a tick asks Game to do on its behalf, as Ghost::Turn does: Game owns
    // the registry's bodies and the level's fireball list.
    struct Turn {
        std::vector<entt::entity> crushed;    // platforms the claw took
        std::vector<std::string> crushedNames; // and their node names, for Built
        bool fired = false;
        glm::dvec2 firePx{0.0}; // where the fireball starts
        glm::dvec2 aimPx{0.0};  // a UNIT vector; Game gives it the fireball's speed
    };

    // One tick, before the physics step: it sets positions, it spits as
    // Turrets::Fire does, and it takes geometry away before the solver reads it.
    // `completed` is GameStateController::isFinished, which gates the firing.
    Turn Tick(entt::registry& registry, entt::entity player, bool completed, float dt);
};

// The level's dragon, its claw, the take_off marker, the level's far corner and
// every platform the claw may take. A level with no dragon.ent gets an empty
// State, which is every level but one.
//
// A whitelist node that is not in `built` is skipped rather than refused: the
// landing check builds no statics (Game::Start's withStatics), and there is then
// nothing for the claw to take.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error);

} // namespace MagicPortals::Dragon
