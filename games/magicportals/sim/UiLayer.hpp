#pragma once

// The two animation primitives every UI layer the original raises over a level
// is made of - ETHFramework's UISprite and UIButton - and the rule that places
// that layer's elements: ui.json's `ui_layer` block, and the arithmetic it
// means.
//
// WHY ITS OWN FILE. The in-level HUD (Hud.hpp) is anchored to corners and timed
// by the level's age. The layers over a level - the pause here, and the
// finished, lost and help screens that share these primitives - are placed by
// FRACTIONS OF THE SCREEN with a sprite origin, and animate on a UI clock the
// pause does not stop. Both halves are pure, so a suite pins them with no
// window, the way Hud.hpp's are.
//
// Units are design units and rectangles are on the VIEW, (0, 0) its top-left,
// +y down, exactly as Hud::Rect.

#include <string>

#include <glm/glm.hpp>

#include "sim/Hud.hpp"

namespace MagicPortals::UiLayer {

struct Rules {
    double spriteAppearMs = 0.0;   // UISprite: alpha smoothEnd(t / this)
    double buttonAppearMs = 0.0;   // UIButton: alpha t / this, position eased in
    double buttonDismissMs = 0.0;  // UIButton::dismiss: alpha 1 - smoothEnd, eased back out
    double buttonSlideUnits = 0.0; // how far out along the ray from the screen centre it starts
};

// ui.json's `ui_layer`, strictly: a missing or malformed number is refused.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// One element as the original's addSprite and addButton take it: a normalized
// position on the screen, the sprite's origin, and its size in units.
struct Placed {
    std::string sprite;
    glm::dvec2 atScreen{0.0}; // normalized: the anchor is view * this
    glm::dvec2 origin{0.0};   // where on the sprite the anchor lands, as a fraction of it
    glm::dvec2 sizeUnits{0.0};
};

// GetScreenSize() * atScreen, on a view `viewUnits` in size: the element's POS.
glm::dvec2 Anchor(const Placed& placed, const glm::dvec2& viewUnits);

// The drawn rectangle of an element whose anchor is at `anchor`: its top-left
// is the anchor less size * origin, as drawSprite sets the origin.
Hud::Rect RectAt(const Placed& placed, const glm::dvec2& anchor);

// A UISprite `ms` into its entrance, as the byte UISprite::update writes:
// fTOu(smoothEnd(min(ms / appear, 1)) * tintAlphaByte). Zero before it starts.
int SpriteAlphaByte(const Rules& rules, int tintAlphaByte, double ms);

// A UIButton `ms` into its entrance: fTOu(min(ms / appear, 1) * 255). LINEAR -
// UIButton::update takes the unfiltered bias for the alpha, and only the
// position is eased.
int ButtonAlphaByte(const Rules& rules, double ms);

// Where UIButton::UIButton starts a button whose anchor is `anchor`:
// anchor + normalize(anchor - viewUnits / 2) * slide. An anchor on the screen's
// centre has no ray and does not move.
glm::dvec2 ButtonStart(const Rules& rules, const glm::dvec2& anchor, const glm::dvec2& viewUnits);

// Where a button's anchor is `ms` into its entrance: from ButtonStart to
// `anchor`, eased by smoothEnd, and the anchor itself once the 700 ms are past.
glm::dvec2 ButtonAnchorAt(const Rules& rules, const glm::dvec2& anchor, const glm::dvec2& viewUnits, double ms);

// And `ms` after UIButton::dismiss: the alpha byte fTOu((1 - smoothEnd) * 255),
// zero once it is over, when the layer removes the button.
int ButtonDismissAlphaByte(const Rules& rules, double ms);

// Its anchor then: from `anchor` back out to ButtonStart, eased by smoothEnd.
glm::dvec2 ButtonDismissAnchorAt(const Rules& rules, const glm::dvec2& anchor, const glm::dvec2& viewUnits,
                                 double ms);

} // namespace MagicPortals::UiLayer
