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

    // Feed the simulation a constant delta instead of the measured one.
    //
    // Zero means the real clock, which is the interactive default and what a
    // player must have - a game that ignores how long a frame took plays in
    // slow motion the moment it drops below its target rate.
    //
    // Non-zero makes a run REPRODUCIBLE. The simulation is already
    // deterministic given a tick count; what varies between two runs of the
    // same scene is how many ticks fit into a frame, because that comes from
    // how fast the machine drew the last one. Pinning the delta pins the tick
    // count, so N frames is always exactly the same N frames - which is what
    // lets a rendered image be compared against anything at all.
    float fixedDelta = 0.0f;

    // Mint an identity for every asset that has none, write the sidecars, and
    // exit without opening a window.
    //
    // A command rather than something the engine does on startup, and that is
    // the whole point: minting is the only part of asset identity that WRITES,
    // so it happens when somebody asks for it. A load path that minted as a
    // side effect would have the test suite creating sidecars in the project's
    // assets folder the first time anyone ran it.
    bool importAssets = false;

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
