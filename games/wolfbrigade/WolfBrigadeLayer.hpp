#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/AudioEngine.hpp"
#include "core/EngineLayer.hpp"

#include "sim/GameData.hpp"
#include "sim/Match.hpp"
#include "sim/Progression.hpp"

namespace WolfBrigade {

// The game, drawing the match it actually runs.
//
// This was a spike. It built a hardcoded lane of coloured quads and moved them
// with a sine wave, to answer three questions the port plan's estimates rested
// on: whether a game can live outside the engine at all, what the frame costs at
// Wolf Brigade's real drawable count, and whether a side-on lane reads on
// screen. All three came back yes, and the numbers are in
// docs/planning/2026-08-25-wolf-brigade-port.md.
//
// What it did NOT do was run the game. Four thousand lines of ported
// simulation, verified against twenty-two of the original's harnesses, sat in a
// library that only the test suites linked - so the thing on screen and the
// thing that had been proven correct were two different programs that had never
// met. This is the meeting.
//
// The layer owns the match and nothing else owns any of it: GameData is loaded
// once, a Profile and a Match are constructed over it, and OnFixedUpdate steps
// the match on the engine's tick and then makes the picture agree with it. The
// engine knows none of those types, which is the whole argument EngineLayer.hpp
// makes.
class WolfBrigadeLayer final : public Supersonic::EngineLayer {
public:
    // `saveDir` is where the profile and the Continue snapshot live, and it is
    // INJECTED rather than resolved here.
    //
    // Empty by default, which means this layer never touches the filesystem -
    // the same contract `Match`'s empty `runPath` already states, for the same
    // reason. Every suite that builds a layer builds it bare, and a hardcoded
    // path would have all of them writing into the ctest working directory and
    // reading each other's files, which is order-dependent failure that reads
    // as a flake.
    //
    // Only the shipped binary passes one, and it gets it from
    // Supersonic::UserDataDirectory. The layer does not ask for itself because
    // "where may I write" is a question about the machine, not about this game.
    explicit WolfBrigadeLayer(std::string saveDir = {});

    const char* Name() const override { return "WolfBrigade"; }

    void OnAttach(entt::registry& registry) override;
    void OnDetach(entt::registry& registry) override;

    // The match steps on the TICK, not the frame.
    //
    // Match::Step takes a delta and the original ran at a fixed rate, so
    // driving it from the frame delta would make the game's speed a function of
    // the display's - the exact bug the engine's own clock work exists to end.
    // The scene authors 30 Hz, which is what the tick rate is for.
    void OnFixedUpdate(entt::registry& registry, float fixedDelta) override;

    // The match this layer is running, or null before OnAttach and after
    // OnDetach.
    //
    // Exposed for the tests. Both constnesses, and the mutable one is not a
    // concession: what the bottom bar shows depends entirely on the state of
    // the run - which building is selected, what is affordable, what has been
    // researched - so a test that cannot put the run into a state cannot reach
    // most of the behaviour. Arranging the WORLD is testing; the thing that
    // would make these checks worthless is arranging the ANSWER, and the
    // assertions all read the simulation back rather than a value the test
    // supplied.
    // THE POINTER DOES NOT SURVIVE A TRANSITION. Leaving for the menu destroys
    // the Match, and a Restart replaces it - so a caller that holds one across
    // a tick in which either can happen is holding a dangling pointer, and the
    // Profile reference reachable through it dies with it. Ask again after the
    // tick rather than keeping it. (The renown itself outlives the run, on the
    // layer's own Profile; it is only this handle that dies.)
    const Match* CurrentMatch() const { return m_match.get(); }
    Match* CurrentMatch() { return m_match.get(); }

    // What the player keeps, which is the thing that OUTLIVES a match.
    //
    // Reachable through CurrentMatch too, and that is exactly why this exists:
    // on the menu, the Armory and the Settings screen there is no match, so the
    // only handle to a profile went away with the run - and those are the three
    // screens whose whole subject is the profile. Null only before OnAttach and
    // after OnDetach.
    const Profile* PlayerProfile() const { return m_profile.get(); }
    Profile* PlayerProfile() { return m_profile.get(); }

    // Which screen the player is on, from `game_flow.gd`.
    //
    // In Godot each of these is a whole scene and a transition is
    // `change_scene_to_file`, which destroys the tree. Here they are states of
    // one layer, because a layer IS the program - there is no tree to swap.
    //
    // Match FIRST, not Menu, even though `project.godot:10` boots the original
    // to the menu. Which screen a launched game opens on is a decision for the
    // commit that finishes the menu, and flipping it here would change what
    // every existing layer suite sees a tick after OnAttach.
    enum class Screen { Match, Menu, Armory, Settings };

    Screen CurrentScreen() const { return m_screen; }

private:
    // One reusable drawable.
    //
    // Pooled rather than created and destroyed per tick. A match spawns and
    // kills units constantly, and rebuilding the entities each tick would churn
    // the registry, invalidate every interpolation, and move the state hash for
    // reasons that are about drawing rather than about the game.
    struct Quad {
        entt::entity entity{entt::null};
        bool live{false};
    };

    // Takes the next free quad from the pool, growing it if it has run out, and
    // parks it where the caller says.
    entt::entity claim(entt::registry& registry, std::size_t& cursor,
                       const glm::vec2& simPosition, const glm::vec2& simSize,
                       const glm::vec3& colour, int32_t layer);

    // Everything past `used` is hidden rather than destroyed, for the reason
    // the pool exists.
    void retire(entt::registry& registry, std::size_t used);

    // The heads-up display: two resource counters, the wave counter and a
    // Pause button.
    //
    // Built ONCE, in OnAttach, and afterwards only written to. That is not a
    // style preference - it is the one hard constraint the UI puts on a caller.
    // A button's `pressed` flag lives on its component and a click is the
    // transition off it, so a screen rebuilt from the simulation every tick
    // hands the release to a component that was never pressed and no button
    // ever fires. `test_uiinput` pins it. The set of elements here never
    // changes, so building once is also just the simple thing; the bottom bar,
    // whose buttons depend on what is selected, is where this will actually
    // take thought.
    void buildHud(entt::registry& registry);

    // This tick's numbers into the labels built above. Writing text is not
    // rebuilding: the entities, and so the click state, survive.
    void updateHud(entt::registry& registry);

    struct Hud {
        entt::entity wood{entt::null};
        entt::entity food{entt::null};
        entt::entity wave{entt::null};
        entt::entity pause{entt::null};
    };
    Hud m_hud;

    // ---- The contextual bottom bar ----------------------------------------
    //
    // What it shows depends on what is selected: a completed building offers a
    // Train button per unit it trains and a Research button per upgrade it
    // still can, anything else offers the Build menu, and a placement in
    // progress replaces the lot with Cancel. That is four different button sets
    // over the same strip.
    //
    // WHICH IS WHY THIS IS THE HARD HALF. The set changes rarely - a selection,
    // a completed building, a bought upgrade - but AFFORDABILITY changes
    // constantly, because workers deposit wood every few seconds. Rebuilding on
    // every affordability change is what the original does and what this cannot
    // do: recreating a button destroys the `pressed` flag the release needs, so
    // a bar rebuilt under the player's finger never fires. See
    // testAButtonRecreatedMidGestureDoesNotFire.
    //
    // So the two are separated. The SIGNATURE - which buttons, in order -
    // decides whether entities are recreated. Everything else, affordability
    // included, is written into the buttons that are already there.
    enum class BarAction { Build, Train, Research, Cancel };

    struct BarButton {
        entt::entity entity{entt::null};
        BarAction action{BarAction::Build};

        // The building, unit or upgrade this button acts on. Empty for Cancel.
        std::string id;
    };

    // What the bar should show right now, derived from the match.
    std::vector<BarButton> desiredBar() const;

    // Makes the strip agree with `desiredBar`, recreating entities only when
    // the signature changed.
    void updateBar(entt::registry& registry);

    // Runs whatever the player pressed. Reads the TICK-latched click.
    void applyBarClicks(entt::registry& registry);

    // Label text for one button, cost and all, the way bottom_bar.gd builds it.
    std::string barLabel(const BarButton& button) const;

    // Whether it is affordable / researchable right now.
    bool barEnabled(const BarButton& button) const;

    // The horizontal stack the buttons hang off. Created once.
    entt::entity m_barStack{entt::null};
    std::vector<BarButton> m_bar;

    // ---- The pause overlay -------------------------------------------------
    //
    // Built once and shown by hiding and unhiding, not by creating and
    // destroying - the same constraint the bar obeys, for the same reason.
    //
    // Hiding the COLUMN is enough for everything in it, which it was not until
    // recently: a hidden stack used to leave its children with no rectangle, so
    // they fell back to their own anchors and landed in a heap in the middle of
    // the screen, invisible and still clickable over the running match. The
    // backdrop is a sibling rather than a child, so it is hidden alongside.
    void buildPauseMenu(entt::registry& registry);

    // Opens and closes it, and runs whatever was pressed.
    void updatePauseMenu(entt::registry& registry);

    void setPauseMenuVisible(entt::registry& registry, bool shown);

    struct PauseMenu {
        entt::entity backdrop{entt::null};
        entt::entity column{entt::null};
        entt::entity resume{entt::null};
        entt::entity restart{entt::null};
        entt::entity mainMenu{entt::null};
        entt::entity quit{entt::null};
    };
    PauseMenu m_pause;

    // Boots a fresh match over the top of this one, which is Restart.
    void restartMatch(entt::registry& registry);

    // ---- The game-over overlay ---------------------------------------------
    //
    // VICTORY or DEFEAT over the dimmed lane, with what the run earned, on
    // layer 10 - above the pause menu's 9, because you cannot pause a finished
    // game and the result must not be coverable by something you can still
    // open.
    void buildGameOver(entt::registry& registry);
    void updateGameOver(entt::registry& registry);

    struct GameOver {
        entt::entity backdrop{entt::null};
        entt::entity column{entt::null};
        entt::entity message{entt::null};
        entt::entity row{entt::null};
        entt::entity restart{entt::null};
        entt::entity mainMenu{entt::null};
    };
    GameOver m_over;

    // ---- The main menu -----------------------------------------------------
    //
    // `main_menu.gd` and `main_menu.tscn`, which in the original is the boot
    // scene. Built once and shown by hiding, like every other screen here.
    //
    // Layer 20, above the result overlay's 10. Not because anything raises a
    // result while the menu is up - going to the menu takes the result down -
    // but because the ordering has to be true independently of that. A screen
    // whose layer is only correct while some other screen behaves is a screen
    // that breaks when the other one changes.
    void buildMenu(entt::registry& registry);

    // This launch's numbers into the labels, and Continue shown only when there
    // is a run to continue. Called on the way IN to the screen rather than
    // every tick: `main_menu.gd` does its reading in `_ready`, and a Continue
    // button whose visibility is recomputed under the player's finger is the
    // rebuild-mid-gesture bug wearing a different hat.
    void refreshMenu(entt::registry& registry);

    void updateMenu(entt::registry& registry);
    void setMenuVisible(entt::registry& registry, bool shown);

    // One option of a radio row: the button, and the id it selects.
    struct Radio {
        entt::entity entity{entt::null};
        std::string id;
    };

    // Paints a radio row so the chosen one reads as chosen.
    //
    // THREE COLOURS, not one, and that is the whole subtlety. `UISystem` picks
    // the fill fresh every frame in the order disabled, pressed, hovered,
    // colour - so writing `color` alone gives a selection that VANISHES the
    // moment the pointer crosses it, which is the one button the player is
    // most likely to have the pointer over.
    //
    // Written per tick from the layer's own selection rather than held on the
    // component. A `selected` field would be the struct, the codec both ways,
    // the inspector twice, a script-ABI bump and two test floors - about eleven
    // sites, of which this game uses none: it ships no scene, no prefab and no
    // script, so the only one that would ever run is the draw.
    void paintRadios(entt::registry& registry, const std::vector<Radio>& row,
                     const std::string& selected) const;

    struct MainMenu {
        entt::entity backdrop{entt::null};
        entt::entity column{entt::null};
        entt::entity best{entt::null};
        entt::entity renown{entt::null};
        entt::entity resume{entt::null};
        entt::entity newGame{entt::null};
        entt::entity armory{entt::null};
        entt::entity settings{entt::null};
        entt::entity quit{entt::null};

        // The two radio rows, each a horizontal stack nested in the column.
        entt::entity modeRow{entt::null};
        entt::entity difficultyRow{entt::null};
        std::vector<Radio> modes;
        std::vector<Radio> difficulties;

        // The New Game confirmation, from `main_menu.gd:73-89`.
        //
        // A separate column and backdrop rather than a flag on the menu,
        // because the original hides `$Center` and `$SoundButton` while it is
        // up - it is a screen over a screen, not a state of one.
        entt::entity confirmBackdrop{entt::null};
        entt::entity confirmColumn{entt::null};
        entt::entity confirmYes{entt::null};
        entt::entity confirmNo{entt::null};
    };
    MainMenu m_menu;

    // Whether the New Game confirmation is up.
    bool m_confirmingNewGame{false};

    void setConfirmVisible(entt::registry& registry, bool shown);

    // ---- Sound -------------------------------------------------------------
    //
    // `audio.gd`'s half that needs a device, which is everything the sim's
    // `Audio` namespace deliberately left out: the voice pool, the mute flag,
    // the volumes and the wiring from gameplay signals to sounds.
    //
    // The synthesis is NOT here - `sim/AudioTones` already produces the samples
    // and has its own suite against the original's harness. What this does is
    // register those samples with the engine once and play them.
    void attachAudio(entt::registry& registry);

    // Subscribes the nine gameplay signals `connect_events` wires. Re-run on
    // every new Match, because a Match owns its own EventBus and the old one
    // died with the run it belonged to.
    void connectAudioEvents();

    // One sfx, rate-limited per id.
    //
    // `play_sfx_throttled`, for the two combat sounds. Many units can attack in
    // the same tick and the soundscape turns to mush without it - the original
    // calls that out by name and picks seventy milliseconds.
    //
    // Measured in SIMULATED time, where the original reads
    // Time.get_ticks_msec(). Every other deadline in this game is the tick's,
    // and a wall clock inside a layer would make how a paused game sounds when
    // it resumes depend on how long the player left it.
    void playSfxThrottled(const std::string& id, double minimumGap);

    // Simulated seconds since the layer attached, and when each throttled id
    // last played. Only the tick advances it, so a paused game holds.
    double m_simTime{0.0};
    std::map<std::string, double> m_lastPlayed;

    // `COMBAT_THROTTLE_MS`, in seconds.
    static constexpr double kCombatThrottle = 0.070;

    // One sfx, through the round-robin pool.
    //
    // SIX VOICES, from `audio.gd`'s own constant, and the pool is ported rather
    // than dropped even though AudioEngine::Play mints a voice per call. It is
    // what stops a wave of forty deaths from starting forty simultaneous
    // sounds, which is a design decision about how the game sounds and not an
    // artefact of Godot needing one player per voice.
    void playSfx(const std::string& id);

    // Where the profile's volume and mute reach the mixer. `audio.gd` computes
    // a dB sum and Godot applies it to a bus; here it is a linear gain folded
    // into the one Play already takes.
    float sfxVolume() const;

    static constexpr std::size_t kSfxVoices = 6;

    // Not owned. Handed over by AudioSystem through the registry context, which
    // is where a layer can reach it - the engine inserts it during init, before
    // any layer is pushed, and it outlives every scene load.
    //
    // Null when the engine has no device, which is a real state on a build
    // machine and on a headless run. Every path here checks it.
    Supersonic::AudioEngine* m_audio{nullptr};

    Supersonic::AudioEngine::VoiceId m_sfxVoices[kSfxVoices]{};
    std::size_t m_nextVoice{0};

    // Which sfx ids actually got a clip. An id whose data names a FILE rather
    // than a tone has nothing to synthesise, and this port has no resource
    // system to load one with - so it is absent rather than silently silent.
    std::vector<std::string> m_sfxNames;

    // The subscriptions on the current Match's bus, so a new Match does not
    // stack a second set of handlers on top.
    std::vector<int> m_audioSubscriptions;

    // ---- The Armory --------------------------------------------------------
    //
    // `armory.gd`: one row per meta upgrade, renown spent on levels that apply
    // to every run after.
    //
    // BUILT ONCE AND WRITTEN, where the original rebuilds every row on every
    // purchase. Godot can afford that because `queue_free` is deferred, so the
    // button that is mid-press survives its own handler. Here a rebuilt button
    // is a new component with `pressed` false, and the release lands on
    // something that was never pressed - the constraint `test_uiinput` pins and
    // the bottom bar was designed around. The set of upgrades is fixed by
    // `meta.json` anyway, so there is nothing a rebuild would change except the
    // text, and text is written.
    void buildArmory(entt::registry& registry);
    void updateArmory(entt::registry& registry);
    void setArmoryVisible(entt::registry& registry, bool shown);

    struct ArmoryRow {
        std::string id;
        entt::entity title{entt::null};
        entt::entity buy{entt::null};
    };

    struct Armory {
        entt::entity backdrop{entt::null};
        entt::entity column{entt::null};
        entt::entity renown{entt::null};
        entt::entity back{entt::null};
        std::vector<ArmoryRow> rows;
    };
    Armory m_armory;

    // ---- Settings ----------------------------------------------------------
    //
    // `settings.gd`: master volume, mute, and a two-tap Reset Progress. Every
    // value it moves already exists on the Profile - this screen is what gives
    // them a way to be moved.
    void buildSettings(entt::registry& registry);
    void updateSettings(entt::registry& registry, float fixedDelta);
    void refreshSettings(entt::registry& registry);
    void setSettingsVisible(entt::registry& registry, bool shown);

    struct Settings {
        entt::entity backdrop{entt::null};
        entt::entity column{entt::null};
        entt::entity volume{entt::null};
        entt::entity down{entt::null};
        entt::entity up{entt::null};
        entt::entity mute{entt::null};
        entt::entity reset{entt::null};
        entt::entity back{entt::null};
    };
    Settings m_settings;

    // Reset Progress takes two taps, from `settings.gd:44-60`, and disarms
    // itself so a stale armed button cannot wipe a profile on a later stray
    // one.
    //
    // Counted in TICKS rather than off a wall clock. The original hangs a
    // three-second SceneTree timer on it; this game's every other deadline is
    // simulated time, and a wall clock in a layer is the thing the engine's own
    // fixed-tick work exists to get rid of.
    bool m_resetArmed{false};
    float m_resetArmedFor{0.0f};

    // ---- Routing -----------------------------------------------------------

    // Leaves whatever screen we are on and enters `next`.
    //
    // ONE function, which is what `game_flow.gd::_change_to` is for: it does
    // exactly two things at every transition, and the second - clearing the
    // pause - has its own comment saying a Quit-to-Menu from a paused game
    // would otherwise leave the menu frozen. Here there are three such things,
    // because the result overlay is a second modal state the original throws
    // away with the scene.
    //
    // NOT DEFERRED, unlike the original's `call_deferred`. That exists because
    // a Godot `pressed` signal is mid-emit when the emitter is freed. Clicks
    // here are tick-latched and read inside the tick, so there is no live
    // emitter - and `restartMatch` already destroys button entities
    // synchronously from inside `updatePauseMenu` and has shipped that way.
    void goTo(entt::registry& registry, Screen next);

    // Makes the in-match screens - HUD, bar, and the lane itself - agree with
    // whether there is a match to look at.
    void setMatchVisible(entt::registry& registry, bool shown);

    // Boots a match and enters it: `game_flow.gd::start_game`, both forks.
    // `fromSave` restores the run on disk; otherwise it abandons it and starts
    // fresh, which is `_start_new_game`.
    void startGame(entt::registry& registry, bool fromSave);

    Screen m_screen{Screen::Match};

    // Whether the overlay is up. Held rather than re-derived from the phase
    // each tick, because the text is written ONCE on the transition: the
    // original reads the wave before anything resets it, and a line recomputed
    // every tick would be a different claim about the same run.
    bool m_showingResult{false};

    // The game's own pause, which is not the engine's. The tick still runs -
    // the layer simply stops stepping the match - so the UI stays live and the
    // button that got us here can get us back.
    bool m_paused{false};

    std::unique_ptr<GameData> m_data;
    std::unique_ptr<Profile> m_profile;
    std::unique_ptr<Match> m_match;

    // Where this game may write, and the two files it writes there. Both are
    // empty when no directory was injected, which is what keeps a bare layer
    // filesystem-free.
    //
    // The FILENAMES are the original's - `wolf_brigade_save.json` and
    // `wolf_brigade_run.json` - so a player's save is the same document either
    // side of the port. They differ in lifetime, which is why the original
    // splits them and this does too: the profile outlives everything, and the
    // run file is written when you leave a live match and deleted when one
    // ends.
    std::string m_saveDir;
    std::string m_profilePath;
    std::string m_runPath;

    // Said once. A save directory that refuses writes must not print a line a
    // tick for the rest of the session.
    bool m_reportedSaveFailure{false};

    // Pushes the saved mode and difficulty into the run being booted. Called
    // by every site that builds a Match, which is the point: there are three.
    void applySavedRules();

    // Writes the profile if it has anything to write. Called at the end of the
    // tick and again on the way out, which between them is `save.gd`'s
    // write-through with one disk touch instead of one per setter.
    void saveProfileIfDirty();

    // The tick itself. Separate from OnFixedUpdate only so that the save above
    // runs whichever of this function's early returns was taken.
    void tick(entt::registry& registry, float fixedDelta);

    std::vector<Quad> m_pool;

    // The camera, kept so the view can follow the lane.
    entt::entity m_camera{entt::null};

    bool m_booted{false};
};

} // namespace WolfBrigade
