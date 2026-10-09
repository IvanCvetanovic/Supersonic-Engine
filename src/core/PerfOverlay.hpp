#pragma once

// A READOUT OF HOW THE GAME IS RUNNING, for a game that opts in (GameManifest::perfOverlay and perfLogSeconds), and a way to
// SEND IT: a small panel in a corner of the screen with the frame rate, where the frame's time went (FramePerf.hpp), the
// scene target's size, and buttons - Share, which hands a report to the system's share sheet on Android (the clipboard
// elsewhere), and scale presets, which fix the scene target's size for a measurement (DynamicResolution::Reconfigure).
//
// Why it exists at all: a player reports "too laggy" on a phone nobody here owns, and the log of an installed app is in
// private storage. With this a player runs the game a minute in one place, taps each preset for a few seconds and sends the
// report; two or three sizes give the part of the frame that follows the pixel count and the part that does not, and the
// split of the frame says whether the GPU or the CPU is the limit.
//
// Off, the default, nothing here runs. It draws with ImGui in the swapchain pass, over the game's picture at the window's
// resolution, and never changes what the scene draws (except through a preset the player taps).

#include "core/FramePerf.hpp"

#include <cstdint>
#include <deque>
#include <string>

namespace Supersonic {

class PerfOverlay {
public:
    struct Settings {
        // Draw the readout and its buttons.
        bool overlay{false};
        // One log line (FormatPerfLine) every this many seconds; 0 writes none.
        float logSeconds{0.0f};
        // The share sheet's title and the report's first line.
        std::string reportTitle{"Performance report"};
    };

    // Where the numbers were taken, from the app each frame (the overlay itself knows nothing of Vulkan or the window).
    struct Environment {
        std::string device;        // the phone or computer, in one line ("" when unknown)
        std::string gpu;           // the GPU and its Vulkan version
        std::string presentMode;
        std::string scene;         // the scene that is open
        std::uint32_t windowWidth{0};
        std::uint32_t windowHeight{0};
        std::uint32_t sceneWidth{0};
        std::uint32_t sceneHeight{0};
        int samples{1};
        float scale{1.0f};
        // The size controller exists (the presets work), and says the picture is as small as it may get and the target is
        // still missed.
        bool hasController{false};
        bool atFloorMissed{false};
        // The part of the window a notch or a gesture bar covers, in pixels, so the panel is not drawn under it.
        float safeLeft{0.0f};
        float safeTop{0.0f};
        // What the platform's touch layer knows (Android only: hasInput false elsewhere): fingers tracked, seconds since the
        // last touch event of any kind (-1: none yet), and the window's focus and resume flags. A screenshot taken while touch
        // seems dead then says whether touches still arrive (the number stays small) and whether the game thinks it has the
        // input (focus).
        bool hasInput{false};
        int touches{0};
        double secondsSinceTouch{-1.0};
        bool focused{true};
        bool resumed{true};
    };

    explicit PerfOverlay(const Settings& settings);

    // Whether the overlay draws or the log writes: the app skips the work of building an Environment when neither.
    bool Active() const { return m_settings.overlay || m_settings.logSeconds > 0.0f; }
    bool Drawing() const { return m_settings.overlay; }

    // One loop iteration, after the previous one's zones were read. Writes the log line when its window is full.
    void Observe(const FramePerfSample& sample, const Environment& environment);

    // Inside an ImGui frame (between NewFrame and Render). Draws when the overlay is on.
    void Draw(const Environment& environment);

    // The text a player sends: where and what, the sizes measured so far, and the latest window.
    std::string BuildReport(const Environment& environment) const;

    // The scale preset the player tapped this frame, or -1; consumed. 0 is Auto (the controller as the game configured it);
    // the others fix the scene target's scale for a measurement.
    int TakePresetRequest();
    static constexpr int kPresetCount = 5;
    // The scale a preset fixes; below 0 for Auto.
    static float PresetScale(int preset);
    static const char* PresetName(int preset);
    // The app tells the overlay which preset is in force (a preset it applied), so the buttons and the results follow.
    void SetActivePreset(int preset);

private:
    struct PresetResult {
        bool valid{false};
        FramePerfSummary summary;
        float scale{1.0f};
        std::string scene;
    };

    void Share(const Environment& environment);

    Settings m_settings;
    FramePerfWindow m_logWindow;       // the log's window
    FramePerfWindow m_readoutWindow;   // refreshes the readout every second
    FramePerfWindow m_presetWindow;    // a longer window per preset, for the report
    FramePerfSummary m_readout;
    bool m_hasReadout{false};

    int m_activePreset{0};
    int m_requestedPreset{-1};
    double m_presetSettleSeconds{0.0};
    PresetResult m_results[kPresetCount];

    bool m_collapsed{false};
    std::string m_notice;
    double m_noticeSeconds{0.0};
    std::deque<std::string> m_recentLines;   // the last log lines, for the report
};

} // namespace Supersonic
