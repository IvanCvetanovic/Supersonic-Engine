#include "sim/Fields.hpp"

#include "sim/Units.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Fields {

namespace {

namespace Json = Supersonic::Json;
using Supersonic::TransformComponent;

constexpr double kTwoPi = 6.283185307179586;

glm::dvec2 PxOf(entt::registry& registry, entt::entity body) {
    return Units::ToPixels(registry.get<TransformComponent>(body).position);
}

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
}

bool Alive(entt::registry& registry, entt::entity body) {
    return body != entt::null && registry.valid(body) && registry.all_of<TransformComponent>(body);
}

// The converter QUOTES the original's custom data - metadata/areaRadius = "64" -
// and Value::AsNumber reads that as readily as a bare one.
double MetaNumber(const Tscn::Node& node, const char* key, double fallback) {
    const Tscn::Value* value = node.Meta(key);
    double read = 0.0;
    if (value == nullptr || !value->AsNumber(read)) return fallback;
    return read;
}

std::string MetaText(const Tscn::Node& node, const char* key) {
    const Tscn::Value* value = node.Meta(key);
    if (value == nullptr || value->kind != Tscn::Value::Kind::String) return std::string();
    return value->text;
}

bool PositionOf(const Tscn::Node& node, glm::dvec2& out) {
    const Tscn::Value* position = node.Find("position");
    if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) return false;
    out = glm::dvec2(position->numbers[0], position->numbers[1]);
    return true;
}

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

    const Json::Value& limits = root["limits"];
    if (!limits.Has("min_radius_px") || !limits["min_radius_px"].IsNumber()) {
        error = path + ": limits.min_radius_px is missing or not a number";
        return false;
    }

    Rules read;
    read.minRadiusPx = limits["min_radius_px"].AsNumber();
    if (read.minRadiusPx <= 0.0) {
        error = path + ": limits.min_radius_px is above zero";
        return false;
    }

    out = std::move(read);
    return true;
}

void State::Tick(entt::registry& registry, entt::entity player, float dt) {
    const double seconds = static_cast<double>(dt);

    for (Field& field : fields) {
        // A ring a fireball destroyed stops swinging and stops killing. It stays
        // in the list, as a spent diamond does, so a suite can still name it.
        if (field.gone) continue;

        // The swing. angle advances by speed radians a second and wraps by taking
        // 2*PI off, as the original's does, and the displacement is a cosine of
        // amplitude stride - so the ring is a full stride from its node at angle
        // zero and the NODE is the centre of the travel.
        if (field.speedRadPerSec != 0.0) {
            field.angleRad += field.speedRadPerSec * seconds;
            if (field.angleRad > kTwoPi) field.angleRad -= kTwoPi;
            const double sign = field.speedRadPerSec < 0.0 ? -1.0 : 1.0;
            const double offset = std::cos(field.angleRad) * field.stridePx * sign;
            // Vertical always: metadata/direction is copied by the original and
            // read by neither of its callbacks, which pass a hardcoded flag.
            field.atPx = field.centrePx + glm::dvec2(0.0, offset);

            // And the sensor the converter gave it follows, so what is drawn and
            // what is triggered are where the ring is. It is a trigger rather
            // than a platform, so moving it pushes nothing about.
            if (Alive(registry, field.body)) {
                const glm::vec3 world = Units::ToWorld(field.atPx.x, field.atPx.y);
                auto& transform = registry.get<TransformComponent>(field.body);
                transform.position = glm::vec3(world.x, world.y, transform.position.z);
            }
        }

        // Then the kill, which is a poll and not an entry test: a player inside a
        // ring dies whether it walked in or was put there. The original tests
        // isCharacterDead first, which here is having claimed the death already.
        if (playerKilled || !Alive(registry, player)) continue;
        if (Squared(PxOf(registry, player) - field.atPx) >= field.radiusPx * field.radiusPx) continue;
        playerKilled = true;
        killedBy = field.name;
    }
}

std::size_t State::Standing() const {
    std::size_t left = 0;
    for (const Field& field : fields) {
        if (!field.gone) ++left;
    }
    return left;
}

entt::entity State::Destroy(const std::string& name, entt::registry& registry) {
    for (Field& field : fields) {
        if (field.name != name || field.gone) continue;
        field.gone = true;
        const entt::entity body = field.body;
        field.body = entt::null;
        if (Alive(registry, body)) {
            registry.destroy(body);
            return body;
        }
        return entt::null;
    }
    return entt::null;
}

const Field* State::FindField(const std::string& name) const {
    for (const Field& field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

std::size_t State::Moving() const {
    std::size_t moving = 0;
    for (const Field& field : fields) {
        if (field.speedRadPerSec != 0.0) ++moving;
    }
    return moving;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        // By role, and that is safe here: shock_agent and shock_agent.ent are two
        // spellings of ONE entity, where `pickup` covers two mechanisms and
        // Diamonds has to select by name.
        if (Roles::RoleOf(roles, node) != Roles::kShockField) continue;

        Field field;
        field.name = node.name;
        if (!PositionOf(node, field.centrePx)) {
            error = node.name + " is a shock agent with no position";
            return false;
        }
        field.atPx = field.centrePx;
        field.radiusPx = MetaNumber(node, "areaRadius", 0.0);
        field.speedRadPerSec = MetaNumber(node, "speed", 0.0);
        field.stridePx = MetaNumber(node, "stride", 0.0);
        field.burnable = MetaNumber(node, "burnable", 0.0) != 0.0;
        field.direction = MetaText(node, "direction");

        // Each of these would look like a level that works. A ring of no radius
        // kills nothing and is invisible in the inventory; a speed with no stride
        // is motion that never goes anywhere, and a stride with no speed is an
        // amplitude nothing drives - either way one of the two was lost.
        if (field.radiusPx < rules.minRadiusPx) {
            error = node.name + " is a shock agent whose areaRadius is under the floor";
            return false;
        }
        if ((field.speedRadPerSec != 0.0) != (field.stridePx != 0.0)) {
            error = node.name + " has a speed and a stride that disagree about whether it moves";
            return false;
        }

        // The 30 px trigger the converter gives every agent. Absent when a level
        // is started without its statics, which is not an error.
        const auto found = built.entities.find(node.name);
        if (found != built.entities.end()) field.body = found->second;

        out.fields.push_back(field);
    }

    return true;
}

} // namespace MagicPortals::Fields
