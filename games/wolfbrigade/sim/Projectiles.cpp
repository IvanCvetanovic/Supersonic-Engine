#include "sim/Projectiles.hpp"

#include "sim/Damageable.hpp"

namespace WolfBrigade {

namespace {

glm::vec2 moveToward(const glm::vec2& from, const glm::vec2& to, double distance) {
    const glm::vec2 offset = to - from;
    const float length = glm::length(offset);
    const float travel = static_cast<float>(distance);
    if (length <= travel || length < 1e-6f) return to;
    return from + offset * (travel / length);
}

bool targetAlive(const Damageable* target) { return target != nullptr && target->IsAlive(); }

} // namespace

void Projectile::Launch(const glm::vec2& from, Damageable* at, int amount, float projectileSpeed) {
    position = from;
    target = at;
    lastTargetPosition = targetAlive(at) ? at->Position() : from;
    damage = amount;
    speed = projectileSpeed;
    active = true;
}

void Projectile::Step(double delta) {
    if (!active) return;

    // Re-aimed every step while the target lives, then remembered. An arrow
    // that only aimed once would miss anything that moved, and one that kept
    // asking a dead target would have nowhere to go.
    glm::vec2 destination = lastTargetPosition;
    if (targetAlive(target)) {
        destination = target->Position();
        lastTargetPosition = destination;
    }

    position = moveToward(position, destination, static_cast<double>(speed) * delta);

    if (glm::distance(position, destination) <= kHitRadius) {
        // Only if it is still alive. An arrow that arrives at a corpse fizzles,
        // rather than dealing damage to something that has already died - which
        // would make a volley kill a raider twice and the second one count.
        if (targetAlive(target)) target->TakeDamage(damage);
        Deactivate();
    }
}

void Projectile::Deactivate() {
    active = false;
    target = nullptr;
}

void ProjectilePool::Spawn(const glm::vec2& from, Damageable* target, int damage, float speed) {
    for (auto& projectile : m_all) {
        if (projectile->active) continue;
        projectile->Launch(from, target, damage, speed);
        return;
    }

    auto fresh = std::make_unique<Projectile>();
    fresh->Launch(from, target, damage, speed);
    m_all.push_back(std::move(fresh));
}

void ProjectilePool::Step(double delta, bool playing) {
    if (!playing) return;
    for (auto& projectile : m_all) projectile->Step(delta);
}

int ProjectilePool::ActiveCount() const {
    int count = 0;
    for (const auto& projectile : m_all) {
        if (projectile->active) ++count;
    }
    return count;
}

const Projectile* ProjectilePool::At(int index) const {
    if (index < 0 || index >= static_cast<int>(m_all.size())) return nullptr;
    return m_all[static_cast<size_t>(index)].get();
}

} // namespace WolfBrigade
