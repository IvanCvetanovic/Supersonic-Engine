#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace Supersonic {

// Omnidirectional shadows for point lights.
//
// Only the first directional light cast a shadow, so a room lit entirely by
// lamps had none at all - every point light shone straight through walls, and
// an object resting on the floor had nothing tying it there.
//
// A point light needs to see in every direction, so the scene is rendered six
// times per light into the faces of a cube map. The shader then samples it by
// the direction from the light to the fragment, which is what makes the lookup
// one texture fetch rather than six comparisons.
//
// Deliberately free of Vulkan: the face orientations and the depth convention
// are where the mistakes live - a face flipped or an up vector chosen wrongly
// produces shadows that look almost right - and keeping this to glm is what
// lets a test link it.
namespace PointShadow {

inline constexpr uint32_t kFaceCount = 6;

// How many point lights can cast at once. Fixed and allocated up front: growing
// this per light would put image lifetime on the same code path as a window
// resize, and a light created mid-play is far harder to stress than a resize.
inline constexpr uint32_t kMaxShadowCasters = 2;

// Near plane for every face. Small enough not to clip geometry hugging the
// light, large enough that the depth range stays usable.
inline constexpr float kNearPlane = 0.05f;

// The six view-projection matrices, in the order the Vulkan cube map expects:
// +X, -X, +Y, -Y, +Z, -Z.
std::array<glm::mat4, kFaceCount> BuildFaceViewProj(const glm::vec3& lightPosition,
                                                    float farPlane);

// Which face a direction from the light falls on, by major axis. Matches how a
// cube sampler chooses, so a test can check one face's matrix against the
// direction that will actually be looked up through it.
uint32_t FaceForDirection(const glm::vec3& direction);

// The depth a fragment at `direction` from the light would have been written
// with, in the same [0, 1] range the face's projection produces.
//
// This is what makes the lookup cheap: the shader compares against this rather
// than needing the six matrices in a uniform, because a perspective projection
// with a 90-degree field of view depends only on the distance along the major
// axis.
float DepthForDirection(const glm::vec3& direction, float farPlane);

} // namespace PointShadow

} // namespace Supersonic
