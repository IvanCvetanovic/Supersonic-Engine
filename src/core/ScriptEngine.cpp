#include "core/ScriptEngine.hpp"
#include "core/Components.hpp"

#include <cmath>

namespace Engine {

void ScriptEngine::Update(entt::registry& registry, float deltaTime) {
    auto view = registry.view<TransformComponent, ScriptComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& script = view.get<ScriptComponent>(entity);

        if (!script.isEnabled) continue;

        // Per-entity clock. A single file-static s_time meant oscillators could
        // never be rewound or phase-offset from one another, and a rewind was
        // immediately overwritten on the next unpaused frame.
        script.elapsed += deltaTime;

        if (script.scriptName == "RotatorScript") {
            transform.rotation.y += 0.8f * deltaTime;
        } else if (script.scriptName == "OscillatorScript") {
            // Offset from the entity's own baseline rather than snapping every
            // oscillator to an absolute world height.
            if (!script.baselineCaptured) {
                script.baseline = transform.position;
                script.baselineCaptured = true;
            }
            transform.position.y = script.baseline.y + std::sin(script.elapsed * 2.0f) * 0.5f;
        } else if (script.scriptName == "LightFlickerScript") {
            // Named in the ScriptComponent comment but never implemented, so
            // selecting it silently did nothing.
            if (auto* light = registry.try_get<LightComponent>(entity)) {
                if (!script.baselineCaptured) {
                    script.baseIntensity = light->intensity;
                    script.baselineCaptured = true;
                }
                const float flicker = 0.85f + 0.15f * std::sin(script.elapsed * 11.0f)
                                                   * std::sin(script.elapsed * 3.7f);
                light->intensity = script.baseIntensity * flicker;
            }
        }
    }
}

} // namespace Engine
