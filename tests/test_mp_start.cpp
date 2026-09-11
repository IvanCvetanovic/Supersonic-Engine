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

#include "sim/Game.hpp"
#include "sim/Player.hpp"
#include "sim/Roles.hpp"

#include <algorithm>
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
const Chapter kChapters[] = {
    {"", "chapter 1", 31, 16},
    {"a", "chapter 2", 32, 0},
    {"b", "chapter 3", 31, 0},
    {"c", "chapter 4", 2, 0},
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

struct Outcome {
    bool started = false;
    bool landed = false;
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
        if (!Roles::IsPorted(role)) ++outcome.ignored[role];
    }
    for (int tick = 0; tick < kLandingTicks && !outcome.landed; ++tick) {
        Game::Tick(data, registry, level, 0.0f, kStep);
        outcome.landed = Player::Grounded(registry, level.player, data.tuning);
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
            CHECK_MSG(outcome.landed, name + ": the player did not land within three seconds");
            std::string line = outcome.Plays() ? "plays" : outcome.landed ? "starts" : "starts, DOES NOT LAND";
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

void runTests() {
    Level30StillPlays();
    EveryLevelStartsOrSaysWhy();
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
