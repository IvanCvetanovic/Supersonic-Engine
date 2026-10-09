#pragma once

// WHAT A FRAME SAYS ABOUT ITSELF: the numbers a log line and an on-screen readout carry.
//
// A developer cannot attach a profiler to a player's phone, and the log of an installed Android app is in private storage no
// file manager shows. So when a phone is "too laggy" the only evidence is what the game itself writes down and lets the
// player send: how long the frames were, and how that time split between waiting for the GPU, the game's own work, recording
// the scene and the other parts of a frame (Profiler.hpp's zones). That separates a GPU-bound phone from a CPU-bound one, and
// the controller of DynamicResolution.hpp cannot do it by itself (it sees a frame's length, not what the frame waited for).
//
// This file is the arithmetic and the wording and nothing else - no Vulkan, no clock, no ImGui - so a test feeds it frames and
// reads the text. PerfOverlay.hpp is the part that shows it and shares it.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Supersonic {

// The parts of one loop iteration a perf line splits it into, in the order of FramePerfSample::partMs.
// Wait is the CPU blocked on the GPU or the display (the fences and the acquire inside DrawFrame); Game is everything a
// game's layers did (their tick and their update); Scene is the recording and submitting of the scene's draws; Drain is
// time spent waiting for a queue or a device to go idle, which is a stall of its own wherever it happens (a texture's first
// use, a mesh upload, a resize) and is also counted inside the zone it happened in.
enum class PerfPart : std::size_t { Wait, Prepare, Shadow, Scene, Sync, Game, Transform, Ui, Drain, Count };

constexpr std::size_t kPerfPartCount = static_cast<std::size_t>(PerfPart::Count);

const char* PerfPartName(PerfPart part);

struct FramePerfSample {
    // The whole iteration, measured by the wall clock and not clamped (the simulation's own delta is clamped to 100 ms; a
    // readout must not be).
    float frameSeconds{0.0f};
    std::array<float, kPerfPartCount> partMs{};
    int ticks{0};    // simulation ticks the iteration ran
    int drains{0};   // idle waits it made
};

struct FramePerfSummary {
    int frames{0};
    double seconds{0.0};
    float fps{0.0f};       // frames over seconds
    float medianMs{0.0f};
    float p95Ms{0.0f};
    float maxMs{0.0f};
    int over33{0};         // frames longer than 33.4 ms (under 30 fps)
    int over100{0};
    float ticksPerFrame{0.0f};
    int drains{0};
    std::array<float, kPerfPartCount> meanPartMs{};
};

// Frames gathered until a window of time is full. Take() summarises them and starts the next window.
class FramePerfWindow {
public:
    // A frame that is not a measurement (zero, negative) counts for nothing. A long one does count: a hitch is what a
    // readout is for.
    void Add(const FramePerfSample& sample);

    int Frames() const { return static_cast<int>(m_samples.size()); }
    double Seconds() const { return m_seconds; }
    bool Ready(double seconds, int minimumFrames = 10) const;

    FramePerfSummary Take();

private:
    std::vector<FramePerfSample> m_samples;
    double m_seconds{0.0};
};

// One log line for a summary, in the style of the engine's others: "Perf: 17.2 fps, frame 58.1 ms (p95 80, max 130, 40 over
// 33), wait 41.2 game 12.0 ...". `suffix` is the caller's own state (scale, sizes, present mode), appended after a comma.
// Short enough to survive a viewer that cuts a log line at 170 characters.
std::string FormatPerfLine(const FramePerfSummary& summary, const std::string& suffix);

// The same numbers as the lines of an on-screen readout (three lines, none longer than 56 characters).
std::vector<std::string> FormatPerfReadout(const FramePerfSummary& summary);

// A one-line verdict from the split, for a reader who does not want the numbers: where the frame's time went.
// "GPU or display bound" when the CPU waited for most of the frame; "CPU bound" when it barely waited; "mixed" between; and
// "stalls" when idle waits alone are a large share.
std::string PerfVerdict(const FramePerfSummary& summary);

} // namespace Supersonic
