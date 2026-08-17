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

} // namespace Supersonic
