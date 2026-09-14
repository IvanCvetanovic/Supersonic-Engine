#include "sim/Hazards.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "sim/Units.hpp"

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

    // How far past the level a player may go before it has fallen out of it.
    const Json::Value& bounds = root["bounds"];
    if (!bounds.IsObject()) {
        error = path + ": no \"bounds\" object";
        return false;
    }
    const Json::Value& margin = bounds["margin_px"];
    if (!margin.IsArray() || margin.AsArray().size() != 2) {
        error = path + ": no \"bounds.margin_px\" pair";
        return false;
    }
    double margins[2] = {0.0, 0.0};
    int axis = 0;
    for (const Json::Value& value : margin.AsArray()) {
        if (!value.IsNumber()) {
            error = path + ": bounds.margin_px holds a value that is not a number";
            return false;
        }
        margins[axis++] = value.AsNumber();
    }
    read.boundsMarginPx = glm::dvec2(margins[0], margins[1]);

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

    // AND THE LEVEL'S EDGE, which is checkGameLost's second death and was the
    // one nothing here tested: a player who walked off a ledge fell for ever.
    // The four comparisons are the original's, in the order it writes them.
    //
    // GUARDED ON HAVING A MARKER. The layer's loadLevel refuses a level with no
    // level_bounds, but Game::Start does not - and every sim suite goes through
    // Game::Start. An absent marker left boundsPx at (0, 0), and the test would
    // then kill the player on the first tick of all 128 levels.
    if (!haveBounds || playerDied) return;
    const auto* transform = registry.try_get<Supersonic::TransformComponent>(player);
    if (transform == nullptr) return;
    const glm::dvec2 atPx = Units::ToPixels(transform->position);
    const glm::dvec2 maxPx = boundsPx + marginPx;
    const glm::dvec2 minPx = -marginPx;
    if (atPx.x > maxPx.x || atPx.y > maxPx.y || atPx.x < minPx.x || atPx.y < minPx.y) {
        playerDied = true;
        diedByFalling = true;
        killedBy = "the fall";
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
    out.marginPx = rules.boundsMarginPx;
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);

        // The level's extent - the `max` entity, which Camera reads for its
        // clamp and Boss for its spike cull. Read here rather than handed in so
        // that Hazards owns every answer to "what killed the player".
        if (role == Roles::kLevelBounds && !out.haveBounds) {
            const Tscn::Value* position = node.Find("position");
            if (position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
                out.boundsPx = glm::dvec2(position->numbers[0], position->numbers[1]);
                out.haveBounds = true;
            }
        }
        if (role != Roles::kHazard) continue;

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
