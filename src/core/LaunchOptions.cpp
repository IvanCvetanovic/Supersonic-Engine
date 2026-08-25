#include "core/LaunchOptions.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace Supersonic {

const char* LaunchOptions::Usage() {
    return "Usage: SupersonicEngine [options]\n"
           "  --frames <n>    render exactly n frames, then exit (0 = until closed)\n"
           "  --scene <path>  load this scene instead of the manifest's startup scene\n"
           "  --screenshot <path>  write a PNG of the last frame and exit\n"
           "  --fixed-step [s]  simulate at a constant delta (default 1/60) so a\n"
           "                    run reproduces exactly; without it the simulation\n"
                    "                    follows the real clock\n"
           "  --import-assets give every asset under assets/ a stable identity,\n"
           "                  writing a .meta beside each, then exit\n"
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
        } else if (arg == "--import-assets") {
            options.importAssets = true;
        } else if (arg == "--screenshot") {
            if (!value(options.screenshotPath)) return fail("--screenshot needs a path");
            if (options.screenshotPath.empty()) return fail("--screenshot needs a path");
        } else if (arg == "--fixed-step") {
            // The seconds are optional, so the common case is just the flag.
            // Peeked rather than consumed: the next argument may be another
            // option, and swallowing it would turn "--fixed-step --frames 60"
            // into a parse error about a step of "--frames".
            options.fixedDelta = 1.0f / 60.0f;
            if (i + 1 < argc) {
                const std::string next = argv[i + 1];
                if (!next.empty() && next[0] != '-') {
                    ++i;
                    char* end = nullptr;
                    const double parsed = std::strtod(next.c_str(), &end);
                    if (end == next.c_str() || (end && *end != '\0')) {
                        return fail("--fixed-step wants seconds, got '" + next + "'");
                    }
                    if (!(parsed > 0.0) || parsed > 1.0) {
                        return fail("--fixed-step wants a step in (0, 1] seconds, got '" +
                                    next + "'");
                    }
                    options.fixedDelta = static_cast<float>(parsed);
                }
            }
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
