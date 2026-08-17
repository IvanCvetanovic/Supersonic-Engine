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
}

} // extern "C"
