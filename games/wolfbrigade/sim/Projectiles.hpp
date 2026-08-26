#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace WolfBrigade {

class Damageable;

// One arrow, from `scripts/entities/projectile.gd`.
//
// Homes on its target and deals damage on arrival. If the target dies in
// flight it COASTS to the last place it saw it and fizzles - no damage, no
// snapping to a corpse, no arrow hanging in the air. That is a deliberate
// choice and it is what makes a volley into a dying raider look right.
struct Projectile {
    bool active{false};

    glm::vec2 position{0.0f};

    // Where the target was last seen. The whole reason the arrow can outlive
    // its target: without it, a dead target is a null destination and the
    // arrow either stops dead or flies to the origin.
    glm::vec2 lastTargetPosition{0.0f};

    Damageable* target{nullptr};
    int damage{0};
    float speed{700.0f};

    // Close enough to count as a hit. An arrow moving 700px a second at 60Hz
    // covers 11.7px a step, so anything smaller than this is an arrow that
    // steps over its target and never arrives.
    static constexpr float kHitRadius = 12.0f;

    void Launch(const glm::vec2& from, Damageable* at, int amount, float projectileSpeed);
    void Step(double delta);
    void Deactivate();
};

// The pool, from `scripts/systems/projectiles.gd`.
//
// Reuses an inactive arrow instead of allocating one per shot - the original's
// seventh rule again, and the reason is a phone: a hundred arrows a second of
// allocate-and-free is a hundred a second of pressure on an allocator that has
// better things to do.
class ProjectilePool {
public:
    // Fires, reusing an inactive projectile if there is one and growing the
    // pool if there is not. The pool never shrinks: its size settles at the
    // most arrows that were ever in the air at once, which is the number it
    // needs.
    void Spawn(const glm::vec2& from, Damageable* target, int damage, float speed);

    // Advances every live arrow. `playing` freezes them on victory or defeat,
    // which is the same board-freeze invariant the units obey - an arrow
    // landing after the player has lost would deal damage to a finished game.
    void Step(double delta, bool playing);

    int ActiveCount() const;
    int PoolSize() const { return static_cast<int>(m_all.size()); }
    void Clear() { m_all.clear(); }

    const Projectile* At(int index) const;

private:
    std::vector<std::unique_ptr<Projectile>> m_all;
};

} // namespace WolfBrigade
