#include "core/LaunchOptions.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace Supersonic {

const char* LaunchOptions::Usage() {
    return "Usage: SupersonicEngine [options]\n"
           "  --frames <n>    render exactly n frames, then exit (0 = until closed)\n"
           "  --scene <path>  load this scene instead of the manifest's startup scene\n"
           "  --help          print this message\n";
}

LaunchOptions LaunchOptions::Parse(int argc, const char* const* argv) {
    LaunchOptions options;

    auto fail = [&options](std::string message) {
        options.ok = false;
        options.error = std::move(message);
        return options;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";

        // A flag that takes a value must actually have been given one. Reading
        // argv[i + 1] without this check walks off the end of the array when
        // the flag is last, which is the single most common way a hand-rolled
        // argument parser turns a typo into a crash.
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[i + 1] ? argv[i + 1] : "";
            ++i;
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            options.helpRequested = true;
            return options;
        } else if (arg == "--frames") {
            std::string raw;
            if (!value(raw)) return fail("--frames needs a count");

            // std::stoi throws on garbage and silently accepts a trailing
            // suffix; both are worse than saying what was wrong.
            char* end = nullptr;
            const long parsed = std::strtol(raw.c_str(), &end, 10);
            if (end == raw.c_str() || (end && *end != '\0')) {
                return fail("--frames wants a whole number, got '" + raw + "'");
            }
            if (parsed < 0) return fail("--frames cannot be negative");
            if (parsed > 1000000) return fail("--frames is implausibly large: " + raw);
            options.maxFrames = static_cast<int>(parsed);
        } else if (arg == "--scene") {
            if (!value(options.scenePath)) return fail("--scene needs a path");
            if (options.scenePath.empty()) return fail("--scene needs a path");
        } else {
            return fail("unrecognised option '" + arg + "'");
        }
    }

    return options;
}

} // namespace Supersonic
