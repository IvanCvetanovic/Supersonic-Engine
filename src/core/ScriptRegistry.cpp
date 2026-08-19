#include "core/ScriptRegistry.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <iostream>

namespace Supersonic {

ScriptRegistry& ScriptRegistry::Get() {
    static ScriptRegistry instance;
    return instance;
}

void ScriptRegistry::Register(const std::string& name, SupersonicScriptUpdateFn update, Origin origin) {
    if (name.empty() || update == nullptr) {
        SUPERSONIC_LOG_ERROR("ScriptRegistry") << "Refusing to register an unnamed or null script." << std::endl;
        return;
    }

    const bool replacing = m_scripts.find(name) != m_scripts.end();
    m_scripts[name] = Entry{ update, origin };

    SUPERSONIC_LOG_INFO("ScriptRegistry") << (replacing ? "Replaced" : "Registered")
              << (origin == Origin::Plugin ? " plugin" : " built-in")
              << " script '" << name << "'." << std::endl;
}

void ScriptRegistry::UnregisterPluginScripts() {
    size_t removed = 0;
    for (auto it = m_scripts.begin(); it != m_scripts.end();) {
        if (it->second.origin == Origin::Plugin) {
            it = m_scripts.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        SUPERSONIC_LOG_INFO("ScriptRegistry") << "Dropped " << removed << " plugin script(s)." << std::endl;
    }
}

const ScriptRegistry::Entry* ScriptRegistry::Find(const std::string& name) const {
    auto it = m_scripts.find(name);
    return it == m_scripts.end() ? nullptr : &it->second;
}

std::vector<std::string> ScriptRegistry::Names() const {
    std::vector<std::string> names;
    names.reserve(m_scripts.size());
    for (const auto& [name, entry] : m_scripts) {
        (void)entry;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

size_t ScriptRegistry::PluginScriptCount() const {
    return static_cast<size_t>(std::count_if(m_scripts.begin(), m_scripts.end(),
        [](const auto& pair) { return pair.second.origin == Origin::Plugin; }));
}

} // namespace Supersonic
