#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Supersonic {

// Notices when an asset file on disk changes, so the engine can drop what it
// cached and read it again.
//
// The engine already did exactly this for one file: HotReloadEngine stats the
// script plugin every frame and reloads when the write time moves. That machine
// worked and was pointed at a single path. Textures and meshes, the assets
// someone actually iterates on, had no equivalent - and could not have had one,
// because the registries had no way to un-cache anything until deferred
// destruction landed.
//
// Deliberately a poll rather than a platform watch API. Polling a few dozen
// paths is one stat each and needs no per-platform backend, and this engine
// already pays that cost once per frame without anyone noticing. A real
// FindFirstChangeNotification / inotify layer is worth writing when the count
// reaches the hundreds, and not before.
class AssetWatcher {
public:
    // Called with the path that changed. The callback runs from Poll, on the
    // main thread, so it is safe for it to touch the registries.
    using Callback = std::function<void(const std::string&)>;

    void SetCallback(Callback callback) { m_callback = std::move(callback); }

    // Starts watching a path, recording its current write time so an asset that
    // was already on disk does not fire a spurious reload on the first poll.
    void Watch(const std::string& path);

    void Forget(const std::string& path);
    void Clear();

    // Records a path's CURRENT write time without firing, so a write the ENGINE
    // made itself is not read back as a change.
    //
    // Every reload so far has been someone editing a file in another program,
    // where firing is the whole point. Materials break that: the inspector
    // edits the asset in place and saves it, so the mtime moves and the next
    // poll reloads the file over the values still being dragged. The reload
    // reads back what was just written, so it looks harmless - until the frame
    // between the save and the poll, where it discards that frame's edit and
    // the slider jumps backwards under the cursor.
    //
    // So: every engine-side write calls this immediately afterwards. Watching a
    // path this way is safe even if it was not watched before, which is what
    // makes it usable straight after Create().
    void Acknowledge(const std::string& path);

    // Stats every watched path and fires the callback for each one whose write
    // time moved. Returns how many fired.
    //
    // A file that has vanished is NOT reported as changed: a missing asset is
    // already handled by the registries' fallbacks, and reloading on delete
    // would replace a working texture with a checkerboard the moment someone
    // moved a file in Explorer.
    size_t Poll();

    size_t WatchedCount() const { return m_watched.size(); }

    std::vector<std::string> WatchedPaths() const {
        std::vector<std::string> paths;
        paths.reserve(m_watched.size());
        for (const auto& [path, entry] : m_watched) paths.push_back(path);
        return paths;
    }

private:
    struct Entry {
        std::filesystem::file_time_type writeTime{};
        bool present{false};
    };

    Callback m_callback;
    std::unordered_map<std::string, Entry> m_watched;
};

} // namespace Supersonic
