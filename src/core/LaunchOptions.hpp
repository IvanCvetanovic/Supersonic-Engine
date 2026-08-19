#pragma once

#include <string>

namespace Supersonic {

// What the engine was asked to do on the command line.
//
// main() took no arguments at all, which meant the engine could only be run by
// a human closing a window. Nothing could launch it for a fixed number of
// frames, so CI could prove the binary links and nothing more - and the
// validation layers, the whole reason the project asks for the SDK, could not
// run anywhere except on a developer's desktop.
struct LaunchOptions {
    // 0 runs until the window is closed, which is the interactive default.
    // Any positive value renders exactly that many frames and exits, which is
    // what makes a headless smoke test possible.
    int maxFrames = 0;

    // Empty means the manifest's startup scene, i.e. unchanged behaviour.
    std::string scenePath;

    // Empty means no capture. Written after the last frame, so it pairs with
    // --frames: the point is a picture CI can compare, not a live viewfinder.
    std::string screenshotPath;

    // --help is a request, not a failure: it prints usage and exits zero.
    // Folding it into `ok` would make asking for help an error exit, which
    // breaks any script that checks the status.
    bool helpRequested = false;

    // Parse failures are carried rather than thrown: main() prints usage and
    // exits non-zero, and a test can assert on the message.
    bool ok = true;
    std::string error;

    static LaunchOptions Parse(int argc, const char* const* argv);
    static const char* Usage();
};

} // namespace Supersonic
