#pragma once

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

#include "sim/Camera.hpp"
#include "sim/Chapters.hpp"
#include "sim/Game.hpp"
#include "sim/Art.hpp"
#include "sim/Particles.hpp"
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
    static constexpr const char* kBack = "mp.back";          // Escape: out to the menu, and back through it
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
        std::string data = MAGICPORTALS_DATA_DIR;
        std::string portData = MAGICPORTALS_PORT_DATA_DIR;
        std::filesystem::path prisms; // where LevelBuilder may write
        // What res:// stands for: the converter writes the levels' art beside
        // the levels, so the directory above them (Sprites.hpp).
        std::string art = MAGICPORTALS_LEVELS_DIR "/..";
        // The original's extracted assets, whose entities/ holds the images of
        // what no level places (Art.hpp).
        std::string original = MAGICPORTALS_ORIGINAL_DIR;

        // WHERE THE PLAYER'S MEDALS ARE KEPT, and empty by default - which
        // means this layer never touches the filesystem.
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
    // The original's own three (MainMenu, WorldSelector, LevelSelector), less
    // what needs state the port does not keep: no score, so no locking, no
    // page counter, no swipe. What is drawn is its art, where its own
    // normalized positions put it.
    // `Finished` is the odd one: it sits OVER the level it finished, which
    // stays loaded and drawn but stops ticking, so it is placed against the
    // camera's view rather than against the menu's own box.
    enum class Screen { None, Main, Worlds, Levels, Finished };

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

    // Whether B has the bodies' boxes shown over the art.
    bool ShowingBoxes() const { return m_showBoxes; }

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
    };

    // One of the level's sprites as a textured quad, and what it follows: the
    // crystal, static portal or no-portal zone it pictures, or else its node's
    // body. Scenery follows nothing.
    struct DrawnSprite {
        Sprites::Sprite sprite;
        entt::entity quad{entt::null};
        float z{0.0f};
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
    // other screen this KEEPS the level, which simply stops ticking.
    void openFinished(entt::registry& registry);
    void layOutMenu();
    void buildMenu(entt::registry& registry);
    void unloadMenu(entt::registry& registry);
    // Just the drawables, which a rebuilt screen replaces but a click on a
    // level takes away for good.
    void unloadMenuDrawables(entt::registry& registry);
    void menuTick(entt::registry& registry);
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
    void updateHud(entt::registry& registry);

    // The level at `index` in chapters.json, in place of whatever was there.
    // False, with LoadError, when it is refused; nothing of it is left then.
    bool loadLevel(entt::registry& registry, int index);
    void unloadLevel(entt::registry& registry);
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
    float m_direction{0.0f};                // this tick's walk: -1 left, 1 right, 0 standing
    bool m_facingRight{false};              // which way the player last walked
    // Chapter 1's boss: its reach as a box, beholder.ent's sheet when the image
    // is there, and its spikes. The beholder is drawn at its adder's z_index
    // among the art, and a spike at the -4 the original adds it at.
    entt::entity m_beholderBox{entt::null};
    entt::entity m_beholderQuad{entt::null};
    glm::dvec2 m_beholderScale{1.0}; // its pulse as last set, which it keeps while it throws rocks
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
        std::vector<Particle> particles;
    };

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

    // ---- the medal screen ---------------------------------------------------
    //
    // LevelFinishedLayer draws a good deal more than a medal, and the port drew
    // only the medal. Decoded from its constructor and its draw (bytes
    // 271777..273843 and 274980..276253): a dimming veil over the frozen level,
    // the "level finished" banner, the plaque naming the portals spent, the
    // golden-score plaque when the play earned one, and a crystal beside the
    // medal when the level had any.
    //
    // One vector rather than a member each: they differ only in image and
    // placement, and nothing addresses an individual one.
    struct Decoration {
        entt::entity quad{entt::null};

        // Normalized ON THE CAMERA'S VIEW, because that is how the original
        // places them - against GetScreenSize - and the medal screen sits over
        // the level rather than in the menu's own box.
        glm::dvec2 atView{0.0};

        // HOW IT IS SIZED, stated rather than inferred from a zero.
        //
        // Stretched: sizeView is the size, as a fraction of the view. Only the
        // veil wants this - it is a gradient strip the original pulls one and a
        // half screens wide, and sizing it from its own aspect would draw a
        // hairline.
        //
        // ByHeight: heightView is its height as a fraction of the view and the
        // width follows the IMAGE'S OWN aspect. Everything else wants this, for
        // the reason the chapter icons needed it: an 84x128 image drawn square
        // is an image nobody authored.
        enum class Sizing { Stretched, ByHeight };
        Sizing sizing{Sizing::ByHeight};
        glm::dvec2 sizeView{0.0}; // Stretched
        double heightView = 0.0;  // ByHeight
        std::string image;        // resolved path, for asking its aspect

        // A pixel offset from the medal, applied after atView. The crystal is
        // placed at medalPos + (-30, 48) in the original's own pixels rather
        // than at a fraction of the screen, and expressing that as a fraction
        // would be a different position at a different window shape.
        //
        // No y flip: Units::ToWorld takes the remake's pixels with +y DOWN and
        // flips once inside, so the original's screen-space offsets carry over
        // verbatim. 48 is 48 further DOWN, as it is in the original.
        glm::dvec2 offsetPx{0.0};

        // WHERE ON THE SPRITE atView lands, as a fraction of it: (0.5, 0.5) is
        // its centre, which is what almost everything here uses.
        //
        // The original's addSprite takes this as the sprite's origin, and the
        // golden-score plaque is the one that does not centre: it is placed at
        // vector2(0.23, 0.5) of the screen with an origin of vector2(0.5, 0.33),
        // so a third of the way down rather than half. Drawing it centred put it
        // visibly high, which is the "tiny plaque floating" in the first
        // screenshot of this screen.
        glm::dvec2 pivot{0.5, 0.5};

        float z = 0.0f;
    };
    std::vector<Decoration> m_menuDecor;

    // The portals-spent counter the medal is computed FROM.
    //
    // The original builds ScoreCounter(0, numPortals, 100): it starts at zero
    // and steps ONE toward the play's portal count every 100 ms, and its draw
    // recomputes the medal from getCurrent() on every frame. So the medal
    // climbs bronze to silver to gold as the number rises, rather than being
    // stamped at the end. That is a behaviour and not decoration - the port
    // showing the final medal immediately was wrong in a way no screenshot
    // would have revealed.
    static constexpr double kCounterStrideMs = 100.0;
    int m_counterShown = 0;        // where the count has got to
    double m_counterClockMs = 0.0; // time owed to the next step
    int m_medalDrawn = 0;          // which medal the quad currently wears

    // THE BEAT BETWEEN GOING IN AND BEING SCORED.
    //
    // GameStateController::checkGameEnd does not put the medal up when the exit
    // is reached. In order it runs levelFinishedEffect - the red_suck_effect at
    // the door, the red_sparkles on the character, and playFinalDoorSound - then
    // Hide()s the character and zeroes its velocity, then writes the score; and
    // only once gameEndElapsedTime passes gameWonDelay does it raise the
    // levelFinishedLayer and play playVictorySound.
    //
    // gameWonDelay is 1400 ms: the constructor writes 1400 to gameLostDelay and
    // copies the same register into gameWonDelay (bytes 125310.., instructions
    // 24-29). A level may override it - the constructor's later arm reads a
    // `delay` entity's `time` - but NO LEVEL IN THE GAME PLACES ONE, checked
    // across all 128, so the override is recorded here and not built.
    //
    // This port showed the medal and played both sounds on the tick the exit
    // reported, with the character still standing in the doorway walking on the
    // spot. That is what the owner saw.
    static constexpr double kFinishDelayMs = 1400.0;
    bool m_finishing = false;      // gone into the door, not yet scored
    double m_finishClockMs = 0.0;  // how long since

    // The medal the counter's CURRENT value earns, by the same computeScore the
    // final one uses. Zero when there is nothing to show.
    int MedalShown() const;

    // WHAT THE PLAYER HAS EARNED, across runs.
    //
    // The level grid draws a small medal on every level already finished, and
    // the port kept no score at all - so there was nothing to draw and every
    // button was bare. Memory-only unless Paths::saveDir says otherwise, which
    // is what keeps eighteen suites off the filesystem.
    Scores::Store m_scores;

    struct Hud {
        entt::entity status{entt::null};
        entt::entity result{entt::null};
        entt::entity controls{entt::null};
    };
    Hud m_hud;

    // ---- the on-screen controls ---------------------------------------------
    //
    // What the original puts on a phone, and what the owner asked for: a walk
    // arrow in each bottom corner, and reset and menu together at the top right.
    // Its own screenshot is the reference - arrow_left.png and arrow_right.png
    // are pale discs cropped by the screen's edge, restart_level_button.png is
    // the circular arrows, and main_menu_shortcut.png is the pause bars. The
    // file called resume_button.png is a PLAY triangle and is not this pair's,
    // whatever its name suggests.
    //
    // DRAWN AS WORLD QUADS PINNED TO THE VIEW, not as UIImageComponent. That
    // component exists and anchors to a corner by itself, but it wants an
    // ImTextureID from the renderer's UI image service rather than a path, and
    // nothing in this layer has ever used that road. These follow the medal
    // screen's way instead - a quad placed every tick at
    // m_follow.centrePx + (fraction - 0.5) * ViewPx() - which costs one line of
    // arithmetic and puts the buttons in the SAME space ScreenToLevelPx already
    // answers in, which is the space the portal-shot check has to share.
    enum class Control { Left, Right, Reset, Menu };
    struct ControlButton {
        Control kind{Control::Left};
        entt::entity quad{entt::null};
        glm::dvec2 centrePx{0.0}; // in the level's pixels, recomputed each tick
        glm::dvec2 sizePx{0.0};
    };
    std::vector<ControlButton> m_controls;

    void buildControls(entt::registry& registry);
    void unloadControls(entt::registry& registry);
    // Places them against the camera and returns which one the pointer is on,
    // or nullptr. Called once a tick, before the shot is fired, because a tap
    // that works a control must NOT also open a portal under it.
    const ControlButton* layOutControls(entt::registry& registry);
};

} // namespace MagicPortals
