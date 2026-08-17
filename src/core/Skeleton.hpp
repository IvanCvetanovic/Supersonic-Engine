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

    // Largest distance from a bind-pose vertex to its dominant joint's origin.
    // Used to inflate the pose bounds so an animated mesh is not culled by the
    // bind-pose AABB it has moved out of.
    float skinRadius{0.0f};

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
