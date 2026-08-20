#pragma once

#include <glm/glm.hpp>

namespace Supersonic {

// World-level physics, stored in the registry's context rather than on an
// entity.
//
// There is exactly one of these per scene and no entity owns it, which is what
// the context is for. `PhysicsSystem::Update` reads it if it is there and falls
// back to these defaults if it is not, so nothing has to install it and no
// existing caller changes.
struct PhysicsSettings {
    // Metres per second squared, in world space. It was a file-static constant,
    // which meant a scene set on the moon, underwater, or in a corridor with
    // gravity pointing sideways was not a tuning problem but an edit to the
    // engine.
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};

    // An unconditional solid plane, applied during integration to every
    // non-kinematic body, whether or not the scene contains a floor.
    //
    // Off by default, and that is the change. It used to exist always and
    // there was no switch: nothing could fall below y = 0, a pit was not
    // possible, a level built below the origin was unreachable, and a body
    // resting on the plane rested on something with the body's own restitution
    // and NO friction at all - so it kept its horizontal speed forever unless
    // damping took it. It also did all of that silently, because there is no
    // entity to select and nothing to see.
    //
    // What it is still good for is a prototype scene with no floor built yet,
    // and a safety net under a level that has holes in it. Both are reasons to
    // ask for it, not reasons for it to be there uninvited.
    bool hasGroundPlane{false};

    // Where that plane sits, when it is on. Measured against the BOTTOM of a
    // body's collider bounds in world space, not its transform origin.
    float groundPlaneY{0.0f};
};

} // namespace Supersonic
