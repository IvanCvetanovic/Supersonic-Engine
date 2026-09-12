#pragma once

// What the port plays, and when: the port's sounds.json.
//
// The original's AudioManager is a list of hooks - playPortalCreatedSound,
// playCrystalPickSound, playDeathSound - each naming one or two of the 46 mp3s
// in its soundfx/, with the volume, sample speed and rate limit it sets. All of
// that is decoded from android_game.bin and carried as data, so this file reads
// it rather than restating it in C++.
//
// The port cannot fire a hook the way the original does, by calling it from the
// script that did the thing. It watches its own simulation instead: a counter
// that went up, a flag that turned over. `events` is that half of the table -
// the port's own event names, each naming the hook it plays.
//
// NOTHING HERE IS PART OF THE SIMULATION. A sound is a picture with a speaker:
// the layer latches events on the tick and plays them on the frame, so no clip,
// no random draw and no missing file can reach Game::Level or the state hash.
// The reader lives beside the other readers only because that is where the
// port's data readers live (Art.hpp is the same shape).
//
// The mp3s themselves are the original's and stay outside this repository,
// under MAGICPORTALS_ORIGINAL_DIR/soundfx.

#include <map>
#include <string>
#include <vector>

namespace MagicPortals::Sounds {

// One of the original's AudioManager hooks.
struct Hook {
    std::string name;
    // One file, or two. Two means either "both together" or "one at random",
    // and the original does both: playExplosionSound plays explosion_huge AND
    // explosion_small, playCrystalPickSound picks one of the two gathers.
    std::vector<std::string> files;
    bool both = false;
    double volume = 1.0;

    // The sample speed, which is the engine's pitch. Three shapes, because the
    // original has three: a number it states, a range it draws from
    // (playWoodDragSound), and one it computes from the thing making the sound
    // (playDoorOpenSound's 3000 / stride, which the caller supplies).
    double speed = 1.0;
    bool speedIsRandom = false;
    double speedFrom = 0.0;
    double speedTo = 0.0;
    bool speedFromDoorStride = false;

    // No sooner than this after the last hook on the same `timer`. The original
    // keeps one Timer per group, not one per hook: playCrystalPickSound and
    // playCrystalTempAlertSound share AudioManager.crystalSoundTimer, so they
    // rate-limit EACH OTHER. An empty timer is not limited.
    double minIntervalMs = 0.0;
    std::string timer;
};

// One of the five tracks, as the original starts it.
struct Track {
    std::string file;
    double volume = 1.0;
    bool loop = true;
};

struct Rules {
    std::map<std::string, Hook> hooks;
    std::map<std::string, Track> music;        // "menu", "game", "boss", "rocket", "back_home"
    std::map<std::string, std::string> events; // the port's event -> a hook's name

    // Null when there is none. An event mapped to an empty name is SILENT and
    // deliberately so: the port names what it can see happen even where the
    // original has no sound for it, rather than inventing one.
    const Hook* FindHook(const std::string& name) const;
    const Hook* ForEvent(const std::string& event) const;
    const Track* FindTrack(const std::string& name) const;
};

// False, with `error`, for a file that does not read, a hook with no files, a
// speed that is none of the three shapes, or an event naming a hook that is not
// there. A typo in a sound's name is a sound nobody hears, which is the quietest
// kind of bug; it is refused at load instead.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// Where the original's sounds are, given its extracted assets.
std::string Directory(const std::string& originalDirectory);

} // namespace MagicPortals::Sounds
