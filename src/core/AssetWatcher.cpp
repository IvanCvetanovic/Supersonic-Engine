#include "core/AssetWatcher.hpp"

namespace fs = std::filesystem;

namespace Supersonic {

void AssetWatcher::Watch(const std::string& path) {
    if (path.empty()) return;
    if (m_watched.find(path) != m_watched.end()) return;

    Entry entry{};
    std::error_code ec;
    entry.writeTime = fs::last_write_time(path, ec);
    // Recording the CURRENT time is what stops an asset already on disk from
    // firing a reload on the very first poll. A watcher that reloads everything
    // once at startup is indistinguishable from one that is broken.
    entry.present = !ec;
    m_watched.emplace(path, entry);
}

void AssetWatcher::Acknowledge(const std::string& path) {
    if (path.empty()) return;

    auto it = m_watched.find(path);
    if (it == m_watched.end()) {
        // Watch() already records the current time without firing, which is
        // exactly what acknowledging an unwatched path means.
        Watch(path);
        return;
    }

    std::error_code ec;
    const auto writeTime = fs::last_write_time(path, ec);
    if (ec) {
        // The write failed, or something removed the file between writing and
        // acknowledging. Leaving the old time and clearing present means the
        // file fires when it comes back, which is the behaviour a caller who
        // could not write would want anyway.
        it->second.present = false;
        return;
    }

    it->second.writeTime = writeTime;
    it->second.present = true;
}

void AssetWatcher::Forget(const std::string& path) {
    m_watched.erase(path);
}

void AssetWatcher::Clear() {
    m_watched.clear();
}

size_t AssetWatcher::Poll() {
    // Collected during the scan and fired afterwards, NOT from inside the loop.
    //
    // A callback is allowed to watch things - reloading a material reads the
    // textures it names, and acknowledging a path the engine wrote adds it if
    // it was not watched yet. Either inserts into m_watched, which can rehash
    // it, which invalidates the iterator the loop is holding. That is undefined
    // behaviour that costs nothing until the load factor happens to tip, so it
    // would have shipped and then crashed on somebody's machine and not ours.
    std::vector<std::string> changed;

    for (auto& [path, entry] : m_watched) {
        std::error_code ec;
        const auto writeTime = fs::last_write_time(path, ec);

        if (ec) {
            // Gone, or briefly unreadable because something is mid-write. Both
            // are handled by waiting: the registries already fall back visibly
            // for a missing file, and reloading on a partial write would read
            // half a PNG.
            entry.present = false;
            continue;
        }

        // A file that has just reappeared counts as changed, so deleting and
        // restoring one - which is what several art tools do instead of writing
        // in place - still reloads.
        const bool moved = !entry.present || writeTime != entry.writeTime;
        entry.writeTime = writeTime;
        entry.present = true;

        if (moved) changed.push_back(path);
    }

    if (m_callback) {
        for (const auto& path : changed) m_callback(path);
    }

    return changed.size();
}

} // namespace Supersonic
