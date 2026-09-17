#pragma once

// The player: a dynamic capsule, steered the way the remake steers its
// CharacterBody2D (player.gd:89-111), by the numbers in the remake's player.json.
//
// Dynamic rather than kinematic because the spike measured F2: a kinematic body
// trips no trigger. Rotation frozen and locked to the plane (S1). Every number is
// carried as data and never pinned - the remake marks each one _guess.
//
// Two places it cannot be player.gd line for line, both because a dynamic body is
// not a CharacterBody2D:
//   - Gravity is applied every tick, not only off the floor. move_and_slide snaps
//     a character to the floor; a dynamic body has no snap, and without gravity it
//     would hover over a lift going down, since a contact only holds what presses
//     into it. On the floor the contact takes the gravity back out.
//   - It grips nothing: kFriction is 0. A CharacterBody2D feels no friction - its
//     script sets its velocity, and player.json's own friction_px_s2 is what
//     stops it - and measured, a gripping player fights its own steering: at
//     friction 1 the contact takes back most of each tick's acceleration, 1400
//     px/s^2 on flat ground comes out near 200, and the notch where level30's
//     first two platforms meet holds it still for 26 ticks. test_mp_play walks
//     that seam at both and prints them.

#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Player {

inline constexpr float kFriction = 0.0f;

struct Tuning {
    double walkSpeedPx = 0.0;    // movement.walk_speed_px_s
    double accelerationPx = 0.0; // movement.acceleration_px_s2
    double frictionPx = 0.0;     // movement.friction_px_s2: how fast it stops with no input
    double airControl = 0.0;     // movement.air_control: the share of both it keeps in the air
    double gravityPx = 0.0;      // gravity.gravity_px_s2
    double maxFallPx = 0.0;      // gravity.max_fall_speed_px_s
    double widthPx = 0.0;        // body.width_px
    double heightPx = 0.0;       // body.height_px
};

// From player.json. False, with `error`, when a number is missing: player.gd falls
// back to literals, the port does not invent them.
bool LoadTuning(const std::string& path, Tuning& out, std::string& error);

entt::entity Spawn(entt::registry& registry, const glm::dvec2& atPx, const Tuning& tuning);

// Something solid under the feet: a ray from a pixel above the bottom of the
// capsule, reaching three below it - the spike's landing test.
bool Grounded(entt::registry& registry, entt::entity player, const Tuning& tuning);

// SideScrollerCharacter::applyForces' dirVec (art.json character.push): the
// velocity it sets, which detectPushing aims its ray along. On the ground the
// walk, `direction` times the walk speed, across the body's vertical velocity;
// off it the body's own x velocity times `airShare`, across the same. In the
// remake's pixels a second, +y down. Asked BEFORE Steer, of the velocity the
// physics step left, as applyForces reads it before it sets its own.
glm::dvec2 PushAim(entt::registry& registry, entt::entity player, const Tuning& tuning, float direction,
                   double airShare);

// SideScrollerCharacter::detectPushing: whether a ray from the body's centre,
// `reachPx` along `aimPx` with `offsetPx` added to its far end, meets any body
// but the player's own as GetClosestContact would (Shot::ClosestContact). Never
// when the aim has no x. A picture's question: nothing it answers is kept by the
// level.
bool Pushing(entt::registry& registry, entt::entity player, const glm::dvec2& aimPx, double reachPx,
             const glm::dvec2& offsetPx);

// One tick of steering, before the physics step. `direction` is -1, 0 or 1: the
// remake's two-button pad (docs/original-gameplay.md section 1).
void Steer(entt::registry& registry, entt::entity player, const Tuning& tuning, float direction, float dt);

} // namespace MagicPortals::Player
