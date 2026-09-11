#include "sim/Hazards.hpp"

namespace MagicPortals::Hazards {

void State::Tick(entt::registry& registry, entt::entity player) {
    if (player == entt::null || !registry.valid(player)) return;
    for (Hazard& hazard : hazards) {
        const bool inside = Trigger::Overlaps(registry, player, hazard.box);
        if (inside && !hazard.playerInside && !playerDied) {
            playerDied = true;
            killedBy = hazard.name;
        }
        hazard.playerInside = inside;
    }
}

const Hazard* State::FindHazard(const std::string& name) const {
    for (const Hazard& hazard : hazards) {
        if (hazard.name == name) return &hazard;
    }
    return nullptr;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, State& out, std::string& error) {
    out = State{};
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != "." || Roles::RoleOf(roles, node) != Roles::kHazard) continue;
        Hazard hazard;
        hazard.name = node.name;
        if (!Trigger::FromNode(node, hazard.box, error)) return false;
        out.hazards.push_back(hazard);
    }
    return true;
}

} // namespace MagicPortals::Hazards
