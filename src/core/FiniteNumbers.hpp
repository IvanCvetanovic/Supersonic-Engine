#pragma once

#include <cstddef>
#include <locale>
#include <ostream>

namespace Supersonic {

// A number facet that cannot write a number a JSON reader refuses.
//
// A scene, a prefab, a Play-mode snapshot and an undo step are all written with
// `<<`, and `<< float` writes NaN as "nan" and infinity as "inf". Json::Parse
// rejects both, so one non-finite value anywhere makes the whole file unreadable:
// Play/Stop cannot restore it, undo drops the snapshot, and a saved scene cannot
// be opened. And a non-finite value is easy to produce - ImGui's typed input is
// not clamped unless a field asks for it, and a script can write anything.
//
// ComponentCodec already substitutes at about thirty-five sites (`jsonSafe`, which
// also names the field). There are about a hundred more that write straight to the
// stream, and a new one is added with every component. This is the backstop that
// does not depend on each of them remembering: the stream itself will not write
// what the reader cannot read.
//
// The substitute is jsonSafe's: NaN becomes 0, and infinity the largest finite
// float of the same sign, so "very far away" does not silently become "at the
// origin". Every FINITE value is formatted by the standard facet with the stream's
// own flags, precision and locale, so the text of every file that already loaded
// is byte for byte what it was.
class FiniteNumPut final : public std::num_put<char> {
public:
    // How many numbers this facet has replaced.
    size_t Replaced() const { return m_replaced; }

protected:
    iter_type do_put(iter_type out, std::ios_base& flags, char fill, double value) const override;
    iter_type do_put(iter_type out, std::ios_base& flags, char fill, long double value) const override;

private:
    // Streams here are single-threaded, and a facet belongs to one of them.
    mutable size_t m_replaced{0};
};

// For the lifetime of the guard, `out` writes only numbers a reader can read, and
// its previous locale is put back afterwards, so the caller's stream is as it was.
//
// Nesting is free: when the stream already has the facet (writeScene wraps
// ComponentCodec::Write, which a prefab save calls on its own) the inner guard does
// nothing, so an entity does not pay to build a locale and the count in the warning
// is the whole write's.
//
// A warning is logged if anything was replaced. It says how many, not which: the
// sites that know their field name (`jsonSafe`) have already said so.
class FiniteNumbersOnly {
public:
    explicit FiniteNumbersOnly(std::ostream& out);
    ~FiniteNumbersOnly();

    FiniteNumbersOnly(const FiniteNumbersOnly&) = delete;
    FiniteNumbersOnly& operator=(const FiniteNumbersOnly&) = delete;

private:
    std::ostream& m_out;
    const FiniteNumPut* m_facet{nullptr};
    std::locale m_previous;
    bool m_installed{false};
};

} // namespace Supersonic
