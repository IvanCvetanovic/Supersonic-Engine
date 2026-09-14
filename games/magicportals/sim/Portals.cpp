#include "sim/Portals.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "sim/Units.hpp"
#include "sim/Zerog.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

namespace MagicPortals::Portals {

using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

// portal_system.gd:230-261, in the remake's space: pixels, +y down. Placed
// portals are never turned, so rotate_to_exit would turn through nothing here.
// The port plays the original's invert (transit.json): the traveller comes out
// at the exit itself, turned back, and the player in y only.
void Teleport(entt::registry& registry, entt::entity body, const glm::dvec2& exitPx, const Portal::Transit& transit,
              bool character) {
    auto& transform = registry.get<TransformComponent>(body);
    auto& rigid = registry.get<RigidBodyComponent>(body);
    const glm::dvec2 velocityPx(rigid.velocity.x * Units::kPixelsPerMetre, -rigid.velocity.y * Units::kPixelsPerMetre);
    const glm::dvec2 exitVelocityPx = Portal::ExitVelocity(velocityPx, 0.0, 0.0, transit, character);
    const glm::dvec2 destinationPx = Portal::ExitPosition(exitPx, exitVelocityPx, transit);
    const glm::vec3 destination = Units::ToWorld(destinationPx.x, destinationPx.y);
    transform.position = glm::vec3(destination.x, destination.y, transform.position.z);
    rigid.velocity =
        glm::vec3(Units::ToMetres(exitVelocityPx.x), Units::ToMetres(-exitVelocityPx.y), rigid.velocity.z);
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!Portal::LoadTransit(path, read.transit, error)) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    Supersonic::Json::Parser parser(text);
    Supersonic::Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    const auto field = [&](const char* block, const char* key) -> const Supersonic::Json::Value* {
        if (!root.IsObject() || !root.Has(block) || !root[block].IsObject() || !root[block].Has(key)) {
            error = path + ": " + block + "." + key + " is missing";
            return nullptr;
        }
        return &root[block][key];
    };
    const auto number = [&](const char* block, const char* key, double& to) {
        const Supersonic::Json::Value* value = field(block, key);
        if (value == nullptr) return false;
        if (!value->IsNumber()) {
            error = path + ": " + block + "." + key + " is not a number";
            return false;
        }
        to = value->AsNumber();
        return true;
    };
    const auto flag = [&](const char* block, const char* key, bool& to) {
        const Supersonic::Json::Value* value = field(block, key);
        if (value == nullptr) return false;
        if (!value->IsBool()) {
            error = path + ": " + block + "." + key + " is not a bool";
            return false;
        }
        to = value->AsBool();
        return true;
    };

    double maxPortals = 0.0;
    // READ AND CHECKED, then dropped. The remake marks its placement block
    // _guess and its own _radius_note says the value was unknown; the decoded
    // radii come from the port's placement.json instead (LoadPlacement). It is
    // still required here so a malformed portals.json is refused rather than
    // half-read.
    double remakeRadiusPx = 0.0;
    if (!number("placement", "default_max_portals", maxPortals) ||
        !number("placement", "collision_radius_px", remakeRadiusPx) ||
        !flag("consumption", "consume_on_traverse", read.consumeOnTraverse) ||
        !flag("consumption", "static_portals_persist", read.staticPortalsPersist) ||
        !flag("consumption", "on_cap_recycle_oldest", read.recycleOldestAtCap)) {
        return false;
    }
    read.defaultMaxPortals = static_cast<int>(maxPortals);
    out = read;
    return true;
}

bool LoadPlacement(const std::string& path, Rules& out, std::string& error) {
    namespace Json = Supersonic::Json;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    // Named, not a temporary: Json::Parser holds its text by reference.
    const std::string text = buffer.str();

    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    const Json::Value& portal = root["portal"];
    const Json::Value& antiportal = root["antiportal"];
    if (!portal.IsObject() || !antiportal.IsObject()) {
        error = path + ": portal and antiportal are each an object";
        return false;
    }
    if (!portal["entry_radius_px"].IsNumber() || !antiportal["radius_px"].IsNumber()) {
        error = path + ": portal.entry_radius_px and antiportal.radius_px are each a number";
        return false;
    }

    const Json::Value& cooldowns = root["cooldowns"];
    if (!cooldowns.IsObject() || !cooldowns["first_ms"].IsNumber() || !cooldowns["next_ms"].IsNumber()) {
        error = path + ": cooldowns.first_ms and cooldowns.next_ms are each a number";
        return false;
    }
    const double firstMs = cooldowns["first_ms"].AsNumber(-1.0);
    const double nextMs = cooldowns["next_ms"].AsNumber(-1.0);
    // Zero is allowed - it means no cooldown at all, which is what the remake
    // carried for the first one - but a negative is a file saying nothing
    // sensible.
    if (firstMs < 0.0 || nextMs < 0.0) {
        error = path + ": neither cooldown is below zero";
        return false;
    }
    out.firstPortalMinMs = firstMs;
    out.nextPortalMinMs = nextMs;

    const double entry = portal["entry_radius_px"].AsNumber(0.0);
    const double field = antiportal["radius_px"].AsNumber(0.0);
    // A radius of nothing is a portal nobody can walk into and a field that
    // refuses nothing, and each would look like a level that works.
    if (entry <= 0.0 || field <= 0.0) {
        error = path + ": both radii are above zero";
        return false;
    }

    out.entryRadiusPx = entry;
    out.antiportalRadiusPx = field;
    return true;
}

bool State::TryPlace(const glm::dvec2& atPx) {
    if (budget <= 0) return false;
    for (const NoPortalZone& zone : zones) {
        // The ANTIPORTAL's radius, which is half its own sprite times the node's
        // scale - not the portal's own 14. placement.json has the decode.
        //
        // A zone carrying its OWN radius uses that instead: a gravity well's
        // antiportal is sized from the agent's radius rather than from
        // white_ring.png, so no constant here could express it.
        const double reachPx = zone.radiusPx > 0.0 ? zone.radiusPx : rules.antiportalRadiusPx * zone.scale;
        if (glm::distance(atPx, zone.CentreNowPx()) <= reachPx) return false;
    }
    if (static_cast<int>(placed.size()) >= budget) {
        if (!rules.recycleOldestAtCap) return false;
        placed.erase(placed.begin());
    }
    Placed portal;
    portal.id = portalsUsed;
    portal.atPx = atPx;
    const glm::vec3 centre = Units::ToWorld(atPx.x, atPx.y);
    // And the PORTAL's own, g_portalCollisionRadius.
    portal.trigger = Trigger::Circle{glm::vec2(centre.x, centre.y), Units::ToMetres(rules.entryRadiusPx)};
    placed.push_back(portal);
    ++portalsUsed;
    return true;
}

bool State::Shoot(entt::registry& registry, const glm::dvec2& atPx) {
    if (budget <= 0 || flight || shooter == entt::null || !registry.valid(shooter)) return false;

    // THE COOLDOWNS, and they gate the TAP rather than the placement.
    //
    // The original holds a whole level's taps off for FIRST_PORTAL_MIN_TIME -
    // PortalManager::update skips managePortalInsertion entirely until its
    // gameTimer passes it - and then holds each tap off for
    // NEXT_PORTAL_MIN_TIME since the last one it took.
    //
    // Here rather than in TryPlace on purpose. TryPlace is what an arriving shot
    // calls AND what the suites call to put a portal where a test needs one; a
    // clock there would refuse them for a reason none of them is about. The
    // layer's only tap path is this function (readInput), so this is the seam
    // the original gates.
    //
    // The comparisons are the original's own, and they differ: update refuses
    // while gameTimer is BELOW FIRST_PORTAL_MIN_TIME (a strict less-than), while
    // managePortalInsertion proceeds only when lastPortalTimer is ABOVE
    // NEXT_PORTAL_MIN_TIME - so it refuses on equal.
    // ZERO MEANS OFF, stated rather than implied. sinceTapMs starts at zero, so
    // a bare `sinceTapMs <= nextPortalMinMs` refuses the very first tap even
    // when the cooldown is nothing - which silently disarmed the suites that
    // turn it off and cost a whole build to find.
    const bool tooEarly = rules.firstPortalMinMs > 0.0 && sinceStartMs < rules.firstPortalMinMs;
    const bool tooSoon = rules.nextPortalMinMs > 0.0 && sinceTapMs <= rules.nextPortalMinMs;
    if (tooEarly || tooSoon) return false;

    // Taken, so it is spent - even if the shot goes on to hit a wall and open
    // nothing. The original resets lastPortalTimer before it has any idea
    // whether the shot will land. placement.json says why that does not
    // contradict "a failed shot costs nothing", which is about the budget.
    sinceTapMs = 0.0;
    Flight fired;
    fired.fromPx = Units::ToPixels(registry.get<TransformComponent>(shooter).position);
    fired.toPx = atPx;
    fired.atPx = fired.fromPx;
    flight = fired;
    ++shotsFired;

    // THE RECOIL, in a zero-gravity level, and it is how the player moves there.
    //
    // Here rather than where the shot arrives, because the original puts it
    // here: managePortalInsertion calls applyImpulse immediately after the tap
    // is taken, on the joined path that a failed shot comes down too. So the
    // shove is spent on a tap that goes on to hit a wall, exactly as it is on
    // one that opens a portal - the flight below has not resolved yet.
    //
    // ADDED to the velocity, never set: applyImpulse is
    // SetLinearVelocity(GetLinearVelocity() + impulse), so shots accumulate and
    // drift is the mechanic.
    if (noGravity && recoilMps > 0.0) {
        registry.get<RigidBodyComponent>(shooter).velocity += Zerog::Impulse(fired.fromPx, fired.toPx, recoilMps);
    }
    return true;
}

const NoPortalZone* State::FindZone(const std::string& name) const {
    for (const NoPortalZone& zone : zones) {
        if (zone.name == name) return &zone;
    }
    return nullptr;
}

void State::Tick(entt::registry& registry, float dt) {
    lockoutS = std::max(0.0f, lockoutS - dt);
    // The cooldown clocks run UP, as the original's two Timers do.
    const double elapsedMs = static_cast<double>(dt) * 1000.0;
    sinceStartMs += elapsedMs;
    sinceTapMs += elapsedMs;
    for (NoPortalZone& zone : zones) {
        if (zone.moving) zone.motion.t += dt;
    }

    // The shot flies on, through this tick's stretch of its way, against the
    // world as the step left it. What the stretch meets first decides: a body or
    // a blocker ends it, and a reflector turns it, mirrored across its plane, for
    // the rest of its way. Reaching the end of its way opens a portal there, or
    // fails where none may open.
    if (flight) {
        const glm::dvec2 left = flight->toPx - flight->atPx;
        const double remaining = glm::length(left);
        const double reach = shot.speedPx * static_cast<double>(dt);
        const bool arrives = remaining <= reach;
        const glm::dvec2 next = arrives ? flight->toPx : flight->atPx + left * (reach / remaining);
        double first = 2.0; // along the stretch, from 0 to 1: past its end until something is met
        std::string stoppedBy;
        if (const auto hit = Shot::FirstBody(registry, flight->atPx, next, shooter)) {
            const auto* tag = registry.try_get<Supersonic::TagComponent>(hit->body);
            stoppedBy = tag != nullptr ? tag->tag : std::string("a body");
            first = hit->along;
        }
        for (const Blocker& blocker : blockers) {
            if (const auto along = Shot::Enters(flight->atPx, next, blocker.box); along && *along < first) {
                stoppedBy = blocker.name;
                first = *along;
            }
        }
        int turnedBy = -1;
        if (flight->reflections < shot.maxReflections) {
            for (std::size_t i = 0; i < reflectors.size(); ++i) {
                if (static_cast<int>(i) == flight->lastReflector) continue;
                const auto along =
                    Shot::EntersCircle(flight->atPx, next, reflectors[i].centrePx, shot.reflectRadiusPx);
                if (along && *along < first) {
                    turnedBy = static_cast<int>(i);
                    first = *along;
                }
            }
        }
        if (turnedBy >= 0) {
            // Off the reflector where the shot reached it, mirrored, for the rest
            // of its way.
            const glm::dvec2 at = flight->atPx + (next - flight->atPx) * first;
            glm::dvec2 onward = flight->toPx - at;
            if (reflectors[static_cast<std::size_t>(turnedBy)].vertical) {
                onward.x = -onward.x;
            } else {
                onward.y = -onward.y;
            }
            flight->atPx = at;
            flight->toPx = at + onward;
            flight->lastReflector = turnedBy;
            ++flight->reflections;
            ++reflections;
        } else {
            if (stoppedBy.empty() && arrives && !TryPlace(flight->toPx)) {
                stoppedBy = "the tap, where no portal may open";
            }
            if (!stoppedBy.empty()) {
                ++shotsFailed;
                lastFailure = stoppedBy;
                flight.reset();
            } else if (arrives) {
                flight.reset();
            } else {
                flight->atPx = next;
            }
        }
    }

    // An end of a traversal: a static portal by its place in `statics`, which
    // never moves, or a placed one by its id, since placed portals come and go.
    struct End {
        bool isStatic = false;
        int key = 0;
    };
    struct Entry {
        End end;
        entt::entity body;
    };

    // Every portal's entries are found before any is acted on, as a physics step
    // finds its overlaps before it reports them.
    std::vector<Entry> entries;
    const auto sweep = [&](std::vector<entt::entity>& inside, const auto& trigger, End end) {
        std::vector<entt::entity> now;
        for (const entt::entity body : travellers) {
            if (registry.valid(body) && Trigger::Overlaps(registry, body, trigger)) now.push_back(body);
        }
        for (const entt::entity body : now) {
            if (std::find(inside.begin(), inside.end(), body) == inside.end()) entries.push_back({end, body});
        }
        inside = std::move(now);
    };
    for (std::size_t i = 0; i < statics.size(); ++i) {
        if (statics[i].live) sweep(statics[i].inside, statics[i].trigger, End{true, static_cast<int>(i)});
    }
    for (Placed& portal : placed) sweep(portal.inside, portal.trigger, End{false, portal.id});

    const auto placedWithId = [this](int id) -> const Placed* {
        for (const Placed& portal : placed) {
            if (portal.id == id) return &portal;
        }
        return nullptr;
    };

    for (const Entry& entry : entries) {
        // portal_system.gd:183-191: nothing goes through while the lockout runs,
        // an end a traversal before it spent leads nowhere, and neither does a
        // portal with no partner.
        if (lockoutS > 0.0f) continue;
        std::optional<End> exit;
        if (entry.end.isStatic) {
            const Static& from = statics[static_cast<std::size_t>(entry.end.key)];
            if (!from.live) continue;
            // By destiny first (:216-223). A static portal that names itself
            // leads nowhere (:189), and a spent one is no partner.
            bool named = false;
            for (std::size_t i = 0; i < statics.size() && from.hasDestiny; ++i) {
                if (statics[i].index != from.destiny) continue;
                named = true;
                if (static_cast<int>(i) != entry.end.key && statics[i].live) exit = End{true, static_cast<int>(i)};
                break;
            }
            // Then the placed set (:224-227), when no static portal has the index.
            if (!named && !placed.empty()) exit = End{false, placed.front().id};
        } else {
            if (placedWithId(entry.end.key) == nullptr) continue;
            // Placed portals pair with each other (:208-214): with a pair, the other.
            for (const Placed& portal : placed) {
                if (portal.id != entry.end.key) {
                    exit = End{false, portal.id};
                    break;
                }
            }
            // With no second placement to pair with, the original sends the
            // traveller to the level's first portal_static - but only where the
            // level grants exactly one placement. PortalManager::doTeleporting
            // (bytes 139692..140259) takes the pair above when maxPortals is 2,
            // and teleportToFirstStaticPortal (bytes 145999..146550) when it is
            // 1; that one resolves its exit by SeekEntity('portal_static'), so
            // it is the first in the level's own order. Level 1-2 is the case:
            // one static portal, one placement, and the placement's partner is
            // the static. Without this the player walks through their own
            // portal, which is how this was found - by playing it.
            if (!exit && budget == 1) {
                for (std::size_t i = 0; i < statics.size(); ++i) {
                    if (!statics[i].live) continue;
                    exit = End{true, static_cast<int>(i)};
                    break;
                }
            }
        }
        if (!exit) continue;

        const glm::dvec2 exitPx =
            exit->isStatic ? statics[static_cast<std::size_t>(exit->key)].atPx : placedWithId(exit->key)->atPx;
        Teleport(registry, entry.body, exitPx, rules.transit, entry.body == shooter);
        ++traversals;
        // A placed pair carried something, so neither end is refunded if it is
        // cleared later. Only the pair: teleportToOther is the one route that
        // sets the flag, and a placement whose exit is the level's first static
        // portal goes through teleportToFirstStaticPortal instead.
        if (!entry.end.isStatic && !exit->isStatic) {
            for (Placed& portal : placed) {
                if (portal.id == entry.end.key || portal.id == exit->key) portal.teleported = true;
            }
        }
        lockoutS = static_cast<float>(rules.transit.reentryLockoutS);

        // Both ends of a pair go (:270-282), except a static end, which stays
        // unless portals.json says static portals do not persist.
        if (!rules.consumeOnTraverse) continue;
        for (const End& end : {entry.end, *exit}) {
            if (end.isStatic) {
                if (!rules.staticPortalsPersist) statics[static_cast<std::size_t>(end.key)].live = false;
            } else {
                placed.erase(std::remove_if(placed.begin(), placed.end(),
                                            [&end](const Placed& portal) { return portal.id == end.key; }),
                             placed.end());
            }
        }
    }
}

int State::KillAll(bool refund) {
    const int killed = static_cast<int>(placed.size());
    if (refund) {
        for (const Placed& portal : placed) {
            if (!portal.teleported && portalsUsed > 0) --portalsUsed;
        }
    }
    placed.clear();
    return killed;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built,
          entt::registry& registry, entt::entity player, const Rules& rules, const Mover::Rules& movers,
          const Shot::Rules& shot, State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    out.shot = shot;
    out.shooter = player;
    int maxPortals = rules.defaultMaxPortals;
    if (player != entt::null) out.travellers.push_back(player);

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        if (role == Roles::kStaticPortal) {
            // portal_system.gd:79-87. An inactive one is not a portal, and one with
            // no index cannot be named, so the remake passes both over.
            double active = 1.0;
            if (const Tscn::Value* value = node.Meta("active"); value != nullptr && !value->AsNumber(active)) {
                error = node.name + "'s active is not a number";
                return false;
            }
            const Tscn::Value* index = node.Meta("index");
            if (active == 0.0 || index == nullptr) continue;
            Static portal;
            portal.name = node.name;
            double number = 0.0;
            if (!index->AsNumber(number)) {
                error = node.name + "'s index is not a number";
                return false;
            }
            portal.index = static_cast<int>(number);
            if (const Tscn::Value* destiny = node.Meta("destiny")) {
                if (!destiny->AsNumber(number)) {
                    error = node.name + "'s destiny is not a number";
                    return false;
                }
                portal.destiny = static_cast<int>(number);
                portal.hasDestiny = true;
            }
            if (const Tscn::Value* colour = node.Meta("color");
                colour != nullptr && colour->kind == Tscn::Value::Kind::String) {
                portal.colour = colour->text;
            }
            const Tscn::Value* position = node.Find("position");
            if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) {
                error = node.name + " has no position";
                return false;
            }
            portal.atPx = glm::dvec2(position->numbers[0], position->numbers[1]);
            if (!Trigger::FromNode(node, portal.trigger, error)) return false;
            out.statics.push_back(std::move(portal));
            continue;
        }
        if (role == Roles::kProjectileBlocker) {
            Blocker blocker;
            blocker.name = node.name;
            if (!Trigger::FromNode(node, blocker.box, error)) return false;
            out.blockers.push_back(blocker);
            continue;
        }
        if (role == Roles::kReflector) {
            Reflector reflector;
            reflector.name = node.name;
            const Tscn::Value* position = node.Find("position");
            const Tscn::Value* plane = node.Meta("plane");
            if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2 ||
                (plane != nullptr && (plane->kind != Tscn::Value::Kind::String ||
                                      (plane->text != "vertical" && plane->text != "horizontal")))) {
                error = node.name + " needs a position, and a plane that is vertical or horizontal";
                return false;
            }
            reflector.centrePx = glm::dvec2(position->numbers[0], position->numbers[1]);
            // One placement in the game gives no plane (level26a's); it takes
            // shot.json's default, the remake's reading.
            reflector.vertical = plane != nullptr ? plane->text == "vertical" : shot.defaultPlaneVertical;
            out.reflectors.push_back(reflector);
            continue;
        }
        if (role == Roles::kLevelProperties) {
            // An absent max_portals is the default; an explicit 0 grants none.
            if (const Tscn::Value* value = node.Meta("max_portals")) {
                double number = 0.0;
                if (!value->AsNumber(number)) {
                    error = node.name + "'s max_portals is not a number";
                    return false;
                }
                maxPortals = static_cast<int>(number);
            }
        } else if (role == Roles::kNoPortalZone) {
            NoPortalZone zone;
            zone.name = node.name;
            if (const Tscn::Value* position = node.Find("position");
                position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
                zone.centrePx = glm::dvec2(position->numbers[0], position->numbers[1]);
            }
            if (const Tscn::Value* scale = node.Meta("scale"); scale != nullptr && !scale->AsNumber(zone.scale)) {
                error = node.name + "'s scale is not a number";
                return false;
            }
            // Only a zone with both a speed and a stride patrols; a plain antiportal
            // stands still (behaviours.gd:48-51).
            if (node.Meta("speed") != nullptr && node.Meta("stride") != nullptr) {
                const Tscn::Value* direction = node.Meta("direction");
                const bool sideways = direction != nullptr && direction->kind == Tscn::Value::Kind::String &&
                                      direction->text == "horizontal";
                zone.moving = true;
                if (!Mover::OscillationFromNode(node, sideways ? glm::dvec2(1.0, 0.0) : glm::dvec2(0.0, 1.0),
                                                movers.oscillationRateScale, zone.motion, error)) {
                    return false;
                }
            }
            out.zones.push_back(zone);
        }

        // portal_system.gd:194-203: a body travels when its entity says so.
        double teleportable = 0.0;
        const Tscn::Value* flag = node.Meta("teleportable");
        if (flag == nullptr || !flag->AsNumber(teleportable) || teleportable == 0.0) continue;
        const auto found = built.entities.find(node.name);
        if (found == built.entities.end()) continue;
        const auto* rigid = registry.try_get<RigidBodyComponent>(found->second);
        if (rigid != nullptr && !rigid->isKinematic) out.travellers.push_back(found->second);
    }
    out.budget = std::min(maxPortals, kPairSize);
    return true;
}

} // namespace MagicPortals::Portals
