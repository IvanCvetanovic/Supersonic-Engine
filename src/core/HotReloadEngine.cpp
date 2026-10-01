#include "core/HotReloadEngine.hpp"
#include "core/Log.hpp"
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

namespace Supersonic {

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
void registerScriptThunk(void* /*opaque*/, const char* name, SupersonicScriptUpdateFn update) {
    if (!name || !update) return;
    ScriptRegistry::Get().Register(name, update, ScriptRegistry::Origin::Plugin);
}

} // namespace

namespace {

// How long a retryable failure waits. Frames are ~16 ms and a build tool that still
// has the plugin open lets go in well under a second, so this loses nothing a person
// can see - and a copy and a dlopen every second frame was the alternative.
constexpr std::chrono::milliseconds kRetryInterval{250};

} // namespace

HotReloadEngine::~HotReloadEngine() {
    unload();
}

std::filesystem::path HotReloadEngine::shadowPathFor(int slot) const {
    const fs::path source(m_pluginPath);
    return source.parent_path() /
           (source.stem().string() + ".loaded" + std::to_string(slot) + source.extension().string());
}

void HotReloadEngine::WatchPlugin(const std::string& pluginPath) {
    m_pluginPath = pluginPath;

    std::error_code ec;
    if (!fs::exists(m_pluginPath, ec)) {
        m_status = "plugin not present (" + m_pluginPath + "); built-in scripts only";
        SUPERSONIC_LOG_INFO("HotReload") << m_status << "." << std::endl;
        return;
    }

    if (ReloadNow()) {
        m_lastWriteTime = fs::last_write_time(m_pluginPath, ec);
        m_haveWriteTime = !ec;
    } else if (m_failureIsPermanent) {
        // Seen, and wrong. Poll should wait for the file to change rather than
        // meet this same write time as news and try it a second time.
        m_lastWriteTime = fs::last_write_time(m_pluginPath, ec);
        m_haveWriteTime = !ec;
    }
}

void HotReloadEngine::Poll() {
    if (m_pluginPath.empty()) return;

    std::error_code ec;
    const auto writeTime = fs::last_write_time(m_pluginPath, ec);
    if (ec) return; // mid-build, or the file went away; try again next frame

    const auto size = fs::file_size(m_pluginPath, ec);
    if (ec || size == 0) return;

    if (!m_haveWriteTime || writeTime != m_lastWriteTime) {
        // A linker writes its output in several passes, so a changed timestamp
        // does not mean the file is finished. Wait until timestamp and size hold
        // steady across two polls before touching it.
        if (!m_pendingSeen || writeTime != m_pendingWriteTime || size != m_pendingSize) {
            m_pendingSeen = true;
            m_pendingWriteTime = writeTime;
            m_pendingSize = size;
            return;
        }

        if (!m_reloadPending) {
            SUPERSONIC_LOG_INFO("HotReload") << "Detected change in " << m_pluginPath << "; reloading." << std::endl;
            m_reloadPending = true;
            m_reportedFailure = false;
        }
        m_pendingSeen = false;
    }

    if (!m_reloadPending) return;

    // A failed attempt waits before the next: see kRetryInterval.
    if (std::chrono::steady_clock::now() < m_nextRetry) return;

    // Retry until it succeeds: a build tool can hold the file open for a moment
    // after its final write, and that is not a reason to give up. The previously
    // loaded plugin stays live and working throughout.
    if (ReloadNow()) {
        m_lastWriteTime = writeTime;
        m_haveWriteTime = true;
        m_reloadPending = false;
        return;
    }

    if (!m_reportedFailure) {
        // Once per reload attempt, not once per frame.
        SUPERSONIC_LOG_ERROR("HotReload") << m_status << std::endl;
        m_reportedFailure = true;
    }

    if (m_failureIsPermanent) {
        // The file is the problem, not getting at it, so it will fail the same way
        // until it is replaced. Record that this write time has been seen - exactly
        // as a success does - and nothing more happens until the next one. The
        // running plugin, if there is one, stays live.
        m_lastWriteTime = writeTime;
        m_haveWriteTime = true;
        m_reloadPending = false;
    } else {
        m_nextRetry = std::chrono::steady_clock::now() + kRetryInterval;
    }
}

bool HotReloadEngine::ReloadNow() {
    if (m_pluginPath.empty()) return false;
    ++m_loadAttempts;
    m_failureIsPermanent = false;

    // Open the NEW module before tearing down the old one.
    //
    // Unloading first meant every failed retry left the plugin's scripts
    // unregistered for as long as the file stayed locked, so entities using
    // them silently fell back to nothing in the meantime.
    void* newHandle = nullptr;
    SupersonicScriptPluginRegisterFn registerFn = nullptr;
    const int newSlot = 1 - m_shadowSlot;

    if (!openPlugin(newSlot, newHandle, registerFn)) {
        return false; // m_status explains why; the old plugin is still live
    }

    // Only now is it safe to drop the previous module and its function pointers.
    unload();

    m_library = newHandle;
    m_shadowSlot = newSlot;

    SupersonicScriptHost host{};
    host.apiVersion = SUPERSONIC_SCRIPT_API_VERSION;
    host.opaque = this;
    host.registerScript = &registerScriptThunk;
    registerFn(&host);

    ++m_reloadCount;
    m_status = "loaded " + fs::path(m_pluginPath).filename().string() + " with " +
               std::to_string(ScriptRegistry::Get().PluginScriptCount()) + " script(s)";
    SUPERSONIC_LOG_INFO("HotReload") << m_status << " (reload #" << m_reloadCount << ")." << std::endl;
    return true;
}

bool HotReloadEngine::openPlugin(int slot, void*& outHandle, SupersonicScriptPluginRegisterFn& outRegister) {
    std::error_code ec;

    if (!fs::exists(m_pluginPath, ec)) {
        m_status = "plugin not found: " + m_pluginPath;
        return false;
    }

    // Copy before loading. On Windows a mapped DLL is locked, so rebuilding the
    // plugin would fail while the engine is running; loading a private copy
    // leaves the build output free to be replaced. Two slots are alternated so
    // the new copy never collides with the one still mapped.
    const fs::path shadow = shadowPathFor(slot);
    fs::copy_file(m_pluginPath, shadow, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        m_status = "plugin is still locked by another process; will retry";
        return false;
    }

    void* handle = openLibrary(shadow.string());
    if (!handle) {
        m_status = "failed to load " + shadow.string() + ": " + lastLibraryError();
        return false;
    }

    auto versionFn = reinterpret_cast<SupersonicScriptPluginVersionFn>(
        findSymbol(handle, SUPERSONIC_SCRIPT_PLUGIN_VERSION_SYMBOL));
    auto registerFn = reinterpret_cast<SupersonicScriptPluginRegisterFn>(
        findSymbol(handle, SUPERSONIC_SCRIPT_PLUGIN_REGISTER_SYMBOL));

    if (!versionFn || !registerFn) {
        m_status = std::string("plugin is missing ") + SUPERSONIC_SCRIPT_PLUGIN_VERSION_SYMBOL +
                   " / " + SUPERSONIC_SCRIPT_PLUGIN_REGISTER_SYMBOL;
        closeLibrary(handle);
        m_failureIsPermanent = true;
        return false;
    }

    const int pluginVersion = versionFn();
    if (pluginVersion != SUPERSONIC_SCRIPT_API_VERSION) {
        // A stale plugin built against a different context layout would
        // otherwise scribble over the wrong fields.
        m_status = "plugin API version " + std::to_string(pluginVersion) +
                   " does not match engine version " + std::to_string(SUPERSONIC_SCRIPT_API_VERSION) +
                   "; rebuild the plugin";
        closeLibrary(handle);
        m_failureIsPermanent = true;
        return false;
    }

    outHandle = handle;
    outRegister = registerFn;
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

} // namespace Supersonic
