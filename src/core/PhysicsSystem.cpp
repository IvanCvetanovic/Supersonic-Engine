#include "core/PhysicsSystem.hpp"
#include "core/Components.hpp"

namespace Engine {

void PhysicsSystem::Update(entt::registry& registry, float deltaTime) {
    if (deltaTime <= 0.0f) return;

    constexpr float gravity = -9.81f;

    auto view = registry.view<TransformComponent, RigidBodyComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);

        if (rigidBody.isKinematic) continue;

        // Apply Gravity
        if (rigidBody.useGravity) {
            rigidBody.velocity.y += gravity * deltaTime;
        }

        // Velocity Integration
        transform.position += rigidBody.velocity * deltaTime;

        // Simple Ground Plane Collision Resolution (y = 0.0)
        if (transform.position.y <= 0.0f) {
            transform.position.y = 0.0f;
            rigidBody.velocity.y = -rigidBody.velocity.y * 0.3f; // Damped bounce
            if (std::abs(rigidBody.velocity.y) < 0.1f) {
                rigidBody.velocity.y = 0.0f;
            }
        }
    }
}

} // namespace Engine
