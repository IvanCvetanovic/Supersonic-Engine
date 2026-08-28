#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Supersonic {

// CPU-side rig and animation data.
//
// Deliberately free of Vulkan and of EnTT: this is where the glTF conventions
// and the interpolation maths live, which is exactly the code that most needs a
// test, and a test target here links no Vulkan library at all.

enum class AnimInterpolation : uint8_t {
    Linear,
    Step,
    CubicSpline,
};

enum class AnimPath : uint8_t {
    Translation,
    Rotation,
    Scale,
};

struct Joint {
    std::string name;

    // Index into Skeleton::joints, or -1 for a root. Joints are stored
    // parent-before-child so a pose is one forward pass with no recursion.
    int32_t parent{-1};

    // Bind-pose inverse, straight from the glTF skin.
    glm::mat4 inverseBind{1.0f};

    // Rest transform, used for any channel an animation does not drive.
    glm::vec3 restTranslation{0.0f};
    glm::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 restScale{1.0f};

    // Composed transform of any non-joint ancestors above a root joint. glTF
    // does not require a joint's parent node to itself be a joint, so without
    // this a rig hanging off a transformed node poses in the wrong place.
    glm::mat4 preTransform{1.0f};
};

struct Skeleton {
    std::vector<Joint> joints;

    // There was a `skinRadius` here, and it was never written or read.
    //
    // It described a first design for keeping an animated mesh from being
    // culled against the bind-pose box it has walked out of: a radius per
    // skeleton, unioned into the pose bounds as a sphere. What actually shipped,
    // in the same commit, is stricter and lives in AnimationSystem::EvaluatePoses
    // - the bind box carried by EVERY joint matrix and unioned in. So the field
    // was dead on arrival, and the documentation that grew up around it went on
    // describing a bug that the code beside it had already prevented.
    //
    // Removed rather than implemented, because implementing it would have made
    // the bounds WORSE. A sphere on a joint origin is isotropic and the origin
    // sits at the end of a bone, so its radius is roughly a bone length in all
    // six directions: measured on this repo's own rig, the radius version bounds
    // 6.9x the volume of what ships.
    //
    // The real defect underneath is the opposite one and is not this field:
    // carrying the WHOLE bind box on every joint is 1.5x to 2.5x looser than
    // necessary, which costs draw calls and makes the editor's AABB pick claim
    // space beside a character. A per-joint bind-space box would fix that, and
    // is an optimisation no game here has asked for yet.

    bool empty() const { return joints.empty(); }
};

// One animated property of one joint over time.
struct AnimChannel {
    int32_t joint{-1};
    AnimPath path{AnimPath::Translation};
    AnimInterpolation interpolation{AnimInterpolation::Linear};

    std::vector<float> times;

    // Linear and Step store one value per key. CubicSpline stores three -
    // in-tangent, value, out-tangent - so this is 3x the key count.
    std::vector<glm::vec4> values;
};

struct AnimationClip {
    std::string name;
    float duration{0.0f};
    std::vector<AnimChannel> channels;
};

} // namespace Supersonic
