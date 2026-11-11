#include "core/ComponentFields.hpp"

namespace Supersonic::ComponentFields {

namespace {

std::vector<Described>& registry() {
    // A function-local static, so registration from a static initialiser in a game
    // is ordered after this exists.
    static std::vector<Described> all;
    return all;
}

}  // namespace

const std::vector<Described>& All() { return registry(); }

bool Add(Described described) {
    for (const auto& existing : registry()) {
        if (existing.key == described.key) return false;
    }
    registry().push_back(std::move(described));
    return true;
}

bool Remove(const std::string& key) {
    auto& all = registry();
    for (auto it = all.begin(); it != all.end(); ++it) {
        if (it->key == key) {
            all.erase(it);
            return true;
        }
    }
    return false;
}

void Clear() { registry().clear(); }

}  // namespace Supersonic::ComponentFields
