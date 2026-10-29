#pragma once

#include <cmath>

namespace Supersonic {

// Float comparisons for std::sort that stay a strict weak ordering when a NaN is in
// the data.
//
// `if (a != b) return a < b;` followed by a tie-break is the usual way to write a
// total order over a float key, and it is not one once NaN is a value: `NaN != x` is
// true while `NaN < x` and `x < NaN` are both false, so NaN is "unordered" with every
// number - equivalent to all of them while they are not equivalent to each other.
// std::sort's behaviour on a comparator like that is undefined, and in practice it
// leaves the finite elements around the NaN out of order (and can run a partition off
// the end of the range). A NaN reaches these sorts honestly: culling reads "not
// outside" for a NaN box, so an entity at NaN is drawn, and a body at NaN is a
// broadphase proxy.
//
// NaN here sorts AFTER every number, and NaNs tie with each other (so the caller's
// own tie-break - an index, a gather order - orders them). For finite data every
// function below answers exactly as the plain comparison does.

// a sorts strictly before b, ascending, NaN last.
inline bool AscendingNaNLast(float a, float b) {
    if (std::isnan(b)) return !std::isnan(a);
    return a < b;
}

// a sorts strictly before b, descending, NaN last.
inline bool DescendingNaNLast(float a, float b) {
    if (std::isnan(a)) return false;
    if (std::isnan(b)) return true;
    return a > b;
}

} // namespace Supersonic
