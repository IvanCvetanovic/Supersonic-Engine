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

// Two scales that a resize would tell apart (every scale is rounded to two decimals, so they differ by 0.01 or nothing).
bool Moved(float before, float after) {
    return std::fabs(after - before) >= 0.005f;
}

} // namespace

DynamicResolution::DynamicResolution(const DynamicResolutionConfig& config) : m_config(config) {
    Mend();
    m_scale = Rounded(Clamp(m_config.startScale, m_config.minScale, m_config.maxScale));
    m_samples.reserve(256);
    m_declared = m_config;
    m_q = QualityScaleFor(m_declared, 0);
    if (Tiered()) {
        // The run starts in the upper tier, at the quality floor or above, with the grace a launch needs; and with no
        // settle, as E47's controller starts (a slow GPU's first window is already long).
        m_waits.reserve(256);
        ApplyView(m_scale, false);
        m_settle = 0;
        m_tierGrace = kTierGraceSeconds;
    }
}

DynamicResolutionConfig DynamicResolution::Mended(const DynamicResolutionConfig& given) {
    // A config that contradicts itself is mended, never trusted: a floor above the ceiling, or a start outside both, would
    // otherwise make the first window change the scale for no reason.
    DynamicResolutionConfig config = given;
    config.minScale = Clamp(config.minScale, 0.05f, 1.0f);
    config.maxScale = Clamp(config.maxScale, config.minScale, 1.0f);
    config.targetFps = std::max(config.targetFps, 1.0f);
    config.headroomFps = std::max(config.headroomFps, config.targetFps);
    config.windowSeconds = std::max(config.windowSeconds, 0.2f);
    // A rise margin under 1.1 would open the way up on windows the step (which lands on 1.2 x the target) then declines to act on.
    config.riseMargin = std::max(config.riseMargin, 1.1f);
    // An emergency floor that is not under the floor, or with no speed to trigger it, is no emergency floor.
    config.emergencyBelowFps = std::max(config.emergencyBelowFps, 0.0f);
    if (config.emergencyBelowFps > 0.0f && config.emergencyScale > 0.0f && config.emergencyScale < config.minScale) {
        config.emergencyScale = Clamp(config.emergencyScale, 0.05f, config.minScale);
    } else {
        config.emergencyScale = 0.0f;
    }
    // E50. A lower tier near the target is no second tier (and one under 10 fps is not a game).
    if (!(config.lowTierFps >= 10.0f && config.lowTierFps <= config.targetFps / 1.5f)) config.lowTierFps = 0.0f;
    config.qualityMaxShare = Clamp(config.qualityMaxShare, 0.05f, 1.0f);
    return config;
}

void DynamicResolution::Mend() {
    m_config = Mended(m_config);
}

float DynamicResolution::FloorFor(float fps) const {
    if (m_config.emergencyScale <= 0.0f) return m_config.minScale;
    // "At the floor" with the controller's own 0.01 tolerance: a fall from 0.51 to a 0.50 floor is a step the 0.01 test below
    // swallows (in float it is 0.0099999905), and a scale held at 0.51 by that is at the floor for every purpose here.
    if (m_scale > m_config.minScale + 0.015f) return m_config.minScale;
    if (fps < m_config.emergencyBelowFps) return m_config.emergencyScale;
    return std::min(m_scale, m_config.minScale);
}

DynamicResolutionConfig DynamicResolution::ForWindow(const DynamicResolutionConfig& declared, std::uint32_t windowHeight) {
    DynamicResolutionConfig config = declared;
    config.minScale = FloorForTargetHeight(declared.minScale, declared.maxScale, windowHeight, declared.minTargetHeight);
    config.emergencyScale = declared.emergencyBelowFps > 0.0f && config.minScale > declared.minScale ? declared.minScale : 0.0f;
    config.windowHeight = windowHeight;
    return config;
}

DynamicResolutionConfig DynamicResolution::Pinned(const DynamicResolutionConfig& config, float scale) {
    DynamicResolutionConfig pinned = config;
    pinned.startScale = pinned.minScale = pinned.maxScale = scale;
    pinned.relaxedRise = false;
    pinned.minTargetHeight = 0;
    pinned.emergencyBelowFps = 0.0f;
    pinned.emergencyScale = 0.0f;
    pinned.lowTierFps = 0.0f;
    return pinned;
}

float DynamicResolution::FloorForTargetHeight(float minScale, float maxScale, std::uint32_t windowHeight,
                                              std::uint32_t minTargetHeight) {
    if (windowHeight == 0 || minTargetHeight == 0) return minScale;
    // Rounded UP to the scale's two decimals: the target is the window's height times the scale, and rounding down could
    // leave it a line or two under what was asked for.
    float needed = std::ceil(100.0f * static_cast<float>(minTargetHeight) / static_cast<float>(windowHeight)) / 100.0f;
    // The target's height is the window's times the scale, truncated and made even (EditorLayer::ScaledExtent), which in float
    // can still come out a line or two short of the ceiling above (h 150 x 0.84 is 125.99999): step up by a hundredth until it
    // is tall enough, or the ceiling is reached.
    const float ceiling = std::max(minScale, maxScale);
    for (int guard = 0; guard < 100 && needed < ceiling; ++guard) {
        const std::uint32_t lines = static_cast<std::uint32_t>(static_cast<float>(windowHeight) * needed) & ~1u;
        if (lines >= minTargetHeight) break;
        needed = Rounded(needed + 0.01f);
    }
    return std::min(std::max(minScale, needed), ceiling);
}

float DynamicResolution::QualityScaleFor(const DynamicResolutionConfig& forWindow, std::uint32_t sceneLines) {
    const DynamicResolutionConfig config = Mended(forWindow);
    if (config.lowTierFps <= 0.0f || config.qualityTargetHeight == 0 || config.windowHeight == 0) return config.minScale;
    // A scene with fewer lines of art on screen than the target gains nothing from more lines than it has.
    const std::uint32_t lines = sceneLines > 0 ? std::min(config.qualityTargetHeight, sceneLines) : config.qualityTargetHeight;
    const float needed = FloorForTargetHeight(config.minScale, config.maxScale, config.windowHeight, lines);
    return Clamp(std::min(needed, config.qualityMaxShare), config.minScale, config.maxScale);
}

void DynamicResolution::Reconfigure(const DynamicResolutionConfig& config) {
    const bool wasTiered = Tiered();
    m_declared = Mended(config);
    // What a scene learned under the old configuration is not evidence about the new one either.
    ForgetScenes();
    if (!Tiered()) {
        m_lowTier = false;
        m_onTrial = false;
        m_tierDown = 0.0;
        m_tierUp = 0.0;
        m_fastRiseLeft = 0.0;
        m_q = m_declared.minScale;
        m_config = m_declared;
        // Into the comfortable range, not the emergency one: a floor that was raised (a window that got shorter) lifts a
        // picture that had fallen below it, and the next windows judge it at the floor again.
        m_scale = Rounded(Clamp(m_scale, m_config.minScale, m_config.maxScale));
        // What was learned about the old configuration is not evidence about the new one.
        ForgetJudgement();
        return;
    }
    // Tiered: the tier is kept (a controller that was held at one size starts in the upper one), the probe on trial ends
    // where it is, and the grace starts as at a launch.
    if (!wasTiered) m_lowTier = false;
    m_onTrial = false;
    m_trialSeconds = 0.0;
    m_tierDown = 0.0;
    m_tierUp = 0.0;
    m_q = QualityScaleFor(m_declared, m_sceneLines);
    ApplyView(m_scale, false);
    m_tierGrace = kTierGraceSeconds;
}

void DynamicResolution::ForgetJudgement() {
    m_samples.clear();
    m_waits.clear();
    m_windowSeconds = 0.0;
    m_calmSeconds = 0.0;
    m_judging = false;
    m_holdSeconds = 0.0;
    m_nextHoldSeconds = kFirstHoldSeconds;
    m_riseOnTrial = false;
    m_sinceRiseSeconds = 0.0;
    m_riseHoldSeconds = 0.0;
    m_nextRiseHoldSeconds = kFirstRiseHoldSeconds;
    m_settle = kSettleFrames;
}

void DynamicResolution::ApplyView(float scale, bool allowEmergency) {
    DynamicResolutionConfig view = m_declared;
    // Nothing reads it in a view (the tier rules read m_declared), and the lower tier's view, whose target IS lowTierFps,
    // would mend it away if it were ever mended.
    view.lowTierFps = 0.0f;
    if (Tiered()) {
        if (m_lowTier) {
            // The pixel floor, the emergency floor under it and the relaxed rise all stay: the lower tier is E47's controller
            // aiming at lowTierFps.
            view.targetFps = m_declared.lowTierFps;
        } else {
            // Below the quality floor belongs to the lower tier. With no emergency floor, AtTheFloor() means "at Q".
            view.minScale = Clamp(std::max(view.minScale, m_q), view.minScale, view.maxScale);
            view.emergencyBelowFps = 0.0f;
            view.emergencyScale = 0.0f;
        }
    }
    view.upAfterSeconds = m_fastRiseLeft > 0.0 ? std::min(kSceneRiseAfterSeconds, m_declared.upAfterSeconds)
                                               : m_declared.upAfterSeconds;
    m_minRiseStep = Tiered() && m_lowTier ? kLowTierMinRiseStep : 0.0f;
    m_config = view;
    const float low = allowEmergency ? AbsoluteFloor() : m_config.minScale;
    m_scale = Rounded(Clamp(scale, low, m_config.maxScale));
    ForgetJudgement();
}

void DynamicResolution::FollowQuality() {
    if (!Tiered() || m_lowTier) return;
    m_config.minScale = Clamp(std::max(m_declared.minScale, m_q), m_declared.minScale, m_declared.maxScale);
    if (m_scale < m_config.minScale) {
        m_scale = Rounded(m_config.minScale);
        // A step being judged was a step across another floor, and the window gathered was at another size.
        m_judging = false;
        m_samples.clear();
        m_waits.clear();
        m_windowSeconds = 0.0;
        m_settle = kSettleFrames;
    }
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
        m_waits.clear();
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
    // (E50: read by ObserveFrame only, after this returns.)
    ++m_windowsEnded;
    m_lastWindowSeconds = elapsed;

    const bool shortfall = fps < m_config.targetFps * kShortfallTolerance;
    m_windowMedianMs = median * 1000.0f;
    m_windowMissed = shortfall;

    // The hold after a step that did not pay runs out with time, or ends at once when the frames get much slower (a heavier
    // scene: worth another try).
    if (m_holdSeconds > 0.0) {
        m_holdSeconds = std::max(0.0, m_holdSeconds - elapsed);
        if (fps < m_revertFps * kReleaseBelow) m_holdSeconds = 0.0;
    }

    // THE RELAXED RISE (E47), when it is on and the loop is not paced by the display: the way up opens at a fraction of the
    // fall's own target instead of at 110 fps, on trial for a while after each rise.
    const bool relaxed = m_config.relaxedRise && !m_loopPaced;
    if (relaxed) {
        if (m_riseHoldSeconds > 0.0) m_riseHoldSeconds = std::max(0.0, m_riseHoldSeconds - elapsed);
        if (m_riseOnTrial) {
            m_sinceRiseSeconds += elapsed;
            if (shortfall) {
                // The rise did not hold (a heavier scene, heat, a prediction that missed): no new one for a while, longer each time.
                m_riseOnTrial = false;
                m_riseHoldSeconds = m_nextRiseHoldSeconds;
                m_nextRiseHoldSeconds = std::min(m_nextRiseHoldSeconds * 2.0, kLongestRiseHoldSeconds);
            } else if (m_sinceRiseSeconds >= kRiseTrustSeconds) {
                m_riseOnTrial = false;
                m_nextRiseHoldSeconds = kFirstRiseHoldSeconds;
            }
        }
    }
    const float riseFps = relaxed ? std::min(m_config.headroomFps, m_config.targetFps * m_config.riseMargin)
                                  : m_config.headroomFps;

    float next = m_scale;
    bool revert = false;
    bool stepDown = false;
    bool rose = false;

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
                next = ScaleAfterShortfall(m_scale, fps, m_config.targetFps, FloorFor(fps));
                stepDown = true;
            }
        } else if (fps >= riseFps) {
            m_calmSeconds += elapsed;
            // (A rise held back after one that did not hold keeps its calm: it goes the window the hold ends.)
            if (m_calmSeconds >= m_config.upAfterSeconds && !(relaxed && m_riseHoldSeconds > 0.0)) {
                m_calmSeconds = 0.0;
                next = ScaleAfterHeadroom(m_scale, fps, m_config.targetFps, m_config.maxScale);
                // 0 unless a tier view asks for more (kLowTierMinRiseStep): E47's rise is untouched.
                if (m_minRiseStep > 0.0f) next = std::min(m_config.maxScale, std::max(next, Rounded(m_scale + m_minRiseStep)));
                rose = true;
            }
        } else {
            m_calmSeconds = 0.0;
        }
    }

    next = Clamp(next, AbsoluteFloor(), m_config.maxScale);
    if (std::fabs(next - m_scale) < 0.01f) return false;
    if (stepDown && next < m_scale) {
        // To be judged by the window after it.
        m_judging = true;
        m_stepFromScale = m_scale;
        m_stepFps = fps;
    }
    if (rose && relaxed && next > m_scale) {
        m_riseOnTrial = true;
        m_sinceRiseSeconds = 0.0;
    }
    m_lastWasRise = !revert && next > m_scale;
    m_scale = next;
    m_lastMedianMs = median * 1000.0f;
    m_lastWasRevert = revert;
    m_settle = kSettleFrames;
    return true;
}

// ---- E50: the tiers and the scene memory ---------------------------------------------------------------------------------

bool DynamicResolution::Tiered() const {
    return m_declared.enabled && m_declared.lowTierFps > 0.0f && !Fixed();
}

float DynamicResolution::TierFps() const {
    if (!Tiered()) return 0.0f;
    return m_lowTier ? m_declared.lowTierFps : m_declared.targetFps;
}

DynamicResolution::TierChange DynamicResolution::TakeTierChange() {
    const TierChange change = m_tierChange;
    m_tierChange = TierChange::None;
    return change;
}

void DynamicResolution::StartSettle() {
    m_sceneSettleFrames = kSceneSettleFrames;
    m_sceneSettleSeconds = kSceneSettleSeconds;
    m_samples.clear();
    m_waits.clear();
    m_windowSeconds = 0.0;
}

bool DynamicResolution::ObserveFrame(float frameSeconds, float waitSeconds) {
    // E47, frame for frame, for a game that asked for neither tiers nor scenes.
    if (!Tiered() && !m_hasScene) return Observe(frameSeconds);
    if (!m_config.enabled) return false;

    // After a scene's entry or a resume: frames counted, not measured. Both counts must run out, so a long load frame (one
    // frame, a tenth of a second) cannot end the settle by itself, nor a burst of quick frames before a second has passed.
    if (SceneSettling()) {
        if (frameSeconds > 0.0f) {
            if (m_sceneSettleFrames > 0) --m_sceneSettleFrames;
            m_sceneSettleSeconds -= std::min(static_cast<double>(frameSeconds), kSceneSettleFrameCapSeconds);
        }
        if (m_sceneSettleFrames <= 0 && m_sceneSettleSeconds <= 0.0) {
            m_sceneSettleFrames = 0;
            m_sceneSettleSeconds = 0.0;
            m_samples.clear();
            m_waits.clear();
            m_windowSeconds = 0.0;
        }
        return false;
    }

    const float before = m_scale;
    const std::size_t samplesBefore = m_samples.size();
    const std::uint64_t windowsBefore = m_windowsEnded;
    const bool changed = Observe(frameSeconds);
    if (m_windowsEnded == windowsBefore) {
        // The waits follow the samples one for one, so each window's waits are its frames' waits.
        if (m_samples.size() > samplesBefore) m_waits.push_back(waitSeconds);
        return Moved(before, m_scale);
    }
    // A window ended with this frame (whose sample was taken and cleared with the rest). Its waits' median, or -1 when any
    // of them was unknown: a prediction from a part of the window would be a guess.
    m_waits.push_back(waitSeconds);
    float waitMedian = -1.0f;
    if (std::none_of(m_waits.begin(), m_waits.end(), [](float wait) { return wait < 0.0f; })) {
        std::vector<float> sorted = m_waits;
        std::sort(sorted.begin(), sorted.end());
        waitMedian = sorted.size() % 2 == 1 ? sorted[sorted.size() / 2]
                                            : 0.5f * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    }
    m_waits.clear();

    // A first visit's quicker rise runs out with the windows; only the view's upAfterSeconds goes back (a whole new view
    // would forget the judgement and the holds).
    if (m_fastRiseLeft > 0.0) {
        m_fastRiseLeft -= m_lastWindowSeconds;
        if (m_fastRiseLeft <= 0.0) {
            m_fastRiseLeft = 0.0;
            m_config.upAfterSeconds = m_declared.upAfterSeconds;
        }
    }
    // The window was measured at `before`: Observe may just have stepped away from it (a step down to be judged, a step
    // taken back), and the tier rules judge the size the frames were drawn at, not the one they have not seen yet.
    if (Tiered()) JudgeTier(changed, waitMedian, before);
    return Moved(before, m_scale);
}

void DynamicResolution::SwitchTier(bool lowTier, float scale) {
    m_lowTier = lowTier;
    m_tierDown = 0.0;
    m_tierUp = 0.0;
    if (lowTier) {
        m_onTrial = false;
        m_trialSeconds = 0.0;
    }
    ApplyView(scale, true);
    // A tier's change is neither a rise on room nor a step taken back, and its window is the one the getters describe.
    m_lastWasRise = false;
    m_lastWasRevert = false;
    m_lastMedianMs = m_windowMedianMs;
}

void DynamicResolution::JudgeTier(bool changedThisWindow, float waitMedian, float measuredScale) {
    const double elapsed = m_lastWindowSeconds;
    const float median = m_windowMedianMs / 1000.0f;
    const float fps = median > 0.0f ? 1.0f / median : 0.0f;
    const bool missed = m_windowMissed;

    // The grace blocks a tier DOWN only. A step taken back during it was the launch's or the load's transient, not the
    // scene: its hold is let go when the grace ends, or a GPU that is merely slow to start would be held at its first size
    // and then judged stuck there.
    const bool inGrace = m_tierGrace > 0.0;
    if (inGrace) {
        if (changedThisWindow && m_lastWasRevert) m_graceRevert = true;
        m_tierGrace = std::max(0.0, m_tierGrace - elapsed);
        if (m_tierGrace <= 0.0 && m_graceRevert) {
            m_graceRevert = false;
            m_holdSeconds = 0.0;
        }
    }
    m_tierHold = std::max(0.0, m_tierHold - elapsed);
    const float target = m_declared.targetFps;
    const float probeFrame = 1.0f / (kProbeHeadroom * target);

    if (!m_lowTier) {
        if (m_onTrial) {
            m_trialSeconds += elapsed;
            if (m_trialSeconds >= kTierTrialSeconds && !missed) {
                // The probe held: the next failure starts the holds from the shortest again.
                m_onTrial = false;
                m_nextTierHold = kFirstTierHoldSeconds;
            }
        }
        // Nothing left to try in the upper tier: measured at the quality floor, or held after a step that bought nothing (a
        // frame the CPU decides), or a probe that missed. Hopeless: not even the lower tier's rate measured at the floor.
        // A step Observe has just taken is still to be judged, and a switch now would forget that judgement (ApplyView):
        // a CPU-bound frame stepped onto the floor would go down a tier there instead of having the step taken back, so
        // such a window counts for nothing. A probe's miss is the exception: its way down is the lower tier's own size.
        const bool measuredAtFloor = measuredScale <= AbsoluteFloor() + 0.005f;
        const bool stuck = missed && (m_onTrial || (!m_judging && (measuredAtFloor || m_holdSeconds > 0.0)));
        const bool hopeless = measuredAtFloor && fps < kShortfallTolerance * m_declared.lowTierFps;
        m_tierDown = (stuck && !inGrace) ? m_tierDown + elapsed : 0.0;
        const double needed = (m_onTrial || hopeless) ? kTierQuickDownSeconds : kTierDownAfterSeconds;
        if (m_tierDown >= needed) {
            float scale = m_scale;
            if (m_onTrial) {
                scale = m_scaleLeft30;
                m_tierHold = m_nextTierHold;
                m_nextTierHold = std::min(m_nextTierHold * 2.0, kLongestTierHoldSeconds);
                ++m_probeFailures;
                m_tierChange = TierChange::ProbeFailed;
            } else {
                m_tierChange = TierChange::Down;
            }
            // The lower tier starts where the upper one is now (at the floor, back at the size a step that bought nothing was
            // taken back to, or where the lower tier was before the probe) and falls from there.
            SwitchTier(true, scale);
        }
        return;
    }

    if (m_tierHold > 0.0 || m_probeFailures >= kMaxProbeFailures) {
        m_tierUp = 0.0;
        return;
    }
    // Would the upper tier hold at the quality floor? Seen directly (the lower tier's view is not paced, so a frame can run
    // far above its target), or predicted: the part of the frame the CPU spent not waiting is exact whatever the size, and
    // the GPU's part (the whole frame, when it waited) follows the pixel count. Both from the size the window was drawn at.
    const bool direct = measuredScale >= m_q - 0.005f && fps >= kShortfallTolerance * target;
    bool predicted = false;
    if (!m_loopPaced && waitMedian >= 0.0f && median > 0.0f && measuredScale > 0.0f) {
        const float ratio = m_q / measuredScale;
        predicted = std::max(median - waitMedian, median * ratio * ratio) <= probeFrame;
    }
    if (direct || predicted) {
        m_tierUp += elapsed;
        // No rise in the lower tier past a size the upper one can reach: the probe decides that.
        m_calmSeconds = 0.0;
    } else {
        m_tierUp = 0.0;
    }
    if (m_tierUp >= kTierUpAfterSeconds) {
        m_scaleLeft30 = m_scale;
        const float scale =
            direct ? measuredScale : Clamp(measuredScale * std::sqrt(probeFrame / median), m_q, m_declared.maxScale);
        m_onTrial = true;
        m_trialSeconds = 0.0;
        m_tierChange = TierChange::Probe;
        SwitchTier(false, scale);
    }
}

DynamicResolution::SceneMemory DynamicResolution::Remember() const {
    SceneMemory memory;
    // A scene left during a probe is remembered as it was before the probe: the lower tier at its size, with no failure.
    memory.lowTier = m_onTrial || m_lowTier;
    memory.scale = m_onTrial ? m_scaleLeft30 : m_scale;
    memory.tierHold = m_tierHold;
    memory.nextTierHold = m_nextTierHold;
    memory.probeFailures = m_probeFailures;
    memory.holdSeconds = m_holdSeconds;
    memory.nextHoldSeconds = m_nextHoldSeconds;
    memory.revertFps = m_revertFps;
    memory.riseHoldSeconds = m_riseHoldSeconds;
    memory.nextRiseHoldSeconds = m_nextRiseHoldSeconds;
    return memory;
}

bool DynamicResolution::EnterScene(std::string_view key, std::uint32_t sceneLines) {
    if (key.empty()) return false;
    const float before = m_scale;

    // The same scene with another count of lines (a zoom): only the quality floor moves.
    if (m_hasScene && key == m_sceneKey) {
        if (sceneLines == m_sceneLines) return false;
        m_sceneLines = sceneLines;
        m_q = QualityScaleFor(m_declared, sceneLines);
        FollowQuality();
        return Moved(before, m_scale);
    }

    // The first scene a controller hears of (at a launch, or a new controller after a pick): adopted where it is, with no
    // settle (a launch has its grace) and nothing to remember of the frames before.
    if (!m_hasScene) {
        m_hasScene = true;
        m_sceneKey.assign(key);
        m_sceneLines = sceneLines;
        m_lastSceneRemembered = false;
        m_q = QualityScaleFor(m_declared, sceneLines);
        FollowQuality();
        return Moved(before, m_scale);
    }

    // Another scene. A picture held at one size remembers nothing and moves nowhere.
    const SceneMemory left = Remember();
    if (!Fixed()) m_scenes.insert_or_assign(m_sceneKey, left);
    m_sceneKey.assign(key);
    m_sceneLines = sceneLines;
    m_q = QualityScaleFor(m_declared, sceneLines);
    if (Fixed()) {
        m_lastSceneRemembered = false;
        return false;
    }

    SceneMemory memory;
    float scale = left.scale;
    bool lowTier = left.lowTier;
    const auto found = m_scenes.find(key);
    if (found != m_scenes.end()) {
        memory = found->second;
        scale = memory.scale;
        lowTier = memory.lowTier;
        m_fastRiseLeft = 0.0;
        m_lastSceneRemembered = true;
    } else {
        // A first visit starts where the last scene was (as it was before a probe on trial), in its tier, and rises sooner
        // for a while. Its holds start empty.
        m_fastRiseLeft = kSceneFastRiseSeconds;
        m_lastSceneRemembered = false;
        // On a loop the display paces, the lower tier below the quality floor has no way up (no relaxed rise, no
        // prediction, and the direct probe is seen only at the floor), so a heavy scene's size would decide a light one's
        // for the session. There a first visit starts no lower than its own quality floor, and the lower tier's rules lower
        // it again where lowTierFps does not hold at it.
        if (m_loopPaced && Tiered() && lowTier) scale = std::max(scale, m_q);
    }
    m_lowTier = Tiered() && lowTier;
    m_onTrial = false;
    m_trialSeconds = 0.0;
    m_tierDown = 0.0;
    m_tierUp = 0.0;
    m_graceRevert = false;
    ApplyView(scale, true);
    // After the view (which forgets holds): this scene's own, which counted down only while it was current.
    m_tierHold = memory.tierHold;
    m_nextTierHold = memory.nextTierHold;
    m_probeFailures = memory.probeFailures;
    m_holdSeconds = memory.holdSeconds;
    m_nextHoldSeconds = memory.nextHoldSeconds;
    m_revertFps = memory.revertFps;
    m_riseHoldSeconds = memory.riseHoldSeconds;
    m_nextRiseHoldSeconds = memory.nextRiseHoldSeconds;
    StartSettle();
    if (Tiered()) m_tierGrace = kTierGraceSeconds;
    return Moved(before, m_scale);
}

void DynamicResolution::ForgetScenes() {
    m_scenes.clear();
    m_tierHold = 0.0;
    m_nextTierHold = kFirstTierHoldSeconds;
    m_probeFailures = 0;
}

void DynamicResolution::NoteResume() {
    if (!Tiered() && !m_hasScene) return;
    StartSettle();
    m_tierDown = 0.0;
    m_tierUp = 0.0;
    if (Tiered()) m_tierGrace = kTierGraceSeconds;
}

} // namespace Supersonic
