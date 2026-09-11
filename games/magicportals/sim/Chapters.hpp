#pragma once

// The game's level order: the converter's chapters.json, its reading of the
// original's goldenScores.enml (the remake's README, step 3). Four worlds of 32
// levels, world by world, each level with its golden score.
//
// The golden score is a maximum portal count: finish the level with that many
// portals or fewer. 0 is a real threshold, "finish using no portals", and seven
// levels ship it (docs/original-gameplay.md section 3). The remake treats -1 as
// unknown; no level in the file has it.
//
// chapters.json is converter output, so it lives beside the levels in the
// remake's gitignored out/ and is read from there (SUPERSONIC_MAGICPORTALS_CHAPTERS).

#include <string>
#include <vector>

namespace MagicPortals::Chapters {

struct Level {
    int world = 0;       // 0-based, as the original numbers them
    int index = 0;       // within its world, 0-based
    std::string name;    // the converted file's stem: level0, level0a, ...
    int goldenScore = 0; // the most portals that earns the medal
};

struct Table {
    std::vector<Level> levels; // world by world, each in order

    // The level's place in `levels`, or -1.
    int Find(const std::string& name) const;

    // The level after `at` in the same world, or -1 at the world's end: the
    // remake's advance_level (level_manager.gd:128-134), which leaves what a
    // finished chapter means to its caller.
    int Next(int at) const;
};

// "1-1" for world 0's first level: 1-based for display only (hud.gd:136-139).
std::string Label(const Level& level);

// False, with `error`, when the file is missing or not the converter's format 1.
bool Load(const std::string& path, Table& out, std::string& error);

} // namespace MagicPortals::Chapters
