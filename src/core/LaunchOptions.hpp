#pragma once

// For uint32_t below. MSVC's <string> drags this in transitively and
// libstdc++'s does not, so leaving it out compiles here and fails under GCC -
// which is exactly what happened to GameRuntime.hpp in the same commit that
// added the window size, and was fixed in only one of the two headers.
#include <cstdint>
#include <string>
#include <vector>

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

    // --screenshot-every N: also write every Nth rendered frame, beside
    // screenshotPath and named by ScreenshotPathForFrame, and beside
    // screenshotUiPath when that is given. Zero means the flag was not given.
    //
    // It exists because a check of anything that MOVES - a walk cycle, a
    // flicker, a patrol - wants several frames of one run, and each extra
    // frame otherwise costs a whole process launch that loads everything
    // again to reach a frame the previous launch had already drawn.
    //
    // It rides on screenshotPath rather than taking a pattern of its own so
    // the last frame is still written exactly where it always was: a script
    // that compares that file keeps working with the flag added.
    int screenshotEvery = 0;

    // --screenshot-ui <path>: the frame as the window shows it, UI included.
    //
    // --screenshot reads the composited scene, before the ImGui pass draws
    // every UI component over it, so no label, panel or button has ever been
    // in one. This reads the SWAPCHAIN image back after that pass instead.
    // Written at the same moments --screenshot is - after the last frame of a
    // --frames run, and with --screenshot-every at every Nth frame, stamped
    // the same way - and either flag may be given without the other.
    //
    // The readback is recorded into the frame's own commands, since a
    // swapchain image cannot be read once it has been presented, so which
    // frames it takes is decided before each is drawn (ReadsBackSwapchain).
    std::string screenshotUiPath;

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

    // --window WxH. Zero means the flag was not given, and the manifest - or
    // failing that the engine's default - answers instead.
    //
    // It exists beside the manifest rather than instead of it because the two
    // serve different people. A game states its size once, in the folder it
    // ships as; a developer overrides it for one run, which is what makes a
    // measurement at another resolution repeatable rather than a source edit
    // somebody has to remember to revert.
    uint32_t windowWidth = 0;
    uint32_t windowHeight = 0;

    // --fullscreen and --windowed: open covering the monitor, or in a window,
    // whatever the manifest says. Neither means the manifest answers.
    //
    // Two flags rather than one, because the run that most needs overriding is
    // the other direction: a headless capture of a game that ships fullscreen
    // wants a window, not the whole of the desk it is being run on. Refused
    // together, as --record and --replay are, rather than one quietly winning.
    bool fullscreen = false;
    bool windowed = false;

    // --hidden: the window is created invisible and is never shown, so it
    // never takes focus either. It reads no live input: the pointer position
    // and the pad, which the OS hands to an unfocused window too, are ignored
    // (Input::IgnoreLiveDevices), so a capture cannot follow the mouse of
    // whoever is at the desk. Everything else runs as it would in a window:
    // the swapchain presents, --frames counts, --screenshot writes.
    //
    // It cannot hide a console window. The executable is a console program,
    // and Windows opens one for it, shown and focused, before main runs when
    // the launcher has none to share; a hidden run is started from a console
    // or with CREATE_NO_WINDOW (AGENTS.md).
    //
    // It exists for the run nobody watches on a desk somebody is using. A
    // capture that opens a window steals the keyboard from whatever that
    // person was doing, and a game that ships fullscreen also switches the
    // monitor under them. Refused beside --fullscreen, since the two ask for
    // opposite things; the manifest's Fullscreen key and every request a game
    // makes of WindowControl are ignored, and the window keeps the size it was
    // created at, so the capture's size depends on the command line alone.
    bool hidden = false;

    // Write every tick's input to this file, and a state hash every so often.
    //
    // Empty means record nothing, which is every ordinary run. A recording is
    // what turns "it went wrong on my machine" into a file somebody else can
    // run - see InputRecording for why it stores what the tick was handed
    // rather than what the devices did.
    std::string recordPath;

    // Feed a recorded run back in, and check it against the hashes it stored.
    //
    // Exclusive with recordPath, and the parser says so rather than picking
    // one: a run that is being fed its input and is also writing that input
    // down would produce a file that agrees with itself by construction, which
    // is a test that passes without checking anything.
    std::string replayPath;

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

    // What was accepted but is probably not what was meant. Carried like the
    // error rather than printed by Parse, so a suite can assert on it and the
    // one SupersonicApp every game constructs logs it - rather than each of
    // the four mains remembering to.
    std::vector<std::string> warnings;

    // Whether --screenshot-every asks for a capture after `renderedFrames`
    // frames have been drawn. Never at zero: nothing has been drawn yet, and
    // 0 % N would otherwise ask for a picture of no frame.
    //
    // Here rather than inside the frame loop because SupersonicApp needs a
    // device and a window before it exists, so no suite could reach a rule
    // written there.
    bool CapturesFrame(long long renderedFrames) const;

    // Where that capture goes: screenshotPath with "_f<frame>" before its
    // extension, so shots/run.png at frame 30 is shots/run_f30.png. The frame
    // is not zero-padded, matching the _f420 names captures are already
    // filed under.
    std::string ScreenshotPathForFrame(long long renderedFrames) const;

    // The same two answers for --screenshot-ui: whether --screenshot-every
    // asks for its capture after `renderedFrames` frames, and under which
    // name, screenshotUiPath stamped as ScreenshotPathForFrame stamps its own.
    bool CapturesUiFrame(long long renderedFrames) const;
    std::string UiScreenshotPathForFrame(long long renderedFrames) const;

    // Whether the frame about to be drawn as number `frame` (counting from 1)
    // has to be read back from the swapchain: a stamped UI frame, or the last
    // frame of a --frames run when --screenshot-ui is given. Asked BEFORE the
    // frame is drawn, because the copy is recorded into its commands.
    bool ReadsBackSwapchain(long long frame) const;

    // `path` with "_f<frame>" before its extension. Both stamped names are
    // this, so the two captures of one frame are named alike.
    static std::string StampFrame(const std::string& path, long long frame);

    // How many PNGs a --frames run owes by its end: every stamped frame
    // CapturesFrame and CapturesUiFrame pick from 1 to maxFrames, and one
    // final file for each of screenshotPath and screenshotUiPath that is set.
    // Zero without --frames: a run somebody closes by hand ends wherever they
    // closed it, so it has not promised any frame.
    long long CapturesOwed() const;

    // Why a run that wrote `written` captures did not do what it was asked,
    // or empty when it wrote all it owed. main() fails the run on anything
    // else, because every way a capture can fail - a surface that cannot be
    // read back, a frame the swapchain was rebuilt instead of drawn, a format
    // it cannot pack, a disk that refused the file - only logs, and a script
    // that finds last week's PNG at the path it named would otherwise take it
    // for this run's.
    std::string MissingCaptures(long long written) const;

    static LaunchOptions Parse(int argc, const char* const* argv);
    static const char* Usage();
};

} // namespace Supersonic
