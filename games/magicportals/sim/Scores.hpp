#pragma once

// The medal each level was cleared with, kept between runs.
//
// WHY THIS EXISTS. The level grid draws a small medal on every level the player
// has finished - LevelChooser::itemDrawCallback asks ScoreManager::getScore for
// a level and, when it is not zero, draws getSmallSpriteMedalName(score) at the
// button's corner. The port kept no score at all, so there was nothing for that
// to read and every button was bare. That is what the owner saw and reported as
// "i dont see any mdels at the level select".
//
// The original keeps this in scores.enml beside its golden scores, written
// through ScoreManager::setScore. This is the same information in the shape the
// port already uses for everything else - JSON, read with core/Json.hpp - and
// deliberately not the original's format: nothing reads the original's file, and
// a second enml parser would exist only to be compatible with a game that is no
// longer installed.
//
// Free of the renderer and of the engine, like the rest of sim/, so a test can
// exercise the whole of it with a temporary directory and no window.

#include <cstddef>
#include <string>
#include <vector>

namespace MagicPortals::Scores {

// A medal, as ScoreManager::computeScore returns one (bytes 363979..364204).
//
// Zero is the important one: getSmallSpriteMedalName maps 3 to gold, 2 to
// silver and ANYTHING ELSE to bronze, so bronze is what an unrecognised value
// becomes - and the grid's guard is `score != 0`, which is what makes zero mean
// "never finished" rather than "finished badly". A level cleared at the worst
// medal still stores 1.
inline constexpr int kUnplayed = 0;
inline constexpr int kBronze = 1;
inline constexpr int kSilver = 2;
inline constexpr int kGold = 3;

// The medals, by world and level.
//
// Addressed by (world, level) rather than by a flat level index because that is
// how the original addresses them - getScore(levelIdx, world), over a ScoreList
// per world - and because a flat index would silently renumber every saved
// medal if a chapter ever gained or lost a level.
class Store {
public:
    // Reads `directory`/scores.json, if there is one.
    //
    // An EMPTY directory is not an error and not a failure to load: it means
    // this store is memory-only and will never touch the filesystem. That is
    // WolfBrigadeLayer's contract for its own save directory, and it exists so
    // that every suite can build a layer bare without eighteen of them writing
    // the same filename into the ctest working directory.
    //
    // A directory with no scores.json in it is not an error either - that is
    // simply a player who has not finished a level yet. False is returned only
    // for a file that exists and cannot be read or does not parse, because
    // silently starting from zero would erase a player's progress on the next
    // Save.
    bool Open(const std::string& directory, std::string& error);

    // The medal this level was cleared with, or kUnplayed.
    int Get(int world, int level) const;

    // Keeps the BETTER of the two, which is what ScoreManager::setScore does:
    // it writes only `if (score > getScore(level, world) || overwrite)`. A
    // replay that goes worse must not take a gold away.
    //
    // Returns whether anything changed, so a caller can skip a write that would
    // rewrite the file with what it already says.
    bool Record(int world, int level, int medal);

    // Writes scores.json, via a temporary and a rename.
    //
    // The rename is PipelineCache's idiom and is here for its reason: a file
    // truncated by a process killed mid-write is worse than no file, and this
    // one is the player's progress. A memory-only store saves nothing and
    // reports success - it has nowhere to write, which is not a failure.
    bool Save(std::string& error) const;

    // Whether this store has somewhere to write.
    bool Persists() const { return !m_path.empty(); }

    // How many levels have a medal. For the tests and the log line.
    std::size_t Count() const { return m_entries.size(); }

private:
    struct Entry {
        int world = 0;
        int level = 0;
        int medal = kUnplayed;
    };

    std::vector<Entry> m_entries;

    // The file, or empty for a memory-only store.
    std::string m_path;
};

} // namespace MagicPortals::Scores
