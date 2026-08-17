#include "editor/GamePackager.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace fs = std::filesystem;

namespace Engine {

namespace {

// Asking the OS where we are removes the guesswork entirely. The old code
// probed a hardcoded "build/GameEngine.exe", which is the Ninja layout; the
// documented Visual Studio build emits build/Debug/GameEngine.exe and POSIX
// builds have no .exe suffix at all, so the copy silently did nothing.
fs::path executablePath() {
    std::error_code ec;

#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size()) return fs::path(std::wstring(buffer.data(), written));
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return fs::weakly_canonical(fs::path(buffer.data()), ec);
#else
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return self;
#endif
}

bool copyTreeIfPresent(const fs::path& from, const fs::path& to, std::string& note) {
    std::error_code ec;
    if (!fs::exists(from, ec)) {
        note += " (skipped missing " + from.string() + ")";
        return true; // not fatal
    }
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        note += " (failed copying " + from.string() + ": " + ec.message() + ")";
        return false;
    }
    return true;
}

} // namespace

SerializationResult GamePackager::PackageStandaloneGame(const std::string& outputFolder) {
    std::error_code ec;

    const fs::path exe = executablePath();
    if (exe.empty() || !fs::exists(exe, ec)) {
        return { false, "Could not locate the running executable; nothing was packaged." };
    }

    const fs::path out(outputFolder);
    fs::create_directories(out, ec);
    if (ec) {
        return { false, "Could not create " + out.string() + ": " + ec.message() };
    }

    // Binary. Without this the output folder is unrunnable, which is exactly
    // what the old unconditional "SUCCESS!" concealed.
    fs::copy_file(exe, out / exe.filename(), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return { false, "Failed to copy " + exe.filename().string() + ": " + ec.message() };
    }

    // Any runtime libraries sitting next to the executable.
    for (const auto& entry : fs::directory_iterator(exe.parent_path(), ec)) {
        if (ec) break;
        const auto& p = entry.path();
        if (!entry.is_regular_file()) continue;
        const std::string ext = p.extension().string();
        if (ext == ".dll" || ext == ".so" || ext == ".dylib") {
            fs::copy_file(p, out / p.filename(), fs::copy_options::overwrite_existing, ec);
            ec.clear();
        }
    }

    std::string note;
    bool ok = true;
    ok &= copyTreeIfPresent("assets/shaders", out / "assets" / "shaders", note);
    ok &= copyTreeIfPresent("assets/scenes", out / "assets" / "scenes", note);

    if (!ok) {
        return { false, "Packaged partially to " + fs::absolute(out, ec).string() + note };
    }

    return { true, "Packaged " + exe.filename().string() + " to " + fs::absolute(out, ec).string() + note };
}

} // namespace Engine
