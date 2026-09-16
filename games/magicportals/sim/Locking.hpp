#pragma once

// What the player's save has opened: ui.json's `locking` block, and the
// original's isWorldUnlocked, isLevelUnlocked and computeWorldAccomplishment over
// the port's own medals.
//
// OWNER RULING R3 (the remake's ui3 spec 8.2): locking as the original, from the
// port's save. The achievements dashboard is the first reader - its lock icons and
// what a tap on a row opens (spec 4.3) - and chapter select and the level grid are
// to read the same functions (spec 5.4, 6.4). A named --level still opens any
// level: nothing here is asked on that path.
//
// Pure: a function of the medals (Scores::Store) and the level order
// (Chapters::Table), so a suite pins it with no window.

#include <string>

#include "sim/Chapters.hpp"
#include "sim/Scores.hpp"

namespace MagicPortals::Locking {

struct Rules {
    int medalMax = 0;             // a level's best medal: the most a level adds to its chapter
    int chapterUnlockPercent = 0; // the chapter before must have reached this
};

// ui.json's `locking`, strictly: a missing or malformed number is refused.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// How many levels a chapter has in the level order; 0 for one it does not have.
int LevelsIn(const Chapters::Table& chapters, int world);

// ScoreManager::computeWorldAccomplishment: fTOu(float(the chapter's medals
// summed) / float(its levels * medalMax) * 100), truncated; 0 for a chapter the
// order does not have.
int ChapterCompletion(const Rules& rules, const Scores::Store& scores, const Chapters::Table& chapters, int world);

// isWorldUnlocked: the first chapter, or the one before at chapterUnlockPercent.
bool ChapterUnlocked(const Rules& rules, const Scores::Store& scores, const Chapters::Table& chapters, int world);

// isLevelUnlocked: its chapter open, and then a level below 1 - a chapter's own
// achievements name level -1 - or the level before it scored.
bool LevelUnlocked(const Rules& rules, const Scores::Store& scores, const Chapters::Table& chapters, int world,
                   int level);

} // namespace MagicPortals::Locking
