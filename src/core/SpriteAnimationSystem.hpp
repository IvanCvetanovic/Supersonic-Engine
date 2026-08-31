#pragma once

#include <entt/entt.hpp>

#include "core/Components.hpp"

namespace Supersonic {

// Advances every sprite flipbook by one tick and writes where its cell is.
//
// Two halves, split on purpose. `Advance` is the clock - it moves the frame
// index on and is the only thing that touches tick state. `Apply` is the
// arithmetic - given a frame index and a grid, it says what the material's UV
// transform must be. Splitting them is what lets the second one be tested
// against a sheet with no registry, no clock and no renderer, which is where
// every off-by-one in a sprite sheet lives.
class SpriteAnimationSystem {
public:
    // One tick of the CLOCK. Writes the frame index and the accumulator, and
    // touches no material.
    //
    // `fixedDelta` is the tick's own step and never a frame delta. Passing a
    // frame delta here would make how fast a sprite animates depend on how fast
    // the display is keeping up, and would take the animation out of the
    // replay - which is the whole reason gameplay moved onto the tick.
    static void Update(entt::registry& registry, float fixedDelta);

    // Writes where each sprite currently IS, into its material. PER FRAME, in
    // both modes, and that is the point of it being separate.
    //
    // The tick loop is gated on play mode. Folded into Update, this ran only
    // while the game was running - so a sprite in the EDITOR drew its whole
    // sheet, with the inspector beside it cheerfully reporting "Frame 0 of 16".
    // A 2D scene could be authored and could not be seen, which is the exact
    // complaint the orthographic viewport was built to answer, reintroduced one
    // commit later inside the feature meant to be authored through it.
    //
    // Advancing is a clock and belongs on the tick. Showing a cell is not: it
    // is where the sprite is, the same argument the paused case already rested
    // on. So the clock is gated and this is not.
    static void Apply(entt::registry& registry);

    // Where one cell of a sheet sits, as the scale and offset a material wants.
    //
    // Static and pure so a test can reach it with no registry: the cell layout
    // is where a sprite sheet actually goes wrong, and it goes wrong quietly -
    // an off-by-one shows a sliver of the neighbouring frame rather than
    // anything that looks like an error.
    //
    // Cells run left to right then top to bottom. A frame past the end WRAPS
    // rather than reading outside the sheet, because the sheet is a texture and
    // sampling past its edge is somebody else's clamp mode deciding what your
    // animation looks like.
    static void CellTransform(uint32_t columns, uint32_t rows, uint32_t cell,
                              glm::vec2& outScale, glm::vec2& outOffset);

    // The frame a flipbook is on after `elapsed` seconds at this rate.
    //
    // Returns the new frame and leaves the remainder in `elapsed`. A while loop
    // rather than a division, matching the particle emitter's accumulator: a
    // tick long enough to cross several frames advances by several, and a rate
    // change takes effect from here rather than re-deriving the position from
    // total time.
    //
    // A non-looping animation stops ON its last frame rather than past it, and
    // reports that it has finished by leaving `playing` false - so an explosion
    // holds its last cell instead of snapping back to its first.
    static void Advance(SpriteAnimationComponent& sprite, float fixedDelta);
};

} // namespace Supersonic
