#include "renderer/PointShadow.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace PointShadow {

namespace {

// Direction each cube face looks along, and its up vector, in the order Vulkan
// stores cube faces. These are not free choices: a cube sampler decides which
// face a direction lands on and where in that face, so a rendered face whose
// orientation disagrees with the sampler's puts the shadow somewhere else
// entirely - which reads as shadow acne or as a shadow sliding across a wall,
// not as an obviously wrong image.
//
// The up vectors are negated relative to the OpenGL convention because Vulkan's
// clip space has +Y downward.
struct Face {
    glm::vec3 forward;
    glm::vec3 up;
};

constexpr std::array<Face, kFaceCount> kFaces = {{
    { {  1.0f,  0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f } },  // +X
    { { -1.0f,  0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f } },  // -X
    { {  0.0f,  1.0f,  0.0f }, { 0.0f,  0.0f,  1.0f } },  // +Y
    { {  0.0f, -1.0f,  0.0f }, { 0.0f,  0.0f, -1.0f } },  // -Y
    { {  0.0f,  0.0f,  1.0f }, { 0.0f, -1.0f,  0.0f } },  // +Z
    { {  0.0f,  0.0f, -1.0f }, { 0.0f, -1.0f,  0.0f } },  // -Z
}};

float safeFar(float farPlane) {
    // A light with a zero or negative range would otherwise divide by zero in
    // the projection and fill the whole map with NaN.
    return std::max(farPlane, kNearPlane * 2.0f);
}

} // namespace

std::array<glm::mat4, kFaceCount> BuildFaceViewProj(const glm::vec3& lightPosition,
                                                    float farPlane) {
    const float far = safeFar(farPlane);

    // 90 degrees exactly, so the six frusta tile the sphere with no gap and no
    // overlap. Anything else leaves seams the sampler cannot hide.
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, kNearPlane, far);

    // GLM builds the OpenGL convention, where clip-space Y points up. Vulkan's
    // points down, and the usual fix - negating proj[1][1] - would also mirror
    // the face, putting the shadow on the wrong side of every object. Flipping
    // the up vector instead (above) keeps the handedness that the cube sampler
    // expects.
    std::array<glm::mat4, kFaceCount> result{};
    for (uint32_t face = 0; face < kFaceCount; ++face) {
        const glm::mat4 view = glm::lookAt(lightPosition,
                                           lightPosition + kFaces[face].forward,
                                           kFaces[face].up);
        result[face] = projection * view;
    }
    return result;
}

uint32_t FaceForDirection(const glm::vec3& direction) {
    const glm::vec3 magnitude = glm::abs(direction);

    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        return direction.x >= 0.0f ? 0u : 1u;
    }
    if (magnitude.y >= magnitude.z) {
        return direction.y >= 0.0f ? 2u : 3u;
    }
    return direction.z >= 0.0f ? 4u : 5u;
}

float DepthForDirection(const glm::vec3& direction, float farPlane) {
    const float far = safeFar(farPlane);

    // Distance along the major axis is the view-space depth for whichever face
    // this direction lands on: the face looks straight down that axis, and its
    // 90-degree frustum means the other two components only move the fragment
    // within the face, never along its depth.
    const glm::vec3 magnitude = glm::abs(direction);
    const float major = std::max(magnitude.x, std::max(magnitude.y, magnitude.z));
    if (major <= 0.0f) return 0.0f;

    // The same value glm::perspective writes, for a view-space depth of
    // -major: with GLM_FORCE_DEPTH_ZERO_TO_ONE the mapping is
    // z_ndc = far / (near - far) * (-z_view) + (near * far) / (near - far),
    // divided by w = major.
    const float a = far / (kNearPlane - far);
    const float b = (kNearPlane * far) / (kNearPlane - far);
    return (-a * major + b) / major;
}

} // namespace PointShadow

} // namespace Supersonic
