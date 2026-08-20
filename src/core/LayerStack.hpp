#pragma once

#include "core/EngineLayer.hpp"

#include <entt/entt.hpp>

#include <memory>
#include <vector>

namespace Supersonic {

// The layers a game has pushed, in the order they run.
//
// Order is push order and nothing else - no priorities, no sorting, no
// dependency graph. A game that needs its systems in a particular sequence
// pushes them in that sequence, which is a thing it already knows and the
// engine never could. The alternative, a priority number per layer, is a
// mechanism that looks like it resolves ordering and does not: two layers with
// the same number are back where they started, and the numbers become a
// negotiation between parts of a game that should not have to know about each
// other.
//
// Kept apart from SupersonicApp so the ordering rules and the reentrancy rule
// below can be tested without a window, a device or a frame.
class LayerStack {
public:
    ~LayerStack() { Clear(); }

    LayerStack() = default;
    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    // Attaches immediately, so a layer can build its world before the frame it
    // was pushed on runs. Null is ignored rather than stored: a stack holding a
    // null entry crashes on the next tick, a long way from the push that did it.
    void Push(std::unique_ptr<EngineLayer> layer, entt::registry& registry) {
        if (!layer) return;
        layer->OnAttach(registry);
        m_layers.push_back(std::move(layer));
    }

    // Detaches in REVERSE order, which is the only order that is safe when
    // layers were pushed in dependency order: the last one pushed may be built
    // on the ones before it, so it has to come apart first.
    void Clear(entt::registry* registry = nullptr) {
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) {
            if (*it && registry) (*it)->OnDetach(*registry);
        }
        m_layers.clear();
    }

    void FixedUpdate(entt::registry& registry, float fixedDelta) {
        // Indexed, not iterated.
        //
        // A layer may push another layer from inside its own tick - a game
        // starting a match, a level loading its own systems - and that
        // invalidates any iterator into the vector. Indexing survives the
        // reallocation, and a layer pushed mid-tick is attached immediately by
        // Push and then ticked at the end of this same pass rather than being
        // skipped until the next frame.
        for (std::size_t i = 0; i < m_layers.size(); ++i) {
            if (m_layers[i]) m_layers[i]->OnFixedUpdate(registry, fixedDelta);
        }
    }

    void Update(entt::registry& registry, float deltaTime) {
        for (std::size_t i = 0; i < m_layers.size(); ++i) {
            if (m_layers[i]) m_layers[i]->OnUpdate(registry, deltaTime);
        }
    }

    std::size_t Size() const { return m_layers.size(); }
    bool Empty() const { return m_layers.empty(); }

    // By index, in run order. For the editor's benefit - a stack you cannot
    // see is one you cannot debug.
    const EngineLayer* At(std::size_t index) const {
        return index < m_layers.size() ? m_layers[index].get() : nullptr;
    }

private:
    std::vector<std::unique_ptr<EngineLayer>> m_layers;
};

} // namespace Supersonic
