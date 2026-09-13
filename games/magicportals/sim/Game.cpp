#include "sim/Game.hpp"

#include "core/Components.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/Trigger.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <string>
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
    if (!Fire::LoadRules(portDataDirectory + "/fire.json", read.fire, error)) return false;
    if (!Hinge::LoadRules(portDataDirectory + "/hinge.json", read.hinge, error)) return false;
    if (!Minions::LoadRules(portDataDirectory + "/minions.json", read.minions, error)) return false;
    if (!Keys::LoadRules(portDataDirectory + "/keys.json", read.keys, error)) return false;
    if (!Diamonds::LoadRules(portDataDirectory + "/diamonds.json", read.diamonds, error)) return false;
    if (!Fields::LoadRules(portDataDirectory + "/fields.json", read.fields, error)) return false;
    if (!Ghost::LoadRules(portDataDirectory + "/ghost.json", read.ghost, error)) return false;
    if (!Torch::LoadRules(portDataDirectory + "/torch.json", read.torch, error)) return false;
    // And what the remake's role table calls a hazard but the original does not.
    if (!Hazards::LoadRules(portDataDirectory + "/hazards.json", read.hazards, error)) return false;
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
    // The two level-property flags, and they are no longer the same case.
    //
    // no_gravity is still refused, because it is a MOVEMENT MODE and not a
    // switch. The original sets world gravity to zero for such a level (against
    // (0, scale(10)) otherwise), and then reads the flag again in MainCharacter,
    // in GameStateController and - the part that makes it a mechanic -
    // PortalManager, where firing a portal calls applyImpulse. In those levels the
    // recoil of a shot is how the player moves. Started without that, a
    // zero-gravity level would drop everything in it and look like a level that
    // works, which is what this guard exists to prevent.
    //
    // darkest is CARRIED instead. Its whole effect in the original's own
    // level-properties reader is one SetAmbientLight(DARKEST_AMBIENT_LIGHT), and
    // this port has no ambient light: Sprites has no colour or tint for it to
    // multiply, so a dark level draws exactly as a lit one either way. Refusing
    // the level did not make that more honest - it only hid twelve levels whose
    // every ROLE the port already plays or does not. Its one gameplay consequence
    // is that a minion in such a level is blind, and minion sight is unbuilt in
    // every level alike; minions.json and art.json both say so.
    bool darkest = false;
    for (const Tscn::Node& node : data.scene.nodes) {
        if (node.parent != "." || Roles::RoleOf(data.roles, node) != Roles::kLevelProperties) continue;
        double on = 0.0;
        if (const Tscn::Value* value = node.Meta("no_gravity");
            value != nullptr && value->AsNumber(on) && on != 0.0) {
            error = node.name + " sets no_gravity, and that is not ported";
            return false;
        }
        on = 0.0;
        if (const Tscn::Value* value = node.Meta("darkest"); value != nullptr && value->AsNumber(on) && on != 0.0) {
            darkest = true;
        }
    }

    UseRemakeGravity(registry);
    LevelBuilder::Options options;
    options.prismDirectory = data.prisms;
    options.roles = &data.roles;
    options.withStatics = withStatics;
    out = Level{};
    out.darkest = darkest;
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
    if (!Hazards::Find(data.scene, data.roles, data.hazards, out.hazards, error)) return false;
    if (!Demolish::Find(data.scene, data.roles, out.built, data.demolish, out.demolish, error)) return false;
    if (!Launchers::Find(data.scene, data.roles, data.launchers, out.launchers, error)) return false;
    if (!Boss::Find(data.scene, data.roles, data.boss, data.launchers, out.boss, error)) return false;
    if (!Turrets::Find(data.scene, data.roles, data.turrets, out.turrets, error)) return false;
    if (!Fire::Find(data.scene, data.roles, out.built, data.fire, out.fire, error)) return false;
    if (!Hinge::Find(data.scene, data.roles, out.built, registry, data.hinge, out.hinge, error)) return false;
    if (!Minions::Find(data.scene, data.roles, data.minions, out.minions, error)) return false;
    if (!Keys::Find(data.scene, data.roles, out.built, data.keys, out.keys, error)) return false;
    if (!Diamonds::Find(data.scene, data.roles, data.diamonds, out.diamonds, error)) return false;
    if (!Fields::Find(data.scene, data.roles, out.built, data.fields, out.fields, error)) return false;
    if (!Ghost::Find(data.scene, data.roles, data.ghost, out.ghost, error)) return false;
    if (!Torch::Find(data.scene, data.roles, data.torch, out.torch, error)) return false;
    if (!Portals::Find(data.scene, data.roles, out.built, registry, out.player, data.portals, data.movers, data.shot,
                       out.portals, error)) {
        return false;
    }

    // And the minions the markers ask for, built last: a minion is teleportable,
    // so it joins the portals' travellers the way what a launcher throws does -
    // after Portals::Find, which is what fills that list to begin with.
    for (const entt::entity body : out.minions.Spawn(registry)) out.portals.travellers.push_back(body);
    return true;
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
    // The minions walk before the step, as the player is steered before it: what
    // sets a velocity belongs on this side, what judges a position on the other.
    level.minions.Tick(registry, dt);
    level.boss.BeforeStep(registry);
    if (level.player != entt::null) Player::Steer(registry, level.player, data.tuning, direction, dt);
}

void AfterStep(const Data& data, entt::registry& registry, Level& level, float dt) {
    level.goals.Tick(registry, level.player, dt);
    level.hazards.Tick(registry, level.player);
    // The fireballs fly on where the step left the player, and judge it there,
    // as the hazards above them do.
    level.turrets.Tick(registry, level.player, dt);
    level.demolish.Tick(registry);
    // The fire agents, what is burning down, and the bombs - before the portals,
    // which move things. What it takes away goes through Forget as a thrown body
    // does: a crate is teleportable, so a burning one can be in portals.travellers.
    for (const entt::entity gone : level.fire.Tick(registry, level.player, dt)) Forget(level, gone);
    // What a fireball ran into (ETHBeginContactCallback_fireball). A carranca's
    // and a fire diamond's are the same fireball.ent and meet the same things;
    // only killMainCharacter differs, and Turrets::Tick has already judged that.
    //
    // AFTER Fire::Tick on purpose. The original sets a flag and the bomb's own
    // callback reads it on a later frame, which is the split Fire.hpp keeps: a
    // chain ripples a tick at a time rather than collapsing into one frame.
    {
        std::vector<std::string> spent;
        const glm::vec2 half(static_cast<float>(Units::ToMetres(level.turrets.rules.hitPx.x * 0.5)),
                             static_cast<float>(Units::ToMetres(level.turrets.rules.hitPx.y * 0.5)));
        for (const Turrets::Fireball& ball : level.turrets.fireballs) {
            Trigger::Box box;
            const glm::vec3 centre = Units::ToWorld(ball.atPx.x, ball.atPx.y);
            box.centre = glm::vec2(centre.x, centre.y);
            box.half = half;

            bool struck = false;
            // A shock agent is the ONE sensor it acts on, tested by name in the
            // original, and it destroys that one and itself with it.
            for (const Fields::Field& field : level.fields.fields) {
                if (field.gone || !registry.valid(field.body)) continue;
                if (!Trigger::Overlaps(registry, field.body, box)) continue;
                const entt::entity gone = level.fields.Destroy(field.name, registry);
                if (gone != entt::null) Forget(level, gone);
                struck = true;
                break;
            }
            // Then the solid things: burn what is burnable, ask what is explosive
            // to go off. Both are the original's own calls, burn() and explode().
            if (!struck) {
                for (Fire::Burnable& burnable : level.fire.burnables) {
                    if (burnable.gone || burnable.burned || !registry.valid(burnable.body)) continue;
                    if (!Trigger::Overlaps(registry, burnable.body, box)) continue;
                    burnable.burned = true;
                    struck = true;
                    break;
                }
            }
            if (!struck) {
                for (Fire::Bomb& bomb : level.fire.bombs) {
                    if (bomb.blown || bomb.requested || !registry.valid(bomb.body)) continue;
                    if (!Trigger::Overlaps(registry, bomb.body, box)) continue;
                    bomb.requested = true;
                    struck = true;
                    break;
                }
            }
            // And chapter 3's boss, which is the ONLY thing a fireball can hurt
            // that Fire does not hold: the ghost is not a scene entity, so
            // Fire::Find never sees it. Its GHOST_RADIUS is a radius rather than
            // a box, and it has no collider here at all, so this is a plain
            // distance. Setting `burned` is the whole of it - Ghost::Tick reads
            // and clears the flag, which is isBurned followed by healBurn.
            if (!struck && level.ghost.Alive()) {
                const glm::dvec2 gap = ball.atPx - level.ghost.atPx;
                if (gap.x * gap.x + gap.y * gap.y <= level.ghost.rules.radiusPx * level.ghost.rules.radiusPx) {
                    level.ghost.burned = true;
                    struck = true;
                }
            }
            if (struck) spent.push_back(ball.name);
        }
        if (!spent.empty()) {
            std::erase_if(level.turrets.fireballs, [&spent](const Turrets::Fireball& ball) {
                return std::find(spent.begin(), spent.end(), ball.name) != spent.end();
            });
        }
    }
    for (const entt::entity gone : level.launchers.Cull(registry)) Forget(level, gone);
    // And a minion the step left in a killer floor. Not a hazard: that floor does
    // nothing to the player, which is the correction hazards.json records.
    for (const entt::entity gone : level.minions.Cull(registry)) Forget(level, gone);
    // The keys, on where the step left whoever carries them. Anything that is a
    // character OR a minion can pick one up, so the carriers are the player and
    // every minion still standing - the first place two of the port's own modules
    // meet. What a keyhole takes away is a door the builder made, so it goes
    // through Forget as a burnt crate does.
    {
        std::vector<entt::entity> carriers;
        carriers.reserve(level.minions.minions.size() + 1);
        if (level.player != entt::null) carriers.push_back(level.player);
        for (const Minions::Minion& minion : level.minions.minions) {
            if (!minion.gone && minion.body != entt::null) carriers.push_back(minion.body);
        }
        for (const entt::entity gone : level.keys.Tick(registry, carriers, dt)) Forget(level, gone);
    }
    // And the shock diamonds, which the OTHER of the two carry filters governs:
    // only a character may take one, so the player alone is offered it and every
    // minion is prey instead. What it strikes is destroyed by Minions rather than
    // here - that module walks its own list every frame, and a body destroyed
    // behind its back would still be in it.
    {
        std::vector<entt::entity> carriers;
        if (level.player != entt::null) carriers.push_back(level.player);
        std::vector<entt::entity> prey;
        prey.reserve(level.minions.minions.size());
        for (const Minions::Minion& minion : level.minions.minions) {
            if (!minion.gone && minion.body != entt::null) prey.push_back(minion.body);
        }
        const std::vector<entt::entity> struck = level.diamonds.Tick(registry, carriers, prey, dt);
        for (const entt::entity gone : level.minions.Take(registry, struck)) Forget(level, gone);
    }
    // turnProjectilesIntoFireBalls. While a fire diamond is carried its callback
    // walks every live projectile.ent every frame and calls burnProjectile, which
    // DELETES the shot and puts a fireball where it was, along the direction it
    // was going. So holding one does not change where a portal opens - it means no
    // portal opens at all, and the tap buys a fireball instead.
    //
    // The original needs a guard for this and the port does not. Its projectile
    // could in principle reach its destiny and open a portal in the frame before
    // the conversion catches it, so computePortalFinalPos aims a fire shot at
    // origin + (destPos - origin) * 64 - a point far outside the level, whose only
    // effect is an enormous stored range, because addProjectile normalizes the
    // direction and the 64 cannot move the aim. Here the conversion runs before
    // Portals::Tick advances the flight, so the shot cannot land first by ordering
    // and there is nothing to scale. Diamonds.hpp carries that decode.
    if (level.diamonds.carrierHasFire && level.portals.flight) {
        const Portals::Flight& shot = *level.portals.flight;
        const glm::dvec2 along = shot.toPx - shot.atPx;
        const double length = std::sqrt(along.x * along.x + along.y * along.y);
        if (length > 0.0) {
            Turrets::Fireball made;
            made.name = "fire_diamond#" + std::to_string(++level.turrets.converted);
            made.atPx = shot.atPx;
            made.velocityPx = (along / length) * level.turrets.rules.speedPx;
            // addFireball's killMainCharacter, passed CLEAR by burnProjectile: the
            // player cannot be hurt by a shot it fired itself.
            made.killsPlayer = false;
            level.turrets.fireballs.push_back(made);
        }
        level.portals.flight.reset();
    }
    // The shock rings swing, and judge where the step left the player. A poll
    // every frame rather than an entry test, which is what separates one of these
    // from a hazard.
    level.fields.Tick(registry, level.player, dt);
    // Chapter 3's boss. AFTER the fireball block above on purpose: a fireball
    // that reaches it this tick sets `burned`, and this reads it on the same
    // tick rather than the next.
    //
    // It REPORTS what it wants done rather than doing it - Minions owns minion
    // bodies, Keys owns keys and Diamonds owns diamonds, and each walks its own
    // list every frame.
    if (level.ghost.present) {
        // ONLY the escorts this boss called, and that is not a shortcut. The
        // original's rage arm seeks `minion.ent` by NAME, and what it spawns is a
        // minion.ent; level31b's own patroller is a ghost_minion.ent and is a
        // different entity, so it never satisfies that seek. Counting every
        // minion in the level would count that patroller, the count would never
        // reach zero, and the boss would sit in RAGE_MODE for ever without ever
        // summoning - a deadlock, not a slow fight.
        const std::string escort = level.ghost.name + "#";
        std::size_t standing = 0;
        for (const Minions::Minion& minion : level.minions.minions) {
            if (minion.gone || minion.body == entt::null || !registry.valid(minion.body)) continue;
            if (minion.name.rfind(escort, 0) != 0) continue;
            ++standing;
        }

        const Ghost::State::Turn turn = level.ghost.Tick(registry, level.player, standing, dt);

        // The escort, on the patrol the boss resolved when the level was found.
        if (turn.summon) {
            const std::string called = level.ghost.name + "#" + std::to_string(level.ghost.summons);
            const entt::entity body =
                level.minions.Summon(registry, called, level.ghost.summonPx, level.ghost.patrol);
            // A minion is teleportable, so it joins the travellers as one built
            // at the start of the level does.
            if (body != entt::null) level.portals.travellers.push_back(body);
        }

        // The key it drops, which is the only key level31b has: the fight IS the
        // lock. Its colour is key.ent's own, not the keyhole's.
        if (turn.droppedKey) {
            Keys::Key dropped;
            dropped.name = level.ghost.name + "#key";
            dropped.colour = level.ghost.rules.keyColour;
            dropped.atPx = turn.keyPx;
            dropped.fromPx = turn.keyPx;
            dropped.toPx = turn.keyPx;
            level.keys.keys.push_back(dropped);
        }

        // addEntityIfItCantBeFound: a fire diamond back at its spawn, and only
        // when none is left. That is what makes dropping one down the gutter
        // mouth survivable rather than final.
        if (turn.restockDiamond) {
            bool anyLeft = false;
            for (const Diamonds::Diamond& diamond : level.diamonds.diamonds) {
                if (diamond.fire && !diamond.gone) anyLeft = true;
            }
            if (!anyLeft) {
                for (Diamonds::Diamond& diamond : level.diamonds.diamonds) {
                    if (!diamond.fire || !diamond.gone) continue;
                    diamond.gone = false;
                    diamond.owner = entt::null;
                    diamond.atPx = level.ghost.diamondSpawnPx;
                    diamond.fromPx = diamond.atPx;
                    diamond.toPx = diamond.atPx;
                    diamond.sinceAimMs = 0.0;
                    break;
                }
            }
        }

        // The fifth death funnel, as the boss's, the carrancas', the fire's and
        // the rings' are: one death in the port and not six.
        if (level.ghost.playerKilled && !level.hazards.playerDied) {
            level.hazards.playerDied = true;
            level.hazards.killedBy = level.ghost.killedBy;
        }
    }
    // Chapter 4's torches. BEFORE Portals::Tick on purpose: lighting one calls
    // killProjectile in the original, so a shot that works a switch is SPENT and
    // must not go on to open a portal on the same tick it was consumed.
    //
    // Torch owns no bodies. The light wall carries metadata/breakable, so it is
    // already a Demolish::Breakable with a body and a box that Demolish walks
    // every frame - the removal goes through that list rather than behind it, as
    // a struck minion goes through Minions::Take.
    if (!level.torch.lights.empty()) {
        std::optional<glm::dvec2> flightPx;
        if (level.portals.flight) flightPx = level.portals.flight->atPx;
        std::vector<glm::dvec2> fireballPx;
        fireballPx.reserve(level.turrets.fireballs.size());
        for (const Turrets::Fireball& ball : level.turrets.fireballs) fireballPx.push_back(ball.atPx);

        const Torch::State::Turn turn = level.torch.Tick(flightPx, fireballPx, dt);

        // hasProjectileAround accepts a fireball as readily as a portal shot, so
        // either can be the one that is spent.
        if (turn.spentFlight) level.portals.flight.reset();
        if (turn.spentFireball >= 0 && turn.spentFireball < static_cast<int>(level.turrets.fireballs.size())) {
            level.turrets.fireballs.erase(level.turrets.fireballs.begin() + turn.spentFireball);
        }

        if (turn.takeWallAway) {
            for (Demolish::Breakable& breakable : level.demolish.breakables) {
                if (breakable.name != level.torch.wall.name || breakable.broken) continue;
                breakable.broken = true;
                breakable.brokenBy = level.torch.signal.fromTorch.empty() ? "a torch" : level.torch.signal.fromTorch;
                const entt::entity gone = breakable.body;
                breakable.body = entt::null;
                if (registry.valid(gone)) {
                    registry.destroy(gone);
                    Forget(level, gone);
                }
                break;
            }
        }

        // And the way back: a shot signal rebuilds the wall where it stood. This
        // is the only thing in the port that adds a level's own body mid-run, and
        // it is why AfterStep takes the Data at all.
        if (turn.putWallBack) {
            LevelBuilder::Options options;
            options.prismDirectory = data.prisms;
            options.roles = &data.roles;
            std::string built;
            const entt::entity wall =
                LevelBuilder::BuildEntity(data.scene, level.torch.wall.name, registry, options, level.built, built);
            if (wall != entt::null) {
                for (Demolish::Breakable& breakable : level.demolish.breakables) {
                    if (breakable.name != level.torch.wall.name) continue;
                    breakable.broken = false;
                    breakable.brokenBy.clear();
                    breakable.body = wall;
                    break;
                }
            }
        }
    }

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
    if (level.fire.playerKilled && !level.hazards.playerDied) {
        level.hazards.playerDied = true;
        level.hazards.killedBy = level.fire.killedBy;
    }
    if (level.fields.playerKilled && !level.hazards.playerDied) {
        level.hazards.playerDied = true;
        level.hazards.killedBy = level.fields.killedBy;
    }
}

void Tick(const Data& data, entt::registry& registry, Level& level, float direction, float dt) {
    BeforeStep(data, registry, level, direction, dt);
    Supersonic::PhysicsSystem::Update(registry, dt);
    AfterStep(data, registry, level, dt);
}

} // namespace MagicPortals::Game
