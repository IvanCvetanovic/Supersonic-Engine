#include "core/FiniteNumbers.hpp"

#include "core/Log.hpp"

#include <cmath>
#include <limits>

namespace Supersonic {

namespace {

// The same policy as ComponentCodec's jsonSafe: NaN to zero, and infinity to the
// largest finite float of its sign.
template <typename T>
T finiteOrSubstitute(T value, size_t& replaced) {
    if (std::isfinite(value)) return value;
    ++replaced;
    if (std::isnan(value)) return T(0);
    return std::copysign(static_cast<T>(std::numeric_limits<float>::max()), value);
}

} // namespace

FiniteNumPut::iter_type FiniteNumPut::do_put(iter_type out, std::ios_base& flags, char fill,
                                             double value) const {
    return std::num_put<char>::do_put(out, flags, fill, finiteOrSubstitute(value, m_replaced));
}

FiniteNumPut::iter_type FiniteNumPut::do_put(iter_type out, std::ios_base& flags, char fill,
                                             long double value) const {
    return std::num_put<char>::do_put(out, flags, fill, finiteOrSubstitute(value, m_replaced));
}

FiniteNumbersOnly::FiniteNumbersOnly(std::ostream& out) : m_out(out) {
    // Already there: an outer guard owns the count and the restore.
    if (dynamic_cast<const FiniteNumPut*>(&std::use_facet<std::num_put<char>>(out.getloc()))) {
        return;
    }

    // The locale takes ownership of the facet. It is kept alive by the stream's
    // copy until the guard restores the old one, and the count is read before that.
    auto* facet = new FiniteNumPut;
    m_facet = facet;
    m_previous = out.imbue(std::locale(out.getloc(), facet));
    m_installed = true;
}

FiniteNumbersOnly::~FiniteNumbersOnly() {
    if (!m_installed) return;

    const size_t replaced = m_facet->Replaced();
    m_out.imbue(m_previous);

    if (replaced > 0) {
        SUPERSONIC_LOG_WARN("FiniteNumbers")
            << replaced << " number(s) that were NaN or infinite were written as 0 or the "
            << "largest finite value instead, so the file stays loadable." << std::endl;
    }
}

} // namespace Supersonic
