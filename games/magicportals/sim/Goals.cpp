#include "sim/Goals.hpp"

#include "core/Json.hpp"

#include <fstream>
#include <sstream>

namespace MagicPortals::Goals {

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    Supersonic::Json::Parser parser(text);
    Supersonic::Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root.Has("level") || !root["level"].IsObject()) {
        error = path + ": no \"level\" object";
        return false;
    }
    const Supersonic::Json::Value& level = root["level"];
    if (!level.Has("exit_requires_all_crystals") || !level["exit_requires_all_crystals"].IsBool()) {
        error = path + ": level.exit_requires_all_crystals is missing or not a bool";
        return false;
    }
    out = Rules{};
    out.exitRequiresAllCrystals = level["exit_requires_all_crystals"].AsBool();
    return true;
}

int State::Remaining() const {
    int remaining = 0;
    for (const Crystal& crystal : crystals) {
        if (!crystal.collected) ++remaining;
    }
    return remaining;
}

bool State::ExitOpen() const {
    return !rules.exitRequiresAllCrystals || Remaining() == 0;
}

void State::Tick(entt::registry& registry, entt::entity player) {
    if (player == entt::null || !registry.valid(player)) return;
    for (Crystal& crystal : crystals) {
        if (!crystal.collected && Trigger::Overlaps(registry, player, crystal.box)) crystal.collected = true;
    }
    const bool inExit = Trigger::Overlaps(registry, player, exit);
    if (inExit && !playerInExit) {
        ++exitEntries;
        if (ExitOpen()) completed = true;
    }
    playerInExit = inExit;
}

const Crystal* State::FindCrystal(const std::string& name) const {
    for (const Crystal& crystal : crystals) {
        if (crystal.name == name) return &crystal;
    }
    return nullptr;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    int exits = 0;
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        if (role == Roles::kCollectible) {
            if (node.Meta("time") != nullptr) {
                error = node.name + " is a timed crystal, and those are not ported";
                return false;
            }
            Crystal crystal;
            crystal.name = node.name;
            if (!Trigger::FromNode(node, crystal.box, error)) return false;
            out.crystals.push_back(crystal);
        } else if (role == Roles::kExitDoor) {
            if (!Trigger::FromNode(node, out.exit, error)) return false;
            ++exits;
        }
    }
    if (exits != 1) {
        error = std::to_string(exits) + " exits, where a level has one";
        return false;
    }
    return true;
}

} // namespace MagicPortals::Goals
