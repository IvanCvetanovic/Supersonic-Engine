// The player's medals, kept between runs.
//
// The port kept no score at all, so the level grid had nothing to draw a medal
// from and every button was bare - which is what the owner reported. This is
// the store behind it, and what is pinned here is the handful of rules that
// make a save file safe rather than merely present:
//
//   - a worse replay never takes a better medal away (ScoreManager::setScore
//     writes only when the new score beats the old);
//   - an empty save directory touches no filesystem at all, which is the
//     contract every other suite depends on;
//   - a file that will not parse is refused AND left alone, because a player's
//     only copy of their progress must not be replaced by an empty one.
//
// Free of the engine, like the rest of sim/, so all of it runs with a temporary
// directory and no window.

#include "TestHarness.hpp"

#include "sim/Scores.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace MagicPortals;

namespace {

// A directory of this suite's own, emptied first: a store that read what the
// last run left behind would pass or fail for reasons nothing here states.
std::filesystem::path Scratch(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "supersonic-test-mp-scores" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void AnEmptyDirectoryTouchesNothing() {
    Scores::Store store;
    std::string error;
    CHECK_MSG(store.Open("", error), "a memory-only store opens: " + error);
    CHECK_MSG(!store.Persists(), "and has nowhere to write");

    // It still REMEMBERS - it is only the file that is absent. A store that
    // forgot as well would make every suite's medals invisible to itself.
    CHECK(store.Record(0, 3, Scores::kSilver));
    CHECK_EQ(store.Get(0, 3), Scores::kSilver);

    CHECK_MSG(store.Save(error), "saving a memory-only store is not a failure: " + error);
}

void AWorsePlayTakesNothingAway() {
    Scores::Store store;
    std::string error;
    CHECK(store.Open("", error));

    CHECK(store.Record(1, 5, Scores::kGold));
    CHECK_MSG(!store.Record(1, 5, Scores::kBronze), "a worse replay must not overwrite a gold");
    CHECK_EQ(store.Get(1, 5), Scores::kGold);

    // Improving one does take.
    CHECK(store.Record(1, 7, Scores::kBronze));
    CHECK_MSG(store.Record(1, 7, Scores::kSilver), "but a better play does replace it");
    CHECK_EQ(store.Get(1, 7), Scores::kSilver);

    // kUnplayed is the ABSENCE of a medal, not a medal: recording it would put
    // rows in the file that mean "no row", and the grid's guard is score != 0.
    CHECK_MSG(!store.Record(2, 0, Scores::kUnplayed), "an unplayed level is not recorded");
    CHECK_EQ(store.Get(2, 0), Scores::kUnplayed);

    // A level nobody has finished reads as unplayed rather than as bronze.
    CHECK_EQ(store.Get(3, 30), Scores::kUnplayed);
}

void MedalsSurviveTheRun() {
    const std::filesystem::path dir = Scratch("roundtrip");
    std::string error;
    {
        Scores::Store store;
        CHECK_MSG(store.Open(dir.string(), error), error);
        CHECK_MSG(store.Persists(), "a store given a directory has somewhere to write");
        store.Record(0, 0, Scores::kGold);
        store.Record(0, 1, Scores::kSilver);
        store.Record(3, 31, Scores::kBronze);
        CHECK_MSG(store.Save(error), error);
    }
    {
        Scores::Store reopened;
        CHECK_MSG(reopened.Open(dir.string(), error), error);
        CHECK_EQ(reopened.Get(0, 0), Scores::kGold);
        CHECK_EQ(reopened.Get(0, 1), Scores::kSilver);
        CHECK_EQ(reopened.Get(3, 31), Scores::kBronze);
        CHECK_EQ(reopened.Get(2, 2), Scores::kUnplayed);
        CHECK_EQ(static_cast<int>(reopened.Count()), 3);
    }
}

void AFreshFolderIsAPlayerWhoHasFinishedNothing() {
    const std::filesystem::path dir = Scratch("fresh");
    Scores::Store store;
    std::string error;
    CHECK_MSG(store.Open(dir.string(), error), "no scores.json yet is not an error: " + error);
    CHECK_MSG(store.Persists(), "and the first Save is what creates it");
    CHECK_EQ(static_cast<int>(store.Count()), 0);
}

void AnUnreadableFileIsNeverOverwritten() {
    const std::filesystem::path dir = Scratch("corrupt");
    const std::filesystem::path file = dir / "scores.json";
    const std::string garbage = "{ this is not json";
    {
        std::ofstream out(file, std::ios::trunc);
        out << garbage;
    }

    Scores::Store store;
    std::string error;
    CHECK_MSG(!store.Open(dir.string(), error), "a file that will not parse is refused");
    CHECK_MSG(!error.empty(), "and says why");

    // AND IS LEFT ALONE. Starting silently from zero would erase a player's
    // progress on the next clear; refusing to persist is what protects it.
    CHECK_MSG(!store.Persists(), "a store that could not read will not write");
    std::string why;
    CHECK(store.Save(why));

    std::ifstream back(file);
    std::string first;
    std::getline(back, first);
    CHECK_MSG(first == garbage, "the unreadable file is still there, untouched");
}

void runTests() {
    AnEmptyDirectoryTouchesNothing();
    AWorsePlayTakesNothingAway();
    MedalsSurviveTheRun();
    AFreshFolderIsAPlayerWhoHasFinishedNothing();
    AnUnreadableFileIsNeverOverwritten();
}

} // namespace

TEST_MAIN("test_mp_scores", 25)
