#include "core/ScriptEngine.hpp"
#include "core/SimulationClock.hpp"
#include "core/TransformSystem.hpp"
#include "core/PrefabSerializer.hpp"
#include "core/ContactTracker.hpp"
#include "core/SceneManager.hpp"
#include "core/Log.hpp"

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
void scriptMouseDelta(void*, float outDelta[2]) {
    if (!outDelta) return;
    const glm::vec2 delta = Input::MouseDelta();
    outDelta[0] = delta.x;
    outDelta[1] = delta.y;
}
void scriptSetCursorMode(void*, int mode) {
    // An unknown number is ignored rather than clamped. Clamping would turn a
    // plugin built against a newer header into a silently different mode; doing
    // nothing leaves the pointer as it was, which is the safe direction.
    switch (mode) {
        case SUPERSONIC_CURSOR_NORMAL: Input::SetCursorMode(CursorMode::Normal); break;
        case SUPERSONIC_CURSOR_HIDDEN: Input::SetCursorMode(CursorMode::Hidden); break;
        case SUPERSONIC_CURSOR_LOCKED: Input::SetCursorMode(CursorMode::Locked); break;
        default: break;
    }
}
int scriptCursorMode(void*) {
    // The EFFECTIVE mode, not the request: a script asking "am I looking?"
    // wants to know what the pointer is actually doing, and the editor may have
    // taken it back since the request was made.
    switch (Input::EffectiveCursorMode()) {
        case CursorMode::Hidden: return SUPERSONIC_CURSOR_HIDDEN;
        case CursorMode::Locked: return SUPERSONIC_CURSOR_LOCKED;
        case CursorMode::Normal: break;
    }
    return SUPERSONIC_CURSOR_NORMAL;
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

    // Whichever kind of element it is, for the same reason setText takes both:
    // a caller should not have to know what it is holding to ask whether the
    // pointer is over it.
    if (const auto* button = registry->try_get<UIButtonComponent>(entity)) {
        if (button->hovered) return 1;
    }
    if (const auto* field = registry->try_get<UITextFieldComponent>(entity)) {
        if (field->hovered) return 1;
    }
    return 0;
}

void scriptUiSetText(void* opaque, unsigned int entityId, const char* text) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null || !text) return;

    // Whichever of the two carries text. A caller should not have to know
    // which kind of element it is holding to change what it says.
    if (auto* label = registry->try_get<UITextComponent>(entity)) label->text = text;
    if (auto* button = registry->try_get<UIButtonComponent>(entity)) button->label = text;
    if (auto* field = registry->try_get<UITextFieldComponent>(entity)) {
        field->text = text;
        // The caret indexes the string that was just replaced. Left alone it
        // could point into the middle of a character, or past the end.
        field->caret = static_cast<int>(field->text.size());
    }
}

int scriptUiGetText(void* opaque, unsigned int entityId, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    out[0] = '\0';

    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null) return 0;

    const auto* field = registry->try_get<UITextFieldComponent>(entity);
    if (!field) return 0;

    // Bytes here, unlike the field's own maxLength, which counts characters.
    // Two different limits: one is what the author said the name may be, the
    // other is how big a buffer the plugin happened to bring.
    int length = static_cast<int>(field->text.size());
    if (length > capacity - 1) {
        length = capacity - 1;
        // Backed onto a character boundary. Cutting through a multi-byte letter
        // would hand the plugin bytes that are not valid UTF-8, with no way for
        // it to know - so the engine gives back one character less instead.
        while (length > 0 &&
               (static_cast<unsigned char>(field->text[static_cast<size_t>(length)]) & 0xC0u) == 0x80u) {
            --length;
        }
    }

    for (int i = 0; i < length; ++i) out[i] = field->text[static_cast<size_t>(i)];
    out[length] = '\0';
    return length;
}

int scriptUiWasSubmitted(void* opaque, unsigned int entityId) {
    entt::registry* registry = nullptr;
    const auto entity = uiEntity(opaque, entityId, registry);
    if (entity == entt::null) return 0;

    const auto* field = registry->try_get<UITextFieldComponent>(entity);
    return (field && field->submitted) ? 1 : 0;
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

// ---- Authored parameters and per-entity scratch --------------------------
//
// A linear scan over a handful of named pairs, not a map. A script carries a
// few parameters, the vector is contiguous, and the names are short - a hash
// map would allocate per entity to save a comparison that costs nothing at this
// size. If a script ever carries fifty of these, revisit it then and not now.
float* findNamed(std::vector<std::pair<std::string, float>>& values, const char* name) {
    if (!name) return nullptr;
    for (auto& [key, value] : values) {
        if (key == name) return &value;
    }
    return nullptr;
}

float scriptParam(void* opaque, unsigned int entity, const char* name, float fallback) {
    auto* registry = static_cast<entt::registry*>(opaque);
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry || !registry->valid(handle)) return fallback;

    auto* script = registry->try_get<ScriptComponent>(handle);
    if (!script) return fallback;

    const float* found = findNamed(script->parameters, name);
    return found ? *found : fallback;
}

float scriptGetState(void* opaque, unsigned int entity, const char* name, float fallback) {
    auto* registry = static_cast<entt::registry*>(opaque);
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry || !registry->valid(handle)) return fallback;

    auto* script = registry->try_get<ScriptComponent>(handle);
    if (!script) return fallback;

    const float* found = findNamed(script->state, name);
    return found ? *found : fallback;
}

void scriptSetState(void* opaque, unsigned int entity, const char* name, float value) {
    auto* registry = static_cast<entt::registry*>(opaque);
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry || !registry->valid(handle) || !name) return;

    auto* script = registry->try_get<ScriptComponent>(handle);
    if (!script) return;

    if (float* found = findNamed(script->state, name)) {
        *found = value;
        return;
    }
    // Bounded, so a script writing a name built from a counter cannot grow this
    // without limit - which is the shape of the bug that would only show up
    // after an hour of play.
    constexpr size_t kMaxStateEntries = 32;
    if (script->state.size() < kMaxStateEntries) {
        script->state.emplace_back(name, value);
    }
}

// ---- The world block -----------------------------------------------------
//
// Contacts and the command queue live in the registry's context, the same place
// AudioSystem keeps its engine pointer. That keeps ScriptEngine's own signature
// unchanged, and means anything holding the registry can reach them - including
// a game that links the library rather than driving the editor.

ContactTracker* trackerFor(entt::registry& registry) {
    auto* slot = registry.ctx().find<ContactTracker*>();
    return slot ? *slot : nullptr;
}

ScriptEngine::PendingCommands& commandsFor(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<ScriptEngine::PendingCommands>()) {
        return *existing;
    }
    return registry.ctx().emplace<ScriptEngine::PendingCommands>();
}

int scriptContactCount(void* opaque, unsigned int entity) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry) return 0;
    auto* tracker = trackerFor(*registry);
    if (!tracker) return 0;
    return static_cast<int>(tracker->CountFor(static_cast<entt::entity>(entity)));
}

int scriptContactAt(void* opaque, unsigned int entity, int index,
                    unsigned int* outOther, float outNormal[3],
                    int* outPhase, int* outIsTrigger) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || index < 0) return 0;

    auto* tracker = trackerFor(*registry);
    if (!tracker) return 0;

    const auto* events = tracker->For(static_cast<entt::entity>(entity));
    if (!events || static_cast<size_t>(index) >= events->size()) return 0;

    const auto& event = (*events)[static_cast<size_t>(index)];
    // Every out pointer is optional, matching raycast: a script that only wants
    // to know WHO it touched should not have to declare three unused locals.
    if (outOther) *outOther = static_cast<unsigned int>(entt::to_integral(event.other));
    if (outNormal) {
        outNormal[0] = event.normal.x;
        outNormal[1] = event.normal.y;
        outNormal[2] = event.normal.z;
    }
    if (outPhase) *outPhase = static_cast<int>(event.phase);
    if (outIsTrigger) *outIsTrigger = event.isTrigger ? 1 : 0;
    return 1;
}

void scriptSpawnPrefab(void* opaque, const char* prefabPath, const float position[3]) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || !prefabPath) return;

    ScriptEngine::PendingCommands& queue = commandsFor(*registry);

    // Bounded. A script spawning every frame without meaning to would otherwise
    // fill memory before anyone noticed, and the symptom would be an
    // out-of-memory crash rather than a scene full of crates.
    constexpr size_t kMaxSpawnsPerFrame = 256;
    if (queue.spawns.size() >= kMaxSpawnsPerFrame) return;

    ScriptEngine::PendingCommands::Spawn spawn;
    spawn.prefabPath = prefabPath;
    if (position) spawn.position = glm::vec3(position[0], position[1], position[2]);
    queue.spawns.push_back(std::move(spawn));
}

void scriptLoadScene(void* opaque, const char* scenePath) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry || !scenePath || !*scenePath) return;

    // Published by SupersonicApp, the way the contact tracker and the audio
    // engine are. Absent means nobody is driving a frame loop - a test
    // exercising scripts against a bare registry - and asking for a scene there
    // is a no-op rather than a crash.
    auto* slot = registry->ctx().find<SceneManager*>();
    if (!slot || !*slot) return;

    // Already deferred by the manager: RequestLoad records the wish and
    // ApplyPending performs it after the frame's iteration has finished. So
    // this needs no queue of its own, unlike spawn and destroy.
    (*slot)->RequestLoad(scenePath);
}

void scriptDestroyEntity(void* opaque, unsigned int entity) {
    auto* registry = static_cast<entt::registry*>(opaque);
    if (!registry) return;
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry->valid(handle)) return;
    commandsFor(*registry).destroys.push_back(handle);
}

void scriptGetVelocity(void* opaque, unsigned int entity, float outVelocity[3]) {
    if (!outVelocity) return;
    outVelocity[0] = outVelocity[1] = outVelocity[2] = 0.0f;

    auto* registry = static_cast<entt::registry*>(opaque);
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry || !registry->valid(handle)) return;

    if (const auto* body = registry->try_get<RigidBodyComponent>(handle)) {
        outVelocity[0] = body->velocity.x;
        outVelocity[1] = body->velocity.y;
        outVelocity[2] = body->velocity.z;
    }
}

void scriptSetVelocity(void* opaque, unsigned int entity, const float velocity[3]) {
    auto* registry = static_cast<entt::registry*>(opaque);
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry || !registry->valid(handle) || !velocity) return;

    if (auto* body = registry->try_get<RigidBodyComponent>(handle)) {
        body->velocity = glm::vec3(velocity[0], velocity[1], velocity[2]);
    }
}

void scriptAddForce(void* opaque, unsigned int entity, const float force[3]) {
    auto* registry = static_cast<entt::registry*>(opaque);
    const auto handle = static_cast<entt::entity>(entity);
    if (!registry || !registry->valid(handle) || !force) return;

    auto* body = registry->try_get<RigidBodyComponent>(handle);
    if (!body) return;

    // An impulse, not an accumulated force. There is no force accumulator on
    // the body and the solver reads velocity directly, so anything else would
    // misrepresent when it takes effect. Divided by mass, because a heavy thing
    // moving less is the part callers actually expect.
    const float inverseMass = body->mass > 0.0f ? 1.0f / body->mass : 0.0f;
    body->velocity += glm::vec3(force[0], force[1], force[2]) * inverseMass;
}

const SupersonicScriptInput& scriptInput() {
    static const SupersonicScriptInput api{
        nullptr, scriptIsDown, scriptWasPressed, scriptWasReleased, scriptAxis,
        scriptMouseDelta, scriptSetCursorMode, scriptCursorMode
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

    // Every scripted entity, with or without a place in the world. Requiring a
    // transform meant a script on a HUD element never ran - and a HUD element
    // has no transform by design, because it lives in screen space - so
    // "when this button is clicked, do something" could not be written at all.
    auto view = registry.view<ScriptComponent>();
    // Read once for the pass. Absent - a registry nothing has stepped - reads
    // as zero, which is what a simulation that has not started should say.
    float simulatedSeconds = 0.0f;
    if (const auto* clock = registry.ctx().find<SimulationClock>()) {
        simulatedSeconds = clock->SecondsF();
    }

    for (auto entity : view) {
        auto* transform = registry.try_get<TransformComponent>(entity);
        auto& script = view.get<ScriptComponent>(entity);

        if (!script.isEnabled) continue;

        // Per-entity clock, so oscillators can be rewound and phase-offset from
        // one another. A single file-static made that impossible.
        //
        // Advanced by SIMULATED time, not by the frame delta. It used to
        // accumulate the real time the last frame took to draw and then drive
        // std::sin off the result, which made every scripted motion in the
        // engine a function of the display's frame rate - the same scene run
        // twice did not do the same thing, and three runs of one binary over
        // one scene produced three different images.
        //
        // The offset is kept per entity so a script enabled mid-run starts from
        // zero rather than from whatever the world's clock had reached, which
        // is what "rewound and phase-offset" above was always about.
        if (!script.clockStarted) {
            script.clockStarted = true;
            script.clockOrigin = simulatedSeconds;
        }
        script.elapsed = simulatedSeconds - script.clockOrigin;

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
                SUPERSONIC_LOG_WARN("ScriptEngine") << "No script named '" << script.scriptName
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
                                    scriptUiSetText, scriptUiSetVisible, scriptUiSetFill,
                                    scriptUiGetText, scriptUiWasSubmitted};
        ctx.ui = &ui;

        const SupersonicScriptData data{&registry, scriptParam, scriptGetState, scriptSetState};
        ctx.data = &data;

        const SupersonicScriptWorld world{&registry,
                                          scriptContactCount, scriptContactAt,
                                          scriptSpawnPrefab, scriptDestroyEntity,
                                          scriptGetVelocity, scriptSetVelocity,
                                          scriptAddForce, scriptLoadScene};
        ctx.world = &world;
        // A transform-less entity is handed an identity one, so a script that
        // only touches the UI does not have to care.
        if (transform) {
            ctx.position[0] = transform->position.x;
            ctx.position[1] = transform->position.y;
            ctx.position[2] = transform->position.z;
            ctx.rotation[0] = transform->rotation.x;
            ctx.rotation[1] = transform->rotation.y;
            ctx.rotation[2] = transform->rotation.z;
            ctx.scale[0] = transform->scale.x;
            ctx.scale[1] = transform->scale.y;
            ctx.scale[2] = transform->scale.z;
        } else {
            ctx.scale[0] = 1.0f;
            ctx.scale[1] = 1.0f;
            ctx.scale[2] = 1.0f;
        }

        entry->update(&ctx);

        // Nowhere to write the result back to for an entity with no place in
        // the world, which is exactly right: a HUD button that moved itself
        // would be moving something that does not exist.
        if (transform) {
            transform->position = glm::vec3(ctx.position[0], ctx.position[1], ctx.position[2]);
            transform->rotation = glm::vec3(ctx.rotation[0], ctx.rotation[1], ctx.rotation[2]);
            transform->scale = glm::vec3(ctx.scale[0], ctx.scale[1], ctx.scale[2]);
        }
    }
}


void ScriptEngine::ApplyPendingCommands(entt::registry& registry, size_t* outSpawned,
                                        size_t* outDestroyed) {
    if (outSpawned) *outSpawned = 0;
    if (outDestroyed) *outDestroyed = 0;

    auto* queue = registry.ctx().find<PendingCommands>();
    if (!queue || queue->Empty()) return;

    // Moved out before anything is applied. Instantiating a prefab or
    // destroying an entity runs engine code that can queue more commands, and
    // appending to a vector while iterating it is how that becomes a dangling
    // reference rather than an extra crate.
    PendingCommands work = std::move(*queue);
    queue->Clear();

    size_t spawned = 0;
    for (const auto& spawn : work.spawns) {
        SerializationResult result{};
        const entt::entity entity =
            PrefabSerializer::InstantiatePrefab(registry, spawn.prefabPath, &result);
        if (entity == entt::null) {
            SUPERSONIC_LOG_WARN("ScriptEngine")
                << "spawnPrefab failed for " << spawn.prefabPath << ": " << result.message;
            continue;
        }
        if (auto* transform = registry.try_get<TransformComponent>(entity)) {
            transform->position = spawn.position;
        }
        ++spawned;
    }

    size_t destroyed = 0;
    for (const entt::entity entity : work.destroys) {
        // Re-checked, because a script can queue the same entity twice and two
        // scripts can destroy the same entity in one frame.
        if (!registry.valid(entity)) continue;
        // Children first, or they are left pointing at a released handle -
        // and EnTT recycles handles, so a stale parent link does not dangle,
        // it silently resolves to a different entity later.
        TransformSystem::OnParentDestroyed(registry, entity);
        registry.destroy(entity);
        ++destroyed;
    }

    if (outSpawned) *outSpawned = spawned;
    if (outDestroyed) *outDestroyed = destroyed;
}

} // namespace Supersonic
