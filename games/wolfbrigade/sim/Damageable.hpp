#pragma once

#include <glm/glm.hpp>

namespace WolfBrigade {

// Something a unit can hit.
//
// The GDScript says this in a comment - "_attack_target is a Unit or Building
// (both have take_damage/is_alive/hit_half_width)" - and relies on Godot's
// duck typing to make it true. Here it is the interface that comment describes,
// so a target that cannot be hit is a compile error rather than a call into
// nothing.
//
// LIFETIME is the one thing this cannot express and Godot could. The original
// checks is_instance_valid before every use, because a unit is freed 0.4s after
// it dies. A raw pointer here has no such check, so the rule is: whoever owns
// the world keeps its entities alive at least as long as anything can be
// pointing at one, and IsAlive is what says a target has stopped being worth
// hitting. That is also the only case that happens in practice - a dead unit is
// dropped on the next thinking tick, long before anything would free it.
class Damageable {
public:
    virtual ~Damageable() = default;

    virtual void TakeDamage(int amount) = 0;
    virtual bool IsAlive() const = 0;

    // Half the body width, so an attacker stops adjacent to what it is hitting
    // rather than standing inside it. A building is much wider than a unit, and
    // without this a raider walks into the middle of the Town Hall.
    virtual float HitHalfWidth() const = 0;

    virtual glm::vec2 Position() const = 0;
};

} // namespace WolfBrigade
