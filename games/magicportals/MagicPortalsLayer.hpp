#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/EngineLayer.hpp"

#include <random>

#include "core/AudioEngine.hpp"
#include "core/BitmapFont.hpp"

#include "sim/Achievements.hpp"
#include "sim/Camera.hpp"
#include "sim/Chapters.hpp"
#include "sim/Credits.hpp"
#include "sim/Dashboard.hpp"
#include "sim/Game.hpp"
#include "sim/Art.hpp"
#include "sim/Hud.hpp"
#include "sim/LevelEnd.hpp"
#include "sim/Lighting.hpp"
#include "sim/Loading.hpp"
#include "sim/Locking.hpp"
#include "sim/MainMenu.hpp"
#include "sim/MenuState.hpp"
#include "sim/Particles.hpp"
#include "sim/Pause.hpp"
#include "sim/Popup.hpp"
#include "sim/Scores.hpp"
#include "sim/Sounds.hpp"
#include "sim/Sprites.hpp"

namespace MagicPortals {

// Magic Portals as the port plays it: the levels in the original's order, drawn
// with the levels' own art.
//
// Deliberately plain, like HUSK's first view. What it has to get right is the
// seam:
//
//  - The level is the layer's (Game::Level), and it runs on the port's 60 Hz
//    tick, set on the engine's clock. The app steps physics before each
//    OnFixedUpdate, so the layer runs Game::AfterStep then Game::BeforeStep.
//    That is the suites' tick, with the app's step between the halves.
//  - Input is read on the tick. Left and right held are the remake's two-button
//    pad, and a tap fires a portal shot at the point in the level under it. There
//    is one tap per tick at most, so a frame cannot spend the budget twice in one
//    step. R retries and N skips, on the tick as well.
//  - The levels come in chapters.json's order. Reaching the exit loads the next
//    level of the world at once, as the remake advances (main.gd:161-168). Retry
//    is instant: the level is rebuilt from what was read when it loaded, and the
//    disk is not touched (level_manager.gd:5-13). A level the port refuses says
//    why, and N skips it, the remake's own development shortcut. At a world's end
//    the remake opens its menu; the port has none yet, so it says the chapter is
//    complete.
//  - The camera is orthographic and follows the player, as the owner remembers
//    it: Camera::Follow, moved on the tick and drawn between ticks. It shows
//    view.json's height of the level, 256 px, at the window's shape.
//  - Every sprite the level names (Sprites.hpp) is a textured quad, unlit and
//    blended as the level says, in Godot's canvas order. A sprite follows its
//    node's body, or the crystal, static portal or no-portal zone it pictures,
//    and goes when that goes. The bodies are also boxes in the colour of what
//    they are, hidden behind the art until B shows them - and shown anyway when
//    the art cannot be read, which is a level still played, drawn plainly.
//  - The scene holds display values, as the original's 8-bit framebuffer did
//    (RenderSettings::SceneEncoding::DisplayEncoded, set at attach): a texture
//    is sampled as the bytes in its file, tints and blends work on those bytes,
//    and nothing is tone-mapped or bloomed, on a flat black ground.
//  - Every sprite of a level is its colour times min(1, ambient + emissive), as
//    the original draws it (Lighting::AmbientTerm): the ambient the level file
//    gives, or lighting.json's where the script replaces it - a `darkest` level,
//    a lit torch - and the emissive its node or its .ent gives. The layer keeps
//    each sprite's own colour (a crystal's fade, the beholder's red) apart from
//    that factor, and hands both to the engine's 2D sprite path, premultiplied.
//    A static sprite that applies light adds its baked lightmap over that.
//  - A level's <Light>s are the engine's 2D point lights, and their halos added
//    quads, following their owners; the shot carries a light of its own. A
//    sprite that applies light takes, per pixel through its normal map, every
//    light that is not static when it is static itself - its static lights are
//    in its lightmap - and every light when it is not, which is how the player
//    is lit by a torch it walks past.
//  - What the levels do not picture, the game draws as the original's own
//    entities draw it (Art.hpp): the portals a shot opens, and the shot. Their
//    images are read from the original's extracted assets; without them they
//    are boxes. So is the player: dark_mage.ent's sheet, turned as it walks. So
//    is chapter 1's boss: beholder.ent, its eye open or shut, reddening as it
//    is hurt and pulsing as it goes, with the spikes it fires and the rocks it
//    drops.
class MagicPortalsLayer final : public Supersonic::EngineLayer {
public:
    // The port's tick: the remake's physics runs at Godot's default 60 Hz.
    static constexpr float kTick = 1.0f / 60.0f;

    // The actions this layer binds at attach, named so tests and a recording
    // can drive them without knowing which buttons they sit on.
    static constexpr const char* kLeft = "mp.left";          // Left arrow
    static constexpr const char* kLeftAlt = "mp.left.alt";   // A
    static constexpr const char* kRight = "mp.right";        // Right arrow
    static constexpr const char* kRightAlt = "mp.right.alt"; // D
    static constexpr const char* kTap = "mp.tap";            // left mouse: fire a portal shot
    static constexpr const char* kRetry = "mp.retry";        // R
    static constexpr const char* kSkip = "mp.skip";          // N
    static constexpr const char* kBoxes = "mp.boxes";        // B: the bodies' boxes, over the art
    // Escape: the pause over a level being played and back out of it, as the
    // original's back key (GameState::handleBackButton); up a screen in the menu.
    static constexpr const char* kBack = "mp.back";
    // G: write down what the game believes it is drawing, right now.
    //
    // A diagnostic rather than a control, and it exists because a sprite that
    // vanishes on screen has four possible explanations that look identical to
    // a player and to a screenshot: the layer took its quad away, the renderer
    // culled it, a pass refused the draw, or it was drawn and produced nothing
    // visible. This says which.
    static constexpr const char* kDump = "mp.dump";

    // Where the port reads from. The defaults are where the build was told the
    // remake's files are, and the port's own data beside its source.
    struct Paths {
        std::string levels = MAGICPORTALS_LEVELS_DIR;
        std::string chapters = MAGICPORTALS_CHAPTERS_FILE;
        // The original's achievements, extracted beside chapters.json: never in this
        // repository (sim/Achievements.hpp). The dashboard draws no rows without it.
        std::string achievements = MAGICPORTALS_ACHIEVEMENTS_FILE;
        std::string data = MAGICPORTALS_DATA_DIR;
        std::string portData = MAGICPORTALS_PORT_DATA_DIR;
        // Where LevelBuilder may write, and the loading screen's black halo
        // (loadingHaloImage). Nothing is written when it is empty.
        std::filesystem::path prisms;
        // What res:// stands for: the converter writes the levels' art beside
        // the levels, so the directory above them (Sprites.hpp).
        std::string art = MAGICPORTALS_LEVELS_DIR "/..";
        // The original's extracted assets, whose entities/ holds the images of
        // what no level places (Art.hpp).
        std::string original = MAGICPORTALS_ORIGINAL_DIR;

        // WHERE THE PLAYER'S MEDALS ARE KEPT, and empty by default - which
        // means this layer keeps no save on the filesystem.
        //
        // Injected rather than resolved here, which is WolfBrigadeLayer's
        // contract and exists for the reason its header states: "where may I
        // write" is a question about the machine, not about this game, and a
        // layer that resolved its own path could not be built by a test
        // without one. Every suite builds the layer bare, and a hardcoded path
        // would have eighteen of them writing the same filenames into the
        // ctest working directory and reading each other's - order-dependent
        // failure that reads as a flake.
        //
        // Only the shipped binary passes one, from UserDataDirectory.
        std::string saveDir;
    };

    // The last level cleared: the portals spent against its golden score, which
    // is what the remake records (level_manager.gd:116-121).
    struct Cleared {
        std::string name;
        std::string label;
        int portalsUsed = 0;
        int goldenScore = 0;
        int traversals = 0;
        int crystals = 0;
        int crystalsTotal = 0;

        bool Gold() const { return goldenScore >= 0 && portalsUsed <= goldenScore; }
    };

    // Which screen is up. `None` is a level, played exactly as before: a layer
    // built with a level's name never sees the menu at all, which is how every
    // suite, every headless render and --level enter the game.
    //
    // The original's own menu STATES: LoadingScreen, PortalMainMenu,
    // WorldSelector, LevelSelector, CreditsScreen and ScoreDashboard. Each opens
    // under its own black (menu_state.fade) and presses its buttons as
    // Button::update does (menu_state.press). The loading screen and the main menu
    // are the remake's ui3 spec section 2 (sim/Loading.hpp, sim/MainMenu.hpp),
    // credits and the achievements dashboard its sections 3 and 4
    // (sim/Credits.hpp, sim/Dashboard.hpp), all drawn by EmitMenu; chapter select
    // and the grid are still the port's own layout of the original's art, which
    // ui3 sections 5 and 6 are to replace.
    // `Finished` and `Dead` are the odd two: each sits OVER the level it ended,
    // which stays loaded, drawn AND RUNNING - neither screen stops game time
    // (the remake's ui2 spec, D7) - and both are drawn through the screen
    // overlay on the view, as the pause is (sim/LevelEnd.hpp). Everything that
    // treats a screen as "the level is gone" has to name both.
    enum class Screen { None, Loading, Main, Worlds, Levels, Credits, Achievements, Finished, Dead };

    // A button the menu drew, in the menu's pixel box (MenuBoxPx). Kept as
    // data so a click is tested against exactly what was drawn, and so a test
    // can press one without a pointer or a camera.
    struct MenuButton {
        enum class Kind { Play, World, Level, Back, Forward, Retry, Next, List };
        Kind kind{Kind::Play};
        glm::dvec2 centrePx{0.0};
        glm::dvec2 sizePx{0.0};
        int world{0};  // World and Level
        int level{-1}; // Level: its place in chapters.levels
    };

    MagicPortalsLayer(Paths paths, std::string startLevel);

    const char* Name() const override { return "Magic Portals"; }

    void OnAttach(entt::registry& registry) override;
    void OnDetach(entt::registry& registry) override;
    void OnFixedUpdate(entt::registry& registry, float fixedDelta) override;
    void OnUpdate(entt::registry& registry, float deltaTime) override;

    // Why the level being shown did not load, or why nothing could start; empty
    // when it did. A level that fails leaves the layer saying so, rather than
    // throwing out of OnAttach.
    const std::string& LoadError() const { return m_loadError; }

    // Why the level's art could not be drawn, when it could not - the level is
    // then drawn as boxes - or empty.
    const std::string& ArtError() const { return m_artError; }

    // Why the level's lighting could not be read (Lighting::Read), when it could
    // not - its sprites are then drawn in their own colours, unlit, as before
    // lighting existed - or empty.
    const std::string& LightingError() const { return m_lightingError; }

    // The ambient light the last tick coloured the level's sprites with
    // (Lighting::Ambient). (1, 1, 1) when there is no level or its lighting did
    // not read, which multiplies nothing.
    glm::dvec3 AmbientNow() const { return m_ambient; }

    // The lightmap files of the level being drawn, which the layer hands back to
    // the engine's texture registry when the level goes (TextureRegistry::
    // Invalidate). Kept across a retry, which draws the same level again, and
    // given back by anything else that unloads it: the next level, the menu,
    // detaching. Each is the overlay of the sprite it was baked for (since step
    // 47), so giving them back gives back a texture and a material set apiece.
    const std::vector<std::string>& HeldLightmaps() const { return m_heldLightmaps; }
    // How many lightmap paths the layer has handed back over its life, whether or
    // not a registry was there to take them. For the suites, which have none to
    // watch: it is how a retry is seen to keep them.
    std::size_t LightmapsHandedBack() const { return m_lightmapsHandedBack; }

    // Whether B has the bodies' boxes shown over the art.
    bool ShowingBoxes() const { return m_showBoxes; }

    // DEV ONLY: every sprite's 2D light mask forced to 0, so no light reaches any
    // sprite while the lights, the halos and everything else are drawn as before.
    // It exists for one measurement: a capture with the lights and the same capture
    // without them differ by exactly what the lights add (step 49's torch pass).
    // Takes effect on the next tick's colouring.
    void ForceLightMasksOff(bool off) { m_lightMasksOff = off; }
    bool LightMasksForcedOff() const { return m_lightMasksOff; }

    // The level being played, or null when it did not load.
    const Game::Level* SimLevel() const { return m_loaded ? &m_level : nullptr; }

    // The entry in chapters.json being shown, or null when nothing could start.
    const Chapters::Level* Current() const;

    bool ChapterComplete() const { return m_chapterComplete; }

    // The menu, when one is up.
    Screen MenuScreen() const { return m_screen; }
    const std::vector<MenuButton>& MenuButtons() const { return m_menuButtons; }
    // The box the menu is laid out in: view.json's height at the window's
    // shape, with (0, 0) at its top-left corner, so the original's normalized
    // positions multiply straight into it.
    glm::dvec2 MenuBoxPx() const;
    // Press a button, as a click on it does. False when it led nowhere.
    //
    // BY VALUE, and that is not a style choice: pressing one lays the screen
    // out again, which clears the very vector the caller's button lives in -
    // the click loop below iterates that vector - so a reference would dangle
    // before the level it names could be read out of it.
    bool PressMenu(entt::registry& registry, MenuButton button);

    // The sound events latched and not yet played, oldest first.
    //
    // For the suites, and it is the only way to test this half at all: a sound
    // is played on the FRAME, so a run with no audio device - which is every
    // suite, every headless render, and Linux - plays nothing and would
    // otherwise leave nothing to assert. This is what OnUpdate would play.
    std::vector<std::string> LatchedSounds() const;

    // Why there is no sound, when there is none.
    const std::string& SoundsError() const { return m_soundsError; }

    // How many times the player has died this session. Each death is a retry.
    int Deaths() const { return m_deaths; }
    const std::optional<Cleared>& LastCleared() const { return m_lastCleared; }

    // The camera as the last tick left it, and what it shows, in the level's
    // pixels; and the level's extent.
    glm::dvec2 CameraCentrePx() const { return m_follow.centrePx; }
    glm::dvec2 ViewPx() const;
    glm::dvec2 BoundsPx() const { return m_boundsPx; }

    // A screen point (Input's coordinates) as the point in the level under it,
    // in the remake's pixels. False when there is no viewport or camera.
    bool ScreenToLevelPx(const entt::registry& registry, const glm::vec2& screenPoint, glm::dvec2& outPx) const;

    // ---- the HUD, and the seconds a level opens with ------------------------
    //
    // The on-screen controls, in the original's order of drawing: the walk pads
    // in the bottom corners, restart and pause flush in the top-right, and the
    // clear-portals button in the top-left while a placed portal is alive.
    enum class Control { Left, Right, Reset, Menu, Clear };

    // ui.json, as read at attach. HudError says why nothing could start when
    // the port's data - ui.json among it - would not read; empty otherwise.
    const Hud::Rules& HudRules() const { return m_hudRules; }
    const std::string& HudError() const { return m_hudError; }

    // How long the level being shown has been loaded, in milliseconds of the
    // TICK's clock: zero on the tick it loads, a retry included. Everything the
    // level opens with - the black, "Part N", the plaque, the pads' slide and
    // pulse - is a function of this and nothing else.
    double LevelAgeMs() const { return m_levelAgeMs; }

    // A control's rectangle on the view as the last tick laid it out, (0, 0)
    // the view's top-left, in design units; false when it is not shown.
    bool ControlRect(Control control, Hud::Rect& out) const;

    // The no-portal sign's rectangle on the view as the last tick left it, in
    // design units; false in a level that places none.
    bool NoPortalSignRect(Hud::Rect& out) const;

    // ---- the menu states: the loading screen, the main menu and the black -----
    //
    // Every menu state's clock runs on the tick (ui.json menu_state._about): zero
    // on the state's first tick, which is the tick after the one whose release -
    // or whose loading hold, or back key - asked for it, as the original's
    // setState swaps between frames. The layer's own clock, which the title's bob
    // reads, runs from attach.
    const MenuState::Rules& MenuStateRules() const { return m_mainMenuRules.state; }
    const MainMenu::Rules& MainMenuRules() const { return m_mainMenuRules; }
    const Loading::Rules& LoadingRules() const { return m_loadingRules; }
    // How long the menu state that is up has been current: ticks and milliseconds.
    int MenuStateTicks() const { return m_menuClock.ticks; }
    double MenuStateMs() const { return m_menuClock.ms; }
    // The layer's own clock since it attached, in milliseconds of the tick.
    double LayerClockMs() const { return m_layerClockMs; }
    // The main menu's buttons a held touch that went down inside them is still
    // inside (MainMenu::Bit): drawn at the press tint.
    unsigned MainMenuHeld() const { return m_menuTouch.mainHeld; }
    // The main menu's two switches, and the music switch's clock on this state.
    Pause::Switches MainMenuSwitches() const;
    // Press one of the main menu's buttons, as a release on it does - at once: a
    // touch's release asks for it on the tick after. False when it led nowhere: no
    // main menu is up, or the music switch is not there.
    bool PressMainMenu(entt::registry& registry, MainMenu::Button button);

    // ---- credits and the achievements dashboard -------------------------------
    //
    // The main menu's info and Achievements buttons open them; each back button,
    // and the back key, goes back to the main menu on the tick after. Both are
    // drawn by EmitMenu through the overlay, and keep their scroll across the
    // per-tick layout and a resize.
    const Credits::Rules& CreditsRules() const { return m_creditsRules; }
    // The credits' strip as the last tick left it.
    const Credits::Scroll& CreditsScroll() const { return m_credits; }
    // Whether the credits' back button is held down inside: the press tint.
    bool CreditsBackHeld() const { return m_creditsTouch.backHeld; }
    const Dashboard::Rules& DashboardRules() const { return m_dashboardRules; }
    const Locking::Rules& LockingRules() const { return m_lockingRules; }
    // The rows as the dashboard opened with them, and its scroll and buttons.
    const Dashboard::Board& DashboardBoard() const { return m_dashboardBoard; }
    const Dashboard::State& DashboardState() const { return m_dashboardState; }
    // The original's achievements, when Paths::achievements could be read; null
    // otherwise, and AchievementsError says why.
    const Achievements::Content* AchievementsContent() const {
        return m_achievementsLoaded ? &m_achievements : nullptr;
    }
    const std::string& AchievementsError() const { return m_achievementsError; }
    // Puts this frame's menu into the screen overlay: the main menu's pictures, the
    // loading screen's logo and dots, and every menu state's black. Nothing when no
    // menu state is up. OnUpdate calls it once a frame.
    void EmitMenu(entt::registry& registry) const;

    // Puts this frame's HUD into the engine's screen overlay, in the original's
    // order of drawing. OnUpdate calls it once a frame; it is public so a suite
    // can ask for exactly the HUD without the rest of a frame. Nothing is drawn
    // when the registry publishes no overlay, which a bare suite registry does
    // not until it inserts one.
    void EmitHud(entt::registry& registry) const;

    // ---- the pause screen -----------------------------------------------------
    //
    // CustomGameMenuLayer, over the level it pauses: the in-level pause control
    // and Escape open it, and it STOPS GAME TIME - the level is not stepped, the
    // world does not move, the pads are not drawn - while its own entrance runs
    // on a clock of its own. Laid out by sim/Pause.hpp from ui.json and drawn
    // through the screen overlay after restart, pause and the blacks. The level
    // stays loaded under it and MenuScreen() stays Screen::None: the pause is a
    // layer over a level, not a screen that replaces one.
    bool Paused() const { return m_pause.open; }
    // How long it has been up, in milliseconds of the tick's clock: zero on the
    // tick it opened.
    double PauseClockMs() const { return m_pause.clockMs; }
    const Pause::Rules& PauseRules() const { return m_pauseRules; }
    // What the open pause was raised over, read when it opened.
    const Pause::Level& PausedLevel() const { return m_pause.level; }
    // The two switches, and the music switch's clock on this pause.
    Pause::Switches PauseSwitches() const;
    // Whether the game makes a sound at all, and plays its music: the pause's
    // two switches, which hold for the session.
    bool SoundOn() const { return m_soundOn; }
    bool MusicOn() const { return m_musicOn; }
    // Press one of the pause's buttons, as a tap on it does. False when it led
    // nowhere: no pause is up, the button is not there, or it is Achievements,
    // which the port has no screen for yet.
    bool PressPause(entt::registry& registry, Pause::Button button);

    // The level's FRAME clock: its age plus every millisecond game time stood
    // still under a pause or a popup. What the original times by frame time
    // neither stops - the level-start caption, the blacks' wall clock - reads
    // this; what runs on game time reads LevelAgeMs.
    double LevelFrameMs() const { return m_levelAgeMs + m_stoppedMs; }

    // Whether game time is stopped: a pause or a popup is up.
    bool GameTimeStopped() const { return m_pause.open || m_popup.open; }

    // ---- the tutorial and help popups -------------------------------------------
    //
    // ETHFramework's Popup over the level, laid out by sim/Popup.hpp from ui.json's
    // popups block: 1-02 and 1-03 raise one as they load, and thirteen levels from
    // a help block, on a touch released inside it. It STOPS GAME TIME as the pause
    // does, for as long as it is up and through its fade out, while its own
    // entrance and demonstration run on its clock; a touch down anywhere, or the
    // back key, closes it. MenuScreen() stays Screen::None under it.
    bool PopupOpen() const { return m_popup.open; }
    const Popup::Rules& PopupRules() const { return m_popupRules; }
    // The popup that is up, and its class; null when none is.
    const Popup::Open* OpenPopup() const { return m_popup.open ? &m_popup.state : nullptr; }
    const Popup::Class* OpenPopupClass() const { return m_popup.open ? m_popup.cls : nullptr; }
    // Close the popup that is up, as a touch down does. False when none is up or
    // it is already closing.
    bool ClosePopup();
    // The level's help blocks' touch rectangles on the view, as the last tick
    // left the camera, in design units; empty in a level with none.
    std::vector<Hud::Rect> HelpBlockRects() const;

    // DEV ONLY: a touch at a point of the view - given as fractions of it - pressed
    // on the layer's tick `tick` and released on `releaseTick` (the next, when that
    // is not after `tick`), held where it went down in between, for a --fixed-step
    // capture. With no point it lands on the centre of the level's first help
    // block, wherever the camera has it on that tick. The popups and the menu
    // states read it: it opens a help block's popup, closes one that is up, and
    // presses a menu's buttons as a touch does.
    void ScheduleDevTap(int tick, std::optional<glm::dvec2> viewFraction, int releaseTick = 0);
    // DEV ONLY: a drag - a touch down at `from` on tick `tick`, moved at an even pace
    // to reach `to` on `releaseTick`, and released there. Both are fractions of the
    // view. The credits' and the dashboard's scroll captures need a finger that moves.
    void ScheduleDevDrag(int tick, const glm::dvec2& from, const glm::dvec2& to, int releaseTick);

    // DEV ONLY: press one of these on the layer's tick `tick` (1 the first
    // OnFixedUpdate), as a tap would, for a --fixed-step capture that has no
    // input. `Pause` is the in-level pause control; `Back` the back key on a menu
    // state; the rest are the pause's own buttons. A press is taken on the first
    // tick at or after its own on which what it presses is there to press.
    enum class DevPress { Pause, Levels, Resume, Skip, Achievements, Sound, Music, Back };
    void ScheduleDevPress(int tick, DevPress press);

    // DEV ONLY: hold a walk from tick `from` to tick `to` inclusive, as a held
    // arrow would: -1 left, 1 right. A --fixed-step capture has no input, and the
    // finished and lost screens need a walk into a door or a hazard.
    void ScheduleDevHold(int from, int to, float direction);

    // ---- how a level ends -----------------------------------------------------
    //
    // The beat after the door or the death, the HUD going, and the finished and
    // lost screens over the level that goes on running (sim/LevelEnd.hpp, from
    // ui.json's level_end block).
    const LevelEnd::Rules& LevelEndRules() const { return m_levelEndRules; }
    // Whether the door was reached, or the player killed, on this level.
    bool Finishing() const { return m_finishing; }
    bool Dying() const { return m_dying; }
    // Game time since the door or the death: what the beat is counted in.
    double EndedMs() const { return m_dying ? m_dyingClockMs : m_finishClockMs; }
    // The pads' alpha byte as it decays from the end on, once a tick.
    int EndPadAlphaByte() const { return m_padEndByte; }
    // How long the finished or lost screen has been current, in milliseconds of
    // the tick's clock: zero on the tick it came up.
    double EndScreenClockMs() const { return m_end.clockMs; }
    // What the finished screen scores, and where its two counters have got to.
    const LevelEnd::Play& EndPlay() const { return m_end.play; }
    int PortalsCounted() const { return m_end.portals.current; }
    int CrystalsCounted() const { return m_end.crystals.current; }

private:
    // A body the level built, and the box standing for it. The box sits at the
    // body's shape, offset from its entity in the body's own frame.
    struct Drawn {
        entt::entity body{entt::null};
        entt::entity box{entt::null};
        glm::dvec2 offsetPx{0.0};
        glm::dvec2 sizePx{0.0};
        float depth{0.4f};
    };

    // A body a launcher threw, and the box and the sprite standing for it.
    struct ThrownBox {
        entt::entity body{entt::null};
        entt::entity box{entt::null};
        entt::entity quad{entt::null};
        glm::dvec3 emissive{0.0}; // the thrown .ent's, from launchers.json
    };

    // One of the level's sprites as a textured quad, and what it follows: the
    // crystal, static portal or no-portal zone it pictures, or else its node's
    // body. Scenery follows nothing.
    struct DrawnSprite {
        Sprites::Sprite sprite;
        entt::entity quad{entt::null};
        float z{0.0f};
        // C, the instance colour, as its node gives it (eth_color), and a timed
        // crystal's fade, which multiplies its alpha. What the ambient does to
        // them is syncLighting's.
        glm::vec4 colour{1.0f};
        float fade{1.0f};
        glm::dvec3 emissive{0.0}; // its node's eth_emissive
        std::string lightmap;     // its node's eth_lightmap, on disk; empty = none
        // Which lights reach it and how (Lighting::ReceiverMask): its node's
        // eth_static, eth_apply_light, eth_normal (on disk; empty = none), and its
        // eth_z, the original's unrounded depth, which is its lighting height.
        bool isStatic{false};
        bool applyLight{false};
        std::string normal;
        double lookZ{0.0};
        // Where its entity stands now, in the level's pixels: its node's position,
        // or its body's, or its patrolling zone's. What a light it owns follows.
        glm::dvec2 ownerPx{0.0};
        entt::entity body{entt::null};
        int crystal{-1};      // in goals.crystals
        int staticPortal{-1}; // in portals.statics
        int zone{-1};         // in portals.zones
    };

    void bindInput();
    void buildCamera(entt::registry& registry);
    void buildDrawables(entt::registry& registry);
    void buildSprites(entt::registry& registry);
    void buildHud(entt::registry& registry);

    // The menu: opened onto a screen, drawn from its buttons, and taken down
    // again. A level is unloaded first, so the two are never both in the
    // registry.
    void openMenu(entt::registry& registry, Screen screen);
    // The medal screen, over the level that was just finished: unlike every
    // other screen this KEEPS the level, which goes on running under it.
    void openFinished(entt::registry& registry);
    void layOutMenu();
    void buildMenu(entt::registry& registry);
    // A menu state becomes current: its clock from nothing, no touch followed.
    void beginMenuState();
    // The loading screen's quads placed for this tick, and its hold counted.
    void loadingTick(entt::registry& registry);
    // The main menu's touch, as Button::update reads it.
    void mainMenuInput(entt::registry& registry);
    // Chapter select's and the grid's touch, the same way, on their world quads.
    void menuQuadInput(entt::registry& registry);
    // The credits' touch and the strip's tick (CreditsScreenLayer::update).
    void creditsInput(entt::registry& registry, float fixedDelta);
    // The dashboard's touch, wheel and tick (ScoreDashboard::loop, DashboardLayer).
    void dashboardInput(entt::registry& registry);
    // openState(world, level), from the dashboard's start button.
    void openAchievement(entt::registry& registry, int world, int level);
    // Whether a menu state is up, instead of a level or over one.
    bool menuStateUp() const;
    // A picture of the original's that the menu states draw, resolved once; empty
    // when it cannot be read.
    const std::string& menuPicture(const std::string& file) const;
    // A font of the original's, read once for the run.
    void loadUiFont(const std::string& name);
    void unloadMenu(entt::registry& registry);
    // Just the drawables, which a rebuilt screen replaces but a click on a
    // level takes away for good.
    void unloadMenuDrawables(entt::registry& registry);
    void menuTick(entt::registry& registry, float fixedDelta);
    // One of the original's own menu images, which live beside its entities.
    std::string menuImage(const std::string& file) const;

    // The entities' particle systems: built with a level's art, carried per
    // FRAME, and taken away with the level.
    void buildEmitters(entt::registry& registry);
    void unloadEmitters(entt::registry& registry);
    void updateEmitters(entt::registry& registry, float deltaTime);
    // A number in [from, to) from the layer's OWN generator.
    double particleRandom(double from, double to);
    float readInput(entt::registry& registry);
    void syncDrawables(entt::registry& registry);
    void syncSprites(entt::registry& registry);
    void syncBoss(entt::registry& registry);
    // The carrancas' fireballs. Its own function rather than a block inside
    // syncBoss: they belong to a turret, not to the beholder, and every level
    // of chapter 2 that has one has no boss at all.
    void syncTurrets(entt::registry& registry);
    // Every sprite of the level coloured for the ambient light now: after
    // everything above has made, unmade and placed this tick's quads.
    void syncLighting(entt::registry& registry);
    // What a sprite is to the lights: whether it applies light and is static
    // (Lighting::ReceiverMask), its normal map on disk, and its lighting height
    // in the original's units. The default takes no light.
    struct Receiver {
        bool applyLight{false};
        bool isStatic{false};
        std::string normal;
        double z{0.0};
    };
    // A quad's light: its colour C as albedoColor, min(1, ambient + emissive) as
    // its 2D sprite's ambient, its lightmap as the overlay, its height, normal map
    // and light mask, and a mixed blend made premultiplied - or, on a level whose
    // lighting did not read, C alone on the plain unlit path. Each written only
    // when it changes. The one place a level sprite's colour is written.
    void tint(entt::registry& registry, entt::entity quad, const glm::vec4& colour, const glm::dvec3& emissive,
              const std::string& lightmap = {}, const Receiver& receiver = {}) const;
    // The level's lights and their halos, one per Lighting::Look::light, made with
    // the sprites and their particles.
    void buildLights(entt::registry& registry);
    // The level's lights, their halos, and the shot's light and halo, taken away.
    void unloadLights(entt::registry& registry);
    // Every light and halo placed at its owner, on or off as its owner is there
    // or not, in the colour its owner's particles give it now; and the shot's
    // made, placed and unmade with the shot. On the tick, after the sprites, so
    // a suite that only ticks sees them; and on the frame, after the particles,
    // which is when their count changes.
    void syncLights(entt::registry& registry);
    // The live share of the particles of the emitter at `emitter` in m_emitters
    // (Lighting::ParticleRatio): 1 when there is none.
    double particleRatioOf(int emitter) const;
    // Hands the held lightmaps back to the texture registry, when there is one.
    void releaseLightmaps(entt::registry& registry);
    void updateHud(entt::registry& registry);

    // The level at `index` in chapters.json, in place of whatever was there.
    // False, with LoadError, when it is refused; nothing of it is left then.
    bool loadLevel(entt::registry& registry, int index);
    // `keepLightmaps` for a retry, which draws the same level again at once.
    void unloadLevel(entt::registry& registry, bool keepLightmaps = false);
    // The level at `next`, or the chapter's end when it is -1.
    void goTo(entt::registry& registry, int next);
    // The exit reached: recorded, and on to the next level.
    void clearLevel(entt::registry& registry);

    float viewportAspect(const entt::registry& registry) const;
    void placeCamera(entt::registry& registry);

    entt::entity makeBox(entt::registry& registry, const char* tag, const glm::vec3& centre, const glm::vec3& size,
                         const glm::vec3& colour);
    void placeBox(entt::registry& registry, entt::entity box, const glm::dvec2& centrePx, const glm::dvec2& sizePx,
                  float z, float depth, float rotation) const;
    entt::entity makeSprite(entt::registry& registry, const char* tag, const std::string& texture, bool additive);
    void placeSprite(entt::registry& registry, entt::entity quad, const glm::dvec2& centrePx,
                     const glm::dvec2& sizePx, float z, float rotation) const;
    // An image's size, read once per file (Sprites::ImageSize); zero when it
    // cannot be read.
    glm::dvec2 imageSizePx(const std::string& path);
    // An image among the original's entities.
    std::string originalImage(const std::string& sprite) const;

    Paths m_paths;
    std::string m_startLevel;

    Chapters::Table m_chapters;
    int m_current{-1}; // the entry in m_chapters being shown
    bool m_chapterComplete{false};
    std::optional<Cleared> m_lastCleared;
    int m_deaths{0};

    Camera::Rules m_cameraRules;
    double m_viewHeightPx{0.0};
    float m_aspect{16.0f / 9.0f}; // the window's shape as of the last tick
    Camera::Follow m_follow;

    Game::Data m_data;
    int m_dataIndex{-1}; // whose level m_data holds, so a retry reuses it
    // The level's lighting, read with m_data and reused with it.
    Lighting::Scene m_look;
    bool m_lit{false}; // m_look read, so sprites are coloured for the ambient
    std::string m_lightingError;
    glm::dvec3 m_ambient{1.0};
    std::vector<std::string> m_heldLightmaps;
    std::size_t m_lightmapsHandedBack{0};
    Game::Level m_level;
    bool m_loaded{false};
    std::string m_loadError;
    glm::dvec2 m_boundsPx{0.0}; // the level_bounds marker: the level runs from (0, 0) to here

    entt::entity m_camera{entt::null};
    entt::entity m_light{entt::null};
    std::vector<Drawn> m_bodies;
    entt::entity m_player{entt::null};
    std::vector<entt::entity> m_buttons;  // one per Puzzle button, in its order
    std::vector<entt::entity> m_crystals; // one per crystal, null once collected
    entt::entity m_exit{entt::null};
    std::vector<entt::entity> m_portals;  // one per placed portal
    std::vector<entt::entity> m_statics;  // one per static portal, null once spent
    std::vector<entt::entity> m_zones;    // one per no-portal zone
    std::vector<entt::entity> m_hazards;  // one per hazard, at the box that kills
    std::vector<ThrownBox> m_thrown;      // one per thrown body not yet taken back
    entt::entity m_shot{entt::null};      // the portal shot in flight, when there is one

    std::vector<DrawnSprite> m_sprites; // the level's art, in drawing order
    bool m_artReady{false};             // the level's art was read, and is drawn
    std::string m_artError;
    bool m_showBoxes{false};
    int m_playerSlot{0}; // the drawing slot the player takes among the sprites
    std::map<std::string, glm::dvec2> m_imageSizes;
    Art::Rules m_artRules;                  // the port's art.json
    std::vector<entt::entity> m_portalQuads; // one per placed portal, when its image is there
    entt::entity m_shotQuad{entt::null};    // the shot in flight, when its image is there
    entt::entity m_playerQuad{entt::null};  // the player, when its image is there
    double m_playerZ{0.0};                  // the level's player marker's eth_z: the player's lighting height

    // One of the level's <Light>s: the Light2DComponent standing for it, its halo's
    // quad when it has one, and the sprite of the entity that owns it. Every light
    // in the 128 levels has an owner that draws a sprite (72 of 72, counted in
    // step 49), and a light whose owner draws none stands at its node.
    struct PlacedLight {
        std::string node;
        Lighting::Light light;
        bool ownerStatic{false};
        double ownerZ{0.0};
        int sprite{-1};        // its owner's picture in m_sprites, or -1
        int emitter{-1};       // its owner's first <ParticleSystem> in m_emitters, when one was built
        glm::dvec2 atPx{0.0};  // its node's position, for an owner with no picture
        float haloZ{0.0f};     // just in front of its owner's picture, behind its particles
        entt::entity entity{entt::null};
        entt::entity halo{entt::null};
    };
    std::vector<PlacedLight> m_placedLights;
    entt::entity m_shotLight{entt::null}; // the shot's own light while it flies
    entt::entity m_shotHalo{entt::null};  // and its halo, when its image is there
    bool m_lightMasksOff{false};          // ForceLightMasksOff
    float m_direction{0.0f};                // this tick's walk: -1 left, 1 right, 0 standing
    bool m_facingRight{false};              // which way the player last walked
    // Chapter 1's boss: its reach as a box, beholder.ent's sheet when the image
    // is there, and its spikes. The beholder is drawn at its adder's z_index
    // among the art, and a spike at the -4 the original adds it at.
    entt::entity m_beholderBox{entt::null};
    entt::entity m_beholderQuad{entt::null};
    glm::dvec2 m_beholderScale{1.0}; // its pulse as last set, which it keeps while it throws rocks
    glm::vec4 m_beholderColour{1.0f}; // C: (1, hp / max, hp / max), reddening as it is hurt
    std::vector<entt::entity> m_spikes; // one per spike in flight
    float m_beholderZ{0.5f};
    float m_spikeZ{0.5f};

    // The carrancas' fireballs, one quad per fireball in flight (Turrets.hpp).
    //
    // Always boxes: fireball.ent carries no <Sprite> at all - what the original
    // shows is its ParticleSystem and its Light - so there is no image to draw
    // one with, and the box is the picture rather than a stand-in for one.
    std::vector<entt::entity> m_fireballs;

    // One live particle of an emitter's, and the quad standing for it.
    struct Particle {
        glm::dvec2 atPx{0.0};
        glm::dvec2 velocityPx{0.0}; // the original's `dir`, per frame-speed unit
        double angle = 0.0;         // degrees, clockwise on the screen
        double angleDir = 0.0;
        double size = 0.0;
        double lifeMs = 0.0; // its own, spread from the system's
        double elapsedMs = 0.0;
        int repeats = 0;
        int frame = 0;
        bool released = false;
        entt::entity quad{entt::null};
    };

    // One <ParticleSystem> of one placement: the system as the .ent states it,
    // where it sits, and its pool.
    //
    // PER FRAME, and never on the tick. Nothing here touches Game::Level, the
    // simulation's clock or the state hash - a particle is a picture, and the
    // port's determinism is the thing most easily broken by pretending
    // otherwise. The generator is the layer's own for the same reason: the
    // engine's is one process-global stream that every other drawer shares.
    struct Emitter {
        Particles::System system;
        glm::dvec2 atPx{0.0}; // the placement it decorates, which does not move
        float z = 0.0f;       // in front of that placement's own art
        glm::dvec2 cellPx{0.0};
        std::string image;
        int crystal = -1; // when it decorates one: it stops with the crystal
        int sprite = -1;  // the sprite in m_sprites whose entity it belongs to
        int slot = 0;     // which of that entity's <ParticleSystem>s it is, from 0
        double angleDeg = 0.0; // its entity's angle, which every particle starts turned by
        // ETHEntity::KillParticleSystem: no particle is released or renewed, and each
        // lives out the life it has.
        bool killed = false;
        std::vector<Particle> particles;
    };
    // Every particle system of an entity of the original's, at a place: false when
    // the .ent carries none or cannot be read.
    bool addEntityEmitters(const std::string& entity, const glm::dvec2& atPx, float z, double angleDeg);

    // No emitter draws more than this, whatever its .ent asks for. Chapter 1
    // places 189 of them and the largest asks for 32, so this is a guard
    // against a later chapter rather than a limit anything here reaches.
    static constexpr int kMaxParticles = 64;

    std::vector<Emitter> m_emitters;
    // Seeded, so a run looks the same twice and a screenshot can be compared.
    std::mt19937 m_particleRandom{20260912u};

    // ---- sound: the original's AudioManager, carried as data ----------------
    //
    // PER FRAME, exactly like the particles above and for the same reason. The
    // TICK only latches what happened - a counter that went up, a flag that
    // turned over - and OnUpdate plays it. Nothing below reaches Game::Level,
    // the simulation's clock or the state hash, so a level plays identically
    // with no audio device at all. That is not a nicety: it is how every suite
    // and every headless render runs, and what keeps a replay a replay.
    void loadSounds();
    // Remember that `event` happened on this tick. Named for sounds.json's
    // events table; one the file leaves silent costs nothing here. A door
    // brings its own stride: its hook's speed is 3000 / it.
    void latch(const char* event, double doorStrideMs = 0.0);
    // What the simulation did this tick, against what it looked like last.
    void latchSimSounds();
    void playLatched(entt::registry& registry, float deltaTime);
    // The track the game should be playing now - the menu's, the level's, or
    // its boss's - started and stopped as that changes.
    void updateMusic(entt::registry& registry);
    void stopMusic(entt::registry& registry);
    double soundRandom(double from, double to);
    // A hook's sample speed as the engine's pitch: a number it states, a draw
    // from its range, or the opening door's own 3000 / stride.
    float pitchFor(const Sounds::Hook& hook, double doorStrideMs);

    // What the simulation looked like on the previous tick, so this one can
    // tell what changed. Counters and one-way flags only, and nothing here is
    // ever read back into the simulation.
    struct Watch {
        bool valid = false;
        int portalsUsed = 0;
        int traversals = 0;
        int shotsFired = 0;
        int shotsFailed = 0;
        int reflections = 0;
        int crystalsCollected = 0;
        int crystalsExpired = 0;
        int staticsLive = 0;
        int wallsBroken = 0;
        int stonesThrown = 0;
        int fireballsSpat = 0;
        int bossHits = 0;
        int bossVolleys = 0;
        int bossRocksBroken = 0;
        int bossFrame = 0;
        bool bossGone = false;
        bool bossButtonRaised = false;
        // char rather than bool: std::vector<bool> is not a container of bools,
        // and these are only ever compared with the last tick's.
        std::vector<char> doorsOpening;
        std::vector<char> liftsForward;
    };

    // One event the tick saw, with what playing it needs to know.
    struct Latched {
        std::string event;
        double doorStrideMs = 0.0;
    };

    // ---- the drawing diagnostic ---------------------------------------------
    //
    // What the game BELIEVES it is drawing, written down as it changes, so that
    // a sprite which vanishes on screen can be told from one the game stopped
    // drawing. If the picture loses a sprite and nothing is logged for it, the
    // game still thinks it is drawing it - and the fault is below this layer,
    // in the renderer or the GPU. That one distinction is what this is for.
    void reportSprites(entt::registry& registry);

    bool m_dumpRequested{false};    // G was pressed on the tick
    std::vector<char> m_onScreenLast; // one per sprite, as of the last frame
    bool m_reportedOnce{false};     // so the first frame states the whole set

    Sounds::Rules m_soundRules;
    std::string m_soundsError;      // why there is no sound, when there is none
    std::vector<Latched> m_latched; // this tick's events, in the order they happened
    Watch m_watch;
    double m_soundClockMs = 0.0;               // the frames' own clock, for the shared timers
    std::map<std::string, double> m_timerAtMs; // when each shared timer last let one through
    // Seeded and the layer's OWN, for the reason the particles' generator is:
    // the engine's is one process-global stream every other drawer shares, and
    // a run has to sound the same twice.
    std::mt19937 m_soundRandom{20260913u};
    std::string m_track; // which of sounds.json's tracks is playing, or empty
    // A looping voice is never "finished", so ReapFinishedVoices leaves it
    // alone for ever: the layer holds this and stops it itself, or menu to
    // level to menu leaves two tracks playing over each other.
    Supersonic::AudioEngine::VoiceId m_musicVoice{Supersonic::AudioEngine::kInvalidVoice};

    // The menu. Screen::None while a level is played.
    Screen m_screen{Screen::None};

    // ---- the menu states ---------------------------------------------------
    MainMenu::Rules m_mainMenuRules;
    Loading::Rules m_loadingRules;
    // The state's clock: `fresh` until its first tick, which is its zero.
    struct MenuClock {
        bool fresh{true};
        int ticks{0};
        double ms{0.0};
    };
    MenuClock m_menuClock;
    double m_layerClockMs{0.0};
    // The one touch a menu follows (Button::update): where it went down and how
    // far it has travelled; which main-menu buttons it went down inside and is
    // still inside; and which of chapter select's or the grid's quads.
    struct MenuTouch {
        MenuState::Touch touch;
        unsigned mainDown{0u};
        unsigned mainHeld{0u};
        std::optional<MenuButton> downOn;
        bool heldInside{false};
    };
    MenuTouch m_menuTouch;
    // What a release - or the loading screen's hold, or the back key - asked for,
    // done on the next tick: the original's release frame is drawn in the old
    // state, and the new one's black from the frame after (ui3 spec 0.4).
    struct PendingMenu {
        enum class Kind { None, MainButton, Button, Screen, Achievement };
        Kind kind{Kind::None};
        MainMenu::Button main{MainMenu::Button::Play};
        MenuButton button;
        Screen screen{Screen::None};
        int world{-1}; // Achievement: openState's
        int level{-1};
    };
    PendingMenu m_pendingMenu;
    // The main menu's music switch, on the state's clock: when its entrance began
    // and when it was last dismissed (SoundPanelLayer::manageMusicSwitch).
    double m_mainMusicAddedMs{0.0};
    double m_mainMusicDismissedMs{-1.0};
    // The pictures the menu states draw through the overlay, by the name ui.json
    // gives them, each resolved to its hd twin once.
    std::map<std::string, std::string> m_menuPictures;
    // Credits: its rules, the strip, and the one touch it follows.
    Credits::Rules m_creditsRules;
    Credits::Scroll m_credits;
    struct CreditsTouch {
        bool down{false};
        glm::dvec2 lastAt{0.0};
        bool downOnBack{false};
        bool backHeld{false};
    };
    CreditsTouch m_creditsTouch;
    // The dashboard: its rules, the rows it opened with, and its scroll and buttons.
    Dashboard::Rules m_dashboardRules;
    Dashboard::Board m_dashboardBoard;
    Dashboard::State m_dashboardState;
    Locking::Rules m_lockingRules;
    // The original's achievements, read once at attach from Paths::achievements.
    Achievements::Content m_achievements;
    bool m_achievementsLoaded{false};
    std::string m_achievementsError;
    // The loading screen's scene, in the level's space.
    entt::entity m_loadingBg{entt::null};
    entt::entity m_loadingCharacter{entt::null};
    entt::entity m_loadingPortal{entt::null};
    entt::entity m_loadingHalo{entt::null};
    // How many emitters the portal's own are, at the front of m_emitters.
    std::size_t m_loadingPortalEmitters{0};
    bool m_loadingVanished{false};
    // black_halo.bmp as the multiply it is drawn with: black, at an alpha of one
    // less its texel, written once beside the prisms; empty when it cannot be,
    // or when Paths::prisms names no directory.
    std::string loadingHaloImage();
    int m_menuWorld{0}; // whose levels the grid shows
    int m_menuPage{0};  // which page of that grid
    std::vector<MenuButton> m_menuButtons;
    std::vector<entt::entity> m_menuQuads;  // one per button, in its order
    std::vector<entt::entity> m_menuLabels; // one per button, null but for a level's number
    // One per button too, and null for all but a LEVEL THE PLAYER HAS CLEARED:
    // the original draws a medal on a level button only where getScore is not
    // zero, so an unfinished level carries no medal rather than a bronze one.
    std::vector<entt::entity> m_menuMedals;
    entt::entity m_menuBg{entt::null};      // the screen's background
    entt::entity m_menuTitle{entt::null};   // the game's title, on the main screen

    // ---- the finished and lost screens ------------------------------------
    //
    // Drawn through the screen overlay by EmitHud, laid out by sim/LevelEnd.hpp;
    // nothing of them is in the registry. What the layer holds is what a
    // picture cannot be a pure function of: the screen's clock, what the level
    // ended with, and the ScoreCounters, which step on the tick.
    LevelEnd::Rules m_levelEndRules;
    struct EndScreen {
        double clockMs{0.0}; // UI frame time since the screen became current
        LevelEnd::Play play;
        LevelEnd::Counter portals;  // ScoreCounter(0, portalsUsed, 100)
        LevelEnd::Counter crystals; // and the crystals collected
    };
    EndScreen m_end;
    // The walk pads' alpha byte from the door or the death on, decayed a tick at a
    // time; and whether clear-portals was up to be dismissed with restart and pause.
    int m_padEndByte{0};
    bool m_clearShownAtEnd{false};
    // Each end-screen picture's file resolved once per level, and its size in
    // texels; empty and zero for one that cannot be read, which is not drawn.
    std::map<std::string, std::string> m_endImages;
    std::map<std::string, glm::ivec2> m_endTexels;
    // The screen up over the level as pieces, `ms` into it.
    std::vector<LevelEnd::Piece> endPieces(const glm::dvec2& viewUnits, double ms) const;
    // One tick of the screen: its clock, its counters and its buttons. True when
    // a button was pressed, which may have taken the level away.
    bool endScreenTick(entt::registry& registry, float fixedDelta);
    // What the HUD keeps once a level has ended: the pads' last byte, and
    // whether clear-portals goes with restart and pause.
    void endHud();

    // THE BEAT BETWEEN GOING IN AND BEING SCORED.
    //
    // GameStateController::checkGameEnd does not put the medal up when the exit
    // is reached. In order it runs levelFinishedEffect - the red_suck_effect at
    // the door, the red_sparkles on the character, and playFinalDoorSound - then
    // Hide()s the character and zeroes its velocity, then writes the score; and
    // only once gameEndElapsedTime passes gameWonDelay does it raise the
    // levelFinishedLayer and play playVictorySound.
    //
    // gameWonDelay is 1400 ms (ui.json's level_end.beats, decoded): the
    // constructor writes 1400 to gameLostDelay and copies the same register into
    // gameWonDelay. A level may override it - the constructor's later arm reads a
    // `delay` entity's `time` - but NO LEVEL IN THE GAME PLACES ONE, checked
    // across all 128, so the override is recorded here and not built.
    //
    // This port showed the medal and played both sounds on the tick the exit
    // reported, with the character still standing in the doorway walking on the
    // spot. That is what the owner saw.
    bool m_finishing = false;      // gone into the door, not yet scored
    double m_finishClockMs = 0.0;  // how long since

    // AND THE SAME BEAT BEFORE THE LOST SCREEN, which this port had none of:
    // death was an instant retry, so a player who walked into level5's
    // death_area was simply back at the spawn with no screen and no sound, and
    // one who walked off a ledge was not killed at all.
    //
    // checkGameEnd counts gameEndElapsedTime against gameLostDelay and only past
    // it raises the levelLostLayer and plays playDeathSound. gameLostDelay is
    // the register gameWonDelay is copied FROM, so the two beats are the same
    // 1400 ms - ui.json carries both, with the one decode.
    bool m_dying = false;         // killed, the lost screen not yet up
    double m_dyingClockMs = 0.0;  // how long since

    // Whether the platform the dark dragon's death adds has been given its
    // picture yet. It has no node, so buildSprites cannot have made one for it:
    // syncSprites borrows the template sibling's when the body appears, once.
    bool m_platformDrawn = false;
    // And whether it has been given its BOX, which is a separate question: the
    // picture above is gated on there being art at all, and the box view is the
    // mode used when there is none.
    bool m_platformBoxed = false;

    // Puts the lost screen up, over the level that is still loaded behind it.
    // Follows openFinished and NOT openMenu: openMenu unloads the level and
    // sets m_current to -1, after which the restart button's own guard
    // (`if (m_current < 0) return false`) would make it silently do nothing.
    void openDead(entt::registry& registry);

    // WHAT THE PLAYER HAS EARNED, across runs.
    //
    // The level grid draws a small medal on every level already finished, and
    // the port kept no score at all - so there was nothing to draw and every
    // button was bare. Memory-only unless Paths::saveDir says otherwise, which
    // is what keeps eighteen suites off the filesystem.
    Scores::Store m_scores;

    // The engine's own text lines: what a refused level, a chapter's end and the
    // menu screens say. Not the original's, which draws no text over a level.
    struct HudText {
        entt::entity status{entt::null};
        entt::entity result{entt::null};
        entt::entity controls{entt::null};
    };
    HudText m_hud;

    // ---- the on-screen controls ---------------------------------------------
    //
    // What the original draws over a level, placed and timed by ui.json through
    // sim/Hud.hpp: this layer only says where those functions put things.
    //
    // DRAWN THROUGH THE ENGINE'S SCREEN OVERLAY (core/ScreenOverlay.hpp), not as
    // quads in the level. The original blended its HUD straight onto display
    // values; the scene target then blended in linear light and was tone-mapped
    // after, which capped white at 186 and made a translucent button's contrast
    // depend on what was behind it (step 39). The overlay is drawn after the
    // composite, in screen fractions, so it needs no camera, no interpolation
    // and no z. The level has held display values too since step 44; the HUD
    // stays here, after the composite, where no scene setting reaches it
    // (whether the original's 5/6/5 should is the lighting design's step G6).
    //
    // HIT-TESTED IN VIEW SPACE, straight from the pointer's place in the
    // viewport, so a tap on a control does not depend on the camera at all.
    struct ControlButton {
        Control kind{Control::Left};
        std::string image; // the picture, empty when it could not be read
        Hud::Rect rect;    // on the view, as last laid out
        bool shown{false};
        double alpha{0.0}; // display-space, as last laid out
    };
    std::vector<ControlButton> m_controls;

    Hud::Rules m_hudRules;
    bool m_hudReady{false};
    std::string m_hudError;
    double m_levelAgeMs{0.0};

    // The tutorial's ring, drawn on each pad's corner, under the pads; empty
    // outside the tutorial.
    std::string m_ringImage;

    // The current-score plaque and its medal, where a medal is recorded; both
    // empty on a fresh save, as the original adds nothing where getScore is 0.
    std::string m_plaqueImage;
    std::string m_medalImage;

    // "Part N", a quad a letter over the font's own pages, above the black.
    std::string m_captionText;
    Supersonic::BitmapFont m_captionFont;
    bool m_captionFontTried{false};

    // The no-portal sign: taken out of the level's sprites, because the level
    // places it off the level and its script pins it to the camera's corner.
    struct NoPortalSign {
        bool present{false};
        std::string image;         // the hd twin where there is one
        glm::dvec2 sizeUnits{0.0}; // the entity's size, as its .ent draws it
        Hud::Follow follow;        // its centre, in the level's units
        Hud::Rect onView;          // as the last tick left it
        double heldMs{0.0};        // frame time owed to the follow (Hud::HandOver)
    };
    NoPortalSign m_sign;

    // ---- the pause ------------------------------------------------------------
    Pause::Rules m_pauseRules;
    struct PauseScreen {
        bool open{false};
        double clockMs{0.0}; // UI frame time since the tap, on the tick
        Pause::Level level;
        double musicAddedMs{0.0};
        double musicDismissedMs{-1.0};
    };
    PauseScreen m_pause;

    // GAME TIME STOPPED, by a pause or a popup: the world as it stood when it
    // stopped - every simulated body's transform and state, put back after each
    // physics step the app runs regardless - and the flipbooks that were playing,
    // stopped for as long as it is.
    struct Frozen {
        struct Held {
            entt::entity entity{entt::null};
            Supersonic::TransformComponent transform;
            Supersonic::RigidBodyComponent body;
        };
        std::vector<Held> held;
        std::vector<entt::entity> stoppedFlipbooks;
    };
    Frozen m_frozen;
    void freezeWorld(entt::registry& registry);
    void thawWorld(entt::registry& registry);

    // ---- the popups -------------------------------------------------------------
    Popup::Rules m_popupRules;
    struct PopupScreen {
        bool open{false};
        const Popup::Class* cls{nullptr};
        Popup::Open state;
        // Whether the level still owes the half of its tick that follows its input
        // (BeforeStep): true for a popup a tap raised mid-tick, as a pause is; false
        // for one raised as the level loads, whose first tick has not begun.
        bool stepOnResume{false};
    };
    PopupScreen m_popup;
    // Each popup picture's file resolved once per level, the hd twin where one
    // exists; empty for one that cannot be read, which is then not drawn.
    std::map<std::string, std::string> m_popupImages;
    // The level's help blocks: each entity's place and collision box, in units.
    struct HelpBlock {
        glm::dvec2 atUnits{0.0};
        glm::dvec2 boxUnits{0.0};
    };
    std::vector<HelpBlock> m_helpBlocks;
    // A touch that went down on a help block: which, where in the window's pixels,
    // and the furthest it has moved since (HelpBlockController's touchMoveLength).
    struct HelpTouch {
        bool armed{false};
        int block{-1};
        glm::dvec2 downPx{0.0};
        double maxMovePx{0.0};
    };
    HelpTouch m_helpTouch;
    // The plaque's dismissal, on GameLayer's clock (the level's age), once the
    // frame clock has passed its dismissAfterMs; negative before.
    double m_plaqueDismissAgeMs{-1.0};
    // Its alpha as GameLayer's last update wrote it: a UISprite's colour is only
    // written when its layer is updated, so under a stop it holds, and under a
    // popup raised as the level loads it is still the nothing it was built with.
    double m_plaqueAlpha{0.0};
    // The pointer this tick as the popups read it - a real touch or a DEV one.
    struct Touch {
        bool pressed{false};
        bool released{false};
        bool held{false};
        bool over{false};         // on the game at all
        glm::dvec2 atView{0.0};   // design units on the view
        glm::dvec2 atPx{0.0};     // the window's pixels, for the move a touch makes
    };
    Touch touchThisTick(const entt::registry& registry);
    // A popup of `cls` over the level, game time stopped.
    void openPopup(entt::registry& registry, const Popup::Class& cls, bool stepOnResume);
    // One tick under a popup. True when it resumed the level on this tick and the
    // level owes the rest of its tick.
    bool popupTick(entt::registry& registry, float fixedDelta);
    // The help blocks the level places, read as it loads.
    void findHelpBlocks();
    // A tap on a help block, read with the rest of the level's input: true when it
    // took the touch, which then fires no portal.
    bool helpBlockInput(entt::registry& registry);
    // A file among the original's assets, the hd twin beside it where one exists.
    std::string originalAsset(const std::string& relative) const;
    // Whether the frame clock has passed the plaque's dismissal, checked on every
    // tick a level is loaded, stopped or not; and, where GameLayer is updated
    // (`updated`), the plaque's alpha.
    void tickPlaqueDismissal(bool updated);
    // The switches: GlobalSoundSwitch sets the global volume, GlobalMusicSwitch
    // the music. Held for the session only - the original saves the volume
    // (GlobalVolumeManager::saveVolume), which the port does not yet.
    bool m_soundOn{true};
    bool m_musicOn{true};
    // Game time stood still this level, for LevelFrameMs.
    double m_stoppedMs{0.0};
    // Each pause picture's file resolved to the hd art once per level; empty
    // for one that cannot be read, which is then not drawn.
    std::map<std::string, std::string> m_pauseImages;
    // The fonts the pause and the end screens write in, where they are not the
    // caption's own; read once for the run.
    std::map<std::string, Supersonic::BitmapFont> m_uiFonts;
    const Supersonic::BitmapFont* uiFont(const std::string& name) const;

    // DEV ONLY: the scheduled presses and holds, and the layer's own tick count.
    std::vector<std::pair<int, DevPress>> m_devPresses;
    struct DevHold {
        int from{0};
        int to{0};
        float direction{0.0f};
    };
    std::vector<DevHold> m_devHolds;
    struct DevTap {
        int tick{0};
        int releaseTick{0}; // held from its tick until this one, which releases it
        bool onHelpBlock{false};
        glm::dvec2 viewFraction{0.0};
        bool pressed{false};
        // A drag: where it goes, reached on the release tick, from the tick it went down.
        std::optional<glm::dvec2> toFraction;
        int pressedTick{0};
    };
    std::vector<DevTap> m_devTaps;
    int m_ticks{0};

    void buildControls();
    void unloadControls();

    // The pause. Opened only where the pause control could be pressed; closed
    // as UILayer::hide(true) closes it, at once and forgetting its entrance.
    bool pauseAllowed() const;
    void openPause(entt::registry& registry);
    void closePause(entt::registry& registry);
    // Puts every body back where the tap left it, undoing the physics step the
    // app runs before every tick whether or not the game's time is stopped.
    void holdWorld(entt::registry& registry);
    // One tick under the pause. True when it resumed the level on this tick.
    bool pauseTick(entt::registry& registry, float fixedDelta);
    // The rest of a level's tick once its input is read: what comes before the
    // next physics step, the camera, the controls and the drawables.
    void stepLevel(entt::registry& registry, float direction, float fixedDelta);
    // The walk the keys alone ask for, with no pointer.
    float keyDirection() const;
    // Whether a scheduled DEV press of this kind is due, taking it if so.
    bool devPressDue(DevPress press);
    // The control rectangles for this view and this age, and whether each is
    // shown. Pure layout.
    void layOutControls();
    // The no-portal sign a tick on, against the camera as this tick left it.
    void tickNoPortalSign(double dtMs);
    // Which control the pointer is on, or nullptr. Asked once a tick, before a
    // shot is fired, because a tap that works a control must not also open a
    // portal under it.
    const ControlButton* controlUnderPointer(const entt::registry& registry) const;
    // Whether the level being played is the one whose pads pulse long and ring.
    bool tutorialPads() const;
};

} // namespace MagicPortals
