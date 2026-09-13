#include "sim/Ghost.hpp"

#include "sim/Units.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace MagicPortals::Ghost {

namespace {

namespace Json = Supersonic::Json;
using Supersonic::TransformComponent;

glm::dvec2 PxOf(entt::registry& registry, entt::entity body) {
    return Units::ToPixels(registry.get<TransformComponent>(body).position);
}

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
}

double Length(const glm::dvec2& v) {
    return std::sqrt(Squared(v));
}

// NOT called Alive: State has a member of that name, and class scope would hide
// this one inside Tick. Diamonds.cpp made exactly that mistake with Standing.
bool BodyValid(entt::registry& registry, entt::entity body) {
    return body != entt::null && registry.valid(body) && registry.all_of<TransformComponent>(body);
}

bool PositionOf(const Tscn::Node& node, glm::dvec2& out) {
    const Tscn::Value* position = node.Find("position");
    if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) return false;
    out = glm::dvec2(position->numbers[0], position->numbers[1]);
    return true;
}

// The converter QUOTES the original's custom data - metadata/holdTime = "4000" -
// and Tscn::Value::AsNumber reads that as readily as a bare one.
double MetaNumber(const Tscn::Node& node, const char* key, double fallback) {
    const Tscn::Value* value = node.Meta(key);
    double read = 0.0;
    if (value == nullptr || !value->AsNumber(read)) return fallback;
    return read;
}

// One placed waypoint, before the prefix has claimed it - Minions::Find's shape,
// because the patrol this boss hands its escort is an ordinary minion patrol.
struct Placed {
    std::string entityName;
    glm::dvec2 atPx{0.0, 0.0};
    double holdMs = 0.0;
};

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
    const auto text_ = [&](const char* group, const char* key, std::string& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsString()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a string";
            return false;
        }
        into = block[key].AsString();
        return true;
    };

    Rules read;
    double maxHp = 0.0;
    if (!text_("anchor", "entity_name", read.anchorName)) return false;
    if (!number("body", "max_hp", maxHp)) return false;
    if (!number("body", "radius_px", read.radiusPx)) return false;
    if (!number("appear", "total_ms", read.appearMs)) return false;
    if (!number("appear", "seed_elapsed_ms", read.seedElapsedMs)) return false;
    if (!number("shooting", "interval_ms", read.intervalMs)) return false;
    if (!number("shooting", "muzzle_x", read.muzzlePx.x)) return false;
    if (!number("shooting", "muzzle_y", read.muzzlePx.y)) return false;
    if (!text_("shooting", "red_line_entity_name", read.redLineName)) return false;
    if (!text_("shooting", "diamond_spawn_entity_name", read.diamondSpawnName)) return false;
    if (!number("shot", "speed_px_s", read.shotSpeedPxS)) return false;
    if (!number("shot", "hit_px", read.shotHitPx)) return false;
    if (!number("damage", "to_rage_ms", read.toRageMs)) return false;
    if (!number("rage", "summon_window_ms", read.summonWindowMs)) return false;
    if (!text_("rage", "waypoint_prefix", read.waypointPrefix)) return false;
    if (!text_("rage", "anchor_entity_name", read.summonAnchorName)) return false;
    if (!number("death", "burst_ms", read.burstMs)) return false;
    if (!text_("death", "key_colour", read.keyColour)) return false;
    read.maxHp = static_cast<int>(maxHp);

    // Each of these would look like a boss that works. No hp is a boss that is
    // already dead; no radius is one nothing can ever reach; no interval divides
    // the fight into an infinity of shots in one tick; and an empty prefix would
    // resolve every waypoint in the level into its escort's patrol.
    if (read.maxHp <= 0) {
        error = path + ": body.max_hp is above zero";
        return false;
    }
    if (read.radiusPx <= 0.0) {
        error = path + ": body.radius_px is above zero";
        return false;
    }
    if (read.intervalMs <= 0.0) {
        error = path + ": shooting.interval_ms is above zero";
        return false;
    }
    if (read.shotSpeedPxS <= 0.0 || read.shotHitPx <= 0.0) {
        error = path + ": shot.speed_px_s and shot.hit_px are above zero";
        return false;
    }
    if (read.anchorName.empty() || read.waypointPrefix.empty() || read.summonAnchorName.empty()) {
        error = path + ": anchor, rage.waypoint_prefix and rage.anchor_entity_name all name something";
        return false;
    }
    if (read.keyColour.empty()) {
        error = path + ": death.key_colour is the colour key.ent's own custom data carries";
        return false;
    }

    out = std::move(read);
    return true;
}

bool Plays(const Rules& rules, const Tscn::Node& node) {
    // By NAME. Boss::Plays reads an adder's metadata/entity, and ghost_pos has no
    // such field - the entity name is the whole of it. ghost.json says why.
    return Roles::EntityName(node) == rules.anchorName;
}

State::Turn State::Tick(entt::registry& registry, entt::entity player, std::size_t minionsStanding, float dt) {
    Turn turn;
    if (!present) return turn;

    const double ms = static_cast<double>(dt) * 1000.0;
    const double seconds = static_cast<double>(dt);
    const bool playerThere = BodyValid(registry, player);
    const glm::dvec2 playerPx = playerThere ? PxOf(registry, player) : glm::dvec2(0.0);

    // The shots fly first, so one fired last tick is judged where this step left
    // the player - the same moment every other module judges a position at.
    {
        const double hit2 = rules.shotHitPx * rules.shotHitPx;
        std::vector<Shot> living;
        living.reserve(shots.size());
        for (Shot& shot : shots) {
            shot.atPx += shot.velocityPx * seconds;
            shot.flownPx += Length(shot.velocityPx) * seconds;

            // fakeRadius: within scale(16) of its target an isCharacter has hp
            // set to 0. The ghost only ever targets the player, so killPortal -
            // the branch minions.json defers - is unreachable from here.
            if (playerThere && !playerKilled && Squared(playerPx - shot.atPx) < hit2) {
                playerKilled = true;
                killedBy = name;
                continue; // and the shot destroys itself
            }
            if (shot.flownPx >= shot.rangePx) continue;
            living.push_back(shot);
        }
        shots = std::move(living);
    }

    if (gone) return turn;

    elapsedMs += ms;

    switch (phase) {
        case Phase::Appearing: {
            // Degenerate here: every appear waypoint is ghost_pos, because the
            // port's ghost is not flown in by a dying ghost_minion. What is left
            // of it is the duration. Ghost.hpp says why.
            if (elapsedMs > rules.appearMs) {
                phase = Phase::Shooting;
                // NOT zero: the original seeds the clock at the shot interval, so
                // the first shot comes on the first frame of SHOOTING.
                elapsedMs = rules.seedElapsedMs;
            }
            break;
        }

        case Phase::Shooting: {
            if (elapsedMs > rules.intervalMs) {
                elapsedMs = 0.0;

                // The restock rides the same beat: addEntityIfItCantBeFound puts
                // a fire diamond back at its spawn, which is what makes dropping
                // one down the gutter mouth survivable.
                turn.restockDiamond = hasDiamondSpawn;

                // And it shoots only at a player past the red line.
                if (playerThere && (!hasRedLine || playerPx.x > redLineX)) {
                    const glm::dvec2 from = atPx + rules.muzzlePx;
                    const glm::dvec2 along = playerPx - from;
                    const double reach = Length(along);
                    if (reach > 0.0) {
                        Shot made;
                        made.atPx = from;
                        made.velocityPx = (along / reach) * rules.shotSpeedPxS;
                        made.rangePx = reach;
                        shots.push_back(made);
                        ++shotsFired;
                    }
                }
            }

            // isBurned / healBurn. Only this arm reads it in the original, so a
            // fireball that lands while the ghost is already hurt is spent.
            if (burned) {
                burned = false;
                --hp;
                ++hits;
                phase = Phase::Damage;
                elapsedMs = 0.0;
            }
            break;
        }

        case Phase::Damage: {
            // hp first: the original tests it before anything else in this arm.
            if (hp <= 0) {
                phase = Phase::Death;
                elapsedMs = 0.0;
            } else if (elapsedMs > rules.toRageMs) {
                phase = Phase::Rage;
                elapsedMs = 0.0;
                summonedThisRage = false;
            }
            break;
        }

        case Phase::Rage: {
            // It waits on its escort. While none stands it calls one, inside the
            // first window; once one stands it STAYS here until the player kills
            // it, and only then has the window passed and it goes back to
            // shooting.
            if (minionsStanding == 0) {
                if (elapsedMs < rules.summonWindowMs) {
                    if (!summonedThisRage && hasSummonAnchor) {
                        turn.summon = true;
                        summonedThisRage = true;
                        ++summons;
                    }
                } else {
                    phase = Phase::Shooting;
                    elapsedMs = 0.0;
                }
            }
            break;
        }

        case Phase::Death: {
            if (elapsedMs > rules.burstMs) {
                gone = true;
                turn.droppedKey = true;
                turn.keyPx = atPx;
            }
            break;
        }
    }

    return turn;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    std::vector<Placed> placed;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;

        const std::string entity = Roles::EntityName(node);
        const std::string role = Roles::RoleOf(roles, node);

        if (role == Roles::kWaypoint) {
            Placed one;
            one.entityName = entity;
            if (!PositionOf(node, one.atPx)) {
                error = node.name + " is a waypoint with no position";
                return false;
            }
            one.holdMs = MetaNumber(node, "holdTime", 0.0);
            placed.push_back(one);
            continue;
        }

        // The three markers this boss reads BY NAME. None of them carries a role
        // the port plays - minion_spawn_point carries no role at all - so none is
        // reachable any other way.
        if (entity == rules.redLineName) {
            glm::dvec2 atPx(0.0, 0.0);
            if (PositionOf(node, atPx)) {
                out.hasRedLine = true;
                out.redLineX = atPx.x;
            }
            continue;
        }
        if (entity == rules.summonAnchorName) {
            glm::dvec2 atPx(0.0, 0.0);
            if (PositionOf(node, atPx)) {
                out.hasSummonAnchor = true;
                out.summonPx = atPx;
            }
            continue;
        }
        if (entity == rules.diamondSpawnName) {
            glm::dvec2 atPx(0.0, 0.0);
            if (PositionOf(node, atPx)) {
                out.hasDiamondSpawn = true;
                out.diamondSpawnPx = atPx;
            }
            continue;
        }

        if (entity != rules.anchorName) continue;

        glm::dvec2 atPx(0.0, 0.0);
        if (!PositionOf(node, atPx)) {
            error = node.name + " is a ghost with no position";
            return false;
        }
        out.present = true;
        out.name = node.name;
        out.atPx = atPx;
    }

    if (!out.present) return true;

    out.hp = rules.maxHp;
    out.phase = Phase::Appearing;

    // The escort's patrol, resolved the way the original resolves one: seek
    // prefix + n until one is missing. Done HERE rather than at summon time
    // because Game::AfterStep has no scene to resolve it from.
    for (std::size_t index = 0;; ++index) {
        const std::string want = rules.waypointPrefix + std::to_string(index);
        const Placed* found = nullptr;
        for (const Placed& one : placed) {
            if (one.entityName == want) {
                found = &one;
                break;
            }
        }
        if (found == nullptr) break;
        Minions::Waypoint made;
        made.name = want;
        made.atPx = found->atPx;
        made.holdMs = found->holdMs;
        out.patrol.push_back(made);
    }

    return true;
}

} // namespace MagicPortals::Ghost
