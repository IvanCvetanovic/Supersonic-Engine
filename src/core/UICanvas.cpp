#include "core/UICanvas.hpp"

#include <algorithm>

namespace Supersonic {

namespace UICanvas {

namespace {

// Horizontal and vertical position of an anchor within its rect, 0 = start,
// 0.5 = centre, 1 = end. Derived from the enum's order rather than switched on,
// so adding a row or column cannot leave one case behind.
glm::vec2 anchorFraction(UIAnchor anchor) {
    const auto index = static_cast<uint8_t>(anchor);
    const float column = static_cast<float>(index % 3) * 0.5f;
    const float row = static_cast<float>(index / 3) * 0.5f;
    return { column, row };
}

} // namespace

float ScaleFor(const glm::vec2& screenSize) {
    if (screenSize.y <= 0.0f) return 1.0f;
    return screenSize.y / kReferenceHeight;
}

glm::vec2 MeasureStack(const std::vector<glm::vec2>& sizes, bool horizontal, float spacing) {
    glm::vec2 block(0.0f);
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (horizontal) {
            block.x += sizes[i].x + (i > 0 ? spacing : 0.0f);
            block.y = std::max(block.y, sizes[i].y);
        } else {
            block.y += sizes[i].y + (i > 0 ? spacing : 0.0f);
            block.x = std::max(block.x, sizes[i].x);
        }
    }
    return block;
}

std::vector<UIRect> LayoutStack(const std::vector<glm::vec2>& sizes, bool horizontal,
                                float spacing, UIAnchor anchor, const glm::vec2& offset,
                                const UIRect& screen) {
    return LayoutStack(sizes, horizontal, spacing, anchor, offset, screen,
                       ScaleFor(screen.size()));
}

UIRect Stretch(const UIRect& rect, const UIRect& screen, bool fillWidth, bool fillHeight) {
    UIRect out = rect;
    if (fillWidth) { out.min.x = screen.min.x; out.max.x = screen.max.x; }
    if (fillHeight) { out.min.y = screen.min.y; out.max.y = screen.max.y; }
    return out;
}

std::vector<UIRect> LayoutStack(const std::vector<glm::vec2>& sizes, bool horizontal,
                                float spacing, UIAnchor anchor, const glm::vec2& offset,
                                const UIRect& screen, float scale) {
    std::vector<UIRect> rects;
    if (sizes.empty()) return rects;

    const float gap = spacing * scale;

    // The block first, so it can be placed as one thing.
    //
    // Measuring the group and then anchoring it is what makes "centre" mean the
    // group is centred. Anchoring each child independently centres every child
    // on the same point, which stacks them all on top of one another and looks
    // like the layout silently did nothing.
    glm::vec2 block(0.0f);
    for (size_t i = 0; i < sizes.size(); ++i) {
        const glm::vec2 size = sizes[i] * scale;
        if (horizontal) {
            block.x += size.x + (i > 0 ? gap : 0.0f);
            block.y = std::max(block.y, size.y);
        } else {
            block.y += size.y + (i > 0 ? gap : 0.0f);
            block.x = std::max(block.x, size.x);
        }
    }

    // Placed through the same Place() every other element uses, in AUTHORED
    // units, so a stack sits where a single element of the same size would and
    // the two cannot drift apart.
    const UIRect placed = Place(anchor, offset, block / scale, screen);

    float cursor = horizontal ? placed.min.x : placed.min.y;
    rects.reserve(sizes.size());
    for (const glm::vec2& authored : sizes) {
        const glm::vec2 size = authored * scale;

        UIRect rect;
        if (horizontal) {
            // Centred on the cross axis, which is what a row of buttons of
            // different heights should look like.
            rect.min = glm::vec2(cursor, placed.min.y + (block.y - size.y) * 0.5f);
            cursor += size.x + gap;
        } else {
            rect.min = glm::vec2(placed.min.x + (block.x - size.x) * 0.5f, cursor);
            cursor += size.y + gap;
        }
        rect.max = rect.min + size;
        rects.push_back(rect);
    }

    return rects;
}

bool ProjectToScreen(const glm::mat4& viewProj, const glm::vec3& world, const UIRect& screen,
                     glm::vec3& outScreen) {
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);

    // Perspective puts everything behind the eye at w <= 0; orthographic keeps
    // w at 1 and expresses the same thing as a depth outside 0..1. Both are
    // checked, because the engine now has both projections and a label must not
    // depend on which one the camera happens to be using.
    if (clip.w <= 0.0f) return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f) return false;

    // NDC x and y are already in the convention the screen uses: the projection
    // negates its Y row for Vulkan, so +Y in clip space points DOWN, the same
    // way pixel rows count. No second flip here - doing it in both places is
    // how picking was mirrored about the horizontal centreline for a while.
    const glm::vec2 size = screen.size();
    outScreen.x = screen.min.x + (ndc.x * 0.5f + 0.5f) * size.x;
    outScreen.y = screen.min.y + (ndc.y * 0.5f + 0.5f) * size.y;
    outScreen.z = ndc.z;
    return true;
}

UIRect Place(UIAnchor anchor, const glm::vec2& offset, const glm::vec2& size,
             const UIRect& screen) {
    const glm::vec2 screenSize = screen.size();
    const glm::vec2 fraction = anchorFraction(anchor);

    // The anchor point on the screen, then back off by the same fraction of the
    // element's own size. A centred element is centred on its own middle; a
    // right-anchored one has its right edge on the right.
    const glm::vec2 anchorPoint = screen.min + screenSize * fraction;
    glm::vec2 topLeft = anchorPoint - size * fraction;

    // Inward, so the same offset means the same thing at every anchor. Applied
    // per axis, because an anchor can be centred on one axis and not the other:
    // TopCenter takes the vertical offset downward and ignores the horizontal
    // direction entirely, since there is no edge to come in from.
    topLeft.x += (fraction.x == 0.0f) ?  offset.x
               : (fraction.x == 1.0f) ? -offset.x
                                      :  offset.x;
    topLeft.y += (fraction.y == 0.0f) ?  offset.y
               : (fraction.y == 1.0f) ? -offset.y
                                      :  offset.y;

    return { topLeft, topLeft + size };
}

bool Contains(const UIRect& rect, const glm::vec2& point) {
    return point.x >= rect.min.x && point.x <= rect.max.x &&
           point.y >= rect.min.y && point.y <= rect.max.y;
}

UIRect FillHorizontal(const UIRect& rect, float fraction) {
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    return { rect.min, glm::vec2(rect.min.x + rect.size().x * clamped, rect.max.y) };
}


UIButtonState UpdateButton(const UIButtonState& previous, const UIRect& rect,
                           const UIPointer& pointer) {
    UIButtonState state;

    // Something else owns the pointer. Everything resets, including a press
    // that was in progress: a button left armed while a dialog is open would
    // fire the moment the dialog closed.
    if (!pointer.active) return state;

    state.hovered = Contains(rect, pointer.position);

    const bool pressEdge = pointer.down && !pointer.wasDown;

    if (previous.pressed && pointer.down) {
        // Still held. Deliberately not re-testing the rectangle: sliding off a
        // button and back on is how every real toolkit behaves, and a player
        // adjusting their aim mid-press should not lose the press.
        state.pressed = true;
    } else if (pressEdge && state.hovered) {
        state.pressed = true;
    }

    // The release has to happen over the same button the press started on.
    // Testing only "the pointer is up and over the button" would fire for a
    // press that began somewhere else entirely - which is how a player who
    // drags across a menu ends up activating whatever they let go over.
    state.clicked = previous.pressed && !pointer.down && state.hovered;

    return state;
}

namespace {

// A byte that continues a character rather than starting one. Every edit here
// steps over these, because a caret between two of them is inside a character
// and every splice made there produces a string that is not valid UTF-8 - which
// the font will not draw and the script ABI must not hand to a plugin.
bool isContinuation(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

int snapToLeadByte(const std::string& value, int index) {
    const int size = static_cast<int>(value.size());
    if (index <= 0) return 0;
    if (index >= size) return size;
    while (index > 0 && isContinuation(static_cast<unsigned char>(value[static_cast<size_t>(index)]))) {
        --index;
    }
    return index;
}

int nextCharacter(const std::string& value, int index) {
    const int size = static_cast<int>(value.size());
    if (index >= size) return size;
    ++index;
    while (index < size && isContinuation(static_cast<unsigned char>(value[static_cast<size_t>(index)]))) {
        ++index;
    }
    return index;
}

// How many CHARACTERS the string holds, which is what an author counts and is
// not the same as how many bytes it takes.
int characterCount(const std::string& value) {
    int count = 0;
    for (const char byte : value) {
        if (!isContinuation(static_cast<unsigned char>(byte))) ++count;
    }
    return count;
}

int previousCharacter(const std::string& value, int index) {
    if (index <= 0) return 0;
    --index;
    while (index > 0 && isContinuation(static_cast<unsigned char>(value[static_cast<size_t>(index)]))) {
        --index;
    }
    return index;
}

// UTF-8 for one codepoint, appended. Returns how many bytes it took, or 0 for
// something that is not a character worth having in a name: a control code, a
// surrogate half - which is not a codepoint at all, only half of an encoding
// GLFW does not use - or anything past the top of Unicode.
int encodeUtf8(unsigned int codepoint, char out[4]) {
    if (codepoint < 0x20u || codepoint == 0x7Fu) return 0;
    if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) return 0;
    if (codepoint > 0x10FFFFu) return 0;

    if (codepoint < 0x80u) {
        out[0] = static_cast<char>(codepoint);
        return 1;
    }
    if (codepoint < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (codepoint >> 6));
        out[1] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        return 2;
    }
    if (codepoint < 0x10000u) {
        out[0] = static_cast<char>(0xE0u | (codepoint >> 12));
        out[1] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        return 3;
    }
    out[0] = static_cast<char>(0xF0u | (codepoint >> 18));
    out[1] = static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
    return 4;
}

} // namespace

UITextEditState EditText(const UITextEditState& previous, std::string& value,
                         int maxLength, const UIKeyboard& keyboard) {
    UITextEditState state{};

    // submitted and cancelled are one-frame flags and are deliberately not
    // carried over, for the same reason a button's `clicked` is not: a submit
    // that stayed true would be acted on every frame until the next keystroke.
    state.caret = snapToLeadByte(value, previous.caret);

    // Something else owns the keyboard. The caret is still snapped above -
    // the value may have been changed by a script since - but nothing is typed
    // and nothing is submitted.
    if (!keyboard.active) return state;

    // Characters first, then the edit keys.
    //
    // Within one frame that ordering is arbitrary and it is the one place not
    // having a merged event queue shows: type 'x' and press Backspace inside
    // the same frame and the backspace erases what was before the 'x' and keeps
    // the 'x', rather than erasing it. Unreachable at typing speed and sixty
    // frames a second, reachable during a hitch, and cheap to live with next to
    // a second GLFW callback and a discriminated union.
    // Counted once and kept up to date, rather than walked per character: a
    // field is short and this loop is short, but recounting inside it makes the
    // cost quadratic in the length of a name for no reason.
    int length = maxLength > 0 ? characterCount(value) : 0;

    for (int i = 0; i < keyboard.characterCount && keyboard.characters; ++i) {
        char encoded[4];
        const int bytes = encodeUtf8(keyboard.characters[i], encoded);
        if (bytes == 0) continue;

        // Characters, because that is what the author counted. Whole ones -
        // there is no such thing as most of a character, and a value cut
        // through the middle of one is a string the font will not draw and the
        // script ABI must not hand to a plugin.
        if (maxLength > 0 && length >= maxLength) continue;

        value.insert(static_cast<size_t>(state.caret), encoded, static_cast<size_t>(bytes));
        state.caret += bytes;
        ++length;
    }

    if (keyboard.backspace && state.caret > 0) {
        const int from = previousCharacter(value, state.caret);
        value.erase(static_cast<size_t>(from), static_cast<size_t>(state.caret - from));
        state.caret = from;
    }

    if (keyboard.deleteForward && state.caret < static_cast<int>(value.size())) {
        const int to = nextCharacter(value, state.caret);
        value.erase(static_cast<size_t>(state.caret), static_cast<size_t>(to - state.caret));
    }

    // The caret moves by CHARACTERS, not bytes: an arrow key that stepped one
    // byte would land inside a two-byte letter and the next edit would split it.
    if (keyboard.caretLeft)  state.caret = previousCharacter(value, state.caret);
    if (keyboard.caretRight) state.caret = nextCharacter(value, state.caret);
    if (keyboard.caretHome)  state.caret = 0;
    if (keyboard.caretEnd)   state.caret = static_cast<int>(value.size());

    state.submitted = keyboard.submit;
    state.cancelled = keyboard.cancel;

    // A last clamp, because a script may have replaced the value underneath a
    // focused field between frames - setText is reachable from the plugin ABI -
    // and every index above was computed against the string as it was.
    state.caret = snapToLeadByte(value, state.caret);
    return state;
}

} // namespace UICanvas

} // namespace Supersonic
