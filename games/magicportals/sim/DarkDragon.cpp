#include "sim/DarkDragon.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "sim/Shot.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace MagicPortals::DarkDragon {

namespace {

namespace Json = Supersonic::Json;

constexpr std::size_t kAppearWaypoints = 7;

glm::dvec2 PxOf(const entt::registry& registry, entt::entity entity) {
    return Units::ToPixels(registry.get<Supersonic::TransformComponent>(entity).position);
}

// One leg of a waypoint path: where it ends, and how long it takes to get there.
struct Leg {
    glm::dvec2 toPx{0.0};
    double ms = 0.0;
};

// Where a piecewise path has reached after `elapsed`, starting from `fromPx`.
// LINEAR between legs, deliberately: darkdragon.json records smoothBothSides,
// smoothBeginning and smoothEnd per waypoint and applies none of them, because
// what the position is for here is the blast test and the muzzle, and
// smoothBeginning is not decoded anywhere in this port.
glm::dvec2 Along(const glm::dvec2& fromPx, const std::vector<Leg>& legs, double elapsed) {
    glm::dvec2 at = fromPx;
    for (const Leg& leg : legs) {
        if (leg.ms <= 0.0) {
            at = leg.toPx;
            continue;
        }
        if (elapsed <= 0.0) return at;
        if (elapsed < leg.ms) return at + (leg.toPx - at) * (elapsed / leg.ms);
        elapsed -= leg.ms;
        at = leg.toPx;
    }
    return at;
}

double Total(const std::vector<Leg>& legs) {
    double ms = 0.0;
    for (const Leg& leg : legs) ms += leg.ms;
    return ms;
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
    const auto pair = [&](const char* group, const char* key, glm::dvec2& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsArray() || block[key].AsArray().size() != 2 ||
            !block[key].AsArray()[0].IsNumber() || !block[key].AsArray()[1].IsNumber()) {
            error = path + ": " + std::string(group) + "." + key + " is a pair of numbers";
            return false;
        }
        into = glm::dvec2(block[key].AsArray()[0].AsNumber(), block[key].AsArray()[1].AsNumber());
        return true;
    };

    Rules read;
    if (!string("summon", "spawn", read.spawnName)) return false;
    if (!string("summon", "entity", read.entityName)) return false;
    if (!string("summon", "wall", read.wallName)) return false;
    if (!number("summon", "arm_ms", read.armMs)) return false;
    if (!number("summon", "summon_ms", read.summonMs)) return false;

    if (!number("body", "radius_px", read.radiusPx)) return false;
    {
        const Json::Value& block = root["body"];
        if (!block.Has("max_hp") || !block["max_hp"].IsNumber()) {
            error = path + ": body.max_hp is missing or not a number";
            return false;
        }
        read.maxHp = static_cast<int>(block["max_hp"].AsNumber());
    }

    if (!number("fight", "fire_interval_ms", read.fireIntervalMs)) return false;
    if (!number("fight", "fire_interval_step_ms", read.fireIntervalStepMs)) return false;
    if (!number("fight", "muzzle_px", read.muzzlePx)) return false;
    if (!string("fight", "shout", read.shoutName)) return false;
    if (!string("fight", "fighting", read.fightingName)) return false;

    if (!pair("appearing", "shout_move_px", read.shoutMovePx)) return false;
    {
        const Json::Value& holds = root["appearing"]["hold_ms"];
        if (!holds.IsArray()) {
            error = path + ": appearing.hold_ms is missing or not an array";
            return false;
        }
        for (const Json::Value& one : holds.AsArray()) {
            if (!one.IsNumber()) {
                error = path + ": appearing.hold_ms holds something that is not a number";
                return false;
            }
            read.appearHoldMs.push_back(one.AsNumber());
        }
    }

    if (!number("damage", "first_ms", read.damageFirstMs)) return false;
    if (!number("damage", "flash_ms", read.damageFlashMs)) return false;
    if (!pair("damage", "shout_move_px", read.damageMovePx)) return false;
    if (!string("damage", "torch_entity", read.torchEntity)) return false;
    {
        const Json::Value& block = root["damage"];
        if (!block.Has("flashes") || !block["flashes"].IsNumber()) {
            error = path + ": damage.flashes is missing or not a number";
            return false;
        }
        read.damageFlashes = static_cast<int>(block["flashes"].AsNumber());
    }

    if (!number("dying", "hover_ms", read.dyingHoverMs)) return false;
    if (!number("dying", "drift_ms", read.dyingDriftMs)) return false;
    if (!pair("dying", "fall_move_px", read.fallMovePx)) return false;
    if (!number("dying", "fall_px", read.fallPx)) return false;
    if (!string("dying", "key_pos", read.keyPosName)) return false;
    if (!string("dying", "platform_pos", read.platformPosName)) return false;
    if (!string("dying", "key_colour", read.keyColour)) return false;
    if (!string("dying", "platform_entity", read.platformEntity)) return false;

    // SEVEN waypoints, asserted rather than tolerated: the appearing path is what
    // carries the boss from off the left edge to where it fights, and a table of
    // the wrong length would leave it somewhere no decode says it should be.
    if (read.appearHoldMs.size() != kAppearWaypoints) {
        error = path + ": appearing.hold_ms needs " + std::to_string(kAppearWaypoints) + " entries";
        return false;
    }
    // Each of these would be a boss that looks built. No hp is one that dies to
    // the first blast; no interval is one that fires every tick; no radius is one
    // a blast can never reach, which is a fight that cannot be won.
    if (read.maxHp <= 0) {
        error = path + ": body.max_hp is above zero";
        return false;
    }
    if (read.radiusPx <= 0.0) {
        error = path + ": body.radius_px is above zero";
        return false;
    }
    if (read.fireIntervalMs <= 0.0) {
        error = path + ": fight.fire_interval_ms is above zero";
        return false;
    }
    // And a step that outruns the interval would make the last wound give it a
    // zero or negative stride, which is every tick.
    if (read.fireIntervalStepMs < 0.0 ||
        read.fireIntervalStepMs * (read.maxHp - 1) >= read.fireIntervalMs) {
        error = path + ": fight.fire_interval_step_ms leaves an interval above zero at 1 hp";
        return false;
    }
    if (read.armMs < 0.0 || read.summonMs < 0.0) {
        error = path + ": summon.arm_ms and summon.summon_ms are not negative";
        return false;
    }

    out = std::move(read);
    return true;
}

bool Plays(const Rules& rules, const Tscn::Node& node) {
    return Roles::EntityName(node) == rules.spawnName;
}

double State::FireIntervalMs() const {
    // Tested against the LIVE hp every tick, not stamped when it is wounded.
    double interval = rules.fireIntervalMs;
    for (int at = rules.maxHp - 1; at >= 1; --at) {
        if (hp <= at) interval -= rules.fireIntervalStepMs;
    }
    return interval;
}

State::Turn State::Tick(entt::registry& registry, entt::entity player, bool torchLit, const glm::dvec2& torchPx,
                        bool hasTorch, bool completed, float dt) {
    Turn turn;
    if (!present || gone) return turn;

    const double ms = static_cast<double>(dt) * 1000.0;

    // THE SUMMON, and the marker's own two arms. `destroy` deletes nothing: it
    // only records that the light has been seen.
    if (!alive) {
        if (!armed) {
            if (!torchLit) return turn;
            armingMs += ms;
            if (armingMs <= rules.armMs) return turn;
            armed = true;
            summoningMs = 0.0;
            return turn;
        }
        summoningMs += ms;
        if (summoningMs <= rules.summonMs || completed) return turn;

        alive = true;
        summoned = true;
        phase = Phase::Appearing;
        phaseMs = 0.0;
        elapsedMs = 0.0;
        hp = rules.maxHp;
        atPx = spawnPx;
        // breakDarkDragonWall, on the tick it arrives.
        turn.brokeWall = true;
        return turn;
    }

    phaseMs += ms;
    elapsedMs += ms;

    const glm::dvec2 shoutMovedPx = shoutPx + rules.shoutMovePx;

    switch (phase) {
    case Phase::Appearing: {
        const std::vector<Leg> legs{{shoutPx, rules.appearHoldMs[0]},       {shoutPx, rules.appearHoldMs[1]},
                                    {shoutMovedPx, rules.appearHoldMs[2]},  {shoutMovedPx, rules.appearHoldMs[3]},
                                    {shoutMovedPx, rules.appearHoldMs[4]},  {fightingPx, rules.appearHoldMs[5]},
                                    {fightingPx, rules.appearHoldMs[6]}};
        atPx = Along(spawnPx, legs, phaseMs);
        if (phaseMs >= Total(legs)) {
            phase = Phase::Fighting;
            phaseMs = 0.0;
            elapsedMs = 0.0;
        }
        break;
    }

    case Phase::Fighting: {
        atPx = fightingPx;
        // THE WOUND IS READ FIRST, so a blast that reached it this tick is acted
        // on this tick rather than after one more shot. isBurned then healBurn.
        if (burned) {
            burned = false;
            ++hits;
            --hp;
            phase = hp <= 0 ? Phase::Dying : Phase::Damage;
            phaseMs = 0.0;
            elapsedMs = 0.0;
            // Every portal the player has placed, taken away.
            turn.killedPortals = true;
            break;
        }
        if (completed) break;
        if (elapsedMs <= FireIntervalMs()) break;
        if (player == entt::null || !registry.valid(player)) break;

        const glm::dvec2 playerPx = PxOf(registry, player);
        const glm::dvec2 along = playerPx - atPx;
        const double length = std::sqrt(along.x * along.x + along.y * along.y);
        if (length <= 0.0) break;
        // The same sight gate as level31a's dragon, asked the same way round and
        // for the same reason: Shot::FirstBody cannot return a capsule, so the
        // question is "nothing solid before the endpoint". dragon.json decodes it.
        const std::optional<Shot::Hit> blocked = Shot::FirstBody(registry, atPx, playerPx, entt::null);
        if (blocked.has_value() && blocked->along < 1.0) break;

        turn.fired = true;
        turn.aimPx = along / length;
        turn.firePx = atPx + turn.aimPx * rules.muzzlePx;
        ++fired;
        elapsedMs = 0.0;
        break;
    }

    case Phase::Damage: {
        const glm::dvec2 retreatPx = shoutPx + rules.damageMovePx;
        std::vector<Leg> legs;
        legs.push_back({retreatPx, rules.damageFirstMs});
        for (int flash = 0; flash + 1 < rules.damageFlashes; ++flash) {
            legs.push_back({retreatPx, rules.damageFlashMs});
        }
        legs.push_back({fightingPx, rules.damageFlashMs});
        atPx = Along(fightingPx, legs, phaseMs);

        if (phaseMs >= Total(legs)) {
            // AT THE LAST WAYPOINT IT SHOOTS OUT THE TORCH, which in a `darkest`
            // level is the light and, through Torch, the wall that light cleared.
            if (hasTorch) {
                const glm::dvec2 along = torchPx - atPx;
                const double length = std::sqrt(along.x * along.x + along.y * along.y);
                if (length > 0.0) {
                    turn.firedAtTorch = true;
                    turn.fired = true;
                    turn.aimPx = along / length;
                    turn.firePx = atPx + turn.aimPx * rules.muzzlePx;
                    ++fired;
                }
            }
            phase = Phase::Fighting;
            phaseMs = 0.0;
            elapsedMs = 0.0;
        }
        break;
    }

    case Phase::Dying: {
        const glm::dvec2 restPx = shoutPx + rules.fallMovePx;
        const std::vector<Leg> legs{{restPx, rules.dyingHoverMs},
                                    {restPx + glm::dvec2(0.0, rules.fallPx), rules.dyingDriftMs}};
        atPx = Along(fightingPx, legs, phaseMs);
        if (phaseMs >= Total(legs)) {
            gone = true;
            alive = false;
            // The key is the level's only one, and the lock its fight stands for.
            if (hasKeyPos) {
                turn.droppedKey = true;
                turn.keyPx = keyPosPx;
            }
            // And the platform, added at the same waypoint and from the same
            // .ent. Without it level31c's key sits in a hole with no floor: the
            // light wall that floored x 1..127 is `breakable` and lighting the
            // torch - which is what summons this boss - takes it away.
            if (hasPlatformPos) {
                turn.droppedPlatform = true;
                turn.platformPx = platformPosPx;
            }
        }
        break;
    }
    }
    return turn;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string name = Roles::EntityName(node);
        const Tscn::Value* position = node.Find("position");
        const bool placed = position != nullptr && position->kind == Tscn::Value::Kind::Vector2;
        const glm::dvec2 atPx = placed ? glm::dvec2(position->numbers[0], position->numbers[1]) : glm::dvec2(0.0);

        // By entity name AND by role: `dark_dragon_spawn` is what the level calls
        // it, and `boss_spawn` is what the role table makes of it. Asking both
        // means a level that renamed one without the other is not half found.
        if (name == rules.spawnName && Roles::RoleOf(roles, node) == Roles::kBossSpawn) {
            if (!placed) {
                error = node.name + " is a dark dragon spawn with no position";
                return false;
            }
            out.present = true;
            out.name = node.name;
            out.spawnPx = atPx;
            out.atPx = atPx;
        } else if (name == rules.shoutName && placed) {
            out.shoutPx = atPx;
        } else if (name == rules.fightingName && placed) {
            out.fightingPx = atPx;
        } else if (name == rules.keyPosName && placed) {
            out.hasKeyPos = true;
            out.keyPosPx = atPx;
        } else if (name == rules.platformPosName && placed) {
            out.hasPlatformPos = true;
            out.platformPosPx = atPx;
        } else if (name == rules.platformEntity && out.platformNode.empty()) {
            // A sibling of the one the death adds, kept as its template: the
            // entity has no .png of its own, so both its box and its picture
            // come from a node the level already places.
            out.platformNode = node.name;
        } else if (name == rules.wallName) {
            // By NODE, because that is how Demolish holds it.
            out.wallNode = node.name;
        }
    }
    if (!out.present) return true;

    // Without these two it has nowhere to appear from and nowhere to fight, which
    // is a boss that hovers at the origin rather than one that is built.
    if (out.shoutPx == glm::dvec2(0.0) || out.fightingPx == glm::dvec2(0.0)) {
        error = out.name + " is a dark dragon in a level missing " + rules.shoutName + " or " + rules.fightingName;
        return false;
    }
    // And without a key it drops nothing, in a level whose only lock its fight is.
    if (!out.hasKeyPos) {
        error = out.name + " is a dark dragon in a level with no " + rules.keyPosName;
        return false;
    }
    return true;
}

} // namespace MagicPortals::DarkDragon
