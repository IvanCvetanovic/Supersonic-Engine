#pragma once

#include <array>
#include <cstdint>

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

    // ---- What is behind everything ---------------------------------------
    //
    // A game could not choose this. The sky pass was drawn whenever a sky
    // pipeline existed, which is always, and the colour behind it was the
    // literal 0.00023 the offscreen target happened to be cleared to - so
    // every scene in every genre got a procedural horizon gradient, and the
    // only way to not have one was to cover the screen in geometry.
    //
    // For a 2D game that is most of the frame. A side-on lane, a platformer,
    // a board: the background is the majority of every pixel and it is the
    // first thing an art director picks. Wolf Brigade authors #161a22 and
    // renders on near-black because the engine had nowhere to put the number.
    enum class Background {
        // The procedural gradient, or the environment map when one is loaded.
        // The default, because it is what every existing scene has.
        Sky,

        // A flat authored colour, and the sky pass is not recorded at all -
        // so this is one draw call cheaper rather than one draw call painted
        // over. That is the difference between an option and a setting: a sky
        // hidden behind a colour would still shade every pixel nothing else
        // claimed.
        Color,
    };

    Background background{Background::Sky};

    // Linear, and written into a floating-point target like everything else
    // here - bloom_composite.frag tone-maps and encodes once at the end. An
    // authored sRGB colour therefore has to be de-encoded before it lands
    // here, which the inspector does, because a colour picked as #161a22 and
    // written straight into a linear buffer comes out visibly lighter.
    //
    // Under SceneEncoding::DisplayEncoded (below) the target holds display
    // values, so there this IS the colour on screen and nothing de-encodes it.
    float backgroundColor[3]{0.086f, 0.102f, 0.133f};

    // Whether the sky pass runs at all this frame. One place to ask, so the
    // pass that records it and the clear that would otherwise be covered by
    // it cannot come to disagree about which is responsible for the pixel.
    bool drawsSky() const { return background == Background::Sky; }

    // ---- What the scene target's numbers mean ----------------------------
    //
    // Everything above assumes the scene target holds linear radiance, and for
    // a lit 3D scene it must. A 2D game ported from a GLES2-era engine was
    // never lit that way: its artists tinted, faded and added glows on the
    // encoded bytes of an 8-bit framebuffer, and the only arithmetic that
    // reproduces what they saw is that same arithmetic. Through the linear
    // chain a white texel stops at 186 of 255 (Reinhard leaves linear 1.0 at
    // 0.5), and a colour of (1, 0.1, 0.1) multiplied in linear light and then
    // encoded lifts the 0.1 to 0.35. No tint chosen before the chain undoes
    // either, so the chain itself has to be able to say what its numbers are.
    //
    //   LinearHdr        linear radiance: bloom, Reinhard, then the sRGB
    //                    encode. Today's chain and the default.
    //   LinearNoToneMap  linear radiance: bloom, clamp, then the sRGB encode.
    //   DisplayEncoded   the numbers ARE display values. Colour textures are
    //                    sampled without an sRGB decode, so every multiply and
    //                    every blend happens on encoded values; the bright and
    //                    blur passes are not recorded, and the composite only
    //                    clamps.
    //
    // Declared in this order on purpose: the composite reads the mode as the
    // enum's value (0, 1, 2), so a mode added anywhere but the end renumbers
    // the shader's branches.
    enum class SceneEncoding : uint8_t { LinearHdr, LinearNoToneMap, DisplayEncoded };
    SceneEncoding encoding{SceneEncoding::LinearHdr};

    // Applied last, after the encoding. Rgb565 reproduces a 16-bit
    // framebuffer without dithering: every channel lands on one of the 32 or
    // 64 levels such a target can store. The screen overlay is drawn after it
    // and is not quantised.
    enum class OutputQuantize : uint8_t { None, Rgb565 };
    OutputQuantize quantize{OutputQuantize::None};

    // Whether a colour texture is decoded from sRGB when it is sampled. One
    // place to ask, for the same reason as drawsSky: the resolve that picks the
    // upload and the signature that decides when to re-resolve must agree.
    bool decodesColourTextures() const { return encoding != SceneEncoding::DisplayEncoded; }

    // What the scene target is cleared to, before the sky or anything else.
    //
    // A scene with a sky keeps the literal the chain was built around: 0.00023
    // is the linear radiance that Reinhard and the encode bring back to 0.02 on
    // screen, and the sky covers it anyway. A flat colour is written verbatim.
    // In DisplayEncoded the target holds display values, so the linear literal
    // means nothing there and the authored colour is the clear in either case.
    // Null is a scene that never said, and gets what every scene has always had.
    static std::array<float, 3> SceneClearColor(const RenderSettings* settings) {
        if (settings == nullptr ||
            (settings->drawsSky() && settings->encoding != SceneEncoding::DisplayEncoded)) {
            return {0.00023f, 0.00023f, 0.00023f};
        }
        return {settings->backgroundColor[0], settings->backgroundColor[1], settings->backgroundColor[2]};
    }
};

} // namespace Supersonic
