#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Supersonic {

// Where an element attaches to the screen.
//
// A HUD authored against one window size has to survive every other one, and
// the only way that works is for each element to say which edge or corner it
// belongs to. An offset alone puts the score in the top-left at 1280x720 and
// somewhere in the middle of a 4K screen.
enum class UIAnchor : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    Center,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// A rectangle in screen pixels: min is the top-left corner.
struct UIRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    glm::vec2 size() const { return max - min; }
};

// Screen-space layout for HUD elements. No renderer, no ImGui, no Vulkan - the
// arithmetic that decides where things land is the part that is wrong in subtle
// ways at resolutions nobody tested on, so it lives on its own and is tested on
// its own.
namespace UICanvas {

// Height the HUD is authored against. An element 40 pixels tall stays 40
// pixels tall at 1080p, and grows or shrinks proportionally elsewhere, so a
// health bar is the same fraction of the screen on a laptop and a 4K monitor.
inline constexpr float kReferenceHeight = 1080.0f;

// Multiplier taking authored units to pixels on a screen of this size.
float ScaleFor(const glm::vec2& screenSize);

// Places an element of `size` authored units at `offset` from its anchor.
//
// The offset always runs *inward* from the anchored edge, so the same offset of
// (16, 16) means "16 in from the corner" whichever corner is chosen, rather
// than pushing a bottom-right element off the screen.
UIRect Place(UIAnchor anchor, const glm::vec2& offset, const glm::vec2& size,
             const UIRect& screen);

// Same, for something whose size is only known after measuring - a run of text.
// Identical rules; separate name because the caller has to measure first.
inline UIRect PlaceMeasured(UIAnchor anchor, const glm::vec2& offset,
                            const glm::vec2& measuredSize, const UIRect& screen) {
    return Place(anchor, offset, measuredSize, screen);
}

// True when a point falls inside the rectangle, edges included.
bool Contains(const UIRect& rect, const glm::vec2& point);

// Left portion of a rect, for a bar that fills from 0 to 1. Clamped, because a
// health value can go negative in the same frame the death handler runs.
UIRect FillHorizontal(const UIRect& rect, float fraction);


// ---------------------------------------------------------------------------
// Interaction
//
// The HUD could be drawn but not touched: no hit testing existed anywhere, so
// a shipped game could not have a main menu, a pause screen or a single
// button. What follows is the part that decides whether a press counts as a
// click, which is where the mistakes are - and none of them are visible in a
// screenshot, so it lives here rather than in the renderer.
// ---------------------------------------------------------------------------

// The pointer for one frame.
struct UIPointer {
    glm::vec2 position{0.0f};

    bool down{false};

    // Last frame, so a press EDGE can be told from a button already being held.
    // Without it, dragging a held pointer onto a button would press it, and a
    // player dragging across a menu would trigger everything they crossed.
    bool wasDown{false};

    // False when something else owns the pointer - an editor panel over the
    // viewport, or a game that has released the mouse. Interaction stops
    // entirely rather than the element merely not being hovered.
    bool active{true};
};

// What a button carries from one frame to the next.
struct UIButtonState {
    bool hovered{false};

    // Held, having been pressed on this button. Stays true while the pointer
    // is dragged off it, which is what lets a player slide off a button they
    // did not mean to press and back on again.
    bool pressed{false};

    // Released over this button, true for exactly one frame.
    bool clicked{false};
};

// Advances one button. `previous` is that button's state last frame.
UIButtonState UpdateButton(const UIButtonState& previous, const UIRect& rect,
                           const UIPointer& pointer);

// ---------------------------------------------------------------------------
// Typing
//
// The other half of "the HUD could be drawn but not touched": it could be read
// but not written to. A game could show a score and could not ask for a name.
// ---------------------------------------------------------------------------

// The keyboard for one frame, assembled by the caller.
//
// Characters and edit keys arrive from different places on purpose. A character
// is what the OS decided the keystroke MEANS - after shift, after the layout,
// after a dead key - and only a character callback knows it. Backspace is not a
// character and has to repeat while held, and the only thing here that knows
// the OS repeat delay is ImGui. Merging them into one ordered event queue was
// the first design and it bought exactly that ordering, at the price of a
// second GLFW callback and a discriminated union; see EditText for the one case
// where not having it shows.
struct UIKeyboard {
    // Not owned, and valid only for this frame.
    const unsigned int* characters{nullptr};
    int characterCount{0};

    bool backspace{false};
    bool deleteForward{false};
    bool caretLeft{false};
    bool caretRight{false};
    bool caretHome{false};
    bool caretEnd{false};

    bool submit{false};   // Enter
    bool cancel{false};   // Escape

    // False when something else owns the keyboard - an ImGui text box, or an
    // editor that is not playing. Nothing is typed and nothing takes focus, but
    // focus already held is KEPT: moving the mouse off the viewport must not
    // lose half a typed name.
    bool active{true};
};

// Where the caret is, and what the field was asked to do this frame.
struct UITextEditState {
    // A BYTE index into the value, always on a UTF-8 lead byte. Bytes rather
    // than characters because every edit is a splice into a std::string, and a
    // character index would have to be converted at every one of them.
    int caret{0};

    // True for exactly one frame, like a button's `clicked`.
    bool submitted{false};
    bool cancelled{false};
};

// Applies one frame of typing to `value`, in place.
//
// maxLength counts CHARACTERS, not bytes, and zero or less means no limit. A
// field authored to hold 24 that took 24 plain letters but only 12 accented
// ones would be a bug report rather than a design - the author counted what
// they can see. Bytes matter in exactly one other place, the fixed buffer the
// script ABI copies into, and conflating the two limits is the mistake.
UITextEditState EditText(const UITextEditState& previous, std::string& value,
                         int maxLength, const UIKeyboard& keyboard);

} // namespace UICanvas

} // namespace Supersonic
