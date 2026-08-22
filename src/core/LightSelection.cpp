#include "core/LightSelection.hpp"

#include "core/Components.hpp"

#include <algorithm>

namespace Supersonic {

glm::vec3 LightWorldPosition(const entt::registry& registry, entt::entity entity) {
    if (const auto* world = registry.try_get<WorldTransformComponent>(entity)) {
        return glm::vec3(world->matrix[3]);
    }
    if (const auto* local = registry.try_get<TransformComponent>(entity)) {
        return local->position;
    }
    return glm::vec3(0.0f);
}

std::vector<entt::entity> SelectLights(const entt::registry& registry,
                                       const glm::vec3& viewPosition,
                                       std::size_t maxLights) {
    struct Candidate {
        entt::entity entity{entt::null};
        float key{0.0f};
    };

    std::vector<Candidate> candidates;

    for (auto entity : registry.view<const LightComponent>()) {
        const auto& light = registry.get<const LightComponent>(entity);

        // Zero for directional, so they sort ahead of every local light without
        // the comparator needing a special case for them.
        float key = 0.0f;
        if (light.type != static_cast<int>(LightType::Directional)) {
            const float distance = glm::length(LightWorldPosition(registry, entity) - viewPosition);

            // Distance to the edge of its reach. Inside the range this is 1.0
            // for every light, so lights actually reaching the camera are all
            // equally relevant and registry order breaks the tie - which is
            // the honest answer, since none of them can be ranked without
            // knowing what is on screen.
            key = 1.0f + std::max(distance - light.range, 0.0f);
        }

        candidates.push_back(Candidate{entity, key});
    }

    // Stable, deliberately. See the header.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& lhs, const Candidate& rhs) {
                         return lhs.key < rhs.key;
                     });

    if (candidates.size() > maxLights) candidates.resize(maxLights);

    std::vector<entt::entity> chosen;
    chosen.reserve(candidates.size());
    for (const auto& candidate : candidates) chosen.push_back(candidate.entity);
    return chosen;
}

} // namespace Supersonic
