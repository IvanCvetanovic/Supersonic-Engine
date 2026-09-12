#include "sim/Hazards.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace MagicPortals::Hazards {

namespace {

namespace Json = Supersonic::Json;

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot read";
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string content = text.str();
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }

    Rules read;
    const Json::Value& names = root["not_hazard_names"];
    if (!names.IsArray()) {
        error = path + ": no \"not_hazard_names\" array";
        return false;
    }
    for (const Json::Value& name : names.AsArray()) {
        if (!name.IsString()) {
            error = path + ": not_hazard_names holds a value that is not a string";
            return false;
        }
        read.notHazardNames.push_back(name.AsString());
    }

    out = std::move(read);
    return true;
}

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

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != "." || Roles::RoleOf(roles, node) != Roles::kHazard) continue;

        // The role table is the remake's design decision, and in this one place it
        // is wrong about the original: what it lists here kills nothing. Taken out
        // by entity name rather than by node name, since the converter makes the
        // node names unique per placement.
        const std::string entityName = Roles::EntityName(node);
        if (std::find(rules.notHazardNames.begin(), rules.notHazardNames.end(), entityName) !=
            rules.notHazardNames.end()) {
            continue;
        }

        Hazard hazard;
        hazard.name = node.name;
        if (!Trigger::FromNode(node, hazard.box, error)) return false;
        out.hazards.push_back(hazard);
    }
    return true;
}

} // namespace MagicPortals::Hazards
