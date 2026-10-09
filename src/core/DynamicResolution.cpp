#include "core/DynamicResolution.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace {

float Clamp(float value, float low, float high) {
    return std::min(std::max(value, low), high);
}

// Two decimals: a scale that differs in the fourth place is the same scale to a resize.
float Rounded(float scale) {
    return std::round(scale * 100.0f) / 100.0f;
}

} // namespace

DynamicResolution::DynamicResolution(const DynamicResolutionConfig& config) : m_config(config) {
    Mend();
    m_scale = Rounded(Clamp(m_config.startScale, m_config.minScale, m_config.maxScale));
    m_samples.reserve(256);
}

void DynamicResolution::Mend() {
    // A config that contradicts itself is mended, never trusted: a floor above the ceiling, or a start outside both, would
    // otherwise make the first window change the scale for no reason.
    m_config.minScale = Clamp(m_config.minScale, 0.05f, 1.0f);
    m_config.maxScale = Clamp(m_config.maxScale, m_config.minScale, 1.0f);
    m_config.targetFps = std::max(m_config.targetFps, 1.0f);
    m_config.headroomFps = std::max(m_config.headroomFps, m_config.targetFps);
    m_config.windowSeconds = std::max(m_config.windowSeconds, 0.2f);
}

void DynamicResolution::Reconfigure(const DynamicResolutionConfig& config) {
    m_config = config;
    Mend();
    m_scale = Rounded(Clamp(m_scale, m_config.minScale, m_config.maxScale));
    // What was learned about the old configuration is not evidence about the new one.
    m_samples.clear();
    m_windowSeconds = 0.0;
    m_calmSeconds = 0.0;
    m_judging = false;
    m_holdSeconds = 0.0;
    m_nextHoldSeconds = kFirstHoldSeconds;
    m_settle = kSettleFrames;
}

float DynamicResolution::ScaleAfterShortfall(float scale, float fps, float target, float minScale) {
    if (fps >= target || fps <= 0.0f) return scale;
    // Cost follows the pixel count, which follows the square of the scale: the scale that would run at the target is
    // scale * sqrt(fps / target). A little less than that (3%), because landing exactly on the target would leave the next
    // window a hair under it and step again.
    const float ratio = std::sqrt(fps / target) * 0.97f;
    return Rounded(std::max(minScale, scale * Clamp(ratio, 0.5f, 0.95f)));
}

float DynamicResolution::ScaleAfterHeadroom(float scale, float fps, float target, float maxScale) {
    if (fps <= 0.0f) return scale;
    // The scale at which the predicted speed is still the target plus 20%; the pixel count follows the square of the scale, as
    // above.
    const float room = std::sqrt(fps / (target * 1.2f));
    const float next = scale * Clamp(room, 1.0f, 1.25f);
    return Rounded(std::min(maxScale, next));
}

bool DynamicResolution::Observe(float frameSeconds) {
    if (!m_config.enabled) return false;
    if (!(frameSeconds > 0.0f)) return false;
    if (frameSeconds > kIgnoreAboveSeconds) {
        // A load or a hitch: what was gathered before it describes a different moment.
        m_samples.clear();
        m_windowSeconds = 0.0;
        m_settle = std::max(m_settle, 2);
        return false;
    }
    if (m_settle > 0) {
        --m_settle;
        return false;
    }

    m_samples.push_back(frameSeconds);
    m_windowSeconds += frameSeconds;
    if (m_windowSeconds < m_config.windowSeconds || m_samples.size() < kMinimumSamples) return false;

    // The window's speed: the median frame, so that one slow frame decides nothing.
    std::vector<float> sorted = m_samples;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted.size() % 2 == 1
                             ? sorted[sorted.size() / 2]
                             : 0.5f * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    const float fps = 1.0f / median;
    const double elapsed = m_windowSeconds;
    m_samples.clear();
    m_windowSeconds = 0.0;

    const bool shortfall = fps < m_config.targetFps * kShortfallTolerance;
    m_windowMedianMs = median * 1000.0f;
    m_windowMissed = shortfall;

    // The hold after a step that did not pay runs out with time, or ends at once when the frames get much slower (a heavier
    // scene: worth another try).
    if (m_holdSeconds > 0.0) {
        m_holdSeconds = std::max(0.0, m_holdSeconds - elapsed);
        if (fps < m_revertFps * kReleaseBelow) m_holdSeconds = 0.0;
    }

    float next = m_scale;
    bool revert = false;
    bool stepDown = false;

    // THE JUDGEMENT of the last step down: did the frame speed up by what the pixel count promised? A frame the CPU or the
    // driver limits does not, and a smaller picture then costs quality for nothing.
    if (m_judging) {
        m_judging = false;
        const float predicted = (m_stepFromScale / m_scale) * (m_stepFromScale / m_scale) - 1.0f;
        const float gain = m_stepFps > 0.0f ? fps / m_stepFps - 1.0f : 0.0f;
        if (shortfall && predicted > 0.0f && gain < std::min(kMinimumBenefit * predicted, kEnoughBenefit)) {
            revert = true;
            next = m_stepFromScale;
            m_holdSeconds = m_nextHoldSeconds;
            m_nextHoldSeconds = std::min(m_nextHoldSeconds * 2.0, kLongestHoldSeconds);
            // The speed at the size we are going back to, from before the step: "a much heavier scene" is judged against
            // like with like. The faster window just measured (at the smaller size) was used once, and a step that had
            // bought 43% or more cancelled its own hold in the next window and oscillated, a resize every second or two.
            m_revertFps = m_stepFps;
            m_calmSeconds = 0.0;
        } else {
            // It paid (or the target is met now): the next failure starts the holds from the shortest again.
            m_nextHoldSeconds = kFirstHoldSeconds;
        }
    }

    if (!revert) {
        if (shortfall) {
            m_calmSeconds = 0.0;
            if (m_holdSeconds <= 0.0) {
                next = ScaleAfterShortfall(m_scale, fps, m_config.targetFps, m_config.minScale);
                stepDown = true;
            }
        } else if (fps >= m_config.headroomFps) {
            m_calmSeconds += elapsed;
            if (m_calmSeconds >= m_config.upAfterSeconds) {
                m_calmSeconds = 0.0;
                next = ScaleAfterHeadroom(m_scale, fps, m_config.targetFps, m_config.maxScale);
            }
        } else {
            m_calmSeconds = 0.0;
        }
    }

    next = Clamp(next, m_config.minScale, m_config.maxScale);
    if (std::fabs(next - m_scale) < 0.01f) return false;
    if (stepDown && next < m_scale) {
        // To be judged by the window after it.
        m_judging = true;
        m_stepFromScale = m_scale;
        m_stepFps = fps;
    }
    m_scale = next;
    m_lastMedianMs = median * 1000.0f;
    m_lastWasRevert = revert;
    m_settle = kSettleFrames;
    return true;
}

} // namespace Supersonic
