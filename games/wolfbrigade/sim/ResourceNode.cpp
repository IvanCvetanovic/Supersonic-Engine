#include "sim/ResourceNode.hpp"

#include <algorithm>

namespace WolfBrigade {

int ResourceNode::Extract(int n) {
    // Never more than is there, and never a negative bite: a caller asking for
    // -5 would otherwise ADD five to the node and hand back a negative load
    // that the worker then banks as a debt.
    const int got = std::max(0, std::min(n, amount));
    amount -= got;
    return got;
}

float ResourceNode::Fraction() const {
    const float total = static_cast<float>(std::max(maxAmount, 1));
    return std::min(1.0f, std::max(0.0f, static_cast<float>(amount) / total));
}

} // namespace WolfBrigade
