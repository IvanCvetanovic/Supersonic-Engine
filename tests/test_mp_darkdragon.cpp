// Chapter 4's boss: the dark dragon of level 4-32, and the last level of the game.
//
// WHAT THIS SUITE IS REALLY FOR. Every other boss in the game can be reached by
// walking into its level; this one has to be SUMMONED, and it can only be hurt by
// a bomb it lights itself. So the things worth pinning are the chain and the loop:
//
//   - it does NOT appear until the torch is lit, and then only after 3 s + 4 s;
//   - arriving takes `breakable_wall` away, and that wall is a real body;
//   - it is hurt ONLY by a blast - no fireball, no shot, nothing else touches it;
//   - each wound takes the player's portals away and shortens its fire interval;
//   - and at hp 0 it drops the only key the level has, in the level's own colour.
//
// The inventory (test_mp_start) counts roles, and `boss_spawn` is one role for
// four different bosses - so it can say level31c plays without a single one of
// those being true. Only this suite can tell.
//
// Reads the converted levels from outside this repository, and skips, saying
// where it looked, when they are absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/DarkDragon.hpp"
#include "sim/Game.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

const std::string kLevel = "level31c";
// What level31c.tscn itself carries.
constexpr double kSpawnX = -144.0;
constexpr double kSpawnY = 210.0;
constexpr double kShoutX = -4.0;
constexpr double kShoutY = 140.0;
constexpr double kFightX = 16.0;
constexpr double kFightY = 56.0;
constexpr double kKeyX = 78.0;
constexpr double kKeyY = 208.0;

bool Open(const std::string& name, Game::Data& data, entt::registry& registry, Game::Level& level) {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-darkdragon";
    std::string error;
    if (!Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) ||
        !Game::Start(data, registry, level, error)) {
        CHECK_MSG(false, name + ": " + error);
        return false;
    }
    return true;
}

// Ticks until `done`, or for at most `ticks`. Returns how many it took.
template <typename Fn>
int RunUntil(Game::Data& data, entt::registry& registry, Game::Level& level, int ticks, Fn done) {
    for (int tick = 0; tick < ticks; ++tick) {
        if (done()) return tick;
        Game::Tick(data, registry, level, 0.0f, kStep);
    }
    return ticks;
}

void TheRulesRead() {
    DarkDragon::Rules rules;
    std::string error;
    const bool ok = DarkDragon::LoadRules(kPortData + "/darkdragon.json", rules, error);
    CHECK_MSG(ok, error);
    if (!ok) return;

    CHECK(rules.spawnName == "dark_dragon_spawn");
    CHECK(rules.entityName == "dark_dragon.ent");
    CHECK(rules.wallName == "breakable_wall");
    CHECK(rules.keyColour == "yellow");
    CHECK(::test::nearly(static_cast<float>(rules.armMs), 3000.0f));
    CHECK(::test::nearly(static_cast<float>(rules.summonMs), 4000.0f));
    CHECK_EQ(rules.maxHp, 3);
    CHECK(::test::nearly(static_cast<float>(rules.radiusPx), 110.0f));
    CHECK(::test::nearly(static_cast<float>(rules.fireIntervalMs), 4200.0f));
    CHECK(::test::nearly(static_cast<float>(rules.fireIntervalStepMs), 600.0f));
    CHECK(::test::nearly(static_cast<float>(rules.muzzlePx), 95.0f));

    // Seven waypoints carrying it in from off the left edge, 7400 ms in all.
    CHECK_EQ(rules.appearHoldMs.size(), std::size_t{7});
    double appearMs = 0.0;
    for (const double hold : rules.appearHoldMs) appearMs += hold;
    CHECK(::test::nearly(static_cast<float>(appearMs), 7400.0f));
    std::printf("  summoned %.0f + %.0f ms after the torch, arrives over %.0f ms, hp %d\n", rules.armMs,
                rules.summonMs, appearMs, rules.maxHp);
}

// Refused, each for a reason a data file could actually have.
void TheRulesAreRefusedWhenTheyDoNotMakeABoss() {
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "supersonic-test-mp-darkdragon-rules";
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);

    // A whole file, with one field swapped per case.
    const auto body = [](const char* hp, const char* radius, const char* step, const char* holds) {
        return std::string(R"({"summon":{"spawn":"s","entity":"e","wall":"w","arm_ms":3000,"summon_ms":4000},
            "body":{"radius_px":)") + radius + R"(,"max_hp":)" + hp + R"(},
            "fight":{"fire_interval_ms":4200,"fire_interval_step_ms":)" + step +
               R"(,"muzzle_px":95,"shout":"sh","fighting":"f"},
            "appearing":{"hold_ms":)" + holds + R"(,"shout_move_px":[8,-32]},
            "damage":{"first_ms":1000,"flash_ms":300,"flashes":6,"shout_move_px":[32,-32],
                      "torch_entity":"light_off.ent"},
            "dying":{"hover_ms":3000,"drift_ms":6000,"fall_move_px":[32,-64],"fall_px":256,
                     "key_pos":"k","platform_pos":"p","key_colour":"yellow",
                     "platform_entity":"plat.ent"}})";
    };
    const std::string sevenHolds = "[2500,0,1000,1000,700,2200,0]";

    const struct Case {
        const char* name;
        std::string text;
    } cases[] = {
        {"a boss with no hit points", body("0", "110", "600", sevenHolds.c_str())},
        {"a boss no blast can reach", body("3", "0", "600", sevenHolds.c_str())},
        // 3 hp and a 2100 step would leave the last wound a zero interval, which
        // is a boss that fires every tick.
        {"a step that outruns its own interval", body("3", "110", "2100", sevenHolds.c_str())},
        {"six waypoints instead of seven", body("3", "110", "600", "[2500,0,1000,1000,700,2200]")},
    };

    int refused = 0;
    for (const Case& one : cases) {
        const std::filesystem::path file = scratch / "darkdragon.json";
        {
            std::ofstream out(file, std::ios::binary);
            out << one.text;
        }
        DarkDragon::Rules rules;
        std::string error;
        const bool ok = DarkDragon::LoadRules(file.string(), rules, error);
        CHECK_MSG(!ok, std::string("refused: ") + one.name);
        if (!ok) ++refused;
    }
    CHECK_EQ(refused, 4);
}

void ItAnswersForItsOwnNodeAndNoOthers() {
    DarkDragon::Rules rules;
    std::string error;
    if (!DarkDragon::LoadRules(kPortData + "/darkdragon.json", rules, error)) return;

    const struct Case {
        const char* level;
        int expected;
    } cases[] = {{"level31c", 1}, {"level31", 0}, {"level31a", 0}, {"level31b", 0}};

    for (const Case& one : cases) {
        Tscn::Scene scene;
        Roles::Table roles;
        if (!Tscn::Load(kLevels + "/" + one.level + ".tscn", scene, error) ||
            !Roles::Load(kData + "/entity_roles.json", roles, error)) {
            CHECK_MSG(false, error);
            continue;
        }
        int claimed = 0;
        for (const Tscn::Node& node : scene.nodes) {
            if (node.parent != "." || Roles::RoleOf(roles, node) != Roles::kBossSpawn) continue;
            if (DarkDragon::Plays(rules, node)) ++claimed;
        }
        CHECK_MSG(claimed == one.expected,
                  std::string(one.level) + " has " + std::to_string(claimed) + " node(s) this boss claims");
    }
    CHECK(!Roles::IsPorted(Roles::kBossSpawn));
}

void TheLevelPlacesItWhereTheFileSaysIt() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;

    CHECK(level.darkDragon.present);
    if (!level.darkDragon.present) return;
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.spawnPx.x), static_cast<float>(kSpawnX)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.spawnPx.y), static_cast<float>(kSpawnY)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.shoutPx.x), static_cast<float>(kShoutX)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.shoutPx.y), static_cast<float>(kShoutY)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.fightingPx.x), static_cast<float>(kFightX)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.fightingPx.y), static_cast<float>(kFightY)));
    CHECK(level.darkDragon.hasKeyPos);
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.keyPosPx.x), static_cast<float>(kKeyX)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.keyPosPx.y), static_cast<float>(kKeyY)));

    // The wall it breaks, resolved to the NODE Demolish holds it by, and that
    // node is a real breakable with a body.
    CHECK(!level.darkDragon.wallNode.empty());
    const Demolish::Breakable* wall = nullptr;
    for (const Demolish::Breakable& one : level.demolish.breakables) {
        if (one.name == level.darkDragon.wallNode) wall = &one;
    }
    CHECK_MSG(wall != nullptr, "breakable_wall is a Demolish::Breakable");
    if (wall != nullptr) {
        CHECK(!wall->broken);
        CHECK(registry.valid(wall->body));
    }

    // The level it is in: a torch to summon it, a bomb to hurt it, and a yellow
    // lock its key is the only opener of.
    CHECK(!level.torch.lights.empty());
    CHECK(!level.fire.bombs.empty());
    CHECK(level.keys.keys.empty()); // the level places NO key: the fight is the lock
    CHECK(!level.keys.keyholes.empty());
    std::printf("  %s: %zu torch(es), %zu bomb(s), %zu keyhole(s) and no key of its own\n", kLevel.c_str(),
                level.torch.lights.size(), level.fire.bombs.size(), level.keys.keyholes.size());

    // And no other level in the game has one.
    for (const char* other : {"level31", "level31a", "level31b", "level30c"}) {
        Game::Data second;
        entt::registry secondRegistry;
        Game::Level secondLevel;
        if (!Open(other, second, secondRegistry, secondLevel)) continue;
        CHECK_MSG(!secondLevel.darkDragon.present, std::string(other) + " places no dark dragon");
    }
}

// THE CHAIN: it does not come until the player makes light.
void ItDoesNotComeUntilTheTorchIsLit() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!level.darkDragon.present) return;

    // Thirty seconds with the torch unlit, and nothing arrives. This is the check
    // that would catch a boss that simply spawns on a timer.
    for (int tick = 0; tick < 1800; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    CHECK(!level.darkDragon.armed);
    CHECK(!level.darkDragon.alive);
    CHECK(!level.darkDragon.summoned);

    // Light it - which is what a portal shot passing the torch does - and the
    // chain runs: 3000 ms to arm, then 4000 ms more.
    level.torch.lit = 1;
    const int armed = RunUntil(data, registry, level, 600, [&] { return level.darkDragon.armed; });
    CHECK(level.darkDragon.armed);
    CHECK(!level.darkDragon.alive);
    const int came = RunUntil(data, registry, level, 600, [&] { return level.darkDragon.alive; });
    CHECK(level.darkDragon.alive);
    CHECK_EQ(level.darkDragon.hp, level.darkDragon.rules.maxHp);
    std::printf("  armed after %.2f s of light, arrived %.2f s later\n", armed * kStep, came * kStep);
    CHECK(armed * kStep > 2.9f && armed * kStep < 3.2f);
    CHECK(came * kStep > 3.9f && came * kStep < 4.2f);

    // And arriving took the wall away - the body, not just a flag.
    bool broken = false;
    for (const Demolish::Breakable& one : level.demolish.breakables) {
        if (one.name != level.darkDragon.wallNode) continue;
        broken = one.broken;
        CHECK_MSG(one.body == entt::null, "the wall's body went with it");
    }
    CHECK_MSG(broken, "arriving broke breakable_wall");

    // It flies in from off the left edge and settles where `fighting` is.
    RunUntil(data, registry, level, 900, [&] { return level.darkDragon.phase == DarkDragon::Phase::Fighting; });
    CHECK(level.darkDragon.phase == DarkDragon::Phase::Fighting);
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.atPx.x), static_cast<float>(kFightX)));
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.atPx.y), static_cast<float>(kFightY)));
}

// Brings it in and leaves it fighting.
bool Summon(Game::Data& data, entt::registry& registry, Game::Level& level) {
    if (!level.darkDragon.present) return false;
    level.torch.lit = 1;
    RunUntil(data, registry, level, 1800,
             [&] { return level.darkDragon.phase == DarkDragon::Phase::Fighting && level.darkDragon.alive; });
    return level.darkDragon.alive && level.darkDragon.phase == DarkDragon::Phase::Fighting;
}

// THE LOOP: only a blast hurts it, and each wound costs the player their portals.
void OnlyABlastHurtsIt() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!Summon(data, registry, level)) return;

    const int hp = level.darkDragon.hp;

    // Ten seconds of its own fireballs flying about, and it is unhurt: a fireball
    // detonates a bomb but cannot touch the boss.
    for (int tick = 0; tick < 600; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    CHECK_EQ(level.darkDragon.hp, hp);
    CHECK_EQ(level.darkDragon.hits, 0);

    // Now the bomb, moved beside it as portalling it there would - and asked to go
    // off, which is what its own fireball does.
    Fire::Bomb* bomb = nullptr;
    for (Fire::Bomb& one : level.fire.bombs) {
        if (!one.blown && registry.valid(one.body)) bomb = &one;
    }
    CHECK(bomb != nullptr);
    if (bomb == nullptr) return;

    // Two portals on the board, so the wound has something to take away.
    level.portals.TryPlace(glm::dvec2(300.0, 200.0));
    level.portals.TryPlace(glm::dvec2(360.0, 200.0));
    const std::size_t placed = level.portals.placed.size();
    CHECK(placed > 0);

    registry.get<TransformComponent>(bomb->body).position =
        Units::ToWorld(level.darkDragon.atPx.x + 20.0, level.darkDragon.atPx.y);
    bomb->requested = true;
    Game::Tick(data, registry, level, 0.0f, kStep);

    CHECK_EQ(level.darkDragon.hits, 1);
    CHECK_EQ(level.darkDragon.hp, hp - 1);
    CHECK(level.darkDragon.phase == DarkDragon::Phase::Damage);
    // Wounding it takes every portal away.
    CHECK(level.portals.placed.empty());
    // But not the budget: killing portals is not a refund.
    CHECK(level.portals.portalsUsed > 0);
    std::printf("  a blast beside it: hp %d -> %d, and %zu portal(s) taken away\n", hp, level.darkDragon.hp, placed);

    // And it comes back to the fight, faster than it was.
    const double before = level.darkDragon.rules.fireIntervalMs;
    RunUntil(data, registry, level, 600, [&] { return level.darkDragon.phase == DarkDragon::Phase::Fighting; });
    CHECK(level.darkDragon.phase == DarkDragon::Phase::Fighting);
    CHECK(level.darkDragon.FireIntervalMs() < before);
    CHECK(::test::nearly(static_cast<float>(level.darkDragon.FireIntervalMs()),
                         static_cast<float>(before - level.darkDragon.rules.fireIntervalStepMs)));
}

// It shoots at the player, and the shot is a fireball that kills.
void ItSpitsAtThePlayer() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!Summon(data, registry, level)) return;

    // IT CANNOT SEE ACROSS ITS OWN ARENA, and that is the gate rather than a
    // fault. It fights at (16, 56) and the player spawns at (710, 48), 694 px away
    // through the pillars and platforms level31c stands between them - so with its
    // clock well past the interval it still does not shoot. The first cut of this
    // suite ticked six seconds here, asserted a shot, and failed for exactly this
    // reason: the assertion assumed line of sight the level does not give.
    {
        const std::size_t before = level.turrets.fireballs.size();
        const int fired = level.darkDragon.fired;
        level.darkDragon.elapsedMs = level.darkDragon.FireIntervalMs() + 1.0;
        Game::Tick(data, registry, level, 0.0f, kStep);
        CHECK_EQ(level.turrets.fireballs.size(), before);
        CHECK_EQ(level.darkDragon.fired, fired);
        std::printf("  refused the shot across %.0f px of its own arena\n",
                    std::abs(710.0 - level.darkDragon.atPx.x));
    }

    // And it shoots the moment the player comes to it. The clock is set rather
    // than waited out, because a player left standing for six seconds falls, and
    // what is being pinned here is the gate and not gravity.
    {
        const std::size_t before = level.turrets.fireballs.size();
        const int fired = level.darkDragon.fired;
        registry.get<TransformComponent>(level.player).position =
            Units::ToWorld(level.darkDragon.atPx.x, level.darkDragon.atPx.y + 40.0);
        level.darkDragon.elapsedMs = level.darkDragon.FireIntervalMs() + 1.0;
        Game::Tick(data, registry, level, 0.0f, kStep);

        CHECK(level.darkDragon.fired > fired);
        CHECK(level.turrets.fireballs.size() > before);
        bool mine = false;
        for (const Turrets::Fireball& ball : level.turrets.fireballs) {
            if (ball.name.rfind(level.darkDragon.name, 0) != 0) continue;
            mine = true;
            // A carranca's kills and a fire diamond's does not; this one kills.
            CHECK(ball.killsPlayer);
        }
        CHECK(mine);
        // And its clock restarted, so the next shot is a whole interval away.
        CHECK(level.darkDragon.elapsedMs < level.darkDragon.FireIntervalMs());
        std::printf("  and took it from 40 px away, at a %.0f ms interval\n", level.darkDragon.FireIntervalMs());
    }
}

// THE END OF THE GAME: it falls, and leaves the only key the level has.
void ItDropsTheLevelsOnlyKey() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!Summon(data, registry, level)) return;

    // One from death, so the next blast ends it.
    level.darkDragon.hp = 1;
    Fire::Bomb* bomb = nullptr;
    for (Fire::Bomb& one : level.fire.bombs) {
        if (!one.blown && registry.valid(one.body)) bomb = &one;
    }
    if (bomb == nullptr) {
        CHECK_MSG(false, "level31c places a bomb");
        return;
    }
    registry.get<TransformComponent>(bomb->body).position =
        Units::ToWorld(level.darkDragon.atPx.x, level.darkDragon.atPx.y + 20.0);
    bomb->requested = true;
    Game::Tick(data, registry, level, 0.0f, kStep);

    CHECK_EQ(level.darkDragon.hp, 0);
    CHECK(level.darkDragon.phase == DarkDragon::Phase::Dying);
    CHECK(level.keys.keys.empty()); // not yet: it falls first

    // 3000 ms hovering and 6000 ms falling.
    const int fell = RunUntil(data, registry, level, 900, [&] { return level.darkDragon.gone; });
    CHECK(level.darkDragon.gone);
    CHECK(!level.darkDragon.Alive());
    std::printf("  it fell over %.2f s and dropped its key\n", fell * kStep);
    CHECK(fell * kStep > 8.9f && fell * kStep < 9.3f);

    CHECK_EQ(level.keys.keys.size(), std::size_t{1});
    if (!level.keys.keys.empty()) {
        const Keys::Key& dropped = level.keys.keys.front();
        // The colour the level's keyhole and its locked door both carry.
        CHECK(dropped.colour == "yellow");
        CHECK(::test::nearly(static_cast<float>(dropped.atPx.x), static_cast<float>(kKeyX)));
        CHECK(::test::nearly(static_cast<float>(dropped.atPx.y), static_cast<float>(kKeyY)));
        bool matches = false;
        for (const Keys::Keyhole& hole : level.keys.keyholes) {
            if (hole.colour == dropped.colour) matches = true;
        }
        CHECK_MSG(matches, "the key it drops opens the keyhole the level places");
    }

    // AND THE PLATFORM, added at the same waypoint. Until the port had a way to
    // place a body with no node behind it this was a recorded gap, and with a
    // level edge in place it was the difference between the key being reachable
    // and the player falling out of the world going for it.
    const entt::entity platform = level.darkDragon.platformBody;
    CHECK_MSG(platform != entt::null && registry.valid(platform), "the death adds its platform");
    if (platform != entt::null && registry.valid(platform)) {
        const glm::dvec2 atPx = Units::ToPixels(registry.get<TransformComponent>(platform).position);
        // platform_pos, read off level31c.
        CHECK(::test::nearly(static_cast<float>(atPx.x), 160.0f));
        CHECK(::test::nearly(static_cast<float>(atPx.y), 240.0f));
        // The template sibling's own box, not a number this test invented.
        const auto& box = registry.get<Supersonic::BoxColliderComponent>(platform);
        const float widthPx = box.size.x * static_cast<float>(Units::kPixelsPerMetre);
        const float heightPx = box.size.y * static_cast<float>(Units::kPixelsPerMetre);
        CHECK(::test::nearly(widthPx, 64.0f));
        CHECK(::test::nearly(heightPx, 32.0f));
        std::printf("  and a %.0f x %.0f platform at (%.0f, %.0f), from the sibling the level places\n", widthPx,
                    heightPx, atPx.x, atPx.y);
    }

    // And it stays dead.
    for (int tick = 0; tick < 300; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    CHECK_EQ(level.keys.keys.size(), std::size_t{1});
    CHECK(level.darkDragon.gone);
}

void runTests() {
    TheRulesRead();
    TheRulesAreRefusedWhenTheyDoNotMakeABoss();
    ItAnswersForItsOwnNodeAndNoOthers();
    TheLevelPlacesItWhereTheFileSaysIt();
    ItDoesNotComeUntilTheTorchIsLit();
    OnlyABlastHurtsIt();
    ItSpitsAtThePlayer();
    ItDropsTheLevelsOnlyKey();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_darkdragon: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_darkdragon", 60);
}
