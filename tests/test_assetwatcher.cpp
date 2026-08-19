// Tests for noticing that an asset changed on disk.
//
// The engine watched exactly one file - the script plugin - and could not have
// watched any others, because the GPU registries had no way to un-cache
// anything. Editing a texture meant restarting the editor.
//
// Two behaviours here are easy to get wrong in opposite directions: firing for
// files that have not changed (a watcher that reloads everything once at
// startup looks identical to one that is broken), and not firing for files that
// have (which is the whole feature).

#include "TestHarness.hpp"
#include "core/AssetWatcher.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Supersonic;

namespace {

void write(const std::string& path, const char* contents) {
    std::ofstream f(path, std::ios::trunc);
    f << contents;
}

// Filesystem timestamps are coarse - on some filesystems 1s, on NTFS ~100ns but
// with a lazily-updated cache. Two writes inside the same tick are
// indistinguishable, so the test that depends on a CHANGE has to outlive one.
void letTheClockMove() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
}

} // namespace

static void testAnUnchangedFileNeverFires() {
    const std::string path = "test_watch_quiet_tmp.txt";
    write(path, "original");

    AssetWatcher watcher;
    int fired = 0;
    watcher.SetCallback([&](const std::string&) { ++fired; });
    watcher.Watch(path);

    CHECK_EQ(watcher.WatchedCount(), size_t{1});

    // The first poll is the one that matters: Watch records the CURRENT write
    // time, so an asset already on disk must not be treated as new.
    CHECK_EQ(watcher.Poll(), size_t{0});
    CHECK_EQ(watcher.Poll(), size_t{0});
    CHECK_MSG(fired == 0, "an untouched file must never fire");

    std::remove(path.c_str());
}

static void testAChangedFileFiresOnceWithItsPath() {
    const std::string path = "test_watch_edit_tmp.txt";
    write(path, "original");

    AssetWatcher watcher;
    std::vector<std::string> seen;
    watcher.SetCallback([&](const std::string& p) { seen.push_back(p); });
    watcher.Watch(path);
    CHECK_EQ(watcher.Poll(), size_t{0});

    letTheClockMove();
    write(path, "edited");

    CHECK_EQ(watcher.Poll(), size_t{1});
    CHECK_EQ(seen.size(), size_t{1});
    CHECK_MSG(!seen.empty() && seen[0] == path, "the callback must name the file that changed");

    // And exactly once: the new write time is adopted, so a second poll with no
    // further edit is silent. Firing every poll would re-read the asset every
    // frame for the rest of the run.
    CHECK_EQ(watcher.Poll(), size_t{0});
    CHECK_EQ(seen.size(), size_t{1});

    std::remove(path.c_str());
}

static void testADeletedFileDoesNotFire() {
    // A missing asset is already handled by the registries' visible fallbacks.
    // Reloading on delete would swap a working texture for a checkerboard the
    // moment someone dragged a file in Explorer.
    const std::string path = "test_watch_delete_tmp.txt";
    write(path, "here");

    AssetWatcher watcher;
    int fired = 0;
    watcher.SetCallback([&](const std::string&) { ++fired; });
    watcher.Watch(path);
    CHECK_EQ(watcher.Poll(), size_t{0});

    std::remove(path.c_str());
    CHECK_MSG(watcher.Poll() == size_t{0}, "a deleted file must not fire a reload");
    CHECK_EQ(fired, 0);
}

static void testARestoredFileFiresAgain() {
    // Several art tools write by deleting and recreating rather than in place,
    // so a file that comes back has to count as changed even if the timestamp
    // were somehow identical.
    const std::string path = "test_watch_restore_tmp.txt";
    write(path, "here");

    AssetWatcher watcher;
    int fired = 0;
    watcher.SetCallback([&](const std::string&) { ++fired; });
    watcher.Watch(path);
    CHECK_EQ(watcher.Poll(), size_t{0});

    std::remove(path.c_str());
    CHECK_EQ(watcher.Poll(), size_t{0});

    write(path, "back");
    CHECK_MSG(watcher.Poll() == size_t{1}, "a file that reappears must fire");
    CHECK_EQ(fired, 1);

    std::remove(path.c_str());
}

static void testWatchingTheSamePathTwiceIsOneEntry() {
    const std::string path = "test_watch_dupe_tmp.txt";
    write(path, "x");

    AssetWatcher watcher;
    watcher.Watch(path);
    watcher.Watch(path);
    watcher.Watch(path);
    CHECK_MSG(watcher.WatchedCount() == size_t{1},
              "re-watching must be a no-op, since the frame loop calls it every frame");

    watcher.Forget(path);
    CHECK_EQ(watcher.WatchedCount(), size_t{0});
    std::remove(path.c_str());
}

static void testAMissingPathIsHarmless() {
    AssetWatcher watcher;
    int fired = 0;
    watcher.SetCallback([&](const std::string&) { ++fired; });

    watcher.Watch("test_watch_never_existed_8812.png");
    CHECK_EQ(watcher.Poll(), size_t{0});
    CHECK_EQ(fired, 0);

    watcher.Watch("");
    CHECK_MSG(watcher.WatchedCount() == size_t{1}, "an empty path is not watched at all");
}

static void runTests() {
    testAnUnchangedFileNeverFires();
    testAChangedFileFiresOnceWithItsPath();
    testADeletedFileDoesNotFire();
    testARestoredFileFiresAgain();
    testWatchingTheSamePathTwiceIsOneEntry();
    testAMissingPathIsHarmless();
}

TEST_MAIN("test_assetwatcher", 20)
