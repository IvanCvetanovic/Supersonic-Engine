#include "platform/ExecutablePath.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#else
#include <unistd.h>
#endif

namespace Supersonic {

std::filesystem::path ExecutablePath() {
    std::error_code ec;

#if defined(_WIN32)
    // Grown rather than fixed at MAX_PATH: a path longer than that made the
    // old copy in SupersonicApp return empty, which quietly disabled the
    // beside-the-executable plugin lookup and sent it to the build tree.
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), written));
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), ec);
#else
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : self;
#endif
}

std::filesystem::path ExecutableDirectory() {
    const std::filesystem::path exe = ExecutablePath();
    return exe.empty() ? std::filesystem::path{} : exe.parent_path();
}


bool LooksLikeAPackagedFolder(const std::filesystem::path& directory) {
    if (directory.empty()) return false;

    // ONLY IF THE ASSETS ARE ACTUALLY THERE.
    //
    // A packaged game has its assets copied in beside the executable, and
    // anchoring is what makes it survive being launched from a shortcut whose
    // working directory is somewhere else entirely. The same binary run out of
    // a BUILD TREE does not: the executable sits in build/<config>/ and the
    // assets are at the project root, several levels up.
    //
    // Anchoring unconditionally therefore broke exactly the case a game
    // developer is in all day. The first failure is BloomPass throwing about a
    // missing fullscreen_vert.spv during pipeline creation - a message about a
    // shader, a long way from the working directory that actually caused it,
    // and one that appears the moment a game declares itself a game.
    //
    // Checking is cheap and it is also honest: "my assets are beside me" is a
    // fact about the layout on disk, not something a manifest can assert.
    //
    // `assets/shaders` rather than `assets`, and the difference is not
    // pedantry. An empty `assets` directory turns up beside a build output for
    // all sorts of reasons - a previous run of the game creating one relative
    // to the working directory is enough - and anchoring to a folder that has
    // the right NAME and none of the contents fails in exactly the way this
    // check exists to prevent. Shaders are also the first thing the renderer
    // opens, so if they are not here nothing else being here would save it.
    //
    // This is one of TWO descriptions of the same layout. The other is
    // GamePackager, which chooses the directories it copies beside the
    // executable. If that list ever stops including shaders, a packaged game
    // silently stops anchoring and dies on a missing .spv - so
    // test_executable_path builds a folder the way the packager describes one
    // and asserts this agrees, rather than leaving the two to drift.
    return HoldsEngineAssets(directory);
}

bool HoldsEngineAssets(const std::filesystem::path& directory) {
    if (directory.empty()) return false;
    std::error_code exists;
    return std::filesystem::is_directory(directory / "assets" / "shaders", exists) && !exists;
}

AssetRootChoice ChooseAssetRoot(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredRoot,
    const std::function<bool(const std::filesystem::path&)>& holdsEngineAssets) {
    // An empty path is never asked about. An unknown executable directory or
    // an unset root must not turn into a question about the filesystem root.
    const auto holds = [&](const std::filesystem::path& candidate) {
        return !candidate.empty() && holdsEngineAssets(candidate);
    };

    if (holds(executableDirectory)) {
        return {AssetRootSource::PackagedFolder, executableDirectory};
    }
    if (holds(workingDirectory)) {
        return {AssetRootSource::WorkingDirectory, workingDirectory};
    }
    if (holds(configuredRoot)) {
        return {AssetRootSource::ConfiguredRoot, configuredRoot};
    }
    return {AssetRootSource::Unresolved, workingDirectory};
}

std::filesystem::path ConfiguredAssetRoot() {
#if defined(SUPERSONIC_ASSET_ROOT)
    // Written by CMake as a narrow string literal, like every other path a
    // build bakes in here. A checkout path is plain ASCII in practice.
    return std::filesystem::path(SUPERSONIC_ASSET_ROOT);
#else
    return {};
#endif
}

AssetRootChoice AnchorAssetRoot() {
    std::error_code ec;
    std::filesystem::path working = std::filesystem::current_path(ec);
    if (ec) working.clear();

    AssetRootChoice choice =
        ChooseAssetRoot(ExecutableDirectory(), working, ConfiguredAssetRoot(), HoldsEngineAssets);

    const bool moves = choice.source == AssetRootSource::PackagedFolder ||
                       choice.source == AssetRootSource::ConfiguredRoot;
    if (moves) {
        std::filesystem::current_path(choice.root, ec);
        // A directory that could not be entered is not where anything
        // resolves from, whatever the rule said.
        if (ec) return {AssetRootSource::Unresolved, working};
    }
    return choice;
}

std::filesystem::path AssetRoot() {
    std::error_code ec;
    const std::filesystem::path here = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{} : here;
}

std::string SanitiseForPathComponent(const std::string& name) {
    // TRIMMED FIRST, THEN MAPPED, and the order is the whole of it. Mapping
    // first turns a trailing dot into an underscore, and there is then nothing
    // left for the trim to find - so "Game..." becomes "Game___" rather than
    // "Game", which is a folder name nobody chose.
    //
    // Leading and trailing spaces and dots are stripped rather than replaced
    // because Windows silently drops a trailing dot from a directory name: a
    // folder CREATED as "Game." is later OPENED as "Game", and the two names
    // stop agreeing about where the save is.
    const auto notPadding = [](char c) { return c != ' ' && c != '.'; };
    const auto first = std::find_if(name.begin(), name.end(), notPadding);
    const auto last = std::find_if(name.rbegin(), name.rend(), notPadding).base();
    if (first >= last) return {};

    std::string out;
    out.reserve(static_cast<std::size_t>(last - first));

    for (auto it = first; it != last; ++it) {
        // An allow-list, not a deny-list. A deny-list is a guess about which
        // separators the platform has, and this string is written by whoever
        // named the game rather than by the engine.
        const unsigned char c = static_cast<unsigned char>(*it);
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        out.push_back(keep ? static_cast<char>(c) : '_');
    }

    return out;
}

std::filesystem::path UserDataDirectory(const std::string& application) {
    const std::string folder = SanitiseForPathComponent(application);
    if (folder.empty()) return {};

    std::filesystem::path base;

#if defined(_WIN32)
    // Read wide, not through getenv. The narrow environment is the wide one
    // converted to the ANSI code page, so a profile folder with a character
    // that page lacks - the c-acute of a Serbian surname on a Western European
    // system - comes back best-fitted or as '?', and the save goes to a folder
    // that is not the player's.
    const DWORD needed = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (needed > 1) {
        std::wstring appData(needed, L'\0');
        const DWORD written = GetEnvironmentVariableW(L"APPDATA", appData.data(), needed);
        if (written > 0 && written < needed) {
            appData.resize(written);
            base = std::filesystem::path(appData);
        }
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        base = std::filesystem::path(home) / "Library" / "Application Support";
    }
#else
    // XDG first, and only when it is ABSOLUTE. The specification says a
    // relative value is invalid and must be ignored, and honouring one would
    // put the save wherever the game happened to be started from - which is
    // the failure this whole function exists to avoid.
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        const std::filesystem::path candidate(xdg);
        if (candidate.is_absolute()) base = candidate;
    }
    if (base.empty()) {
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            base = std::filesystem::path(home) / ".local" / "share";
        }
    }
#endif

    if (base.empty()) return {};

    const std::filesystem::path directory = base / folder;

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    // create_directories reports "already there" as a non-error with a false
    // return, so the error code is what to read - but a race with another
    // process creating the same directory sets one too. Ask the filesystem
    // what is actually there rather than trusting either.
    if (!std::filesystem::is_directory(directory, ec)) return {};

    return directory;
}

} // namespace Supersonic
