#pragma once

#include <string>

namespace Supersonic {

// What the scene says its surroundings look like.
//
// Scene state that no entity owns, exactly like PhysicsSettings, and stored the
// same way: in the registry's context, written next to the entities rather than
// inside one of them. An environment belongs to the whole scene - there is one
// sky - so making it a component would mean deciding what two of them meant.
struct EnvironmentSettings {
    // A Radiance .hdr. Empty is the analytic two-colour hemisphere, which is
    // what every scene written before this had and what they go on rendering
    // as, unchanged.
    std::string hdriPath;

    // A plain multiplier on the loaded radiance.
    //
    // An HDRI is captured at whatever exposure the photographer used, and there
    // is no convention that makes one file's "1.0" mean the same as another's -
    // so the only honest control is a number the author turns until it looks
    // right.
    float intensity{1.0f};
};

} // namespace Supersonic
