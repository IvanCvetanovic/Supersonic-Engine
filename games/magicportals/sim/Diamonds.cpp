#include "sim/Diamonds.hpp"

#include "sim/Units.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Diamonds {

namespace {

namespace Json = Supersonic::Json;
using Supersonic::TransformComponent;

glm::dvec2 PxOf(entt::registry& registry, entt::entity body) {
    return Units::ToPixels(registry.get<TransformComponent>(body).position);
}

double Squared(const glm::dvec2& v) {
    return v.x * v.x + v.y * v.y;
}

// Not State::Standing, which counts diamonds: this is whether an entity is still
// there to be measured against.
bool Alive(entt::registry& registry, entt::entity body) {
    return body != entt::null && registry.valid(body) && registry.all_of<TransformComponent>(body);
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
    const auto name = [&](const char* group, std::string& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has("entity_name") || !block["entity_name"].IsString()) {
            error = path + ": " + std::string(group) + ".entity_name is missing or not a string";
            return false;
        }
        into = block["entity_name"].AsString();
        return true;
    };

    Rules read;
    if (!number("shock", "range_px", read.rangePx)) return false;
    if (!number("shock", "leash_px", read.leashPx)) return false;
    if (!number("shock", "reaim_ms", read.reaimMs)) return false;
    if (!number("shock", "stride_ms", read.strideMs)) return false;
    if (!name("shock", read.shockName)) return false;
    if (!name("fire", read.fireName)) return false;

    // Each of these would look like a level that works. A range of nothing is a
    // diamond that can never be picked up and a minion that can never be struck;
    // a leash shorter than the range would snap one onto its owner before it
    // could ever trail; a stride or re-aim of nothing divides by zero. And two
    // names that are equal would hand the fire diamond this behaviour, which is
    // the one thing selecting by name exists to prevent.
    if (read.rangePx <= 0.0) {
        error = path + ": shock.range_px is above zero";
        return false;
    }
    if (read.leashPx <= read.rangePx) {
        error = path + ": shock.leash_px is longer than shock.range_px";
        return false;
    }
    if (read.reaimMs <= 0.0) {
        error = path + ": shock.reaim_ms is above zero";
        return false;
    }
    if (read.strideMs <= 0.0) {
        error = path + ": shock.stride_ms is above zero";
        return false;
    }
    if (read.shockName.empty() || read.fireName.empty()) {
        error = path + ": shock.entity_name and fire.entity_name both name an entity";
        return false;
    }
    if (read.shockName == read.fireName) {
        error = path + ": shock.entity_name and fire.entity_name are different entities";
        return false;
    }

    out = std::move(read);
    return true;
}

std::vector<entt::entity> State::Tick(entt::registry& registry, const std::vector<entt::entity>& carriers,
                                      const std::vector<entt::entity>& prey, float dt) {
    std::vector<entt::entity> hit;
    const double ms = static_cast<double>(dt) * 1000.0;
    const double range2 = rules.rangePx * rules.rangePx;

    for (Diamond& diamond : diamonds) {
        if (diamond.gone) continue;

        // The top-level branch, on ownerID. Unowned: poll for a carrier and then
        // RETURN - the original's arm ends in a jump to the function's exit, so
        // the diamond does nothing else on the frame it is taken. It is the next
        // frame that finds ownerID set and runs everything below.
        if (diamond.owner == entt::null) {
            if (Carry::Acquire(diamond, registry, carriers, rules.rangePx)) ++picked;
            continue;
        }

        // Carried. A lost owner puts it back to unowned - the original writes
        // ownerID = -1 and nothing more - and the payload below still runs on
        // that frame, because it sits after the branch rather than inside it.
        if (!Carry::Drop(diamond, registry)) Carry::Trail(diamond, registry, rules, ms);

        // The payload: the first minion within the SAME range dies, and the
        // diamond goes with it. Destroying it is Minions' to do, not this
        // module's, so it is handed back instead.
        for (const entt::entity body : prey) {
            if (!Alive(registry, body)) continue;
            if (Squared(PxOf(registry, body) - diamond.atPx) >= range2) continue;
            diamond.gone = true;
            ++struck;
            hit.push_back(body);
            break;
        }
    }

    return hit;
}

const Diamond* State::Find(const std::string& name) const {
    for (const Diamond& diamond : diamonds) {
        if (diamond.name == name) return &diamond;
    }
    return nullptr;
}

std::size_t State::Standing() const {
    std::size_t left = 0;
    for (const Diamond& diamond : diamonds) {
        if (!diamond.gone) ++left;
    }
    return left;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;

        // By ENTITY NAME, not by role. shock_diamond.ent and fire_diamond.ent
        // share the `pickup` role and are different mechanisms; the role table
        // cannot tell them apart and this module must.
        if (Roles::EntityName(node) != rules.shockName) continue;

        glm::dvec2 atPx(0.0, 0.0);
        if (!PositionOf(node, atPx)) {
            error = node.name + " is a shock diamond with no position";
            return false;
        }

        Diamond diamond;
        diamond.name = node.name;
        diamond.atPx = atPx;
        diamond.fromPx = atPx;
        diamond.toPx = atPx;
        out.diamonds.push_back(diamond);
    }

    (void)roles;
    return true;
}

} // namespace MagicPortals::Diamonds
