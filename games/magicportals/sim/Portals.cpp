#include "sim/Portals.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::Portals {

using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

// portal_system.gd:230-261, in the remake's space: pixels, +y down. Placed
// portals are never turned, so rotate_to_exit turns through nothing here.
void Teleport(entt::registry& registry, entt::entity body, const glm::dvec2& exitPx, const Portal::Transit& transit) {
    auto& transform = registry.get<TransformComponent>(body);
    auto& rigid = registry.get<RigidBodyComponent>(body);
    const glm::dvec2 velocityPx(rigid.velocity.x * Units::kPixelsPerMetre, -rigid.velocity.y * Units::kPixelsPerMetre);
    const glm::dvec2 exitVelocityPx = Portal::ExitVelocity(velocityPx, 0.0, 0.0, transit);
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
    if (!number("placement", "default_max_portals", maxPortals) ||
        !number("placement", "collision_radius_px", read.collisionRadiusPx) ||
        !flag("consumption", "consume_on_traverse", read.consumeOnTraverse) ||
        !flag("consumption", "static_portals_persist", read.staticPortalsPersist) ||
        !flag("consumption", "on_cap_recycle_oldest", read.recycleOldestAtCap)) {
        return false;
    }
    read.defaultMaxPortals = static_cast<int>(maxPortals);
    out = read;
    return true;
}

bool State::TryPlace(const glm::dvec2& atPx) {
    if (budget <= 0) return false;
    for (const NoPortalZone& zone : zones) {
        if (glm::distance(atPx, zone.centrePx) <= rules.collisionRadiusPx * zone.scale) return false;
    }
    if (static_cast<int>(placed.size()) >= budget) {
        if (!rules.recycleOldestAtCap) return false;
        placed.erase(placed.begin());
    }
    Placed portal;
    portal.atPx = atPx;
    const glm::vec3 centre = Units::ToWorld(atPx.x, atPx.y);
    portal.trigger = Trigger::Circle{glm::vec2(centre.x, centre.y), Units::ToMetres(rules.collisionRadiusPx)};
    placed.push_back(portal);
    ++portalsUsed;
    return true;
}

void State::Tick(entt::registry& registry, float dt) {
    lockoutS = std::max(0.0f, lockoutS - dt);

    // Every portal's entries are found before any is acted on, as a physics step
    // finds its overlaps before it reports them.
    struct Entry {
        std::size_t portal;
        entt::entity body;
    };
    std::vector<Entry> entries;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        std::vector<entt::entity> now;
        for (const entt::entity body : travellers) {
            if (registry.valid(body) && Trigger::Overlaps(registry, body, placed[i].trigger)) now.push_back(body);
        }
        for (const entt::entity body : now) {
            if (std::find(placed[i].inside.begin(), placed[i].inside.end(), body) == placed[i].inside.end()) {
                entries.push_back({i, body});
            }
        }
        placed[i].inside = std::move(now);
    }

    for (const Entry& entry : entries) {
        // portal_system.gd:183-191: nothing goes through while the lockout runs,
        // and a portal with no partner leads nowhere. A spent pair leaves nothing
        // for the entries after it.
        if (lockoutS > 0.0f || placed.size() < 2) continue;
        const Placed& exit = placed[entry.portal == 0 ? 1 : 0];
        Teleport(registry, entry.body, exit.atPx, rules.transit);
        ++traversals;
        lockoutS = static_cast<float>(rules.transit.reentryLockoutS);
        // Both ends go: they are one pair (portal_system.gd:270-280).
        if (rules.consumeOnTraverse) placed.clear();
    }
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built,
          entt::registry& registry, entt::entity player, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;
    int maxPortals = rules.defaultMaxPortals;
    if (player != entt::null) out.travellers.push_back(player);

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        if (role == Roles::kStaticPortal) {
            error = node.name + " is a static portal, and those are not ported";
            return false;
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
            if (node.Meta("speed") != nullptr && node.Meta("stride") != nullptr) {
                error = node.name + " is a moving no-portal zone, and those are not ported";
                return false;
            }
            NoPortalZone zone;
            if (const Tscn::Value* position = node.Find("position");
                position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
                zone.centrePx = glm::dvec2(position->numbers[0], position->numbers[1]);
            }
            if (const Tscn::Value* scale = node.Meta("scale"); scale != nullptr && !scale->AsNumber(zone.scale)) {
                error = node.name + "'s scale is not a number";
                return false;
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
