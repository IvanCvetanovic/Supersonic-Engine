#include "sim/Torch.hpp"

#include "core/Json.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace MagicPortals::Torch {

namespace {

namespace Json = Supersonic::Json;

constexpr double kPi = 3.14159265358979323846;

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
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

    const auto number = [&](const char* group, const char* key, double& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsNumber()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a number";
            return false;
        }
        into = block[key].AsNumber();
        return true;
    };
    const auto string = [&](const char* group, const char* key, std::string& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsString()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a string";
            return false;
        }
        into = block[key].AsString();
        return true;
    };

    Rules read;
    {
        const Json::Value& block = root["torch"];
        if (!block.Has("entity_names") || !block["entity_names"].IsArray()) {
            error = path + ": torch.entity_names is missing or not an array";
            return false;
        }
        for (const Json::Value& one : block["entity_names"].AsArray()) {
            if (!one.IsString()) {
                error = path + ": torch.entity_names holds a value that is not a string";
                return false;
            }
            read.torchNames.push_back(one.AsString());
        }
    }
    if (!number("torch", "reach_px", read.reachPx)) return false;
    if (!string("wall", "entity_name", read.wallName)) return false;
    if (!number("wall", "fade_ms", read.wallFadeMs)) return false;
    if (!number("signal", "orbit_px", read.orbitPx)) return false;
    if (!number("signal", "spin_deg_s", read.spinDegPerSec)) return false;

    // The signal polls with the SAME scale(24) the torch does, and this module
    // keeps one number for both. Read the second and refuse a file where they
    // disagree, rather than carry a key that nothing reads - a value silently
    // ignored is worse than one that is absent.
    double signalReachPx = 0.0;
    if (!number("signal", "reach_px", signalReachPx)) return false;
    if (signalReachPx != read.reachPx) {
        error = path + ": signal.reach_px and torch.reach_px are the same poll and must agree";
        return false;
    }

    // Each of these would look like a mechanism that works. No reach is a switch
    // nothing can ever work; no orbit is a signal sitting on the torch it came
    // from, which is the one thing that makes it hard to hit; no spin is a signal
    // that never moves; and no fade would take the wall away on the frame it was
    // flagged, which is exactly when the signal must still be able to find it.
    if (read.torchNames.empty()) {
        error = path + ": torch.entity_names names at least one entity";
        return false;
    }
    if (read.wallName.empty()) {
        error = path + ": wall.entity_name names an entity";
        return false;
    }
    if (read.reachPx <= 0.0) {
        error = path + ": torch.reach_px is above zero";
        return false;
    }
    if (read.orbitPx <= 0.0 || read.spinDegPerSec <= 0.0) {
        error = path + ": signal.orbit_px and signal.spin_deg_s are above zero";
        return false;
    }
    if (read.wallFadeMs <= 0.0) {
        error = path + ": wall.fade_ms is above zero";
        return false;
    }

    out = std::move(read);
    return true;
}

State::Turn State::Tick(const std::optional<glm::dvec2>& flightPx, const std::vector<glm::dvec2>& fireballPx,
                        float dt) {
    Turn turn;
    const double ms = static_cast<double>(dt) * 1000.0;
    const double seconds = static_cast<double>(dt);

    // The wall on its way out. destroy() only FLAGS it in the original; the
    // wall's own callback counts to 1500 ms and only then deletes itself. That
    // delay is why a signal can appear at all, so it is modelled rather than
    // collapsed into the frame the torch was lit.
    if (wall.present && wall.going && !wall.gone) {
        wall.goingMs += ms;
        if (wall.goingMs >= rules.wallFadeMs) {
            wall.going = false;
            wall.gone = true;
            ++wallsGone;
            turn.takeWallAway = true;
        }
    }

    // The signal circles the torch it came from rather than sitting on it, which
    // is the whole of what makes putting a torch back out a shot worth aiming.
    if (signal.present) {
        signal.angleDeg += rules.spinDegPerSec * seconds;
        while (signal.angleDeg >= 360.0) signal.angleDeg -= 360.0;
        const double radians = signal.angleDeg * kPi / 180.0;
        signal.atPx = signal.originPx + glm::dvec2(-std::cos(radians), std::sin(radians)) * rules.orbitPx;
    }

    // What is near a point: the player's shot first, then the fireballs in the
    // order Game holds them. -2 is nothing, -1 the flight, 0 and up a fireball.
    const double reach2 = rules.reachPx * rules.reachPx;
    const auto shotNear = [&](const glm::dvec2& atPx) -> int {
        if (flightPx.has_value() && Squared(*flightPx - atPx) < reach2) return -1;
        for (std::size_t i = 0; i < fireballPx.size(); ++i) {
            if (Squared(fireballPx[i] - atPx) < reach2) return static_cast<int>(i);
        }
        return -2;
    };
    const auto spend = [&](int shot) {
        if (shot == -1) {
            turn.spentFlight = true;
        } else {
            turn.spentFireball = shot;
        }
    };

    // ONE switch per tick. The original's poll takes the first projectile it
    // finds and killProjectile's it, and one shot cannot work two switches.
    if (signal.present) {
        const int shot = shotNear(signal.atPx);
        if (shot != -2) {
            for (Light& light : lights) {
                if (light.name != signal.fromTorch) continue;
                light.lit = false;
                // The original adds a FRESH light_off.ent carrying `switch` = 1.
                // Here the torch was never removed, so the flag is all there is
                // of that, and nothing in the decode reads it either.
                light.switched = true;
                break;
            }
            signal = Signal{};
            ++putOut;
            if (wall.present) {
                wall.gone = false;
                wall.going = false;
                wall.goingMs = 0.0;
                ++wallsBack;
                turn.putWallBack = true;
            }
            spend(shot);
            return turn;
        }
    }

    for (Light& light : lights) {
        if (light.lit) continue;
        const int shot = shotNear(light.atPx);
        if (shot == -2) continue;

        light.lit = true;
        ++lit;
        spend(shot);

        // seekLightWallToDestroy, then addFireSignalIfNecessary - and both ask
        // whether a light_wall.ent can still be FOUND, which a flagged one can.
        if (wall.present && !wall.gone) {
            if (!wall.going) {
                wall.going = true;
                wall.goingMs = 0.0;
            }
            // Capped at one. The original would add a second signal for a second
            // torch lit inside the wall's 1500 ms window, and no level in the game
            // can ask for that - every one of the ten places a single torch. The
            // cap is here so the rule is the code's rather than the data's.
            if (!signal.present) {
                signal.present = true;
                signal.originPx = light.atPx;
                signal.angleDeg = 0.0;
                signal.atPx = light.atPx + glm::dvec2(-rules.orbitPx, 0.0);
                signal.fromTorch = light.name;
            }
        }
        break;
    }

    return turn;
}

const Light* State::Find(const std::string& name) const {
    for (const Light& light : lights) {
        if (light.name == name) return &light;
    }
    return nullptr;
}

std::size_t State::Unlit() const {
    std::size_t left = 0;
    for (const Light& light : lights) {
        if (!light.lit) ++left;
    }
    return left;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string entity = Roles::EntityName(node);

        // The wall is in NO role table at all - Roles::RoleOf answers empty for
        // it, and IsPorted answers true - so it has never once been reported
        // inert. It is found by name here for that reason.
        if (entity == rules.wallName) {
            glm::dvec2 atPx(0.0, 0.0);
            if (!PositionOf(node, atPx)) {
                error = node.name + " is a light wall with no position";
                return false;
            }
            out.wall.present = true;
            out.wall.name = node.name;
            continue;
        }

        bool isTorch = false;
        for (const std::string& want : rules.torchNames) {
            if (entity == want) {
                isTorch = true;
                break;
            }
        }
        if (!isTorch) continue;

        Light light;
        light.name = node.name;
        if (!PositionOf(node, light.atPx)) {
            error = node.name + " is a torch with no position";
            return false;
        }
        out.lights.push_back(light);
    }

    (void)roles;
    return true;
}

} // namespace MagicPortals::Torch
