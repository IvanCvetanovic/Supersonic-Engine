#include "core/ScriptEngine.hpp"
#include "core/Components.hpp"
#include <cmath>

namespace Engine {

static float s_time = 0.0f;

void ScriptEngine::Update(entt::registry& registry, float deltaTime) {
    s_time += deltaTime;

    auto view = registry.view<TransformComponent, ScriptComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        const auto& script = view.get<ScriptComponent>(entity);

        if (!script.isEnabled) continue;

        if (script.scriptName == "RotatorScript") {
            transform.rotation.y += 0.8f * deltaTime;
        } else if (script.scriptName == "OscillatorScript") {
            transform.position.y = 1.0f + std::sin(s_time * 2.0f) * 0.5f;
        }
    }
}

} // namespace Engine
