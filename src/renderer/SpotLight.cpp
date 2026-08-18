#include "renderer/SpotLight.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace SpotLight {

namespace {

// A vector perpendicular to `direction`, for building the view matrix.
//
// glm::lookAt degenerates when the up vector is parallel to the view
// direction, which is exactly the case for a lamp pointing straight down - the
// single most common way anyone places a spot light.
glm::vec3 upFor(const glm::vec3& direction) {
    return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                         : glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 safeDirection(const glm::vec3& direction) {
    const float length = glm::length(direction);
    // A light authored with no direction at all points down, which is the
    // least surprising choice for a lamp and, more importantly, is a direction
    // rather than a NaN.
    return length > 1e-6f ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
}

} // namespace

glm::mat4 BuildViewProj(const glm::vec3& position, const glm::vec3& direction,
                        float outerAngleRadians, float range) {
    const glm::vec3 forward = safeDirection(direction);
    const float far = std::max(range, kNearPlane * 2.0f);

    // The frustum has to cover the cone, so its field of view is the FULL
    // angle - twice the half-angle from the axis to the edge. Using the half
    // angle would light a cone the shadow map does not cover, and everything
    // outside it would be lit through walls.
    //
    // Widened slightly so the cone's edge is inside the map rather than
    // exactly on its border, where filtering samples past the edge.
    const float fov = std::clamp(outerAngleRadians * 2.0f * 1.05f,
                                 glm::radians(1.0f), glm::radians(179.0f));

    glm::mat4 projection = glm::perspective(fov, 1.0f, kNearPlane, far);

    // GLM builds for OpenGL, whose clip space has +Y up; Vulkan's points down.
    // Left uncorrected the map is sampled upside down and the shadow appears
    // above whatever casts it.
    //
    // Applied to the PROJECTION, before it is combined with the view. Negating
    // the same element of the finished product instead is not a Y flip at all:
    // that element of the product is a combination of the view's rows, so
    // negating it corrupts the transform rather than mirroring it - which is
    // what this did until a test compared it against the convention the
    // cascades use.
    projection[1][1] *= -1.0f;

    const glm::mat4 view = glm::lookAt(position, position + forward, upFor(forward));
    return projection * view;
}

float ConeAttenuation(const glm::vec3& spotDirection, const glm::vec3& toFragment,
                      float innerAngleRadians, float outerAngleRadians) {
    const glm::vec3 axis = safeDirection(spotDirection);

    const float length = glm::length(toFragment);
    // A fragment exactly at the light is inside the cone by any definition.
    if (length < 1e-6f) return 1.0f;

    const float cosAngle = glm::dot(axis, toFragment / length);

    // Authored the wrong way round - an inner angle wider than the outer -
    // would otherwise divide by a negative and invert the cone, lighting
    // everything except the middle. Swapping is what the author meant.
    float inner = std::min(innerAngleRadians, outerAngleRadians);
    float outer = std::max(innerAngleRadians, outerAngleRadians);

    inner = std::clamp(inner, 0.0f, glm::radians(89.0f));
    outer = std::clamp(outer, 0.0f, glm::radians(89.0f));

    const float cosInner = std::cos(inner);
    const float cosOuter = std::cos(outer);

    // Compared as cosines rather than angles: no inverse trig per fragment,
    // and the comparison flips because cosine decreases as the angle grows.
    if (cosAngle <= cosOuter) return 0.0f;
    if (cosAngle >= cosInner) return 1.0f;

    const float span = cosInner - cosOuter;
    if (span <= 1e-6f) return 1.0f;   // inner == outer: a hard edge, not a divide by zero

    const float t = (cosAngle - cosOuter) / span;

    // Smoothstep rather than linear. A linear ramp in cosine leaves a visible
    // crease where the falloff meets full brightness, which reads as a banding
    // artefact rather than as the edge of a light.
    return t * t * (3.0f - 2.0f * t);
}

} // namespace SpotLight

} // namespace Supersonic
