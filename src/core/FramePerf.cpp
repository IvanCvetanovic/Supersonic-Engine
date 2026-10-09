#include "core/FramePerf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Supersonic {

namespace {

// A frame is "over 33" when it is slower than 30 fps, with a little slack for a 60 Hz display's 33.3 ms.
constexpr float kOver33Seconds = 0.0334f;
constexpr float kOver100Seconds = 0.100f;

// A number with fixed decimals, which streams make verbose and which a log line wants short.
std::string Num(double value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

float Part(const FramePerfSummary& summary, PerfPart part) {
    return summary.meanPartMs[static_cast<std::size_t>(part)];
}

} // namespace

const char* PerfPartName(PerfPart part) {
    static constexpr const char* kNames[kPerfPartCount] = {
        "wait", "prep", "shadow", "scene", "sync", "game", "xform", "ui", "drain",
    };
    return kNames[static_cast<std::size_t>(part)];
}

void FramePerfWindow::Add(const FramePerfSample& sample) {
    if (!(sample.frameSeconds > 0.0f)) return;
    m_samples.push_back(sample);
    m_seconds += sample.frameSeconds;
}

bool FramePerfWindow::Ready(double seconds, int minimumFrames) const {
    return m_seconds >= seconds && Frames() >= minimumFrames;
}

FramePerfSummary FramePerfWindow::Take() {
    FramePerfSummary summary;
    summary.frames = Frames();
    summary.seconds = m_seconds;
    if (summary.frames > 0) {
        std::vector<float> times;
        times.reserve(m_samples.size());
        double ticks = 0.0;
        for (const FramePerfSample& sample : m_samples) {
            times.push_back(sample.frameSeconds);
            if (sample.frameSeconds > kOver33Seconds) ++summary.over33;
            if (sample.frameSeconds > kOver100Seconds) ++summary.over100;
            ticks += sample.ticks;
            summary.drains += sample.drains;
            for (std::size_t i = 0; i < kPerfPartCount; ++i) summary.meanPartMs[i] += sample.partMs[i];
        }
        std::sort(times.begin(), times.end());
        const std::size_t n = times.size();
        const float median = n % 2 == 1 ? times[n / 2] : 0.5f * (times[n / 2 - 1] + times[n / 2]);
        // The value at or above 95% of the frames.
        const std::size_t p95Index = std::min(n - 1, static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n))) - 1);
        summary.medianMs = median * 1000.0f;
        summary.p95Ms = times[p95Index] * 1000.0f;
        summary.maxMs = times.back() * 1000.0f;
        summary.fps = summary.seconds > 0.0 ? static_cast<float>(static_cast<double>(n) / summary.seconds) : 0.0f;
        summary.ticksPerFrame = static_cast<float>(ticks / static_cast<double>(n));
        for (float& ms : summary.meanPartMs) ms /= static_cast<float>(n);
    }
    m_samples.clear();
    m_seconds = 0.0;
    return summary;
}

std::string FormatPerfLine(const FramePerfSummary& summary, const std::string& suffix) {
    std::string line = "Perf: " + Num(summary.fps, 1) + " fps, frame " + Num(summary.medianMs, 1) + " ms (p95 " +
                       Num(summary.p95Ms, 0) + ", max " + Num(summary.maxMs, 0) + ", " + std::to_string(summary.over33) +
                       "/" + std::to_string(summary.frames) + " over 33), wait " + Num(Part(summary, PerfPart::Wait), 1) +
                       " game " + Num(Part(summary, PerfPart::Game), 1) + " scene " + Num(Part(summary, PerfPart::Scene), 1) +
                       " prep " + Num(Part(summary, PerfPart::Prepare), 1) + " shadow " +
                       Num(Part(summary, PerfPart::Shadow), 1) + " sync " + Num(Part(summary, PerfPart::Sync), 1) +
                       " drain " + Num(Part(summary, PerfPart::Drain), 1) + "/" + std::to_string(summary.drains) +
                       ", ticks " + Num(summary.ticksPerFrame, 2);
    if (!suffix.empty()) line += ", " + suffix;
    return line;
}

std::vector<std::string> FormatPerfReadout(const FramePerfSummary& summary) {
    std::vector<std::string> lines;
    lines.push_back(Num(summary.fps, 1) + " fps  frame " + Num(summary.medianMs, 1) + " ms  (p95 " + Num(summary.p95Ms, 0) +
                    "  max " + Num(summary.maxMs, 0) + ")");
    lines.push_back("wait " + Num(Part(summary, PerfPart::Wait), 1) + "  game " + Num(Part(summary, PerfPart::Game), 1) +
                    "  scene " + Num(Part(summary, PerfPart::Scene), 1) + "  prep " + Num(Part(summary, PerfPart::Prepare), 1) +
                    " ms");
    lines.push_back("shadow " + Num(Part(summary, PerfPart::Shadow), 1) + "  sync " + Num(Part(summary, PerfPart::Sync), 1) +
                    "  drain " + Num(Part(summary, PerfPart::Drain), 1) + " (n " + std::to_string(summary.drains) + ")  ticks " +
                    Num(summary.ticksPerFrame, 1));
    return lines;
}

std::string PerfVerdict(const FramePerfSummary& summary) {
    if (summary.frames <= 0 || summary.medianMs <= 0.0f) return "no frames yet";
    const float frame = 1000.0f / std::max(summary.fps, 0.001f);   // the mean frame, which the mean parts add up to
    const float wait = Part(summary, PerfPart::Wait);
    const float drain = Part(summary, PerfPart::Drain);
    if (drain > 0.25f * frame) return "stalls: idle waits take over a quarter of the frame";
    // An idle wait is the CPU waiting for the GPU too, in a place the fence wait does not cover (the game's own update, ahead of
    // the frame's recording): a frame serialised by uploads must not read as CPU bound.
    const float waiting = wait + drain;
    if (waiting > 0.6f * frame) return "GPU or display bound: the CPU waits most of the frame";
    if (waiting < 0.15f * frame) return "CPU bound: the CPU barely waits";
    return "mixed: neither side clearly limits";
}

} // namespace Supersonic
