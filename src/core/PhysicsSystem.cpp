#include "core/PhysicsSystem.hpp"
#include "core/Components.hpp"

#include <cmath>

namespace Supersonic {

namespace {
constexpr float kGravity = -9.81f;
constexpr float kGroundPlaneY = 0.0f;
constexpr float kRestitution = 0.3f;
constexpr float kRestVelocity = 0.1f;
} // namespace

void PhysicsSystem::Update(entt::registry& registry, float deltaTime) {
    if (deltaTime <= 0.0f) return;

    auto view = registry.view<TransformComponent, RigidBodyComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);

        if (rigidBody.isKinematic) continue;

        if (rigidBody.useGravity) {
            rigidBody.velocity.y += kGravity * deltaTime;
        }

        transform.position += rigidBody.velocity * deltaTime;

        // Resolve against the bottom of the collider, not the transform origin.
        // Clamping the origin to y = 0 buried every body by half its height and
        // made it impossible to rest anything below the world plane.
        float halfHeight = 0.5f * transform.scale.y;
        if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
            halfHeight = 0.5f * box->size.y * transform.scale.y;
        } else if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
            halfHeight = sphere->radius * transform.scale.y;
        }

        const float bottom = transform.position.y - halfHeight;
        if (bottom < kGroundPlaneY) {
            transform.position.y = kGroundPlaneY + halfHeight;

            // Only reflect when actually moving into the plane. Inverting
            // unconditionally re-launched bodies that were already rising.
            if (rigidBody.velocity.y < 0.0f) {
                rigidBody.velocity.y = -rigidBody.velocity.y * kRestitution;
                if (std::fabs(rigidBody.velocity.y) < kRestVelocity) {
                    rigidBody.velocity.y = 0.0f;
                }
            }
        }
    }
}

} // namespace Supersonic
