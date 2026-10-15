#pragma once

// RENDER SCALE CONTROL: what a game may ask of the scene target's size while it runs.
//
// DynamicResolution.hpp chooses the size by how fast the GPU draws; this is the player's say over that choice. A phone's
// options screen can put a row on it: leave the controller to decide (Automatic), or hold the picture at a share of the size
// the controller started at (Fixed) - sharper than the controller would keep on a slow GPU, or smaller than it would on a
// fast one - and the choice survives the controller's own judgement, which only ever sees the length of a frame.
//
// Published in the registry context as `RenderScaleControl*`, the way WindowControl* is and for the same reason: the one
// place a layer can reach without the engine handing it another argument.
//
//     if (auto* const* control = registry.ctx().find<RenderScaleControl*>(); control && (*control)->Available()) {
//         (*control)->RequestFixed(1.0f);   // the sharpest picture the controller would start at
//     }
//
// REQUESTS ARE DEFERRED, as WindowControl's are: they are latched here, in a class with no engine behind it (so the latching
// is testable without a GPU), and applied by SupersonicApp at the top of the next frame, before the controller sees that
// frame's time. The last request before then wins. Main thread only.
//
// AVAILABLE only when there is a controller to ask: the game turned DynamicResolution on, and this run is not a capture or a
// fixed-step run (whose pictures must reproduce) unless the game holds the picture at a fixed size for it (a developer flag).
// Otherwise a request is dropped, and a game that wants a row for it hides the row.

namespace Supersonic {

enum class RenderScaleMode : int {
    Automatic = 0,   // the controller's own choice, from the game's configuration
    Fixed = 1,       // a share of the size the controller started at, held
};

struct RenderScaleRequest {
    RenderScaleMode mode{RenderScaleMode::Automatic};
    float fraction{1.0f};   // Fixed only: the share of the starting size, in (0, 1]
};

class RenderScaleControl {
public:
    // Whether a controller exists to be asked, and the scale the scene target is at now (of the window's size). Both are what
    // the engine last published: this runs in the game's tick, which is before the frame's resize.
    bool Available() const { return m_available; }
    float CurrentScale() const { return m_currentScale; }

    // The mode the game last asked for (Automatic until it asks). It is the request, not the applied state: a menu that asks
    // and reads back in the same tick reads what it asked.
    RenderScaleMode Mode() const { return m_requested.mode; }
    float FixedFraction() const { return m_requested.fraction; }

    // The controller decides, from the game's configuration.
    void RequestAutomatic() {
        m_requested = RenderScaleRequest{RenderScaleMode::Automatic, 1.0f};
        m_pending = true;
    }
    // Hold the picture at `fraction` of the size the controller started at (the window's size, or the largest that fits the
    // game's pixel budget): 1 is that size, 0.5 a half-width, half-height picture. Held at no less than the game's floor
    // (a pixel floor, if it set one), and at no more than the window.
    void RequestFixed(float fraction) {
        if (!(fraction > 0.0f)) fraction = 0.1f;
        if (fraction > 1.0f) fraction = 1.0f;
        m_requested = RenderScaleRequest{RenderScaleMode::Fixed, fraction};
        m_pending = true;
    }

    // ---- The engine's side.

    // What the engine publishes after a frame's controller step.
    void Publish(bool available, float currentScale) {
        m_available = available;
        m_currentScale = currentScale;
        if (!available) m_pending = false;   // nothing to ask: a request that arrived is dropped, not kept for later
    }
    // The request latched since the last call, if any; taking it clears it.
    bool TakeRequest(RenderScaleRequest* out) {
        if (!m_pending) return false;
        m_pending = false;
        if (out != nullptr) *out = m_requested;
        return true;
    }

private:
    bool m_available{false};
    float m_currentScale{1.0f};
    bool m_pending{false};
    RenderScaleRequest m_requested;
};

} // namespace Supersonic
