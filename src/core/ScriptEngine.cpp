#include "core/ScriptEngine.hpp"

#include <algorithm>
#include "core/Input.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/Components.hpp"
#include "core/ScriptRegistry.hpp"

#include <cmath>
#include <iostream>

namespace Supersonic {

namespace {

// Built-in transform scripts, written against the same C ABI as plugin scripts
// so both go through one code path. ScriptEngine used to be a two-branch
// if/else on a string with no way to add a third.

namespace {

// The engine side of SupersonicScriptInput. These live in the executable, not
// in the plugin, so a reload cannot leave a script holding a pointer into a
// module that has been freed.
int scriptIsDown(void*, const char* action) {
    return (action && Input::IsDown(action)) ? 1 : 0;
}
int scriptWasPressed(void*, const char* action) {
    return (action && Input::WasPressed(action)) ? 1 : 0;
}
int scriptWasReleased(void*, const char* action) {
    return (action && Input::WasReleased(action)) ? 1 : 0;
}
float scriptAxis(void*, const char* axis) {
    return axis ? Input::GetAxis(axis) : 0.0f;
}


// The engine side of SupersonicScriptPhysics. `opaque` is the registry the
// current update is running over, so a script queries the world it lives in
// rather than a snapshot of it.
int scriptRaycast(void* opaque, const float origin[3], const float direction[3],
                  float maxDistance, unsigned int ignoreEntity,
                  float outPoint[3], float outNormal[3], float* outDistance,
                  unsigned int* outEntity) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || !origin || !direction) return 0;

    const auto ignore = ignoreEntity == 0xFFFFFFFFu
                      ? entt::null
                      : static_cast<entt::entity>(ignoreEntity);

    const PhysicsSystem::RayHit hit = PhysicsSystem::Raycast(
        *registry, glm::vec3(origin[0], origin[1], origin[2]),
        glm::vec3(direction[0], direction[1], direction[2]), maxDistance, ignore);

    if (!hit.hit) return 0;

    // Every out parameter is optional, so a script that only wants "did I hit
    // anything" passes nulls rather than dummy storage.
    if (outPoint) {
        outPoint[0] = hit.point.x; outPoint[1] = hit.point.y; outPoint[2] = hit.point.z;
    }
    if (outNormal) {
        outNormal[0] = hit.normal.x; outNormal[1] = hit.normal.y; outNormal[2] = hit.normal.z;
    }
    if (outDistance) *outDistance = hit.distance;
    if (outEntity) *outEntity = static_cast<unsigned int>(entt::to_integral(hit.entity));
    return 1;
}

int scriptIsGrounded(void* opaque, const float position[3], float distance,
                     unsigned int ignoreEntity) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || !position) return 0;

    const auto ignore = ignoreEntity == 0xFFFFFFFFu
                      ? entt::null
                      : static_cast<entt::entity>(ignoreEntity);

    return PhysicsSystem::IsGrounded(*registry,
                                     glm::vec3(position[0], position[1], position[2]),
                                     distance, ignore) ? 1 : 0;
}


// The engine side of SupersonicScriptAnimation. `opaque` is the registry the
// current update is running over.
void scriptPlayClip(void* opaque, unsigned int entityId, const char* clipName) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || !clipName) return;

    const auto entity = static_cast<entt::entity>(entityId);
    if (!registry->valid(entity)) return;

    auto* animator = registry->try_get<AnimatorComponent>(entity);
    if (!animator) return;

    // Ignored when it is already the requested clip. A script that calls this
    // every frame - which is the natural way to write "play run while moving" -
    // would otherwise restart the transition on every one of them and hold the
    // character at the first frame of the blend forever.
    if (animator->clipName == clipName) return;

    animator->clipName = clipName;
    animator->playing = true;
}

int scriptIsPlayingClip(void* opaque, unsigned int entityId, const char* clipName) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || !clipName) return 0;

    const auto entity = static_cast<entt::entity>(entityId);
    if (!registry->valid(entity)) return 0;

    const auto* animator = registry->try_get<AnimatorComponent>(entity);
    return (animator && animator->clipName == clipName) ? 1 : 0;
}


// The engine side of SupersonicScriptUI. `opaque` is the registry the current
// update is running over.
entt::entity uiEntity(void* opaque, unsigned int entityId, entt::registry*& outRegistry) {
    outRegistry = static_cast<entt::registry*>(opaque);
    if (!outRegistry) return entt::null;

    const auto entity = static_cast<entt::entity>(entityId);
    return outRegistry->valid(entity) ? entity : entt::null;
}

int scriptUiWasClicked(void* opaque, unsigned int entityId) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null) return 0;

    const auto* button = registry->try_get<UIButtonComponent>(entity);
    return (button && button->clicked) ? 1 : 0;
}

int scriptUiIsHovered(void* opaque, unsigned int entityId) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null) return 0;

    const auto* button = registry->try_get<UIButtonComponent>(entity);
    return (button && button->hovered) ? 1 : 0;
}

void scriptUiSetText(void* opaque, unsigned int entityId, const char* text) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null || !text) return;

    // Whichever of the two carries text. A caller should not have to know
    // which kind of element it is holding to change what it says.
    if (auto* label = registry->try_get<UITextComponent>(entity)) label->text = text;
    if (auto* button = registry->try_get<UIButtonComponent>(entity)) button->label = text;
}

void scriptUiSetVisible(void* opaque, unsigned int entityId, int visible) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null) return;

    const bool show = visible != 0;
    if (auto* label = registry->try_get<UITextComponent>(entity)) label->visible = show;
    if (auto* panel = registry->try_get<UIPanelComponent>(entity)) panel->visible = show;
    if (auto* button = registry->try_get<UIButtonComponent>(entity)) button->visible = show;
}

void scriptUiSetFill(void* opaque, unsigned int entityId, float fill) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null) return;

    // Clamped here as well as at draw time: a health bar driven straight from
    // a hit-points variable goes negative on the frame the player dies.
    if (auto* panel = registry->try_get<UIPanelComponent>(entity)) {
        panel->fill = std::clamp(fill, 0.0f, 1.0f);
    }
}

const SupersonicScriptInput& scriptInput() {
    static const SupersonicScriptInput api{
        nullptr, scriptIsDown, scriptWasPressed, scriptWasReleased, scriptAxis
    };
    return api;
}

} // namespace

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
        ctx.input = &scriptInput();

        // Rebuilt per entity because it carries the registry pointer; the
        // function pointers themselves are constant.
        const SupersonicScriptPhysics physics{&registry, scriptRaycast, scriptIsGrounded};
        ctx.physics = &physics;

        const SupersonicScriptAnimation animation{&registry, scriptPlayClip, scriptIsPlayingClip};
        ctx.animation = &animation;

        const SupersonicScriptUI ui{&registry, scriptUiWasClicked, scriptUiIsHovered,
                                    scriptUiSetText, scriptUiSetVisible, scriptUiSetFill};
        ctx.ui = &ui;
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
