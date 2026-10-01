#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace Supersonic {

// glm::normalize, except that a vector with no direction gets `fallback` instead of
// NaN.
//
// glm::normalize divides by sqrt(dot(v, v)) and does not look: a zero vector gives
// 0/0, and a vector with a NaN or an infinity in it gives NaN. A NaN normal is NaN
// lighting on every pixel of every triangle it touches, and it is easy to reach with
// data from outside the engine - an exporter that writes (0,0,0) for a degenerate
// vertex, a "vn 0 0 0", a triangle whose corners are in a line, a node scaled to
// zero.
//
// For every vector that HAS a direction the result is glm::normalize's, bit for bit:
// it is called, not reimplemented.
inline glm::vec3 NormalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float lengthSquared = glm::dot(v, v);
    // Written so that NaN fails it: `!(x > t)` is true for NaN, `x <= t` is not.
    if (!(lengthSquared > 1e-30f) || !std::isfinite(lengthSquared)) return fallback;
    return glm::normalize(v);
}

} // namespace Supersonic
