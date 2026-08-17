#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/ScriptPluginApi.h"

namespace Engine {

// Name -> script lookup, replacing the hardcoded if/else chain in ScriptEngine.
//
// Entries are tagged with their origin so a plugin reload can drop exactly the
// scripts that plugin contributed without disturbing the built-ins.
class ScriptRegistry {
public:
    enum class Origin { BuiltIn, Plugin };

    struct Entry {
        EngineScriptUpdateFn update{nullptr};
        Origin origin{Origin::BuiltIn};
    };

    static ScriptRegistry& Get();

    void Register(const std::string& name, EngineScriptUpdateFn update, Origin origin);

    // Called before unloading a plugin. Leaving a function pointer that lives in
    // a freed module is the classic hot-reload crash.
    void UnregisterPluginScripts();

    const Entry* Find(const std::string& name) const;
    std::vector<std::string> Names() const;

    size_t PluginScriptCount() const;

private:
    ScriptRegistry() = default;

    std::unordered_map<std::string, Entry> m_scripts;
};

} // namespace Engine
