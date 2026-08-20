#pragma once

namespace Supersonic {

// Scene-level rendering, in the registry's context beside PhysicsSettings.
//
// These live here rather than on the BloomPass that consumes them for two
// reasons, and the second is the one that matters.
//
// The obvious one: a value on a renderer object is lost when the scene is
// saved, so a look tuned in the editor survives until the next reload and no
// longer. Everything that belongs to a scene has to be reachable from the
// registry, because the registry is what gets written.
//
// The one that is easy to miss: undo. EditHistory captures a scene by
// serialising it, so anything not in that text is invisible to undo, to redo,
// and to the snapshot Play takes and Stop restores. A bloom threshold nudged
// during play and then reverted would silently keep the nudged value. Putting
// it in the context means all four get it for free and none of them had to
// learn about bloom.
struct RenderSettings {
    // Where a highlight starts to bloom. The scene is HDR at this point, so
    // 1.0 means "brighter than white" rather than "white".
    float bloomThreshold{1.0f};

    // How gradually it starts, so a surface drifting past the threshold fades
    // in instead of popping.
    float bloomSoftKnee{0.5f};

    // How much of the blurred image is added back.
    float bloomIntensity{0.55f};

    // Applied in the composite, before the tone map.
    float exposure{1.0f};

    // Distance fog. Density zero is no fog, which is the default and is why
    // there is no separate enable flag - the shader's falloff is exactly 1.0
    // at zero density, so "off" is the same arithmetic as "on" rather than a
    // branch that can disagree with it.
    //
    // Density rather than a start and end distance: one number instead of two,
    // no visible edge where a linear ramp would begin, and nothing to get
    // backwards. A density of about 0.02 puts the horizon at roughly fifty
    // units.
    float fogDensity{0.0f};
    float fogColor[3]{0.55f, 0.60f, 0.68f};
};

} // namespace Supersonic
