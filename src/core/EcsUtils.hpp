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

// The light whose ambient colours light the scene.
//
// Ambient is a scene-wide term taken from one light, and which one used to be
// decided by EnTT's iteration order - which walks its packed array backwards,
// so it was the light created LAST. A scene's sun could carry a carefully
// authored sky colour and be ignored in favour of a point light's default,
// and adding any light changed the ambient of the whole scene.
//
// Hemispheric ambient stands in for the sky, so the sky light is the one that
// should supply it: the first shadow-casting directional light, then any
// directional light, then whatever exists at all so a scene of nothing but
// point lights is not black. That last case is order-dependent, deliberately:
// with no directional light there is no sky light to prefer and any lamp is as
// good an answer as another - what matters is that there IS one, or the scene
// goes black rather than merely differently lit.
inline entt::entity FindAmbientLight(entt::registry& registry) {
    entt::entity anyDirectional = entt::null;
    entt::entity anyLight = entt::null;

    for (auto entity : registry.view<LightComponent>()) {
        const auto& light = registry.get<LightComponent>(entity);
        const bool directional = light.type == static_cast<int>(LightType::Directional);

        if (directional && light.castsShadow) return entity;
        if (directional && anyDirectional == entt::null) anyDirectional = entity;
        if (anyLight == entt::null) anyLight = entity;
    }

    return anyDirectional != entt::null ? anyDirectional : anyLight;
}

} // namespace Supersonic
