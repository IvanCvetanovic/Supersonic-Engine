/*
 * Example hot-reloadable script plugin.
 *
 * Build it, run the engine, then edit the numbers below and build the plugin
 * target again while the engine is still running - HotReloadEngine notices the
 * new binary and swaps it in without a restart. Assign a script by name in the
 * Inspector's Script Component section.
 *
 * Only the flat C ABI in core/ScriptPluginApi.h crosses the boundary. Do not
 * hold engine pointers or allocate anything the engine will free: this module
 * gets unloaded underneath its own function pointers on every reload.
 */

#include "core/ScriptPluginApi.h"

#include <cstdio>
#include <unordered_map>

#include <cmath>

namespace {

constexpr float kTau = 6.28318530718f;

// A figure-eight orbit in the XZ plane.
void orbitScript(SupersonicScriptContext* ctx) {
    const float t = ctx->elapsed * 0.9f;
    const float radius = 2.0f;
    ctx->position[0] = std::sin(t) * radius;
    ctx->position[2] = std::sin(t * 2.0f) * radius * 0.5f;
    ctx->rotation[1] = -t;
}

// Bobs up and down with a per-entity phase offset, so several entities using
// this script do not move in lockstep.
void bobScript(SupersonicScriptContext* ctx) {
    const float phase = static_cast<float>(ctx->entityId % 16u) / 16.0f * kTau;
    ctx->position[1] += std::cos(ctx->elapsed * 2.2f + phase) * 1.2f * ctx->deltaTime;
}

// Tumbles on all three axes at mutually irrational-ish rates.
void tumbleScript(SupersonicScriptContext* ctx) {
    ctx->rotation[0] += 1.10f * ctx->deltaTime;
    ctx->rotation[1] += 0.70f * ctx->deltaTime;
    ctx->rotation[2] += 0.31f * ctx->deltaTime;
}

// Breathes between two sizes; edit the constants and rebuild to see the reload.
void breatheScript(SupersonicScriptContext* ctx) {
    const float s = 1.0f + 0.30f * std::sin(ctx->elapsed * 1.7f);
    ctx->scale[0] = s;
    ctx->scale[1] = s;
    ctx->scale[2] = s;
}


// Drives the entity from the player's input: WASD or the left stick to move,
// Space or gamepad A to hop, Shift to sprint.
//
// This is the script that proves gameplay can read input at all - before the
// input layer existed there was no way to write it without reaching past the
// engine into GLFW, and a hot-reloaded plugin certainly could not.
void playerScript(SupersonicScriptContext* ctx) {
    if (!ctx->input) return;

    const float speed = ctx->input->isDown(ctx->input->opaque, "Sprint") ? 9.0f : 3.5f;

    // The axes read the same whether the player is on a keyboard or a pad; the
    // script never finds out which.
    const float moveX = ctx->input->axis(ctx->input->opaque, "MoveX");
    const float moveY = ctx->input->axis(ctx->input->opaque, "MoveY");

    ctx->position[0] += moveX * speed * ctx->deltaTime;
    ctx->position[2] -= moveY * speed * ctx->deltaTime;

    // wasPressed is an edge, so holding the button does not re-trigger.
    if (ctx->input->wasPressed(ctx->input->opaque, "Jump")) {
        ctx->position[1] += 1.2f;
    }

    // Fall until something solid is underfoot, rather than toward a hardcoded
    // height. This is the query that did not exist before: a script had no way
    // to ask the world what was beneath it.
    const bool grounded = ctx->physics &&
        ctx->physics->isGrounded(ctx->physics->opaque, ctx->position, 0.2f, ctx->entityId);
    if (!grounded) {
        ctx->position[1] -= 4.0f * ctx->deltaTime;
    }

    // Face the direction of travel.
    if (moveX * moveX + moveY * moveY > 0.01f) {
        ctx->rotation[1] = std::atan2(moveX, moveY);
    }
}


// Switches clips on the move, which is what animation blending exists for.
//
// The engine cross-fades whenever an animator's clip changes; before the
// animation block on the script context, nothing in a running game could make
// that happen - only the inspector could, which is no use to a character that
// should break into a run when it starts moving.
void animatedPlayerScript(SupersonicScriptContext* ctx) {
    if (!ctx->input) return;

    const float moveX = ctx->input->axis(ctx->input->opaque, "MoveX");
    const float moveY = ctx->input->axis(ctx->input->opaque, "MoveY");
    const bool moving = (moveX * moveX + moveY * moveY) > 0.04f;

    const float speed = ctx->input->isDown(ctx->input->opaque, "Sprint") ? 9.0f : 3.5f;
    ctx->position[0] += moveX * speed * ctx->deltaTime;
    ctx->position[2] -= moveY * speed * ctx->deltaTime;

    if (moving) {
        ctx->rotation[1] = std::atan2(moveX, moveY);
    }

    // Asked every frame on purpose. The engine ignores a request for the clip
    // already playing, so this does not restart the animation or hold it at
    // frame zero - which is what makes "play this while that is true" the
    // natural way to write it.
    if (ctx->animation) {
        ctx->animation->play(ctx->animation->opaque, ctx->entityId,
                             moving ? "Twist" : "Bend");
    }
}

// Counts clicks on its own entity into a label.
//
// The count lives in the plugin, keyed by entity, rather than in
// ScriptComponent::elapsed - which is a CLOCK the engine advances every frame,
// so a counter kept there drifts upward whether anything was clicked or not.
// The first version of this script did exactly that and reported four clicks
// for one, which is a good demonstration of why per-entity script state and
// per-entity script time are not the same thing.
//
// Plugin-local means the count resets when the plugin is hot-reloaded. For a
// sample that is fine, and it shows a plugin may keep state of its own.
std::unordered_map<unsigned int, int> g_clickCounts;

void ClickCounterScript(SupersonicScriptContext* ctx) {
    if (!ctx->ui) return;

    if (ctx->ui->wasClicked(ctx->ui->opaque, ctx->entityId)) {
        const int count = ++g_clickCounts[ctx->entityId];

        char label[64];
        std::snprintf(label, sizeof(label), "CLICKED %d", count);
        ctx->ui->setText(ctx->ui->opaque, ctx->entityId, label);
    }
}

// Demonstrates both halves of the data block.
//
// "speed" and "radius" are AUTHORED: set them per entity in the inspector and
// two crates running this same script patrol differently. Before parameters,
// that needed two scripts.
//
// "phase" is STATE, and the point of it is what happens when you rebuild this
// plugin while the engine is running. It lives on the engine side, so the
// entity keeps the angle it had reached - the crate carries on from where it
// was instead of snapping back to zero. Storing it in a static here would both
// dangle on unload and be shared by every entity using the script.
void patrolScript(SupersonicScriptContext* ctx) {
    if (!ctx || !ctx->data) return;

    const float speed  = ctx->data->param(ctx->data->opaque, ctx->entityId, "speed", 1.0f);
    const float radius = ctx->data->param(ctx->data->opaque, ctx->entityId, "radius", 2.0f);

    float phase = ctx->data->getState(ctx->data->opaque, ctx->entityId, "phase", 0.0f);
    phase += ctx->deltaTime * speed;
    ctx->data->setState(ctx->data->opaque, ctx->entityId, "phase", phase);

    ctx->position[0] += std::cos(phase) * radius * ctx->deltaTime * speed;
    ctx->position[2] += std::sin(phase) * radius * ctx->deltaTime * speed;
}

} // namespace

extern "C" {

SUPERSONIC_SCRIPT_EXPORT int SupersonicScriptPluginVersion(void) {
    return SUPERSONIC_SCRIPT_API_VERSION;
}

SUPERSONIC_SCRIPT_EXPORT void SupersonicScriptPluginRegister(SupersonicScriptHost* host) {
    if (!host || host->apiVersion != SUPERSONIC_SCRIPT_API_VERSION || !host->registerScript) {
        return;
    }

    host->registerScript(host->opaque, "OrbitScript", &orbitScript);
    host->registerScript(host->opaque, "BobScript", &bobScript);
    host->registerScript(host->opaque, "TumbleScript", &tumbleScript);
    host->registerScript(host->opaque, "BreatheScript", &breatheScript);
    host->registerScript(host->opaque, "PlayerScript", &playerScript);
    host->registerScript(host->opaque, "AnimatedPlayerScript", &animatedPlayerScript);
    host->registerScript(host->opaque, "ClickCounterScript", &ClickCounterScript);
    host->registerScript(host->opaque, "PatrolScript", &patrolScript);
}

} // extern "C"
