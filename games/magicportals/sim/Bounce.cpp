#include "sim/Bounce.hpp"

#include "core/Components.hpp"
#include "core/DetMath.hpp"
#include "core/Json.hpp"
#include "sim/Mover.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace MagicPortals::Bounce {

namespace {

namespace Json = Supersonic::Json;

constexpr double kTwoPi = 6.28318530717958647692;

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
    const auto flag = [&](const char* group, const char* key, bool& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsBool()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a bool";
            return false;
        }
        into = block[key].AsBool();
        return true;
    };

    Rules read;
    if (!number("motion", "speed_radians_per_second", read.speedRadPerSec)) return false;
    if (!number("motion", "stride_px", read.stridePx)) return false;
    if (!number("motion", "frame_clamp_ms", read.frameClampMs)) return false;
    if (!flag("motion", "vertical", read.vertical)) return false;

    // Each of these would be a bouncer that looks built. No speed is a slab that
    // never moves; no stride is one that moves nowhere; and no clamp is the
    // original's guard gone, so one long frame would throw the slab across the
    // room - which is the whole reason STimeManager::unitsPerSecond has it.
    if (read.speedRadPerSec <= 0.0) {
        error = path + ": motion.speed_radians_per_second is above zero";
        return false;
    }
    if (read.stridePx <= 0.0) {
        error = path + ": motion.stride_px is above zero";
        return false;
    }
    if (read.frameClampMs <= 0.0) {
        error = path + ": motion.frame_clamp_ms is above zero";
        return false;
    }

    out = std::move(read);
    return true;
}

glm::dvec2 Bob::AtPx(const Rules& rules) const {
    // DetMath rather than the platform's cos, so a bob comes out the same on
    // every C runtime - the contract Mover::Oscillation::AtPx keeps with sin and
    // the transit's turn keeps in Portal.cpp. Both toolchains are verified on
    // every commit here, so this is a live difference and not a hypothetical.
    //
    // COSINE, so at angle 0 the slab stands a WHOLE STRIDE from its node: the
    // node is the middle of the bob and never where it starts. Mover's swing is
    // a sine of half its stride, which is a different curve from a different
    // reading of different data - Bounce.hpp lists all four differences.
    const double wave = static_cast<double>(Supersonic::DetMath::cos(static_cast<float>(angle)));
    const glm::dvec2 axis = rules.vertical ? glm::dvec2(0.0, 1.0) : glm::dvec2(1.0, 0.0);
    return originPx + axis * (wave * rules.stridePx);
}

void State::Tick(entt::registry& registry, float dt) {
    // min(200, elapsed), as STimeManager::unitsPerSecond takes it, and then to
    // seconds because the speed is in radians a second. m_factor is 1 in normal
    // play - it is the pause and slow-motion dial, and isPaused is m_factor == 0
    // - so it is not carried here.
    const double milliseconds = std::min(static_cast<double>(dt) * 1000.0, rules.frameClampMs);
    const double advance = rules.speedRadPerSec * (milliseconds / 1000.0);

    for (Bob& bob : bobs) {
        bob.angle += advance;
        // The original wraps by ADDING -2*PI once past 2*PI, one turn at a time.
        while (bob.angle >= kTwoPi) bob.angle -= kTwoPi;

        if (!registry.valid(bob.entity)) continue;
        const glm::dvec2 at = bob.AtPx(rules);
        // MoveKinematic, not a bare transform write: it sets the velocity the
        // contact solve reads, which is what makes the slab carry what stands on
        // it rather than slide out from under it.
        Mover::MoveKinematic(registry, bob.entity, Units::ToWorld(at.x, at.y), dt);
    }
}

const Bob* State::Find(const std::string& name) const {
    for (const Bob& bob : bobs) {
        if (bob.name == name) return &bob;
    }
    return nullptr;
}

bool Wire(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        if (Roles::RoleOf(roles, node) != Roles::kBouncer) continue;

        const Tscn::Value* position = node.Find("position");
        if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) {
            error = node.name + " is a bouncer with no position";
            return false;
        }

        const auto found = built.entities.find(node.name);
        if (found == built.entities.end()) {
            error = node.name + " was not built";
            return false;
        }

        Bob bob;
        bob.name = node.name;
        bob.entity = found->second;
        bob.originPx = glm::dvec2(position->numbers[0], position->numbers[1]);
        // Every bouncer in the game starts at the same phase: ETHCallback_bounce
        // passes startAngle 0, and linearMotion stamps it on first sight.
        bob.angle = 0.0;
        out.bobs.push_back(bob);
    }
    return true;
}

} // namespace MagicPortals::Bounce
