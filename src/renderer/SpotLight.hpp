#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Supersonic {

// Spot lights: a cone of light with a shadow.
//
// The engine had two kinds of light, neither of which can be aimed. A torch, a
// street lamp, a spotlight over a stage, headlights - all of them are a point
// light that only shines within a cone, and none of them could be expressed.
//
// The cone falloff is the part worth keeping away from the renderer. It is a
// handful of dot products, and every way of getting it wrong produces
// something that still looks like a spotlight: a hard edge that crawls with
// the camera, a cone that inverts when the angles are authored the wrong way
// round, or one that goes black at the centre.
namespace SpotLight {

// How many spot lights can cast a shadow at once. Fixed and allocated up
// front, for the same reason the point lights are: a light created during play
// must not drag image allocation onto the frame path.
inline constexpr uint32_t kMaxShadowCasters = 2;

// Near plane for a spot's shadow frustum.
inline constexpr float kNearPlane = 0.05f;

// The transform a spot light's shadow map is rendered with, and that the
// shader projects a fragment through. One frustum, not six: a cone only ever
// looks one way.
glm::mat4 BuildViewProj(const glm::vec3& position, const glm::vec3& direction,
                        float outerAngleRadians, float range);

// How much light reaches a point, from the cone alone: 1 inside the inner
// angle, falling to 0 at the outer one, and 0 beyond.
//
// `toFragment` runs from the light toward the surface, the same direction the
// light itself points, so a fragment straight ahead scores 1.
float ConeAttenuation(const glm::vec3& spotDirection, const glm::vec3& toFragment,
                      float innerAngleRadians, float outerAngleRadians);

} // namespace SpotLight

} // namespace Supersonic
