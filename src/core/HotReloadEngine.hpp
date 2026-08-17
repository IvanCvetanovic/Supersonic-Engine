#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Engine {

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

    // Forces a reload attempt regardless of timestamps. Returns false when the
    // attempt failed; a locked file is retryable and Poll will try again.
    bool ReloadNow();

private:
    bool load();
    void unload();

    std::string m_pluginPath;
    std::string m_status{"no plugin configured"};

    // The plugin is copied here before loading so the build system can relink
    // the original while a previous copy is still mapped into this process.
    std::filesystem::path m_shadowPath;

    void* m_library{nullptr};
    std::filesystem::file_time_type m_lastWriteTime{};
    bool m_haveWriteTime{false};
    uint32_t m_reloadCount{0};

    // Debounce: a linker writes its output in passes, so the timestamp and size
    // must hold steady across two polls before the file is considered complete.
    bool m_pendingSeen{false};
    std::filesystem::file_time_type m_pendingWriteTime{};
    uintmax_t m_pendingSize{0};
};

} // namespace Engine
