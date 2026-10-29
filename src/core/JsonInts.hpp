#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Supersonic {

// Integers out of JSON numbers.
//
// A JSON number is a double, and converting a double to an integer type it does not fit
// is undefined behaviour in C++: a negative to unsigned, 1e20, NaN. `static_cast` at
// about twenty sites in this file did exactly that on whatever a scene said, and a
// scene can say anything.
//
// What the cast DID in practice on x86 - both MSVC and GCC, since the hardware
// instruction is the same - is kept, and made defined, so no scene that loads today
// loads differently:
//   unsigned: a value that fits in 64 bits wraps modulo 2^32 ("Layer": -1 is every
//             bit set, which is how a hand-written "all layers" gets written), and
//             anything else, NaN included, is 0.
//   signed:   a value that fits is truncated toward zero, as always. One that does
//             not used to be INT_MIN whatever its sign; it now saturates to the end it
//             is past, so "MaxLength": 1e10 is a very long field and not a negative
//             one. NaN is 0.
inline uint32_t asU32(double value) {
    // 2^63: the first double a 64-bit signed integer cannot hold.
    constexpr double kLimit = 9223372036854775808.0;
    if (!(value > -kLimit && value < kLimit)) return 0u;   // false for NaN as well
    return static_cast<uint32_t>(static_cast<int64_t>(value));
}

inline int32_t asI32(double value) {
    if (value != value) return 0;
    if (value >= 2147483647.0) return std::numeric_limits<int32_t>::max();
    if (value <= -2147483648.0) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value);
}

// An index out of a JSON number: the value if it names one, and SIZE_MAX - which is
// past the end of any array, so "no such element" - for a negative, a NaN or anything
// too big. `-1` means "none" in several places in the file formats, and
// static_cast<size_t>(-1.0) was undefined behaviour that happened to give it.
inline size_t asIndex(double value) {
    constexpr double kLimit = 9223372036854775808.0;
    if (!(value >= 0.0 && value < kLimit)) return std::numeric_limits<size_t>::max();
    return static_cast<size_t>(value);
}

} // namespace Supersonic
