// Every converted level, started the way the port starts one: Game::LoadData,
// then Game::Start into a fresh registry, then three seconds of ticks with no
// input, in which the player has to land.
//
// This is the remaster's inventory. Each level comes out one of three ways:
//   - refused: Game::Start says why, and the reasons, counted, are the order
//     the remaining roles are built in;
//   - starts, but carries roles the port leaves inert (Roles::IsPorted), listed
//     against it;
//   - plays: starts, lands, and every role in it is one the port plays.
// docs/planning/2026-09-11-magic-portals-remaster.md reads its numbers from here.
//
// What is pinned: the 128 files are there, every level either starts or says
// why, a level that starts lands, and each chapter's counts of levels that start
// and that play do not fall. The floors are raised as roles land.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Lighting.hpp"
#include "sim/Player.hpp"
#include "sim/Roles.hpp"
#include "sim/Sprites.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;
constexpr int kLandingTicks = 180;
constexpr int kLevelsPerChapter = 32;

// The converter names chapter 1's levels level0..level31, and chapters 2 to 4
// the same with a, b and c after the number.
struct Chapter {
    const char* suffix;
    const char* name;
    int startFloor; // levels that start today
    int playFloor;  // levels that play today
};
// The play floors for chapters 2, 3 and 4 were left at zero while those chapters
// were being built, so three chapters' worth of gains were protected by nothing:
// a regression that stopped 31 levels playing would have passed. They are raised
// here to what both toolchains measure, which is what "the floors are raised as
// roles land" was always meant to mean.
const Chapter kChapters[] = {
    {"", "chapter 1", 32, 32},
    {"a", "chapter 2", 32, 32}, // complete: level31a's dragon was the last of them
    {"b", "chapter 3", 32, 32}, // complete
    // ALL 32 START AND ALL 32 PLAY. `no_gravity` was the last thing in the whole
    // game that stopped a level STARTING; `bouncer` and `gravity_well` were the
    // last two roles that stopped one PLAYING; and level31c's dark dragon was the
    // last unbuilt thing in the game.
    {"c", "chapter 4", 32, 32},
};

// The reason with the node's name taken off the front, so that the same refusal
// in two levels counts once: "crystal_ent_998 is a timed crystal, ..." and
// "crystal_ent_12 is a timed crystal, ..." are one reason.
std::string ReasonOf(const std::string& error) {
    const std::size_t cut = error.find_first_of(" :'");
    if (cut == std::string::npos || cut == 0) return error;
    const std::size_t from = error.compare(cut, 2, "'s") == 0 ? cut + 2 : cut;
    return "<node>" + error.substr(from);
}

// How far a player in a zero-gravity level may move in three seconds with no
// input. Nothing pulls it and nothing steers it, so the honest answer is zero,
// and in level2c it is exactly that: 0.0000 px, measured on both toolchains.
//
// TWO things move it anyway in other levels, and neither is a fall:
//   - a level that places the player overlapping what is under it has the solver
//     push it out ONCE and then stop - 3.75 px in level1c, level12c and level15c;
//   - and level1c also places a BOUNCER, a slab that bobs a pixel (Bounce.hpp),
//     so a player resting on one RIDES it for as long as the level runs rather
//     than ever settling. That is the slab carrying what stands on it, which is
//     the whole point of `bouncer` being in Roles::Moves.
//
// This is the slack for both: a tile. A real fall is some 4400 px over the same
// three seconds, so nothing that is actually falling can hide under it, and
// test_mp_zerog measures each properly rather than lumping them together -
// stillness in level2c, where nothing else moves, and the ride in level1c,
// where something does.
constexpr double kFloatDriftPx = 16.0;

// And how far one may move in a zero-gravity level THAT HOLDS A GRAVITY WELL,
// where the stillness above is the wrong question: a well pulls, so the player
// moves by design, and level17c holds four of them.
//
// This is deliberately not "anything at all". What it still catches is what a
// newly written force gets wrong: a normalize at dead centre giving a NaN, or a
// runaway acceleration flinging the player out of the world. Both would sail
// past a check that merely asked whether the entity still existed - which is
// what this line said for one draft, and it was no check at all.
constexpr double kPulledDriftPx = 1000.0;

struct Outcome {
    bool started = false;
    bool landed = false;   // or, in a zero-gravity level, stayed afloat
    bool noGravity = false;
    bool pulled = false;   // a zero-gravity level with a well in it
    std::string why;
    std::map<std::string, int> ignored; // role -> placements the port leaves inert

    bool Plays() const { return started && landed && ignored.empty(); }
};

Outcome StartAndDrop(const std::string& path) {
    Outcome outcome;
    Game::Data data;
    if (!Game::LoadData(path, kData, std::filesystem::temp_directory_path() / "supersonic-test-mp-start", data,
                        outcome.why)) {
        return outcome;
    }
    entt::registry registry;
    Game::Level level;
    if (!Game::Start(data, registry, level, outcome.why)) return outcome;
    outcome.started = true;
    for (const Tscn::Node& node : data.scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(data.roles, node);
        // A boss spawn plays when it is a boss the port HAS: level31's beholder,
        // level31b's ghost, or level31a's dragon. Asked per NODE and never per
        // role, which is what lets one boss be built without claiming another -
        // chapter 4's dark dragon stays inert, and stays honestly counted.
        //
        // Dragon::Plays answers for TWO entity names, because level31a places
        // dragon.ent and dragon_knight_spawn under the one role; dragon.json says
        // on what ground the second is claimed.
        const bool boss = role == Roles::kBossSpawn &&
                          (Boss::Plays(data.boss, node) || Ghost::Plays(data.ghost, node) ||
                           Dragon::Plays(data.dragon, node) || DarkDragon::Plays(data.darkDragon, node));
        if (!Roles::IsPorted(role) && !boss) ++outcome.ignored[role];
    }
    // A ZERO-GRAVITY LEVEL'S PLAYER NEVER LANDS, and asking it to would fail all
    // eighteen of them. What is asked instead is the DUAL of landing: with no
    // gravity and no steering, it does not move at all. Skipping the check for
    // these levels would have been the dishonest fix - a level quietly falling
    // through its own floor would then have read as playing, which is the same
    // blindness light_wall.ent had before test_mp_torch watched the body.
    // A ZERO-GRAVITY LEVEL WITH A GRAVITY WELL IN IT IS NOT STILL, and asking it
    // to be is asking the wrong question. The stillness check below rests on
    // "nothing pulls it and nothing steers it"; a well is precisely a thing that
    // pulls it, so level17c - which holds FOUR, more than any level in the game -
    // moves a stationary player by design. What is asked of those levels instead
    // is that the player is still SOMEWHERE, which is all this inventory can
    // honestly claim about a room whose contents are dragging it about.
    // test_mp_wells measures the pull properly.
    outcome.noGravity = level.noGravity;
    outcome.pulled = !level.wells.wells.empty();
    const glm::dvec2 spawnPx = Units::ToPixels(registry.get<Supersonic::TransformComponent>(level.player).position);
    for (int tick = 0; tick < kLandingTicks && !outcome.landed; ++tick) {
        Game::Tick(data, registry, level, 0.0f, kStep);
        if (!level.noGravity) outcome.landed = Player::Grounded(registry, level.player, data.tuning);
    }
    if (level.noGravity) {
        const glm::dvec2 nowPx = Units::ToPixels(registry.get<Supersonic::TransformComponent>(level.player).position);
        const glm::dvec2 drift = nowPx - spawnPx;
        const double moved = std::sqrt(drift.x * drift.x + drift.y * drift.y);
        outcome.landed = outcome.pulled ? (std::isfinite(moved) && moved < kPulledDriftPx) : moved < kFloatDriftPx;
    }
    return outcome;
}

using Tally = std::map<std::string, std::vector<std::string>>; // reason or role -> levels

void PrintTally(const char* heading, const Tally& tally) {
    std::printf("  %s, most levels first:\n", heading);
    std::vector<std::pair<std::string, std::vector<std::string>>> ordered(tally.begin(), tally.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    for (const auto& [what, levels] : ordered) {
        std::string list;
        for (const std::string& level : levels) list += " " + level;
        std::printf("    %3zu  %s:%s\n", levels.size(), what.c_str(), list.c_str());
    }
}

void EveryLevelStartsOrSaysWhy() {
    Tally refusals;
    Tally inert;
    int startedEverywhere = 0;
    int playsEverywhere = 0;
    for (const Chapter& chapter : kChapters) {
        int started = 0;
        int plays = 0;
        std::printf("  %s\n", chapter.name);
        for (int i = 0; i < kLevelsPerChapter; ++i) {
            const std::string name = "level" + std::to_string(i) + chapter.suffix;
            const std::string path = kLevels + "/" + name + ".tscn";
            std::error_code ec;
            const bool present = std::filesystem::is_regular_file(path, ec);
            CHECK_MSG(present, path);
            if (!present) continue;

            const Outcome outcome = StartAndDrop(path);
            CHECK_MSG(outcome.started || !outcome.why.empty(), name + " neither started nor said why");
            if (!outcome.started) {
                refusals[ReasonOf(outcome.why)].push_back(name);
                std::printf("    %-10s refused: %s\n", name.c_str(), outcome.why.c_str());
                continue;
            }
            ++started;
            const std::string wanted =
                !outcome.noGravity ? ": the player did not land within three seconds"
                : outcome.pulled   ? ": the player was flung far from where its wells found it"
                                   : ": the player did not stay afloat where it started";
            CHECK_MSG(outcome.landed, name + wanted);
            const char* const failed = !outcome.noGravity ? "starts, DOES NOT LAND"
                                       : outcome.pulled   ? "starts, FLUNG BY ITS WELLS"
                                                          : "starts, DOES NOT FLOAT";
            std::string line = outcome.Plays() ? "plays" : outcome.landed ? "starts" : failed;
            if (!outcome.ignored.empty()) {
                line += "; inert:";
                for (const auto& [role, count] : outcome.ignored) {
                    line += " " + role + (count > 1 ? " x" + std::to_string(count) : "");
                    inert[role].push_back(name);
                }
            }
            if (outcome.Plays()) ++plays;
            std::printf("    %-10s %s\n", name.c_str(), line.c_str());
        }
        std::printf("  %s: %d of %d start, %d play\n", chapter.name, started, kLevelsPerChapter, plays);
        CHECK_MSG(started >= chapter.startFloor, std::string(chapter.name) + ": " + std::to_string(started) +
                                                     " start, the floor is " + std::to_string(chapter.startFloor));
        CHECK_MSG(plays >= chapter.playFloor, std::string(chapter.name) + ": " + std::to_string(plays) +
                                                  " play, the floor is " + std::to_string(chapter.playFloor));
        startedEverywhere += started;
        playsEverywhere += plays;
    }

    std::printf("  %d of %d levels start, %d play.\n", startedEverywhere, kLevelsPerChapter * 4, playsEverywhere);
    PrintTally("What stops a level starting", refusals);
    PrintTally("Roles left inert in levels that start", inert);
}

void Level30StillPlays() {
    const Outcome outcome = StartAndDrop(kLevels + "/level30.tscn");
    CHECK_MSG(outcome.started, outcome.why);
    CHECK(outcome.landed);
    CHECK(outcome.Plays());
}

// Every level's art, read as the layer reads it (Sprites.hpp). The reader is
// strict, and a level whose art it refuses is drawn as boxes while this
// inventory still counts it as playing - so every level is read here, whether
// or not it starts. The totals are the converter's, pinned.
void EveryLevelsArtReads() {
    int sprites = 0;
    int added = 0;
    int read = 0;
    for (const Chapter& chapter : kChapters) {
        for (int i = 0; i < kLevelsPerChapter; ++i) {
            const std::string name = "level" + std::to_string(i) + chapter.suffix;
            Tscn::Scene scene;
            std::vector<Sprites::Sprite> found;
            std::string error;
            const bool ok = Tscn::Load(kLevels + "/" + name + ".tscn", scene, error) &&
                            Sprites::Find(scene, kLevels + "/..", found, error);
            CHECK_MSG(ok, name + "'s art: " + error);
            if (!ok) continue;
            ++read;
            sprites += static_cast<int>(found.size());
            for (const Sprites::Sprite& sprite : found) {
                if (sprite.additive) ++added;
            }
        }
    }
    std::printf("  art: %d of %d levels read, %d sprites, %d of them added\n", read, kLevelsPerChapter * 4, sprites,
                added);
    CHECK_EQ(read, kLevelsPerChapter * 4);
    CHECK_EQ(sprites, 2883);
    CHECK_EQ(added, 99);
}

// Every level's lighting, read as the layer will read it (Lighting.hpp). Nothing
// draws it yet, and a level whose lighting the reader refuses still starts and
// plays - so, as for the art, every level is read here and the totals are the
// converter's own report for the four worlds (the remake's b572fec), pinned.
// test_mp_lighting pins what individual levels say.
void EveryLevelsLightingReads() {
    int read = 0;
    int lightmaps = 0;
    int lightmappedLevels = 0;
    int lights = 0;
    int staticLights = 0;
    int halos = 0;
    int normals = 0;
    int emissive = 0;
    int deep = 0;
    for (const Chapter& chapter : kChapters) {
        for (int i = 0; i < kLevelsPerChapter; ++i) {
            const std::string name = "level" + std::to_string(i) + chapter.suffix;
            Tscn::Scene scene;
            Lighting::Scene lighting;
            std::string error;
            const bool ok = Tscn::Load(kLevels + "/" + name + ".tscn", scene, error) &&
                            Lighting::Read(scene, kLevels + "/..", lighting, error);
            CHECK_MSG(ok, name + "'s lighting: " + error);
            if (!ok) continue;
            ++read;
            int here = 0;
            for (const auto& [node, look] : lighting.nodes) {
                if (!look.lightmap.empty()) ++here;
                if (look.light) {
                    ++lights;
                    if (look.isStatic) ++staticLights;
                    if (!look.light->halo.empty()) ++halos;
                }
                if (!look.normal.empty()) ++normals;
                if (look.emissive != glm::dvec3(0.0)) ++emissive;
                if (look.z != 0.0) ++deep;
            }
            lightmaps += here;
            if (here > 0) ++lightmappedLevels;
        }
    }
    std::printf("  lighting: %d of %d levels read, %d lightmaps in %d levels, %d lights (%d static, %d with a halo), "
                "%d normal maps, %d emissive, %d with a depth\n",
                read, kLevelsPerChapter * 4, lightmaps, lightmappedLevels, lights, staticLights, halos, normals,
                emissive, deep);
    CHECK_EQ(read, kLevelsPerChapter * 4);
    CHECK_EQ(lightmaps, 730);
    CHECK_EQ(lightmappedLevels, 67);
    CHECK_EQ(lights, 72);
    CHECK_EQ(staticLights, 68);
    CHECK_EQ(halos, 71);
    CHECK_EQ(normals, 1720);
    CHECK_EQ(emissive, 2140);
    CHECK_EQ(deep, 1533);
}

// The twelve levels that set `darkest`. The simulation carries the flag and acts
// on nothing: what it changes is the ambient light the level is DRAWN with,
// DARKEST_AMBIENT_LIGHT (0.01, 0.01, 0.01) in place of the file's (lighting.json,
// Lighting::Ambient, step 45). So what is pinned here is that they start, that
// the flag reaches Game::Level, and that the ambient they start with is darkest's
// whatever their file says. minions.json holds the one gameplay consequence: a
// minion in such a level is blind, which minion sight must honour when it is
// built.
void DarkestLevelsStartAndCarryTheFlag() {
    const char* const kDarkest[] = {"level19c", "level20c", "level21c", "level22c", "level23c", "level24c",
                                    "level25c", "level26c", "level28c", "level29c", "level30c", "level31c"};
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-start";

    int carried = 0;
    for (const char* name : kDarkest) {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        std::string error;
        const bool ok =
            Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) &&
            Game::Start(data, registry, level, error);
        CHECK_MSG(ok, std::string(name) + ": " + error);
        if (!ok) continue;
        CHECK_MSG(level.darkest, std::string(name) + " sets darkest, and Game::Level says so");
        if (level.darkest) ++carried;
        Lighting::Scene look;
        const bool read = Lighting::Read(data.scene, kLevels + "/..", look, error);
        CHECK_MSG(read, std::string(name) + "'s lighting: " + error);
        if (read) {
            CHECK_MSG(Lighting::Ambient(data.lighting, look.ambient, level.darkest, level.torch) ==
                          glm::dvec3(0.01, 0.01, 0.01),
                      std::string(name) + " starts drawn at darkest's 0.01");
            CHECK_MSG(look.ambient != glm::dvec3(0.01, 0.01, 0.01),
                      std::string(name) + "'s file says otherwise, so the replacement is what is pinned");
        }
    }
    CHECK_EQ(carried, 12);

    // And a level that does not set it says so too - without this the flag being
    // true everywhere would pass every check above.
    {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        std::string error;
        if (Game::LoadData(kLevels + "/level0.tscn", kData, prisms, data, error) &&
            Game::Start(data, registry, level, error)) {
            CHECK_MSG(!level.darkest, "level0 is not a dark level");
            Lighting::Scene look;
            if (Lighting::Read(data.scene, kLevels + "/..", look, error)) {
                CHECK_MSG(Lighting::Ambient(data.lighting, look.ambient, level.darkest, level.torch) ==
                              glm::dvec3(0.35, 0.3, 0.35),
                          "and is drawn at its file's own ambient");
            }
        }
    }
    std::printf("  %d of 12 darkest levels start and carry the flag\n", carried);
}

void runTests() {
    Level30StillPlays();
    DarkestLevelsStartAndCarryTheFlag();
    EveryLevelStartsOrSaysWhy();
    EveryLevelsArtReads();
    EveryLevelsLightingReads();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_start: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_start", 250);
}
