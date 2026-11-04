#pragma once

#include <entt/entt.hpp>

#include <vector>
#include <glm/glm.hpp>

#include "core/Components.hpp"

namespace Supersonic {

// Resolves the parent/child graph into world matrices once per frame.
//
// Transforms used to be flat: every TransformComponent was world-space and
// there was no parent field at all, so nothing could be grouped, attached or
// moved together. Everything downstream - rendering, picking, the gizmo,
// serialization - now reads WorldTransformComponent rather than composing the
// local transform itself.
class TransformSystem {
public:
    // Recomputes every entity's world matrix. Safe to call every frame; it is a
    // single pass with memoisation, not a recursive walk per entity.
    static void UpdateWorldTransforms(entt::registry& registry);

    // World matrix for one entity, falling back to its local matrix when it has
    // not been resolved yet.
    static glm::mat4 GetWorldMatrix(const entt::registry& registry, entt::entity entity);

    // Reparents while preserving the entity's current world placement, which is
    // what makes dragging something onto a parent in the hierarchy not teleport
    // it. Rejects cycles.
    static bool SetParent(entt::registry& registry, entt::entity child, entt::entity parent);

    // True if `candidate` is `entity` or any of its descendants. Used to stop a
    // reparent from creating a loop.
    static bool IsDescendantOf(const entt::registry& registry, entt::entity candidate, entt::entity entity);

    // Detaches children before their parent is destroyed so they do not point
    // at a released handle.
    static void OnParentDestroyed(entt::registry& registry, entt::entity parent);

    // The same, for every entity in `parents` at once: one walk of the
    // hierarchy rather than one per parent. A script pass that destroys fifty
    // bullets in a tick walked every parented entity in the scene fifty times.
    // Children are promoted in the hierarchy's own order, exactly as fifty
    // single walks would promote them, and each from its own cached world
    // matrix - which no promotion rewrites - so the locals written are the
    // same bits. Handles that are not valid are ignored.
    static void OnParentsDestroyed(entt::registry& registry, const std::vector<entt::entity>& parents);

    // Decomposes a world matrix into a local transform under the given parent.
    static void SetWorldMatrix(entt::registry& registry, entt::entity entity, const glm::mat4& world);
};

} // namespace Supersonic
