#include <cerrno>
#include <cstdlib>
#include "core/GameRuntime.hpp"
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
           "  --record <path>   write every tick's input, and a state hash every\n"
           "                    second, so the run can be replayed elsewhere\n"
           "  --replay <path>   run a recorded session's input back and check it\n"
           "                    against the hashes it stored; exits non-zero on\n"
           "                    the first tick that disagrees\n"
           "  --window <WxH>  open at this size instead of the manifest's, e.g.\n"
           "                  --window 1920x1080. This is the render resolution\n"
           "                  for a game: the offscreen target follows the window\n"
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
        } else if (arg == "--window") {
            std::string raw;
            if (!value(raw)) return fail("--window needs a size, e.g. 1920x1080");

            // Split on the FIRST separator and require the rest to parse whole,
            // so "1920x1080x" and "1920 x 1080" are refused rather than
            // silently read as 1920x1080. A size that was almost right is the
            // one worth complaining about.
            const std::size_t split = raw.find_first_of("xX");
            if (split == std::string::npos || split == 0 || split + 1 >= raw.size()) {
                return fail("--window wants <width>x<height>, got '" + raw + "'");
            }

            const auto extent = [](const std::string& text, uint32_t& out) {
                if (text.empty()) return false;
                for (const char c : text) {
                    if (c < '0' || c > '9') return false;
                }
                errno = 0;
                const unsigned long parsed = std::strtoul(text.c_str(), nullptr, 10);
                if (errno != 0 || parsed > GameManifest::kMaximumExtent) return false;
                out = static_cast<uint32_t>(parsed);
                return true;
            };

            uint32_t width = 0;
            uint32_t height = 0;
            if (!extent(raw.substr(0, split), width) ||
                !extent(raw.substr(split + 1), height)) {
                return fail("--window wants <width>x<height>, got '" + raw + "'");
            }
            if (width < GameManifest::kMinimumExtent ||
                height < GameManifest::kMinimumExtent) {
                return fail("--window is too small to be usable: '" + raw + "'");
            }

            options.windowWidth = width;
            options.windowHeight = height;
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
        } else if (arg == "--record") {
            if (!value(options.recordPath)) return fail("--record needs a path");
            if (options.recordPath.empty()) return fail("--record needs a path");
        } else if (arg == "--replay") {
            if (!value(options.replayPath)) return fail("--replay needs a path");
            if (options.replayPath.empty()) return fail("--replay needs a path");
        } else {
            return fail("unrecognised option '" + arg + "'");
        }
    }

    // Checked after the loop rather than when the second flag arrives, so the
    // message does not depend on which order they were typed in.
    //
    // Refused rather than resolved. A run being fed its input while writing
    // that input down produces a file that agrees with itself by construction
    // - a verification that passes without verifying, which is worse than
    // either flag failing outright.
    if (!options.recordPath.empty() && !options.replayPath.empty()) {
        return fail("--record and --replay cannot both be given: a run cannot "
                    "record the input it is being fed");
    }

    return options;
}

} // namespace Supersonic
