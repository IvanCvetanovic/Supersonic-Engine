#pragma once

#include <glm/glm.hpp>

#include "core/UICanvas.hpp"

namespace Supersonic {

// Where the game is being shown, in the same screen coordinates the pointer is
// reported in.
//
// A game layer could not find this out. It gets a registry and a delta, and
// the rectangle the frame was drawn into was known only to the editor, which
// passed it to UISystem and UIInput and to nothing else. So a layer holding a
// camera it created itself still had no way to turn a pointer position into a
// point in its own world - the one conversion every click in every game needs.
//
// Wolf Brigade is the caller that made it concrete. Its gesture machine was
// ported, tested against the original's own harness, and then called by
// nothing: the header says in as many words that the CALLER converts screen to
// world because it "is the thing that knows which camera drew the frame", and
// there was no way for the caller to know where that frame was.
//
// In the registry's context beside PhysicsSettings and RenderSettings, and
// for a different reason from either: those are scene state and are saved,
// while this is a fact about the window that is republished every frame. It
// lives there because the context is the one place a layer can reach without
// the engine handing it another argument - and adding an argument to
// OnUpdate would put the burden on every layer that does not care.
//
// NOT THE WINDOW. In the editor the game is a panel with a menu bar above it
// and an inspector beside it, so the origin is not zero and the size is not
// the window's; in a packaged game it is the whole window and they coincide.
// A layer that assumed the second is wrong exactly while somebody is editing.
struct ViewportInfo {
    // Top-left and bottom-right, in the coordinates Input reports the pointer
    // in - which is window space in both modes.
    UIRect rect;

    // True while the pointer is over the game rather than over a panel, a menu
    // or another window. A game must not act on a click that landed on the
    // inspector, and it cannot tell from the position alone: a point inside
    // the viewport rectangle can still be under a floating tool window.
    bool pointerOverGame{false};

    glm::vec2 Size() const { return rect.size(); }

    // The pointer in VIEWPORT coordinates, which is what unprojecting wants.
    // Screen coordinates would be out by the panel's origin, and in the editor
    // that is most of the way across the screen.
    glm::vec2 ToLocal(const glm::vec2& screenPosition) const {
        return screenPosition - rect.min;
    }

    bool Contains(const glm::vec2& screenPosition) const {
        return UICanvas::Contains(rect, screenPosition);
    }
};

} // namespace Supersonic
