#include "sim/Game.hpp"

#include "core/PhysicsSettings.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace MagicPortals::Game {

namespace {

// A body a launcher threw and has taken back, dropped from what tracked it.
void Forget(Level& level, entt::entity body) {
    auto& travellers = level.portals.travellers;
    travellers.erase(std::remove(travellers.begin(), travellers.end(), body), travellers.end());
    auto& stones = level.demolish.stones;
    stones.erase(std::remove_if(stones.begin(), stones.end(),
                                [body](const Demolish::Stone& stone) { return stone.body == body; }),
                 stones.end());
}

} // namespace

bool LoadData(const std::string& levelPath, const std::string& dataDirectory,
              const std::filesystem::path& prismDirectory, Data& out, std::string& error,
              const std::string& portDataDirectory) {
    Data read;
    if (!Tscn::Load(levelPath, read.scene, error)) return false;
    if (!Roles::Load(dataDirectory + "/entity_roles.json", read.roles, error)) return false;
    if (!Player::LoadTuning(dataDirectory + "/player.json", read.tuning, error)) return false;
    if (!Goals::LoadRules(dataDirectory + "/portals.json", read.goals, error)) return false;
    if (!Portals::LoadRules(dataDirectory + "/portals.json", read.portals, error)) return false;
    if (!Mover::LoadRules(portDataDirectory + "/movers.json", read.movers, error)) return false;
    if (!Demolish::LoadRules(portDataDirectory + "/demolish.json", read.demolish, error)) return false;
    if (!Launchers::LoadRules(portDataDirectory + "/launchers.json", read.launchers, error)) return false;
    if (!Shot::LoadRules(portDataDirectory + "/shot.json", read.shot, error)) return false;
    std::error_code ec;
    std::filesystem::create_directories(prismDirectory, ec);
    read.prisms = prismDirectory;
    out = std::move(read);
    return true;
}

void UseRemakeGravity(entt::registry& registry) {
    Supersonic::PhysicsSettings settings;
    settings.gravity = glm::vec3(0.0f, -Units::ToMetres(Units::kRemakeWorldGravityPx), 0.0f);
    registry.ctx().insert_or_assign<Supersonic::PhysicsSettings>(std::move(settings));
}

bool Start(const Data& data, entt::registry& registry, Level& out, std::string& error, bool withStatics) {
    // Level flags the port does not play yet. Started without them, a zero-gravity
    // level would drop everything in it and a dark one would be lit, and either
    // would look like a level that works.
    for (const Tscn::Node& node : data.scene.nodes) {
        if (node.parent != "." || Roles::RoleOf(data.roles, node) != Roles::kLevelProperties) continue;
        for (const char* flag : {"no_gravity", "darkest"}) {
            double on = 0.0;
            if (const Tscn::Value* value = node.Meta(flag); value != nullptr && value->AsNumber(on) && on != 0.0) {
                error = node.name + " sets " + flag + ", and that is not ported";
                return false;
            }
        }
    }

    UseRemakeGravity(registry);
    LevelBuilder::Options options;
    options.prismDirectory = data.prisms;
    options.roles = &data.roles;
    options.withStatics = withStatics;
    out = Level{};
    if (!LevelBuilder::Build(data.scene, registry, options, out.built, error)) return false;
    if (withStatics && !Puzzle::Wire(data.scene, data.roles, out.built, out.channels, error)) return false;
    if (withStatics && !Mover::Wire(data.scene, data.roles, out.built, data.movers, out.movers, error)) return false;

    const Tscn::Node* spawn = nullptr;
    for (const Tscn::Node& node : data.scene.nodes) {
        if (node.parent == "." && Roles::RoleOf(data.roles, node) == Roles::kPlayerSpawn) {
            spawn = &node;
            break;
        }
    }
    const Tscn::Value* at = spawn != nullptr ? spawn->Find("position") : nullptr;
    if (at == nullptr || at->kind != Tscn::Value::Kind::Vector2) {
        error = "the level has no player_spawn with a position";
        return false;
    }
    out.player = Player::Spawn(registry, glm::dvec2(at->numbers[0], at->numbers[1]), data.tuning);

    if (!Goals::Find(data.scene, data.roles, data.goals, out.goals, error)) return false;
    if (!Hazards::Find(data.scene, data.roles, out.hazards, error)) return false;
    if (!Demolish::Find(data.scene, data.roles, out.built, data.demolish, out.demolish, error)) return false;
    if (!Launchers::Find(data.scene, data.roles, data.launchers, out.launchers, error)) return false;
    return Portals::Find(data.scene, data.roles, out.built, registry, out.player, data.portals, data.movers, data.shot,
                         out.portals, error);
}

void BeforeStep(const Data& data, entt::registry& registry, Level& level, float direction, float dt) {
    level.channels.Tick(registry, dt);
    level.movers.Tick(registry, dt);
    // What a launcher throws travels and breaks walls as a stone the level
    // places does.
    for (const Launchers::Thrown& thrown : level.launchers.Throw(registry, dt)) {
        if (thrown.is.teleportable) level.portals.travellers.push_back(thrown.body);
        if (thrown.is.demolisher) level.demolish.stones.push_back(Demolish::Stone{thrown.name, thrown.body});
    }
    if (level.player != entt::null) Player::Steer(registry, level.player, data.tuning, direction, dt);
}

void AfterStep(entt::registry& registry, Level& level, float dt) {
    level.goals.Tick(registry, level.player, dt);
    level.hazards.Tick(registry, level.player);
    level.demolish.Tick(registry);
    for (const entt::entity gone : level.launchers.Cull(registry)) Forget(level, gone);
    level.portals.Tick(registry, dt);
}

void Tick(const Data& data, entt::registry& registry, Level& level, float direction, float dt) {
    BeforeStep(data, registry, level, direction, dt);
    Supersonic::PhysicsSystem::Update(registry, dt);
    AfterStep(registry, level, dt);
}

} // namespace MagicPortals::Game
