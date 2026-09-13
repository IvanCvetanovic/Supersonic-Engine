#include "sim/Roles.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace MagicPortals::Roles {

bool Moves(const std::string& role) {
    // kBouncer joins them: its slab bobs, and a static body would carry nothing
    // while it did. The remake's MOVING_ROLES does not list it, because the
    // remake does not move it either - this is the port going past the remake to
    // the original, as the acceptance rule allows where the original is decoded.
    return role == kMovingPlatform || role == kLift || role == kSwitchedDoor || role == kBouncer;
}

bool IsPorted(const std::string& role) {
    static const char* const kPorted[] = {
        kPlayerSpawn, kLevelBounds, kLevelProperties, kExitDoor, kCollectible,
        kSwitch, kSwitchedDoor, kLift, kMovingPlatform, kNoPortalZone, kStaticPortal, kHazard, kDemolisher,
        kLauncher, kProjectileBlocker, kReflector, kTurret, kFireSource, kExplosive, kHinge,
        kEnemySpawn, kWaypoint, kKey, kKeyhole, kLockedDoor, kShockField, kPickup, kTorch, kBouncer, kGravityWell,
        // Nothing to play: drawn, or read by another role.
        "lift_marker", "camera_start", "scenery_hint", "scenery_fx",
    };
    if (role.empty()) return true;
    return std::find(std::begin(kPorted), std::end(kPorted), role) != std::end(kPorted);
}

bool Load(const std::string& path, Table& out, std::string& error) {
    namespace Json = Supersonic::Json;
    out = Table{};

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot read";
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Named, not a temporary: the parser keeps a reference to what it reads,
    // and its constructor from an rvalue is deleted for exactly that reason.
    const std::string content = text.str();
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    const Json::Value& roles = root["roles"];
    if (!roles.IsObject()) {
        error = path + ": no \"roles\" object";
        return false;
    }

    for (const auto& [role, entry] : roles.AsObject()) {
        const Json::Value& names = entry["names"];
        if (!names.IsArray()) {
            error = path + ": role " + role + " has no \"names\" array";
            return false;
        }
        for (const Json::Value& name : names.AsArray()) {
            if (!name.IsString()) {
                error = path + ": role " + role + " lists a name that is not a string";
                return false;
            }
            const auto [it, inserted] = out.roleOf.emplace(name.AsString(), role);
            if (!inserted && it->second != role) {
                error = path + ": " + name.AsString() + " is listed as both " + it->second + " and " + role;
                return false;
            }
        }
    }
    return true;
}

std::string EntityName(const Tscn::Node& node) {
    const Tscn::Value* name = node.Meta("entity_name");
    return name != nullptr && name->kind == Tscn::Value::Kind::String ? name->text : std::string();
}

std::string RoleOf(const Table& table, const Tscn::Node& node) {
    const auto found = table.roleOf.find(EntityName(node));
    return found == table.roleOf.end() ? std::string() : found->second;
}

} // namespace MagicPortals::Roles
