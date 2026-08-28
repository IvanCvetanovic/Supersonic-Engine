#pragma once

#include <entt/entt.hpp>

namespace Supersonic {

// Draws the world between two ticks instead of on them.
//
// A simulation that thinks twenty times a second and is drawn a hundred and
// forty-four times a second draws the same twenty positions seven times each,
// and it looks exactly like that: motion in steps. The fix is not to think more
// often - that is the cost the tick rate was chosen to avoid - it is to draw
// each frame somewhere between the last two ticks, using the fraction of a tick
// the frame arrived at.
//
// WHY THE AUTHORITATIVE VALUE LIVES HERE and not in TransformComponent. The
// obvious arrangement is to interpolate TransformComponent in place, and it
// destroys the simulation: the next tick would read the drawn position rather
// than the simulated one, so the world's state would depend on the frame rate -
// which is the entire thing the fixed tick exists to prevent, reintroduced by
// the feature meant to hide it.
//
// So TransformComponent holds the interpolated value only while a frame is
// being drawn, and this component holds the two ticks it sits between. Every
// tick begins by putting the authoritative value back.
//
// OPT-IN, by adding the component. An entity without it draws at its simulated
// position exactly as before, which is the right default for scenery, for a UI
// element, and for anything a script moves per frame rather than per tick.
class InterpolationSystem {
public:
    // Start of a tick, BEFORE anything moves. Restores the authoritative
    // transform over whatever the last frame drew, then records it as the
    // tick's starting point.
    //
    // Called inside the tick loop rather than once per frame, and that is the
    // difference between working and appearing to. A frame that runs no ticks
    // would, on the per-frame version, copy the current state onto the previous
    // one - so the lerp below would run between two identical values and the
    // result would be a still image at every frame rate, which reads as
    // interpolation working perfectly.
    static void BeginTick(entt::registry& registry);

    // End of a tick. What the tick produced becomes the value to interpolate
    // towards.
    static void EndTick(entt::registry& registry);

    // Between ticks: write the drawn transform, `alpha` of the way from the
    // previous tick to the current one.
    //
    // Called once per frame after the tick loop, with SimulationClock::alpha.
    static void Apply(entt::registry& registry, float alpha);
};

} // namespace Supersonic
