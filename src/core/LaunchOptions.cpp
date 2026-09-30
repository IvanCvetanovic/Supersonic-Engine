#include <cerrno>
#include <cstdlib>
#include "core/GameRuntime.hpp"
#include "core/LaunchOptions.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace Supersonic {

bool LaunchOptions::CapturesFrame(long long renderedFrames) const {
    return screenshotEvery > 0 && !screenshotPath.empty() && renderedFrames > 0 &&
           renderedFrames % screenshotEvery == 0;
}

std::string LaunchOptions::ScreenshotPathForFrame(long long renderedFrames) const {
    return StampFrame(screenshotPath, renderedFrames);
}

bool LaunchOptions::CapturesUiFrame(long long renderedFrames) const {
    return screenshotEvery > 0 && !screenshotUiPath.empty() && renderedFrames > 0 &&
           renderedFrames % screenshotEvery == 0;
}

std::string LaunchOptions::UiScreenshotPathForFrame(long long renderedFrames) const {
    return StampFrame(screenshotUiPath, renderedFrames);
}

bool LaunchOptions::ReadsBackSwapchain(long long frame) const {
    if (screenshotUiPath.empty() || frame <= 0) return false;
    return CapturesUiFrame(frame) || (maxFrames > 0 && frame == maxFrames);
}

long long LaunchOptions::CapturesOwed() const {
    if (maxFrames <= 0) return 0;
    // Stamped frames are the multiples of N from N to maxFrames, which is
    // CapturesFrame's rule; test_launchoptions counts it both ways.
    const long long stamped = screenshotEvery > 0 ? maxFrames / screenshotEvery : 0;
    long long owed = 0;
    if (!screenshotPath.empty()) owed += stamped + 1;
    if (!screenshotUiPath.empty()) owed += stamped + 1;
    return owed;
}

std::string LaunchOptions::MissingCaptures(long long written) const {
    const long long owed = CapturesOwed();
    if (written >= owed) return {};
    return std::to_string(owed - written) + " of the " + std::to_string(owed) +
           " capture(s) this run was asked for were not written; the log says why";
}

std::string LaunchOptions::StampFrame(const std::string& pathText, long long frame) {
    // std::filesystem for the split rather than the last '.', which would take
    // "captures.v2/run" apart at the directory. replace_filename keeps the
    // separator the caller typed, so the log names the file as it was asked for.
    std::filesystem::path path(pathText);
    const std::string stamped = path.stem().string() + "_f" + std::to_string(frame) +
                                path.extension().string();
    path.replace_filename(stamped);
    return path.string();
}

const char* LaunchOptions::Usage() {
    return "Usage: SupersonicEngine [options]\n"
           "  --frames <n>    render exactly n frames, then exit (0 = until closed)\n"
           "  --scene <path>  load this scene instead of the manifest's startup scene\n"
           "  --screenshot <path>  write a PNG of the last frame and exit\n"
           "  --screenshot-ui <path>  the same, of the window as shown: read back\n"
           "                    after the UI is drawn, so HUD text and panels are in it\n"
           "  --screenshot-every <n>  with either, also write every nth frame\n"
           "                    as <path-stem>_f<frame><ext>; pair it with\n"
           "                    --fixed-step, or the frames are wall-clock apart\n"
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
           "  --fullscreen    open covering the monitor, at its current mode\n"
           "  --windowed      open in a window, even if the manifest says fullscreen\n"
           "  --hidden        never show the window or take focus, and read no live\n"
           "                  mouse or pad; captures run as usual. Pair it with --frames\n"
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
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--windowed") {
            options.windowed = true;
        } else if (arg == "--hidden") {
            options.hidden = true;
        } else if (arg == "--import-assets") {
            options.importAssets = true;
        } else if (arg == "--screenshot") {
            if (!value(options.screenshotPath)) return fail("--screenshot needs a path");
            if (options.screenshotPath.empty()) return fail("--screenshot needs a path");
        } else if (arg == "--screenshot-ui") {
            if (!value(options.screenshotUiPath)) return fail("--screenshot-ui needs a path");
            if (options.screenshotUiPath.empty()) return fail("--screenshot-ui needs a path");
        } else if (arg == "--screenshot-every") {
            std::string raw;
            if (!value(raw)) return fail("--screenshot-every needs a frame count");

            // Whole, as --frames is, so "30f" is not quietly read as 30.
            char* end = nullptr;
            const long parsed = std::strtol(raw.c_str(), &end, 10);
            if (end == raw.c_str() || (end && *end != '\0')) {
                return fail("--screenshot-every wants a whole number, got '" + raw + "'");
            }
            // Refused rather than read as "never": zero is what a script
            // computing N from a run length produces by accident, and a flag
            // that then wrote nothing would look like a broken capture.
            if (parsed < 1) {
                return fail("--screenshot-every wants a count of at least 1, got '" + raw + "'");
            }
            if (parsed > 1000000) return fail("--screenshot-every is implausibly large: " + raw);
            options.screenshotEvery = static_cast<int>(parsed);
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

    // Refused for the reason above: whichever one won, the other was typed by
    // somebody who expected it to do something.
    if (options.fullscreen && options.windowed) {
        return fail("--fullscreen and --windowed cannot both be given");
    }

    // Refused rather than one winning, for the same reason. A hidden window
    // that went fullscreen would still switch the monitor it is not shown on.
    if (options.hidden && options.fullscreen) {
        return fail("--hidden and --fullscreen cannot both be given: a hidden "
                    "window never covers a monitor");
    }

    // After the loop for the same reason: --screenshot may come after it.
    // Refused, not ignored, because the stamped names are derived from that
    // path and there is nothing sensible to invent in its place.
    if (options.screenshotEvery > 0 && options.screenshotPath.empty() &&
        options.screenshotUiPath.empty()) {
        return fail("--screenshot-every needs --screenshot <path> or --screenshot-ui "
                    "<path>: the frames it writes are named after that path");
    }

    // Two captures of every chosen frame into one file would leave whichever
    // was written second, and say nothing about the other.
    if (!options.screenshotPath.empty() && options.screenshotPath == options.screenshotUiPath) {
        return fail("--screenshot and --screenshot-ui name the same file, '" +
                    options.screenshotPath + "': each writes its own");
    }

    // A warning rather than a refusal: frames N apart on the real clock are
    // still pictures, just not ones that reproduce, and an interactive look at
    // a run is a fair use. What it must not do is pass silently for a
    // measurement, since frame N is then not N/60 s of game time.
    if (options.screenshotEvery > 0 && options.fixedDelta <= 0.0f) {
        options.warnings.push_back(
            "--screenshot-every without --fixed-step: the simulation follows the real "
            "clock, so the frames are not a fixed game time apart and will not "
            "reproduce from one run to the next");
    }

    // A warning too: a run that ends only when the game quits is a fair thing
    // for a script to launch, but a hidden one that nobody meant to leave
    // running is a process nobody can see to close.
    if (options.hidden && options.maxFrames == 0) {
        options.warnings.push_back(
            "--hidden without --frames: there is no window to close, so the run ends "
            "only when the game quits or the process is stopped");
    }

    return options;
}

} // namespace Supersonic
