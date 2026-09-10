#pragma once

// The portal traversal, as arithmetic.
//
// portal_system.gd's _teleport (230-263), reproduced in the remake's own space
// - pixels, +y down, radians clockwise on screen - so a Godot answer worked by
// hand can be checked against it directly. What is reproduced is STRUCTURE:
// the traveller keeps its velocity turned through the exit's rotation minus the
// entry's, scaled, and comes out along that velocity, clear of the exit. The
// NUMBERS the remake feeds it - the mode, the scale, the offset, the lockout -
// are every one marked _guess in its portals.json, so they arrive as data
// (Transit) and no test pins them.
//
// Godot does this in single precision; this does it in double, with the angle's
// sine and cosine from DetMath for the reason the transform uses it. The two
// agree to far better than a pixel, and the tests allow for the difference.

#include <string>

#include <glm/glm.hpp>

namespace MagicPortals::Portal {

struct Transit {
    // "reset" zeroes the velocity and "rotate_to_exit" turns it. Anything else
    // keeps it as it was - including "clamp", which portals.json offers as an
    // option and the remake's match statement has no arm for.
    std::string momentumMode;
    double exitSpeedScale = 0.0;
    double exitOffsetPx = 0.0;
    double reentryLockoutS = 0.0;
};

// Reads the "transit" object of the remake's portals.json. Every field must be
// there: a missing one is an error, not a default, because the defaults would
// be guesses written into code.
bool LoadTransit(const std::string& path, Transit& out, std::string& error);

// portal_system.gd:237-245.
glm::dvec2 ExitVelocity(const glm::dvec2& velocity, double entryRotation, double exitRotation,
                        const Transit& transit);

// portal_system.gd:249-252: out along the exit velocity, or straight down (+y,
// the remake's down) for a traveller that arrives still.
glm::dvec2 ExitPosition(const glm::dvec2& exitPortal, const glm::dvec2& exitVelocity,
                        const Transit& transit);

} // namespace MagicPortals::Portal
