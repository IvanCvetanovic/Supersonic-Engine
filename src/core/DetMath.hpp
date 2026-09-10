#pragma once

// Transcendental functions that give the same bits on every platform.
//
// test_determinism's canonical scene hashed to one number on Windows and
// another on Linux, and the whole difference was libm. Measured on 10 September:
// the first call to disagree was atan2f in EulerFromRotation during tick 29, one
// ulp apart, and sinf, cosf and asinf disagreed the same way later on
// (docs/planning/2026-09-10-cross-platform-determinism.md). The C standard does
// not require sin to be correctly rounded, so every C runtime is entitled to
// its own last bit, and a simulation that calls one inherits it.
//
// These are built only from what IEEE 754 DOES pin down: + - * / and sqrt, plus
// floor, fmod, frexp, ldexp and copysign, which are exact. They are evaluated in
// double and rounded to float once at the end, so any conforming platform
// computes every intermediate bit the same way. They are also accurate: the
// double-precision error is about nine orders of magnitude below a float ulp,
// so the result is the correctly rounded float except when the true value lies
// within that error of a rounding midpoint - and even then, every machine
// gives the same answer.
//
// FOR THE SIMULATION. The renderer, the editor and asset import keep libm: a
// picture one ulp different on another machine is not a bug, a replay that
// stops reproducing is.
//
// Two things this depends on are enforced in CMakeLists.txt rather than hoped
// for. Floating-point contraction is OFF, because an FMA fusing a*b+c rounds
// once where the code says twice, and only some targets have FMA. And double is
// IEEE binary64 evaluated in SSE or NEON registers rather than x87, which is
// true of every x86-64 and ARM64 toolchain the engine builds with.
//
// test_detmath pins these functions' bits against a table produced by a Python
// transcription of the same operations, so a compiler that reordered anything
// shows up as a failing line rather than a replay that quietly diverges.

#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Supersonic::DetMath {

namespace detail {

// Each is the nearest double to the value named, written so the literal
// round-trips exactly.
constexpr double kPi = 3.141592653589793;
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kTwoOverPi = 0.6366197723675814;
// pi/2 in two parts, the first keeping only its top 33 bits, so that k * hi is
// exact for every k below 2^20 and the reduction loses nothing there (Cody and
// Waite; the values are fdlibm's).
constexpr double kHalfPiHi = 1.5707963267341256;
constexpr double kHalfPiLo = 6.077100506506192e-11;
// ln 2 split the same way for exp's reduction (fdlibm's again).
constexpr double kLn2Hi = 0.6931471803691238;
constexpr double kLn2Lo = 1.9082149292705877e-10;
constexpr double kInvLn2 = 1.4426950408889634;
constexpr double kSqrtHalf = 0.7071067811865476;
// Past this an angle is folded into one turn with fmod first. fmod is exact,
// so the fold is deterministic; it is not accurate for angles this large, and
// nothing a simulation produces is one.
constexpr double kHugeAngle = 1073741824.0; // 2^30

inline float quietNaN() { return std::numeric_limits<float>::quiet_NaN(); }

// sin and cos on [-pi/4, pi/4]: Taylor series to x^15 and x^16, where the next
// term is below 1e-16 of the result.
inline double sinKernel(double r) {
    const double r2 = r * r;
    return r + r * r2 *
                   (-1.0 / 6.0 +
                    r2 * (1.0 / 120.0 +
                          r2 * (-1.0 / 5040.0 +
                                r2 * (1.0 / 362880.0 +
                                      r2 * (-1.0 / 39916800.0 +
                                            r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0)))))));
}

inline double cosKernel(double r) {
    const double r2 = r * r;
    return 1.0 +
           r2 * (-1.0 / 2.0 +
                 r2 * (1.0 / 24.0 +
                       r2 * (-1.0 / 720.0 +
                             r2 * (1.0 / 40320.0 +
                                   r2 * (-1.0 / 3628800.0 +
                                         r2 * (1.0 / 479001600.0 +
                                               r2 * (-1.0 / 87178291200.0 + r2 * (1.0 / 20922789888000.0))))))));
}

// x reduced to r in about [-pi/4, pi/4], and which quarter-turn it came from.
inline double reduce(double x, int& quadrant) {
    if (!(std::fabs(x) < kHugeAngle)) x = std::fmod(x, kTwoPi);
    const double k = std::floor(x * kTwoOverPi + 0.5);
    quadrant = static_cast<int>(static_cast<long long>(k) & 3);
    return (x - k * kHalfPiHi) - k * kHalfPiLo;
}

// atan of a non-negative, finite a.
//
// Past 1 it uses atan(a) = pi/2 - atan(1/a). Then it halves the angle twice
// with atan(a) = 2 atan(a / (1 + sqrt(1 + a^2))) - only a divide and an exactly
// rounded square root each time - which leaves |a| <= tan(pi/16) = 0.199, where
// the series to a^23 is below 1e-18 of the result.
inline double atanPositive(double a) {
    const bool inverted = a > 1.0;
    if (inverted) a = 1.0 / a;
    a = a / (1.0 + std::sqrt(1.0 + a * a));
    a = a / (1.0 + std::sqrt(1.0 + a * a));
    const double a2 = a * a;
    const double series =
        a + a * (a2 * (-1.0 / 3.0 +
                       a2 * (1.0 / 5.0 +
                             a2 * (-1.0 / 7.0 +
                                   a2 * (1.0 / 9.0 +
                                         a2 * (-1.0 / 11.0 +
                                               a2 * (1.0 / 13.0 +
                                                     a2 * (-1.0 / 15.0 +
                                                           a2 * (1.0 / 17.0 +
                                                                 a2 * (-1.0 / 19.0 +
                                                                       a2 * (1.0 / 21.0 + a2 * (-1.0 / 23.0))))))))))));
    const double result = 4.0 * series;
    return inverted ? kHalfPi - result : result;
}

// ln of a positive, finite v: frexp to a mantissa in [sqrt(1/2), sqrt(2)), then
// ln m = 2 atanh((m - 1) / (m + 1)), a series in s^2 with |s| <= 0.172.
inline double logPositive(double v) {
    int k = 0;
    double m = std::frexp(v, &k);
    if (m < kSqrtHalf) {
        m *= 2.0;
        --k;
    }
    const double s = (m - 1.0) / (m + 1.0);
    const double s2 = s * s;
    const double t =
        s2 * (1.0 / 3.0 +
              s2 * (1.0 / 5.0 +
                    s2 * (1.0 / 7.0 +
                          s2 * (1.0 / 9.0 +
                                s2 * (1.0 / 11.0 +
                                      s2 * (1.0 / 13.0 +
                                            s2 * (1.0 / 15.0 +
                                                  s2 * (1.0 / 17.0 +
                                                        s2 * (1.0 / 19.0 + s2 * (1.0 / 21.0 + s2 * (1.0 / 23.0)))))))))));
    const double dk = static_cast<double>(k);
    return dk * kLn2Hi + ((2.0 * s + 2.0 * s * t) + dk * kLn2Lo);
}

// e^z: z = k ln2 + r with |r| <= ln2 / 2, a Taylor series to r^14 for e^r, and
// an exact ldexp for the 2^k.
inline double expD(double z) {
    if (z > 709.0) return std::numeric_limits<double>::infinity();
    if (z < -745.0) return 0.0;
    const double k = std::floor(z * kInvLn2 + 0.5);
    const double r = (z - k * kLn2Hi) - k * kLn2Lo;
    const double p =
        1.0 +
        r * (1.0 +
             r * (1.0 / 2.0 +
                  r * (1.0 / 6.0 +
                       r * (1.0 / 24.0 +
                            r * (1.0 / 120.0 +
                                 r * (1.0 / 720.0 +
                                      r * (1.0 / 5040.0 +
                                           r * (1.0 / 40320.0 +
                                                r * (1.0 / 362880.0 +
                                                     r * (1.0 / 3628800.0 +
                                                          r * (1.0 / 39916800.0 +
                                                               r * (1.0 / 479001600.0 +
                                                                    r * (1.0 / 6227020800.0 +
                                                                         r * (1.0 / 87178291200.0))))))))))))));
    return std::ldexp(p, static_cast<int>(k));
}

} // namespace detail

// sin and cos of one angle, sharing the reduction. Signed zero is kept
// (sin(-0) is -0), and an infinite or NaN angle gives NaN.
inline void sincos(float x, float& s, float& c) {
    if (!std::isfinite(x)) {
        s = detail::quietNaN();
        c = detail::quietNaN();
        return;
    }
    if (x == 0.0f) {
        s = x;
        c = 1.0f;
        return;
    }
    int quadrant = 0;
    const double r = detail::reduce(static_cast<double>(x), quadrant);
    const double sk = detail::sinKernel(r);
    const double ck = detail::cosKernel(r);
    switch (quadrant) {
    case 0:
        s = static_cast<float>(sk);
        c = static_cast<float>(ck);
        break;
    case 1:
        s = static_cast<float>(ck);
        c = static_cast<float>(-sk);
        break;
    case 2:
        s = static_cast<float>(-sk);
        c = static_cast<float>(-ck);
        break;
    default:
        s = static_cast<float>(-ck);
        c = static_cast<float>(sk);
        break;
    }
}

inline float sin(float x) {
    float s = 0.0f, c = 0.0f;
    sincos(x, s, c);
    return s;
}

inline float cos(float x) {
    float s = 0.0f, c = 0.0f;
    sincos(x, s, c);
    return c;
}

// The full-circle arctangent, with C's conventions for zeros and infinities
// (atan2(+-0, -0) is +-pi, atan2(y, +-inf) is +-0 or +-pi, and so on).
inline float atan2(float y, float x) {
    if (std::isnan(y) || std::isnan(x)) return detail::quietNaN();
    const double yd = y;
    const double xd = x;
    if (yd == 0.0) {
        if (xd > 0.0 || (xd == 0.0 && !std::signbit(xd))) return y;
        return static_cast<float>(std::copysign(detail::kPi, yd));
    }
    if (xd == 0.0) return static_cast<float>(std::copysign(detail::kHalfPi, yd));
    if (std::isinf(xd)) {
        if (std::isinf(yd)) {
            return static_cast<float>(std::copysign(xd > 0.0 ? detail::kPi / 4.0 : 3.0 * detail::kPi / 4.0, yd));
        }
        return static_cast<float>(std::copysign(xd > 0.0 ? 0.0 : detail::kPi, yd));
    }
    if (std::isinf(yd)) return static_cast<float>(std::copysign(detail::kHalfPi, yd));

    double a = detail::atanPositive(std::fabs(yd) / std::fabs(xd));
    if (xd < 0.0) a = detail::kPi - a;
    return static_cast<float>(std::copysign(a, yd));
}

// asin through atan: asin x = atan(x / sqrt(1 - x^2)), with 1 - x^2 formed as
// (1 - x)(1 + x) so it keeps its precision as x approaches 1.
inline float asin(float x) {
    if (std::isnan(x) || std::fabs(x) > 1.0f) return detail::quietNaN();
    if (x == 0.0f) return x;
    const double a = std::fabs(static_cast<double>(x));
    const double r = a == 1.0 ? detail::kHalfPi : detail::atanPositive(a / std::sqrt((1.0 - a) * (1.0 + a)));
    return static_cast<float>(std::copysign(r, static_cast<double>(x)));
}

// base^exponent for a NON-NEGATIVE base, which is every use the simulation has
// (a damping factor clamped at zero). A negative base gives NaN even for an
// integer exponent, where C's pow would not: say what is meant with a multiply.
inline float pow(float base, float exponent) {
    if (exponent == 0.0f || base == 1.0f) return 1.0f;
    if (std::isnan(base) || std::isnan(exponent) || base < 0.0f) return detail::quietNaN();
    const double b = base;
    const double e = exponent;
    const float inf = std::numeric_limits<float>::infinity();
    if (b == 0.0) return e > 0.0 ? 0.0f : inf;
    if (std::isinf(b)) return e > 0.0 ? inf : 0.0f;
    if (std::isinf(e)) return (b < 1.0) == (e > 0.0) ? 0.0f : inf;
    return static_cast<float>(detail::expD(e * detail::logPositive(b)));
}

// glm::angleAxis, with the half angle's sine and cosine taken from here: the
// quaternion turning `angle` radians about the unit `axis`.
inline glm::quat angleAxis(float angle, const glm::vec3& axis) {
    const float half = angle * 0.5f;
    float s = 0.0f, c = 0.0f;
    sincos(half, s, c);
    return glm::quat(c, axis * s);
}

} // namespace Supersonic::DetMath
