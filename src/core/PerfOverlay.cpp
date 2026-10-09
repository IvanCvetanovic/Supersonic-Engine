#include "core/PerfOverlay.hpp"

#include "core/Log.hpp"

#include "imgui.h"

#if defined(__ANDROID__)
#include "platform/android/AndroidApp.hpp"
#endif

#include <cstdio>

namespace Supersonic {

namespace {

// The readout is redrawn from the last second of frames: fast enough to follow a change, long enough not to flicker.
constexpr double kReadoutSeconds = 1.0;
// A size is measured over this long, after a settle: the resize itself costs a frame or two, and a phone's clocks take a
// moment to follow a change of load.
constexpr double kPresetResultSeconds = 5.0;
constexpr double kPresetSettleSeconds = 2.5;
constexpr std::size_t kRecentLines = 12;
constexpr double kNoticeSeconds = 3.0;

constexpr float kPresetScales[PerfOverlay::kPresetCount] = {-1.0f, 1.0f, 0.6f, 0.4f, 0.3f};
constexpr const char* kPresetNames[PerfOverlay::kPresetCount] = {"Auto", "1.0", "0.6", "0.4", "0.3"};

std::string Fixed(double value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

// Where the numbers were taken, short enough to end a log line.
std::string Describe(const PerfOverlay::Environment& environment) {
    return "scale " + Fixed(environment.scale, 2) + " " + std::to_string(environment.sceneWidth) + "x" +
           std::to_string(environment.sceneHeight) + " of " + std::to_string(environment.windowWidth) + "x" +
           std::to_string(environment.windowHeight) + ", " + std::to_string(environment.samples) + "x, " +
           environment.presentMode;
}

} // namespace

PerfOverlay::PerfOverlay(const Settings& settings) : m_settings(settings) {}

float PerfOverlay::PresetScale(int preset) {
    return preset >= 0 && preset < kPresetCount ? kPresetScales[preset] : -1.0f;
}

const char* PerfOverlay::PresetName(int preset) {
    return preset >= 0 && preset < kPresetCount ? kPresetNames[preset] : "?";
}

int PerfOverlay::TakePresetRequest() {
    const int request = m_requestedPreset;
    m_requestedPreset = -1;
    return request;
}

void PerfOverlay::SetActivePreset(int preset) {
    if (preset < 0 || preset >= kPresetCount) return;
    m_activePreset = preset;
    // What was gathered describes the size before this one.
    m_presetWindow.Take();
    m_presetSettleSeconds = kPresetSettleSeconds;
    m_results[preset].valid = false;
}

void PerfOverlay::Observe(const FramePerfSample& sample, const Environment& environment) {
    if (!Active()) return;

    if (m_noticeSeconds > 0.0) m_noticeSeconds -= sample.frameSeconds;
    m_readoutWindow.Add(sample);
    if (m_settings.logSeconds > 0.0f) m_logWindow.Add(sample);
    if (m_presetSettleSeconds > 0.0) {
        m_presetSettleSeconds -= sample.frameSeconds;
    } else {
        m_presetWindow.Add(sample);
    }

    if (m_readoutWindow.Ready(kReadoutSeconds)) {
        m_readout = m_readoutWindow.Take();
        m_hasReadout = true;
    }
    if (m_presetWindow.Ready(kPresetResultSeconds)) {
        PresetResult& result = m_results[m_activePreset];
        result.valid = true;
        result.summary = m_presetWindow.Take();
        result.scale = environment.scale;
        result.scene = environment.scene;
    }
    if (m_settings.logSeconds > 0.0f && m_logWindow.Ready(m_settings.logSeconds)) {
        const std::string line = FormatPerfLine(m_logWindow.Take(), Describe(environment));
        SUPERSONIC_LOG_INFO("Perf") << line;
        m_recentLines.push_back(line);
        while (m_recentLines.size() > kRecentLines) m_recentLines.pop_front();
    }
}

std::string PerfOverlay::BuildReport(const Environment& environment) const {
    std::string report = m_settings.reportTitle + "\n";
    report += "Device: " + (environment.device.empty() ? std::string("unknown") : environment.device) + "\n";
    report += "GPU: " + (environment.gpu.empty() ? std::string("unknown") : environment.gpu) + "\n";
    report += "Window " + std::to_string(environment.windowWidth) + "x" + std::to_string(environment.windowHeight) +
              ", scene target " + std::to_string(environment.sceneWidth) + "x" + std::to_string(environment.sceneHeight) +
              " (scale " + Fixed(environment.scale, 2) + "), " + std::to_string(environment.samples) + "x MSAA, present " +
              (environment.presentMode.empty() ? std::string("?") : environment.presentMode) + "\n";
    report += "Scene: " + (environment.scene.empty() ? std::string("?") : environment.scene) + "\n";
    report += std::string("Size in force: ") + PresetName(m_activePreset) +
              (environment.atFloorMissed ? " (at the smallest size and still under the target)" : "") + "\n\n";

    report += "Sizes measured (about 5 s each, in the scene above):\n";
    bool any = false;
    for (int i = 0; i < kPresetCount; ++i) {
        const PresetResult& result = m_results[i];
        if (!result.valid) continue;
        any = true;
        const FramePerfSummary& s = result.summary;
        report += std::string("  ") + PresetName(i) + " (scale " + Fixed(result.scale, 2) + "): " + Fixed(s.fps, 1) +
                  " fps, frame " + Fixed(s.medianMs, 1) + " ms (p95 " + Fixed(s.p95Ms, 0) + "), wait " +
                  Fixed(s.meanPartMs[static_cast<std::size_t>(PerfPart::Wait)], 1) + " game " +
                  Fixed(s.meanPartMs[static_cast<std::size_t>(PerfPart::Game)], 1) + " scene " +
                  Fixed(s.meanPartMs[static_cast<std::size_t>(PerfPart::Scene)], 1) + " shadow " +
                  Fixed(s.meanPartMs[static_cast<std::size_t>(PerfPart::Shadow)], 1) + " drain " +
                  Fixed(s.meanPartMs[static_cast<std::size_t>(PerfPart::Drain)], 1) + "/" + std::to_string(s.drains) +
                  " - " + PerfVerdict(s) + "\n";
    }
    if (!any) report += "  none yet: tap a size and wait about 8 seconds, then share again.\n";

    if (m_hasReadout) {
        report += "\nLatest second:\n";
        for (const std::string& line : FormatPerfReadout(m_readout)) report += "  " + line + "\n";
        report += "  " + PerfVerdict(m_readout) + "\n";
    }
    if (!m_recentLines.empty()) {
        report += "\nRecent log lines:\n";
        for (const std::string& line : m_recentLines) report += line + "\n";
    }
    return report;
}

void PerfOverlay::Share(const Environment& environment) {
    const std::string report = BuildReport(environment);
#if defined(__ANDROID__)
    if (Android::ShareText(m_settings.reportTitle, report)) {
        m_notice = "Choose where to send the report";
    } else {
        m_notice = "Could not open the share sheet";
    }
#else
    ImGui::SetClipboardText(report.c_str());
    m_notice = "Copied to the clipboard";
#endif
    m_noticeSeconds = kNoticeSeconds;
}

void PerfOverlay::Draw(const Environment& environment) {
    if (!m_settings.overlay) return;

    ImGui::SetNextWindowPos(ImVec2(environment.safeLeft + 8.0f, environment.safeTop + 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##perfoverlay", nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (ImGui::SmallButton(m_collapsed ? "perf +" : "perf -")) m_collapsed = !m_collapsed;
    if (!m_collapsed) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Share")) Share(environment);
        if (m_noticeSeconds > 0.0) {
            ImGui::SameLine();
            ImGui::TextUnformatted(m_notice.c_str());
        }

        if (m_hasReadout) {
            for (const std::string& line : FormatPerfReadout(m_readout)) ImGui::TextUnformatted(line.c_str());
            ImGui::TextUnformatted(PerfVerdict(m_readout).c_str());
        } else {
            ImGui::TextUnformatted("measuring...");
        }
        ImGui::TextUnformatted((Describe(environment)).c_str());
        if (!environment.gpu.empty()) ImGui::TextUnformatted(environment.gpu.c_str());

        if (environment.hasController) {
            ImGui::TextUnformatted("size:");
            for (int i = 0; i < kPresetCount; ++i) {
                ImGui::SameLine();
                const bool active = i == m_activePreset;
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::SmallButton(kPresetNames[i]) && !active) m_requestedPreset = i;
                if (active) ImGui::PopStyleColor();
            }
        }
    }
    ImGui::End();
}

} // namespace Supersonic
