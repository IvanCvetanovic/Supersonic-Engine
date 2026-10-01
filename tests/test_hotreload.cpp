// Hot reload of the script plugin: what the engine does with a plugin that cannot
// work.
//
// HotReloadEngine had no suite. The case that matters is the one a person hits while
// iterating on a plugin: they build something that loads but is wrong - the wrong API
// version, a missing entry point - and the engine has to say so ONCE and then wait to
// be given a different file, not copy and re-open the broken one every other frame
// for as long as the game runs.
//
// The "plugin" here is a real shared library that is not a plugin: the C library, or
// kernel32 on Windows. It loads fine and has none of the entry points, which is
// exactly a plugin that fails for a reason no retry will cure.

#include "TestHarness.hpp"
#include "core/HotReloadEngine.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace Supersonic;
namespace fs = std::filesystem;

namespace {

// The path of some shared library on this machine that is not a script plugin, or
// empty if there is no way to find one.
std::string aSharedLibraryThatIsNotAPlugin() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const HMODULE module = GetModuleHandleA("kernel32.dll");
    if (!module || GetModuleFileNameA(module, buffer, MAX_PATH) == 0) return {};
    return buffer;
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&std::printf), &info) == 0 || !info.dli_fname) return {};
    return info.dli_fname;
#endif
}

fs::path scratch() {
    const fs::path dir = fs::temp_directory_path() / "supersonic-test-hotreload";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// A copy of that library under a name of our own, so the test owns the file it
// touches the timestamp of. Empty if it cannot be made.
std::string aBrokenPlugin(const char* name) {
    const std::string source = aSharedLibraryThatIsNotAPlugin();
    if (source.empty()) return {};
    const fs::path target = scratch() / name;
    std::error_code ec;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    return ec ? std::string{} : target.string();
}

} // namespace

static void testABrokenPluginIsTriedOnceAndNotEveryOtherFrame() {
    const std::string plugin = aBrokenPlugin("broken_plugin.so");
    if (plugin.empty()) {
        CHECK_MSG(true, "no shared library to stand in for a plugin on this machine");
        return;
    }

    HotReloadEngine engine;
    engine.WatchPlugin(plugin);
    CHECK_MSG(!engine.IsLoaded(), "a library with no plugin entry points is not loaded");
    CHECK_MSG(engine.GetLoadAttempts() == 1, "and the first attempt is the watch itself");
    CHECK_MSG(engine.GetStatus().find("missing") != std::string::npos,
              "the status says what is wrong: " + engine.GetStatus());

    // A game runs for thousands of frames after this. The file is not going to
    // change, and neither will the answer.
    for (int frame = 0; frame < 200; ++frame) engine.Poll();

    CHECK_MSG(engine.GetLoadAttempts() <= 2,
              "200 frames with the same broken file is not 100 more copies and loads (attempts: " +
                  std::to_string(engine.GetLoadAttempts()) + ")");
    CHECK(!engine.IsLoaded());
}

static void testABrokenPluginThatChangesIsTriedAgain() {
    const std::string plugin = aBrokenPlugin("changing_plugin.so");
    if (plugin.empty()) {
        CHECK_MSG(true, "no shared library to stand in for a plugin on this machine");
        return;
    }

    HotReloadEngine engine;
    engine.WatchPlugin(plugin);
    for (int frame = 0; frame < 50; ++frame) engine.Poll();
    const uint32_t before = engine.GetLoadAttempts();

    // Rebuilding the plugin is a new write time. That IS a reason to look again.
    std::error_code ec;
    fs::last_write_time(plugin, fs::file_time_type::clock::now() + std::chrono::seconds(30), ec);
    CHECK_MSG(!ec, "the test can move the file's write time");
    for (int frame = 0; frame < 50; ++frame) engine.Poll();

    CHECK_MSG(engine.GetLoadAttempts() > before,
              "a new write time is tried, so fixing the plugin does not need a restart");
    CHECK_MSG(engine.GetLoadAttempts() <= before + 2,
              "and once, not on every frame after it (attempts: " +
                  std::to_string(engine.GetLoadAttempts()) + ")");
}

static void testAMissingPluginIsNotAnError() {
    HotReloadEngine engine;
    engine.WatchPlugin((scratch() / "no_such_plugin.so").string());
    for (int frame = 0; frame < 20; ++frame) engine.Poll();
    CHECK_MSG(engine.GetLoadAttempts() == 0, "a plugin that is not there is never loaded");
    CHECK(!engine.IsLoaded());
}

static void runTests() {
    testABrokenPluginIsTriedOnceAndNotEveryOtherFrame();
    testABrokenPluginThatChangesIsTriedAgain();
    testAMissingPluginIsNotAnError();
}

TEST_MAIN("test_hotreload", 8)
