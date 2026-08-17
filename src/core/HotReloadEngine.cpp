#include "core/HotReloadEngine.hpp"
#include "core/ScriptRegistry.hpp"
#include "core/ScriptPluginApi.h"

#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace Engine {

namespace {

void* openLibrary(const std::string& path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void* handle) {
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* findSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

std::string lastLibraryError() {
#if defined(_WIN32)
    return "OS error " + std::to_string(static_cast<unsigned long>(GetLastError()));
#else
    const char* err = dlerror();
    return err ? std::string(err) : std::string("unknown");
#endif
}

// Trampoline the plugin calls to announce a script.
void registerScriptThunk(void* /*opaque*/, const char* name, EngineScriptUpdateFn update) {
    if (!name || !update) return;
    ScriptRegistry::Get().Register(name, update, ScriptRegistry::Origin::Plugin);
}

} // namespace

HotReloadEngine::~HotReloadEngine() {
    unload();
}

void HotReloadEngine::WatchPlugin(const std::string& pluginPath) {
    m_pluginPath = pluginPath;
    m_shadowPath = fs::path(pluginPath).parent_path() /
                   (fs::path(pluginPath).stem().string() + ".loaded" +
                    fs::path(pluginPath).extension().string());

    std::error_code ec;
    if (!fs::exists(m_pluginPath, ec)) {
        m_status = "plugin not present (" + m_pluginPath + "); built-in scripts only";
        std::cout << "[HotReload] " << m_status << "." << std::endl;
        return;
    }

    ReloadNow();
}

void HotReloadEngine::Poll() {
    if (m_pluginPath.empty()) return;

    std::error_code ec;
    const auto writeTime = fs::last_write_time(m_pluginPath, ec);
    if (ec) return; // mid-build, or the file went away; try again next frame

    const auto size = fs::file_size(m_pluginPath, ec);
    if (ec || size == 0) return;

    if (m_haveWriteTime && writeTime == m_lastWriteTime) {
        m_pendingSeen = false;
        return;
    }

    // A linker writes the output in several passes, so a changed timestamp does
    // not mean the file is finished. Wait until the same timestamp and size have
    // been observed on two consecutive polls before loading, otherwise a single
    // rebuild triggers several reloads and can briefly map a partial DLL.
    if (!m_pendingSeen || writeTime != m_pendingWriteTime || size != m_pendingSize) {
        m_pendingSeen = true;
        m_pendingWriteTime = writeTime;
        m_pendingSize = size;
        return;
    }

    std::cout << "[HotReload] Detected change in " << m_pluginPath << "; reloading." << std::endl;
    m_pendingSeen = false;

    // Only remember this timestamp once the load actually succeeded. A build
    // tool can still hold the file open for a moment after finishing its last
    // write, and treating that transient sharing violation as "done" would
    // leave the plugin unloaded until the file happened to change again.
    if (ReloadNow()) {
        m_lastWriteTime = writeTime;
        m_haveWriteTime = true;
    }
}

bool HotReloadEngine::ReloadNow() {
    unload();
    if (!load()) {
        std::cerr << "[HotReload] " << m_status << std::endl;
        return false;
    }
    ++m_reloadCount;
    std::cout << "[HotReload] " << m_status << " (reload #" << m_reloadCount << ")." << std::endl;
    return true;
}

bool HotReloadEngine::load() {
    std::error_code ec;

    if (!fs::exists(m_pluginPath, ec)) {
        m_status = "plugin not found: " + m_pluginPath;
        return false;
    }

    // Copy before loading. On Windows a mapped DLL is locked, so building the
    // plugin again would fail while the engine is running; loading a private
    // copy leaves the build output free to be replaced.
    //
    // A failure here is almost always the build tool still holding the file, so
    // it is retryable rather than fatal. Loading it in place instead would lock
    // the build output and break the next rebuild.
    fs::copy_file(m_pluginPath, m_shadowPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        m_status = "plugin is still locked by another process; will retry";
        return false;
    }

    m_library = openLibrary(m_shadowPath.string());
    if (!m_library) {
        m_status = "failed to load " + m_shadowPath.string() + ": " + lastLibraryError();
        return false;
    }

    auto versionFn = reinterpret_cast<EngineScriptPluginVersionFn>(
        findSymbol(m_library, ENGINE_SCRIPT_PLUGIN_VERSION_SYMBOL));
    auto registerFn = reinterpret_cast<EngineScriptPluginRegisterFn>(
        findSymbol(m_library, ENGINE_SCRIPT_PLUGIN_REGISTER_SYMBOL));

    if (!versionFn || !registerFn) {
        m_status = std::string("plugin is missing ") + ENGINE_SCRIPT_PLUGIN_VERSION_SYMBOL +
                   " / " + ENGINE_SCRIPT_PLUGIN_REGISTER_SYMBOL;
        closeLibrary(m_library);
        m_library = nullptr;
        return false;
    }

    const int pluginVersion = versionFn();
    if (pluginVersion != ENGINE_SCRIPT_API_VERSION) {
        // A stale plugin built against a different context layout would
        // otherwise scribble over the wrong fields.
        m_status = "plugin API version " + std::to_string(pluginVersion) +
                   " does not match engine version " + std::to_string(ENGINE_SCRIPT_API_VERSION) +
                   "; rebuild the plugin";
        closeLibrary(m_library);
        m_library = nullptr;
        return false;
    }

    EngineScriptHost host{};
    host.apiVersion = ENGINE_SCRIPT_API_VERSION;
    host.opaque = this;
    host.registerScript = &registerScriptThunk;
    registerFn(&host);

    m_status = "loaded " + fs::path(m_pluginPath).filename().string() + " with " +
               std::to_string(ScriptRegistry::Get().PluginScriptCount()) + " script(s)";
    return true;
}

void HotReloadEngine::unload() {
    // Order matters: the registry holds function pointers into this module, so
    // they must go before the module does.
    ScriptRegistry::Get().UnregisterPluginScripts();

    if (m_library) {
        closeLibrary(m_library);
        m_library = nullptr;
    }
}

} // namespace Engine
