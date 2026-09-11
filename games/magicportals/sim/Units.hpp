#pragma once

// The remake's pixels, and the engine's metres.
//
// 50 px to the metre, which is the original's own scale: Ethanon hands Box2D
// every position divided by DEFAULT_SCALE = 50 (the remake's
// docs/ethanon-formats.md:856, citing ETHPhysicsSimulator.cpp:27). The engine's
// solver is tuned in metres, so at this scale its constants land where the
// original's Box2D did.
//
// And y flips. Godot's 2D and the original's Box2D both point +y DOWN; the
// engine is y-up. Rotation flips with it: a positive Godot rotation turns
// clockwise on screen, which is negative about the engine's +z.

#include <glm/glm.hpp>

namespace MagicPortals::Units {

inline constexpr double kPixelsPerMetre = 50.0;

// Box2D's DEFAULT_GRAVITY, (0, 10) in its units per second squared with +y down
// (ETHPhysicsSimulator.cpp:26, same citation). Decoded, not guessed - unlike the
// remake's 1200 px/s^2 player gravity, which its own player.json marks _guess.
inline constexpr double kGravity = 10.0;

// The remake's world gravity, which is what its bodies fall at: Godot 4's
// physics/2d/default_gravity, since the remake's project.godot sets none. The
// port plays like the remake, so its world runs at this; the spike measured the
// solver against the original, and runs at kGravity. The remake's player falls
// at its own player.json gravity, not at either.
inline constexpr double kRemakeWorldGravityPx = 980.0;

inline float ToMetres(double px) {
    return static_cast<float>(px / kPixelsPerMetre);
}

// A point in the remake's space (px, +y down) as a point in the engine's plane.
inline glm::vec3 ToWorld(double xPx, double yPx) {
    return glm::vec3(ToMetres(xPx), ToMetres(-yPx), 0.0f);
}

inline glm::dvec2 ToPixels(const glm::vec3& world) {
    return glm::dvec2(world.x * kPixelsPerMetre, -world.y * kPixelsPerMetre);
}

inline float ToWorldRotation(double godotRadians) {
    return static_cast<float>(-godotRadians);
}

} // namespace MagicPortals::Units
