#pragma once

#include <entt/entt.hpp>
#include <string>

namespace Engine {

class HotReloadEngine {
public:
    static bool LoadScriptLibrary(const std::string& libraryPath);
    static bool UnloadScriptLibrary();
    static void CheckForScriptUpdates(entt::registry& registry);

private:
    static void* s_libraryHandle;
    static std::string s_activePath;
};

} // namespace Engine
