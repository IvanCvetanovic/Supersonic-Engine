#include "core/TransformSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace Supersonic {

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
    // One stamp per call, so "already composed" means "composed during THIS
    // call" and nothing else. Never zero, because that is what a component
    // freshly created below holds.
    static uint32_t s_call = 0;
    if (++s_call == 0) ++s_call;
    const uint32_t stamp = s_call;

    // Every entity with a transform gets a world matrix, so downstream systems
    // never have to ask whether one exists. Done first and alone, because
    // nothing after it may emplace: adding to a pool can move it, and the loops
    // below hold references into one.
    for (auto entity : registry.view<TransformComponent>()) {
        (void)registry.get_or_emplace<WorldTransformComponent>(entity);
    }

    // Entities with no parent at all, which in nearly every scene is nearly
    // every entity. Their world matrix IS their local one - no chain to walk,
    // no ancestors to look up, one matrix each.
    //
    // This used to go through the general path below: a vector cleared and
    // pushed to, a hierarchy lookup, and a reverse iteration, all to compose a
    // chain of length one.
    for (auto entity : registry.view<TransformComponent>(entt::exclude<HierarchyComponent>)) {
        auto& world = registry.get<WorldTransformComponent>(entity);
        world.matrix = registry.get<TransformComponent>(entity).getModelMatrix();
        world.resolvedStamp = stamp;
    }

    // Everything that is parented to something. The walk climbs until it finds
    // an ancestor already composed this call and then composes back down,
    // which is what the comment here used to claim and did not do: nothing was
    // ever marked, so a chain of depth d re-derived every ancestor for each of
    // its d nodes, and ten siblings under one parent built that parent's matrix
    // ten times.
    std::vector<entt::entity> chain;
    for (auto entity : registry.view<TransformComponent, HierarchyComponent>()) {
        if (registry.get<WorldTransformComponent>(entity).resolvedStamp == stamp) continue;

        chain.clear();
        entt::entity current = entity;
        glm::mat4 base(1.0f);

        while (current != entt::null) {
            // The memoisation. An ancestor carrying this call's stamp is
            // finished; its matrix is the base to compose down from.
            const auto& cached = registry.get<WorldTransformComponent>(current);
            if (cached.resolvedStamp == stamp) {
                base = cached.matrix;
                break;
            }

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

        // Compose from the topmost ancestor down, marking EVERY node on the way
        // rather than only the one that was asked for. The partial composition
        // at each step is that node's world matrix, so storing it is free - and
        // it is what stops the next sibling climbing the same chain again.
        glm::mat4 world = base;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            world = world * registry.get<TransformComponent>(*it).getModelMatrix();
            auto& cache = registry.get<WorldTransformComponent>(*it);
            cache.matrix = world;
            cache.resolvedStamp = stamp;
        }
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

} // namespace Supersonic
