#pragma once

// Rust's float semantics, for the few operations where C++'s differ.
//
// HUSK's sim is held to its Rust original bit for bit, and a port that reads
// right can still disagree in three places: float-to-int casts, min/max with a
// NaN, and clamp. Everything else it uses (+ - * /, sqrt, floor, ceil, round,
// abs) is exactly rounded by IEEE 754 in both languages, provided the compiler
// does not fuse a multiply and an add - which is why HuskSim is built with
// contraction off, and why a test pins the bits of the operations on inputs
// taken from the Rust side.

#include <cmath>
#include <cstdint>

namespace husk {

// f32::max and f32::min are IEEE maxNum/minNum: a NaN operand gives back the
// other one. std::max(a, b) is `a < b ? b : a`, which passes a NaN through or
// not depending on which side it arrived on.
inline float fmaxr(float a, float b) {
    if (a != a) return b;
    if (b != b) return a;
    return a > b ? a : b;
}

inline float fminr(float a, float b) {
    if (a != a) return b;
    if (b != b) return a;
    return a < b ? a : b;
}

// f32::clamp, in its order: below the floor first, then above the ceiling. A
// NaN passes through, as it does in Rust.
inline float clampr(float x, float lo, float hi) {
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return x;
}

// `f as i32` / `f as u32` / ...: truncation toward zero, SATURATING at the
// target's range, and NaN to 0. A plain static_cast is undefined behaviour
// outside the range, which is not a difference anyone would find by reading.
// Every bound below is exactly representable as a float.
inline int32_t asI32(float f) {
    if (f != f) return 0;
    if (f >= 2147483648.0f) return INT32_MAX;
    if (f < -2147483648.0f) return INT32_MIN;
    return static_cast<int32_t>(f);
}

inline uint32_t asU32(float f) {
    if (f != f) return 0;
    if (f >= 4294967296.0f) return UINT32_MAX;
    if (f <= -1.0f) return 0;
    return static_cast<uint32_t>(f);
}

inline uint64_t asU64(float f) {
    if (f != f) return 0;
    if (f >= 18446744073709551616.0f) return UINT64_MAX;
    if (f <= -1.0f) return 0;
    return static_cast<uint64_t>(f);
}

inline uint64_t asU64(double f) {
    if (f != f) return 0;
    if (f >= 18446744073709551616.0) return UINT64_MAX;
    if (f <= -1.0) return 0;
    return static_cast<uint64_t>(f);
}

} // namespace husk
