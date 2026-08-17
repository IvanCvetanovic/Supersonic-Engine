// Tests for the script registry that replaced ScriptEngine's hardcoded
// if/else chain, with emphasis on the invariant hot-reload depends on:
// unloading a plugin must remove every function pointer that lives inside it,
// because calling into a freed module is the classic hot-reload crash.

#include "TestHarness.hpp"

#include <cstddef>
#include "core/ScriptRegistry.hpp"

#include <algorithm>
#include <string>

using namespace Supersonic;

namespace {

int g_builtinCalls = 0;
int g_pluginCalls = 0;

void builtinA(SupersonicScriptContext* ctx) {
    ++g_builtinCalls;
    ctx->rotation[1] += 1.0f;
}

void pluginA(SupersonicScriptContext* ctx) {
    ++g_pluginCalls;
    ctx->position[1] += 2.0f;
}

void pluginB(SupersonicScriptContext*) { ++g_pluginCalls; }

bool contains(const std::vector<std::string>& names, const std::string& needle) {
    return std::find(names.begin(), names.end(), needle) != names.end();
}

} // namespace

static void testRegisterAndFind() {
    auto& registry = ScriptRegistry::Get();
    registry.Register("BuiltinA", &builtinA, ScriptRegistry::Origin::BuiltIn);

    const auto* entry = registry.Find("BuiltinA");
    CHECK(entry != nullptr);
    if (entry) {
        CHECK(entry->update == &builtinA);
        CHECK(entry->origin == ScriptRegistry::Origin::BuiltIn);
    }

    CHECK_MSG(registry.Find("NotRegistered") == nullptr,
              "an unknown script must resolve to nullptr, not a stale pointer");
}

static void testRejectsInvalidRegistrations() {
    auto& registry = ScriptRegistry::Get();
    const size_t before = registry.Names().size();

    registry.Register("", &builtinA, ScriptRegistry::Origin::BuiltIn);
    registry.Register("NullFn", nullptr, ScriptRegistry::Origin::BuiltIn);

    CHECK_EQ(registry.Names().size(), before);
    CHECK(registry.Find("NullFn") == nullptr);
}

static void testScriptIsInvokable() {
    auto& registry = ScriptRegistry::Get();
    registry.Register("BuiltinA", &builtinA, ScriptRegistry::Origin::BuiltIn);

    g_builtinCalls = 0;
    SupersonicScriptContext ctx{};
    ctx.deltaTime = 1.0f / 60.0f;

    const auto* entry = registry.Find("BuiltinA");
    CHECK(entry != nullptr);
    if (entry && entry->update) entry->update(&ctx);

    CHECK_EQ(g_builtinCalls, 1);
    CHECK_NEAR(ctx.rotation[1], 1.0f);
}

static void testUnloadRemovesOnlyPluginScripts() {
    auto& registry = ScriptRegistry::Get();

    registry.Register("BuiltinA", &builtinA, ScriptRegistry::Origin::BuiltIn);
    registry.Register("PluginA", &pluginA, ScriptRegistry::Origin::Plugin);
    registry.Register("PluginB", &pluginB, ScriptRegistry::Origin::Plugin);

    CHECK_EQ(registry.PluginScriptCount(), size_t{2});
    CHECK(contains(registry.Names(), "PluginA"));

    registry.UnregisterPluginScripts();

    // This is the invariant that keeps a reload from crashing.
    CHECK_EQ(registry.PluginScriptCount(), size_t{0});
    CHECK_MSG(registry.Find("PluginA") == nullptr, "plugin scripts must not survive an unload");
    CHECK_MSG(registry.Find("PluginB") == nullptr, "plugin scripts must not survive an unload");
    CHECK_MSG(registry.Find("BuiltinA") != nullptr, "built-ins must survive a plugin unload");
}

static void testReRegistrationReplacesPointer() {
    // A reload registers the same names again, pointing into the new module.
    auto& registry = ScriptRegistry::Get();

    registry.Register("Reloadable", &pluginA, ScriptRegistry::Origin::Plugin);
    CHECK(registry.Find("Reloadable")->update == &pluginA);

    registry.UnregisterPluginScripts();
    registry.Register("Reloadable", &pluginB, ScriptRegistry::Origin::Plugin);

    const auto* entry = registry.Find("Reloadable");
    CHECK(entry != nullptr);
    if (entry) {
        CHECK_MSG(entry->update == &pluginB, "re-registration must point at the new function");
    }

    registry.UnregisterPluginScripts();
}

static void testNamesAreSorted() {
    auto& registry = ScriptRegistry::Get();
    registry.Register("Zeta", &builtinA, ScriptRegistry::Origin::BuiltIn);
    registry.Register("Alpha", &builtinA, ScriptRegistry::Origin::BuiltIn);

    const auto names = registry.Names();
    CHECK_MSG(std::is_sorted(names.begin(), names.end()),
              "the Inspector dropdown relies on a stable sorted order");
}

static void testApiVersionIsPinned() {
    // The engine refuses to load a plugin whose version differs; if this
    // constant changes, every plugin must be rebuilt. Version 2 added the input
    // accessors, which is exactly the kind of change the check exists to catch.
    CHECK_EQ(SUPERSONIC_SCRIPT_API_VERSION, 2);

    // Context layout is part of the ABI. Offsets rather than a total size: the
    // total moves with padding on a different platform, while an offset that
    // shifts means a plugin built against the old header reads the wrong field.
    CHECK_EQ(offsetof(SupersonicScriptContext, deltaTime), size_t{0});
    CHECK_EQ(offsetof(SupersonicScriptContext, position), sizeof(float) * 2);
    CHECK_EQ(offsetof(SupersonicScriptContext, entityId), sizeof(float) * 11);
    CHECK_MSG(offsetof(SupersonicScriptContext, input) >= sizeof(float) * 11 + sizeof(unsigned int),
              "the input pointer must follow entityId, not displace it");

    // The accessors are function pointers into the ENGINE, so a plugin reload
    // cannot dangle them. Their presence is the ABI contract.
    SupersonicScriptInput api{};
    CHECK_MSG(api.isDown == nullptr && api.axis == nullptr,
              "a zero-initialised input block must be inert, not garbage");
}

static void runTests() {
    testRegisterAndFind();
    testRejectsInvalidRegistrations();
    testScriptIsInvokable();
    testUnloadRemovesOnlyPluginScripts();
    testReRegistrationReplacesPointer();
    testNamesAreSorted();
    testApiVersionIsPinned();
}

TEST_MAIN("test_scripts")
