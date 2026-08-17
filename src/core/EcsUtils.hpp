#pragma once

#include <entt/entt.hpp>

namespace Engine {

// Several systems want "the one active camera" or "the one directional light".
// Writing that as a view loop with a break at the end works, but the loop
// increment is then unreachable, which /W4 correctly flags. This expresses the
// intent directly.
template <typename View>
entt::entity FirstEntityOf(View&& view) {
    auto it = view.begin();
    return it == view.end() ? entt::null : *it;
}

} // namespace Engine
