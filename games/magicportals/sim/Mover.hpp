#pragma once

// What moves a level's movers.
//
// F1 (the engine's 89f48a8) made the contact solve read a kinematic body's
// velocity. A mover that moves its transform and leaves its velocity at zero is
// only a displacement again - it shoves what it meets and carries nothing - and
// it says nothing about it. So nothing in the port moves a kinematic body except
// MoveKinematic, which writes both.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Mover {

// Puts a kinematic body at `target` for the step about to be taken, with the
// velocity that gets it there in `dt`: (target - where it is) / dt.
void MoveKinematic(entt::registry& registry, entt::entity entity, const glm::vec3& target, float dt);

// A switched door, behaviours.gd's Mover.gated (:140-148, :170-176): it opens by
// rising its own height, 126 px, over `stride` milliseconds, and closes the same
// way. Reversed mid-travel it resumes from where it is rather than snapping.
inline constexpr double kDoorRisePx = 126.0;

struct Door {
    glm::vec3 closed{0.0f}; // world metres, where the level places it
    glm::vec3 open{0.0f};
    float durationS = 1.0f;
    float progress = 0.0f; // 0 closed, 1 open
    bool opening = false;

    // One step of travel, and the body moved there (MoveKinematic).
    void Tick(entt::registry& registry, entt::entity entity, float dt);
};

// A door as the level places it: closed where the node stands, opening upward by
// kDoorRisePx over its metadata/stride milliseconds - a bare number with no
// `speed`, so a duration, by behaviours.gd's stride rule. False, with `error`,
// when the node has no stride.
bool DoorFromNode(const Tscn::Node& node, Door& out, std::string& error);

// The port's own mover numbers, games/magicportals/data/movers.json: two of the
// remake's guesses, which it keeps in behaviours.gd rather than in its data.
struct Rules {
    double oscillationRateScale = 0.0; // RATE_SCALE_K, _guess
    double liftSpeedPx = 0.0;          // speed_px_s's default, _guess
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// A swing about where the level places something, behaviours.gd's OSCILLATE
// profile (:158-164), along an axis, by half its stride either way:
//   offset = sin(rateScale * speed * t) * stride / 2.
// `speed` is a frequency, not a velocity, by the remake's reading of the data.
// It starts at the level's place, t = 0. Moving platforms swing vertically, and
// patrolling no-portal zones on the axis their `direction` names.
struct Oscillation {
    glm::dvec2 originPx{0.0};
    glm::dvec2 axis{0.0, 1.0}; // in the remake's pixels, +y down: its Vector2.DOWN
    double stridePx = 0.0;
    double speed = 0.0;
    double rateScale = 0.0;
    double t = 0.0;

    glm::dvec2 AtPx() const;
};

// From a node's position, speed and stride. False, with `error`, when it has no
// speed or no stride: the remake falls back to literals, the port does not
// invent them.
bool OscillationFromNode(const Tscn::Node& node, const glm::dvec2& axis, double rateScale, Oscillation& out,
                         std::string& error);

// A lift, behaviours.gd's SHUTTLE profile (:129-138, :165-169): toward marker `b`
// at its speed, then back to `a`, turning the moment it arrives. It starts where
// the level places it, which in every placement is marker `a`.
struct Shuttle {
    glm::dvec2 aPx{0.0};
    glm::dvec2 bPx{0.0};
    double speedPx = 0.0;
    bool forward = true; // heading for b
    glm::dvec2 atPx{0.0};

    void Advance(double dt);
};

struct Platform {
    std::string name;
    entt::entity entity = entt::null;
    Oscillation motion;
};

struct Lift {
    std::string name;
    entt::entity entity = entt::null;
    Shuttle motion;
};

// A level's moving platforms and lifts. Its doors are its switches' (Puzzle).
struct Movers {
    std::vector<Platform> platforms;
    std::vector<Lift> lifts;

    // One step of each, before the physics step, every body moved with
    // MoveKinematic so that it carries what stands on it.
    void Tick(entt::registry& registry, float dt);

    const Platform* FindPlatform(const std::string& name) const;
    const Lift* FindLift(const std::string& name) const;
};

// A built level's moving platforms and lifts. A lift's ends are the marker
// entities its `a` and `b` name, or its own place where it names none
// (level_runtime.gd:157-163), and its speed is its own or movers.json's. False,
// with `error`, for a platform with no speed or stride, or a mover not built.
bool Wire(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          Movers& out, std::string& error);

} // namespace MagicPortals::Mover
