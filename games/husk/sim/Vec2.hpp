#pragma once

// glam 0.30.10's Vec2, transcribed rather than reimplemented.
//
// The sim's positions are glam Vec2s, and "the same maths" is not enough to
// reproduce their bits: `normalize` multiplies by 1/len rather than dividing by
// len, and `clamp_length_max` divides by the root of the squared length rather
// than calling length(). Each body below is the expression glam evaluates, in
// its order (src/f32/vec2.rs in the glam-0.30.10 crate, the version HUSK's
// Cargo.lock pins). Not glm, whose normalize is `v * inversesqrt(dot(v, v))`.

#include "RustMath.hpp"

#include <cmath>

namespace husk {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    static constexpr Vec2 splat(float v) { return {v, v}; }

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator-(Vec2 a) { return {-a.x, -a.y}; }
    friend constexpr Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
    friend constexpr Vec2 operator*(float s, Vec2 a) { return {s * a.x, s * a.y}; }
    friend constexpr Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
    friend constexpr Vec2 operator/(Vec2 a, Vec2 b) { return {a.x / b.x, a.y / b.y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }

    // PartialEq: component-wise ==, so -0.0 equals 0.0 and a NaN equals nothing.
    friend constexpr bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
    friend constexpr bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }

    float dot(Vec2 r) const { return (x * r.x) + (y * r.y); }
    float lengthSquared() const { return dot(*this); }
    float length() const { return std::sqrt(dot(*this)); }
    float lengthRecip() const { return 1.0f / length(); }
    float distance(Vec2 r) const { return (*this - r).length(); }
    float distanceSquared(Vec2 r) const { return (*this - r).lengthSquared(); }
    Vec2 normalize() const { return *this * lengthRecip(); }

    Vec2 clampLengthMax(float max) const {
        const float lengthSq = lengthSquared();
        if (lengthSq > max * max) {
            return max * (*this / std::sqrt(lengthSq));
        }
        return *this;
    }

    Vec2 max(Vec2 r) const { return {fmaxr(x, r.x), fmaxr(y, r.y)}; }
    Vec2 min(Vec2 r) const { return {fminr(x, r.x), fminr(y, r.y)}; }
    Vec2 clamp(Vec2 lo, Vec2 hi) const { return max(lo).min(hi); }
};

inline constexpr Vec2 kZero{0.0f, 0.0f};
inline constexpr Vec2 kUnitX{1.0f, 0.0f};

} // namespace husk
