#pragma once

// The original's achievements, as the remake's gitignored achievements.json holds
// them, and what the port's save unlocks of them.
//
// THE CONTENT IS NOT THIS REPOSITORY'S. The 82 titles, descriptions, icons and
// points - and the secret achievement's words - are Asantee-authored. They are
// extracted by the remake's tools/asbc/achievements.py from the decode into its
// out/data/, beside chapters.json, and read from there at run time the way the
// levels are (SUPERSONIC_MAGICPORTALS_ACHIEVEMENTS). Nothing here names one, and
// no suite embeds one: the suites build their own lists. Without the file the
// dashboard (sim/Dashboard.hpp) draws no rows.
//
// WHAT A SAVE UNLOCKS. The original keeps the ids it has unlocked in its own save
// and adds to it as the game is played; most of its conditions need the game
// itself - a portal count, a time, an entity. The port keeps medals only
// (Scores::Store), so it shows what those decide, exactly as the original's main
// menu re-derives them on every visit (PortalMainMenu::checkPreviouslyUnlockAchievements):
// the chapter-completion rules of registerMedalAchievements and the level-medal
// rules of checkForLevelAchievements, both read out of the decode by the tool.
// The rest stay locked until the port plays them.

#include <string>
#include <vector>

#include "sim/Chapters.hpp"
#include "sim/Locking.hpp"
#include "sim/Scores.hpp"

namespace MagicPortals::Achievements {

struct Entry {
    int id = -1;
    int world = -1;
    int level = -1; // -1 for a chapter's own
    int points = 0;
    std::string title;
    std::string description;
    std::string icon; // within the dashboard's icon directory
    bool isNew = false;
    bool isSecret = false;
};

// What a secret achievement shows while it is locked (Achievement::getTitle,
// getDescription, getIconSprite).
struct Secret {
    std::string title;
    std::string descriptionPrefix;    // then world + 1,
    std::string descriptionSeparator; // then level + 1
    std::string icon;
};

// registerMedalAchievements: a chapter at `minPercent` or more unlocks `id`.
struct CompletionRule {
    int world = 0;
    int minPercent = 0;
    int id = -1;
};

// checkForLevelAchievements, the conditions a save decides: a level's medal at
// `minMedal` or more unlocks `id`.
struct MedalRule {
    int world = 0;
    int level = 0;
    int minMedal = 0;
    int id = -1;
};

struct Content {
    std::vector<Entry> entries; // AchievementManager's order: the dashboard's rows
    Secret secret;
    std::vector<CompletionRule> fromCompletion;
    std::vector<MedalRule> fromLevelMedal;
};

// The tool's format 1, strictly: false with `error` when the file is missing,
// unreadable or not that shape, leaving `out` as it was.
bool Load(const std::string& path, Content& out, std::string& error);

// The ids the save unlocks, ascending.
std::vector<int> Unlocked(const Content& content, const Locking::Rules& locking, const Scores::Store& scores,
                          const Chapters::Table& chapters);

bool IsUnlocked(const std::vector<int>& unlocked, int id);

// AchievementManager::computeAchievementPoints: every unlocked entry's points.
int Points(const Content& content, const std::vector<int>& unlocked);

// Achievement::getTitle, getDescription and getIconSprite: the entry's own,
// unless it is secret and still locked.
std::string TitleOf(const Content& content, const Entry& entry, bool unlocked);
std::string DescriptionOf(const Content& content, const Entry& entry, bool unlocked);
std::string IconOf(const Content& content, const Entry& entry, bool unlocked);

} // namespace MagicPortals::Achievements
