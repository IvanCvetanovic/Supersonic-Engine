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

void AssetWatcher::Forget(const std::string& path) {
    m_watched.erase(path);
}

void AssetWatcher::Clear() {
    m_watched.clear();
}

size_t AssetWatcher::Poll() {
    size_t fired = 0;

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
        const bool changed = !entry.present || writeTime != entry.writeTime;
        entry.writeTime = writeTime;
        entry.present = true;

        if (changed) {
            ++fired;
            if (m_callback) m_callback(path);
        }
    }

    return fired;
}

} // namespace Supersonic
