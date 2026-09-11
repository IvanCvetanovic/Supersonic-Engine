#include "sim/Mover.hpp"

#include "core/Components.hpp"
#include "core/DetMath.hpp"
#include "core/Json.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace MagicPortals::Mover {

void MoveKinematic(entt::registry& registry, entt::entity entity, const glm::vec3& target, float dt) {
    auto& transform = registry.get<Supersonic::TransformComponent>(entity);
    auto& rigid = registry.get<Supersonic::RigidBodyComponent>(entity);
    rigid.velocity = dt > 0.0f ? (target - transform.position) / dt : glm::vec3(0.0f);
    transform.position = target;
}

void Door::Tick(entt::registry& registry, entt::entity entity, float dt) {
    const float step = dt / durationS;
    progress = std::clamp(progress + (opening ? step : -step), 0.0f, 1.0f);
    MoveKinematic(registry, entity, glm::mix(closed, open, progress), dt);
}

bool DoorFromNode(const Tscn::Node& node, Door& out, std::string& error) {
    const Tscn::Value* stride = node.Meta("stride");
    double milliseconds = 0.0;
    if (stride == nullptr || !stride->AsNumber(milliseconds)) {
        error = node.name + " has no stride";
        return false;
    }
    const Tscn::Value* position = node.Find("position");
    glm::dvec2 at(0.0);
    if (position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
        at = glm::dvec2(position->numbers[0], position->numbers[1]);
    }

    out = Door{};
    out.closed = Units::ToWorld(at.x, at.y);
    // Up the screen, which is -y in the remake's pixels (behaviours.gd:145).
    out.open = Units::ToWorld(at.x, at.y - kDoorRisePx);
    // Floored at 10 ms, as behaviours.gd floors it (:147).
    out.durationS = static_cast<float>(std::max(milliseconds / 1000.0, 0.01));
    return true;
}

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
    const auto number = [&](const char* block, const char* key, double& to) {
        if (!root.IsObject() || !root.Has(block) || !root[block].IsObject() || !root[block].Has(key) ||
            !root[block][key].IsNumber()) {
            error = path + ": " + block + "." + key + " is missing or not a number";
            return false;
        }
        to = root[block][key].AsNumber();
        return true;
    };
    Rules read;
    if (!number("oscillation", "rate_scale", read.oscillationRateScale) ||
        !number("lift", "speed_px_s", read.liftSpeedPx)) {
        return false;
    }
    out = read;
    return true;
}

glm::dvec2 Oscillation::AtPx() const {
    // DetMath rather than the platform's sin, so that a swing comes out the same
    // on every C runtime, as the transit's turn does (Portal.cpp).
    const double wave = static_cast<double>(Supersonic::DetMath::sin(static_cast<float>(rateScale * speed * t)));
    return originPx + axis * (wave * stridePx * 0.5);
}

bool OscillationFromNode(const Tscn::Node& node, const glm::dvec2& axis, double rateScale, Oscillation& out,
                         std::string& error) {
    Oscillation read;
    const Tscn::Value* speed = node.Meta("speed");
    const Tscn::Value* stride = node.Meta("stride");
    if (speed == nullptr || !speed->AsNumber(read.speed) || stride == nullptr || !stride->AsNumber(read.stridePx)) {
        error = node.name + " has no speed and stride to swing by";
        return false;
    }
    const Tscn::Value* position = node.Find("position");
    if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) {
        error = node.name + " has no position";
        return false;
    }
    read.originPx = glm::dvec2(position->numbers[0], position->numbers[1]);
    read.axis = axis;
    read.rateScale = rateScale;
    out = read;
    return true;
}

void Shuttle::Advance(double dt) {
    const glm::dvec2 target = forward ? bPx : aPx;
    const glm::dvec2 gap = target - atPx;
    const double distance = glm::length(gap);
    const double step = speedPx * dt;
    // Vector2.move_toward, then is_equal_approx: arriving turns it round.
    if (distance <= step) {
        atPx = target;
        forward = !forward;
    } else {
        atPx += gap / distance * step;
    }
}

void Movers::Tick(entt::registry& registry, float dt) {
    for (Platform& platform : platforms) {
        platform.motion.t += dt;
        const glm::dvec2 at = platform.motion.AtPx();
        MoveKinematic(registry, platform.entity, Units::ToWorld(at.x, at.y), dt);
    }
    for (Lift& lift : lifts) {
        lift.motion.Advance(dt);
        MoveKinematic(registry, lift.entity, Units::ToWorld(lift.motion.atPx.x, lift.motion.atPx.y), dt);
    }
}

const Platform* Movers::FindPlatform(const std::string& name) const {
    for (const Platform& platform : platforms) {
        if (platform.name == name) return &platform;
    }
    return nullptr;
}

const Lift* Movers::FindLift(const std::string& name) const {
    for (const Lift& lift : lifts) {
        if (lift.name == name) return &lift;
    }
    return nullptr;
}

bool Wire(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          Movers& out, std::string& error) {
    out = Movers{};
    const auto positionOf = [](const Tscn::Node& node, glm::dvec2& to) {
        const Tscn::Value* position = node.Find("position");
        if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) return false;
        to = glm::dvec2(position->numbers[0], position->numbers[1]);
        return true;
    };
    const auto entityOf = [&](const Tscn::Node& node, entt::entity& to) {
        const auto found = built.entities.find(node.name);
        if (found == built.entities.end()) {
            error = node.name + " was not built";
            return false;
        }
        to = found->second;
        return true;
    };
    // level_runtime.gd:157-163: a marker, by the entity name a lift gives it.
    const auto markerAt = [&](const std::string& name, const glm::dvec2& fallback) {
        if (name.empty()) return fallback;
        for (const Tscn::Node& node : scene.nodes) {
            glm::dvec2 at(0.0);
            if (node.parent == "." && Roles::RoleOf(roles, node) == "lift_marker" && Roles::EntityName(node) == name &&
                positionOf(node, at)) {
                return at;
            }
        }
        return fallback;
    };

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        if (role == Roles::kMovingPlatform) {
            Platform platform;
            platform.name = node.name;
            // Vertical, always (behaviours.gd:105-108).
            if (!OscillationFromNode(node, glm::dvec2(0.0, 1.0), rules.oscillationRateScale, platform.motion, error) ||
                !entityOf(node, platform.entity)) {
                return false;
            }
            out.platforms.push_back(platform);
        } else if (role == Roles::kLift) {
            Lift lift;
            lift.name = node.name;
            glm::dvec2 at(0.0);
            if (!positionOf(node, at)) {
                error = node.name + " has no position";
                return false;
            }
            const auto nameOf = [&node](const char* key) {
                const Tscn::Value* value = node.Meta(key);
                return value != nullptr && value->kind == Tscn::Value::Kind::String ? value->text : std::string();
            };
            lift.motion.aPx = markerAt(nameOf("a"), at);
            lift.motion.bPx = markerAt(nameOf("b"), at);
            lift.motion.atPx = at;
            lift.motion.speedPx = rules.liftSpeedPx;
            if (const Tscn::Value* speed = node.Meta("speed"); speed != nullptr && !speed->AsNumber(lift.motion.speedPx)) {
                error = node.name + "'s speed is not a number";
                return false;
            }
            if (!entityOf(node, lift.entity)) return false;
            out.lifts.push_back(lift);
        }
    }
    return true;
}

} // namespace MagicPortals::Mover
