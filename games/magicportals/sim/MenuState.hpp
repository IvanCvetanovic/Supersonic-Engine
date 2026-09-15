#pragma once

// What every menu state of the original shares - ui.json's `menu_state` block
// and ETHFramework's Button effects - and the arithmetic they mean.
//
// WHY ITS OWN FILE. The loading screen, the main menu and the screens after it
// (chapter select, the level grid, credits, the dashboard) are each a STATE of
// the original, and every state opens under the same black (FadeInController)
// and presses its buttons the same way (Button::update). A button's idle bounce
// and blink are Button's too, whichever screen sets them. sim/UiLayer.hpp keeps
// the entrance those buttons make; this keeps the rest. Pure, so a suite pins it
// with no window.

#include <string>

#include <glm/glm.hpp>

#include "sim/UiLayer.hpp"

namespace MagicPortals::MenuState {

struct Rules {
    double fadeMs = 0.0;           // the state's black, 1 -> 0, linear
    int pressTintByte = 0;         // a held button's colour, a multiply of this / 255
    double tileTravelUnits = 0.0;  // a page tile refuses a touch that has moved further
};

// ui.json's `menu_state`, strictly: a missing or malformed number is refused.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// The state's black `ms` after its first frame, as drawBlackRect writes it:
// iTOb(fTOu((1 - ms / fade) * 255)), and nothing once the fade is over.
int FadeAlphaByte(const Rules& rules, double ms);

// Button::bounce and Button::blinkColor's bias: (t mod stride) / stride,
// reversed on odd strides - a triangle from 0 at t = 0.
double Triangle(double ms, double strideMs);

// Button::setBounce: the scale about the button's origin goes A -> B within a
// stride, eased by smoothEnd, and back the next.
struct Bounce {
    glm::dvec2 scaleA{1.0};
    glm::dvec2 scaleB{1.0};
    double strideMs = 0.0;
};
glm::dvec2 BounceScale(const Bounce& bounce, double ms);

// Button::setBlink and setBlinkAlpha: the colour and the alpha go A -> B LINEARLY
// on the same reversed bias.
struct Blink {
    double colourA = 1.0; // grey, the same in r, g and b
    double colourB = 1.0;
    double alphaA = 1.0;
    double alphaB = 1.0;
    double strideMs = 0.0;
};
struct BlinkValue {
    double colour = 1.0;
    double alpha = 1.0;
};
BlinkValue BlinkAt(const Blink& blink, double ms);

// Button::draw's arithmetic for one channel: the press tint, the custom colour's
// channel and the blink's, multiplied as FloatColors and truncated to a byte.
// `tintByte` is 255 unpressed and the rules' press tint while held.
int ChannelByte(int tintByte, int customByte, double blink);

// A rectangle scaled by `scale` about the point `origin` of it, as
// drawScaledSprite scales a sprite about its origin.
Hud::Rect ScaledAbout(const Hud::Rect& rect, const glm::dvec2& origin, const glm::dvec2& scale);

// ---- the press, as Button::update reads a touch ----------------------------
//
// What a menu keeps between ticks for the one touch it follows: where it went
// down, whether that was inside the button it went down on, and the furthest it
// has travelled since. Which button is the caller's to name.
struct Touch {
    bool down = false;
    glm::dvec2 downAt{0.0};
    double maxTravelUnits = 0.0;
};

// A touch pressed at `at`: its travel starts from there.
void TouchDown(Touch& touch, const glm::dvec2& at);
// A tick of it held at `at`: the largest distance from where it went down.
void TouchHeld(Touch& touch, const glm::dvec2& at);
// Whether a page tile still takes it (TouchGapDetector against scale(48)).
bool TileTakes(const Rules& rules, const Touch& touch);

} // namespace MagicPortals::MenuState
