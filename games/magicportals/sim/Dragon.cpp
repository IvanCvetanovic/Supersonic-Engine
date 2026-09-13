#include "sim/Dragon.hpp"

#include "core/Components.hpp"
#include "core/DetMath.hpp"
#include "core/Json.hpp"
#include "sim/Shot.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace MagicPortals::Dragon {

namespace {

namespace Json = Supersonic::Json;

constexpr std::size_t kWaypoints = 6;

glm::dvec2 PxOf(const entt::registry& registry, entt::entity entity) {
    return Units::ToPixels(registry.get<Supersonic::TransformComponent>(entity).position);
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
    const auto numbers = [&](const char* group, const char* key, std::vector<double>& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsArray()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not an array";
            return false;
        }
        into.clear();
        for (const Json::Value& one : block[key].AsArray()) {
            if (!one.IsNumber()) {
                error = path + ": " + std::string(group) + "." + key + " holds something that is not a number";
                return false;
            }
            into.push_back(one.AsNumber());
        }
        return true;
    };

    Rules read;
    if (!string("flight", "entity", read.entityName)) return false;
    if (!string("flight", "take_off", read.takeOffName)) return false;
    if (!string("knight", "spawn", read.knightSpawnName)) return false;
    if (!number("flight", "advance_px_s", read.advancePxS)) return false;
    if (!number("flight", "climb_px_s", read.climbPxS)) return false;
    if (!number("flight", "sink_px_s", read.sinkPxS)) return false;
    if (!number("flight", "hover_ceiling_px", read.hoverCeilingPx)) return false;
    if (!number("flight", "bob_amplitude_px", read.bobAmplitudePx)) return false;
    if (!number("flight", "bob_frequency", read.bobFrequency)) return false;
    if (!number("fire", "interval_ms", read.fireIntervalMs)) return false;
    if (!number("fire", "muzzle_px", read.muzzlePx)) return false;

    if (!string("claw", "entity", read.clawName)) return false;
    if (!number("claw", "scan_ms", read.scanMs)) return false;
    if (!number("claw", "arm_px", read.armPx)) return false;
    if (!number("claw", "crush_px", read.crushPx)) return false;
    if (!number("claw", "lead_px", read.leadPx)) return false;
    if (!number("claw", "lift_px", read.liftPx)) return false;
    if (!number("claw", "bob_amplitude_px", read.clawBobAmplitudePx)) return false;
    if (!number("claw", "bob_frequency", read.clawBobFrequency)) return false;
    if (!numbers("claw", "hold_ms", read.holdMs)) return false;
    if (!numbers("claw", "angles_deg", read.anglesDeg)) return false;
    if (!number("camera", "view_width_px", read.viewWidthPx)) return false;

    {
        const Json::Value& path2 = root["claw"]["path_px"];
        if (!path2.IsArray()) {
            error = path + ": claw.path_px is missing or not an array";
            return false;
        }
        for (const Json::Value& one : path2.AsArray()) {
            if (!one.IsArray() || one.AsArray().size() != 2 || !one.AsArray()[0].IsNumber() ||
                !one.AsArray()[1].IsNumber()) {
                error = path + ": claw.path_px holds something that is not a pair of numbers";
                return false;
            }
            read.pathPx.emplace_back(one.AsArray()[0].AsNumber(), one.AsArray()[1].AsNumber());
        }
    }
    {
        const Json::Value& names = root["claw"]["crushes"];
        if (!names.IsArray()) {
            error = path + ": claw.crushes is missing or not an array";
            return false;
        }
        for (const Json::Value& one : names.AsArray()) {
            if (!one.IsString()) {
                error = path + ": claw.crushes holds something that is not a string";
                return false;
            }
            read.crushes.push_back(one.AsString());
        }
    }

    // SIX waypoints, and the count is asserted rather than tolerated. Five would
    // leave the claw resting mid-swing, because the callback parks it on
    // getNumWaypoints() - 2 and relies on the last two coinciding; dragon.json has
    // the decode. A table of the wrong length here is a data error, not a shrug.
    if (read.holdMs.size() != kWaypoints || read.pathPx.size() != kWaypoints ||
        read.anglesDeg.size() != kWaypoints) {
        error = path + ": claw.hold_ms, claw.path_px and claw.angles_deg each need " + std::to_string(kWaypoints) +
                " entries";
        return false;
    }
    if (read.crushes.empty()) {
        error = path + ": claw.crushes names what the claw may take and is empty";
        return false;
    }
    // A claw that never sweeps is a level whose floor never goes, and a dragon
    // that never advances never reaches take_off: both would look built and play
    // as scenery.
    if (read.scanMs <= 0.0) {
        error = path + ": claw.scan_ms is above zero";
        return false;
    }
    if (read.advancePxS <= 0.0) {
        error = path + ": flight.advance_px_s is above zero";
        return false;
    }
    if (read.fireIntervalMs <= 0.0) {
        error = path + ": fire.interval_ms is above zero";
        return false;
    }
    if (read.viewWidthPx <= 0.0) {
        error = path + ": camera.view_width_px is above zero";
        return false;
    }
    // The crush line must not sit outside the arm line, or the claw would destroy
    // a platform it never swung at.
    if (read.crushPx > read.armPx) {
        error = path + ": claw.crush_px is within claw.arm_px";
        return false;
    }

    out = std::move(read);
    return true;
}

bool Plays(const Rules& rules, const Tscn::Node& node) {
    const std::string name = Roles::EntityName(node);
    return name == rules.entityName || name == rules.knightSpawnName;
}

void Claw::Park(const Rules& rules) {
    // setCurrentWaypoint(getNumWaypoints() - 2). The last two waypoints coincide,
    // so this is the motionless idle pose rather than a point mid-swing.
    waypoint = static_cast<int>(rules.holdMs.size()) - 2;
    if (waypoint < 0) waypoint = 0;
    inWaypointMs = 0.0;
}

void Claw::Reset() {
    waypoint = 0;
    inWaypointMs = 0.0;
}

void Claw::Advance(const Rules& rules, double ms) {
    const int last = static_cast<int>(rules.holdMs.size()) - 1;
    if (last < 0) return;
    inWaypointMs += ms;
    while (waypoint < last && inWaypointMs >= rules.holdMs[static_cast<std::size_t>(waypoint)]) {
        inWaypointMs -= rules.holdMs[static_cast<std::size_t>(waypoint)];
        ++waypoint;
    }
    if (waypoint >= last) inWaypointMs = 0.0;
}

bool Claw::IsLastFrame(const Rules& rules) const {
    return waypoint + 1 >= static_cast<int>(rules.holdMs.size());
}

glm::dvec2 Claw::AtPx(const Rules& rules, const glm::dvec2& cameraLeftPx) const {
    const std::size_t at = static_cast<std::size_t>(waypoint);
    if (at >= rules.pathPx.size()) return cameraLeftPx;
    glm::dvec2 along = rules.pathPx[at];
    // LINEAR between the two, deliberately: dragon.json records smoothEnd and
    // smoothBeginning per waypoint and neither is applied, because the claw's
    // position has no gameplay consequence - both of its tests are against the
    // camera - and smoothBeginning is not decoded anywhere in this port.
    if (at + 1 < rules.pathPx.size() && rules.holdMs[at] > 0.0) {
        const double through = std::clamp(inWaypointMs / rules.holdMs[at], 0.0, 1.0);
        along += (rules.pathPx[at + 1] - rules.pathPx[at]) * through;
    }
    glm::dvec2 out = cameraLeftPx + along + glm::dvec2(rules.leadPx, heightPx - rules.liftPx);
    const double wave = static_cast<double>(
        Supersonic::DetMath::sin(static_cast<float>(cameraLeftPx.x * rules.clawBobFrequency)));
    out.y += wave * rules.clawBobAmplitudePx;
    return out;
}

double Claw::AngleDeg(const Rules& rules) const {
    const std::size_t at = static_cast<std::size_t>(waypoint);
    return at < rules.anglesDeg.size() ? rules.anglesDeg[at] : 0.0;
}

glm::dvec2 State::CameraLeftPx() const {
    // AutoCameraController::update: clamp(master.x, camMin.x, camMax.x), y = 0,
    // with camMin (0,0) and camMax the level's far corner less the screen.
    const double most = std::max(0.0, boundsX - viewWidthPx);
    return glm::dvec2(std::clamp(atPx.x, 0.0, most), 0.0);
}

State::Turn State::Tick(entt::registry& registry, entt::entity player, bool completed, float dt) {
    Turn turn;
    if (!present) return turn;

    const double ms = static_cast<double>(dt) * 1000.0;
    const double seconds = static_cast<double>(dt);
    elapsedMs += ms;

    // THE ONE-FRAME LAG IS THE ORIGINAL'S. Both branches below test virtualPos as
    // it was at the top of the tick while the adds write the live one, and the
    // drawn bob reads the same snapshot. dragon.json cites the instructions.
    const glm::dvec2 previous = virtualPx;
    virtualPx.x += rules.advancePxS * seconds;
    if (hasTakeOff && previous.x > takeOffPx.x) {
        virtualPx.y += rules.climbPxS * seconds;
        tookOff = true;
    } else if (previous.y < rules.hoverCeilingPx) {
        virtualPx.y += rules.sinkPxS * seconds;
    }
    const double wave =
        static_cast<double>(Supersonic::DetMath::sin(static_cast<float>(previous.x * rules.bobFrequency)));
    atPx = virtualPx + glm::dvec2(0.0, wave * rules.bobAmplitudePx);

    // THE CLAW. Its sweep is on a 250 ms clock of its own, not the frame's.
    if (hasClaw) {
        claw.Advance(rules, ms);
        claw.scanMs += ms;
        if (claw.scanMs > rules.scanMs) {
            const glm::dvec2 camera = CameraLeftPx();
            for (Crushable& one : crushables) {
                if (one.gone) continue;
                // The 94 px line re-arms the swing and remembers the height to
                // swing at; only when the swing has finished.
                if (one.atPx.x < camera.x + rules.armPx && claw.IsLastFrame(rules)) {
                    claw.heightPx = one.atPx.y;
                    claw.Reset();
                }
                // And the 64 px line destroys, every sweep, whatever the
                // animation is doing.
                if (one.atPx.x < camera.x + rules.crushPx) {
                    one.gone = true;
                    ++crushed;
                    turn.crushedNames.push_back(one.name);
                    if (registry.valid(one.entity)) turn.crushed.push_back(one.entity);
                    one.entity = entt::null;
                }
            }
            claw.scanMs = 0.0;
        }
    }

    // THE FIREBALLS. Before take-off only - ins 634-637, and dragon.json says why
    // that reading was checked twice - and only with the player in sight.
    if (!completed && !tookOff && elapsedMs > rules.fireIntervalMs && player != entt::null &&
        registry.valid(player)) {
        const glm::dvec2 playerPx = PxOf(registry, player);
        const glm::dvec2 along = playerPx - atPx;
        const double length = std::sqrt(along.x * along.x + along.y * along.y);
        if (length > 0.0) {
            // GetClosestContact(dragonPos -> charPos), and fire only if the
            // closest thing on that segment IS the character. Geometry between
            // the two stops the shot, which is the whole of the gate.
            //
            // ASKED THE OTHER WAY ROUND HERE, and it has to be. Shot::FirstBody
            // walks box, sphere and hull colliders; the player is a CAPSULE
            // (Player::Spawn), so it is not in that set and FirstBody can never
            // return it. Teaching it capsules would make every portal shot in the
            // game stop on the player, which is not what the original does.
            //
            // The segment ENDS at the player, so "the first thing hit is the
            // character" and "nothing solid lies before the endpoint" are the
            // same statement about the same world - and the second is the one
            // this port's primitive can answer. dragon.json records the swap.
            const std::optional<Shot::Hit> blocked = Shot::FirstBody(registry, atPx, playerPx, entt::null);
            if (!blocked.has_value() || blocked->along >= 1.0) {
                turn.fired = true;
                turn.aimPx = along / length;
                turn.firePx = atPx + turn.aimPx * rules.muzzlePx;
                ++fired;
                elapsedMs = 0.0;
            }
        }
    }
    return turn;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    out.viewWidthPx = rules.viewWidthPx;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string name = Roles::EntityName(node);
        const Tscn::Value* position = node.Find("position");
        const bool placed = position != nullptr && position->kind == Tscn::Value::Kind::Vector2;
        const glm::dvec2 atPx = placed ? glm::dvec2(position->numbers[0], position->numbers[1]) : glm::dvec2(0.0);

        if (name == rules.entityName) {
            if (!placed) {
                error = node.name + " is the dragon and has no position";
                return false;
            }
            out.present = true;
            out.name = node.name;
            out.virtualPx = atPx;
            out.atPx = atPx;
        } else if (name == rules.takeOffName && placed) {
            out.hasTakeOff = true;
            out.takeOffPx = atPx;
        } else if (name == rules.clawName && placed) {
            out.hasClaw = true;
        } else if (Roles::RoleOf(roles, node) == Roles::kLevelBounds && placed) {
            out.boundsX = atPx.x;
        }
    }
    if (!out.present) return true;

    // The level's far corner is what the camera clamps against, so a dragon level
    // without one would scroll past its own end.
    if (out.boundsX <= 0.0) {
        error = out.name + " is a dragon in a level with no level_bounds";
        return false;
    }
    // And with nothing to fly toward it would never take off, never stop firing,
    // and never end its part of the level.
    if (!out.hasTakeOff) {
        error = out.name + " is a dragon in a level with no " + rules.takeOffName;
        return false;
    }

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string name = Roles::EntityName(node);
        if (std::find(rules.crushes.begin(), rules.crushes.end(), name) == rules.crushes.end()) continue;
        const Tscn::Value* position = node.Find("position");
        if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) continue;
        const auto found = built.entities.find(node.name);
        // Skipped rather than refused: Game::Start's withStatics false builds no
        // static bodies at all, and there is then nothing for the claw to take.
        if (found == built.entities.end()) continue;

        Crushable one;
        one.name = node.name;
        one.entity = found->second;
        one.atPx = glm::dvec2(position->numbers[0], position->numbers[1]);
        out.crushables.push_back(one);
    }
    out.claw.Park(rules);
    return true;
}

} // namespace MagicPortals::Dragon
