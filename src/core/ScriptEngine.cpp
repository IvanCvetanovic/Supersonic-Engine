#include "core/ScriptEngine.hpp"
#include "core/Components.hpp"
#include "core/ScriptRegistry.hpp"

#include <cmath>
#include <iostream>

namespace Supersonic {

namespace {

// Built-in transform scripts, written against the same C ABI as plugin scripts
// so both go through one code path. ScriptEngine used to be a two-branch
// if/else on a string with no way to add a third.

extern "C" void builtinRotator(SupersonicScriptContext* ctx) {
    ctx->rotation[1] += 0.8f * ctx->deltaTime;
}

extern "C" void builtinOscillator(SupersonicScriptContext* ctx) {
    // Exact frame-to-frame delta of 0.5*sin(2t), applied relative to wherever
    // the entity already is. The old version snapped every oscillator to an
    // absolute world height; integrating sin() instead would change both the
    // amplitude and the phase the name implies, and accumulate float error.
    const float now = ctx->elapsed;
    const float previous = now - ctx->deltaTime;
    ctx->position[1] += (std::sin(now * 2.0f) - std::sin(previous * 2.0f)) * 0.5f;
}

extern "C" void builtinSpinner(SupersonicScriptContext* ctx) {
    ctx->rotation[0] += 0.4f * ctx->deltaTime;
    ctx->rotation[1] += 0.9f * ctx->deltaTime;
    ctx->rotation[2] += 0.2f * ctx->deltaTime;
}

extern "C" void builtinPulse(SupersonicScriptContext* ctx) {
    const float s = 1.0f + 0.15f * std::sin(ctx->elapsed * 3.0f);
    ctx->scale[0] = s;
    ctx->scale[1] = s;
    ctx->scale[2] = s;
}

} // namespace

void ScriptEngine::RegisterBuiltInScripts() {
    auto& registry = ScriptRegistry::Get();
    registry.Register("RotatorScript", &builtinRotator, ScriptRegistry::Origin::BuiltIn);
    registry.Register("OscillatorScript", &builtinOscillator, ScriptRegistry::Origin::BuiltIn);
    registry.Register("SpinnerScript", &builtinSpinner, ScriptRegistry::Origin::BuiltIn);
    registry.Register("PulseScript", &builtinPulse, ScriptRegistry::Origin::BuiltIn);
}

void ScriptEngine::Update(entt::registry& registry, float deltaTime) {
    auto& scripts = ScriptRegistry::Get();

    auto view = registry.view<TransformComponent, ScriptComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& script = view.get<ScriptComponent>(entity);

        if (!script.isEnabled) continue;

        // Per-entity clock, so oscillators can be rewound and phase-offset from
        // one another. A single file-static made that impossible.
        script.elapsed += deltaTime;

        // LightFlicker needs a component the flat script ABI does not carry, so
        // it stays native. The ABI covers transform scripts by design.
        if (script.scriptName == "LightFlickerScript") {
            if (auto* light = registry.try_get<LightComponent>(entity)) {
                if (!script.baselineCaptured) {
                    script.baseIntensity = light->intensity;
                    script.baselineCaptured = true;
                }
                const float flicker = 0.85f + 0.15f * std::sin(script.elapsed * 11.0f)
                                                   * std::sin(script.elapsed * 3.7f);
                light->intensity = script.baseIntensity * flicker;
            }
            continue;
        }

        const auto* entry = scripts.Find(script.scriptName);
        if (!entry || !entry->update) {
            if (!script.warnedMissing) {
                std::cerr << "[ScriptEngine] No script named '" << script.scriptName
                          << "' is registered; entity will not be driven." << std::endl;
                script.warnedMissing = true;
            }
            continue;
        }
        script.warnedMissing = false;

        // Flat POD in, flat POD out. Nothing with a C++ layout crosses into a
        // module that can be unloaded underneath us.
        SupersonicScriptContext ctx{};
        ctx.deltaTime = deltaTime;
        ctx.elapsed = script.elapsed;
        ctx.entityId = static_cast<unsigned int>(entt::to_integral(entity));
        ctx.position[0] = transform.position.x;
        ctx.position[1] = transform.position.y;
        ctx.position[2] = transform.position.z;
        ctx.rotation[0] = transform.rotation.x;
        ctx.rotation[1] = transform.rotation.y;
        ctx.rotation[2] = transform.rotation.z;
        ctx.scale[0] = transform.scale.x;
        ctx.scale[1] = transform.scale.y;
        ctx.scale[2] = transform.scale.z;

        entry->update(&ctx);

        transform.position = glm::vec3(ctx.position[0], ctx.position[1], ctx.position[2]);
        transform.rotation = glm::vec3(ctx.rotation[0], ctx.rotation[1], ctx.rotation[2]);
        transform.scale = glm::vec3(ctx.scale[0], ctx.scale[1], ctx.scale[2]);
    }
}

} // namespace Supersonic
