#include "sim/SaveIds.hpp"

#include "sim/Building.hpp"
#include "sim/Unit.hpp"

namespace WolfBrigade {

Damageable* SidResolver::AsDamageable(int sid) const {
    // Units first, then buildings. The two maps cannot both hold an id -
    // SidTable mints from one counter - so the order is a formality, and
    // saying so here stops somebody "fixing" it into an ambiguity later.
    if (Unit* unit = AsUnit(sid)) return unit;
    if (Building* building = AsBuilding(sid)) return building;
    return nullptr;
}

} // namespace WolfBrigade
