#pragma once

#include <entt/entt.hpp>

#include "core/Components.hpp"

namespace Supersonic {

// Several systems want "the one active camera" or "the one directional light".
// Writing that as a view loop with a break at the end works, but the loop
// increment is then unreachable, which /W4 correctly flags. This expresses the
// intent directly.
template <typename View>
entt::entity FirstEntityOf(View&& view) {
    auto it = view.begin();
    return it == view.end() ? entt::null : *it;
}

// The scene's active camera: the first one flagged primary, falling back to the
// first camera at all so a scene authored before the flag existed still renders.
inline entt::entity FindPrimaryCamera(entt::registry& registry) {
    entt::entity fallback = entt::null;
    for (auto entity : registry.view<CameraComponent>()) {
        if (registry.get<CameraComponent>(entity).isPrimary) return entity;
        if (fallback == entt::null) fallback = entity;
    }
    return fallback;
}

} // namespace Supersonic
