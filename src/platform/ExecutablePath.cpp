#include "platform/ExecutablePath.hpp"

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


bool AnchorAssetRootToExecutable() {
    const std::filesystem::path directory = ExecutableDirectory();
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
    std::error_code exists;
    if (!std::filesystem::is_directory(directory / "assets" / "shaders", exists) || exists) {
        return false;
    }

    std::error_code ec;
    std::filesystem::current_path(directory, ec);
    return !ec;
}

std::filesystem::path AssetRoot() {
    std::error_code ec;
    const std::filesystem::path here = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{} : here;
}

} // namespace Supersonic
