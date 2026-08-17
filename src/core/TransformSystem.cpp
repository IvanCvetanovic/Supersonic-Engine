#include "core/TransformSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace Engine {

namespace {

// Same Euler convention getModelMatrix composes with (T * Rx * Ry * Rz * S).
// Decomposing with any other order silently reinterprets the rotation.
void decomposeToLocal(const glm::mat4& m, TransformComponent& out) {
    out.position = glm::vec3(m[3]);

    glm::vec3 scale(glm::length(glm::vec3(m[0])),
                    glm::length(glm::vec3(m[1])),
                    glm::length(glm::vec3(m[2])));

    if (glm::determinant(glm::mat3(m)) < 0.0f) {
        scale.x = -scale.x;
    }
    if (scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f) {
        return; // degenerate; leave rotation and scale alone
    }
    out.scale = scale;

    const glm::mat3 rot(glm::vec3(m[0]) / scale.x,
                        glm::vec3(m[1]) / scale.y,
                        glm::vec3(m[2]) / scale.z);

    const float sy = glm::clamp(rot[2][0], -1.0f, 1.0f);
    const float b = std::asin(sy);
    float a = 0.0f;
    float c = 0.0f;
    if (std::fabs(sy) < 0.99999f) {
        a = std::atan2(-rot[2][1], rot[2][2]);
        c = std::atan2(-rot[1][0], rot[0][0]);
    } else {
        a = std::atan2(rot[1][2], rot[1][1]);
    }
    out.rotation = glm::vec3(a, b, c);
}

} // namespace

void TransformSystem::UpdateWorldTransforms(entt::registry& registry) {
    // Every entity with a transform gets a world matrix, so downstream systems
    // never have to ask whether one exists.
    for (auto entity : registry.view<TransformComponent>()) {
        if (!registry.all_of<WorldTransformComponent>(entity)) {
            registry.emplace<WorldTransformComponent>(entity);
        }
    }

    // Iterative resolve with memoisation. A naive recursive walk per entity is
    // O(depth) per node and revisits the same ancestors repeatedly; this touches
    // each chain once and terminates on cycles rather than overflowing the stack.
    auto view = registry.view<TransformComponent, WorldTransformComponent>();

    std::vector<entt::entity> chain;
    for (auto entity : view) {
        chain.clear();

        // Walk up to the first ancestor that is already resolved this frame.
        entt::entity current = entity;
        glm::mat4 base(1.0f);

        while (current != entt::null) {
            chain.push_back(current);

            const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
            const entt::entity parent = hierarchy ? hierarchy->parent : entt::null;

            if (parent == entt::null || !registry.valid(parent) ||
                !registry.all_of<TransformComponent>(parent)) {
                break;
            }

            // Guard against a malformed cycle: if we come back to something
            // already in this chain, stop rather than looping forever.
            bool seen = false;
            for (const auto visited : chain) {
                if (visited == parent) { seen = true; break; }
            }
            if (seen) break;

            current = parent;
        }

        // Compose from the topmost ancestor down.
        glm::mat4 world = base;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            world = world * registry.get<TransformComponent>(*it).getModelMatrix();
        }
        registry.get<WorldTransformComponent>(entity).matrix = world;
    }
}

glm::mat4 TransformSystem::GetWorldMatrix(const entt::registry& registry, entt::entity entity) {
    if (entity == entt::null || !registry.valid(entity)) return glm::mat4(1.0f);

    if (const auto* world = registry.try_get<WorldTransformComponent>(entity)) {
        return world->matrix;
    }
    if (const auto* local = registry.try_get<TransformComponent>(entity)) {
        return local->getModelMatrix();
    }
    return glm::mat4(1.0f);
}

bool TransformSystem::IsDescendantOf(const entt::registry& registry, entt::entity candidate, entt::entity entity) {
    if (candidate == entt::null || entity == entt::null) return false;
    if (candidate == entity) return true;

    entt::entity current = candidate;
    int guard = 0;
    while (current != entt::null && guard++ < 4096) {
        const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
        if (!hierarchy || hierarchy->parent == entt::null) return false;
        if (hierarchy->parent == entity) return true;
        current = hierarchy->parent;
    }
    return false;
}

bool TransformSystem::SetParent(entt::registry& registry, entt::entity child, entt::entity parent) {
    if (child == entt::null || !registry.valid(child)) return false;
    if (child == parent) return false;

    // Parenting to a descendant would create a cycle that the resolve loop
    // would have to break every frame.
    if (parent != entt::null && IsDescendantOf(registry, parent, child)) {
        return false;
    }

    // Preserve the current world placement, so attaching something in the
    // hierarchy does not teleport it.
    const glm::mat4 world = GetWorldMatrix(registry, child);

    if (parent == entt::null) {
        registry.remove<HierarchyComponent>(child);
    } else {
        if (!registry.valid(parent)) return false;
        registry.emplace_or_replace<HierarchyComponent>(child, parent);
    }

    SetWorldMatrix(registry, child, world);
    return true;
}

void TransformSystem::SetWorldMatrix(entt::registry& registry, entt::entity entity, const glm::mat4& world) {
    auto* local = registry.try_get<TransformComponent>(entity);
    if (!local) return;

    glm::mat4 target = world;

    if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        hierarchy && hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
        const glm::mat4 parentWorld = GetWorldMatrix(registry, hierarchy->parent);
        if (std::fabs(glm::determinant(parentWorld)) > 1e-12f) {
            target = glm::inverse(parentWorld) * world;
        }
    }

    decomposeToLocal(target, *local);
}

void TransformSystem::OnParentDestroyed(entt::registry& registry, entt::entity parent) {
    // Promote orphans to the root rather than leaving them pointing at a
    // released handle, which recycling would later turn into a wrong parent.
    for (auto entity : registry.view<HierarchyComponent>()) {
        auto& hierarchy = registry.get<HierarchyComponent>(entity);
        if (hierarchy.parent == parent) {
            const glm::mat4 world = GetWorldMatrix(registry, entity);
            hierarchy.parent = entt::null;
            SetWorldMatrix(registry, entity, world);
        }
    }
}

} // namespace Engine
