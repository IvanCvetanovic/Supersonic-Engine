#pragma once

// DYNAMIC RESOLUTION: the size of a game's scene target, chosen by how fast the GPU draws it.
//
// A phone or a tablet may ask a GPU for more pixels than it can shade: a Xiaomi Pad 5 (Adreno 640) drew one 2D scene at
// 2560x1600 in 117 ms, and every part of that frame scaled with the pixel count (half the width and height: 4x the speed)
// while nothing else moved it. The picture a game draws there is usually authored for a far smaller screen, so the right
// answer is not a fixed cap (a GPU ten times slower needs a size ten times smaller) but the largest size the GPU can keep
// up with.
//
// This is that decision and nothing else: pure arithmetic over the wall time of each frame, with no Vulkan, no clock and no
// state outside itself, so it is tested by feeding it frame times. The engine asks it once per frame (SupersonicApp::Run)
// and resizes the scene target when it says the scale changed; the picture is then stretched to the window as it always was.
//
// HOW IT DECIDES. Frames are gathered for about a second; the MEDIAN of their times is the window's speed, so one slow
// frame (a scene load, a hitch) moves nothing. Below the target frame rate (94% of it: a rate near the target is left
// alone) the scale falls by what the shortfall asks (cost follows the pixel count, so the new scale is the old one times the
// square root of the speed ratio, with a margin), at most to half in one step; at the bottom it stops. Far above the target
// for a long time, it rises, more slowly.
//
// WHAT IT CANNOT SEE. It sees the length of a frame, not what the frame is waiting for. Two consequences, both handled the
// cheap way:
//   - A step down that does not pay for itself. A frame the CPU or the driver limits does not follow the pixel count, so a
//     smaller picture changes nothing: the controller judges every step down by the next window and, when less than a
//     quarter of the gain the pixel count predicted arrived - and, for a large step, less than a sixth in all - (and the
//     target is still missed), takes the step back and holds off for half a minute, doubling the hold after each repeat. A
//     phone that is not GPU-bound keeps its picture.
//   - A rise needs proof of room, and a loop paced by the display's refresh rate cannot give it: only frames far above the
//     target (110 fps for eight seconds on end) prove the GPU has room, so on a paced loop the scale only ever falls, and the
//     size it starts at (startMaxPixels) is the most it will have. The engine's mailbox present, which Android and most
//     desktops offer, is unpaced.
// Steps are rare on purpose: each one recreates the scene target, which costs a frame or two.
//
// TWO TIERS AND A MEMORY PER SCENE (E50, opt-in: lowTierFps, and EnterScene). A GPU too slow for the target at any size
// worth looking at would otherwise be chased down to the emergency floor, a picture stretched three times over, for a
// frame rate it never reaches. With lowTierFps set, the controller has two tiers. The upper one is E47's controller with
// its floor raised to a quality floor Q (qualityTargetHeight lines of picture); where the target cannot be held at Q, the
// lower tier keeps a steady lowTierFps (30) at the largest size that holds it, and a probe goes back up when the frames
// say the upper tier would hold at Q (seen directly, or predicted from the time the frame waited for the GPU). A failed
// probe holds the next one off for a minute, doubling, and two failures end the probing of that scene.
// A scene (EnterScene, a key the game chooses) keeps its own tier and size, and its own holds, for the session: a heavy
// menu no longer decides a light level's size, and a reload does not re-arm a probe that already failed. Every decision
// is still the window's median, so a hitch moves nothing. A game that sets neither has E47's controller, frame for frame.
//
// A PLAYER'S PICK (RenderScaleControl.hpp) is a configuration made from the game's (ForRequest): Automatic is the game's
// own, Fixed a size held (Pinned), and Capped (E50) the game's own with its larger sizes taken away (Capped). A ceiling
// keeps every floor and the fall, so a pick made for frames still falls where the GPU needs it to, where a pin of the same
// size was held at a frame rate the game's own controller left behind.

#include "core/RenderScaleControl.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Supersonic {

struct DynamicResolutionConfig {
    // Off, the default, is the engine as it was: the scene target is the window's size.
    bool enabled{false};

    // The scale a run starts at, of the window's width and height: a game that knows its target screens are far beyond what a
    // GPU draws starts lower, so the first seconds are not slow.
    float startScale{1.0f};
    // A start size by pixel budget as well: when the window has more pixels than this, the run starts at the scale that fits
    // the budget instead (never above startScale). 0 is no budget. A tablet's 4-megapixel window is a larger start than any 2D
    // game's picture needs; the controller then only has to refine.
    std::uint32_t startMaxPixels{0};
    // The scale never leaves this range. The floor is a picture still worth looking at (a third of the window's size); the
    // ceiling is the window's own size. A floor equal to the ceiling is a FIXED scale: no controller, only a size (unless an
    // emergency floor lies under it, below: the scale may then still fall there).
    float minScale{0.3f};
    float maxScale{1.0f};

    // The speed a game wants: below 94% of it (median frame rate of a window) the scale falls.
    float targetFps{58.0f};
    // Above it for upAfterSeconds on end, the scale may rise (never so far that the predicted speed falls below the target
    // with a margin).
    float headroomFps{110.0f};
    float windowSeconds{1.2f};
    float upAfterSeconds{8.0f};

    // ---- Opt-in refinements (E47). Every one is off by default: a game that sets none of them has the controller as E41 made it.

    // A rise on room, not on proof of an enormous one. E41 raises the scale only after headroomFps (110) for upAfterSeconds on
    // end, and a window anywhere between the shortfall line and 110 fps resets that count, so a phone whose frame has a fixed
    // share (the CPU, the driver) that keeps it under 9 ms never gets back a size it lost - to a load hitch at a scene's start,
    // to a heavy scene, to heat. With this on, the way up opens at targetFps x riseMargin as well, and the step is still the
    // one ScaleAfterHeadroom predicts to keep targetFps x 1.2 (the prediction follows the pixel count entirely, so a frame with
    // a fixed share lands above it: the rise errs towards keeping the speed). Never on a loop the display paces (SetLoopPaced):
    // there a window cannot show more than the refresh rate, so room is invisible and the rule would only guess.
    bool relaxedRise{false};
    float riseMargin{1.25f};

    // A floor in pixels: the scene target is never fewer lines high than this (0, the default, is the floor minScale alone).
    // The caller that knows the window turns it into a scale with FloorForTargetHeight, and again whenever the window's size
    // changes; the controller itself only ever sees minScale.
    std::uint32_t minTargetHeight{0};

    // A floor that gives way when the game would be unplayably slow. With minScale as the floor of a picture worth looking at
    // (the pixel floor above), a GPU that cannot keep even 30 fps there would be left to crawl; with emergencyBelowFps > 0 the
    // scale may go on down to emergencyScale, but only on a window that, AT the floor, ran under emergencyBelowFps. Off by
    // default (0): minScale is then a hard floor, as in E41. emergencyScale is worked out by the caller that raises minScale
    // (SupersonicApp: the floor as the game declared it) and is mended to nothing unless it is under minScale.
    float emergencyBelowFps{0.0f};
    float emergencyScale{0.0f};

    // ---- Opt-in tiers (E50). Off by default (lowTierFps 0): a game that sets none of them has E47's controller.

    // The frame rate of the lower tier, kept where targetFps cannot be held at the quality floor. Mended to 0 (no tiers)
    // unless it lies in [10, targetFps / 1.5]: a lower tier that is nearly the target is no second tier.
    float lowTierFps{0.0f};
    // The quality floor of the upper tier in lines of picture: the upper tier never goes below it, and gives way to the lower
    // tier instead. 0 is minScale itself. A scene with fewer lines of art on screen (EnterScene) asks for no more than those.
    std::uint32_t qualityTargetHeight{0};
    // ... and never more than this share of the window, so a short window is not asked for nearly all of its lines.
    float qualityMaxShare{0.75f};
    // The window's height in pixels, written by ForWindow: the quality floor is worked out from it. 0 is unknown (Q is then
    // minScale).
    std::uint32_t windowHeight{0};
};

class DynamicResolution {
public:
    explicit DynamicResolution(const DynamicResolutionConfig& config);

    // One frame's wall time in seconds. True when the scale changed (the caller resizes its target). A frame that is not a
    // measurement - zero or negative, or longer than kIgnoreAboveSeconds (a scene load, a long hitch) - counts for nothing.
    bool Observe(float frameSeconds);

    // Takes a new configuration while running (a graphics-quality pick): mended like the constructor's, the judgement and
    // the hold forgotten, the current scale kept when it still fits the new range and moved into it when it does not. The
    // caller compares Scale() before and after to know whether to resize. What the scenes learned is forgotten as well
    // (ForgetScenes), and a tiered controller keeps its tier and starts the tier grace.
    void Reconfigure(const DynamicResolutionConfig& config);

    // ---- E50: the tiers and the scene memory (opt-in) ----

    // One frame's wall time and the part of it the CPU spent waiting for the GPU (the frame's fence and acquire waits), in
    // seconds; a wait below 0 is unknown. With no tiers and no scene ever entered this is Observe(frameSeconds), exactly.
    // True when the scale changed.
    bool ObserveFrame(float frameSeconds, float waitSeconds);

    // The game's scene, by a key it chooses, and the lines of art it shows (0: unknown; the quality floor then asks for
    // qualityTargetHeight lines). Leaving a scene remembers its tier, size and holds for the session; entering one that was
    // seen restores them, and a first visit starts from where the last scene was (on a loop the display paces, no lower
    // than its own quality floor: nothing else could lift it there) and rises faster for a while. The frames
    // after an entry are not measured (kSceneSettleFrames and kSceneSettleSeconds: the load frame and the first draws are
    // not the scene's speed). The same key with other lines only moves the quality floor. An empty key is no scene. True
    // when the scale changed.
    bool EnterScene(std::string_view key, std::uint32_t sceneLines);
    // Forgets what every scene learned (the current one stays current, as on a first visit), and the probes' holds and
    // failures: a window of another height is another picture.
    void ForgetScenes();
    // The app came back from the background: the same settle and tier grace as a scene's entry, and nothing forgotten. The
    // first frames after a resume (a new swapchain, a sound stream opening) are not the scene's speed. Nothing for E47's
    // controller (no tiers, no scene).
    void NoteResume();

    // Whether the two tiers are in force (lowTierFps, mended, on an enabled controller whose floor is under its ceiling).
    bool Tiered() const;
    // Whether the game noted a scene to this controller (EnterScene): with the tiers, what takes ObserveFrame past E47's
    // controller, and what a resume (NoteResume) and a window without focus are for.
    bool HasScene() const { return m_hasScene; }
    // The frame rate the current tier keeps (targetFps or lowTierFps); 0 with no tiers.
    float TierFps() const;
    // The quality floor of the current scene, as a scale; minScale with no tiers.
    float QualityScale() const { return m_q; }
    // The last tier change, for the log; taking it clears it.
    enum class TierChange { None, Down, Probe, ProbeFailed };
    TierChange TakeTierChange();
    // The current scene's probe hold (seconds left) and failed probes; whether the frames are not being measured after a
    // scene's entry or a resume; whether the last scene entered had been seen before; seconds of tier grace left; and how
    // many windows ended so far (for the log and the tests).
    double TierHoldSeconds() const { return m_tierHold; }
    int ProbeFailures() const { return m_probeFailures; }
    bool SceneSettling() const { return m_sceneSettleFrames > 0 || m_sceneSettleSeconds > 0.0; }
    bool LastSceneRemembered() const { return m_lastSceneRemembered; }
    double TierGraceSeconds() const { return m_tierGrace; }
    std::uint64_t WindowsEnded() const { return m_windowsEnded; }

    // No tier change for this long after a launch, a scene's entry or a resume: pipeline compiles and first draws look
    // like a GPU that cannot hold the target (the Pad 5 launches with about three windows at 8-15 fps).
    static constexpr double kTierGraceSeconds = 4.0;
    // The upper tier gives way after this long with nothing left to try (at the quality floor, or held after a step that
    // did not pay, which is a CPU-bound frame), and after one window when even the lower tier's rate is out of reach at
    // the floor or a probe missed.
    static constexpr double kTierDownAfterSeconds = 2.4;
    static constexpr double kTierQuickDownSeconds = 1.2;
    // The lower tier probes after this long of windows that say the upper one would hold (as long as upAfterSeconds).
    static constexpr double kTierUpAfterSeconds = 8.0;
    // A probe is on trial this long (as a relaxed rise is).
    static constexpr double kTierTrialSeconds = 20.0;
    // After a failed probe: no probe for this long, doubling up to the longest; after kMaxProbeFailures, none in that scene
    // for the session.
    static constexpr double kFirstTierHoldSeconds = 60.0;
    static constexpr double kLongestTierHoldSeconds = 480.0;
    static constexpr int kMaxProbeFailures = 2;
    // A probe aims at targetFps x this (69.6 fps at 58: 14.4 ms), where ScaleAfterHeadroom lands.
    static constexpr float kProbeHeadroom = 1.2f;
    // The settle after a scene's entry or a resume lasts at least this many frames AND this many seconds, a frame counting
    // for no more than kSceneSettleFrameCapSeconds: a 600 ms load frame is one frame and a tenth of a second, not the settle.
    static constexpr int kSceneSettleFrames = 30;
    static constexpr double kSceneSettleSeconds = 1.0;
    static constexpr double kSceneSettleFrameCapSeconds = 0.1;
    // A first visit to a scene rises after this long of calm (instead of upAfterSeconds) for its first seconds.
    static constexpr float kSceneRiseAfterSeconds = 2.4f;
    static constexpr double kSceneFastRiseSeconds = 20.0;
    // The lower tier's least rise. ScaleAfterHeadroom sizes a rise as if the whole frame were the GPU's, which is right
    // when it is; when the CPU sets the frame (the GPU waits for nothing), the picture can grow for free until the GPU's
    // part reaches the CPU's, and the predicted step shrinks to nothing: a CPU-heavy scene entered at a heavy menu's size
    // (38.5 fps at 0.31, a step of 0.0106) stayed there for good. A rise that overshoots is caught by the rise trial and
    // held, as any rise is.
    static constexpr float kLowTierMinRiseStep = 0.03f;

    // The quality floor, as a scale, of a configuration worked out for its window (ForWindow writes windowHeight), for a
    // scene showing `sceneLines` lines of art (0: unknown): qualityTargetHeight lines, or the scene's own if fewer, never
    // more than qualityMaxShare of the window, inside [minScale, maxScale]. minScale with no tiers or no height.
    static float QualityScaleFor(const DynamicResolutionConfig& forWindow, std::uint32_t sceneLines);
    // A configuration mended as the controller mends it (the constructor and Reconfigure): pure, so a test can state it.
    static DynamicResolutionConfig Mended(const DynamicResolutionConfig& config);

    // Whether the loop is paced by the display (a FIFO present: MoltenVK, a device with no MAILBOX). Set by the engine, which
    // knows the present mode; a paced loop never takes the relaxed rise (DynamicResolutionConfig::relaxedRise).
    void SetLoopPaced(bool paced) { m_loopPaced = paced; }
    bool LoopPaced() const { return m_loopPaced; }

    float Scale() const { return m_scale; }
    // Whether the last change was a rise on room (not the taking back of a step down, which also enlarges the picture) (for
    // the log).
    bool LastChangeWasARise() const { return m_lastWasRise; }
    // The median frame time, in milliseconds, of the window that changed the scale last; 0 before one.
    float LastMedianMilliseconds() const { return m_lastMedianMs; }
    // The median of the window that ended last, whether or not it changed anything (for a log line); 0 before one. And
    // whether that window was a shortfall (below 94% of the target): at the floor, true means the picture cannot get
    // smaller and the target is still missed.
    float WindowMedianMilliseconds() const { return m_windowMedianMs; }
    bool WindowMissedTheTarget() const { return m_windowMissed; }
    bool AtTheFloor() const { return m_scale <= AbsoluteFloor() + 0.005f; }
    // Whether the last change took back a step down that did not pay for itself (for the log).
    bool LastChangeWasARevert() const { return m_lastWasRevert; }

    // A window is a shortfall only below this fraction of the target: a frame rate within 6% of it is on target, and stepping
    // again for the last few frames a second costs a resize for nothing.
    static constexpr float kShortfallTolerance = 0.94f;
    // A step down must bring at least this share of the speed-up the pixel count predicts, or it is taken back.
    static constexpr float kMinimumBenefit = 0.25f;
    // ... but a step that bought at least this much is kept, however large the prediction was: the prediction is quadratic,
    // so a halving "needs" a 75% speed-up, and a frame with a fixed share (a memory-bound pass, a window-sized pass) that
    // got 40% faster was being thrown away and the player held at the slower size.
    static constexpr float kEnoughBenefit = 0.15f;
    // After a step that did not pay: no step down for this long, doubling after each repeat up to the longest. A heavier scene
    // (the frame rate falling below kReleaseBelow of what it was at the revert) ends the hold early.
    static constexpr double kFirstHoldSeconds = 30.0;
    static constexpr double kLongestHoldSeconds = 480.0;
    static constexpr float kReleaseBelow = 0.7f;
    // The relaxed rise's own judgement: a shortfall within this many seconds of a rise says the rise was a mistake (a heavier
    // scene, heat, a prediction that did not hold). The next rise then waits kFirstRiseHoldSeconds, doubling after each
    // repeat up to kLongestRiseHoldSeconds, so a rise and a fall cannot take turns every few seconds (each costs a resize).
    static constexpr double kRiseTrustSeconds = 20.0;
    static constexpr double kFirstRiseHoldSeconds = 60.0;
    static constexpr double kLongestRiseHoldSeconds = 480.0;
    // Frames after a change that are not counted: the resize itself costs one or two.
    static constexpr int kSettleFrames = 4;
    // A frame longer than this is a load or a hitch, not the GPU's speed.
    static constexpr float kIgnoreAboveSeconds = 1.5f;
    // The smallest window that decides anything.
    static constexpr std::size_t kMinimumSamples = 6;

    // The new scale for a window that drew at `fps` against `target`, from `scale`: the pure rule, public so a test can state
    // it. Falls to at least half of `scale` in one step, never below `minScale`.
    static float ScaleAfterShortfall(float scale, float fps, float target, float minScale);
    // The new scale after a long calm at `fps`: rises by at most a quarter, never above `maxScale`, and never to a size that is
    // predicted to fall below the target with 20% to spare.
    static float ScaleAfterHeadroom(float scale, float fps, float target, float maxScale);
    // The floor, as a share of a window `windowHeight` pixels high, that keeps the scene target at least `minTargetHeight`
    // pixels high: `minScale` raised to it, never above `maxScale`. 0 for either pixel count is no pixel floor (`minScale`
    // as given). The pixel count is what a picture's legibility follows (a text of 8 pixels in the art is unreadable below
    // about 360 lines whatever the window is), where a share of the window is a very different picture on a phone and on a
    // tablet.
    static float FloorForTargetHeight(float minScale, float maxScale, std::uint32_t windowHeight, std::uint32_t minTargetHeight);
    // The configuration a game declared, for a window `windowHeight` lines high: minScale raised by the pixel floor
    // (minTargetHeight) and, when the declaration asks for an emergency floor (emergencyBelowFps > 0) and the pixel floor
    // raised minScale, the declared floor kept as emergencyScale. Pure, so that whoever owns the window can call it again when
    // the window's height changes and reconfigure only when the result differs.
    static DynamicResolutionConfig ForWindow(const DynamicResolutionConfig& declared, std::uint32_t windowHeight);
    // The same configuration held at one size: no controller, only a scale (min = max = start), with no emergency floor to let
    // it fall, no rise, no pixel floor. What a player's pick of a size, or a measurement's fixed size, is.
    static DynamicResolutionConfig Pinned(const DynamicResolutionConfig& config, float scale);
    // E50. A configuration worked out for its window (ForWindow) with a ceiling: it starts at `ceiling` and never goes above
    // it (inside [minScale, maxScale]: a ceiling under the floor is the floor), with every floor, the emergency floor, the
    // relaxed rise and the target kept, and no lower tier (a ceiling is asked for frames, not for a steady lowTierFps). Unlike
    // a pin, it still falls where the GPU needs it to, to the emergency floor if need be. On a loop the display paces
    // (`paced`), where there is an emergency floor and the ceiling is under kCeilingNearTheFloor x minScale, the ceiling is
    // the floor as well (minScale = maxScale, the emergency floor kept): a window under emergencyBelowFps falls straight to
    // the emergency floor. Unpaced, the small step to the pixel floor shows what it bought and is judged as any step is.
    static DynamicResolutionConfig Capped(const DynamicResolutionConfig& forWindow, float ceiling, bool paced = false);
    // The step from a ceiling under 1.25 x the pixel floor down to that floor cuts fewer than about a third of the pixels
    // (1 - 1/1.25^2 = 0.36): on a loop the display paces it can land in the same refresh interval, be taken back as a step
    // that bought nothing, and hold the picture above the floor from which alone the emergency floor is reached (a 1340x800
    // tablet's Fast, 0.5 over 0.45, rested at 20 fps where the game's own controller kept 30 at the emergency floor).
    static constexpr float kCeilingNearTheFloor = 1.25f;
    // E50. The configuration a player's pick gives the controller: `declared` is the game's own (the start size its budget
    // chose included), `windowHeight` the window it is worked out for, `startScale` the scale the controller started at (a
    // pick's fraction is a share of it). Automatic is ForWindow; Fixed is the size held at fraction x startScale, no lower
    // than the window's floor and no higher than the window (Pinned); Capped is the window's configuration under a ceiling of
    // fraction x startScale (Capped; `paced` as there). Pure, so a test can say what a pick, or a new window height under
    // one, asks for.
    static DynamicResolutionConfig ForRequest(const DynamicResolutionConfig& declared, std::uint32_t windowHeight,
                                              float startScale, RenderScaleMode mode, float fraction, bool paced = false);
    // E50. Whether a pick asks for what the controller already is, so that the engine changes nothing (a new configuration
    // forgets what the controller learned): `pinned` and `pinnedFraction` say whether the picture is held and at which
    // share of the start size (0 where a measurement's preset held it, which no pick asks for), `cappedFraction` the ceiling
    // in force (0 for none). Fixed is applied when held at that share; Automatic when neither held nor capped; Capped when not
    // held and capped at that share.
    static bool RequestIsApplied(RenderScaleMode mode, float fraction, bool pinned, float pinnedFraction, float cappedFraction);
    // E50, a switch for the case where a ceiling can settle larger than the game's own controller: with tiers, the upper
    // tier never goes below its quality floor Q, so on a window where Q is under a ceiling (a 2560x1600 tablet started at
    // 0.73 has Q 0.34 and Fast's ceiling 0.365) both aim at the target below Q, and a ceiling's small step down can be taken
    // back where the controller's large one is kept. On, a Capped pick's ceiling is no higher than the window's Q for
    // qualityTargetHeight lines (on a tiered configuration only: with no tiers there is no Q). Off by default: it would lower
    // Balanced on most windows (0.7 to 0.68 on a 1340x800 tablet, to 0.5 on a 2400x1080 phone) for the sake of that one case.
    static constexpr bool kCeilingAtMostTheQualityFloor = false;

private:
    // What a scene learned, kept while another is current: its tier and size, and every hold with the length of the next.
    struct SceneMemory {
        bool lowTier{false};
        float scale{1.0f};
        double tierHold{0.0};
        double nextTierHold{kFirstTierHoldSeconds};
        int probeFailures{0};
        double holdSeconds{0.0};
        double nextHoldSeconds{kFirstHoldSeconds};
        float revertFps{0.0f};
        double riseHoldSeconds{0.0};
        double nextRiseHoldSeconds{kFirstRiseHoldSeconds};
    };

    void Mend();
    // The configuration E47's machinery runs on, built from m_declared for the current tier (the upper tier's floor raised
    // to the quality floor, with no emergency floor under it; the lower tier aiming at lowTierFps), then Reconfigure's
    // forgetting: the scale moved into the view's range (the emergency range too when `allowEmergency`), the window, the
    // judgement, the holds and the rise trial cleared, the settle frames to run.
    void ApplyView(float scale, bool allowEmergency);
    // Reconfigure's forgetting, shared by it and ApplyView.
    void ForgetJudgement();
    // The quality floor moved (a scene's lines, a scene adopted): the upper tier's floor follows it without forgetting
    // anything, and lifts a picture under it.
    void FollowQuality();
    // The tier rules, on a window that just ended, measured at `measuredScale` (the scale before Observe acted on it).
    void JudgeTier(bool changedThisWindow, float waitMedian, float measuredScale);
    void SwitchTier(bool lowTier, float scale);
    SceneMemory Remember() const;
    void StartSettle();
    // Held at one size: a floor at the ceiling with no emergency floor under it (m_declared is mended, so an emergency floor
    // that is there is under minScale). One with an emergency floor still falls, and remembers its scenes like any other.
    bool Fixed() const { return m_declared.minScale >= m_declared.maxScale && m_declared.emergencyScale <= 0.0f; }
    // The lowest scale there is: the emergency floor when the config has one, else minScale.
    float AbsoluteFloor() const { return m_config.emergencyScale > 0.0f ? m_config.emergencyScale : m_config.minScale; }
    // The floor a shortfall at `fps` may fall to from the current scale: minScale from above (a fall is measured at the
    // comfortable floor first), the emergency floor when the window at that floor ran under emergencyBelowFps, and not below
    // where it already is otherwise.
    float FloorFor(float fps) const;

    DynamicResolutionConfig m_config;
    float m_scale;
    float m_lastMedianMs{0.0f};
    float m_windowMedianMs{0.0f};
    bool m_windowMissed{false};
    bool m_lastWasRevert{false};
    int m_settle{0};
    double m_windowSeconds{0.0};
    double m_calmSeconds{0.0};
    std::vector<float> m_samples;

    // The last step down, to be judged by the window after it.
    bool m_judging{false};
    float m_stepFromScale{1.0f};
    float m_stepFps{0.0f};
    // The hold after a step that did not pay: seconds left, the length of the next one, and the speed at the revert.
    double m_holdSeconds{0.0};
    double m_nextHoldSeconds{kFirstHoldSeconds};
    float m_revertFps{0.0f};

    // The relaxed rise (DynamicResolutionConfig::relaxedRise): whether the display paces the loop; whether the last change was a
    // rise; seconds since the last rise while it is still on trial; the hold after a rise that did not hold and the length of
    // the next one.
    bool m_loopPaced{false};
    bool m_lastWasRise{false};
    bool m_riseOnTrial{false};
    double m_sinceRiseSeconds{0.0};
    double m_riseHoldSeconds{0.0};
    double m_nextRiseHoldSeconds{kFirstRiseHoldSeconds};

    // ---- E50. m_config above is the view of the current tier; m_declared is the configuration as given (mended), which
    // every tier rule reads. With no tiers the two are the same.
    DynamicResolutionConfig m_declared;
    float m_q{0.0f};                 // the current scene's quality floor, as a scale
    bool m_lowTier{false};
    // A probe on trial in the upper tier: for how long, and the size the lower tier had, to go back to.
    bool m_onTrial{false};
    double m_trialSeconds{0.0};
    float m_scaleLeft30{1.0f};
    // Seconds of windows that ask for a tier down, and for a probe.
    double m_tierDown{0.0};
    double m_tierUp{0.0};
    double m_tierGrace{0.0};
    bool m_graceRevert{false};       // a step was taken back during the grace: its hold is the transient's, not the scene's
    double m_tierHold{0.0};
    double m_nextTierHold{kFirstTierHoldSeconds};
    int m_probeFailures{0};
    TierChange m_tierChange{TierChange::None};
    double m_fastRiseLeft{0.0};      // a first visit's quicker rise, seconds left
    float m_minRiseStep{0.0f};       // the view's least rise: kLowTierMinRiseStep in the lower tier, 0 elsewhere (E47)
    int m_sceneSettleFrames{0};
    double m_sceneSettleSeconds{0.0};
    bool m_hasScene{false};
    std::string m_sceneKey;
    std::uint32_t m_sceneLines{0};
    bool m_lastSceneRemembered{false};
    std::map<std::string, SceneMemory, std::less<>> m_scenes;
    // The frames' waits for the GPU, beside m_samples; and the windows that ended, with the last one's length.
    std::vector<float> m_waits;
    std::uint64_t m_windowsEnded{0};
    double m_lastWindowSeconds{0.0};
};

} // namespace Supersonic
