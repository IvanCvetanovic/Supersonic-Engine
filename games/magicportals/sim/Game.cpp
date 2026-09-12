#include "sim/Game.hpp"

#include "core/Components.hpp"
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

// The beholder dead: the level's button goes to its button_dest, its box and
// its body both, so what presses it and what draws it follow.
void RaiseButton(entt::registry& registry, Level& level) {
    const glm::vec3 at = Units::ToWorld(level.boss.buttonDestPx.x, level.boss.buttonDestPx.y);
    for (Puzzle::Button& button : level.channels.buttons) {
        if (button.name == level.boss.buttonNode) button.box.centre = glm::vec2(at.x, at.y);
    }
    const auto found = level.built.entities.find(level.boss.buttonNode);
    if (found != level.built.entities.end() && registry.valid(found->second)) {
        auto& transform = registry.get<Supersonic::TransformComponent>(found->second);
        transform.position = glm::vec3(at.x, at.y, transform.position.z);
    }
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
    // The remake's portals.json marks its transit block a guess. The port plays
    // the original's rule, decoded, from its own transit.json.
    if (!Portal::LoadTransit(portDataDirectory + "/transit.json", read.portals.transit, error)) return false;
    // And over its guessed collision_radius_px, the two decoded radii: a
    // portal's own, and the quite separate one an antiportal refuses a tap in.
    if (!Portals::LoadPlacement(portDataDirectory + "/placement.json", read.portals, error)) return false;
    if (!Mover::LoadRules(portDataDirectory + "/movers.json", read.movers, error)) return false;
    if (!Demolish::LoadRules(portDataDirectory + "/demolish.json", read.demolish, error)) return false;
    if (!Launchers::LoadRules(portDataDirectory + "/launchers.json", read.launchers, error)) return false;
    if (!Shot::LoadRules(portDataDirectory + "/shot.json", read.shot, error)) return false;
    if (!Boss::LoadRules(portDataDirectory + "/boss.json", read.boss, error)) return false;
    if (!Turrets::LoadRules(portDataDirectory + "/turrets.json", read.turrets, error)) return false;
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
    if (!Boss::Find(data.scene, data.roles, data.boss, data.launchers, out.boss, error)) return false;
    if (!Turrets::Find(data.scene, data.roles, data.turrets, out.turrets, error)) return false;
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
    // And what the carrancas spit. Their fireballs are sensors with no body, so
    // nothing here hands them to the portals or the demolisher as a thrown stone
    // is handed; Turrets.hpp says why that is a step of its own.
    level.turrets.Fire(dt);
    level.boss.BeforeStep(registry);
    if (level.player != entt::null) Player::Steer(registry, level.player, data.tuning, direction, dt);
}

void AfterStep(entt::registry& registry, Level& level, float dt) {
    level.goals.Tick(registry, level.player, dt);
    level.hazards.Tick(registry, level.player);
    // The fireballs fly on where the step left the player, and judge it there,
    // as the hazards above them do.
    level.turrets.Tick(registry, level.player, dt);
    level.demolish.Tick(registry);
    for (const entt::entity gone : level.launchers.Cull(registry)) Forget(level, gone);
    // The beholder's rocks, against what the step ran them into.
    for (const entt::entity gone : level.boss.Contacts(registry, level.player, level.portals, dt)) Forget(level, gone);
    level.portals.Tick(registry, dt);
    // Then its turn, on where the portals left things. A rock it drops travels
    // and breaks walls as a thrown one does.
    std::vector<entt::entity> stones;
    stones.reserve(level.demolish.stones.size());
    for (const Demolish::Stone& stone : level.demolish.stones) stones.push_back(stone.body);
    const Boss::State::Turn turn = level.boss.Tick(registry, level.player, stones, dt);
    for (const entt::entity gone : turn.broken) Forget(level, gone);
    for (const Boss::Rock& rock : turn.dropped) {
        if (level.boss.rock.teleportable) level.portals.travellers.push_back(rock.body);
        if (level.boss.rock.demolisher) level.demolish.stones.push_back(Demolish::Stone{rock.name, rock.body});
    }
    if (turn.buttonRaised) RaiseButton(registry, level);
    // Whatever kills the player, it dies as a hazard kills it.
    if (level.boss.playerKilled && !level.hazards.playerDied) {
        level.hazards.playerDied = true;
        level.hazards.killedBy = level.boss.killedBy;
    }
    if (level.turrets.playerKilled && !level.hazards.playerDied) {
        level.hazards.playerDied = true;
        level.hazards.killedBy = level.turrets.killedBy;
    }
}

void Tick(const Data& data, entt::registry& registry, Level& level, float direction, float dt) {
    BeforeStep(data, registry, level, direction, dt);
    Supersonic::PhysicsSystem::Update(registry, dt);
    AfterStep(registry, level, dt);
}

} // namespace MagicPortals::Game
