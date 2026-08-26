#pragma once

#include <string>
#include <unordered_map>

#include "sim/Damageable.hpp"

namespace WolfBrigade {

class Building;
class Unit;
struct ResourceNode;

// ONE monotonic id space across units, buildings and resource nodes.
//
// Straight from `snapshot.gd`, which says why in a comment worth repeating: a
// unit's attack target is polymorphic - it may be a Unit OR a Building - and a
// single id space lets that reference come back with no type tag in the file.
// Two spaces would need one, and a type tag is a thing that can be wrong.
//
// THE UPCAST IS AN INVARIANT, not a formality. A unit is keyed by
// `static_cast<const Damageable*>(unit)`, and every lookup upcasts the same way.
// The standard does not promise a base subobject sits at offset zero, so keying
// on `const void*` and hoping would produce misses - and a miss here returns
// -1, which is indistinguishable from the legitimate "this referent was not
// captured" answer. Do not simplify this to void*.
//
// Resource nodes are not Damageable and get their own map, drawing from the
// same counter. Two maps, one id space: the type safety is kept and the file
// still needs no tags.
class SidTable {
public:
    static constexpr int kNone = -1;

    // Mints the next id. The order ids are handed out in is the caller's
    // capture order and nothing depends on it, which is why a restored board
    // re-captures with different ids and the equivalence check has to say so.
    int Add(const Damageable* entity) {
        if (entity == nullptr) return kNone;
        const int sid = m_next++;
        m_damageables[entity] = sid;
        return sid;
    }

    int Add(const ResourceNode* node) {
        if (node == nullptr) return kNone;
        const int sid = m_next++;
        m_nodes[node] = sid;
        return sid;
    }

    // -1 for anything that was not captured, which is the correct answer for a
    // reference to something already dead, already exhausted, or deliberately
    // outside the scene the caller enumerated.
    int Of(const Damageable* entity) const {
        if (entity == nullptr) return kNone;
        const auto it = m_damageables.find(entity);
        return it == m_damageables.end() ? kNone : it->second;
    }

    int Of(const ResourceNode* node) const {
        if (node == nullptr) return kNone;
        const auto it = m_nodes.find(node);
        return it == m_nodes.end() ? kNone : it->second;
    }

    int Count() const { return m_next; }

private:
    std::unordered_map<const Damageable*, int> m_damageables;
    std::unordered_map<const ResourceNode*, int> m_nodes;
    int m_next{0};
};

// The other direction: an id from the file back to the thing just rebuilt.
//
// Typed lookups rather than one that returns a base pointer, because the caller
// always knows what it expects - a build target is a Building, a gather target
// is a ResourceNode - and only the attack target is genuinely polymorphic.
// AsDamageable is the one that answers that question, and it is the only place
// the single id space is actually cashed in.
class SidResolver {
public:
    void Bind(int sid, Unit* unit) {
        if (sid >= 0 && unit != nullptr) m_units[sid] = unit;
    }
    void Bind(int sid, Building* building) {
        if (sid >= 0 && building != nullptr) m_buildings[sid] = building;
    }
    void Bind(int sid, ResourceNode* node) {
        if (sid >= 0 && node != nullptr) m_nodes[sid] = node;
    }

    Unit* AsUnit(int sid) const { return Find(m_units, sid); }
    Building* AsBuilding(int sid) const { return Find(m_buildings, sid); }
    ResourceNode* AsNode(int sid) const { return Find(m_nodes, sid); }

    // A unit or a building, whichever holds this id. Null for an id nobody
    // bound - a wild number in a hand-edited file, or a reference to something
    // the capture skipped.
    Damageable* AsDamageable(int sid) const;

private:
    template <typename T>
    static T* Find(const std::unordered_map<int, T*>& table, int sid) {
        const auto it = table.find(sid);
        return it == table.end() ? nullptr : it->second;
    }

    std::unordered_map<int, Unit*> m_units;
    std::unordered_map<int, Building*> m_buildings;
    std::unordered_map<int, ResourceNode*> m_nodes;
};

} // namespace WolfBrigade
