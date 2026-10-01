#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "core/ScriptPluginApi.h"

namespace Supersonic {

// Watches a native script plugin and reloads it when it changes on disk.
//
// The previous implementation had an empty CheckForScriptUpdates body, no file
// watcher, no compiler invocation and no symbol lookup, and nothing anywhere in
// the engine called it - the whole class had zero references.
class HotReloadEngine {
public:
    HotReloadEngine() = default;
    ~HotReloadEngine();

    HotReloadEngine(const HotReloadEngine&) = delete;
    HotReloadEngine& operator=(const HotReloadEngine&) = delete;

    // Sets the plugin to watch. Attempts an immediate load; a missing plugin is
    // not an error, the engine simply runs with built-in scripts only.
    void WatchPlugin(const std::string& pluginPath);

    // Cheap: stats the file and reloads only when the write time moved. Call
    // once per frame.
    void Poll();

    bool IsLoaded() const { return m_library != nullptr; }
    const std::string& GetStatus() const { return m_status; }
    const std::string& GetPluginPath() const { return m_pluginPath; }
    uint32_t GetReloadCount() const { return m_reloadCount; }

    // How many times a load has been ATTEMPTED, successful or not. A failed attempt
    // copies the whole plugin and opens it, so this is what a retry storm is
    // measured in; a reload count only moves when one works.
    uint32_t GetLoadAttempts() const { return m_loadAttempts; }

    // Forces a reload attempt regardless of timestamps. Returns false when the
    // attempt failed; a locked file is retryable and Poll will try again.
    bool ReloadNow();

private:
    // Opens and validates a candidate module WITHOUT disturbing the one already
    // loaded, so a failed attempt leaves the running plugin intact.
    bool openPlugin(int slot, void*& outHandle, SupersonicScriptPluginRegisterFn& outRegister);
    void unload();

    // The plugin is shadow-copied before loading so the build system can relink
    // the original while a copy is still mapped. Two slots are alternated so a
    // new copy never collides with the one currently in use.
    std::filesystem::path shadowPathFor(int slot) const;

    std::string m_pluginPath;
    std::string m_status{"no plugin configured"};

    void* m_library{nullptr};
    int m_shadowSlot{0};
    std::filesystem::file_time_type m_lastWriteTime{};
    bool m_haveWriteTime{false};
    uint32_t m_reloadCount{0};
    uint32_t m_loadAttempts{0};

    // Set by a failed openPlugin when the failure is in the FILE - a missing entry
    // point, a different API version - rather than in getting at it. The same file
    // fails the same way every time, so retrying it is not patience, it is a copy and
    // an open per retry for as long as the game runs.
    bool m_failureIsPermanent{false};

    // A retryable failure (a locked file, a library that will not open) is tried
    // again, but not on every second frame.
    std::chrono::steady_clock::time_point m_nextRetry{};

    // Debounce: a linker writes its output in passes, so the timestamp and size
    // must hold steady across two polls before the file is considered complete.
    bool m_pendingSeen{false};
    std::filesystem::file_time_type m_pendingWriteTime{};
    uintmax_t m_pendingSize{0};

    // A reload stays pending until it succeeds, and its failure is reported
    // once rather than every frame the file remains locked.
    bool m_reloadPending{false};
    bool m_reportedFailure{false};
};

} // namespace Supersonic
