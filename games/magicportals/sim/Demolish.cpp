#include "sim/Demolish.hpp"

#include "core/Json.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::Demolish {

namespace {

// hazards.gd:62-66: a flag is set when it is there and is neither "" nor "0".
bool Flagged(const Tscn::Node& node, const char* key) {
    const Tscn::Value* value = node.Meta(key);
    if (value == nullptr) return false;
    if (value->kind == Tscn::Value::Kind::String) return !value->text.empty() && value->text != "0";
    double number = 0.0;
    return value->AsNumber(number) && number != 0.0;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    namespace Json = Supersonic::Json;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    const auto has = [&](const char* block, const char* key) {
        if (!root.IsObject() || !root.Has(block) || !root[block].IsObject() || !root[block].Has(key)) {
            error = path + ": " + block + "." + key + " is missing";
            return false;
        }
        return true;
    };

    Rules read;
    if (!has("breakable", "names")) return false;
    const Json::Value& names = root["breakable"]["names"];
    if (!names.IsArray()) {
        error = path + ": breakable.names is not an array";
        return false;
    }
    for (const Json::Value& name : names.AsArray()) {
        if (!name.IsString()) {
            error = path + ": breakable.names lists something that is not a string";
            return false;
        }
        read.breakableNames.push_back(name.AsString());
    }
    if (!has("contact", "margin_px")) return false;
    if (!root["contact"]["margin_px"].IsNumber()) {
        error = path + ": contact.margin_px is not a number";
        return false;
    }
    read.contactMarginPx = root["contact"]["margin_px"].AsNumber();
    out = std::move(read);
    return true;
}

void State::Tick(entt::registry& registry) {
    const float margin = Units::ToMetres(rules.contactMarginPx);
    for (Breakable& breakable : breakables) {
        if (breakable.broken) continue;
        Trigger::Box reach = breakable.box;
        reach.half += glm::vec2(margin);
        for (const Stone& stone : stones) {
            if (!registry.valid(stone.body) || !Trigger::Overlaps(registry, stone.body, reach)) continue;
            breakable.broken = true;
            breakable.brokenBy = stone.name;
            if (registry.valid(breakable.body)) registry.destroy(breakable.body);
            break;
        }
    }
}

const Stone* State::FindStone(const std::string& name) const {
    for (const Stone& stone : stones) {
        if (stone.name == name) return &stone;
    }
    return nullptr;
}

const Breakable* State::FindBreakable(const std::string& name) const {
    for (const Breakable& breakable : breakables) {
        if (breakable.name == name) return &breakable;
    }
    return nullptr;
}

int State::Broken() const {
    return static_cast<int>(
        std::count_if(breakables.begin(), breakables.end(), [](const Breakable& b) { return b.broken; }));
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const auto found = built.entities.find(node.name);

        if (Roles::RoleOf(roles, node) == Roles::kDemolisher) {
            if (found == built.entities.end()) {
                error = node.name + " is a rolling stone with no body";
                return false;
            }
            out.stones.push_back(Stone{node.name, found->second});
            continue;
        }

        const bool flagged = Flagged(node, "breakable");
        const std::string entityName = Roles::EntityName(node);
        const bool byName = std::find(rules.breakableNames.begin(), rules.breakableNames.end(), entityName) !=
                            rules.breakableNames.end();
        if ((!flagged && !byName) || found == built.entities.end()) continue;

        glm::dvec2 offsetPx(0.0);
        glm::dvec2 sizePx(0.0);
        if (!LevelBuilder::ShapeBoundsPx(scene, node, offsetPx, sizePx)) {
            error = node.name + " is breakable, and its shape has no bounds";
            return false;
        }
        // Quarter turns, and nothing between: a turned box is not its shape.
        double radians = 0.0;
        if (const Tscn::Value* rotation = node.Find("rotation")) rotation->AsNumber(radians);
        const double quarters = radians / (3.14159265358979323846 * 0.5);
        const double nearest = std::round(quarters);
        if (std::fabs(quarters - nearest) > 1e-3) {
            error = node.name + " is breakable and turned by " + std::to_string(radians) +
                    " radians, and only quarter turns are ported";
            return false;
        }
        // Turned in the remake's pixels, +y down: a quarter turn takes (x, y) to
        // (-y, x).
        const int turns = ((static_cast<int>(nearest) % 4) + 4) % 4;
        for (int i = 0; i < turns; ++i) {
            offsetPx = glm::dvec2(-offsetPx.y, offsetPx.x);
            sizePx = glm::dvec2(sizePx.y, sizePx.x);
        }
        glm::dvec2 atPx(0.0);
        if (const Tscn::Value* position = node.Find("position");
            position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
            atPx = glm::dvec2(position->numbers[0], position->numbers[1]);
        }

        Breakable breakable;
        breakable.name = node.name;
        breakable.body = found->second;
        breakable.flagged = flagged;
        const glm::vec3 centre = Units::ToWorld(atPx.x + offsetPx.x, atPx.y + offsetPx.y);
        breakable.box.centre = glm::vec2(centre.x, centre.y);
        breakable.box.half = glm::vec2(Units::ToMetres(sizePx.x * 0.5), Units::ToMetres(sizePx.y * 0.5));
        out.breakables.push_back(std::move(breakable));
    }
    return true;
}

} // namespace MagicPortals::Demolish
