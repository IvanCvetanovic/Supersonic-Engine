// The game's level order, chapters.json, as the port reads it.
//
// Everything here is converter output, so it is pinned exactly: four worlds of
// 32 levels, named as the converter names the files, each with a file in the
// levels directory, and the golden scores the file spells. A golden score of 0
// is a real threshold, "finish using no portals", not "unknown". Eight levels
// ship it, counted with grep: the remake's docs/original-gameplay.md section 3
// says seven, and the file says eight.
//
// Reads chapters.json and the levels from outside this repository, and skips,
// saying where it looked, when they are absent.

#include "TestHarness.hpp"

#include "sim/Chapters.hpp"

#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>

using namespace MagicPortals;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kChapters = MAGICPORTALS_CHAPTERS_FILE;

Chapters::Table g_table;

void FourWorldsOfThirtyTwo() {
    const char* const suffix[] = {"", "a", "b", "c"};
    CHECK_EQ(g_table.levels.size(), std::size_t{128});
    int misnamed = 0;
    int missing = 0;
    for (std::size_t i = 0; i < g_table.levels.size(); ++i) {
        const Chapters::Level& level = g_table.levels[i];
        const int world = static_cast<int>(i / 32);
        const int index = static_cast<int>(i % 32);
        const std::string name = "level" + std::to_string(index) + suffix[world];
        if (level.world != world || level.index != index || level.name != name) {
            ++misnamed;
            std::printf("  entry %zu is %d-%d %s, where %s was expected\n", i, level.world, level.index,
                        level.name.c_str(), name.c_str());
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(kLevels + "/" + level.name + ".tscn", ec)) ++missing;
    }
    CHECK_EQ(misnamed, 0);
    CHECK_EQ(missing, 0);
}

void TheGoldenScoresTheFileSpells() {
    // Chapter 1's first eleven, read off chapters.json.
    const int expected[] = {0, 1, 2, 2, 2, 6, 2, 4, 2, 4, 6};
    for (int i = 0; i < static_cast<int>(std::size(expected)); ++i) {
        const int at = g_table.Find("level" + std::to_string(i));
        CHECK(at >= 0);
        if (at >= 0) CHECK_EQ(g_table.levels[static_cast<std::size_t>(at)].goldenScore, expected[i]);
    }
    std::string zero;
    int unknown = 0;
    for (const Chapters::Level& level : g_table.levels) {
        if (level.goldenScore == 0) zero += (zero.empty() ? "" : " ") + level.name;
        if (level.goldenScore < 0) ++unknown;
    }
    CHECK_MSG(zero == "level0 level14 level0a level1a level6a level27a level1b level5c", zero);
    CHECK_EQ(unknown, 0);
}

void NextStaysInItsWorld() {
    CHECK_EQ(g_table.Find("level0"), 0);
    CHECK_EQ(g_table.Find("level30"), 30);
    CHECK_EQ(g_table.Find("level0a"), 32);
    CHECK_EQ(g_table.Find("level99"), -1);
    CHECK_EQ(g_table.Next(0), 1);
    CHECK_EQ(g_table.Next(30), 31);
    // A world's last level has no next: what a finished chapter means is the
    // caller's to say.
    CHECK_EQ(g_table.Next(31), -1);
    CHECK_EQ(g_table.Next(127), -1);
    CHECK_EQ(g_table.Next(-1), -1);
    CHECK(Chapters::Label(g_table.levels[0]) == "1-1");
    CHECK(Chapters::Label(g_table.levels[32]) == "2-1");
    CHECK(Chapters::Label(g_table.levels[127]) == "4-32");
}

void runTests() {
    FourWorldsOfThirtyTwo();
    TheGoldenScoresTheFileSpells();
    NextStaysInItsWorld();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(kChapters, ec) || !std::filesystem::is_directory(kLevels, ec)) {
        std::printf("test_mp_chapters: SKIPPED - needs chapters.json at %s\n"
                    "  and the converted levels at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_CHAPTERS=... and -DSUPERSONIC_MAGICPORTALS_LEVELS=...\n",
                    kChapters.c_str(), kLevels.c_str());
        return 77;
    }
    std::string error;
    if (!Chapters::Load(kChapters, g_table, error)) {
        std::printf("test_mp_chapters: %s\n", error.c_str());
        return 1;
    }
    runTests();
    return ::test::summary("test_mp_chapters", 30);
}
