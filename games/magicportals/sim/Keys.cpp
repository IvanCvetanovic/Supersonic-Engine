#include "sim/Keys.hpp"

#include "sim/Carry.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Keys {

namespace {

namespace Json = Supersonic::Json;

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
}

std::string ColourOf(const Tscn::Node& node) {
    const Tscn::Value* value = node.Meta("color");
    if (value == nullptr || value->kind != Tscn::Value::Kind::String) return std::string();
    return value->text;
}

bool PositionOf(const Tscn::Node& node, glm::dvec2& out) {
    const Tscn::Value* position = node.Find("position");
    if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) return false;
    out = glm::dvec2(position->numbers[0], position->numbers[1]);
    return true;
}

// One placed door, before a keyhole has claimed it.
struct Door {
    std::string name;
    std::string colour;
    glm::dvec2 atPx{0.0, 0.0};
    entt::entity body = entt::null;
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

    Rules read;
    if (!number("key", "range_px", read.rangePx)) return false;
    if (!number("key", "leash_px", read.leashPx)) return false;
    if (!number("key", "reaim_ms", read.reaimMs)) return false;
    if (!number("key", "stride_ms", read.strideMs)) return false;
    if (!number("keyhole", "fade_start_ms", read.fadeStartMs)) return false;
    if (!number("keyhole", "fade_ms", read.fadeMs)) return false;

    // Each of these would look like a level that works. A range of nothing is a
    // key that can never be picked up and a door that can never be opened; a
    // leash shorter than the range would snap a key onto its owner before it
    // could ever trail; a stride or re-aim of nothing divides by zero; and a fade
    // of nothing takes the door the instant the keyhole is reached, which is a
    // different puzzle from one that gives the player a second to watch.
    if (read.rangePx <= 0.0) {
        error = path + ": key.range_px is above zero";
        return false;
    }
    if (read.leashPx <= read.rangePx) {
        error = path + ": key.leash_px is longer than key.range_px";
        return false;
    }
    if (read.reaimMs <= 0.0) {
        error = path + ": key.reaim_ms is above zero";
        return false;
    }
    if (read.strideMs <= 0.0) {
        error = path + ": key.stride_ms is above zero";
        return false;
    }
    if (read.fadeStartMs < 0.0) {
        error = path + ": keyhole.fade_start_ms is not negative";
        return false;
    }
    if (read.fadeMs <= 0.0) {
        error = path + ": keyhole.fade_ms is above zero";
        return false;
    }

    out = std::move(read);
    return true;
}

std::vector<entt::entity> State::Tick(entt::registry& registry, const std::vector<entt::entity>& carriers, float dt) {
    std::vector<entt::entity> removed;
    const double ms = static_cast<double>(dt) * 1000.0;
    const double range2 = rules.rangePx * rules.rangePx;

    for (Key& key : keys) {
        if (key.spent) continue;

        // The carry path, which a shock diamond shares: let go of an owner that
        // has ceased to be, then take the first carrier within range - here the
        // player and every minion still standing, since a key admits isCharacter
        // OR isMinion - and trail it. A key begins trailing in the same tick it
        // is taken, as the original does.
        Carry::Drop(key, registry);
        if (key.owner == entt::null) {
            if (!Carry::Acquire(key, registry, carriers, rules.rangePx)) continue;
            ++picked;
        }
        Carry::Trail(key, registry, rules, ms);

        // And within the SAME range of a keyhole of its colour, it opens it. The
        // key is spent either way: the original sends one that has found its
        // keyhole down the fly-in branch, and it never polls again.
        for (std::size_t index = 0; index < keyholes.size(); ++index) {
            Keyhole& keyhole = keyholes[index];
            if (keyhole.unlocked || keyhole.colour != key.colour) continue;
            if (Squared(keyhole.atPx - key.atPx) >= range2) continue;
            keyhole.unlocked = true;
            keyhole.sinceUnlockMs = 0.0;
            key.spent = true;
            key.opened = static_cast<int>(index);
            ++unlocked;
            break;
        }
    }

    // An unlocked keyhole holds, fades, and takes its door with it.
    for (Keyhole& keyhole : keyholes) {
        if (!keyhole.unlocked || keyhole.gone) continue;
        keyhole.sinceUnlockMs += ms;
        const double over = keyhole.sinceUnlockMs - rules.fadeStartMs;
        keyhole.alpha = 1.0 - std::min(1.0, std::max(0.0, over / rules.fadeMs));
        if (keyhole.alpha > 0.0) continue;

        keyhole.gone = true;
        ++opened;
        if (keyhole.door == entt::null || !registry.valid(keyhole.door)) continue;
        // As Demolish takes a breakable wall the builder made: destroyed in the
        // registry, and left in Built::entities, which every reader guards with
        // registry.valid.
        registry.destroy(keyhole.door);
        removed.push_back(keyhole.door);
    }

    return removed;
}

const Key* State::FindKey(const std::string& name) const {
    for (const Key& key : keys) {
        if (key.name == name) return &key;
    }
    return nullptr;
}

const Keyhole* State::FindKeyhole(const std::string& name) const {
    for (const Keyhole& keyhole : keyholes) {
        if (keyhole.name == name) return &keyhole;
    }
    return nullptr;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    std::vector<Door> doors;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        if (role != Roles::kKey && role != Roles::kKeyhole && role != Roles::kLockedDoor) continue;

        const std::string colour = ColourOf(node);
        if (colour.empty()) {
            error = node.name + " carries no colour, and a key is paired by nothing else";
            return false;
        }
        glm::dvec2 atPx(0.0, 0.0);
        if (!PositionOf(node, atPx)) {
            error = node.name + " has no position";
            return false;
        }

        if (role == Roles::kKey) {
            Key key;
            key.name = node.name;
            key.colour = colour;
            key.atPx = atPx;
            key.fromPx = atPx;
            key.toPx = atPx;
            out.keys.push_back(key);
        } else if (role == Roles::kKeyhole) {
            Keyhole keyhole;
            keyhole.name = node.name;
            keyhole.colour = colour;
            keyhole.atPx = atPx;
            out.keyholes.push_back(keyhole);
        } else {
            Door door;
            door.name = node.name;
            door.colour = colour;
            door.atPx = atPx;
            // A door with no body built is passed over, which is what a level
            // started without its statics leaves.
            const auto found = built.entities.find(node.name);
            if (found != built.entities.end()) door.body = found->second;
            doors.push_back(door);
        }
    }

    // Each keyhole takes the nearest door of its own colour. The original walks
    // the buckets around itself and falls back to a global search, so nothing
    // rests on the walk; and across all 41 pairs the nearest is the right one,
    // by a factor of four even in the one level holding two pairs of a colour.
    for (Keyhole& keyhole : out.keyholes) {
        const Door* best = nullptr;
        double bestDistance = 0.0;
        for (const Door& door : doors) {
            if (door.colour != keyhole.colour) continue;
            const double distance = Squared(door.atPx - keyhole.atPx);
            if (best != nullptr && distance >= bestDistance) continue;
            best = &door;
            bestDistance = distance;
        }
        if (best == nullptr) {
            error = keyhole.name + " is a " + keyhole.colour + " keyhole, and the level has no door of that colour";
            return false;
        }
        keyhole.doorName = best->name;
        keyhole.door = best->body;
    }

    return true;
}

} // namespace MagicPortals::Keys
