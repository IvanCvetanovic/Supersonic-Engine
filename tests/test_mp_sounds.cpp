// The port's sounds: the original's AudioManager, decoded and carried as data.
//
// Two halves, like test_mp_sprites. The reader is tested on files this suite
// writes, and on the port's own sounds.json, which is IN this repository - so
// that part runs anywhere. Only the last case opens the original's own mp3s,
// and it says where it looked when they are not there.
//
// What is pinned here is the decode, not a preference. Every name and number
// below was read out of android_game.bin's AudioManager (the byte offsets are
// in docs/planning/2026-09-11-magic-portals-remaster.md), and several of them
// are surprising enough that a future reader would "fix" them without a test
// saying otherwise: the crystal VANISH sound is spike_hit, unlocking a door is
// a crystal_gather at half speed, and a menu button is the only thing in the
// game that plays button.mp3.

#include "TestHarness.hpp"

#include "sim/Sounds.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kOriginal = MAGICPORTALS_ORIGINAL_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-sounds";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// A sounds.json with `body` as its hooks, music and events.
std::string WriteRules(const char* name, const std::string& body) {
    const std::filesystem::path path = Scratch() / name;
    std::ofstream file(path, std::ios::trunc);
    file << body;
    file.close();
    return path.string();
}

// The smallest file the reader accepts, for the refusal cases to vary.
std::string Whole(const std::string& hooks, const std::string& events = "{}") {
    return "{\"hooks\":" + hooks +
           ",\"music\":{\"menu\":{\"file\":\"warlords_loop_a.mp3\",\"volume\":1.0,\"loop\":true}}"
           ",\"events\":" + events + "}";
}

void TheReaderRefusesWhatWouldBeSilent() {
    Sounds::Rules rules;
    std::string error;

    // Two files and no word on whether they play together or one at random:
    // the difference between an explosion and a crystal, and unguessable.
    const std::string twoFiles =
        WriteRules("two-files.json", Whole("{\"h\":{\"files\":[\"a.mp3\",\"b.mp3\"]}}"));
    CHECK_MSG(!Sounds::LoadRules(twoFiles, rules, error), "two files with no pick should be refused");
    CHECK_MSG(error.find("both or one") != std::string::npos, "the refusal should say what is missing: " + error);

    // An event naming a hook that is not there. This is THE bug this reader
    // exists to catch: a typo in an event's hook is a sound nobody ever hears,
    // and nothing at runtime would ever say so.
    const std::string typo =
        WriteRules("typo.json", Whole("{\"playDeathSound\":{\"files\":[\"a.mp3\"]}}",
                                      "{\"player_died\":\"playDeethSound\"}"));
    CHECK_MSG(!Sounds::LoadRules(typo, rules, error), "an event naming no hook should be refused");
    CHECK_MSG(error.find("playDeethSound") != std::string::npos, "the refusal should name the typo: " + error);

    // A speed that is none of the three shapes the original uses.
    const std::string speed =
        WriteRules("speed.json", Whole("{\"h\":{\"files\":[\"a.mp3\"],\"speed\":\"quickly\"}}"));
    CHECK_MSG(!Sounds::LoadRules(speed, rules, error), "an unreadable speed should be refused");

    // A rate limit with no timer to keep it on: the original's limits are per
    // shared Timer, and which hooks share one is the whole point.
    const std::string noTimer =
        WriteRules("no-timer.json", Whole("{\"h\":{\"files\":[\"a.mp3\"],\"min_interval_ms\":50}}"));
    CHECK_MSG(!Sounds::LoadRules(noTimer, rules, error), "a rate limit with no timer should be refused");

    // And an event may be deliberately silent, which is not an error: the port
    // names what it can see happen even where the original has no sound for it.
    const std::string silent =
        WriteRules("silent.json", Whole("{\"h\":{\"files\":[\"a.mp3\"]}}", "{\"button_pressed\":\"\"}"));
    CHECK_MSG(Sounds::LoadRules(silent, rules, error), "a silent event should be allowed: " + error);
    CHECK_MSG(rules.ForEvent("button_pressed") == nullptr, "a silent event plays nothing");
    CHECK_MSG(rules.ForEvent("never_happens") == nullptr, "an event nobody named plays nothing");
}

void TheSpeedShapesRead() {
    Sounds::Rules rules;
    std::string error;
    const std::string path = WriteRules(
        "speeds.json",
        Whole("{\"fixed\":{\"files\":[\"a.mp3\"],\"speed\":0.5},"
              "\"drawn\":{\"files\":[\"b.mp3\"],\"speed\":\"random 0.5 to 2.0\"},"
              "\"door\":{\"files\":[\"c.mp3\"],\"speed\":\"3000 / doorOpenStride\"}}"));
    CHECK_MSG(Sounds::LoadRules(path, rules, error), error);

    const Sounds::Hook* fixed = rules.FindHook("fixed");
    const Sounds::Hook* drawn = rules.FindHook("drawn");
    const Sounds::Hook* door = rules.FindHook("door");
    CHECK_MSG(fixed != nullptr && drawn != nullptr && door != nullptr, "all three hooks should read");
    if (fixed == nullptr || drawn == nullptr || door == nullptr) return;

    CHECK_NEAR(static_cast<float>(fixed->speed), 0.5f);
    CHECK_MSG(!fixed->speedIsRandom && !fixed->speedFromDoorStride, "a number is just a number");
    CHECK_MSG(drawn->speedIsRandom, "\"random 0.5 to 2.0\" is a draw");
    CHECK_NEAR(static_cast<float>(drawn->speedFrom), 0.5f);
    CHECK_NEAR(static_cast<float>(drawn->speedTo), 2.0f);
    CHECK_MSG(door->speedFromDoorStride, "the door's speed comes from its own stride");
}

// The port's real file, and the decode it carries.
void ThePortsOwnTableIsTheOriginals() {
    Sounds::Rules rules;
    std::string error;
    const std::string path = kPortData + "/sounds.json";
    CHECK_MSG(Sounds::LoadRules(path, rules, error), path + ": " + error);
    if (rules.hooks.empty()) return;

    // The original's AudioManager has upwards of sixty hooks; a table that lost
    // most of them would still read.
    CHECK_MSG(rules.hooks.size() >= 55,
              "expected the whole AudioManager, got " + std::to_string(rules.hooks.size()) + " hooks");

    const auto plays = [&rules](const char* hook, const char* file) {
        const Sounds::Hook* found = rules.FindHook(hook);
        CHECK_MSG(found != nullptr, std::string(hook) + " should be in the table");
        if (found == nullptr) return;
        CHECK_MSG(!found->files.empty() && found->files[0] == file,
                  std::string(hook) + " plays " + file + ", not " +
                      (found->files.empty() ? std::string("nothing") : found->files[0]));
    };

    // The plain ones, which are most of them.
    plays("playPortalCreatedSound", "portal_created.mp3");
    plays("playPortalLaunchSound", "portal_launch.mp3");
    plays("playTeleportSound", "teleport.mp3");
    plays("playDeathSound", "dark_whoosh_17.mp3");
    plays("playDemolitionSound", "demolition.mp3");
    plays("playElevatorSound", "elevator.mp3");
    plays("playDoorOpenSound", "door_open.mp3");
    plays("playReflectSound", "projectile_reflect.mp3");

    // And the ones nobody would guess. These are the whole reason this test
    // exists: each was read off the bytecode and each looks like a mistake.
    plays("playCrystalVanishSound", "spike_hit.mp3");   // entry 25, for a crystal
    plays("playDoorUnlockSound", "crystal_gather_G.mp3"); // a crystal, for a door
    plays("playStoneAppearSound", "portal_created.mp3");
    plays("playBeholderHitSound", "demolition.mp3");
    plays("playLevelFinishedSound", "teleport.mp3");
    plays("playEnemyShootExplodeSound", "portal_fail.mp3");
    // The menu's button is the ONLY thing in the game that plays button.mp3.
    plays("getButtonSoundName", "button.mp3");
    // Every one of the level's own buttons is a teleport.
    plays("getRetryButtonSound", "teleport.mp3");

    // The door unlocks at half speed, which is what makes it a door.
    const Sounds::Hook* unlock = rules.FindHook("playDoorUnlockSound");
    if (unlock != nullptr) CHECK_NEAR(static_cast<float>(unlock->speed), 0.5f);

    // playVictorySound sets the speed BACK to 1.0, because the light and
    // roundabout hooks leave that same file at 0.6.
    const Sounds::Hook* victory = rules.FindHook("playVictorySound");
    const Sounds::Hook* lightOn = rules.FindHook("playLightOnSound");
    const Sounds::Hook* roundabout = rules.FindHook("playRoundaboutSound");
    CHECK_MSG(victory != nullptr && lightOn != nullptr && roundabout != nullptr, "the sweep hooks should read");
    if (victory != nullptr && lightOn != nullptr && roundabout != nullptr) {
        CHECK_MSG(victory->files.size() == 1 && victory->files[0] == "magical_sweep_08.mp3",
                  "victory is the sweep");
        CHECK_NEAR(static_cast<float>(victory->speed), 1.0f);
        CHECK_NEAR(static_cast<float>(lightOn->speed), 0.6f);
        // playLightOnSound and playRoundaboutSound are the same bytes.
        CHECK_MSG(lightOn->files == roundabout->files && lightOn->both == roundabout->both &&
                      ::test::nearly(static_cast<float>(lightOn->speed), static_cast<float>(roundabout->speed)),
                  "light-on and roundabout are byte for byte the same hook");
    }

    // Two together, versus one of two at random.
    const Sounds::Hook* explosion = rules.FindHook("playExplosionSound");
    const Sounds::Hook* crystal = rules.FindHook("playCrystalPickSound");
    const Sounds::Hook* alert = rules.FindHook("playCrystalTempAlertSound");
    CHECK_MSG(explosion != nullptr && crystal != nullptr && alert != nullptr, "the paired hooks should read");
    if (explosion != nullptr && crystal != nullptr && alert != nullptr) {
        CHECK_MSG(explosion->files.size() == 2 && explosion->both, "an explosion is both samples together");
        CHECK_MSG(crystal->files.size() == 2 && !crystal->both, "a crystal is one of two at random");
        CHECK_NEAR(static_cast<float>(crystal->volume), 0.8f);
        // The shared timer: the two crystal sounds hold EACH OTHER off, which
        // is one Timer in the original, not one per hook.
        CHECK_MSG(!crystal->timer.empty() && crystal->timer == alert->timer,
                  "the pick and the alert share one timer");
        CHECK_NEAR(static_cast<float>(crystal->minIntervalMs), 50.0f);
    }

    // The wood drag draws its speed, and is the hook that proved the whole
    // table had to be keyed on what reaches PlaySample.
    const Sounds::Hook* drag = rules.FindHook("playWoodDragSound");
    CHECK_MSG(drag != nullptr, "playWoodDragSound should read");
    if (drag != nullptr) {
        CHECK_MSG(drag->files.size() == 1 && drag->files[0] == "wood_drag.mp3",
                  "the drag plays wood_drag, not the portal_fail it only sets the speed on");
        CHECK_MSG(drag->speedIsRandom, "its speed is drawn");
    }

    // The five tracks, at the volumes the original starts them.
    const Sounds::Track* menu = rules.FindTrack("menu");
    const Sounds::Track* game = rules.FindTrack("game");
    const Sounds::Track* boss = rules.FindTrack("boss");
    CHECK_MSG(menu != nullptr && game != nullptr && boss != nullptr, "menu, game and boss music should read");
    if (menu != nullptr && game != nullptr && boss != nullptr) {
        CHECK_MSG(menu->file == "warlords_loop_a.mp3", "the menu is warlords_loop_a");
        CHECK_MSG(game->file == "finding_wonderland_60_loop.mp3", "a level is finding_wonderland");
        CHECK_MSG(boss->file == "running_with_wolves_loop_b.mp3", "a boss is running_with_wolves");
        CHECK_NEAR(static_cast<float>(menu->volume), 1.0f);
        CHECK_NEAR(static_cast<float>(game->volume), 0.7f);
        CHECK_NEAR(static_cast<float>(boss->volume), 0.6f);
        CHECK_MSG(menu->loop && game->loop && boss->loop, "all three loop");
    }
    CHECK_MSG(rules.music.size() == 5, "the original names five tracks, got " + std::to_string(rules.music.size()));

    // The events the layer actually latches, each resolving to a hook.
    const auto playsFor = [&rules](const char* event, const char* hook) {
        const Sounds::Hook* found = rules.ForEvent(event);
        CHECK_MSG(found != nullptr, std::string(event) + " should name a hook");
        if (found != nullptr) CHECK_MSG(found->name == hook, std::string(event) + " plays " + found->name);
    };
    playsFor("portal_placed", "playPortalCreatedSound");
    playsFor("shot_fired", "playPortalLaunchSound");
    playsFor("shot_failed", "playPortalFailedSound");
    playsFor("traversal", "playTeleportSound");
    playsFor("crystal_collected", "playCrystalPickSound");
    playsFor("player_died", "playDeathSound");
    // THE FINISH IS THE DOOR'S SOUND, not the medal's, and this pinned the wrong
    // one. GameStateController::checkGameEnd calls levelFinishedEffect - which
    // ends in playFinalDoorSound - then hides the character, and only once
    // gameWonDelay (1400 ms) has passed does it raise the finish layer and play
    // playVictorySound. The cue this used to name, playLevelFinishedSound, is
    // defined in AudioManager and called by NOTHING in the binary: a grep for its
    // CALLINTF finds the definition and no call site. sounds.json keeps it listed
    // as a decoded cue with no moment rather than deleting it.
    playsFor("level_finished", "playFinalDoorSound");
    // Dying is two moments as finishing is: the fall's own cue at the moment it
    // happens, and the lost screen's 1400 ms later. An hp death has no cue of
    // its own at all - checkGameLost plays one only on the bounds branch.
    playsFor("player_fell", "playDieByFallSound");
    playsFor("player_died", "playDeathSound");
    playsFor("medal_shown", "playVictorySound");
    playsFor("wall_broken", "playDemolitionSound");
    playsFor("menu_button", "getButtonSoundName");
    playsFor("level_button", "getRetryButtonSound");
}

// The one case that opens the original's own files.
void EverySoundNamedIsReallyThere() {
    Sounds::Rules rules;
    std::string error;
    if (!Sounds::LoadRules(kPortData + "/sounds.json", rules, error)) return; // said so above

    const std::string directory = Sounds::Directory(kOriginal);
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        std::printf("  no sound files at %s - the part that opens them is skipped\n", directory.c_str());
        return;
    }

    // Three silent-missing-art bugs in two days went unnoticed because nothing
    // asserted the files were there. A sound that is not there is quieter
    // still: there is not even a blank square to see.
    int checked = 0;
    std::string missing;
    const auto exists = [&](const std::string& file, const std::string& owner) {
        ++checked;
        std::error_code fileEc;
        if (std::filesystem::is_regular_file(directory + "/" + file, fileEc)) return;
        if (missing.empty()) missing = file + " (" + owner + ")";
    };
    for (const auto& [name, hook] : rules.hooks) {
        for (const std::string& file : hook.files) exists(file, name);
    }
    for (const auto& [name, track] : rules.music) exists(track.file, name);

    std::printf("  %d sound file(s) named, in %s\n", checked, directory.c_str());
    CHECK_MSG(checked > 50, "expected the whole table to name files, got " + std::to_string(checked));
    CHECK_MSG(missing.empty(), "named but not in soundfx/: " + missing);
}

void runTests() {
    TheReaderRefusesWhatWouldBeSilent();
    TheSpeedShapesRead();
    ThePortsOwnTableIsTheOriginals();
    EverySoundNamedIsReallyThere();
}

} // namespace

TEST_MAIN("test_mp_sounds", 45)
