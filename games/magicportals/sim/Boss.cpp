#include "sim/Boss.hpp"

#include "core/Components.hpp"
#include "core/DetMath.hpp"
#include "core/Json.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Trigger.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>

namespace MagicPortals::Boss {

using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;
namespace DetMath = Supersonic::DetMath;

namespace {

namespace Json = Supersonic::Json;

// PIb, Ethanon's own (GameMath.h:40), and the turn linearMotion wraps at.
constexpr float kHalfPi = 1.570796327f;
constexpr double kTwoPi = 6.283185307179586;
constexpr float kRadiansPerDegree = 0.017453292f;

glm::dvec2 PxOf(const entt::registry& registry, entt::entity e) {
    return Units::ToPixels(registry.get<TransformComponent>(e).position);
}

glm::dvec2 VelocityPx(const entt::registry& registry, entt::entity e) {
    const glm::vec3 v = registry.get<RigidBodyComponent>(e).velocity;
    return glm::dvec2(v.x * Units::kPixelsPerMetre, -v.y * Units::kPixelsPerMetre);
}

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
}

Trigger::Circle CircleAt(const glm::dvec2& atPx, double radiusPx) {
    const glm::vec3 centre = Units::ToWorld(atPx.x, atPx.y);
    return Trigger::Circle{glm::vec2(centre.x, centre.y), Units::ToMetres(radiusPx)};
}

// How hard a rock at `at`, moving at `velocity`, runs into what is centred at
// `other`: its velocity along the line between them, in the original's units
// (ETHBeginContactCallback_rolling_stone).
double Intensity(const glm::dvec2& at, const glm::dvec2& velocity, const glm::dvec2& other, double pxPerUnit) {
    const glm::dvec2 line = other - at;
    const double length = std::sqrt(Squared(line));
    if (length <= 0.0) return 0.0;
    return (velocity.x * line.x + velocity.y * line.y) / length / pxPerUnit;
}

bool InAPortal(entt::registry& registry, entt::entity body, const Portals::State& portals) {
    for (const Portals::Placed& portal : portals.placed) {
        if (Trigger::Overlaps(registry, body, portal.trigger)) return true;
    }
    for (const Portals::Static& portal : portals.statics) {
        if (portal.live && Trigger::Overlaps(registry, body, portal.trigger)) return true;
    }
    return false;
}

std::string Dotted(std::initializer_list<const char*> keys) {
    std::string out;
    for (const char* key : keys) out += (out.empty() ? "" : ".") + std::string(key);
    return out;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
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
    const auto at = [&](std::initializer_list<const char*> keys) -> const Json::Value* {
        const Json::Value* value = &root;
        for (const char* key : keys) {
            if (!value->IsObject() || !value->Has(key)) {
                error = path + ": " + Dotted(keys) + " is missing";
                return nullptr;
            }
            value = &(*value)[key];
        }
        return value;
    };
    const auto number = [&](std::initializer_list<const char*> keys, double& to) {
        const Json::Value* value = at(keys);
        if (value == nullptr) return false;
        if (!value->IsNumber()) {
            error = path + ": " + Dotted(keys) + " is not a number";
            return false;
        }
        to = value->AsNumber();
        return true;
    };
    const auto word = [&](std::initializer_list<const char*> keys, std::string& to) {
        const Json::Value* value = at(keys);
        if (value == nullptr) return false;
        if (!value->IsString()) {
            error = path + ": " + Dotted(keys) + " is not a string";
            return false;
        }
        to = value->AsString("");
        return true;
    };
    const auto pair = [&](std::initializer_list<const char*> keys, glm::dvec2& to) {
        const Json::Value* value = at(keys);
        if (value == nullptr) return false;
        if (!value->IsArray() || value->AsArray().size() != 2 || !value->AsArray()[0].IsNumber() ||
            !value->AsArray()[1].IsNumber()) {
            error = path + ": " + Dotted(keys) + " is not two numbers";
            return false;
        }
        to = glm::dvec2(value->AsArray()[0].AsNumber(), value->AsArray()[1].AsNumber());
        return true;
    };

    Rules read;
    double maxHp = 0.0;
    if (!word({"beholder", "entity"}, read.entity) || !number({"beholder", "max_hp"}, maxHp) ||
        !number({"beholder", "radius_px"}, read.radiusPx) || !number({"beholder", "seek", "step_px"}, read.stepPx) ||
        !number({"beholder", "seek", "wobble_px"}, read.wobblePx) ||
        !number({"beholder", "seek", "glide_ms"}, read.glideMs) ||
        !number({"beholder", "seek", "retarget_ms"}, read.retargetMs) ||
        !number({"beholder", "seek", "near_px"}, read.nearPx) || !number({"beholder", "seek", "hold_ms"}, read.holdMs) ||
        !number({"beholder", "throw_rock", "duration_ms"}, read.throwMs) ||
        !number({"beholder", "throw_rock", "stride_ms"}, read.rockStrideMs) ||
        !number({"beholder", "throw_rock", "height_px"}, read.rockHeightPx) ||
        !word({"beholder", "throw_rock", "entity"}, read.rockEntity) ||
        !number({"beholder", "throw_rock", "bob_rad_s"}, read.bobRadS) ||
        !number({"beholder", "throw_rock", "bob_px"}, read.bobPx) ||
        !number({"beholder", "got_damage", "duration_ms"}, read.hurtMs) ||
        !number({"beholder", "got_damage", "volley_ms"}, read.volleyMs) ||
        !number({"beholder", "got_damage", "step_deg"}, read.spikeStepDeg) ||
        !number({"beholder", "got_damage", "turn_deg"}, read.spikeTurnDeg) ||
        !number({"beholder", "got_damage", "spike_speed_px_s"}, read.spikeSpeedPx) ||
        !number({"beholder", "dead", "duration_ms"}, read.deadMs) ||
        !number({"beholder", "dead", "blast_px"}, read.blastPx) ||
        !number({"spike", "hit_share"}, read.spikeHitShare) || !pair({"spike", "character_px"}, read.characterPx) ||
        !pair({"spike", "cull_margin_px"}, read.cullMarginPx) ||
        !number({"rock", "crush_intensity"}, read.crushIntensity) || !number({"rock", "px_per_unit"}, read.pxPerUnit) ||
        !number({"rock", "contact_margin_px"}, read.contactMarginPx)) {
        return false;
    }
    if (maxHp < 1.0 || maxHp != std::floor(maxHp)) {
        error = path + ": beholder.max_hp is not a whole number from 1";
        return false;
    }
    read.maxHp = static_cast<int>(maxHp);
    // Each of these divides, or steps a loop, so none may be 0.
    if (read.glideMs <= 0.0 || read.spikeStepDeg <= 0.0 || read.pxPerUnit <= 0.0) {
        error = path + ": beholder.seek.glide_ms, beholder.got_damage.step_deg and rock.px_per_unit are each above 0";
        return false;
    }
    out = std::move(read);
    return true;
}

bool Plays(const Rules& rules, const Tscn::Node& node) {
    const Tscn::Value* entity = node.Meta("entity");
    return entity != nullptr && entity->kind == Tscn::Value::Kind::String && entity->text == rules.entity;
}

glm::dvec2 Glide::AtPx() const {
    // getCurrentPos: past its time it is there, and before, it is smoothEnd of
    // how far through it is (getBias).
    if (elapsedMs > timeMs) return toPx;
    const double through = std::clamp(elapsedMs / timeMs, 0.0, 1.0);
    const double bias = DetMath::sin(static_cast<float>(through) * kHalfPi);
    return fromPx + (toPx - fromPx) * bias;
}

void State::BeforeStep(entt::registry& registry) {
    for (Rock& made : rocks) {
        if (!registry.valid(made.body)) continue;
        made.atPx = PxOf(registry, made.body);
        made.velocityPx = VelocityPx(registry, made.body);
    }
}

std::vector<entt::entity> State::Contacts(entt::registry& registry, entt::entity player,
                                          const Portals::State& portals, float dt) {
    std::vector<entt::entity> broken;
    const bool hasPlayer = player != entt::null && registry.valid(player);
    const double reachPx = rock.radiusPx + contactMarginPx;
    for (const Rock& made : rocks) {
        if (!registry.valid(made.body)) {
            broken.push_back(made.body);
            continue;
        }
        if (InAPortal(registry, made.body, portals)) continue;
        const glm::dvec2 at = PxOf(registry, made.body);
        // Where it would have got to had nothing stopped it: the solver can turn
        // a hard hit away within the step, and BeginContact would still report it.
        const glm::dvec2 ahead = made.atPx + made.velocityPx * static_cast<double>(dt);
        // Into the player hard enough, and it dies.
        if (hasPlayer && !playerKilled &&
            (Trigger::Overlaps(registry, player, CircleAt(at, reachPx)) ||
             Trigger::Overlaps(registry, player, CircleAt(ahead, reachPx))) &&
            Intensity(at, made.velocityPx, PxOf(registry, player), rules.pxPerUnit) > rules.crushIntensity) {
            playerKilled = true;
            killedBy = made.name;
        }
        // Into anything static, and it breaks: destroyOnStaticHit.
        std::vector<entt::entity> near;
        for (const glm::dvec2& where : {at, ahead}) {
            const glm::vec3 centre = Units::ToWorld(where.x, where.y);
            Supersonic::PhysicsSystem::OverlapSphere(registry, centre, Units::ToMetres(reachPx), near, made.body,
                                                     false);
        }
        const bool hit = std::any_of(near.begin(), near.end(), [&](entt::entity other) {
            return !registry.all_of<RigidBodyComponent>(other) && registry.all_of<TransformComponent>(other) &&
                   Intensity(at, made.velocityPx, PxOf(registry, other), rules.pxPerUnit) > 0.0;
        });
        if (!hit) continue;
        registry.destroy(made.body);
        broken.push_back(made.body);
        ++rocksBroken;
    }
    std::erase_if(rocks, [&broken](const Rock& r) {
        return std::find(broken.begin(), broken.end(), r.body) != broken.end();
    });
    return broken;
}

State::Turn State::Tick(entt::registry& registry, entt::entity player, const std::vector<entt::entity>& stones,
                        float dt) {
    Turn turn;
    flySpikes(registry, player, dt);
    if (!beholder || beholder->gone) return turn;
    Beholder& b = *beholder;
    const double ms = static_cast<double>(dt) * 1000.0;
    const bool hasPlayer = player != entt::null && registry.valid(player);

    // ETHCallback_beholder, in its order. Its time in this phase, and death.
    b.elapsedMs += ms;
    const double elapsedNow = b.elapsedMs;
    if (b.hp < 0) {
        b.phase = Phase::Dead;
        b.deadMs += ms;
    }
    // The phase this turn plays is the one it began with: the original reads its
    // state before it looks around, so a hit below changes the next turn's.
    const Phase phaseNow = b.phase;
    const glm::dvec2 playerPx = hasPlayer ? PxOf(registry, player) : b.atPx;
    const glm::dvec2 currentPx = b.atPx;
    const double reach2 = rules.radiusPx * rules.radiusPx;

    // What is around it. A rolling stone rising within its radius breaks and
    // hurts it: one a turn, as the original stops looking at the first.
    for (const entt::entity stone : stones) {
        if (!registry.valid(stone) || !registry.all_of<RigidBodyComponent>(stone)) continue;
        if (Squared(PxOf(registry, stone) - currentPx) >= reach2) continue;
        // Its velocity against (0, -1), up on screen, is above 0.
        if (-VelocityPx(registry, stone).y <= 0.0) continue;
        registry.destroy(stone);
        turn.broken.push_back(stone);
        std::erase_if(rocks, [stone](const Rock& r) { return r.body == stone; });
        --b.hp;
        ++b.hits;
        b.phase = Phase::GotDamage;
        b.elapsedMs = 0.0;
        b.pulseMs = 0.0;
        b.volleyClockMs = 0.0;
        b.bobAngle = kHalfPi;
        b.bobFromPx = b.atPx;
        break;
    }
    // And the player within its radius dies.
    if (hasPlayer && !playerKilled && Squared(playerPx - currentPx) < reach2) {
        playerKilled = true;
        killedBy = b.name;
    }

    switch (phaseNow) {
    case Phase::Seeking: {
        b.frame = 0;
        // Toward the player's x, by no more than step_px, at its start height
        // and a wobble: the cosine of its time in milliseconds, as it has it.
        const float wobble = DetMath::cos(static_cast<float>(elapsedNow) + kHalfPi);
        const glm::dvec2 dest(currentPx.x + std::clamp(playerPx.x - currentPx.x, -rules.stepPx, rules.stepPx),
                              b.startHeightPx + static_cast<double>(wobble) * rules.wobblePx);
        follow(b, dest, ms);
        b.pulseMs += ms;
        if (std::fabs(playerPx.x - currentPx.x) < rules.nearPx) {
            b.approachMs += ms;
        } else {
            b.approachMs = 0.0;
        }
        if (b.approachMs > rules.holdMs) {
            b.phase = Phase::ThrowRock;
            b.elapsedMs = 0.0;
            b.approachMs = 0.0;
            b.bobAngle = kHalfPi;
            b.bobFromPx = b.atPx;
        }
        break;
    }
    case Phase::ThrowRock: {
        // linearMotion, vertical: a bob about where it shut its eye.
        b.bobAngle += rules.bobRadS * static_cast<double>(dt);
        if (b.bobAngle > kTwoPi) b.bobAngle -= kTwoPi;
        b.atPx = b.bobFromPx + glm::dvec2(0.0, DetMath::cos(static_cast<float>(b.bobAngle)) * rules.bobPx);
        b.frame = 1;
        // overTimeEntityAdder: a rock over the player every stride, on a clock
        // made the first time and never reset.
        if (!b.rockClockMade) {
            b.rockClockMade = true;
            b.rockClockMs = 0.0;
        }
        b.rockClockMs += ms;
        if (b.rockClockMs >= rules.rockStrideMs) {
            turn.dropped.push_back(drop(registry, glm::dvec2(playerPx.x, rules.rockHeightPx)));
            b.rockClockMs = 0.0;
            ++b.rocksThrown;
        }
        if (elapsedNow > rules.throwMs) {
            b.phase = Phase::Seeking;
            b.elapsedMs = 0.0;
        }
        break;
    }
    case Phase::GotDamage: {
        b.volleyClockMs += ms;
        b.frame = 0;
        if (b.volleyClockMs > rules.volleyMs) {
            // Every step_deg from the ring's turn to a full circle past it,
            // inclusive, clockwise from up; the next ring is turned.
            const float turnDeg = b.alternate ? static_cast<float>(rules.spikeTurnDeg) : 0.0f;
            b.alternate = !b.alternate;
            const float step = static_cast<float>(rules.spikeStepDeg);
            for (float a = turnDeg; a <= turnDeg + 360.0f; a += step) {
                float s = 0.0f;
                float c = 1.0f;
                DetMath::sincos(a * kRadiansPerDegree, s, c);
                glm::dvec2 direction(s, -c);
                direction /= std::sqrt(Squared(direction));
                spikes.push_back(Spike{currentPx, direction});
            }
            b.volleyClockMs = 0.0;
            ++b.volleys;
        }
        b.pulseMs += ms;
        if (elapsedNow > rules.hurtMs) {
            b.phase = Phase::Seeking;
            b.elapsedMs = 0.0;
        }
        break;
    }
    case Phase::Dead: {
        b.pulseMs += ms;
        b.frame = 0;
        if (b.deadMs > rules.deadMs) {
            // explode(currentPos, 2, true): a player in the blast dies.
            if (hasPlayer && !playerKilled && Trigger::Overlaps(registry, player, CircleAt(currentPx, rules.blastPx))) {
                playerKilled = true;
                killedBy = b.name + "'s blast";
            }
            buttonRaised = true;
            turn.buttonRaised = true;
            b.gone = true;
        }
        break;
    }
    }
    return turn;
}

Rock State::drop(entt::registry& registry, const glm::dvec2& atPx) {
    Rock made;
    made.name = beholder->name + "#" + std::to_string(beholder->rocksThrown + 1);
    made.body = LevelBuilder::BuildRigidCircle(registry, made.name, atPx, rock.radiusPx, LevelBuilder::Options{});
    made.atPx = atPx;
    rocks.push_back(made);
    return made;
}

void State::flySpikes(entt::registry& registry, entt::entity player, float dt) {
    // projectileBehaviour, then ETHCallback_beholder_spike's two tests: out of
    // the level and its margin it goes; in the player's shrunken box it kills.
    const glm::dvec2 lowPx = -rules.cullMarginPx;
    const glm::dvec2 highPx = boundsPx + rules.cullMarginPx;
    const bool hasPlayer = player != entt::null && registry.valid(player);
    const glm::dvec2 playerPx = hasPlayer ? PxOf(registry, player) : glm::dvec2(0.0);
    const glm::dvec2 halfPx = rules.characterPx * rules.spikeHitShare * 0.5;
    for (Spike& spike : spikes) spike.atPx += spike.directionPx * (rules.spikeSpeedPx * static_cast<double>(dt));
    std::erase_if(spikes, [&](const Spike& spike) {
        return spike.atPx.x < lowPx.x || spike.atPx.y < lowPx.y || spike.atPx.x > highPx.x || spike.atPx.y > highPx.y;
    });
    if (!hasPlayer || playerKilled) return;
    for (const Spike& spike : spikes) {
        if (std::fabs(spike.atPx.x - playerPx.x) > halfPx.x || std::fabs(spike.atPx.y - playerPx.y) > halfPx.y) continue;
        playerKilled = true;
        killedBy = beholder ? beholder->name + "'s spike" : std::string("a beholder spike");
        return;
    }
}

void State::follow(Beholder& b, const glm::dvec2& destPx, double ms) {
    // followUp: made the first time, and taken up again from where it has got
    // to once more than retarget_ms have passed.
    if (!b.gliding) {
        b.gliding = true;
        b.glide = Glide{b.atPx, destPx, rules.glideMs, 0.0};
        b.switchMs = 0.0;
    }
    if (destPx != b.atPx && b.switchMs > rules.retargetMs) {
        b.glide = Glide{b.glide.AtPx(), destPx, rules.glideMs, 0.0};
        b.switchMs = 0.0;
    }
    b.switchMs += ms;
    b.glide.elapsedMs += ms;
    b.atPx = b.glide.AtPx();
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, const Launchers::Rules& launchers,
          State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    out.contactMarginPx = rules.contactMarginPx;
    bool haveDest = false;
    bool haveBounds = false;
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        const std::string entityName = Roles::EntityName(node);
        const Tscn::Value* position = node.Find("position");
        const bool placed = position != nullptr && position->kind == Tscn::Value::Kind::Vector2;
        const glm::dvec2 atPx = placed ? glm::dvec2(position->numbers[0], position->numbers[1]) : glm::dvec2(0.0);
        if (role == Roles::kLevelBounds && placed) {
            out.boundsPx = atPx;
            haveBounds = true;
        }
        // What the dying beholder seeks by name: SeekEntity("button") and
        // SeekEntity("button_dest").
        if (entityName == "button" && out.buttonNode.empty()) out.buttonNode = node.name;
        if (entityName == "button_dest" && placed && !haveDest) {
            out.buttonDestPx = atPx;
            haveDest = true;
        }
        if (role != Roles::kBossSpawn || !Plays(rules, node)) continue;
        if (out.beholder) {
            error = node.name + " is a second beholder, and a level has one";
            return false;
        }
        if (!placed) {
            error = node.name + " has no position";
            return false;
        }
        Beholder beholder;
        beholder.name = node.name;
        beholder.atPx = atPx;
        beholder.startHeightPx = atPx.y;
        beholder.hp = rules.maxHp;
        out.beholder = beholder;
    }
    if (!out.beholder) return true;
    const auto throwable = launchers.throwables.find(rules.rockEntity);
    if (throwable == launchers.throwables.end()) {
        error = out.beholder->name + " drops " + rules.rockEntity + ", which launchers.json does not describe";
        return false;
    }
    out.rock = throwable->second;
    if (out.buttonNode.empty() || !haveDest || !haveBounds) {
        error = out.beholder->name + " is in a level with no button, button_dest or level_bounds";
        return false;
    }
    return true;
}

} // namespace MagicPortals::Boss
