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

#include "sim/Camera.hpp"
#include "sim/Chapters.hpp"
#include "sim/Game.hpp"
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
//  - What the levels do not picture - the player, the portals a shot opens, the
//    shot - is still a box.
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
    float readInput(entt::registry& registry);
    void syncDrawables(entt::registry& registry);
    void syncSprites(entt::registry& registry);
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

    struct Hud {
        entt::entity status{entt::null};
        entt::entity result{entt::null};
        entt::entity controls{entt::null};
    };
    Hud m_hud;
};

} // namespace MagicPortals
