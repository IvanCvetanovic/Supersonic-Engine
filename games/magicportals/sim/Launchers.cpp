#include "sim/Launchers.hpp"

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace MagicPortals::Launchers {

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    namespace Json = Supersonic::Json;
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
    if (!root.IsObject() || !root.Has("throwables") || !root["throwables"].IsObject() || !root.Has("despawners") ||
        !root["despawners"].IsObject() || !root.Has("first_throw") || !root["first_throw"].IsObject()) {
        error = path + ": throwables, despawners and first_throw are each an object";
        return false;
    }

    Rules read;
    for (const auto& [name, entry] : root["throwables"].AsObject()) {
        if (!name.empty() && name[0] == '_') continue;
        if (!entry.IsObject() || !entry.Has("radius_px") || !entry["radius_px"].IsNumber() ||
            !entry.Has("demolisher") || !entry["demolisher"].IsBool() || !entry.Has("teleportable") ||
            !entry["teleportable"].IsBool() || !entry.Has("sprite") || !entry["sprite"].IsString()) {
            error = path + ": throwables." + name + " needs radius_px, demolisher, teleportable and sprite";
            return false;
        }
        Throwable throwable;
        throwable.radiusPx = entry["radius_px"].AsNumber();
        throwable.demolisher = entry["demolisher"].AsBool();
        throwable.teleportable = entry["teleportable"].AsBool();
        throwable.sprite = entry["sprite"].AsString("");
        read.throwables[name] = throwable;
    }
    const Json::Value& names = root["despawners"]["names"];
    if (!names.IsArray()) {
        error = path + ": despawners.names is not an array";
        return false;
    }
    for (const Json::Value& name : names.AsArray()) {
        if (!name.IsString()) {
            error = path + ": despawners.names lists something that is not a string";
            return false;
        }
        read.despawners.push_back(name.AsString());
    }
    if (!root["first_throw"].Has("after_strides") || !root["first_throw"]["after_strides"].IsNumber()) {
        error = path + ": first_throw.after_strides is missing or not a number";
        return false;
    }
    read.firstThrowStrides = root["first_throw"]["after_strides"].AsNumber();
    out = std::move(read);
    return true;
}

std::vector<Thrown> State::Throw(entt::registry& registry, float dt) {
    std::vector<Thrown> now;
    for (std::size_t i = 0; i < launchers.size(); ++i) {
        Launcher& launcher = launchers[i];
        launcher.leftS -= dt;
        if (launcher.leftS > 0.0) continue;
        launcher.leftS += launcher.strideS;
        Thrown thrown;
        thrown.name = launcher.name + "#" + std::to_string(++launcher.thrown);
        thrown.launcher = i;
        thrown.is = launcher.throws;
        thrown.body = LevelBuilder::BuildRigidCircle(registry, thrown.name, launcher.atPx, launcher.throws.radiusPx,
                                                     LevelBuilder::Options{});
        live.push_back(thrown);
        now.push_back(thrown);
    }
    return now;
}

std::vector<entt::entity> State::Cull(entt::registry& registry) {
    std::vector<entt::entity> gone;
    for (const Thrown& thrown : live) {
        if (!registry.valid(thrown.body)) {
            gone.push_back(thrown.body);
            continue;
        }
        const Launcher& launcher = launchers[thrown.launcher];
        const glm::dvec2 at = Units::ToPixels(registry.get<Supersonic::TransformComponent>(thrown.body).position);
        bool out = at.x < launcher.minPx.x || at.x > launcher.maxPx.x || at.y < launcher.minPx.y ||
                   at.y > launcher.maxPx.y;
        for (const Despawner& despawner : despawners) {
            if (out) break;
            out = Trigger::Overlaps(registry, thrown.body, despawner.box);
        }
        if (!out) continue;
        registry.destroy(thrown.body);
        gone.push_back(thrown.body);
    }
    live.erase(std::remove_if(live.begin(), live.end(),
                              [&gone](const Thrown& thrown) {
                                  return std::find(gone.begin(), gone.end(), thrown.body) != gone.end();
                              }),
               live.end());
    removed += static_cast<int>(gone.size());
    return gone;
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string entityName = Roles::EntityName(node);
        if (std::find(rules.despawners.begin(), rules.despawners.end(), entityName) != rules.despawners.end()) {
            Despawner despawner;
            despawner.name = node.name;
            if (!Trigger::FromNode(node, despawner.box, error)) return false;
            out.despawners.push_back(despawner);
            continue;
        }
        if (Roles::RoleOf(roles, node) != "launcher") continue;

        Launcher launcher;
        launcher.name = node.name;
        const Tscn::Value* position = node.Find("position");
        if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) {
            error = node.name + " has no position";
            return false;
        }
        launcher.atPx = glm::dvec2(position->numbers[0], position->numbers[1]);
        const Tscn::Value* entity = node.Meta("entity");
        if (entity == nullptr || entity->kind != Tscn::Value::Kind::String) {
            error = node.name + " names no entity to throw";
            return false;
        }
        launcher.entity = entity->text;
        const auto throwable = rules.throwables.find(launcher.entity);
        if (throwable == rules.throwables.end()) {
            error = node.name + " throws " + launcher.entity + ", which launchers.json does not describe";
            return false;
        }
        launcher.throws = throwable->second;
        const auto number = [&](const char* key, double& to) {
            const Tscn::Value* value = node.Meta(key);
            if (value == nullptr || !value->AsNumber(to)) {
                error = node.name + "'s " + key + " is missing or not a number";
                return false;
            }
            return true;
        };
        double strideMs = 0.0;
        if (!number("stride", strideMs) || !number("minX", launcher.minPx.x) || !number("minY", launcher.minPx.y) ||
            !number("maxX", launcher.maxPx.x) || !number("maxY", launcher.maxPx.y)) {
            return false;
        }
        if (strideMs <= 0.0) {
            error = node.name + "'s stride is not above 0";
            return false;
        }
        launcher.strideS = strideMs / 1000.0;
        launcher.leftS = rules.firstThrowStrides * launcher.strideS;
        out.launchers.push_back(launcher);
    }
    return true;
}

} // namespace MagicPortals::Launchers
