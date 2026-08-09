#include "core/HotReloadEngine.hpp"
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Engine {

void* HotReloadEngine::s_libraryHandle = nullptr;
std::string HotReloadEngine::s_activePath = "";

bool HotReloadEngine::LoadScriptLibrary(const std::string& libraryPath) {
    UnloadScriptLibrary();

#if defined(_WIN32)
    s_libraryHandle = LoadLibraryA(libraryPath.c_str());
#else
    s_libraryHandle = dlopen(libraryPath.c_str(), RTLD_NOW);
#endif

    if (s_libraryHandle) {
        s_activePath = libraryPath;
        std::cout << "[HotReloadEngine] Successfully loaded live C++ script library: " << libraryPath << std::endl;
        return true;
    }
    return false;
}

bool HotReloadEngine::UnloadScriptLibrary() {
    if (s_libraryHandle) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)s_libraryHandle);
#else
        dlclose(s_libraryHandle);
#endif
        s_libraryHandle = nullptr;
        std::cout << "[HotReloadEngine] Unloaded live C++ script library." << std::endl;
        return true;
    }
    return false;
}

void HotReloadEngine::CheckForScriptUpdates(entt::registry& registry) {
    // Dynamic script hot-reload monitor
    (void)registry;
}

} // namespace Engine
