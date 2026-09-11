#pragma once

// What the port draws for what no level pictures: the port's art.json.
//
// The levels picture everything placed in them (Sprites.hpp). What the game
// itself adds - the portals a shot opens, the shot - the original draws with
// its own entities, and art.json says which image each of those .ent files
// names, whether it is added, and how its sheet is cut. Those are the .ent's
// facts, and test_mp_sprites pins them. How fast a sheet plays is not decoded,
// so it is carried as a _guess and pinned by nothing.
//
// The images are the original's, read from the extracted APK beside the remake
// (MAGICPORTALS_ORIGINAL_DIR) and never committed.

#include <string>

namespace MagicPortals::Art {

struct Picture {
    std::string sprite;           // an image among the original's entities
    bool additive = false;        // the .ent's blendMode 1
    int columns = 1;              // its SpriteCut
    int rows = 1;
    double framesPerSecond = 0.0; // _guess, for a sheet of more than one frame

    int Frames() const { return columns * rows; }
};

// The player, as dark_mage.ent draws it: a sheet whose rows are directions and
// whose columns are a walk.
struct Character : Picture {
    int startFrame = 0;    // the .ent's startFrame
    double pivotXPx = 0.0; // its PivotAdjust: the point of the image, from its
    double pivotYPx = 0.0; // centre, that stands on the entity
    int leftRow = 0;       // derived from the decoded DIRECTION enum, not testimony
    int rightRow = 0;
    int idleColumn = 0;    // _guess
};

struct Rules {
    Picture portal;      // portal.ent
    Picture shot;        // projectile.ent
    Character character; // dark_mage.ent, the player
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

} // namespace MagicPortals::Art
