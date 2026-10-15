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

#include <cstddef>
#include <cstdint>
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
    // ceiling is the window's own size. A floor equal to the ceiling is a FIXED scale: no controller, only a size.
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
};

class DynamicResolution {
public:
    explicit DynamicResolution(const DynamicResolutionConfig& config);

    // One frame's wall time in seconds. True when the scale changed (the caller resizes its target). A frame that is not a
    // measurement - zero or negative, or longer than kIgnoreAboveSeconds (a scene load, a long hitch) - counts for nothing.
    bool Observe(float frameSeconds);

    // Takes a new configuration while running (a graphics-quality pick): mended like the constructor's, the judgement and
    // the hold forgotten, the current scale kept when it still fits the new range and moved into it when it does not. The
    // caller compares Scale() before and after to know whether to resize.
    void Reconfigure(const DynamicResolutionConfig& config);

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

private:
    void Mend();
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
};

} // namespace Supersonic
