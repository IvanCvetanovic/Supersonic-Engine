// Magic Portals as the port plays it. MagicPortalsLayer.hpp says what it is and
// what it is not.

#include "MagicPortalsLayer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "core/Application.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Raycast.hpp"
#include "core/RenderSettings.hpp"
// For RenderSystem::Stats, which the app publishes into the registry context:
// what the last frame actually drew, culled and refused.
#include "core/RenderSystem.hpp"
#include "core/ScreenOverlay.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"
// For handing a level's lightmaps back when it goes. Only through the pointer the
// app publishes in the registry's context, which a suite's bare registry has not.
#include "renderer/TextureRegistry.hpp"

#include "sim/Roles.hpp"
#include "sim/Units.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

namespace MagicPortals {

namespace {

// How far in front of the level the camera stands. It is orthographic, so only
// the ordering matters: everything drawn lies between it and the far plane.
constexpr float kCameraDistance = 20.0f;

// The window's shape until a viewport says otherwise: the 16:9 at which the
// view is 455 px wide.
constexpr float kDefaultAspect = 16.0f / 9.0f;

// Markers - buttons, crystals, the exit, portals - stand in front of the bodies.
constexpr float kMarkerZ = 0.5f;
constexpr float kMarkerDepth = 0.1f;

// No-portal zones lie behind everything, thin: shown where nothing covers them,
// and never over the player.
constexpr float kZoneZ = -0.35f;
constexpr float kZoneDepth = 0.02f;
const glm::vec3 kZoneColour(0.45f, 0.14f, 0.16f);
const glm::vec3 kHazardColour(1.00f, 0.25f, 0.10f);

// The level's art, one slot per sprite from the back: flat quads a hair apart,
// in the order Godot draws the canvas (Sprites.hpp), all of it behind the
// markers. The player takes the slot after the last sprite at z_index 0 or
// below, which is where the remake's player - added to the level after its
// nodes, at z_index 0 - is drawn. A thrown stone goes just behind it.
constexpr float kSpriteBackZ = -1.5f;
constexpr float kSpriteSlotZ = 0.004f;

float SlotZ(int slot) { return kSpriteBackZ + kSpriteSlotZ * static_cast<float>(slot); }

const glm::vec3 kStaticColour(0.42f, 0.44f, 0.50f);
const glm::vec3 kDoorColour(0.30f, 0.45f, 0.75f);
const glm::vec3 kCrateColour(0.72f, 0.50f, 0.26f);      // teleportable
const glm::vec3 kFixedCrateColour(0.42f, 0.28f, 0.16f); // teleportable 0
const glm::vec3 kStoneColour(0.58f, 0.58f, 0.62f);      // a rolling stone
const glm::vec3 kBreakableColour(0.86f, 0.74f, 0.48f);  // what a stone breaks: sandy, as the walls are
const glm::vec3 kPlayerColour(1.00f, 0.78f, 0.25f);
const glm::vec3 kButtonUpColour(0.80f, 0.22f, 0.18f);
const glm::vec3 kButtonDownColour(0.25f, 0.85f, 0.30f);
const glm::vec3 kCrystalColour(0.35f, 0.90f, 1.00f);
const glm::vec3 kExitColour(0.25f, 0.70f, 0.35f);
const glm::vec3 kExitReachedColour(0.60f, 1.00f, 0.60f);
const glm::vec3 kPortalColour(0.90f, 0.30f, 0.90f);
const glm::vec3 kShotColour(0.60f, 0.60f, 1.00f); // projectile.ent's light is this blue
constexpr double kShotSizePx = 8.0;
const glm::vec3 kStaticRedColour(0.90f, 0.30f, 0.25f);  // a static portal the level colours red
const glm::vec3 kStaticBlueColour(0.30f, 0.50f, 1.00f); // and blue
const glm::vec3 kBeholderColour(0.55f, 0.20f, 0.60f);   // the beholder's reach
const glm::vec3 kSpikeColour(0.85f, 0.85f, 0.70f);
constexpr double kSpikeBoxPx = 6.0;
// A carranca's fireball, in the colour of its own Light (r 1, g 0.5, b 0.2) and
// at the size of its own Collision (16 x 16). Both are fireball.ent's, which is
// as close to its picture as the port can get: the entity has no sprite.
const glm::vec3 kFireballColour(1.00f, 0.50f, 0.20f);
constexpr double kFireballBoxPx = 16.0;
// Where the original adds a spike: at z -4 (ETHCallback_beholder).
constexpr int kSpikeZIndex = -4;

bool IsTrigger(const entt::registry& registry, entt::entity entity) {
    using namespace Supersonic;
    if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) return box->isTrigger;
    if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) return sphere->isTrigger;
    if (const auto* hull = registry.try_get<ConvexHullColliderComponent>(entity)) return hull->isTrigger;
    return false;
}

std::string Count(int n) { return std::to_string(n); }

// The first top-level node of a role, or null.
const Tscn::Node* FirstOfRole(const Game::Data& data, const char* role) {
    for (const Tscn::Node& node : data.scene.nodes) {
        if (node.parent == "." && Roles::RoleOf(data.roles, node) == role) return &node;
    }
    return nullptr;
}

bool PositionOf(const Tscn::Node* node, glm::dvec2& out) {
    const Tscn::Value* at = node != nullptr ? node->Find("position") : nullptr;
    if (at == nullptr || at->kind != Tscn::Value::Kind::Vector2) return false;
    out = glm::dvec2(at->numbers[0], at->numbers[1]);
    return true;
}

// WHAT THE SCENE'S NUMBERS ARE: display values, as the original's were.
//
// The original drew into an 8-bit GLES2 framebuffer. Every tint, fade and glow
// in its art was multiplied and blended on the encoded bytes, and an additive
// sum clipped at 255. Through the engine's default chain (sRGB decode, bloom,
// Reinhard, encode) a white texel stopped at 186 and 1-1's torch flame at
// (197, 158, 127), where the original's is (253, 252, 159). The remake's fit of
// the original's pixels settles which arithmetic it was (its lighting
// design_port.md section 0, fit.md section 4): blending in linear light scores
// 13.59 of 255 over 1,359 unlit blocks against 0.28 on encoded values, and
// 11.90 against 0.92 over 1,042 lightmapped ones. Everything the lighting adds
// after this multiplies those same bytes, so this is not a look to tune but the
// space the rest is defined in.
//
// A flat black behind everything, which also means no sky pass: Ethanon clears
// to black (ETHEngine.cpp:114, and GLES2Video.cpp:87's default), and the
// original's own LoadingScreen::preLoop sets SetBackgroundColor(0xFF000000). A
// display-encoded scene left on the sky is incoherent (RenderSettings.hpp).
//
// No bloom, which the original never drew. The engine records no bloom chain
// for this encoding whatever the intensity says (BloomPass::RunsBloomChain);
// zero makes the scene's own settings say the same.
//
// Quantised to 5/6/5 as the original's 16-bit surface when lighting.json says so
// (step 55, the design's G6): OnAttach reads it, since the rules come with a level.
Supersonic::RenderSettings SceneRendering() {
    Supersonic::RenderSettings rendering;
    rendering.encoding = Supersonic::RenderSettings::SceneEncoding::DisplayEncoded;
    rendering.background = Supersonic::RenderSettings::Background::Color;
    rendering.backgroundColor[0] = 0.0f;
    rendering.backgroundColor[1] = 0.0f;
    rendering.backgroundColor[2] = 0.0f;
    rendering.bloomIntensity = 0.0f;
    return rendering;
}

} // namespace

MagicPortalsLayer::MagicPortalsLayer(Paths paths, std::string startLevel)
    : m_paths(std::move(paths)), m_startLevel(std::move(startLevel)) {}

const Chapters::Level* MagicPortalsLayer::Current() const {
    return m_current >= 0 ? &m_chapters.levels[static_cast<std::size_t>(m_current)] : nullptr;
}

glm::dvec2 MagicPortalsLayer::ViewPx() const {
    return glm::dvec2(m_viewHeightPx * static_cast<double>(m_aspect), m_viewHeightPx);
}

// ---- attach and detach ------------------------------------------------------

void MagicPortalsLayer::OnAttach(entt::registry& registry) {
    // The port thinks at 60 Hz, and the layer states it: there is no scene to
    // author the clock in.
    auto& clock = registry.ctx().contains<Supersonic::SimulationClock>()
                      ? registry.ctx().get<Supersonic::SimulationClock>()
                      : registry.ctx().emplace<Supersonic::SimulationClock>();
    clock.fixedDelta = kTick;
    // And the scene's encoding (SceneRendering), before anything is drawn, for
    // the menu as for a level. Assigned rather than emplaced: a --scene load
    // may already have put a RenderSettings of its own in the context.
    registry.ctx().insert_or_assign<Supersonic::RenderSettings>(SceneRendering());
    {
        Lighting::Rules lighting;
        std::string why;
        if (Lighting::LoadRules(m_paths.portData + "/lighting.json", lighting, why) && lighting.framebufferRgb565) {
            registry.ctx().get<Supersonic::RenderSettings>().quantize =
                Supersonic::RenderSettings::OutputQuantize::Rgb565;
        }
    }

    bindInput();
    loadSounds();

    // The medals earned before this run, if this build was told where they are
    // kept. An empty saveDir is the ordinary case for a test and means this
    // never opens a file; a file that exists and will not parse is reported and
    // then left alone rather than overwritten.
    if (std::string why; !m_scores.Open(m_paths.saveDir, why)) {
        SUPERSONIC_LOG_WARN("Magic Portals") << "medals not loaded: " << why << std::endl;
    }

    // The original's achievements (sim/Achievements.hpp). They are not this
    // repository's, so a machine without the remake's out/ has none: the dashboard
    // then draws no rows, and this says why, once. Not a reason to refuse the start.
    if (std::string why; Achievements::Load(m_paths.achievements, m_achievements, why)) {
        m_achievementsLoaded = true;
    } else {
        m_achievementsError = why;
        SUPERSONIC_LOG_WARN("Magic Portals") << "no achievement rows: " << why << std::endl;
    }

    std::string error;
    const bool read = Chapters::Load(m_paths.chapters, m_chapters, error) &&
                      Camera::LoadRules(m_paths.data + "/portals.json", m_cameraRules, error) &&
                      Camera::LoadViewHeight(m_paths.portData + "/view.json", m_viewHeightPx, error) &&
                      Art::LoadRules(m_paths.portData + "/art.json", m_artRules, error) &&
                      Hud::LoadRules(m_paths.portData + "/ui.json", m_hudRules, error) &&
                      Pause::LoadRules(m_paths.portData + "/ui.json", m_pauseRules, error) &&
                      LevelEnd::LoadRules(m_paths.portData + "/ui.json", m_levelEndRules, error) &&
                      Popup::LoadRules(m_paths.portData + "/ui.json", m_popupRules, error) &&
                      MainMenu::LoadRules(m_paths.portData + "/ui.json", m_mainMenuRules, error) &&
                      Loading::LoadRules(m_paths.portData + "/ui.json", m_loadingRules, error) &&
                      Credits::LoadRules(m_paths.portData + "/ui.json", m_creditsRules, error) &&
                      Selector::LoadRules(m_paths.portData + "/ui.json", m_selectorRules, error) &&
                      Dashboard::LoadRules(m_paths.portData + "/ui.json", m_dashboardRules, error) &&
                      Locking::LoadRules(m_paths.portData + "/ui.json", m_lockingRules, error);
    // ui.json is the port's own and committed, so a HUD that will not read is a
    // fault in this repository and stops the start as loudly as a missing level.
    m_hudReady = read;
    if (!read) {
        m_loadError = error;
        m_hudError = error;
    } else if (m_startLevel.empty()) {
        // No level named: the loading screen and then the menu, which is what the
        // game itself opens with (owner ruling R4). A named level is entered
        // directly, so --level and every suite reach the game exactly as they did
        // before the menu existed.
        buildCamera(registry);
        openMenu(registry, Screen::Loading);
    } else if (const int start = m_chapters.Find(m_startLevel); start < 0) {
        m_loadError = m_startLevel + " is not a level of " + m_paths.chapters;
    } else {
        buildCamera(registry);
        loadLevel(registry, start);
    }
    if (read) {
        // The pictures the menu states draw through the overlay, each resolved to
        // its hd twin once: the backgrounds are entities, named with their
        // directory; the rest are sprites. One that cannot be read says so, once,
        // and is not drawn.
        const auto resolve = [this](const std::string& file, bool named) {
            if (m_menuPictures.find(file) != m_menuPictures.end()) return;
            const std::string path = named ? originalAsset(file) : menuImage(file);
            if (imageSizePx(path).y > 0.0) {
                m_menuPictures[file] = path;
            } else {
                SUPERSONIC_LOG_WARN("Magic Portals") << "menu picture could not be read: " << path << std::endl;
                m_menuPictures[file] = std::string();
            }
        };
        const MainMenu::Rules& menu = m_mainMenuRules;
        resolve(menu.background.sprite, true);
        const std::initializer_list<const std::string*> sprites = {
            &menu.play.sprite,  &menu.title.sprite,     &menu.credits.sprite,   &menu.achievements.sprite,
            &menu.sound.sprite, &menu.soundOffSprite,   &menu.music.sprite,     &menu.music.offSprite,
            &m_loadingRules.logo.sprite};
        for (const std::string* file : sprites) {
            resolve(*file, false);
        }
        // Credits and the dashboard: the backgrounds are entities again, and the
        // achievements' icons live in their own directory, with no hd twins.
        const Credits::Rules& credits = m_creditsRules;
        const Dashboard::Rules& board = m_dashboardRules;
        resolve(credits.background.sprite, true);
        resolve(board.background.sprite, true);
        const std::initializer_list<const std::string*> more = {
            &credits.back.sprite,     &credits.papyrus.sprite,  &credits.strip.sprite, &board.back.sprite,
            &board.rows.barSprite,    &board.rows.lockSprite,   &board.plaque.sprite,  &board.bar.sprite,
            &board.start.sprite};
        for (const std::string* file : more) {
            resolve(*file, false);
        }
        // Chapter select and the grid: the background an entity, the rest sprites.
        const Selector::Rules& selector = m_selectorRules;
        resolve(selector.background.sprite, true);
        const Selector::Rules::Chapters& c = selector.chapters;
        const Selector::Rules::Levels& l = selector.levels;
        for (const std::string* file :
             {&c.title.sprite, &c.back.sprite, &c.forward.sprite, &c.lockSprite, &c.medalGold, &c.medalSilver,
              &c.medalBronze, &c.counter.sprite, &l.back.sprite, &l.forward.sprite, &l.tileSprite,
              &l.lockedTileSprite, &l.bossSprite, &l.medalGold, &l.medalSilver, &l.medalBronze}) {
            resolve(*file, false);
        }
        int worlds = 0;
        for (const Chapters::Level& level : m_chapters.levels) worlds = std::max(worlds, level.world + 1);
        for (int w = 0; w < worlds; ++w) resolve(c.iconPrefix + std::to_string(w) + ".png", false);
        if (m_achievementsLoaded) {
            for (const Achievements::Entry& entry : m_achievements.entries) {
                resolve(board.rows.iconDirectory + entry.icon, true);
            }
            resolve(board.rows.iconDirectory + m_achievements.secret.icon, true);
        }
    }
    if (m_current < 0 && m_screen == Screen::None) {
        SUPERSONIC_LOG_ERROR("Magic Portals") << "Could not start: " << m_loadError << std::endl;
    }
    buildHud(registry);
    updateHud(registry);
}

void MagicPortalsLayer::OnDetach(entt::registry& registry) {
    // Before the entities go: a looping voice is never "finished", so nothing
    // else will ever free it.
    stopMusic(registry);
    unloadLevel(registry);
    unloadMenu(registry);
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };
    destroy(m_camera);
    destroy(m_light);
    destroy(m_hud.status);
    destroy(m_hud.result);
    destroy(m_hud.controls);
    m_current = -1;
}

void MagicPortalsLayer::bindInput() {
    using namespace Supersonic;
    Input::BindActionKey(kLeft, Key::Left);
    Input::BindActionKey(kLeftAlt, Key::A);
    Input::BindActionKey(kRight, Key::Right);
    Input::BindActionKey(kRightAlt, Key::D);
    Input::BindActionMouseButton(kTap, MouseButton::Left);
    Input::BindActionKey(kRetry, Key::R);
    Input::BindActionKey(kSkip, Key::N);
    Input::BindActionKey(kBoxes, Key::B);
    Input::BindActionKey(kBack, Key::Escape);
    // G, because Key has no function keys at all - it stops at the letters,
    // the arrows and the modifiers.
    Input::BindActionKey(kDump, Key::G);
}

// ---- levels -----------------------------------------------------------------

bool MagicPortalsLayer::loadLevel(entt::registry& registry, int index) {
    // A retry draws the same level again at once, so its lightmaps stay where
    // they are: a retry does not touch the disk (level_manager.gd:5-13).
    unloadLevel(registry, m_loaded && index == m_current);
    m_current = index;
    m_chapterComplete = false;
    m_loadError.clear();
    // Every level begins here - the first one, a retry, a skip, and going on
    // from a medal all come through - so this is the one place the finish beat
    // has to be forgotten. Left set, the NEXT level would open already on its
    // way to being scored and would put its medal up 1400 ms in, having been
    // played by nobody.
    m_finishing = false;
    m_finishClockMs = 0.0;
    // And the death beat, for the same reason: left set, the next level would
    // open already dying and put the lost screen up 1400 ms in.
    m_dying = false;
    m_dyingClockMs = 0.0;
    // And the screen either raises, with its counters, and the HUD's last byte.
    m_end = EndScreen{};
    m_padEndByte = 0;
    m_clearShownAtEnd = false;
    // And the level's age, which everything it opens with is timed by. A retry
    // is a new level to the original - its restart button builds a fresh Game
    // state, whose preLoop starts the black, "Part N" and the plaque again
    // (GameLayer::update, bytes 100358..100663) - so it is zero here too.
    m_levelAgeMs = 0.0;
    // And the time a pause or a popup stood it still, which the new level has had
    // none of, and the plaque's dismissal, which it has not reached.
    m_stoppedMs = 0.0;
    m_plaqueDismissAgeMs = -1.0;
    m_plaqueAlpha = 0.0;
    const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(index)];

    // A refused level says why on the HUD, which a player reads, and in the log,
    // which is all a run leaves behind afterwards. Without the second, a chapter
    // that stops at its fourteenth level leaves nothing to say which one that
    // was: the message below is the only record. It covers the first level too,
    // which `m_current` being set above keeps out of OnAttach's own report.
    const auto refuse = [this, &entry](std::string why) {
        m_loadError = std::move(why);
        SUPERSONIC_LOG_ERROR("Magic Portals") << entry.name << " refused: " << m_loadError << std::endl;
        return false;
    };

    std::string error;
    if (index != m_dataIndex) {
        m_dataIndex = -1;
        m_lit = false;
        m_look = Lighting::Scene{};
        if (!Game::LoadData(m_paths.levels + "/" + entry.name + ".tscn", m_paths.data, m_paths.prisms, m_data,
                            error, m_paths.portData)) {
            return refuse(error);
        }
        m_dataIndex = index;
        // Its lighting, read once with it. A level whose lighting will not read
        // is still played, its sprites in their own colours - as a level whose
        // art will not read is still played as boxes - and says why.
        m_lit = Lighting::Read(m_data.scene, m_paths.art, m_look, m_lightingError);
        if (m_lit) {
            m_lightingError.clear();
        } else {
            SUPERSONIC_LOG_WARN("Magic Portals") << entry.name << " drawn unlit: " << m_lightingError << std::endl;
        }
    }
    if (!Game::Start(m_data, registry, m_level, error)) {
        // Start may have built some of the level before it refused.
        unloadLevel(registry);
        return refuse(error);
    }
    if (!PositionOf(FirstOfRole(m_data, Roles::kLevelBounds), m_boundsPx) || m_boundsPx.x <= 0.0 ||
        m_boundsPx.y <= 0.0) {
        unloadLevel(registry);
        return refuse(entry.name + " has no level_bounds");
    }
    // Where the camera starts, or the spawn in a level that places none
    // (level_runtime.gd:214-215).
    glm::dvec2 cameraStartPx(0.0);
    if (!PositionOf(FirstOfRole(m_data, "camera_start"), cameraStartPx)) {
        PositionOf(FirstOfRole(m_data, Roles::kPlayerSpawn), cameraStartPx);
    }

    m_loaded = true;
    // The lightmaps this level names, held until it goes. The same list again
    // after a retry, which kept the last one.
    m_heldLightmaps.clear();
    if (m_lit) {
        for (const auto& [node, look] : m_look.nodes) {
            if (!look.lightmap.empty()) m_heldLightmaps.push_back(look.lightmap);
        }
        // The nodes are hashed: sorted, so the list does not depend on the map.
        std::sort(m_heldLightmaps.begin(), m_heldLightmaps.end());
        m_heldLightmaps.erase(std::unique(m_heldLightmaps.begin(), m_heldLightmaps.end()), m_heldLightmaps.end());
    }
    // The player's lighting height: its marker's depth in the level file. The
    // player is added where the marker stands, and the reader holds a look for
    // the spriteless marker for exactly this (2-09's stands at z 2).
    m_playerZ = 0.0;
    if (const Tscn::Node* spawn = FirstOfRole(m_data, Roles::kPlayerSpawn); m_lit && spawn != nullptr) {
        if (const auto look = m_look.nodes.find(spawn->name); look != m_look.nodes.end()) m_playerZ = look->second.z;
    }
    buildDrawables(registry);
    m_aspect = viewportAspect(registry);
    // THE LEVEL OPENS ON THE PLAYER, by the owner's choice.
    //
    // The original holds on camera_start - which sits near a level's exit - for
    // cameraHoldTime and then eases to the player, and this port did the same:
    // Camera.hpp decodes it and Camera::Follow still carries the hold, which
    // test_mp_camera pins. But both of its numbers are _guess in portals.json,
    // and shown a level the owner asked for the camera to begin already framed
    // on the character rather than pan in from the goal.
    //
    // So the hold is not removed from the camera, only unused here: Start takes
    // the player's own place, and what hold_time_s would have run is zeroed. A
    // level that wants the establishing look back needs only this line.
    glm::dvec2 openOnPx = cameraStartPx;
    if (m_level.player != entt::null && registry.valid(m_level.player)) {
        openOnPx = Units::ToPixels(registry.get<Supersonic::TransformComponent>(m_level.player).position);
    }
    m_follow.Start(m_cameraRules, openOnPx, ViewPx(), m_boundsPx);
    m_follow.holdLeftS = 0.0;
    // AFTER buildDrawables, which is what sets m_artReady at the end of
    // buildSprites: built before it, the controls would find the flag still
    // false and quietly make nothing at all.
    buildControls();
    placeCamera(registry);
    layOutControls();
    // The sign starts from where the level put it, aimed at this camera.
    tickNoPortalSign(0.0);
    // A new level is a cut, not a pan: there is nothing to draw the camera
    // coming from.
    if (m_camera != entt::null && registry.valid(m_camera)) {
        if (auto* interpolated = registry.try_get<Supersonic::InterpolatedCameraComponent>(m_camera)) {
            interpolated->captured = false;
        }
    }
    syncDrawables(registry);
    // The help blocks it places, and the popup it opens as it loads, which
    // Game::preLoop raises in the load frame (Game::managePopups, bytes
    // 115157..115478): a retry of 1-02 raises it again. Only where the HUD is
    // drawn at all - a level drawn as boxes has no UI to put one in.
    findHelpBlocks();
    if (m_artReady && m_hudReady) {
        if (const Popup::Class* popup = Popup::LevelStartClass(m_popupRules, entry.name)) {
            openPopup(registry, *popup, false);
        }
    }
    return true;
}

void MagicPortalsLayer::unloadLevel(entt::registry& registry, bool keepLightmaps) {
    // THE LIGHTMAPS GO BACK WITH THE LEVEL. Each lightmapped sprite holds a
    // texture and a material set of its own (730 across the game), and the
    // registry keeps both until told to drop them; a session walking the chapters
    // would otherwise hold every level's at once (the lighting design's section
    // 6, and step 42's walk). A retry keeps them: it draws them again at once.
    if (!keepLightmaps) releaseLightmaps(registry);
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };
    for (Drawn& drawn : m_bodies) destroy(drawn.box);
    m_bodies.clear();
    for (auto& e : m_buttons) destroy(e);
    m_buttons.clear();
    for (auto& e : m_crystals) destroy(e);
    m_crystals.clear();
    for (auto& e : m_portals) destroy(e);
    m_portals.clear();
    for (auto& e : m_statics) destroy(e);
    m_statics.clear();
    for (auto& e : m_zones) destroy(e);
    m_zones.clear();
    for (auto& e : m_hazards) destroy(e);
    m_hazards.clear();
    for (ThrownBox& thrown : m_thrown) {
        destroy(thrown.box);
        destroy(thrown.quad);
    }
    m_thrown.clear();
    for (DrawnSprite& drawn : m_sprites) destroy(drawn.quad);
    m_sprites.clear();
    // The walk arrows and the corner buttons go with the level they were built
    // for, and so does what the level opened with.
    unloadControls();
    // And a pause or a popup over it: whatever it held goes with the bodies it
    // held, and so do the help blocks and a touch on one.
    m_pause = PauseScreen{};
    m_popup = PopupScreen{};
    m_frozen = Frozen{};
    m_helpBlocks.clear();
    m_helpTouch = HelpTouch{};
    // The particles the level's entities were emitting go with them; a retry
    // would otherwise pile a second pool on the first. So do its lights, and the
    // shot's.
    unloadEmitters(registry);
    unloadLights(registry);
    // The next level starts its own comparison. Without this, the first tick
    // of a level would hear every counter fall back to zero as if it had
    // happened - a retry would play the whole level's sounds at once.
    m_watch = Watch{};
    // The next level is compared against itself, not against this one. Without
    // this, its first frame reports every sprite of the level just unloaded as
    // having changed - the same stale-baseline fault the sound watch above has.
    m_onScreenLast.clear();
    m_reportedOnce = false;
    m_artReady = false;
    for (auto& e : m_portalQuads) destroy(e);
    m_portalQuads.clear();
    destroy(m_shotQuad);
    destroy(m_playerQuad);
    destroy(m_beholderBox);
    destroy(m_beholderQuad);
    m_beholderColour = glm::vec4(1.0f);
    for (auto& e : m_spikes) destroy(e);
    m_spikes.clear();
    for (auto& e : m_fireballs) destroy(e);
    m_fireballs.clear();
    destroy(m_shot);
    destroy(m_player);
    destroy(m_exit);
    // And the level's own bodies, with what its launchers threw.
    for (auto& [name, entity] : m_level.built.entities) {
        entt::entity e = entity;
        destroy(e);
    }
    for (const Launchers::Thrown& thrown : m_level.launchers.live) {
        entt::entity e = thrown.body;
        destroy(e);
    }
    for (const Boss::Rock& rock : m_level.boss.rocks) {
        entt::entity e = rock.body;
        destroy(e);
    }
    // The platform the dark dragon's death added, which belongs to no Built and
    // so is not in the loop above.
    {
        entt::entity e = m_level.darkDragon.platformBody;
        destroy(e);
    }
    m_platformDrawn = false;
    m_platformBoxed = false;
    destroy(m_level.player);
    m_level = Game::Level{};
    m_loaded = false;
    m_ambient = glm::dvec3(1.0);
}

void MagicPortalsLayer::releaseLightmaps(entt::registry& registry) {
    if (auto* const* textures = registry.ctx().find<Supersonic::TextureRegistry*>();
        textures != nullptr && *textures != nullptr) {
        // A path never acquired is simply not there; Invalidate says false and
        // does nothing. Since step 47 each is acquired by the sprite that draws
        // it, the first frame the level is drawn. Before the sprites go, which
        // is safe: nothing renders between here and their destruction below.
        for (const std::string& path : m_heldLightmaps) (*textures)->Invalidate(path);
    }
    m_lightmapsHandedBack += m_heldLightmaps.size();
    m_heldLightmaps.clear();
}

void MagicPortalsLayer::goTo(entt::registry& registry, int next) {
    if (next >= 0) {
        loadLevel(registry, next);
        return;
    }
    unloadLevel(registry);
    m_loadError.clear();
    m_chapterComplete = true;
}

// Defined below. Declared here because clearLevel records the FINAL medal and
// comes first in this file.
int MedalFor(const MagicPortalsLayer::Cleared& cleared);

void MagicPortalsLayer::clearLevel(entt::registry& registry) {
    const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(m_current)];
    Cleared cleared;
    cleared.name = entry.name;
    cleared.label = Chapters::Label(entry);
    cleared.portalsUsed = m_level.portals.portalsUsed;
    cleared.goldenScore = entry.goldenScore;
    cleared.traversals = m_level.portals.traversals;
    cleared.crystalsTotal = static_cast<int>(m_level.goals.crystals.size());
    cleared.crystals = cleared.crystalsTotal - m_level.goals.Remaining();
    m_lastCleared = cleared;

    // THE FINAL MEDAL, not the one the screen is about to count up to.
    //
    // The finished screen's medal follows its counter as it rises, which is a
    // picture (LevelEnd::ShownScore); this is the result. The original writes it at this same moment
    // and from a different object - GameStateController::writeScore, not the
    // layer that animates it - for the same reason.
    //
    // Saved only when it changed. ScoreManager::setScore writes only when the
    // new score beats the old, so a replay that goes worse takes nothing away
    // and costs no write at all.
    if (m_scores.Record(entry.world, entry.index, MedalFor(cleared))) {
        if (std::string why; !m_scores.Save(why)) {
            SUPERSONIC_LOG_WARN("Magic Portals") << "medals not saved: " << why << std::endl;
        }
    }

    // The original does not go straight on: GameStateController::writeScore
    // puts a LevelFinishedLayer up, with the medal the play earned and buttons
    // to play it again, go on, or pick another.
    openFinished(registry);
}

// The medal a play earns, as computeScore has it (ScoreManager.angelscript,
// bytes 363979..364204): 3 is gold, 2 silver, anything else bronze. One
// implementation, LevelEnd::ComputeScore, which the finished screen's live medal
// uses too.
int MedalFor(const MagicPortalsLayer::Cleared& cleared) {
    return LevelEnd::ComputeScore(cleared.portalsUsed, cleared.goldenScore, cleared.crystals, cleared.crystalsTotal);
}

// ---- the entities' particles --------------------------------------------------
//
// What makes the original's scenery move: each entity's own <ParticleSystem>
// (sim/Particles.hpp), which the converter drops. A crystal sparkles, a torch
// burns, a static portal turns - none of it is in the level file.
//
// Drawn the way everything else in this port is: one textured quad per live
// particle, added rather than mixed because every emitter in the game is
// AM_ADD. Carried on the FRAME, in OnUpdate, because a particle is a picture:
// it must not reach Game::Level, the fixed tick or the state hash.
//
// The arithmetic is Ethanon's own (ETHParticleManager::UpdateParticleSystem,
// ResetParticle and PositionParticle), kept in its units: a frame-speed unit
// is a sixtieth of a second, and every rate below is per one of those.

double MagicPortalsLayer::particleRandom(double from, double to) {
    if (!(to > from)) return from;
    std::uniform_real_distribution<double> spread(from, to);
    return spread(m_particleRandom);
}

void MagicPortalsLayer::buildEmitters(entt::registry& registry) {
    unloadEmitters(registry);
    // Without the level's art there is nothing to decorate: the level is drawn
    // as boxes and says so.
    if (!m_artReady) return;

    std::map<std::string, std::string> entities; // node -> its entity's name
    for (const Tscn::Node& node : m_data.scene.nodes) {
        if (node.parent != ".") continue;
        entities[node.name] = Roles::EntityName(node);
    }

    for (std::size_t index = 0; index < m_sprites.size(); ++index) {
        const DrawnSprite& drawn = m_sprites[index];
        const auto found = entities.find(drawn.sprite.node);
        if (found == entities.end() || found->second.empty()) continue;
        std::string entity = found->second;
        // A level names its entities both ways: "portal_static" and "sky.ent".
        if (entity.size() > 4 && entity.compare(entity.size() - 4, 4, ".ent") == 0) {
            entity.resize(entity.size() - 4);
        }
        std::vector<Particles::System> systems;
        std::string error;
        if (!Particles::Load(m_paths.original + "/entities/" + entity + ".ent", systems, error)) continue;
        for (std::size_t slot = 0; slot < systems.size(); ++slot) {
            const Particles::System& system = systems[slot];
            const std::string image = m_paths.original + "/particles/" + system.bitmap;
            const glm::dvec2 sheet = imageSizePx(image);
            if (sheet.x <= 0.0 || sheet.y <= 0.0) {
                SUPERSONIC_LOG_WARN("Magic Portals") << "particle image could not be read: " << image << std::endl;
                continue;
            }
            Emitter emitter;
            emitter.system = system;
            emitter.atPx = Sprites::CentrePx(drawn.sprite);
            // Just in front of the art it decorates, and behind the markers.
            emitter.z = drawn.z + 0.01f;
            emitter.image = image;
            emitter.cellPx = glm::dvec2(sheet.x / system.columns, sheet.y / system.rows);
            emitter.crystal = drawn.crystal;
            // Which entity and which of its systems: a light's brightness follows
            // the live share of its owner's FIRST system (syncLights).
            emitter.sprite = static_cast<int>(index);
            emitter.slot = static_cast<int>(slot);
            emitter.particles.resize(static_cast<std::size_t>(std::min(system.count, kMaxParticles)));
            m_emitters.push_back(std::move(emitter));
        }
    }
}

bool MagicPortalsLayer::addEntityEmitters(const std::string& entity, const glm::dvec2& atPx, float z,
                                          double angleDeg) {
    std::vector<Particles::System> systems;
    std::string error;
    if (!Particles::Load(m_paths.original + "/entities/" + entity, systems, error) || systems.empty()) return false;
    for (const Particles::System& system : systems) {
        const std::string image = m_paths.original + "/particles/" + system.bitmap;
        const glm::dvec2 sheet = imageSizePx(image);
        if (sheet.x <= 0.0 || sheet.y <= 0.0) {
            SUPERSONIC_LOG_WARN("Magic Portals") << "particle image could not be read: " << image << std::endl;
            continue;
        }
        Emitter emitter;
        emitter.system = system;
        emitter.atPx = atPx;
        // A later system of the same entity draws over an earlier one.
        emitter.z = z + 0.001f * static_cast<float>(m_emitters.size());
        emitter.image = image;
        emitter.cellPx = glm::dvec2(sheet.x / system.columns, sheet.y / system.rows);
        emitter.angleDeg = angleDeg;
        emitter.particles.resize(static_cast<std::size_t>(std::min(system.count, kMaxParticles)));
        m_emitters.push_back(std::move(emitter));
    }
    return true;
}

std::string MagicPortalsLayer::loadingHaloImage() {
    // Only into a directory the caller named: an empty Paths::prisms would put the
    // file in the working directory, so the halo is then left undrawn instead.
    if (m_paths.prisms.empty()) return {};
    const std::string source = m_paths.original + "/" + m_loadingRules.haloSprite;
    std::error_code ec;
    std::filesystem::create_directories(m_paths.prisms, ec);
    const std::filesystem::path target = m_paths.prisms / "black_halo_multiply.png";
    if (std::filesystem::exists(target, ec) && std::filesystem::last_write_time(target, ec) >=
                                                    std::filesystem::last_write_time(source, ec)) {
        return target.string();
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* grey = stbi_load(source.c_str(), &width, &height, &channels, 1);
    if (grey == nullptr) {
        SUPERSONIC_LOG_WARN("Magic Portals") << "loading halo could not be read: " << source << std::endl;
        return {};
    }
    // AM_MODULATE draws dst * texel; black at alpha (1 - texel) leaves exactly that.
    std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++i) {
        rgba[i * 4u + 3u] = static_cast<unsigned char>(255 - grey[i]);
    }
    stbi_image_free(grey);
    if (stbi_write_png(target.string().c_str(), width, height, 4, rgba.data(), width * 4) == 0) {
        SUPERSONIC_LOG_WARN("Magic Portals") << "loading halo could not be written: " << target.string() << std::endl;
        return {};
    }
    return target.string();
}

void MagicPortalsLayer::unloadEmitters(entt::registry& registry) {
    for (Emitter& emitter : m_emitters) {
        for (Particle& particle : emitter.particles) {
            if (particle.quad != entt::null && registry.valid(particle.quad)) registry.destroy(particle.quad);
            particle.quad = entt::null;
        }
    }
    m_emitters.clear();
}

void MagicPortalsLayer::updateEmitters(entt::registry& registry, float deltaTime) {
    using namespace Supersonic;
    if (m_emitters.empty()) return;
    // The original caps a frame at 250 ms before turning it into its own unit,
    // so a stall does not fling every particle across the level.
    const double elapsedMs = std::min(static_cast<double>(deltaTime) * 1000.0, 250.0);
    const double frameSpeed = elapsedMs / 1000.0 * 60.0;

    for (Emitter& emitter : m_emitters) {
        const Particles::System& system = emitter.system;
        // A collected crystal takes its sparkle with it.
        bool emitting = true;
        if (emitter.crystal >= 0 && emitter.crystal < static_cast<int>(m_level.goals.crystals.size())) {
            const Goals::Crystal& crystal = m_level.goals.crystals[static_cast<std::size_t>(emitter.crystal)];
            emitting = !crystal.collected && !crystal.expired;
        }

        const int frames = std::max(1, system.Frames());
        const auto reset = [&](Particle& particle) {
            particle.elapsedMs = 0.0;
            particle.released = true;
            particle.lifeMs = system.lifeTimeMs + particleRandom(-system.randomLifeTimeMs * 0.5,
                                                                 system.randomLifeTimeMs * 0.5);
            if (particle.lifeMs <= 0.0) particle.lifeMs = std::max(1.0, system.lifeTimeMs);
            particle.size = system.size + particleRandom(-system.randomizeSize * 0.5, system.randomizeSize * 0.5);
            particle.angleDir = system.angleDir + particleRandom(-system.randAngle * 0.5, system.randAngle * 0.5);
            particle.angle = system.angleStart + particleRandom(0.0, system.randAngleStart) + emitter.angleDeg;
            particle.velocityPx =
                system.direction + glm::dvec2(particleRandom(-system.randomizeDir.x * 0.5, system.randomizeDir.x * 0.5),
                                              particleRandom(-system.randomizeDir.y * 0.5, system.randomizeDir.y * 0.5));
            particle.atPx = emitter.atPx + system.startPoint +
                            glm::dvec2(particleRandom(-system.randStartPoint.x * 0.5, system.randStartPoint.x * 0.5),
                                       particleRandom(-system.randStartPoint.y * 0.5, system.randStartPoint.y * 0.5));
            // PLAY_ANIMATION walks the sheet by age; PICK_RANDOM_FRAME takes one.
            particle.frame = system.animationMode == 2
                                 ? static_cast<int>(particleRandom(0.0, static_cast<double>(frames)))
                                 : 0;
            if (particle.frame >= frames) particle.frame = frames - 1;
        };

        for (std::size_t i = 0; i < emitter.particles.size(); ++i) {
            Particle& particle = emitter.particles[i];
            const auto hide = [&]() {
                if (particle.quad != entt::null && registry.valid(particle.quad)) registry.destroy(particle.quad);
                particle.quad = entt::null;
            };
            if (!emitting) {
                hide();
                continue;
            }

            particle.elapsedMs += elapsedMs;
            if (!particle.released) {
                if (emitter.killed) continue;
                // Staggered across one lifetime, in pool order, unless the
                // system releases the lot at once.
                const double releaseAt = (system.lifeTimeMs + system.randomLifeTimeMs) *
                                         (static_cast<double>(i) / static_cast<double>(emitter.particles.size()));
                if (!system.allAtOnce && particle.elapsedMs <= releaseAt) continue;
                reset(particle);
            }

            particle.velocityPx += system.gravity * frameSpeed;
            particle.atPx += particle.velocityPx * frameSpeed;
            particle.angle += particle.angleDir * frameSpeed;
            particle.size = std::clamp(particle.size + system.growth * frameSpeed, system.minSize, system.maxSize);

            const double age = particle.lifeMs > 0.0 ? particle.elapsedMs / particle.lifeMs : 1.0;
            if (particle.elapsedMs > particle.lifeMs) {
                ++particle.repeats;
                if ((system.repeat > 0 && particle.repeats >= system.repeat) || emitter.killed) {
                    hide();
                    continue;
                }
                reset(particle);
            }
            if (system.animationMode == 1 && frames > 1) {
                particle.frame = std::min(static_cast<int>(static_cast<double>(frames) * age), frames - 1);
            }

            if (particle.size <= 0.0) {
                hide();
                continue;
            }
            if (particle.quad == entt::null) {
                particle.quad = makeSprite(registry, "Magic Portals Particle", emitter.image, system.additive);
                if (frames > 1) {
                    auto& animation = registry.emplace<SpriteAnimationComponent>(particle.quad);
                    animation.columns = static_cast<uint32_t>(system.columns);
                    animation.rows = static_cast<uint32_t>(system.rows);
                    animation.frameCount = 1;
                    animation.playing = false; // the frame is this loop's, by age or at random
                }
            }
            if (frames > 1) {
                registry.get<SpriteAnimationComponent>(particle.quad).firstFrame =
                    static_cast<uint32_t>(particle.frame);
            }
            const glm::dvec4 colour = system.colour0 + (system.colour1 - system.colour0) * std::clamp(age, 0.0, 1.0);
            registry.get<MaterialComponent>(particle.quad).albedoColor =
                glm::vec4(static_cast<float>(colour.r), static_cast<float>(colour.g), static_cast<float>(colour.b),
                          static_cast<float>(colour.a));
            // The bitmap's own shape at the particle's size, turned as the
            // original turns it: its angle is degrees clockwise on the screen.
            const double height = emitter.cellPx.x > 0.0 ? particle.size * (emitter.cellPx.y / emitter.cellPx.x)
                                                         : particle.size;
            placeSprite(registry, particle.quad, particle.atPx, glm::dvec2(particle.size, height), emitter.z,
                        Units::ToWorldRotation(particle.angle * 3.14159265358979323846 / 180.0));
        }
    }
}

// ---- the menu ---------------------------------------------------------------
//
// The original's three screens, drawn with its own art: MainMenu, then
// WorldSelector, then LevelSelector.
//
// They are quads in the level's own pixel space rather than UI components,
// because the engine's UIImageComponent takes an uploaded texture handle and
// its UIButtonComponent is a coloured rounded rectangle with a text label -
// neither can show a PNG named by path, which is what every button here is. So
// the menu is drawn the way the levels are, on the same orthographic camera,
// and clicked through the same screen-to-plane mapping.
//
// What the port leaves out, and why. The original locks worlds and levels
// behind a save file its ScoreManager keeps, pages the grid by swiping
// (Swyper), and draws a page counter. The port keeps no save, so NOTHING IS
// LOCKED - a port decision, not the original's rule - and the grid pages with
// the original's own two buttons instead of a swipe.

std::string MagicPortalsLayer::menuImage(const std::string& file) const {
    // The original keeps its menu art in two places: the title, the buttons and
    // the icons under sprites/, but the two screen BACKGROUNDS among its
    // entities, beside the .ent files that place them. Asking in the wrong one
    // used to fail silently - the quad was simply not made - which is how the
    // main screen first shipped with no background at all.
    // THE HD SET, ALWAYS, which is the owner's call and not the original's rule.
    //
    // The APK ships sprites/hd/ beside sprites/: the same art at exactly twice
    // the size - level_button 64 -> 128, medal_gold_m 32 -> 64, game_main_title
    // 256 -> 512 - drawn by the artists rather than resampled. The original
    // chooses between them at run time: isHd() is GetScreenSize().y > 480 and
    // getHdSpriteDensity() returns 2 above it, so a 2013 phone got the small set
    // and a large screen the big one. That choice was a memory budget, and the
    // owner's word is that it has outlived its reason - so the port takes the
    // HD art whatever the window is doing.
    //
    // THE FALLBACK IS NOT DECORATION: 69 of the 80 files in sprites/ have an hd
    // twin and eleven do not, so a miss here is normal and must fall through
    // rather than fail. Nothing that reads a size may assume which one it got;
    // menuTick's medal is measured against the button's own size for exactly
    // that reason.
    std::error_code ec;
    const std::string hd = m_paths.original + "/sprites/hd/" + file;
    if (std::filesystem::exists(hd, ec)) return hd;
    const std::string sprites = m_paths.original + "/sprites/" + file;
    if (std::filesystem::exists(sprites, ec)) return sprites;
    return m_paths.original + "/entities/" + file;
}

glm::dvec2 MagicPortalsLayer::MenuBoxPx() const {
    // view.json's height, at the window's shape: the same box a level is shown
    // in, so the menu's art is the size the original drew it at.
    const double height = m_viewHeightPx > 0.0 ? m_viewHeightPx : 256.0;
    return glm::dvec2(height * static_cast<double>(m_aspect), height);
}

void MagicPortalsLayer::layOutMenu() {
    m_menuButtons.clear();
    if (m_screen == Screen::None) return;
    const glm::dvec2 box = MenuBoxPx();
    const auto at = [&box](double nx, double ny) { return glm::dvec2(nx * box.x, ny * box.y); };

    // A button is drawn at ITS OWN shape. Sizing one by the box alone stretches
    // whatever is not square, which is how the chapter icons - 84 x 128 - first
    // went out looking squashed.
    const auto sized = [this, &box](const char* file, double heightFraction) {
        const double height = box.y * heightFraction;
        const glm::dvec2 image = imageSizePx(menuImage(file));
        const double aspect = image.y > 0.0 ? image.x / image.y : 1.0;
        return glm::dvec2(height * aspect, height);
    };

    // The loading screen has no button; credits and the dashboard say what a touch
    // is on themselves (sim/Credits.hpp, sim/Dashboard.hpp).
    if (m_screen == Screen::Loading || m_screen == Screen::Credits || m_screen == Screen::Achievements) return;
    if (m_screen == Screen::Main) {
        // DRAWN THROUGH THE OVERLAY by EmitMenu from sim/MainMenu, which also says
        // what a touch is on. This is TAP START as it sits once its entrance is
        // over, for PressMenu and the suites: the view is the menu's box.
        const Hud::Rect settled = MainMenu::SettledPlayRect(m_mainMenuRules, box);
        MenuButton play;
        play.kind = MenuButton::Kind::Play;
        play.centrePx = settled.Centre();
        play.sizePx = settled.size;
        m_menuButtons.push_back(play);
        return;
    }

    if (m_screen == Screen::Finished || m_screen == Screen::Dead) {
        // OVER THE LEVEL, laid out by sim/LevelEnd.hpp on the camera's view and
        // drawn through the overlay (EmitHud): these are the buttons as they sit
        // once their entrance is over, in the level's pixels, which is what
        // PressMenu and the suites read. A tap is tested against where each one
        // is on its tick (endScreenTick), entrance and all.
        //
        // The finished screen's three are ONE COLUMN at x 0.75 and the lost
        // screen's two ONE ROW at y 0.6: AngelScript pushes a call's arguments
        // last-first, so LevelFinishedLayer's `PshC4 A; PshC4 B; vector2()` is
        // vector2(B, A), and the veil in the same constructor - one and a half
        // screens WIDE - is what settles the order. ui.json carries the pairs
        // already turned round.
        constexpr double kSettledMs = 1.0e9;
        const glm::dvec2 view = ViewPx();
        const glm::dvec2 corner = m_follow.centrePx - view * 0.5;
        for (const LevelEnd::Piece& piece : endPieces(view, kSettledMs)) {
            if (piece.element != LevelEnd::Element::Button) continue;
            MenuButton button;
            switch (piece.button) {
            case LevelEnd::Button::Restart:
                button.kind = MenuButton::Kind::Retry;
                break;
            case LevelEnd::Button::Next:
                button.kind = MenuButton::Kind::Next;
                break;
            case LevelEnd::Button::List:
                button.kind = MenuButton::Kind::List;
                break;
            }
            button.centrePx = corner + piece.rect.Centre();
            button.sizePx = piece.rect.size;
            m_menuButtons.push_back(button);
        }
        return;
    }

    // CHAPTER SELECT AND THE GRID are drawn through the overlay by EmitMenu from
    // sim/Selector, which also says what a touch is on (selectorInput). These are
    // their tiles at rest on their own pages and the two arrows once their entrance
    // is over, for PressMenu and the suites: the view is the menu's box.
    if (m_screen == Screen::Worlds || m_screen == Screen::Levels) {
        const Selector::Board& board = m_selectorBoard;
        for (int item = 0; item < board.items; ++item) {
            const Hud::Rect rect = Selector::ItemRect(m_selectorRules, board, item, box);
            MenuButton button;
            button.centrePx = rect.Centre();
            button.sizePx = rect.size;
            if (board.levels) {
                button.kind = MenuButton::Kind::Level;
                button.world = board.world;
                button.level = board.tiles[static_cast<std::size_t>(item)].level;
            } else {
                button.kind = MenuButton::Kind::World;
                button.world = item;
            }
            m_menuButtons.push_back(button);
        }
        constexpr double kSettledMs = 1.0e9;
        const bool levels = m_screen == Screen::Levels;
        for (const auto& [kind, placed] :
             {std::pair<MenuButton::Kind, const UiLayer::Placed*>{
                  MenuButton::Kind::Back, levels ? &m_selectorRules.levels.back : &m_selectorRules.chapters.back},
              std::pair<MenuButton::Kind, const UiLayer::Placed*>{
                  MenuButton::Kind::Forward,
                  levels ? &m_selectorRules.levels.forward : &m_selectorRules.chapters.forward}}) {
            const Hud::Rect rect = UiLayer::RectAt(
                *placed, UiLayer::ButtonAnchorAt(m_selectorRules.layer, UiLayer::Anchor(*placed, box), box, kSettledMs));
            MenuButton button;
            button.kind = kind;
            button.world = m_selectorBoard.world;
            button.centrePx = rect.Centre();
            button.sizePx = rect.size;
            m_menuButtons.push_back(button);
        }
    }
}

void MagicPortalsLayer::buildMenu(entt::registry& registry) {
    using namespace Supersonic;
    unloadMenuDrawables(registry);
    if (m_screen == Screen::None) return;

    // An image that cannot be read SAYS SO. Skipping it quietly is what hid the
    // missing backgrounds: a menu with no background looks like a menu someone
    // designed that way, and nothing anywhere said the file had not been found.
    const auto quadFor = [this, &registry](const char* tag, const std::string& path) {
        if (imageSizePx(path).y > 0.0) return makeSprite(registry, tag, path, false);
        SUPERSONIC_LOG_WARN("Magic Portals") << "menu image could not be read: " << path << std::endl;
        return entt::entity{entt::null};
    };

    // The finished and lost screens put nothing in the registry: they are drawn
    // over their level through the screen overlay (EmitHud). Nor do the main
    // menu, credits and the dashboard, which EmitMenu draws, backgrounds and all,
    // in display values.
    if (m_screen == Screen::Finished || m_screen == Screen::Dead || m_screen == Screen::Main ||
        m_screen == Screen::Credits || m_screen == Screen::Achievements) {
        return;
    }
    if (m_screen == Screen::Loading) {
        // scenes/loading_screen.esc: its background, its character and its portal,
        // in the level's space as a level's art is. Sized by the 1x files, drawn
        // with the hd twins (sim/Loading.hpp; ui.json loading).
        m_loadingBg = quadFor("Magic Portals Loading Background", originalAsset(m_loadingRules.background.sprite));
        const std::string halo = originalImage(m_artRules.portal.sprite);
        if (imageSizePx(halo).y > 0.0) {
            m_loadingPortal = makeSprite(registry, "Magic Portals Loading Portal", halo, m_artRules.portal.additive);
        }
        // black_halo.ent under it, as the multiply it is (ui.json loading.portal).
        if (const std::string multiply = loadingHaloImage(); !multiply.empty()) {
            m_loadingHalo = makeSprite(registry, "Magic Portals Loading Halo", multiply, false);
        }
        // portal.ent's particle systems, the spiral and portal.png, just in front of
        // its halo and in the order the .ent states them.
        unloadEmitters(registry);
        m_loadingVanished = false;
        const glm::dvec2 portalAt = Loading::PortalAt(m_loadingRules, MenuBoxPx());
        addEntityEmitters(m_loadingRules.portalEntity, portalAt, 0.32f, 0.0);
        m_loadingPortalEmitters = m_emitters.size();
        const Art::Character& mage = m_artRules.character;
        const std::string sheet = originalAsset("entities/" + mage.sprite);
        if (imageSizePx(sheet).y > 0.0) {
            m_loadingCharacter = makeSprite(registry, "Magic Portals Loading Character", sheet, mage.additive);
            auto& animation = registry.emplace<SpriteAnimationComponent>(m_loadingCharacter);
            animation.columns = static_cast<uint32_t>(mage.columns);
            animation.rows = static_cast<uint32_t>(mage.rows);
            animation.firstFrame = static_cast<uint32_t>(m_loadingRules.firstFrame);
            animation.frameCount = 1;
            animation.playing = false; // the frame is the loop's (Loading::CharacterFrame)
        }
        return;
    }
    // Chapter select and the grid put nothing in the registry either: EmitMenu
    // draws them through the screen overlay (sim/Selector).
}

void MagicPortalsLayer::unloadMenuDrawables(entt::registry& registry) {
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };
    for (auto& e : m_menuQuads) destroy(e);
    m_menuQuads.clear();
    for (auto& e : m_menuLabels) destroy(e);
    m_menuLabels.clear();
    for (auto& e : m_menuMedals) destroy(e);
    m_menuMedals.clear();
    destroy(m_menuBg);
    destroy(m_menuTitle);
    destroy(m_loadingBg);
    destroy(m_loadingCharacter);
    destroy(m_loadingPortal);
    destroy(m_loadingHalo);
}

void MagicPortalsLayer::unloadMenu(entt::registry& registry) {
    unloadMenuDrawables(registry);
    m_menuButtons.clear();
}

void MagicPortalsLayer::openFinished(entt::registry& registry) {
    // setCurrentLayer('levelFinishedLayer') and playVictorySound.
    latch("medal_shown");
    // The screen's clock starts here, on the tick it becomes current, and its
    // counters from nothing: ScoreCounter(0, numPortals, 100) and the crystals'
    // own. Set once, here - a window resize lays the screen out again and must
    // not restart the count.
    m_end.clockMs = 0.0;
    if (m_lastCleared) {
        m_end.play.portalsUsed = m_lastCleared->portalsUsed;
        m_end.play.goldenScore = m_lastCleared->goldenScore;
        m_end.play.crystals = m_lastCleared->crystals;
        m_end.play.crystalsTotal = m_lastCleared->crystalsTotal;
    }
    m_end.portals = LevelEnd::Counter{0, m_end.play.portalsUsed, 0.0};
    m_end.crystals = LevelEnd::Counter{0, m_end.play.crystals, 0.0};
    // The level STAYS, and goes on running: neither end screen stops game time
    // (spec D7). The HUD it has left is laid out by layOutControls as a level
    // that has ended.
    m_screen = Screen::Finished;
    m_aspect = viewportAspect(registry);
    layOutMenu();
    SUPERSONIC_LOG_INFO("Magic Portals") << "finished screen current on tick " << m_ticks << std::endl;
}

void MagicPortalsLayer::openDead(entt::registry& registry) {
    // The SCREEN'S cue, and not the moment of death: checkGameEnd plays
    // playDeathSound here, where it raises levelLostLayer. A fall's own sound
    // played 1400 ms ago and an hp death made no sound at all.
    latch("player_died");
    m_end.clockMs = 0.0;
    // The level STAYS, and goes on running, as it does behind the medal.
    m_screen = Screen::Dead;
    m_aspect = viewportAspect(registry);
    layOutMenu();
    SUPERSONIC_LOG_INFO("Magic Portals") << "lost screen current on tick " << m_ticks << std::endl;
}

void MagicPortalsLayer::openMenu(entt::registry& registry, Screen screen) {
    // A level and a menu are never both in the registry.
    unloadLevel(registry);
    m_loaded = false;
    m_current = -1;
    m_chapterComplete = false;
    m_loadError.clear();
    // A STATE CHANGE opens under a black of its own, with every clock from
    // nothing (BaseState::preLoop). Another page of the same grid is not one.
    const bool changed = screen != m_screen;
    if (changed) beginMenuState();
    m_screen = screen;
    m_aspect = viewportAspect(registry);
    if (changed && screen == Screen::Credits) {
        // CreditsScreenLayer's constructor: the strip's top on the bottom edge.
        m_credits = Credits::Start(MenuBoxPx());
        m_creditsTouch = CreditsTouch{};
    }
    if (changed && screen == Screen::Achievements) {
        // ScoreDashboard::preLoop: a fresh list at the top, and what the save has
        // unlocked of it now.
        m_dashboardBoard = Dashboard::Build(m_achievementsLoaded ? &m_achievements : nullptr, m_lockingRules, m_scores,
                                            m_chapters);
        m_dashboardState = Dashboard::State{};
        for (const std::string* font :
             {&m_dashboardRules.header.font, &m_dashboardRules.title.font, &m_dashboardRules.description.font,
              &m_dashboardRules.points.font, &m_dashboardRules.total.font, &m_dashboardRules.newLabel.text.font}) {
            loadUiFont(*font);
        }
    }
    if (changed && (screen == Screen::Worlds || screen == Screen::Levels)) {
        // LevelSelector::preLoop: what the save has opened, and setCurrentPage from
        // page 0 to the opening page, which slides in under the black.
        m_selectorBoard = screen == Screen::Worlds
                              ? Selector::BuildChapters(m_selectorRules, m_lockingRules, m_scores, m_chapters)
                              : Selector::BuildLevels(m_selectorRules, m_lockingRules, m_scores, m_chapters, m_menuWorld);
        m_selectorState = Selector::Open(m_selectorBoard, Selector::OpeningPage(m_selectorBoard, m_menuWorld));
        m_selectorDown = Selector::Hit{};
        m_selectorHeld = Selector::Hit{};
        const Selector::Rules& selector = m_selectorRules;
        for (const std::string* font :
             {&selector.chapters.percent.font, &selector.chapters.warning.font, &selector.levels.number.font,
              &selector.levels.cornerChapter.font, &selector.levels.cornerPercent.font}) {
            loadUiFont(*font);
        }
    }
    layOutMenu();
    buildMenu(registry);
    if (screen == Screen::Loading) loadUiFont(m_loadingRules.dots.font);
}

bool MagicPortalsLayer::PressMenu(entt::registry& registry, MenuButton button) {
    // Every button makes a noise, and not the same one: the menu's own is the
    // only thing in the game that plays button.mp3, while the buttons a level
    // puts up - retry, next, the list - are all a teleport.
    switch (button.kind) {
    case MenuButton::Kind::Play:
    case MenuButton::Kind::World:
    case MenuButton::Kind::Level:
    case MenuButton::Kind::Back:
    case MenuButton::Kind::Forward:
        latch("menu_button");
        break;
    case MenuButton::Kind::Retry:
    case MenuButton::Kind::Next:
    case MenuButton::Kind::List:
        latch("level_button");
        break;
    }

    switch (button.kind) {
    case MenuButton::Kind::Play:
        openMenu(registry, Screen::Worlds);
        return true;
    case MenuButton::Kind::World:
        // WorldChooser::validateItem (owner ruling R3): a locked chapter does nothing.
        if (m_screen == Screen::Worlds &&
            !Locking::ChapterUnlocked(m_lockingRules, m_scores, m_chapters, button.world)) {
            return false;
        }
        m_menuWorld = button.world;
        m_menuPage = 0;
        openMenu(registry, Screen::Levels);
        return true;
    case MenuButton::Kind::Level: {
        if (button.level < 0 || button.level >= static_cast<int>(m_chapters.levels.size())) return false;
        // LevelChooser::validateItem: a locked level does nothing.
        const Chapters::Level& chosen = m_chapters.levels[static_cast<std::size_t>(button.level)];
        if (!Locking::LevelUnlocked(m_lockingRules, m_scores, m_chapters, chosen.world, chosen.index)) return false;
        unloadMenu(registry);
        m_screen = Screen::None;
        loadLevel(registry, button.level);
        return true;
    }
    case MenuButton::Kind::Back:
        // PageManager::update: the previous page, or from the first page a state
        // up - chapter select from the grid, the main menu from chapter select.
        if (m_screen != Screen::Worlds && m_screen != Screen::Levels) return false;
        if (m_selectorState.page > 0) {
            Selector::SetPage(m_selectorState, m_selectorState.page - 1);
            return true;
        }
        openMenu(registry, m_screen == Screen::Levels ? Screen::Worlds : Screen::Main);
        return true;
    case MenuButton::Kind::Forward:
        if (m_screen != Screen::Worlds && m_screen != Screen::Levels) return false;
        if (m_selectorState.page >= m_selectorBoard.Pages() - 1) return false;
        Selector::SetPage(m_selectorState, m_selectorState.page + 1);
        return true;
    case MenuButton::Kind::Retry:
        // The level is still loaded behind the medal; loadLevel rebuilds it.
        if (m_current < 0) return false;
        unloadMenu(registry);
        m_screen = Screen::None;
        loadLevel(registry, m_current);
        return true;
    case MenuButton::Kind::Next:
        if (m_current < 0) return false;
        unloadMenu(registry);
        m_screen = Screen::None;
        goTo(registry, m_chapters.Next(m_current));
        return true;
    case MenuButton::Kind::List:
        if (m_current >= 0) m_menuWorld = m_chapters.levels[static_cast<std::size_t>(m_current)].world;
        openMenu(registry, Screen::Levels);
        return true;
    }
    return false;
}

void MagicPortalsLayer::menuTick(entt::registry& registry, float fixedDelta) {
    using namespace Supersonic;
    // WHAT THE LAST TICK ASKED FOR, before anything else: a release, the loading
    // screen's hold or the back key. The original's setState swaps between
    // frames, so its release frame is drawn in the old state untinted and the new
    // state's black comes on the next (ui3 spec 0.4, A-S5).
    if (m_pendingMenu.kind != PendingMenu::Kind::None) {
        const PendingMenu pending = m_pendingMenu;
        m_pendingMenu = PendingMenu{};
        switch (pending.kind) {
        case PendingMenu::Kind::MainButton:
            PressMainMenu(registry, pending.main);
            break;
        case PendingMenu::Kind::Button:
            PressMenu(registry, pending.button);
            break;
        case PendingMenu::Kind::Screen:
            openMenu(registry, pending.screen);
            break;
        case PendingMenu::Kind::Achievement:
            openAchievement(registry, pending.world, pending.level);
            break;
        case PendingMenu::Kind::None:
            break;
        }
        // A level was entered: this was the tick it loaded on, and its first tick
        // is the next.
        if (m_screen == Screen::None || m_screen == Screen::Finished || m_screen == Screen::Dead) return;
    }

    // The state's clock: its first tick is its zero, and it goes on from there.
    if (m_menuClock.fresh) {
        m_menuClock.fresh = false;
    } else {
        ++m_menuClock.ticks;
        m_menuClock.ms += static_cast<double>(fixedDelta) * 1000.0;
    }

    m_aspect = viewportAspect(registry);
    layOutMenu(); // the window may have changed shape since the last tick
    const glm::dvec2 box = MenuBoxPx();

    // Only the menu screens: the finished and lost screens are over a running
    // level and never take this tick (OnFixedUpdate).
    if (m_camera != entt::null && registry.valid(m_camera)) {
        auto& camera = registry.get<CameraComponent>(m_camera);
        const glm::vec3 centre = Units::ToWorld(box.x * 0.5, box.y * 0.5);
        camera.position = glm::vec3(centre.x, centre.y, kCameraDistance);
        camera.aspect = m_aspect;
        camera.orthoHeight = Units::ToMetres(box.y);
        registry.get<TransformComponent>(m_camera).position = camera.position;
        // A screen is a cut, not a pan.
        if (auto* interpolated = registry.try_get<InterpolatedCameraComponent>(m_camera)) {
            interpolated->captured = false;
        }
    }

    if (m_screen == Screen::Loading) {
        loadingTick(registry);
        return;
    }
    if (m_screen == Screen::Main) {
        mainMenuInput(registry);
        return;
    }
    if (m_screen == Screen::Credits) {
        creditsInput(registry, fixedDelta);
        return;
    }
    if (m_screen == Screen::Achievements) {
        dashboardInput(registry);
        return;
    }

    if (m_screen == Screen::Worlds || m_screen == Screen::Levels) selectorInput(registry);
}

void MagicPortalsLayer::beginMenuState() {
    m_menuClock = MenuClock{};
    m_menuTouch = MenuTouch{};
    // SoundPanelLayer is built afresh with the state: its music switch comes in
    // with the rest, if the sound is on.
    m_mainMusicAddedMs = 0.0;
    m_mainMusicDismissedMs = -1.0;
}

void MagicPortalsLayer::loadingTick(entt::registry& registry) {
    // Nothing on the loading screen takes a touch; a DEV tap due now is spent.
    (void)touchThisTick(registry);
    const Loading::Rules& rules = m_loadingRules;
    const glm::dvec2 view = MenuBoxPx();
    // LoadingScreen::loop's frame: 1 on the state's first tick.
    const int frame = m_menuClock.ticks + 1;
    const double tickMs = static_cast<double>(kTick) * 1000.0;

    if (m_loadingBg != entt::null && registry.valid(m_loadingBg)) {
        placeSprite(registry, m_loadingBg, rules.background.centreOfScreen * view, rules.background.sizeUnits, -1.0f,
                    0.0f);
    }
    const glm::dvec2 portal = Loading::PortalAt(rules, view);
    if (m_loadingPortal != entt::null && registry.valid(m_loadingPortal)) {
        placeSprite(registry, m_loadingPortal, portal, imageSizePx(originalImage(m_artRules.portal.sprite)), 0.3f,
                    0.0f);
    }
    if (m_loadingHalo != entt::null && registry.valid(m_loadingHalo)) {
        // black_halo.ent, OVER the portal's halo and under its particles. The
        // decode adds it at the portal's z less 4, which Ethanon's draw hash would
        // sort under the halo; the recording says otherwise - launch_gold frame 183,
        // once the spiral is killed, is a black blob with no trace of the added
        // halo, and frames 74-155 show the spiral light on dark - so the port draws
        // what the frames show. Why is not settled.
        placeSprite(registry, m_loadingHalo, portal,
                    imageSizePx(m_paths.original + "/" + rules.haloSprite) * rules.haloScale, 0.31f, 0.0f);
    }
    if (!Loading::CharacterShown(rules, frame) && !m_loadingVanished) {
        // The frame the last texture loads: vanishEffect and killPortal.
        m_loadingVanished = true;
        for (std::size_t i = 0; i < m_loadingPortalEmitters && i < m_emitters.size(); ++i) m_emitters[i].killed = true;
        addEntityEmitters(rules.vanish.suckEntity, Loading::SuckAt(rules, view), 0.6f,
                          Loading::SuckAngleDeg(rules, view));
        addEntityEmitters(rules.vanish.sparklesEntity, Loading::SparklesAt(rules, view), 0.61f, 0.0);
    }
    if (m_loadingCharacter != entt::null && registry.valid(m_loadingCharacter)) {
        if (Loading::CharacterShown(rules, frame)) {
            const Art::Character& mage = m_artRules.character;
            const glm::dvec2 cell = imageSizePx(originalImage(mage.sprite)) / glm::dvec2(mage.columns, mage.rows);
            registry.get<Supersonic::SpriteAnimationComponent>(m_loadingCharacter).firstFrame =
                static_cast<uint32_t>(Loading::CharacterFrame(rules, frame, tickMs));
            // The image stands with its pivot on the entity, as the level's player.
            placeSprite(registry, m_loadingCharacter,
                        Loading::CharacterAt(rules, view, frame) - glm::dvec2(mage.pivotXPx, mage.pivotYPx), cell,
                        0.5f, 0.0f);
        } else {
            // ETHEntity::Hide, on the frame the last texture loads.
            registry.destroy(m_loadingCharacter);
            m_loadingCharacter = entt::null;
        }
    }
    if (Loading::HoldOver(rules, frame, tickMs)) {
        m_pendingMenu.kind = PendingMenu::Kind::Screen;
        m_pendingMenu.screen = Screen::Main;
        SUPERSONIC_LOG_INFO("Magic Portals") << "loading held on tick " << m_ticks << " (frame " << frame
                                             << "); the main menu is next" << std::endl;
    }
}

void MagicPortalsLayer::mainMenuInput(entt::registry& registry) {
    using Supersonic::Input;
    // OWNER RULING R2: the back key on the main menu leaves the game, as
    // MainMenuLayer::update does (GetKeyState(14), then Exit). The engine's
    // latch stops the run loop after this frame.
    if (Input::TickWasPressed(kBack) || devPressDue(DevPress::Back)) {
        SUPERSONIC_LOG_INFO("Magic Portals") << "back key on the main menu on tick " << m_ticks << ": quitting"
                                             << std::endl;
        Supersonic::Application::RequestQuit();
        return;
    }
    const Touch touch = touchThisTick(registry);
    const glm::dvec2 view = MenuBoxPx();
    const Pause::Switches switches = MainMenuSwitches();
    if (touch.pressed) {
        MenuState::TouchDown(m_menuTouch.touch, touch.atView);
        m_menuTouch.mainDown =
            MainMenu::ButtonsAt(m_mainMenuRules, switches, view, m_menuClock.ms, touch.atView);
    }
    const unsigned inside =
        touch.over ? MainMenu::ButtonsAt(m_mainMenuRules, switches, view, m_menuClock.ms, touch.atView) : 0u;
    m_menuTouch.mainHeld = touch.held && !touch.released ? (m_menuTouch.mainDown & inside) : 0u;
    if (!touch.released) return;
    const unsigned pressed = m_menuTouch.mainDown & inside;
    m_menuTouch = MenuTouch{};
    const std::optional<MainMenu::Button> first = MainMenu::FirstActed(pressed);
    if (!first) return;
    if (*first == MainMenu::Button::Sound || *first == MainMenu::Button::Music) {
        // A switch changes on its own update, so on the release frame itself.
        PressMainMenu(registry, *first);
        return;
    }
    m_pendingMenu.kind = PendingMenu::Kind::MainButton;
    m_pendingMenu.main = *first;
}

void MagicPortalsLayer::selectorInput(entt::registry& registry) {
    using Supersonic::Input;
    const Selector::Rules& rules = m_selectorRules;
    // The back key goes up a state, on the next tick as a release does: from
    // either page of the grid to chapter select, and from chapter select to the
    // main menu (LevelSelector::loop, ui3 spec 5.6, 6.6).
    if (Input::TickWasPressed(kBack) || devPressDue(DevPress::Back)) {
        m_menuTouch = MenuTouch{};
        m_selectorDown = Selector::Hit{};
        m_selectorHeld = Selector::Hit{};
        m_pendingMenu.kind = PendingMenu::Kind::Screen;
        m_pendingMenu.screen = m_screen == Screen::Levels ? Screen::Worlds : Screen::Main;
        return;
    }
    const Touch touch = touchThisTick(registry);
    const glm::dvec2 view = MenuBoxPx();
    const auto same = [](const Selector::Hit& a, const Selector::Hit& b) {
        return a.control == b.control && a.item == b.item;
    };
    if (touch.pressed) {
        MenuState::TouchDown(m_menuTouch.touch, touch.atView);
        m_selectorDown =
            Selector::HitAt(rules, m_selectorBoard, m_selectorState, view, m_menuClock.ms, touch.atView);
        Selector::TouchDown(m_selectorState, touch.atView.x);
    }
    if (touch.held || touch.released) {
        MenuState::TouchHeld(m_menuTouch.touch, touch.atView);
        Selector::TouchMove(m_selectorState, touch.atView.x, view.x);
    }
    const Selector::Hit on = touch.over ? Selector::HitAt(rules, m_selectorBoard, m_selectorState, view,
                                                          m_menuClock.ms, touch.atView)
                                        : Selector::Hit{};
    const bool inside = m_selectorDown.control != Selector::Control::None && same(on, m_selectorDown);
    m_selectorHeld = inside && touch.held && !touch.released ? m_selectorDown : Selector::Hit{};
    if (touch.released) {
        const Selector::Hit down = m_selectorDown;
        // Page::update's TouchGapDetector: a tile refuses a finger that travelled.
        const bool takes =
            down.control != Selector::Control::Item || MenuState::TileTakes(rules.state, m_menuTouch.touch);
        Selector::TouchUp(rules, m_selectorBoard, m_selectorState);
        m_menuTouch = MenuTouch{};
        m_selectorDown = Selector::Hit{};
        if (inside && takes) {
            MenuButton button;
            button.world = m_selectorBoard.world;
            switch (down.control) {
            case Selector::Control::Back:
                button.kind = MenuButton::Kind::Back;
                break;
            case Selector::Control::Forward:
                button.kind = MenuButton::Kind::Forward;
                break;
            case Selector::Control::Item:
                if (m_selectorBoard.levels) {
                    button.kind = MenuButton::Kind::Level;
                    button.level = m_selectorBoard.tiles[static_cast<std::size_t>(down.item)].level;
                } else {
                    button.kind = MenuButton::Kind::World;
                    button.world = down.item;
                }
                break;
            case Selector::Control::None:
                break;
            }
            const bool pageTurn =
                (button.kind == MenuButton::Kind::Back && m_selectorState.page > 0) ||
                button.kind == MenuButton::Kind::Forward;
            if (down.control != Selector::Control::None) {
                if (pageTurn) {
                    // A page of the same state: drawn from the release frame.
                    PressMenu(registry, button);
                } else {
                    m_pendingMenu.kind = PendingMenu::Kind::Button;
                    m_pendingMenu.button = button;
                }
            }
        }
    }
    Selector::Step(rules, m_selectorBoard, m_selectorState);
}

void MagicPortalsLayer::creditsInput(entt::registry& registry, float fixedDelta) {
    using Supersonic::Input;
    // BackButtonLayer::isBackButtonPressed's key: the main menu, on the next tick.
    if (Input::TickWasPressed(kBack) || devPressDue(DevPress::Back)) {
        m_creditsTouch = CreditsTouch{};
        m_pendingMenu.kind = PendingMenu::Kind::Screen;
        m_pendingMenu.screen = Screen::Main;
        return;
    }
    const Touch touch = touchThisTick(registry);
    const glm::dvec2 view = MenuBoxPx();
    const double ms = m_menuClock.ms;
    // GetTouchState 1 or 2: pressed this tick, or still held.
    const bool touching = touch.pressed || (touch.held && !touch.released);
    if (touch.pressed) {
        m_creditsTouch.down = true;
        m_creditsTouch.lastAt = touch.atView;
        m_creditsTouch.downOnBack = Credits::BackHitRect(m_creditsRules, view, ms).Contains(touch.atView);
    }
    // GetTouchMove: how far it went since the last tick, nothing on the tick it went down.
    double move = 0.0;
    if (touching) {
        move = touch.atView.y - m_creditsTouch.lastAt.y;
        m_creditsTouch.lastAt = touch.atView;
    }
    // The state's first tick is where the constructor put the strip; it moves from
    // the next, a tick's worth each.
    if (m_menuClock.ticks > 0) {
        Credits::Step(m_creditsRules, m_credits, view, static_cast<double>(fixedDelta) * 1000.0, touching, move);
    }
    const bool inside = touch.over && Credits::BackHitRect(m_creditsRules, view, ms).Contains(touch.atView);
    m_creditsTouch.backHeld = m_creditsTouch.down && m_creditsTouch.downOnBack && touching && inside;
    if (!touch.released) return;
    const bool pressed = m_creditsTouch.down && m_creditsTouch.downOnBack && inside;
    m_creditsTouch = CreditsTouch{};
    if (!pressed) return;
    // getButtonSoundName, and the menu state on the next tick.
    latch("menu_button");
    m_pendingMenu.kind = PendingMenu::Kind::Screen;
    m_pendingMenu.screen = Screen::Main;
}

void MagicPortalsLayer::dashboardInput(entt::registry& registry) {
    using Supersonic::Input;
    // DashboardLayer::update: the back key goes where the back button goes.
    if (Input::TickWasPressed(kBack) || devPressDue(DevPress::Back)) {
        m_pendingMenu.kind = PendingMenu::Kind::Screen;
        m_pendingMenu.screen = Screen::Main;
        return;
    }
    const Touch touch = touchThisTick(registry);
    Dashboard::Touch input;
    input.pressed = touch.pressed;
    input.held = touch.held;
    input.released = touch.released;
    input.over = touch.over;
    input.at = touch.atView;
    input.wheelNotches = static_cast<double>(Input::TickScroll());
    const Dashboard::Outcome outcome =
        Dashboard::Tick(m_dashboardRules, m_dashboardBoard, m_dashboardState, MenuBoxPx(), m_menuClock.ms, input);
    if (outcome.pick) latch("achievement_pick");
    // displayLevelLockedNotification's placement is not decoded (spec U2): the
    // denial is heard, and nothing is drawn for it.
    if (outcome.denied) latch("achievement_denied");
    if (outcome.back) {
        latch("menu_button");
        m_pendingMenu.kind = PendingMenu::Kind::Screen;
        m_pendingMenu.screen = Screen::Main;
        return;
    }
    if (outcome.start) {
        // getStartAchievementSoundName, and openState on the next tick.
        latch("level_button");
        m_pendingMenu.kind = PendingMenu::Kind::Achievement;
        m_pendingMenu.world = outcome.world;
        m_pendingMenu.level = outcome.level;
    }
}

void MagicPortalsLayer::openAchievement(entt::registry& registry, int world, int level) {
    // openState (bytes 312702..312880): the level, where there is one and it is
    // open; otherwise the chapter's grid, where the chapter is open.
    if (world >= 0 && level >= 0) {
        if (!Locking::LevelUnlocked(m_lockingRules, m_scores, m_chapters, world, level)) return;
        for (std::size_t i = 0; i < m_chapters.levels.size(); ++i) {
            const Chapters::Level& entry = m_chapters.levels[i];
            if (entry.world != world || entry.index != level) continue;
            unloadMenu(registry);
            m_screen = Screen::None;
            loadLevel(registry, static_cast<int>(i));
            return;
        }
        return;
    }
    if (world >= 0 && Locking::ChapterUnlocked(m_lockingRules, m_scores, m_chapters, world)) {
        m_menuWorld = world;
        m_menuPage = 0;
        openMenu(registry, Screen::Levels);
    }
}

bool MagicPortalsLayer::menuStateUp() const {
    switch (m_screen) {
    case Screen::Loading:
    case Screen::Main:
    case Screen::Worlds:
    case Screen::Levels:
    case Screen::Credits:
    case Screen::Achievements:
        return true;
    case Screen::None:
    case Screen::Finished:
    case Screen::Dead:
        return false;
    }
    return false;
}

const std::string& MagicPortalsLayer::menuPicture(const std::string& file) const {
    static const std::string none;
    const auto found = m_menuPictures.find(file);
    return found != m_menuPictures.end() ? found->second : none;
}

void MagicPortalsLayer::loadUiFont(const std::string& name) {
    if (name == m_hudRules.caption.font) {
        if (m_captionFontTried) return;
        m_captionFontTried = true;
        std::string why;
        if (!m_captionFont.Load(m_paths.original + "/data/" + name, why)) {
            SUPERSONIC_LOG_WARN("Magic Portals") << "no text in " << name << ": " << why << std::endl;
        }
        return;
    }
    if (m_uiFonts.find(name) != m_uiFonts.end()) return;
    std::string why;
    if (!m_uiFonts[name].Load(m_paths.original + "/data/" + name, why)) {
        SUPERSONIC_LOG_WARN("Magic Portals") << "no text in " << name << ": " << why << std::endl;
    }
}

Pause::Switches MagicPortalsLayer::MainMenuSwitches() const {
    Pause::Switches switches;
    switches.soundOn = m_soundOn;
    switches.musicOn = m_musicOn;
    switches.musicAddedMs = m_mainMusicAddedMs;
    switches.musicDismissedMs = m_mainMusicDismissedMs;
    return switches;
}

bool MagicPortalsLayer::PressMainMenu(entt::registry& registry, MainMenu::Button button) {
    if (m_screen != Screen::Main) return false;
    switch (button) {
    case MainMenu::Button::Play:
    case MainMenu::Button::Title:
        // MainMenuLayer::update's play_button and PortalMainMenu::loop's title:
        // createLevelSelectState, which is chapter select from the menu.
        latch("menu_button");
        openMenu(registry, Screen::Worlds);
        return true;
    case MainMenu::Button::Credits:
        // PortalMainMenu::loop: the CreditsScreen state (ui3 spec 3), with
        // getItemSelectButtonSoundName, which sounds.json maps with the level's own
        // buttons.
        latch("level_button");
        openMenu(registry, Screen::Credits);
        return true;
    case MainMenu::Button::Achievements:
        // And the ScoreDashboard state (ui3 spec 4), with the same noise.
        latch("level_button");
        openMenu(registry, Screen::Achievements);
        return true;
    case MainMenu::Button::Sound: {
        // GlobalSoundSwitch::manageSoundSwitch, and SoundPanelLayer's music switch
        // dismissed while the sound is off and added afresh when it is back: the
        // pause's arithmetic on this state's clock.
        const double now = m_menuClock.ms;
        const double dismissMs = m_mainMenuRules.layer.buttonDismissMs;
        const bool dismissing = m_mainMusicDismissedMs >= 0.0 && now - m_mainMusicDismissedMs < dismissMs;
        m_soundOn = !m_soundOn;
        if (!m_soundOn) {
            if (!dismissing && now >= m_mainMusicAddedMs) m_mainMusicDismissedMs = now;
        } else {
            m_mainMusicAddedMs = dismissing ? m_mainMusicDismissedMs + dismissMs : now;
        }
        latch("menu_button");
        return true;
    }
    case MainMenu::Button::Music: {
        const bool shown = m_soundOn && m_menuClock.ms >= m_mainMusicAddedMs &&
                           !(m_mainMusicDismissedMs >= 0.0 &&
                             m_menuClock.ms - m_mainMusicDismissedMs < m_mainMenuRules.layer.buttonDismissMs);
        if (!shown) return false;
        m_musicOn = !m_musicOn;
        latch("menu_button");
        return true;
    }
    }
    return false;
}

void MagicPortalsLayer::EmitMenu(entt::registry& registry) const {
    using Supersonic::ScreenOverlay;
    auto* const* slot = registry.ctx().find<ScreenOverlay*>();
    if (slot == nullptr || *slot == nullptr) return;
    ScreenOverlay& overlay = **slot;
    if (!m_hudReady || !menuStateUp()) return;
    const glm::dvec2 view = MenuBoxPx();
    if (view.x <= 0.0 || view.y <= 0.0) return;

    const auto add = [&overlay, &view](const Hud::Rect& rect, const std::string& image, const glm::vec4& colour,
                                       const glm::dvec2& uvMin = glm::dvec2(0.0),
                                       const glm::dvec2& uvMax = glm::dvec2(1.0)) {
        if (colour.a <= 0.0f) return;
        ScreenOverlay::Quad quad;
        quad.min = glm::vec2(rect.min / view);
        quad.max = glm::vec2(rect.Max() / view);
        quad.uvMin = glm::vec2(uvMin);
        quad.uvMax = glm::vec2(uvMax);
        quad.color = colour;
        quad.texture = image;
        overlay.Add(std::move(quad));
    };
    const auto write = [&](const std::string& fontName, const std::string& words, const glm::dvec2& centre,
                           double unitsPerFontPx, double alpha) {
        const Supersonic::BitmapFont* font = uiFont(fontName);
        if (font == nullptr) return;
        const auto& pages = font->Pages();
        for (const Hud::Glyph& glyph : Hud::LayOutText(*font, words, centre, unitsPerFontPx)) {
            if (glyph.page < 0 || static_cast<std::size_t>(glyph.page) >= pages.size()) continue;
            add(glyph.rect, pages[static_cast<std::size_t>(glyph.page)],
                glm::vec4(1.0f, 1.0f, 1.0f, static_cast<float>(alpha)), glyph.uvOffset,
                glyph.uvOffset + glyph.uvScale);
        }
    };
    const Hud::Rect whole{glm::dvec2(0.0), view};
    const float black =
        static_cast<float>(MenuState::FadeAlphaByte(m_mainMenuRules.state, m_menuClock.ms)) / 255.0f;

    if (m_screen == Screen::Main) {
        // The background, then the layer's buttons in the order they were added,
        // each at its bounce, bob, press tint, blink and entrance (sim/MainMenu).
        for (const MainMenu::Piece& piece :
             MainMenu::Pieces(m_mainMenuRules, MainMenuSwitches(), view, m_menuClock.ms, m_layerClockMs,
                              m_menuTouch.mainHeld)) {
            const std::string& image = menuPicture(piece.file);
            if (image.empty()) continue;
            const float grey = static_cast<float>(piece.rgbByte) / 255.0f;
            add(piece.rect, image, glm::vec4(grey, grey, grey, static_cast<float>(piece.alphaByte) / 255.0f));
        }
        // The state's black, drawn after the layer manager: over all of it.
        add(whole, std::string(), glm::vec4(0.0f, 0.0f, 0.0f, black));
        return;
    }
    if (m_screen == Screen::Credits) {
        // The background, the back button (UILayer::draw), then the papyrus and the
        // strip (CreditsScreenLayer::draw), and the state's black over all of it.
        for (const Credits::Piece& piece :
             Credits::Pieces(m_creditsRules, view, m_menuClock.ms, m_credits, m_creditsTouch.backHeld)) {
            const std::string& image = menuPicture(piece.file);
            if (image.empty()) continue;
            const float grey = static_cast<float>(piece.rgbByte) / 255.0f;
            add(piece.rect, image, glm::vec4(grey, grey, grey, static_cast<float>(piece.alphaByte) / 255.0f));
        }
        add(whole, std::string(), glm::vec4(0.0f, 0.0f, 0.0f, black));
        return;
    }
    if (m_screen == Screen::Achievements) {
        // Every picture and word in ScoreDashboard::loop's order (sim/Dashboard.hpp),
        // then the state's black.
        for (const Dashboard::Piece& piece : Dashboard::Pieces(m_dashboardRules, m_dashboardBoard, m_dashboardState,
                                                               view, m_menuClock.ms, m_layerClockMs)) {
            const glm::vec4 colour(glm::vec3(piece.rgbBytes) / 255.0f, static_cast<float>(piece.alphaByte) / 255.0f);
            if (piece.kind == Dashboard::Kind::Sprite) {
                const std::string& image = menuPicture(piece.file);
                if (!image.empty()) add(piece.rect, image, colour);
                continue;
            }
            const Supersonic::BitmapFont* font = uiFont(piece.file);
            if (font == nullptr) continue;
            const auto& pages = font->Pages();
            // drawCenteredText centres the summed box; DrawText draws from its top-left.
            const std::vector<Hud::Glyph> glyphs =
                piece.centred ? Hud::LayOutText(*font, piece.words, piece.at, piece.unitsPerFontPx)
                              : Hud::LayOutTextFrom(*font, piece.words, piece.at, piece.unitsPerFontPx);
            for (const Hud::Glyph& glyph : glyphs) {
                if (glyph.page < 0 || static_cast<std::size_t>(glyph.page) >= pages.size()) continue;
                add(glyph.rect, pages[static_cast<std::size_t>(glyph.page)], colour, glyph.uvOffset,
                    glyph.uvOffset + glyph.uvScale);
            }
        }
        add(whole, std::string(), glm::vec4(0.0f, 0.0f, 0.0f, black));
        return;
    }
    if (m_screen == Screen::Loading) {
        // BaseState::loop draws the black first; LoadingScreen::loop then writes
        // the dots, while it is still loading, and the logo on every frame.
        add(whole, std::string(), glm::vec4(0.0f, 0.0f, 0.0f, black));
        const Loading::Rules& rules = m_loadingRules;
        const int frame = m_menuClock.ticks + 1;
        if (Loading::IsLoading(rules, frame)) {
            const glm::dvec2 at = rules.dots.centreOfScreen * view;
            write(rules.dots.font, Loading::DotsText(rules, Loading::LoadedBefore(rules, frame)), at,
                  rules.dots.unitsPerFontPx, 1.0);
            write(rules.dots.font, Loading::TrackText(rules), at, rules.dots.unitsPerFontPx,
                  static_cast<double>(rules.dots.trackAlphaByte) / 255.0);
        }
        if (const std::string& logo = menuPicture(rules.logo.sprite); !logo.empty()) {
            add(UiLayer::RectAt(rules.logo, UiLayer::Anchor(rules.logo, view)), logo, glm::vec4(1.0f));
        }
        return;
    }
    if (m_screen == Screen::Worlds || m_screen == Screen::Levels) {
        // Every picture and word in the original's order (sim/Selector.hpp), then the
        // state's black.
        for (const Selector::Piece& piece :
             Selector::Pieces(m_selectorRules, m_selectorBoard, m_selectorState, view, m_menuClock.ms, m_layerClockMs,
                              m_selectorHeld.control, m_selectorHeld.item)) {
            const glm::vec4 colour(glm::vec3(piece.rgbBytes) / 255.0f, static_cast<float>(piece.alphaByte) / 255.0f);
            if (piece.kind == Selector::Kind::Sprite) {
                const std::string& image = menuPicture(piece.file);
                if (!image.empty()) add(piece.rect, image, colour, piece.uvMin, piece.uvMax);
                continue;
            }
            const Supersonic::BitmapFont* font = uiFont(piece.file);
            if (font == nullptr) continue;
            const auto& pages = font->Pages();
            const std::vector<Hud::Glyph> glyphs =
                piece.centred ? Hud::LayOutText(*font, piece.words, piece.at, piece.unitsPerFontPx)
                              : Hud::LayOutTextFrom(*font, piece.words, piece.at, piece.unitsPerFontPx);
            for (const Hud::Glyph& glyph : glyphs) {
                if (glyph.page < 0 || static_cast<std::size_t>(glyph.page) >= pages.size()) continue;
                add(glyph.rect, pages[static_cast<std::size_t>(glyph.page)], colour, glyph.uvOffset,
                    glyph.uvOffset + glyph.uvScale);
            }
        }
    }
    add(whole, std::string(), glm::vec4(0.0f, 0.0f, 0.0f, black));
}

// ---- the camera -------------------------------------------------------------

float MagicPortalsLayer::viewportAspect(const entt::registry& registry) const {
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport == nullptr) return kDefaultAspect;
    const glm::vec2 size = viewport->Size();
    return size.x > 0.0f && size.y > 0.0f ? size.x / size.y : kDefaultAspect;
}

void MagicPortalsLayer::buildCamera(entt::registry& registry) {
    using namespace Supersonic;
    m_camera = registry.create();
    registry.emplace<TagComponent>(m_camera, "Magic Portals Camera");
    auto& camera = registry.emplace<CameraComponent>(m_camera);
    camera.projection = CameraComponent::Projection::Orthographic;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    camera.isPrimary = true;
    camera.flyControlsEnabled = false;
    // Looking down -z at the plane the level lies in, with the engine's +y up
    // the screen. Where it looks is the tick's (placeCamera).
    camera.yaw = -90.0f;
    camera.pitch = 0.0f;
    camera.updateCameraVectors();
    camera.aspect = kDefaultAspect;
    registry.emplace<TransformComponent>(m_camera);
    // Moved on the tick and drawn between ticks, as HUSK's is.
    registry.emplace<InterpolatedCameraComponent>(m_camera);

    m_light = registry.create();
    registry.emplace<TagComponent>(m_light, "Magic Portals Light");
    auto& light = registry.emplace<LightComponent>(m_light);
    light.type = 0;
    light.direction = glm::vec3(0.35f, 0.6f, 1.0f);
    light.intensity = 1.3f;
}

void MagicPortalsLayer::placeCamera(entt::registry& registry) {
    if (m_camera == entt::null || !registry.valid(m_camera)) return;
    auto& camera = registry.get<Supersonic::CameraComponent>(m_camera);
    const glm::vec3 centre = Units::ToWorld(m_follow.centrePx.x, m_follow.centrePx.y);
    camera.position = glm::vec3(centre.x, centre.y, kCameraDistance);
    camera.aspect = m_aspect;
    camera.orthoHeight = Units::ToMetres(m_viewHeightPx);
    registry.get<Supersonic::TransformComponent>(m_camera).position = camera.position;
}

bool MagicPortalsLayer::ScreenToLevelPx(const entt::registry& registry, const glm::vec2& screenPoint,
                                        glm::dvec2& outPx) const {
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    // Not gated on a level being loaded: the menu is drawn in this same plane
    // and clicked through this same mapping.
    if (viewport == nullptr || m_camera == entt::null || !registry.valid(m_camera)) return false;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return false;
    // The camera as THIS viewport would draw it, whatever shape it last had.
    Supersonic::CameraComponent camera = registry.get<Supersonic::CameraComponent>(m_camera);
    camera.aspect = size.x / size.y;
    const Supersonic::Ray ray = Supersonic::Raycast::ScreenPointToRay(viewport->ToLocal(screenPoint), size, camera);
    // The level is the plane z = 0. The ray is met there, rather than trusting
    // its origin to lie on it.
    if (std::fabs(ray.direction.z) < 1e-6f) return false;
    const float t = -ray.origin.z / ray.direction.z;
    outPx = Units::ToPixels(ray.origin + ray.direction * t);
    return true;
}

// ---- the picture ------------------------------------------------------------

entt::entity MagicPortalsLayer::makeBox(entt::registry& registry, const char* tag, const glm::vec3& centre,
                                        const glm::vec3& size, const glm::vec3& colour) {
    using namespace Supersonic;
    const entt::entity e = registry.create();
    registry.emplace<TagComponent>(e, tag);
    auto& transform = registry.emplace<TransformComponent>(e);
    transform.position = centre;
    transform.scale = size; // the box primitive is one unit on a side
    // "Box", not "Cube": the cube is the engine's rainbow debug primitive.
    registry.emplace<MeshComponent>(e).primitiveType = "Box";
    auto& material = registry.emplace<MaterialComponent>(e);
    material.albedoColor = glm::vec4(colour, 1.0f);
    material.roughness = 0.85f;
    material.metallic = 0.0f;
    registry.emplace<RenderableComponent>(e);
    return e;
}

void MagicPortalsLayer::placeBox(entt::registry& registry, entt::entity box, const glm::dvec2& centrePx,
                                 const glm::dvec2& sizePx, float z, float depth, float rotation) const {
    auto& transform = registry.get<Supersonic::TransformComponent>(box);
    const glm::vec3 centre = Units::ToWorld(centrePx.x, centrePx.y);
    transform.position = glm::vec3(centre.x, centre.y, z);
    transform.scale = glm::vec3(Units::ToMetres(sizePx.x), Units::ToMetres(sizePx.y), depth);
    transform.rotation = glm::vec3(0.0f, 0.0f, rotation);
}

void MagicPortalsLayer::buildDrawables(entt::registry& registry) {
    using namespace Supersonic;
    for (const auto& [name, entity] : m_level.built.entities) {
        const Tscn::Node* node = m_data.scene.FindNode(name);
        if (node == nullptr || IsTrigger(registry, entity)) continue;
        Drawn drawn;
        if (!LevelBuilder::ShapeBoundsPx(m_data.scene, *node, drawn.offsetPx, drawn.sizePx)) continue;
        drawn.body = entity;
        glm::vec3 colour = kStaticColour;
        drawn.depth = 0.6f;
        if (const auto* rigid = registry.try_get<RigidBodyComponent>(entity)) {
            drawn.depth = 0.4f;
            if (rigid->isKinematic) {
                colour = kDoorColour;
            } else {
                const bool travels = std::find(m_level.portals.travellers.begin(), m_level.portals.travellers.end(),
                                               entity) != m_level.portals.travellers.end();
                colour = travels ? kCrateColour : kFixedCrateColour;
            }
        }
        // What a stone breaks, and the stones, told apart from the rest.
        const Demolish::State& demolish = m_level.demolish;
        if (std::any_of(demolish.breakables.begin(), demolish.breakables.end(),
                        [entity](const Demolish::Breakable& b) { return b.body == entity; })) {
            colour = kBreakableColour;
        } else if (std::any_of(demolish.stones.begin(), demolish.stones.end(),
                               [entity](const Demolish::Stone& s) { return s.body == entity; })) {
            colour = kStoneColour;
        }
        drawn.box = makeBox(registry, "Magic Portals Body", glm::vec3(0.0f), glm::vec3(1.0f), colour);
        // Bodies move on the tick and are drawn between ticks.
        registry.emplace<InterpolatedTransformComponent>(drawn.box);
        m_bodies.push_back(drawn);
    }

    m_player = makeBox(registry, "Magic Portals Player", glm::vec3(0.0f), glm::vec3(1.0f), kPlayerColour);
    registry.emplace<InterpolatedTransformComponent>(m_player);

    for (const Puzzle::Button& button : m_level.channels.buttons) {
        (void)button;
        m_buttons.push_back(makeBox(registry, "Magic Portals Button", glm::vec3(0.0f), glm::vec3(1.0f), kButtonUpColour));
    }
    for (const Goals::Crystal& crystal : m_level.goals.crystals) {
        (void)crystal;
        m_crystals.push_back(makeBox(registry, "Magic Portals Crystal", glm::vec3(0.0f), glm::vec3(1.0f), kCrystalColour));
    }
    m_exit = makeBox(registry, "Magic Portals Exit", glm::vec3(0.0f), glm::vec3(1.0f), kExitColour);
    for (const Portals::Static& portal : m_level.portals.statics) {
        m_statics.push_back(makeBox(registry, "Magic Portals Static Portal", glm::vec3(0.0f), glm::vec3(1.0f),
                                    portal.colour == "red" ? kStaticRedColour : kStaticBlueColour));
    }
    for (std::size_t i = 0; i < m_level.portals.zones.size(); ++i) {
        m_zones.push_back(makeBox(registry, "Magic Portals No-Portal Zone", glm::vec3(0.0f), glm::vec3(1.0f), kZoneColour));
    }
    for (std::size_t i = 0; i < m_level.hazards.hazards.size(); ++i) {
        m_hazards.push_back(makeBox(registry, "Magic Portals Hazard", glm::vec3(0.0f), glm::vec3(1.0f), kHazardColour));
    }
    buildSprites(registry);

    // The player, as dark_mage.ent draws it, facing as its start frame faces
    // (Art.hpp): each level starts on it.
    const Art::Character& mage = m_artRules.character;
    const std::string sheet = originalImage(mage.sprite);
    m_facingRight = mage.startFrame / mage.columns == mage.rightRow;
    m_direction = 0.0f;
    if (m_artReady && imageSizePx(sheet) != glm::dvec2(0.0)) {
        m_playerQuad = makeSprite(registry, "Magic Portals Player Sprite", sheet, mage.additive);
        registry.emplace<InterpolatedTransformComponent>(m_playerQuad);
        auto& animation = registry.emplace<SpriteAnimationComponent>(m_playerQuad);
        animation.columns = static_cast<uint32_t>(mage.columns);
        animation.rows = static_cast<uint32_t>(mage.rows);
        animation.framesPerSecond = static_cast<float>(mage.framesPerSecond);
        animation.firstFrame = static_cast<uint32_t>(mage.startFrame);
        animation.frameCount = 1;
        animation.playing = false;
    }
}

// ---- the level's art ----------------------------------------------------------

entt::entity MagicPortalsLayer::makeSprite(entt::registry& registry, const char* tag, const std::string& texture,
                                           bool additive) {
    using namespace Supersonic;
    const entt::entity e = registry.create();
    registry.emplace<TagComponent>(e, tag);
    registry.emplace<TransformComponent>(e);
    registry.emplace<MeshComponent>(e).primitiveType = "Quad";
    auto& material = registry.emplace<MaterialComponent>(e);
    // Unlit, as the remake draws its canvas: the engine's PBR lights never reach
    // it. What lights a level sprite is its 2D record, written by tint.
    material.unlit = true;
    material.transparent = true;
    material.blend = additive ? MaterialComponent::BlendMode::Additive : MaterialComponent::BlendMode::Alpha;
    material.albedoTexturePath = texture;
    registry.emplace<RenderableComponent>(e).castsShadow = false;
    return e;
}

void MagicPortalsLayer::placeSprite(entt::registry& registry, entt::entity quad, const glm::dvec2& centrePx,
                                    const glm::dvec2& sizePx, float z, float rotation) const {
    auto& transform = registry.get<Supersonic::TransformComponent>(quad);
    const glm::vec3 centre = Units::ToWorld(centrePx.x, centrePx.y);
    transform.position = glm::vec3(centre.x, centre.y, z);
    // The quad primitive is one unit on a side, facing the camera.
    transform.scale = glm::vec3(Units::ToMetres(sizePx.x), Units::ToMetres(sizePx.y), 1.0f);
    transform.rotation = glm::vec3(0.0f, 0.0f, rotation);
}

glm::dvec2 MagicPortalsLayer::imageSizePx(const std::string& path) {
    if (const auto known = m_imageSizes.find(path); known != m_imageSizes.end()) return known->second;
    int width = 0;
    int height = 0;
    std::string error;
    const glm::dvec2 size =
        Sprites::ImageSize(path, width, height, error) ? glm::dvec2(width, height) : glm::dvec2(0.0);
    m_imageSizes.emplace(path, size);
    return size;
}

std::string MagicPortalsLayer::originalImage(const std::string& sprite) const {
    return m_paths.original + "/entities/" + sprite;
}

void MagicPortalsLayer::buildSprites(entt::registry& registry) {
    using namespace Supersonic;
    std::vector<Sprites::Sprite> sprites;
    m_artError.clear();
    m_sign = NoPortalSign{};
    if (!Sprites::Find(m_data.scene, m_paths.art, sprites, m_artError)) {
        // Played anyway, as boxes: the art is the original's, and a machine
        // without it can still play the port.
        SUPERSONIC_LOG_WARN("Magic Portals") << "Drawing the level as boxes: " << m_artError << std::endl;
        m_playerSlot = 0;
        return;
    }
    // THE NO-PORTAL SIGN IS NOT DRAWN WHERE THE LEVEL PUTS IT. Its node sits off
    // the level at (-61, -24), and its script eases it to the camera's corner
    // every frame (ui.json, hud.no_portal_sign), so it is taken out of the
    // level's sprites and drawn with the HUD. Found by its entity name, as
    // PortalManager's SeekEntity finds it.
    if (m_hudReady && !m_hudRules.sign.entity.empty()) {
        for (auto it = sprites.begin(); it != sprites.end(); ++it) {
            const Tscn::Node* node = m_data.scene.FindNode(it->node);
            if (node == nullptr || Roles::EntityName(*node) != m_hudRules.sign.entity) continue;
            m_sign.present = true;
            m_sign.sizeUnits = it->sizePx;
            m_sign.follow.at = Sprites::CentrePx(*it);
            // The hd twin, which the original draws on any screen over 480 px
            // and which the captures match (0.977 against 0.818 for the 1x art
            // at twice its size). The converter copies only the 1x.
            const std::string hd =
                m_paths.original + "/entities/hd/" + std::filesystem::path(it->texture).filename().string();
            std::error_code ec;
            m_sign.image = std::filesystem::exists(hd, ec) ? hd : it->texture;
            sprites.erase(it);
            break;
        }
    }
    m_playerSlot = static_cast<int>(std::count_if(sprites.begin(), sprites.end(),
                                                  [](const Sprites::Sprite& s) { return s.zIndex <= 0; }));
    const auto indexOf = [](const auto& list, const std::string& name) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].name == name) return static_cast<int>(i);
        }
        return -1;
    };
    for (Sprites::Sprite& sprite : sprites) {
        DrawnSprite drawn;
        drawn.quad = makeSprite(registry, "Magic Portals Sprite", sprite.texture, sprite.additive);
        // The player's slot is kept free.
        drawn.z = SlotZ(sprite.order < m_playerSlot ? sprite.order : sprite.order + 1);
        // Its instance colour and emissive, as its node gives them. A node the
        // look does not hold keeps the engine's defaults, colour 1 and emissive 0;
        // Lighting::Read holds every entity node, so that is only ever a level
        // whose lighting did not read, which is drawn unlit anyway.
        if (const auto look = m_look.nodes.find(sprite.node); m_lit && look != m_look.nodes.end()) {
            drawn.colour = glm::vec4(look->second.colour);
            drawn.emissive = look->second.emissive;
            // Its baked light, when the original draws one for it: static and
            // applying light (the design's section 3.1). Lighting::Read
            // already refuses a lightmap anywhere else; the condition is kept
            // so this line says what the engine does rather than what the file
            // happens to hold.
            if (look->second.isStatic && look->second.applyLight) drawn.lightmap = look->second.lightmap;
            // And what it is to the lights (design section 5.2).
            drawn.isStatic = look->second.isStatic;
            drawn.applyLight = look->second.applyLight;
            drawn.normal = look->second.normal;
            drawn.lookZ = look->second.z;
        }
        drawn.ownerPx = sprite.atPx;
        drawn.crystal = indexOf(m_level.goals.crystals, sprite.node);
        drawn.staticPortal = indexOf(m_level.portals.statics, sprite.node);
        drawn.zone = indexOf(m_level.portals.zones, sprite.node);
        if (const auto body = m_level.built.entities.find(sprite.node); body != m_level.built.entities.end()) {
            drawn.body = body->second;
            // Bodies move on the tick and are drawn between ticks, as their boxes are.
            registry.emplace<InterpolatedTransformComponent>(drawn.quad);
        }
        drawn.sprite = std::move(sprite);
        m_sprites.push_back(std::move(drawn));
    }
    // The beholder at its adder's z_index and a spike at -4: after the sprites
    // drawn at or below it and before the next, the player's slot counted in.
    const auto slotAfter = [this](int zIndex) {
        const int below = static_cast<int>(std::count_if(
            m_sprites.begin(), m_sprites.end(), [zIndex](const DrawnSprite& d) { return d.sprite.zIndex <= zIndex; }));
        return SlotZ(below < m_playerSlot ? below : below + 1) - 0.5f * kSpriteSlotZ;
    };
    int adderZ = 0;
    if (m_level.boss.beholder) {
        if (const Tscn::Node* adder = m_data.scene.FindNode(m_level.boss.beholder->name)) {
            double z = 0.0;
            if (const Tscn::Value* value = adder->Find("z_index"); value != nullptr && value->AsNumber(z)) {
                adderZ = static_cast<int>(z);
            }
        }
    }
    m_beholderZ = slotAfter(adderZ);
    m_spikeZ = slotAfter(kSpikeZIndex);
    m_artReady = true;
    // And what the level's own art does not show: the entities' particles.
    buildEmitters(registry);
    // Then its lights, whose brightness their owners' particles set.
    buildLights(registry);
}

void MagicPortalsLayer::syncSprites(entt::registry& registry) {
    using namespace Supersonic;
    for (DrawnSprite& drawn : m_sprites) {
        if (drawn.quad == entt::null) continue;
        const Sprites::Sprite& sprite = drawn.sprite;
        glm::dvec2 centrePx = Sprites::CentrePx(sprite);
        // Where its ENTITY stands, which is what a light it owns is placed from:
        // the sprite hangs off that point by its offset.
        glm::dvec2 ownerPx = sprite.atPx;
        float rotation = Units::ToWorldRotation(sprite.rotation);
        bool gone = false;
        if (drawn.crystal >= 0) {
            const Goals::Crystal& crystal = m_level.goals.crystals[static_cast<std::size_t>(drawn.crystal)];
            gone = crystal.collected || crystal.expired;
            // A timed crystal fades as it runs out: the remake's guess, as the
            // box's is, and here as the alpha the remake fades.
            drawn.fade = 1.0f;
            if (crystal.timed && crystal.leftS < 2.0) {
                drawn.fade = 0.4f + 0.6f * static_cast<float>(std::fabs(std::sin(crystal.leftS * 12.0)));
            }
        } else if (drawn.staticPortal >= 0) {
            gone = !m_level.portals.statics[static_cast<std::size_t>(drawn.staticPortal)].live;
        } else if (drawn.zone >= 0) {
            // A patrolling zone carries its picture with it.
            const Portals::NoPortalZone& zone = m_level.portals.zones[static_cast<std::size_t>(drawn.zone)];
            centrePx += zone.CentreNowPx() - zone.centrePx;
            ownerPx += zone.CentreNowPx() - zone.centrePx;
        } else if (drawn.body != entt::null) {
            // A body the level took away - a wall a stone broke - takes its picture.
            gone = !registry.valid(drawn.body);
            if (!gone) {
                const auto& body = registry.get<TransformComponent>(drawn.body);
                rotation = body.rotation.z;
                // The offset turned with the body, its y flipped on the way to metres.
                const glm::vec2 offset(Units::ToMetres(sprite.offsetPx.x), Units::ToMetres(-sprite.offsetPx.y));
                const glm::vec2 turned(offset.x * std::cos(rotation) - offset.y * std::sin(rotation),
                                       offset.x * std::sin(rotation) + offset.y * std::cos(rotation));
                centrePx =
                    Units::ToPixels(glm::vec3(body.position.x + turned.x, body.position.y + turned.y, 0.0f));
                ownerPx = Units::ToPixels(body.position);
            }
        }
        if (gone) {
            registry.destroy(drawn.quad);
            drawn.quad = entt::null;
            continue;
        }
        placeSprite(registry, drawn.quad, centrePx, sprite.sizePx, drawn.z, rotation);
        drawn.ownerPx = ownerPx;
    }

    // THE PLATFORM THE DARK DRAGON'S DEATH ADDS, which buildSprites cannot have
    // made a picture for: it has no node, and Sprites::Find reads the scene.
    // entities/ holds single_block_plat_no_emissive.ENT and no .png, so the
    // picture is borrowed from the sibling the level already places - the same
    // node Game sized the box from. Added AFTER the loop above, and by index,
    // because pushing into m_sprites while ranging over it would invalidate it.
    const entt::entity platform = m_level.darkDragon.platformBody;
    if (!m_platformDrawn && m_artReady && platform != entt::null && registry.valid(platform)) {
        int templateAt = -1;
        for (std::size_t i = 0; i < m_sprites.size(); ++i) {
            if (m_sprites[i].sprite.node == m_level.darkDragon.platformNode) {
                templateAt = static_cast<int>(i);
                break;
            }
        }
        if (templateAt >= 0) {
            const DrawnSprite& from = m_sprites[static_cast<std::size_t>(templateAt)];
            DrawnSprite made;
            made.sprite = from.sprite;
            made.sprite.node = m_level.darkDragon.name + "#platform";
            // Where the body actually is, and no offset: Game already folded the
            // template's shape offset into the body's own position.
            made.sprite.atPx = Units::ToPixels(registry.get<TransformComponent>(platform).position);
            made.sprite.offsetPx = glm::dvec2(0.0);
            made.sprite.rotation = 0.0;
            made.z = from.z;
            // And its look: the platform the template's .ent is, lit as it is.
            made.colour = from.colour;
            made.emissive = from.emissive;
            made.isStatic = from.isStatic;
            made.applyLight = from.applyLight;
            made.normal = from.normal;
            made.lookZ = from.lookZ;
            made.ownerPx = made.sprite.atPx;
            // But NOT its lightmap, which `made` leaves empty. A bake is the light
            // that fell where the template stands, and the original reads one per
            // entity already in the scene, named by its id (ETHScene.cpp:315-334,
            // add<id> at ETHSpriteEntity.cpp:437); this platform is added when the
            // dragon dies, long after, under an id no file names.
            made.quad = makeSprite(registry, "Magic Portals Dropped Platform", made.sprite.texture,
                                   made.sprite.additive);
            m_sprites.push_back(std::move(made));
            m_platformDrawn = true;
        }
    }
}

void MagicPortalsLayer::syncDrawables(entt::registry& registry) {
    using namespace Supersonic;
    for (Drawn& drawn : m_bodies) {
        // A body the level took away - a wall a stone broke - takes its box with it.
        if (!registry.valid(drawn.body)) {
            if (drawn.box != entt::null && registry.valid(drawn.box)) registry.destroy(drawn.box);
            drawn.box = entt::null;
            continue;
        }
        const auto& body = registry.get<TransformComponent>(drawn.body);
        const float angle = body.rotation.z;
        // The shape's offset, turned with its body. Pixels run +y down, so the
        // offset's y flips on the way to metres, as Units::ToWorld flips a point.
        const glm::vec2 offset(Units::ToMetres(drawn.offsetPx.x), Units::ToMetres(-drawn.offsetPx.y));
        const glm::vec2 turned(offset.x * std::cos(angle) - offset.y * std::sin(angle),
                               offset.x * std::sin(angle) + offset.y * std::cos(angle));
        const glm::dvec2 centrePx = Units::ToPixels(glm::vec3(body.position.x + turned.x, body.position.y + turned.y, 0.0f));
        placeBox(registry, drawn.box, centrePx, drawn.sizePx, 0.0f, drawn.depth, angle);
    }

    // One box and one sprite per body a launcher threw or the beholder dropped,
    // made and unmade to match.
    for (ThrownBox& drawn : m_thrown) {
        if (registry.valid(drawn.body)) continue;
        if (drawn.box != entt::null && registry.valid(drawn.box)) registry.destroy(drawn.box);
        if (drawn.quad != entt::null && registry.valid(drawn.quad)) registry.destroy(drawn.quad);
        drawn.box = entt::null;
        drawn.quad = entt::null;
    }
    std::erase_if(m_thrown, [](const ThrownBox& drawn) { return drawn.box == entt::null; });
    struct Loose {
        entt::entity body;
        double radiusPx;
        std::string sprite;
        glm::dvec3 emissive;
    };
    std::vector<Loose> loose;
    for (const Launchers::Thrown& thrown : m_level.launchers.live) {
        loose.push_back({thrown.body, thrown.is.radiusPx, thrown.is.sprite, thrown.is.emissive});
    }
    for (const Boss::Rock& rock : m_level.boss.rocks) {
        loose.push_back({rock.body, m_level.boss.rock.radiusPx, m_level.boss.rock.sprite, m_level.boss.rock.emissive});
    }
    for (const Loose& thrown : loose) {
        if (!registry.valid(thrown.body)) continue;
        auto drawn = std::find_if(m_thrown.begin(), m_thrown.end(),
                                  [&thrown](const ThrownBox& d) { return d.body == thrown.body; });
        if (drawn == m_thrown.end()) {
            ThrownBox made;
            made.body = thrown.body;
            made.emissive = thrown.emissive;
            made.box = makeBox(registry, "Magic Portals Thrown", glm::vec3(0.0f), glm::vec3(1.0f), kStoneColour);
            registry.emplace<InterpolatedTransformComponent>(made.box);
            // Drawn as what it is, with the image the converter copied for its .ent.
            const std::string texture = m_paths.art + "/assets/entities/" + thrown.sprite;
            if (m_artReady && !thrown.sprite.empty() && imageSizePx(texture) != glm::dvec2(0.0)) {
                made.quad = makeSprite(registry, "Magic Portals Thrown Sprite", texture, false);
                registry.emplace<InterpolatedTransformComponent>(made.quad);
            }
            m_thrown.push_back(made);
            drawn = m_thrown.end() - 1;
        }
        const auto& body = registry.get<TransformComponent>(thrown.body);
        placeBox(registry, drawn->box, Units::ToPixels(body.position), glm::dvec2(thrown.radiusPx * 2.0), 0.0f, 0.4f,
                 body.rotation.z);
        if (drawn->quad != entt::null) {
            placeSprite(registry, drawn->quad, Units::ToPixels(body.position),
                        imageSizePx(m_paths.art + "/assets/entities/" + thrown.sprite),
                        SlotZ(m_playerSlot) - 0.5f * kSpriteSlotZ, body.rotation.z);
        }
    }

    // THE PLATFORM THE DARK DRAGON'S DEATH ADDS, as a box. Its picture is
    // borrowed in syncSprites, but that is gated on m_artReady - and the box
    // view is exactly the mode used when there is NO art - so the box is made
    // here and carries a flag of its own. Every other body in the game has one,
    // and for a while this did not.
    //
    // Sized from the COLLIDER and not from the sprite: a box stands for the
    // body. Added after the loops above, which have finished walking m_bodies.
    const entt::entity platformBody = m_level.darkDragon.platformBody;
    if (!m_platformBoxed && platformBody != entt::null && registry.valid(platformBody)) {
        if (const auto* collider = registry.try_get<BoxColliderComponent>(platformBody)) {
            Drawn made;
            made.body = platformBody;
            made.offsetPx = glm::dvec2(0.0);
            made.sizePx = glm::dvec2(collider->size.x, collider->size.y) * Units::kPixelsPerMetre;
            made.depth = 0.6f; // a static's, as buildDrawables gives one
            made.box = makeBox(registry, "Magic Portals Body", glm::vec3(0.0f), glm::vec3(1.0f), kStaticColour);
            registry.emplace<InterpolatedTransformComponent>(made.box);
            m_bodies.push_back(made);
            m_platformBoxed = true;
        }
    }

    // The player in its slot among the art - or, with the boxes shown or no art
    // to show, where the boxes are.
    const bool artOnly = m_artReady && !m_showBoxes;
    // m_player and m_playerQuad are BOTH gone once the character has stepped into
    // the door (OnFixedUpdate), and the level goes on running for the 1400 ms
    // before it is scored - so this has to tolerate their absence rather than
    // place a box that is no longer there. placeBox does not guard its entity.
    if (m_level.player != entt::null && registry.valid(m_level.player) && m_player != entt::null &&
        registry.valid(m_player)) {
        const glm::dvec2 at = Units::ToPixels(registry.get<TransformComponent>(m_level.player).position);
        placeBox(registry, m_player, at, glm::dvec2(m_data.tuning.widthPx, m_data.tuning.heightPx),
                 artOnly ? SlotZ(m_playerSlot) : 0.1f, artOnly ? 0.5f * kSpriteSlotZ : 0.4f, 0.0f);
        if (m_playerQuad != entt::null) {
            const Art::Character& mage = m_artRules.character;
            // It turns as it walks, which is the owner's word; the row each way
            // walks is read from the original's DIRECTION enum (art.json).
            if (m_direction > 0.0f) m_facingRight = true;
            if (m_direction < 0.0f) m_facingRight = false;
            const int row = m_facingRight ? mage.rightRow : mage.leftRow;
            const bool walking = m_direction != 0.0f;
            const uint32_t first = static_cast<uint32_t>(row * mage.columns + (walking ? 0 : mage.idleColumn));
            const uint32_t count = walking ? static_cast<uint32_t>(mage.columns) : 1u;
            auto& animation = registry.get<SpriteAnimationComponent>(m_playerQuad);
            if (animation.firstFrame != first || animation.frameCount != count) {
                animation.firstFrame = first;
                animation.frameCount = count;
                animation.frame = 0;
                animation.elapsed = 0.0f;
            }
            animation.playing = walking;
            const glm::dvec2 cellPx =
                imageSizePx(originalImage(mage.sprite)) / glm::dvec2(mage.columns, mage.rows);
            // The image stands with its pivot on the entity, as Ethanon draws it
            // (ETHSpriteEntity::ComputeInScreenSpriteCenter).
            placeSprite(registry, m_playerQuad, at - glm::dvec2(mage.pivotXPx, mage.pivotYPx), cellPx,
                        SlotZ(m_playerSlot), 0.0f);
        }
    }

    syncBoss(registry);
    syncTurrets(registry);

    const auto boxPx = [](const Trigger::Box& box, glm::dvec2& centrePx, glm::dvec2& sizePx) {
        centrePx = Units::ToPixels(glm::vec3(box.centre, 0.0f));
        sizePx = glm::dvec2(box.half) * 2.0 * Units::kPixelsPerMetre;
    };
    for (std::size_t i = 0; i < m_buttons.size() && i < m_level.channels.buttons.size(); ++i) {
        const Puzzle::Button& button = m_level.channels.buttons[i];
        glm::dvec2 centrePx, sizePx;
        boxPx(button.box, centrePx, sizePx);
        placeBox(registry, m_buttons[i], centrePx, sizePx, kMarkerZ, kMarkerDepth, 0.0f);
        registry.get<MaterialComponent>(m_buttons[i]).albedoColor =
            glm::vec4(button.pressed ? kButtonDownColour : kButtonUpColour, 1.0f);
    }
    for (std::size_t i = 0; i < m_crystals.size() && i < m_level.goals.crystals.size(); ++i) {
        if (m_crystals[i] == entt::null) continue;
        const Goals::Crystal& crystal = m_level.goals.crystals[i];
        if (crystal.collected || crystal.expired) {
            registry.destroy(m_crystals[i]);
            m_crystals[i] = entt::null;
            continue;
        }
        glm::dvec2 centrePx, sizePx;
        boxPx(crystal.box, centrePx, sizePx);
        placeBox(registry, m_crystals[i], centrePx, glm::dvec2(14.0), kMarkerZ, kMarkerDepth, 0.785398f);
        // A timed crystal dims and brightens as it runs out: the remake's fade,
        // 0.4 + 0.6 |sin(12 t)| over its last two seconds, as brightness over the
        // dark ground rather than as alpha. A guess, as the remake's is
        // (behaviours.gd:227-230). The original has crystal_temp_alert.mp3, so it
        // warns somehow, but not necessarily like this. Nothing depends on it.
        float brightness = 1.0f;
        if (crystal.timed && crystal.leftS < 2.0) {
            brightness = 0.4f + 0.6f * static_cast<float>(std::fabs(std::sin(crystal.leftS * 12.0)));
        }
        registry.get<MaterialComponent>(m_crystals[i]).albedoColor = glm::vec4(kCrystalColour * brightness, 1.0f);
    }
    {
        glm::dvec2 centrePx, sizePx;
        boxPx(m_level.goals.exit, centrePx, sizePx);
        placeBox(registry, m_exit, centrePx, sizePx, kMarkerZ, kMarkerDepth, 0.0f);
        registry.get<MaterialComponent>(m_exit).albedoColor =
            glm::vec4(m_level.goals.completed ? kExitReachedColour : kExitColour, 1.0f);
    }

    // One box per placed portal, made and unmade to match.
    const std::vector<Portals::Placed>& placed = m_level.portals.placed;
    while (m_portals.size() > placed.size()) {
        if (registry.valid(m_portals.back())) registry.destroy(m_portals.back());
        m_portals.pop_back();
    }
    while (m_portals.size() < placed.size()) {
        m_portals.push_back(makeBox(registry, "Magic Portals Portal", glm::vec3(0.0f), glm::vec3(1.0f), kPortalColour));
    }
    const double diameterPx = m_level.portals.rules.entryRadiusPx * 2.0;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        placeBox(registry, m_portals[i], placed[i].atPx, glm::dvec2(diameterPx), kMarkerZ, kMarkerDepth, 0.0f);
    }
    // And one picture per placed portal, portal.ent's halo, when the original's
    // image is there: just behind the player, which walks into it.
    const std::string halo = originalImage(m_artRules.portal.sprite);
    const glm::dvec2 haloPx = imageSizePx(halo);
    const bool haloReady = m_artReady && haloPx != glm::dvec2(0.0);
    while (m_portalQuads.size() > (haloReady ? placed.size() : 0)) {
        if (registry.valid(m_portalQuads.back())) registry.destroy(m_portalQuads.back());
        m_portalQuads.pop_back();
    }
    while (haloReady && m_portalQuads.size() < placed.size()) {
        m_portalQuads.push_back(makeSprite(registry, "Magic Portals Portal Sprite", halo, m_artRules.portal.additive));
    }
    for (std::size_t i = 0; i < m_portalQuads.size(); ++i) {
        placeSprite(registry, m_portalQuads[i], placed[i].atPx, haloPx, SlotZ(m_playerSlot) - 0.25f * kSpriteSlotZ,
                    0.0f);
    }

    // The shot in flight, made when one is fired and unmade when it lands or fails:
    // a box, and projectile.ent's sheet played round when the original's image is
    // there, just in front of the player it leaves.
    if (m_level.portals.flight) {
        if (m_shot == entt::null) {
            m_shot = makeBox(registry, "Magic Portals Shot", glm::vec3(0.0f), glm::vec3(1.0f), kShotColour);
        }
        placeBox(registry, m_shot, m_level.portals.flight->atPx, glm::dvec2(kShotSizePx), kMarkerZ, kMarkerDepth,
                 0.0f);
        const Art::Picture& bolt = m_artRules.shot;
        const std::string sheet = originalImage(bolt.sprite);
        const glm::dvec2 sheetPx = imageSizePx(sheet);
        if (m_shotQuad == entt::null && m_artReady && sheetPx != glm::dvec2(0.0)) {
            m_shotQuad = makeSprite(registry, "Magic Portals Shot Sprite", sheet, bolt.additive);
            // Played on the tick by the engine's SpriteAnimationSystem.
            auto& animation = registry.emplace<SpriteAnimationComponent>(m_shotQuad);
            animation.columns = static_cast<uint32_t>(bolt.columns);
            animation.rows = static_cast<uint32_t>(bolt.rows);
            animation.framesPerSecond = static_cast<float>(bolt.framesPerSecond);
            animation.loop = true;
        }
        if (m_shotQuad != entt::null) {
            placeSprite(registry, m_shotQuad, m_level.portals.flight->atPx,
                        glm::dvec2(sheetPx.x / bolt.columns, sheetPx.y / bolt.rows),
                        SlotZ(m_playerSlot) + 0.25f * kSpriteSlotZ, 0.0f);
        }
    } else {
        if (m_shot != entt::null && registry.valid(m_shot)) registry.destroy(m_shot);
        m_shot = entt::null;
        if (m_shotQuad != entt::null && registry.valid(m_shotQuad)) registry.destroy(m_shotQuad);
        m_shotQuad = entt::null;
    }

    // Static portals, at their trigger boxes. One that is spent - only when
    // portals.json says static portals do not persist - goes.
    const std::vector<Portals::Static>& statics = m_level.portals.statics;
    for (std::size_t i = 0; i < m_statics.size() && i < statics.size(); ++i) {
        if (m_statics[i] == entt::null) continue;
        if (!statics[i].live) {
            registry.destroy(m_statics[i]);
            m_statics[i] = entt::null;
            continue;
        }
        glm::dvec2 centrePx, sizePx;
        boxPx(statics[i].trigger, centrePx, sizePx);
        placeBox(registry, m_statics[i], centrePx, sizePx, kMarkerZ, kMarkerDepth, 0.0f);
    }

    // No-portal zones, as the square round the circle a tap is refused in, where
    // each is now: a patrolling one moves.
    const std::vector<Portals::NoPortalZone>& zones = m_level.portals.zones;
    for (std::size_t i = 0; i < m_zones.size() && i < zones.size(); ++i) {
        const double sizePx = m_level.portals.rules.antiportalRadiusPx * zones[i].scale * 2.0;
        placeBox(registry, m_zones[i], zones[i].CentreNowPx(), glm::dvec2(sizePx), kZoneZ, kZoneDepth, 0.0f);
    }

    // Hazards, at the box that kills: the remake's trigger, not the shape the
    // converter gives them (Hazards.hpp).
    const std::vector<Hazards::Hazard>& hazards = m_level.hazards.hazards;
    for (std::size_t i = 0; i < m_hazards.size() && i < hazards.size(); ++i) {
        glm::dvec2 centrePx, sizePx;
        boxPx(hazards[i].box, centrePx, sizePx);
        placeBox(registry, m_hazards[i], centrePx, sizePx, kMarkerZ, kMarkerDepth, 0.0f);
    }

    syncSprites(registry);

    // The art in place of the boxes, unless B asks for them or there is no art.
    // What no level pictures - the player, the portals a shot opens, the shot -
    // is a box either way.
    const auto show = [&registry](entt::entity e, bool visible) {
        if (e != entt::null && registry.valid(e)) registry.get<RenderableComponent>(e).isVisible = visible;
    };
    for (const Drawn& drawn : m_bodies) show(drawn.box, !artOnly);
    for (const ThrownBox& drawn : m_thrown) show(drawn.box, !artOnly);
    for (const entt::entity e : m_buttons) show(e, !artOnly);
    for (const entt::entity e : m_crystals) show(e, !artOnly);
    for (const entt::entity e : m_statics) show(e, !artOnly);
    for (const entt::entity e : m_zones) show(e, !artOnly);
    for (const entt::entity e : m_hazards) show(e, !artOnly);
    show(m_exit, !artOnly);
    // A placed portal and the shot, behind the original's pictures once they
    // stand in for them.
    for (const entt::entity e : m_portals) show(e, !(artOnly && haloReady));
    show(m_shot, !(artOnly && m_shotQuad != entt::null));
    show(m_player, !(artOnly && m_playerQuad != entt::null));
    show(m_beholderBox, !(artOnly && m_beholderQuad != entt::null));

    // Last: every quad this tick made is there to colour.
    syncLighting(registry);
}

// ---- the ambient light ----------------------------------------------------------
//
// The original draws every sprite as texel x colour x min(1, ambient + emissive)
// before it adds anything (ETHRenderEntity.cpp:113-117), whatever its blend and
// whether or not it applies light: pass 1 of every draw. The remake's fit of the
// original's pixels holds that to 0.28 of 255 over 1,359 blocks of sprites
// without a lightmap, where drawing them full bright is 45.30 (fit.md 4). With the
// scene holding display values (SceneRendering), the multiply lands on the bytes,
// as it did in the original.
//
// THROUGH THE ENGINE'S 2D SPRITE PATH (MaterialComponent::sprite2D), since step
// 47, the lighting design's G4. Step 45 folded the factor into the albedo colour;
// the engine now takes it apart from the colour, which the lights will need (a
// lamp is not dimmed by the room it shines in), and multiplies the two on the
// CPU exactly as the fold did, so a sprite without a lightmap draws the same.
//
// AND ITS LIGHTMAP, added after the multiply and dimmed by nothing: the baked
// add<id>.png of a static, light-applying sprite (design decisions 3 and 4, from
// the fit: multiplying it scores 26.95 of 255 where adding it scores 0.92, and
// the shipped PNG beats the ETC1 file on 11 of 11 entities). Every level that
// places a torch is a `darkest` level and ships no lightmap (torch.json's census).
// Once a torch is lit the level bakes at run time (step 55, the design's G6): every
// static sprite takes the static lights live, and a file lightmap would be dropped.
//
// PREMULTIPLIED, every mixed sprite of a lit level. The original adds a sprite's
// live light at full weight where its base is weighted by alpha, which one draw
// can only do premultiplied (design section 4.5). Where no light adds anything it
// is the straight mix to within a rounding. The added sprites stay added: no
// blendMode-1 instance applies light.
//
// AND WHAT REACHES IT (since step 49, the design's G5): its height, the original's
// depth; its normal map, when it applies light; and its light mask
// (Lighting::ReceiverMask), which lets a static sprite take only the lights that
// are not static - the shot's - and the player every light, the torch's included.
// The lights themselves, and their halos, are syncLights'.
//
// What is NOT coloured, and why:
//   - the particles. Ethanon multiplies a particle system by
//     min(1, luminance + ambient) only when it is alpha-blended
//     (ETHParticleManager.cpp:382-389), and every one of the game's 102 is added
//     (Particles.hpp). So updateEmitters is unchanged, by the rule rather than by
//     omission.
//   - the boxes: placeholders the PBR path draws, which stand for things rather
//     than being them. At a dark level's 0.01 they would vanish.
//   - the menu, the medal screens and the HUD, none of which is in a level.
void MagicPortalsLayer::syncLighting(entt::registry& registry) {
    m_ambient = m_lit ? Lighting::Ambient(m_data.lighting, m_look.ambient, m_level.darkest, m_level.torch)
                      : glm::dvec3(1.0);

    const bool runtimeBake = m_lit && Lighting::RuntimeBake(m_level.torch);
    static const std::string noLightmap;
    for (const DrawnSprite& drawn : m_sprites) {
        Receiver receiver;
        receiver.applyLight = drawn.applyLight;
        receiver.isStatic = drawn.isStatic;
        receiver.normal = drawn.normal;
        receiver.z = drawn.lookZ;
        receiver.runtimeBake = runtimeBake;
        tint(registry, drawn.quad, drawn.colour * glm::vec4(1.0f, 1.0f, 1.0f, drawn.fade), drawn.emissive,
             runtimeBake ? noLightmap : drawn.lightmap, receiver);
    }
    // What no level places, with its .ent's emissive (art.json, launchers.json).
    // None has a lightmap: a bake belongs to an entity the level file placed.
    const glm::vec4 white(1.0f);
    // And its .ent's lighting facts. Its normal map is the original's, beside the
    // image it is drawn with; its height, for the player, its marker's depth.
    const auto receiverOf = [this](const Art::Picture& picture, double z) {
        Receiver receiver;
        receiver.applyLight = picture.applyLight;
        receiver.isStatic = picture.isStatic;
        if (!picture.normal.empty()) receiver.normal = originalImage("normalmaps/" + picture.normal);
        receiver.z = z;
        return receiver;
    };
    tint(registry, m_playerQuad, white, m_artRules.character.emissive, {},
         receiverOf(m_artRules.character, m_playerZ));
    for (const entt::entity quad : m_portalQuads) {
        tint(registry, quad, white, m_artRules.portal.emissive, {}, receiverOf(m_artRules.portal, 0.0));
    }
    tint(registry, m_shotQuad, white, m_artRules.shot.emissive, {}, receiverOf(m_artRules.shot, m_artRules.shot.z));
    tint(registry, m_beholderQuad, m_beholderColour, m_artRules.beholder.emissive, {},
         receiverOf(m_artRules.beholder, 0.0));
    // A spike is a box when its image is not there, and a box is not coloured.
    for (const entt::entity spike : m_spikes) {
        if (spike == entt::null || !registry.valid(spike)) continue;
        if (!registry.get<Supersonic::MaterialComponent>(spike).unlit) continue;
        tint(registry, spike, white, m_artRules.spike.emissive, {}, receiverOf(m_artRules.spike, kSpikeZIndex));
    }
    // A thrown stone takes no light yet: rolling_stone.ent applies light, and
    // launchers.json does not carry that or its normal map (step 49's open items).
    for (const ThrownBox& thrown : m_thrown) tint(registry, thrown.quad, white, thrown.emissive);

    // The lights, placed after everything that owns one.
    syncLights(registry);
}

void MagicPortalsLayer::tint(entt::registry& registry, entt::entity quad, const glm::vec4& colour,
                             const glm::dvec3& emissive, const std::string& lightmap, const Receiver& receiver) const {
    using Supersonic::MaterialComponent;
    if (quad == entt::null || !registry.valid(quad)) return;
    auto& material = registry.get<MaterialComponent>(quad);

    // A level whose lighting did not read is drawn as before lighting existed:
    // the plain unlit path, its colour alone, mixed straight.
    MaterialComponent::Sprite2DLight sprite;
    MaterialComponent::BlendMode blend = material.blend;
    std::string normal;
    if (m_lit) {
        sprite.enabled = true;
        sprite.ambient = glm::vec3(Lighting::AmbientTerm(m_ambient, emissive));
        if (blend == MaterialComponent::BlendMode::Alpha) blend = MaterialComponent::BlendMode::Premultiplied;
        // What reaches it (design section 5.2). The height is the original's own
        // depth, not the slot the port draws it in: a torch drawn in front of a
        // wall is not nearer to it, and a light 24 units above its owner at z -18
        // is 6 above the player at 0.
        sprite.height = Units::ToMetres(receiver.z);
        sprite.normalYDown = m_data.lighting.normalMapGreenDown;
        sprite.lightMask = m_lightMasksOff ? std::uint8_t{0} : Lighting::ReceiverMask(receiver.isStatic, receiver.applyLight, receiver.runtimeBake);
        // A sprite that takes no light has no use for a normal map, and naming none
        // keeps it in the material set it shares with its image's other copies.
        // Without one the engine samples the flat map: a sprite that applies light
        // and names no <Normal> is lit face-on, as Ethanon's default_nm.png lights it.
        if (receiver.applyLight) normal = receiver.normal;
    } else if (blend == MaterialComponent::BlendMode::Premultiplied) {
        blend = MaterialComponent::BlendMode::Alpha;
    }

    // Each written only when it changes: a still level writes nothing a tick,
    // and the overlay's and the normal map's paths are in SyncResources'
    // signature, which a rewrite of the same path would not move but a churn of
    // it would.
    if (material.albedoColor != colour) material.albedoColor = colour;
    if (material.sprite2D != sprite) material.sprite2D = sprite;
    // Empty on an unlit level: buildSprites takes a lightmap only from a look
    // that read.
    if (material.overlayTexturePath != lightmap) material.overlayTexturePath = lightmap;
    if (material.normalTexturePath != normal) material.normalTexturePath = normal;
    if (material.blend != blend) material.blend = blend;
}

// ---- the lights and their halos ---------------------------------------------------
//
// THE LIGHTS (design section 5.3). Each <Light> a level places is a Light2DComponent
// at its owner plus the light's offset, not turned with the owner
// (BuildChildLight, ETHEntityRenderingManager.cpp:174-184), with:
//   - its height the owner's depth plus the offset's z, in the original's units
//     like every sprite's (tint), so the one scale to metres cancels in the facing
//     and the falloff;
//   - its colour <Color> x the level's lightIntensity, and x the live share of the
//     owner's first particle system when the owner is not static
//     (Lighting::LightColour);
//   - its layer its owner's staticness (Lighting::LightLayer), which is what keeps
//     a torch out of the walls whose lightmaps already hold it;
//   - off while its owner is not drawn: a light on a hidden entity emits nothing
//     (ETHEntityRenderingManager.cpp:110-116).
//
// THE HALOS (design section 5.5, plan_port System 8). An added quad of the halo's
// size in world units at the owner plus the light's UNSCALED offset, coloured
// <Color> x haloBrightness x the owner's live particle share for any owner
// (ETHRenderEntity.cpp:354-388), x lighting.json's scale, with no ambient and no
// intensity, so it is not on the 2D sprite path at all. Drawn a quarter slot in
// front of its owner's picture: over what is behind the owner (the arches and sky
// of 1-1, which the remake's fit finds it adds over), under what is in front of
// it (1-1's wall04, where it finds none), and behind the owner's particles, which
// Ethanon draws after the halo at the same depth.
//
// THE SHOT carries projectile.ent's light and halo (art.json), made with the shot
// and gone with it. Not static, so it reaches every sprite that applies light,
// static walls included.
//
// All of it is presentation: nothing here reads back into Game::Level.

void MagicPortalsLayer::buildLights(entt::registry& registry) {
    using namespace Supersonic;
    unloadLights(registry);
    if (!m_lit || !m_artReady) return;
    // In the file's order, so a level's lights are gathered in the same order
    // every run; m_look's nodes are hashed.
    for (const Tscn::Node& node : m_data.scene.nodes) {
        if (node.parent != ".") continue;
        const auto look = m_look.nodes.find(node.name);
        if (look == m_look.nodes.end() || !look->second.light) continue;
        PlacedLight placed;
        placed.node = node.name;
        placed.light = *look->second.light;
        placed.ownerStatic = look->second.isStatic;
        placed.ownerZ = look->second.z;
        PositionOf(&node, placed.atPx);
        for (std::size_t i = 0; i < m_sprites.size(); ++i) {
            if (m_sprites[i].sprite.node == node.name) {
                placed.sprite = static_cast<int>(i);
                break;
            }
        }
        for (std::size_t i = 0; placed.sprite >= 0 && i < m_emitters.size(); ++i) {
            if (m_emitters[i].sprite == placed.sprite && m_emitters[i].slot == 0) {
                placed.emitter = static_cast<int>(i);
                break;
            }
        }
        if (placed.sprite >= 0) {
            placed.haloZ = m_sprites[static_cast<std::size_t>(placed.sprite)].z + 0.25f * kSpriteSlotZ;
        } else {
            // No picture to stand in front of: after the sprites at or below its
            // z_index, as the beholder is placed.
            int zIndex = 0;
            double z = 0.0;
            if (const Tscn::Value* value = node.Find("z_index"); value != nullptr && value->AsNumber(z)) {
                zIndex = static_cast<int>(z);
            }
            const int below = static_cast<int>(std::count_if(m_sprites.begin(), m_sprites.end(), [zIndex](const DrawnSprite& d) {
                return d.sprite.zIndex <= zIndex;
            }));
            placed.haloZ = SlotZ(below < m_playerSlot ? below : below + 1) - 0.5f * kSpriteSlotZ;
        }
        placed.entity = registry.create();
        registry.emplace<TagComponent>(placed.entity, "Magic Portals 2D Light");
        registry.emplace<TransformComponent>(placed.entity);
        registry.emplace<Light2DComponent>(placed.entity);
        if (!placed.light.halo.empty() && imageSizePx(placed.light.halo) != glm::dvec2(0.0)) {
            placed.halo = makeSprite(registry, "Magic Portals Halo", placed.light.halo, true);
        }
        m_placedLights.push_back(std::move(placed));
    }
}

void MagicPortalsLayer::unloadLights(entt::registry& registry) {
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };
    for (PlacedLight& placed : m_placedLights) {
        destroy(placed.entity);
        destroy(placed.halo);
    }
    m_placedLights.clear();
    for (TorchLight& torch : m_torchLights) {
        destroy(torch.light);
        destroy(torch.halo);
    }
    m_torchLights.clear();
    destroy(m_shotLight);
    destroy(m_shotHalo);
}

double MagicPortalsLayer::particleRatioOf(int emitter) const {
    if (emitter < 0 || emitter >= static_cast<int>(m_emitters.size())) return Lighting::ParticleRatio(0, 0);
    const Emitter& from = m_emitters[static_cast<std::size_t>(emitter)];
    // A particle has a quad exactly while it is drawn: released, bigger than
    // nothing, not spent and its system emitting (updateEmitters).
    const int active = static_cast<int>(std::count_if(from.particles.begin(), from.particles.end(),
                                                      [](const Particle& p) { return p.quad != entt::null; }));
    return Lighting::ParticleRatio(active, from.system.count);
}

void MagicPortalsLayer::syncLights(entt::registry& registry) {
    using namespace Supersonic;
    if (!m_loaded || !m_lit) return;
    const double intensity = m_look.intensity;
    const double haloScale = m_data.lighting.haloBrightnessScale;

    // A light, its halo and where they stand, in the colours `ratio` gives them.
    const auto place = [&](entt::entity light, entt::entity halo, const Lighting::Light& from, const glm::dvec2& ownerPx,
                           double ownerZ, bool ownerStatic, double ratio, float haloZ, bool present) {
        if (light != entt::null && registry.valid(light)) {
            const glm::vec3 at = Units::ToWorld(ownerPx.x + from.offset.x, ownerPx.y + from.offset.y);
            registry.get<TransformComponent>(light).position = glm::vec3(at.x, at.y, 0.0f);
            auto& component = registry.get<Light2DComponent>(light);
            component.color = glm::vec3(Lighting::LightColour(from, intensity, ownerStatic, ratio));
            component.intensity = 1.0f;
            component.range = Units::ToMetres(from.range);
            component.height = Units::ToMetres(ownerZ + from.offset.z);
            component.layers = Lighting::LightLayer(ownerStatic);
            component.enabled = present;
        }
        if (halo != entt::null && registry.valid(halo)) {
            placeSprite(registry, halo, ownerPx + from.haloOffset, from.haloSize, haloZ, 0.0f);
            registry.get<RenderableComponent>(halo).isVisible = present;
            const glm::vec4 colour(glm::vec3(Lighting::HaloColour(from, ratio, haloScale)), 1.0f);
            auto& material = registry.get<MaterialComponent>(halo);
            if (material.albedoColor != colour) material.albedoColor = colour;
        }
    };

    for (const PlacedLight& placed : m_placedLights) {
        bool present = true;
        glm::dvec2 ownerPx = placed.atPx;
        if (placed.sprite >= 0) {
            const DrawnSprite& owner = m_sprites[static_cast<std::size_t>(placed.sprite)];
            present = owner.quad != entt::null && registry.valid(owner.quad);
            ownerPx = owner.ownerPx;
        }
        place(placed.entity, placed.halo, placed.light, ownerPx, placed.ownerZ, placed.ownerStatic,
              particleRatioOf(placed.emitter), placed.haloZ, present);
    }

    // light_from_projectile.ent's, at each torch while it is lit (step 55, the
    // design's G6): added by the shot that lights it and deleted by the signal that
    // puts it out (torch.json). The torch's own depth is the entity's, which is a
    // guess recorded in art.json. Its flame is not built, so its halo is at the
    // share of a system with none.
    const Art::Picture& torchLight = m_artRules.torchLight;
    const std::vector<Torch::Light>& torches = m_level.torch.lights;
    m_torchLights.resize(torches.size());
    for (std::size_t i = 0; i < torches.size(); ++i) {
        TorchLight& made = m_torchLights[i];
        if (!torches[i].lit || !torchLight.light) {
            if (made.light != entt::null && registry.valid(made.light)) registry.destroy(made.light);
            if (made.halo != entt::null && registry.valid(made.halo)) registry.destroy(made.halo);
            made = TorchLight{};
            continue;
        }
        double ownerZ = torchLight.z;
        float haloZ = SlotZ(m_playerSlot) - 0.5f * kSpriteSlotZ;
        if (const auto look = m_look.nodes.find(torches[i].name); look != m_look.nodes.end()) ownerZ = look->second.z;
        for (const DrawnSprite& drawn : m_sprites) {
            if (drawn.sprite.node != torches[i].name) continue;
            haloZ = drawn.z + 0.25f * kSpriteSlotZ;
            break;
        }
        if (made.light == entt::null) {
            made.light = registry.create();
            registry.emplace<TagComponent>(made.light, "Magic Portals Torch Light");
            registry.emplace<TransformComponent>(made.light);
            registry.emplace<Light2DComponent>(made.light);
        }
        const std::string torchHalo = originalImage(torchLight.light->halo);
        if (made.halo == entt::null && m_artReady && imageSizePx(torchHalo) != glm::dvec2(0.0)) {
            made.halo = makeSprite(registry, "Magic Portals Torch Halo", torchHalo, true);
        }
        place(made.light, made.halo, *torchLight.light, torches[i].atPx, ownerZ, torchLight.isStatic,
              Lighting::ParticleRatio(0, 0), haloZ, true);
    }

    // The shot's, while it flies.
    const Art::Picture& shot = m_artRules.shot;
    if (!m_level.portals.flight || !shot.light) {
        auto destroy = [&registry](entt::entity& e) {
            if (e != entt::null && registry.valid(e)) registry.destroy(e);
            e = entt::null;
        };
        destroy(m_shotLight);
        destroy(m_shotHalo);
        return;
    }
    if (m_shotLight == entt::null) {
        m_shotLight = registry.create();
        registry.emplace<TagComponent>(m_shotLight, "Magic Portals Shot Light");
        registry.emplace<TransformComponent>(m_shotLight);
        registry.emplace<Light2DComponent>(m_shotLight);
    }
    const std::string haloImage = originalImage(shot.light->halo);
    if (m_shotHalo == entt::null && m_artReady && imageSizePx(haloImage) != glm::dvec2(0.0)) {
        m_shotHalo = makeSprite(registry, "Magic Portals Shot Halo", haloImage, true);
    }
    // projectile.ent has no particle system, so nothing scales either. Its halo a
    // quarter slot in front of its own picture, which is a quarter slot in front
    // of the player's.
    place(m_shotLight, m_shotHalo, *shot.light, m_level.portals.flight->atPx, shot.z, shot.isStatic,
          Lighting::ParticleRatio(0, 0), SlotZ(m_playerSlot) + 0.5f * kSpriteSlotZ, true);
}

void MagicPortalsLayer::syncTurrets(entt::registry& registry) {
    using namespace Supersonic;
    const Turrets::State& turrets = m_level.turrets;
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };

    // One quad per fireball in flight, grown and shrunk to match, which is how
    // the beholder's spikes are drawn. Always a BOX: fireball.ent has no
    // <Sprite> at all - the original shows its ParticleSystem and its Light -
    // so unlike a spike there is no image to fall back from.
    while (m_fireballs.size() > turrets.fireballs.size()) {
        destroy(m_fireballs.back());
        m_fireballs.pop_back();
    }
    while (m_fireballs.size() < turrets.fireballs.size()) {
        m_fireballs.push_back(
            makeBox(registry, "Magic Portals Fireball", glm::vec3(0.0f), glm::vec3(1.0f), kFireballColour));
    }
    for (std::size_t i = 0; i < m_fireballs.size(); ++i) {
        // At the marker z, which is the port's choice and not the original's:
        // the original states a spike's z (-4) and says nothing about a
        // fireball's.
        placeBox(registry, m_fireballs[i], turrets.fireballs[i].atPx, glm::dvec2(kFireballBoxPx), kMarkerZ,
                 kMarkerDepth, 0.0f);
    }
}

void MagicPortalsLayer::syncBoss(entt::registry& registry) {
    using namespace Supersonic;
    const Boss::State& boss = m_level.boss;
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };
    if (!boss.beholder || boss.beholder->gone) {
        destroy(m_beholderBox);
        destroy(m_beholderQuad);
    } else {
        const Boss::Beholder& beholder = *boss.beholder;
        // Its reach - where a rising rock hurts it and the player dies - as a box.
        if (m_beholderBox == entt::null) {
            m_beholderBox =
                makeBox(registry, "Magic Portals Beholder", glm::vec3(0.0f), glm::vec3(1.0f), kBeholderColour);
            registry.emplace<InterpolatedTransformComponent>(m_beholderBox);
        }
        placeBox(registry, m_beholderBox, beholder.atPx, glm::dvec2(boss.rules.radiusPx * 2.0), kMarkerZ, kMarkerDepth,
                 0.0f);
        // beholder.ent's sheet: its eye open or shut, going red as it is hurt,
        // and pulsing as bounce() has it, but for while it throws rocks (art.json).
        const Art::Beholder& picture = m_artRules.beholder;
        const std::string sheet = originalImage(picture.sprite);
        const glm::dvec2 sheetPx = imageSizePx(sheet);
        if (m_beholderQuad == entt::null && m_artReady && sheetPx != glm::dvec2(0.0)) {
            m_beholderQuad = makeSprite(registry, "Magic Portals Beholder Sprite", sheet, picture.additive);
            registry.emplace<InterpolatedTransformComponent>(m_beholderQuad);
            auto& animation = registry.emplace<SpriteAnimationComponent>(m_beholderQuad);
            animation.columns = static_cast<uint32_t>(picture.columns);
            animation.rows = static_cast<uint32_t>(picture.rows);
            animation.frameCount = 1;
            animation.playing = false;
        }
        if (m_beholderQuad != entt::null) {
            auto& animation = registry.get<SpriteAnimationComponent>(m_beholderQuad);
            animation.firstFrame = static_cast<uint32_t>(beholder.frame);
            animation.frame = 0;
            switch (beholder.phase) {
            case Boss::Phase::Seeking: m_beholderScale = picture.seeking.ScaleAt(beholder.pulseMs); break;
            case Boss::Phase::GotDamage: m_beholderScale = picture.hurt.ScaleAt(beholder.pulseMs); break;
            case Boss::Phase::Dead: m_beholderScale = picture.dead.ScaleAt(beholder.pulseMs); break;
            case Boss::Phase::ThrowRock: break;
            }
            const glm::dvec2 cellPx = sheetPx / glm::dvec2(picture.columns, picture.rows);
            placeSprite(registry, m_beholderQuad, beholder.atPx, cellPx * m_beholderScale, m_beholderZ, 0.0f);
            const float left = static_cast<float>(std::max(beholder.hp, 0)) / static_cast<float>(boss.rules.maxHp);
            m_beholderColour = glm::vec4(1.0f, left, left, 1.0f);
        }
    }

    // Its spikes: beholder_spike.ent turned to where each flies and standing on
    // its pivot, or small boxes without the image.
    const Art::Spike& spike = m_artRules.spike;
    const std::string image = originalImage(spike.sprite);
    const glm::dvec2 imagePx = imageSizePx(image);
    const bool pictured = m_artReady && imagePx != glm::dvec2(0.0);
    while (m_spikes.size() > boss.spikes.size()) {
        destroy(m_spikes.back());
        m_spikes.pop_back();
    }
    while (m_spikes.size() < boss.spikes.size()) {
        m_spikes.push_back(pictured ? makeSprite(registry, "Magic Portals Spike", image, spike.additive)
                                    : makeBox(registry, "Magic Portals Spike", glm::vec3(0.0f), glm::vec3(1.0f),
                                              kSpikeColour));
    }
    for (std::size_t i = 0; i < m_spikes.size(); ++i) {
        const Boss::Spike& flying = boss.spikes[i];
        const glm::dvec2 d = flying.directionPx;
        // The image's down is turned onto its way, and so its right onto
        // (dy, -dx). The picture stands with its pivot on the spike.
        const glm::dvec2 pivot = spike.pivotXPx * glm::dvec2(d.y, -d.x) + spike.pivotYPx * d;
        const float rotation = std::atan2(static_cast<float>(-d.y), static_cast<float>(d.x)) + 1.5707964f;
        if (pictured) {
            placeSprite(registry, m_spikes[i], flying.atPx - pivot, imagePx, m_spikeZ, rotation);
        } else {
            placeBox(registry, m_spikes[i], flying.atPx, glm::dvec2(kSpikeBoxPx), kMarkerZ, kMarkerDepth, rotation);
        }
    }
}

void MagicPortalsLayer::buildHud(entt::registry& registry) {
    using namespace Supersonic;
    auto label = [&registry](const char* name, UIAnchor anchor, glm::vec2 offset, float size) {
        const entt::entity e = registry.create();
        registry.emplace<TagComponent>(e, name);
        auto& text = registry.emplace<UITextComponent>(e);
        text.anchor = anchor;
        text.offset = offset;
        text.fontSize = size;
        text.text = "";
        return e;
    };
    m_hud.status = label("Magic Portals Status", UIAnchor::TopLeft, glm::vec2(24.0f, 18.0f), 26.0f);
    m_hud.result = label("Magic Portals Result", UIAnchor::TopLeft, glm::vec2(24.0f, 56.0f), 20.0f);
    m_hud.controls = label("Magic Portals Controls", UIAnchor::BottomLeft, glm::vec2(24.0f, 24.0f), 20.0f);
}

void MagicPortalsLayer::updateHud(entt::registry& registry) {
    using namespace Supersonic;
    auto set = [&registry](entt::entity e, std::string text) {
        if (e != entt::null && registry.valid(e)) registry.get<UITextComponent>(e).text = std::move(text);
    };
    std::string status;
    if (menuStateUp()) {
        // NO TEXT on a menu state (ui3 spec D7): every picture the original draws
        // there is its own art, and its own fonts where it writes anything.
    } else if (m_current < 0) {
        status = "Magic Portals could not start: " + m_loadError;
    } else {
        const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(m_current)];
        if (m_chapterComplete) {
            status = "Chapter " + Count(entry.world + 1) + " complete";
        } else if (!m_loaded) {
            status = Chapters::Label(entry) + " is not playable yet: " + m_loadError;
        }
        // A level being played says NOTHING. The original draws no text over a
        // level at all - no level number, no portal or crystal count - and that
        // is measured rather than remembered: across the settled frames of 41
        // different levels the only screen-fixed regions are the two button
        // corners and the two pads (static_hud.md section 6). The counts this line
        // carried are on the medal screen, where the original puts them.
    }
    set(m_hud.status, status);

    std::string result;
    // Nothing over a level, the finished and lost screens included: those draw
    // the original's own count and medal over it.
    if (m_lastCleared && !m_loaded) {
        const Cleared& c = *m_lastCleared;
        result = c.label + " cleared with " + Count(c.portalsUsed) + (c.portalsUsed == 1 ? " portal" : " portals") +
                 (c.crystalsTotal > 0 ? ", " + Count(c.crystals) + "/" + Count(c.crystalsTotal) + " crystals" : "") +
                 (c.Gold() ? " - gold" : "");
    }
    set(m_hud.result, result);
    // The keys are the port's, and the menus and a level have the original's own
    // buttons on them now, so the help goes with the rest of the text.
    set(m_hud.controls, "");
}

// ---- the tick ----------------------------------------------------------------

void MagicPortalsLayer::buildControls() {
    unloadControls();
    // No art, no HUD: the level is being drawn as boxes, and the keys still work.
    if (!m_artReady || !m_hudReady || m_current < 0) return;
    const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(m_current)];

    // A picture the HUD cannot read is said once, here, rather than drawn as
    // the white the renderer falls back to.
    const auto readable = [this](const std::string& image) -> std::string {
        if (imageSizePx(image) == glm::dvec2(0.0)) {
            SUPERSONIC_LOG_WARN("Magic Portals") << "HUD image could not be read: " << image << std::endl;
            return {};
        }
        return image;
    };

    const struct Made {
        Control kind;
        const std::string& file;
    } made[] = {
        {Control::Left, m_hudRules.pads.leftSprite},
        {Control::Right, m_hudRules.pads.rightSprite},
        {Control::Reset, m_hudRules.restart.sprite},
        {Control::Menu, m_hudRules.pause.sprite},
        {Control::Clear, m_hudRules.clearPortals.sprite},
    };
    for (const Made& one : made) {
        ControlButton button;
        button.kind = one.kind;
        // menuImage prefers sprites/hd/, which is what the original draws on any
        // screen over 480 px tall, and falls back for a file with no hd twin.
        button.image = readable(menuImage(one.file));
        // A control whose picture is missing is not a control either: a tap
        // must not be eaten by something nobody can see.
        if (!button.image.empty()) m_controls.push_back(button);
    }

    // The ring, only where it is ever drawn. ring_sprite.png has no hd twin,
    // and needs none: it is drawn at a stated size, not its own.
    if (tutorialPads()) m_ringImage = readable(menuImage(m_hudRules.pads.ringSprite));

    // The plaque, only for a level with a medal recorded: addCurrentMedalSprite
    // adds nothing where getScore is 0, so a fresh save shows no plaque at all.
    if (const std::string medal = Hud::MedalSprite(m_hudRules, m_scores.Get(entry.world, entry.index));
        !medal.empty()) {
        m_plaqueImage = readable(menuImage(m_hudRules.plaque.sprite));
        m_medalImage = readable(menuImage(medal));
    }

    // "Part N", over the font's own pages. Read once, the first time a level
    // wants it: the font is the original's and lives beside its other data, so
    // a machine without it plays with no caption and says so.
    if (!m_captionFontTried) {
        m_captionFontTried = true;
        std::string why;
        const std::string font = m_paths.original + "/data/" + m_hudRules.caption.font;
        if (!m_captionFont.Load(font, why)) {
            SUPERSONIC_LOG_WARN("Magic Portals") << "no level caption: " << why << std::endl;
        }
    }
    m_captionText = Hud::CaptionText(m_hudRules, entry.index);

    // The pause's pictures, each resolved once for the level: any of them may be
    // drawn the moment the pause opens.
    for (const std::string* file :
         {&m_pauseRules.goldenPlaque.sprite, &m_pauseRules.currentPlaque.sprite, &m_pauseRules.medalBronze,
          &m_pauseRules.medalSilver, &m_pauseRules.medalGold, &m_pauseRules.levels.sprite,
          &m_pauseRules.resume.sprite, &m_pauseRules.skip.sprite, &m_pauseRules.achievements.sprite,
          &m_pauseRules.sound.sprite, &m_pauseRules.soundOffSprite, &m_pauseRules.music.sprite,
          &m_pauseRules.musicOffSprite}) {
        if (m_pauseImages.find(*file) == m_pauseImages.end()) m_pauseImages[*file] = readable(menuImage(*file));
    }
    // And the finished and lost screens', with each one's size in texels: the
    // veil is drawn as a clamped texture, which is split by its texels. The
    // crystal is an entity's picture, whose hd twin sits beside it.
    const LevelEnd::Rules::Finished& finished = m_levelEndRules.finished;
    const LevelEnd::Rules::Lost& lost = m_levelEndRules.lost;
    for (const std::string* file :
         {&finished.veil.sprite, &finished.title.sprite, &finished.portalsPlaque.sprite, &finished.goldenPlaque.sprite,
          &finished.restart.sprite, &finished.next.sprite, &finished.list.sprite, &finished.medalBronze,
          &finished.medalSilver, &finished.medalGold, &lost.veil.sprite, &lost.title.sprite, &lost.restart.sprite,
          &lost.list.sprite}) {
        if (m_endImages.find(*file) == m_endImages.end()) m_endImages[*file] = readable(menuImage(*file));
    }
    if (m_endImages.find(finished.crystalSprite) == m_endImages.end()) {
        std::error_code ec;
        const std::string hd = m_paths.original + "/entities/hd/" + finished.crystalSprite;
        m_endImages[finished.crystalSprite] =
            readable(std::filesystem::exists(hd, ec) ? hd : originalImage(finished.crystalSprite));
    }
    for (const auto& [file, image] : m_endImages) {
        m_endTexels[file] = image.empty() ? glm::ivec2(0) : glm::ivec2(imageSizePx(image));
    }
    // And the popups': the framework's and every class's, named within the
    // original's assets rather than by file alone - the hand and the stone are
    // entities, the smoke a particle - each the hd twin where one exists.
    const auto popupImage = [&](const std::string& file) {
        if (m_popupImages.find(file) == m_popupImages.end()) m_popupImages[file] = readable(originalAsset(file));
    };
    popupImage(m_popupRules.card.sprite);
    popupImage(m_popupRules.closeButton.sprite);
    for (const Popup::Class& popup : m_popupRules.classes) {
        popupImage(popup.card.sprite);
        for (const Popup::Item& item : popup.items) popupImage(item.sprite);
    }

    // And their fonts, where they are not the caption's, read once for the run.
    const Pause::Rules& pause = m_pauseRules;
    for (const std::string* name : {&pause.title.font, &pause.goldenNumber.font, &finished.counter.font,
                                    &finished.goldenNumber.font, &finished.crystalCount.font}) {
        if (*name == m_hudRules.caption.font || m_uiFonts.find(*name) != m_uiFonts.end()) continue;
        std::string why;
        if (!m_uiFonts[*name].Load(m_paths.original + "/data/" + *name, why)) {
            SUPERSONIC_LOG_WARN("Magic Portals") << "no text in " << *name << ": " << why << std::endl;
        }
    }
}

void MagicPortalsLayer::unloadControls() {
    m_controls.clear();
    m_ringImage.clear();
    m_plaqueImage.clear();
    m_medalImage.clear();
    m_captionText.clear();
    m_pauseImages.clear();
    m_endImages.clear();
    m_endTexels.clear();
    m_popupImages.clear();
}

const Supersonic::BitmapFont* MagicPortalsLayer::uiFont(const std::string& name) const {
    if (name == m_hudRules.caption.font) return &m_captionFont;
    const auto found = m_uiFonts.find(name);
    return found != m_uiFonts.end() ? &found->second : nullptr;
}

bool MagicPortalsLayer::tutorialPads() const {
    const Chapters::Level* level = Current();
    return m_hudReady && level != nullptr && level->name == m_hudRules.pads.tutorialLevel;
}

void MagicPortalsLayer::layOutControls() {
    const glm::dvec2 view = ViewPx();
    // Only while a level is being played: the menu screens have their own
    // buttons.
    const bool playing = m_loaded && m_screen == Screen::None && !m_finishing && !m_dying;
    // AND AS A LEVEL ENDS, from the door or the death on, finished and lost
    // screens included (spec 3.4, 4.1, D8): the pads decay from the byte the
    // pulse left them at, and nothing is pressed - the tick reads no input for
    // an ended level.
    const bool ended = m_loaded && (m_finishing || m_dying);
    // A weightless level has no walk pads at all: MainCharacter neither updates
    // nor draws them when noGravity is set. Nor are they drawn while a pause has
    // game time stopped (spec 2.2, measured gain 0.010 / -0.025), where restart,
    // pause and clear-portals stay drawn under its dim, frozen and unpressed:
    // the tick reads no input for a level while a pause is up.
    // A popup stops game time the same way, and the pads go with it (spec 5.2).
    const bool pads = (playing || (ended && m_padEndByte > 0)) && !m_level.portals.noGravity && !GameTimeStopped();
    const double padAlpha = playing ? Hud::PadOpacity(m_hudRules, m_levelAgeMs, tutorialPads())
                                    : static_cast<double>(m_padEndByte) / 255.0;
    // Restart, pause and clear-portals: CUT at the door, in one frame (gain
    // 0.4705 -> -0.034), and DISMISSED at a death as UIButtons, 700 ms out along
    // their rays. Both are measurements and the decode explains only the second
    // (spec U1); both are followed.
    // While playing, restart and pause are GameLayer's UIButtons, and come IN as
    // UIButtons on GameLayer's updates - the level's age, which a popup raised as
    // the level loads holds at zero: under 1-02's and 1-03's popups they are not
    // there to see, and they slide in once it has gone (spec 5.4, A-H9). On any
    // other level the entrance is over at 700 ms, under the opening's black.
    // Clear-portals keeps the flat 120 it had: PortalManager adds it with each
    // portal, and no capture shows its entrance.
    const auto corner = [&](const Hud::Placement& placement, bool present) {
        if (playing && &placement != &m_hudRules.clearPortals) {
            const LevelEnd::Dismissed in =
                LevelEnd::HudEntered(m_levelEndRules, placement, m_hudRules.alphaByte, view, m_levelAgeMs);
            return std::make_tuple(in.rect, present && in.shown, in.alpha);
        }
        if (playing) return std::make_tuple(Hud::Place(placement, view), present, Hud::Opacity(m_hudRules));
        if (m_dying && present) {
            const LevelEnd::Dismissed out =
                LevelEnd::HudDismissed(m_levelEndRules, placement, m_hudRules.alphaByte, view, m_dyingClockMs);
            return std::make_tuple(out.rect, out.shown, out.alpha);
        }
        return std::make_tuple(Hud::Place(placement, view), false, 0.0);
    };
    for (ControlButton& button : m_controls) {
        switch (button.kind) {
        case Control::Left:
            button.rect = Hud::PadRect(m_hudRules, Hud::Side::Left, view, m_levelAgeMs);
            button.shown = pads;
            button.alpha = padAlpha;
            break;
        case Control::Right:
            button.rect = Hud::PadRect(m_hudRules, Hud::Side::Right, view, m_levelAgeMs);
            button.shown = pads;
            button.alpha = padAlpha;
            break;
        case Control::Reset:
            std::tie(button.rect, button.shown, button.alpha) = corner(m_hudRules.restart, true);
            break;
        case Control::Menu:
            std::tie(button.rect, button.shown, button.alpha) = corner(m_hudRules.pause, true);
            break;
        case Control::Clear:
            // PortalManager::update: none in a level that grants no portal, and
            // otherwise exactly while a placed one is alive - and at a death,
            // dismissed with the other two if it was up (INFERRED: the spec's
            // recordings had no portal placed).
            std::tie(button.rect, button.shown, button.alpha) = corner(
                m_hudRules.clearPortals,
                playing ? m_level.portals.budget > 0 && !m_level.portals.placed.empty() : m_clearShownAtEnd);
            break;
        }
    }
}

bool MagicPortalsLayer::ControlRect(Control control, Hud::Rect& out) const {
    for (const ControlButton& button : m_controls) {
        if (button.kind != control || !button.shown) continue;
        out = button.rect;
        return true;
    }
    return false;
}

bool MagicPortalsLayer::NoPortalSignRect(Hud::Rect& out) const {
    if (!m_sign.present) return false;
    out = m_sign.onView;
    return true;
}

void MagicPortalsLayer::tickNoPortalSign(double dtMs) {
    if (!m_sign.present || !m_hudReady) return;
    const glm::dvec2 view = ViewPx();
    // Ethanon's GetCameraPos is the screen's top-left corner in the world.
    const glm::dvec2 corner = m_follow.centrePx - view * 0.5;
    m_sign.follow.Tick(m_hudRules, Hud::SignTarget(m_hudRules, corner, m_sign.sizeUnits), dtMs);
    m_sign.onView.size = m_sign.sizeUnits;
    m_sign.onView.min = m_sign.follow.at - m_sign.sizeUnits * 0.5 - corner;
}

void MagicPortalsLayer::EmitHud(entt::registry& registry) const {
    using Supersonic::ScreenOverlay;
    auto* const* slot = registry.ctx().find<ScreenOverlay*>();
    if (slot == nullptr || *slot == nullptr) return;
    ScreenOverlay& overlay = **slot;
    // Nothing over a menu screen, which is drawn in the level's space. The
    // finished and lost screens are over a level, and drawn here.
    const bool overLevel = m_screen == Screen::Finished || m_screen == Screen::Dead;
    if (!m_hudReady || !m_loaded || (m_screen != Screen::None && !overLevel)) return;
    const glm::dvec2 view = ViewPx();
    if (view.x <= 0.0 || view.y <= 0.0) return;

    // A rectangle on the view, in design units, as fractions of the image the
    // view fills; a picture with nothing to show is not sent at all.
    const auto add = [&overlay, &view](const Hud::Rect& rect, const std::string& image, const glm::vec4& colour,
                                       const glm::dvec2& uvMin = glm::dvec2(0.0),
                                       const glm::dvec2& uvMax = glm::dvec2(1.0)) {
        if (colour.a <= 0.0f) return;
        ScreenOverlay::Quad quad;
        quad.min = glm::vec2(rect.min / view);
        quad.max = glm::vec2(rect.Max() / view);
        quad.uvMin = glm::vec2(uvMin);
        quad.uvMax = glm::vec2(uvMax);
        quad.color = colour;
        quad.texture = image;
        overlay.Add(std::move(quad));
    };
    const auto white = [](double alpha) { return glm::vec4(1.0f, 1.0f, 1.0f, static_cast<float>(alpha)); };
    const auto find = [this](Control kind) -> const ControlButton* {
        for (const ControlButton& button : m_controls) {
            if (button.kind == kind && button.shown) return &button;
        }
        return nullptr;
    };
    // Text on the view, centred on `at` or from it as its top-left, a quad a
    // letter over the font's own pages.
    const auto write = [&](const std::string& fontName, const std::string& words, const glm::dvec2& at,
                           double unitsPerFontPx, bool centred, double alpha) {
        const Supersonic::BitmapFont* font = uiFont(fontName);
        if (font == nullptr) return;
        const auto& pages = font->Pages();
        const std::vector<Hud::Glyph> glyphs = centred ? Hud::LayOutText(*font, words, at, unitsPerFontPx)
                                                       : Hud::LayOutTextFrom(*font, words, at, unitsPerFontPx);
        for (const Hud::Glyph& glyph : glyphs) {
            if (glyph.page < 0 || static_cast<std::size_t>(glyph.page) >= pages.size()) continue;
            add(glyph.rect, pages[static_cast<std::size_t>(glyph.page)], white(alpha), glyph.uvOffset,
                glyph.uvOffset + glyph.uvScale);
        }
    };

    // IN THE ORIGINAL'S ORDER OF DRAWING, which is the whole of the layering:
    // there is no depth here, and a quad added later is drawn over one before.

    // 1. The no-portal sign: a level entity, so drawn with the scene, under all
    //    of the UI and both blacks. At full opacity - its see-through look is
    //    the PNG's own alpha.
    if (m_sign.present && !m_sign.image.empty()) add(m_sign.onView, m_sign.image, white(1.0));

    // 2. The tutorial's rings, which ScreenPad draws from its UPDATE and so
    //    ahead of every draw; centred on the pads' corners, sliding with them.
    const bool tutorial = tutorialPads();
    const bool padsShown = find(Control::Left) != nullptr || find(Control::Right) != nullptr;
    // The pulse stops when a level ends, and the ring with it.
    if (const Hud::Ring ring = Hud::RingAt(m_hudRules, m_levelAgeMs, tutorial);
        ring.shown && padsShown && !m_finishing && !m_dying && !m_ringImage.empty()) {
        for (const Hud::Side side : {Hud::Side::Left, Hud::Side::Right}) {
            Hud::Rect rect;
            rect.size = glm::dvec2(ring.sizeUnits);
            rect.min = Hud::PadCorner(m_hudRules, side, view, m_levelAgeMs) - rect.size * 0.5;
            add(rect, m_ringImage, white(ring.alpha));
        }
    }

    // 3. The UI layer, in the order its sprites were added: restart and pause
    //    (GameLayer), the plaque and then its medal over it (Game::preLoop), and
    //    the clear-portals button, which PortalManager adds while playing.
    // Each at the alpha layOutControls left it: its 120, or what a death's
    // dismiss has left of it.
    for (const Control kind : {Control::Reset, Control::Menu}) {
        if (const ControlButton* button = find(kind)) add(button->rect, button->image, white(button->alpha));
    }
    if (!m_plaqueImage.empty() && !m_medalImage.empty()) {
        // As GameLayer's last update left it (tickPlaqueDismissal).
        const double plaque = m_plaqueAlpha;
        const auto centred = [](const glm::dvec2& centre, const glm::dvec2& size) {
            return Hud::Rect{centre - size * 0.5, size};
        };
        add(centred(m_hudRules.plaque.centreUnits, m_hudRules.plaque.sizeUnits), m_plaqueImage, white(plaque));
        add(centred(m_hudRules.plaque.medalCentreUnits, m_hudRules.plaque.medalSizeUnits), m_medalImage,
            white(plaque));
    }
    if (const ControlButton* clear = find(Control::Clear)) add(clear->rect, clear->image, white(clear->alpha));

    // 4. The pause, when one is up: CustomGameMenuLayer, the CURRENT UI layer,
    //    which UILayerManager::draw draws after GameLayer - so over restart,
    //    pause, the plaque and clear-portals, each frozen at what it was - and,
    //    being the UI layer manager's, before either black (spec 0.5, 2.2). The
    //    pictures and alphas are sim/Pause's; the two texts come last, as
    //    GameMenuLayer::draw and CustomGameMenuLayer::draw draw them after
    //    UILayer::draw.
    if (m_pause.open) {
        for (const Pause::Sprite& sprite :
             Pause::Sprites(m_pauseRules, m_pause.level, PauseSwitches(), view, m_pause.clockMs)) {
            const float alpha = static_cast<float>(sprite.alphaByte) / 255.0f;
            if (sprite.element == Pause::Element::Dim) {
                // square.png is opaque white in every texel, so tinted black it is
                // exactly a plain black.
                add(sprite.rect, std::string(), glm::vec4(0.0f, 0.0f, 0.0f, alpha));
                continue;
            }
            const auto image = m_pauseImages.find(sprite.file);
            if (image == m_pauseImages.end() || image->second.empty()) continue;
            add(sprite.rect, image->second, glm::vec4(1.0f, 1.0f, 1.0f, alpha));
        }
        const double text = static_cast<double>(Pause::TextAlphaByte(m_pauseRules, m_pause.clockMs)) / 255.0;
        write(m_pauseRules.title.font, Pause::TitleText(m_pauseRules, m_pause.level),
              Pause::TitleCentre(m_pauseRules, view), m_pauseRules.title.unitsPerFontPx, true, text);
        write(m_pauseRules.goldenNumber.font, Pause::GoldenText(m_pause.level),
              Pause::GoldenCentre(m_pauseRules, view), m_pauseRules.goldenNumber.unitsPerFontPx, true, text);
    }

    // 4a. A popup, when one is up: the CURRENT UI layer, drawn where the pause is,
    //     over restart, pause and the plaque (frozen, or not yet come in at a level's
    //     start) and under both blacks and the caption (spec 5.2's order). The
    //     pieces are sim/Popup's: the dim, the card, the close button, then the
    //     class's own draw() - turned sprites turned in the view's square units,
    //     which the overlay is told in fractions of this view's shape.
    if (m_popup.open && m_popup.cls != nullptr) {
        const float aspect = static_cast<float>(view.x / view.y);
        for (const Popup::Piece& piece : Popup::Pieces(m_popupRules, *m_popup.cls, m_popup.state, view)) {
            const glm::vec4 colour(glm::vec3(piece.rgb), static_cast<float>(piece.alphaByte) / 255.0f);
            if (piece.element == Popup::Element::Dim) {
                // eth_framework_square.png is opaque white in every texel, so tinted
                // black it is exactly a plain black.
                add(piece.rect, std::string(), colour);
                continue;
            }
            const auto image = m_popupImages.find(piece.sprite);
            if (image == m_popupImages.end() || image->second.empty() || colour.a <= 0.0f) continue;
            ScreenOverlay::Quad quad;
            quad.min = glm::vec2(piece.rect.min / view);
            quad.max = glm::vec2(piece.rect.Max() / view);
            quad.uvMin = glm::vec2(piece.uvMin);
            quad.uvMax = glm::vec2(piece.uvMax);
            quad.color = colour;
            quad.texture = image->second;
            if (piece.angleDeg != 0.0) {
                quad.basis = ScreenOverlay::Rotation(glm::radians(static_cast<float>(piece.angleDeg)), aspect);
            }
            overlay.Add(std::move(quad));
        }
    }

    // 4b. The finished or the lost screen, when one is up: the CURRENT UI layer
    //     over the running level and whatever the HUD has left, and under the
    //     walk pads, which fade on through its veil (spec 3.4's order, 3.2). The
    //     pieces are sim/LevelEnd's, in LevelFinishedLayer's and LevelLostLayer's
    //     own order. The veil is fade_edge.png stretched a screen and a half (or
    //     0.9 of one) wide: drawn as the three strips of a CLAMPED texture, since
    //     the overlay's sampler repeats, and a repeat would blend the opaque first
    //     texel with the clear last one across the screen's left edge.
    if (overLevel) {
        const auto image = [this](const std::string& file) -> const std::string& {
            static const std::string none;
            const auto found = m_endImages.find(file);
            return found != m_endImages.end() ? found->second : none;
        };
        for (const LevelEnd::Piece& piece : endPieces(view, m_end.clockMs)) {
            const double alpha = static_cast<double>(piece.alphaByte) / 255.0;
            switch (piece.element) {
            case LevelEnd::Element::Counter:
            case LevelEnd::Element::GoldenNumber:
            case LevelEnd::Element::CrystalCount:
                write(piece.file, piece.words, piece.at, piece.unitsPerFontPx, piece.centred, alpha);
                break;
            case LevelEnd::Element::Veil: {
                const std::string& veil = image(piece.file);
                const auto texels = m_endTexels.find(piece.file);
                if (veil.empty() || texels == m_endTexels.end()) break;
                for (const LevelEnd::Strip& strip : LevelEnd::ClampedStrips(piece.rect, texels->second)) {
                    add(strip.rect, veil, white(alpha), strip.uvMin, strip.uvMax);
                }
                break;
            }
            case LevelEnd::Element::Title:
            case LevelEnd::Element::PortalsPlaque:
            case LevelEnd::Element::GoldenPlaque:
            case LevelEnd::Element::Button:
            case LevelEnd::Element::Medal:
            case LevelEnd::Element::Crystal:
                if (const std::string& file = image(piece.file); !file.empty()) add(piece.rect, file, white(alpha));
                break;
            }
        }
    }

    // 5. The two blacks, with the pads between them: BaseState's FadeInController,
    //    the character's pads, then Game's own FadeInController. In display
    //    values, as the original drew them, so each is its decoded alpha as it
    //    stands - two stacked blacks leave (t / 700)^2 of the picture, t being
    //    their own age: the level's less overlay.startAfterMs (step 40). On the
    //    FRAME clock, because FadeInController reads GetTime(), which a pause does
    //    not stop; the pads run on game time, which it does.
    const Hud::Rect whole{glm::dvec2(0.0), view};
    const int over = m_hudRules.overlay.layersOverPads;
    const int under = m_hudRules.overlay.layers - over;
    const auto black = [](double alpha) { return glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(alpha)); };
    add(whole, std::string(), black(Hud::OverlayLayersAlpha(m_hudRules, LevelFrameMs(), under)));
    for (const Control kind : {Control::Left, Control::Right}) {
        if (const ControlButton* button = find(kind)) add(button->rect, button->image, white(button->alpha));
    }
    add(whole, std::string(), black(Hud::OverlayLayersAlpha(m_hudRules, LevelFrameMs(), over)));

    // 6. "Part N", over everything, laid out for this view's width. On the frame
    //    clock too: ETHTextDrawer::Draw adds the engine's own frame time, so a
    //    caption still fading when a pause opens goes on fading above it. The
    //    popups' captures show that; no capture of a pause does (spec 2.2, U3).
    const double caption = Hud::CaptionAlpha(m_hudRules, LevelFrameMs());
    if (caption > 0.0 && !m_captionText.empty()) {
        const auto& pages = m_captionFont.Pages();
        for (const Hud::Glyph& glyph : Hud::LayOutCaption(m_hudRules, m_captionFont, m_captionText, view)) {
            // Matura84 keeps "Part 1"'s P and r on page 0 and its a, t and 1 on
            // page 2, so each letter names its own page.
            if (glyph.page < 0 || static_cast<std::size_t>(glyph.page) >= pages.size()) continue;
            add(glyph.rect, pages[static_cast<std::size_t>(glyph.page)], white(caption), glyph.uvOffset,
                glyph.uvOffset + glyph.uvScale);
        }
    }
}

const MagicPortalsLayer::ControlButton* MagicPortalsLayer::controlUnderPointer(const entt::registry& registry) const {
    using Supersonic::Input;
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport == nullptr || !viewport->pointerOverGame) return nullptr;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return nullptr;
    // The pointer as a place on the VIEW: the viewport is the view, stretched
    // to it, so no camera is needed to say which control a tap is on.
    const glm::dvec2 view = ViewPx();
    const glm::dvec2 at = glm::dvec2(viewport->ToLocal(Input::MousePosition()) / size) * view;
    for (const ControlButton& button : m_controls) {
        if (!button.shown) continue;
        bool on = false;
        switch (button.kind) {
        case Control::Left:
            on = Hud::OnPad(m_hudRules, Hud::Side::Left, view, m_levelAgeMs, at);
            break;
        case Control::Right:
            on = Hud::OnPad(m_hudRules, Hud::Side::Right, view, m_levelAgeMs, at);
            break;
        case Control::Reset:
        case Control::Menu:
        case Control::Clear:
            on = button.rect.Contains(at);
            break;
        }
        if (on) return &button;
    }
    return nullptr;
}

float MagicPortalsLayer::keyDirection() const {
    using Supersonic::Input;
    float direction = 0.0f;
    if (Input::IsDown(kLeft) || Input::IsDown(kLeftAlt)) direction -= 1.0f;
    if (Input::IsDown(kRight) || Input::IsDown(kRightAlt)) direction += 1.0f;
    // DEV ONLY: a held walk a capture scheduled for this tick.
    for (const DevHold& hold : m_devHolds) {
        if (m_ticks >= hold.from && m_ticks <= hold.to) direction += hold.direction;
    }
    return std::clamp(direction, -1.0f, 1.0f);
}

float MagicPortalsLayer::readInput(entt::registry& registry) {
    using Supersonic::Input;
    float direction = keyDirection();

    // The on-screen controls, laid out and asked about before anything else
    // reads the pointer.
    layOutControls();

    // THE BACK KEY PAUSES, where the pause control could be pressed:
    // GameState::handleBackButton (bytes 98331..98935) shows the pause while
    // GameLayer is current. It used to leave the level for its grid, which is
    // now the pause's own first button.
    if (pauseAllowed() && (Input::TickWasPressed(kBack) || devPressDue(DevPress::Pause))) {
        openPause(registry);
        return direction;
    }
    // DEV ONLY: a scheduled tap owns the pointer on its ticks, wherever the real
    // cursor happens to rest over the window.
    const bool devTap = std::any_of(m_devTaps.begin(), m_devTaps.end(), [this](const DevTap& tap) {
        return tap.tick <= m_ticks;
    });
    const ControlButton* under = devTap ? nullptr : controlUnderPointer(registry);
    const bool held = Input::IsDown(kTap);
    const bool pressed = Input::TickWasPressed(kTap);

    // A HELP BLOCK takes a touch that goes down on it - no portal, no control -
    // and opens its popup when that touch comes up; the popup stops the level on
    // this tick, as the pause control does. The controls are asked first: they
    // are GameLayer's buttons, which the block's disabled touches do not reach.
    if (under == nullptr && helpBlockInput(registry)) return direction;

    if (under != nullptr) {
        switch (under->kind) {
        // HELD, not tapped: a walk button is leaned on, and IsDown is what a
        // replayed tick feeds (Input.hpp lists it among the queries a replay
        // diverts), so the suites can drive these exactly as they drive a key.
        case Control::Left:
            if (held) direction -= 1.0f;
            break;
        case Control::Right:
            if (held) direction += 1.0f;
            break;
        // And these are EDGES: held down, a reset would restart the level every
        // tick for as long as the button was pressed.
        case Control::Reset:
            if (pressed && m_current >= 0 && !m_chapterComplete) loadLevel(registry, m_current);
            break;
        // GameLayer's menu button calls GameState::showMenuPopup (bytes
        // 98935..99036): the pause, over this level, rather than out of it.
        case Control::Menu:
            if (pressed) openPause(registry);
            break;
        // PortalManager::killAll(true): the placed portals go, those that never
        // carried anything are given back, and it plays playPortalKilledSound
        // once whatever the count.
        case Control::Clear:
            if (pressed && m_level.portals.KillAll(true) > 0) latch("portal_spent");
            break;
        }
        // A TAP THAT WORKED A CONTROL IS SPENT. Without this, leaning on the
        // walk arrow also fires a portal into the floor beneath it every time
        // the cooldown allows - the control would work and quietly cost the
        // player their portal budget.
        return direction;
    }

    // A pointer over a panel or another window is not the game's.
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport != nullptr && viewport->pointerOverGame && pressed) {
        glm::dvec2 atPx(0.0);
        if (ScreenToLevelPx(registry, Input::MousePosition(), atPx)) m_level.portals.Shoot(registry, atPx);
    }
    return direction;
}

// ---- what the game thinks it is drawing --------------------------------------
//
// Written down as it CHANGES, and in full when G asks.
//
// The point is one distinction and nothing else. When a sprite disappears from
// the screen there are four explanations and a player cannot tell them apart:
//
//   1. this layer took the quad away        - a "gone" line appears below
//   2. the renderer culled it               - drawn falls, culled rises
//   3. a pass refused the draw              - dropped rises
//   4. it was drawn and produced no pixels  - nothing changes anywhere
//
// A screenshot cannot separate those and neither can any test in this
// repository: every headless run agrees the transforms, the interpolation and
// the frustum are correct. So the game says what it believes, the counters say
// what the frame did, and the difference between them is the answer.
void MagicPortalsLayer::reportSprites(entt::registry& registry) {
    using namespace Supersonic;
    if (!m_loaded || m_sprites.empty()) return;
    if (m_camera == entt::null || !registry.valid(m_camera)) return;

    // What the camera shows, in world metres, exactly as the renderer's own
    // cull will judge it.
    const auto& camera = registry.get<CameraComponent>(m_camera);
    const float halfHeight = camera.orthoHeight * 0.5f;
    const float halfWidth = halfHeight * camera.aspect;
    const glm::vec2 viewMin(camera.position.x - halfWidth, camera.position.y - halfHeight);
    const glm::vec2 viewMax(camera.position.x + halfWidth, camera.position.y + halfHeight);

    m_onScreenLast.resize(m_sprites.size(), char{0});

    const Supersonic::RenderSystem::Stats** slot =
        registry.ctx().find<const Supersonic::RenderSystem::Stats*>();
    const Supersonic::RenderSystem::Stats* stats = slot != nullptr ? *slot : nullptr;

    const auto counters = [stats]() -> std::string {
        if (stats == nullptr) return std::string(" (no counters)");
        return " [drawn " + std::to_string(stats->drawn) + ", culled " + std::to_string(stats->culled) +
               ", blended " + std::to_string(stats->transparentDrawn) + ", dropped " +
               std::to_string(stats->dropped) + "]";
    };

    int onScreen = 0;
    for (std::size_t i = 0; i < m_sprites.size(); ++i) {
        const DrawnSprite& drawn = m_sprites[i];

        bool showing = false;
        glm::vec3 centre(0.0f);
        glm::vec3 half(0.0f);
        if (drawn.quad != entt::null && registry.valid(drawn.quad)) {
            const auto& transform = registry.get<TransformComponent>(drawn.quad);
            const auto* renderable = registry.try_get<RenderableComponent>(drawn.quad);
            // The quad primitive is one unit on a side and centred, so its box
            // is its own scale about its own position.
            centre = transform.position;
            half = transform.scale * 0.5f;
            showing = renderable != nullptr && renderable->isVisible &&
                      centre.x + half.x >= viewMin.x && centre.x - half.x <= viewMax.x &&
                      centre.y + half.y >= viewMin.y && centre.y - half.y <= viewMax.y;
        }
        if (showing) ++onScreen;

        const char was = m_onScreenLast[i];
        const char now = showing ? char{1} : char{0};
        m_onScreenLast[i] = now;
        if (was == now && m_reportedOnce && !m_dumpRequested) continue;

        // Only what changed, unless G asked for everything. A level draws
        // twenty of these and a walk crosses an edge every few seconds, so the
        // log stays short enough to read.
        if (m_dumpRequested || was != now) {
            const auto* renderable = drawn.quad != entt::null && registry.valid(drawn.quad)
                                         ? registry.try_get<RenderableComponent>(drawn.quad)
                                         : nullptr;
            const auto* material = drawn.quad != entt::null && registry.valid(drawn.quad)
                                       ? registry.try_get<MaterialComponent>(drawn.quad)
                                       : nullptr;
            SUPERSONIC_LOG_INFO("Magic Portals")
                << (drawn.quad == entt::null ? "GONE    " : (showing ? "on      " : "off     "))
                << drawn.sprite.texture
                << "  at (" << centre.x << ", " << centre.y << ") size (" << half.x * 2.0f << " x "
                << half.y * 2.0f << ")"
                << "  visible=" << (renderable != nullptr && renderable->isVisible ? 1 : 0)
                << " mesh=" << (renderable != nullptr ? renderable->meshID : 0u)
                << " albedo=" << (renderable != nullptr ? renderable->albedoTextureID : 0u)
                << " alpha=" << (material != nullptr ? material->albedoColor.a : -1.0f)
                << counters() << std::endl;
        }
    }

    // A frame that refused a draw says so once, loudly, whatever else changed:
    // it is the one outcome that was invisible to every counter until now.
    if (stats != nullptr && stats->dropped > 0) {
        SUPERSONIC_LOG_ERROR("Magic Portals")
            << "the renderer REFUSED " << stats->dropped
            << " draw(s) this frame; its instance buffer was full." << std::endl;
    }

    if (m_dumpRequested) {
        SUPERSONIC_LOG_INFO("Magic Portals")
            << "-- " << onScreen << " of " << m_sprites.size() << " sprite(s) on screen"
            << counters() << std::endl;
    }
    m_dumpRequested = false;
    m_reportedOnce = true;
}

// ---- sound ------------------------------------------------------------------
//
// The original's AudioManager, read from the port's sounds.json
// (sim/Sounds.hpp) and fired from what the port can watch its own simulation
// do: a counter that went up, a flag that turned over.
//
// The division is the particles', for the particles' reason. The TICK only
// latches an event's NAME; the FRAME plays it. So no clip, no random draw and
// no missing file can reach Game::Level, the simulation's clock or the state
// hash, and a run with no audio device at all takes exactly the same path
// through the simulation - which is how every suite runs, and what keeps a
// replay a replay.

void MagicPortalsLayer::loadSounds() {
    std::string error;
    if (!Sounds::LoadRules(m_paths.portData + "/sounds.json", m_soundRules, error)) {
        // A game with no sound is still a game, so this does not refuse to
        // start - but it says so once, because the alternative is silence that
        // looks exactly like silence nobody asked about.
        m_soundsError = error;
        SUPERSONIC_LOG_ERROR("Magic Portals") << "no sound: " << error << std::endl;
    }
}

std::vector<std::string> MagicPortalsLayer::LatchedSounds() const {
    std::vector<std::string> events;
    events.reserve(m_latched.size());
    for (const Latched& latched : m_latched) events.push_back(latched.event);
    return events;
}

void MagicPortalsLayer::latch(const char* event, double doorStrideMs) {
    // Only what the table names. An event it leaves silent, or does not know,
    // costs nothing here and says nothing.
    if (m_soundRules.ForEvent(event) == nullptr) return;
    m_latched.push_back(Latched{event, doorStrideMs});
}

double MagicPortalsLayer::soundRandom(double from, double to) {
    if (to <= from) return from;
    std::uniform_real_distribution<double> spread(from, to);
    return spread(m_soundRandom);
}

float MagicPortalsLayer::pitchFor(const Sounds::Hook& hook, double doorStrideMs) {
    double speed = hook.speed;
    if (hook.speedIsRandom) {
        speed = soundRandom(hook.speedFrom, hook.speedTo);
    } else if (hook.speedFromDoorStride) {
        // playDoorOpenSound scales by 3000 / the door's own stride, so a slow
        // door is a slow sound.
        speed = doorStrideMs > 0.0 ? 3000.0 / doorStrideMs : 1.0;
    }
    // XAudio2 allows a source voice a frequency ratio of 2 unless it was
    // created to allow more, and the engine creates them plainly. A door
    // quicker than 1500 ms asks for more than that, so this clamps rather than
    // letting the voice fail - a divergence, and stated: the original has no
    // such ceiling.
    if (speed < 0.25) speed = 0.25;
    if (speed > 2.0) speed = 2.0;
    return static_cast<float>(speed);
}

void MagicPortalsLayer::latchSimSounds() {
    if (!m_loaded) return;

    Watch now;
    now.valid = true;
    now.portalsUsed = m_level.portals.portalsUsed;
    now.traversals = m_level.portals.traversals;
    now.shotsFired = m_level.portals.shotsFired;
    now.shotsFailed = m_level.portals.shotsFailed;
    now.reflections = m_level.portals.reflections;
    for (const Goals::Crystal& crystal : m_level.goals.crystals) {
        if (crystal.collected) ++now.crystalsCollected;
        if (crystal.expired) ++now.crystalsExpired;
    }
    for (const Portals::Static& portal : m_level.portals.statics) {
        if (portal.live) ++now.staticsLive;
    }
    now.wallsBroken = m_level.demolish.Broken();
    for (const Launchers::Launcher& launcher : m_level.launchers.launchers) now.stonesThrown += launcher.thrown;
    // Summed across the carrancas, as the launchers' throws are: each keeps its
    // own count, and what the sound wants is how many were spat in all.
    for (const Turrets::Turret& turret : m_level.turrets.turrets) now.fireballsSpat += turret.fired;
    if (m_level.boss.beholder.has_value()) {
        now.bossHits = m_level.boss.beholder->hits;
        now.bossVolleys = m_level.boss.beholder->volleys;
        now.bossFrame = m_level.boss.beholder->frame;
        now.bossGone = m_level.boss.beholder->gone;
    }
    now.bossRocksBroken = m_level.boss.rocksBroken;
    now.bossButtonRaised = m_level.boss.buttonRaised;
    now.doorsOpening.reserve(m_level.channels.doors.size());
    for (const Puzzle::SwitchedDoor& door : m_level.channels.doors) {
        now.doorsOpening.push_back(door.motion.opening ? char{1} : char{0});
    }
    now.liftsForward.reserve(m_level.movers.lifts.size());
    for (const Mover::Lift& lift : m_level.movers.lifts) {
        now.liftsForward.push_back(lift.motion.forward ? char{1} : char{0});
    }

    // The first tick of a level sets the mark rather than playing against a
    // zeroed one: otherwise a level that begins with two static portals and a
    // standing door would announce all of it in its opening frame.
    if (!m_watch.valid) {
        m_watch = std::move(now);
        return;
    }

    for (int i = m_watch.portalsUsed; i < now.portalsUsed; ++i) latch("portal_placed");
    for (int i = m_watch.traversals; i < now.traversals; ++i) latch("traversal");
    for (int i = m_watch.shotsFired; i < now.shotsFired; ++i) latch("shot_fired");
    for (int i = m_watch.shotsFailed; i < now.shotsFailed; ++i) latch("shot_failed");
    for (int i = m_watch.reflections; i < now.reflections; ++i) latch("shot_reflected");
    for (int i = m_watch.crystalsCollected; i < now.crystalsCollected; ++i) latch("crystal_collected");
    for (int i = m_watch.crystalsExpired; i < now.crystalsExpired; ++i) latch("crystal_expired");
    // A static portal goes when it is spent, so this count FALLS.
    for (int i = now.staticsLive; i < m_watch.staticsLive; ++i) latch("portal_spent");
    for (int i = m_watch.wallsBroken; i < now.wallsBroken; ++i) latch("wall_broken");
    for (int i = m_watch.stonesThrown; i < now.stonesThrown; ++i) latch("stone_thrown");
    for (int i = m_watch.fireballsSpat; i < now.fireballsSpat; ++i) latch("fireball_spat");
    for (int i = m_watch.bossHits; i < now.bossHits; ++i) latch("boss_hurt");
    for (int i = m_watch.bossVolleys; i < now.bossVolleys; ++i) latch("boss_spikes");
    for (int i = m_watch.bossRocksBroken; i < now.bossRocksBroken; ++i) latch("boss_rock_broken");
    if (now.bossFrame == 1 && m_watch.bossFrame != 1) latch("boss_eyes_shut");
    if (now.bossGone && !m_watch.bossGone) latch("boss_dead");
    if (now.bossButtonRaised && !m_watch.bossButtonRaised) latch("boss_button_raised");

    // A door that started moving, with its own stride: the hook's speed is
    // 3000 / it, so a slow door sounds slow.
    const std::size_t doors = now.doorsOpening.size() < m_watch.doorsOpening.size()
                                  ? now.doorsOpening.size()
                                  : m_watch.doorsOpening.size();
    for (std::size_t i = 0; i < doors; ++i) {
        if (now.doorsOpening[i] == m_watch.doorsOpening[i]) continue;
        const double strideMs = static_cast<double>(m_level.channels.doors[i].motion.durationS) * 1000.0;
        latch(now.doorsOpening[i] != 0 ? "door_opened" : "door_closed", strideMs);
    }
    // And a lift each time it turns, which is each end of its run.
    const std::size_t lifts = now.liftsForward.size() < m_watch.liftsForward.size()
                                  ? now.liftsForward.size()
                                  : m_watch.liftsForward.size();
    for (std::size_t i = 0; i < lifts; ++i) {
        if (now.liftsForward[i] != m_watch.liftsForward[i]) latch("lift_turned");
    }

    m_watch = std::move(now);
}

void MagicPortalsLayer::playLatched(entt::registry& registry, float deltaTime) {
    // The frames' own clock, which the shared timers are kept on.
    m_soundClockMs += static_cast<double>(deltaTime) * 1000.0;
    if (m_latched.empty()) return;
    // The sound switch off is SetGlobalVolume(0): nothing is heard, and what the
    // tick latched is dropped rather than kept for later.
    if (!m_soundOn) {
        m_latched.clear();
        return;
    }

    Supersonic::AudioEngine** slot = registry.ctx().find<Supersonic::AudioEngine*>();
    Supersonic::AudioEngine* audio = slot != nullptr ? *slot : nullptr;
    if (audio == nullptr || !audio->IsAvailable()) {
        // No device - a suite, a headless render, or Linux, where the original's
        // mp3s do not decode. The events are DROPPED rather than kept: a queue
        // that grows while nothing plays it is a leak with a delay on it.
        m_latched.clear();
        return;
    }

    const std::string directory = Sounds::Directory(m_paths.original) + "/";
    for (const Latched& latched : m_latched) {
        const Sounds::Hook* hook = m_soundRules.ForEvent(latched.event);
        if (hook == nullptr || hook->files.empty()) continue;

        // The shared timers. The original keeps one Timer per group rather
        // than one per hook, so the two crystal sounds hold EACH OTHER off -
        // which is what stops a run of pickups from stacking into a chord.
        if (!hook->timer.empty() && hook->minIntervalMs > 0.0) {
            const auto at = m_timerAtMs.find(hook->timer);
            if (at != m_timerAtMs.end() && m_soundClockMs - at->second < hook->minIntervalMs) continue;
            m_timerAtMs[hook->timer] = m_soundClockMs;
        }

        const float volume = static_cast<float>(hook->volume);
        const float pitch = pitchFor(*hook, latched.doorStrideMs);
        if (hook->both || hook->files.size() == 1) {
            // Two samples TOGETHER, which is what an explosion is.
            for (const std::string& file : hook->files) audio->Play(directory + file, false, volume, pitch);
        } else {
            // Or one of the two, drawn: the crystal gathers, the wood knocks.
            const auto which = static_cast<std::size_t>(soundRandom(0.0, static_cast<double>(hook->files.size())));
            audio->Play(directory + hook->files[which < hook->files.size() ? which : 0], false, volume, pitch);
        }
    }
    m_latched.clear();
}

void MagicPortalsLayer::updateMusic(entt::registry& registry) {
    // Which track belongs to what is on screen. The medal screen keeps the
    // level loaded, so it falls into the level's own branch and the music does
    // not stop underneath it.
    std::string wanted;
    if (!m_soundOn || !m_musicOn) {
        // Either switch off: no music at all.
    } else if (m_screen == Screen::Main || m_screen == Screen::Worlds || m_screen == Screen::Levels ||
               m_screen == Screen::Credits || m_screen == Screen::Achievements) {
        // The menu's track goes on under credits and the dashboard, which play none.
        wanted = "menu";
    } else if (m_loaded) {
        // Game.angelscript starts playGameMusic(isBossFight); the port's boss
        // levels are the ones that built a beholder.
        wanted = m_level.boss.beholder.has_value() ? "boss" : "game";
    }
    if (wanted == m_track) return;

    Supersonic::AudioEngine** slot = registry.ctx().find<Supersonic::AudioEngine*>();
    Supersonic::AudioEngine* audio = slot != nullptr ? *slot : nullptr;
    // No device: m_track is left alone, so the right track starts if one ever
    // appears, rather than the layer believing it already did.
    if (audio == nullptr || !audio->IsAvailable()) return;

    if (m_musicVoice != Supersonic::AudioEngine::kInvalidVoice) {
        audio->Stop(m_musicVoice);
        m_musicVoice = Supersonic::AudioEngine::kInvalidVoice;
    }
    m_track = wanted;
    if (m_track.empty()) return;
    const Sounds::Track* track = m_soundRules.FindTrack(m_track);
    if (track == nullptr) return;
    m_musicVoice = audio->Play(Sounds::Directory(m_paths.original) + "/" + track->file, track->loop,
                               static_cast<float>(track->volume), 1.0f);
}

void MagicPortalsLayer::stopMusic(entt::registry& registry) {
    if (m_musicVoice == Supersonic::AudioEngine::kInvalidVoice) {
        m_track.clear();
        return;
    }
    Supersonic::AudioEngine** slot = registry.ctx().find<Supersonic::AudioEngine*>();
    if (slot != nullptr && *slot != nullptr) (*slot)->Stop(m_musicVoice);
    m_musicVoice = Supersonic::AudioEngine::kInvalidVoice;
    m_track.clear();
}

void MagicPortalsLayer::OnFixedUpdate(entt::registry& registry, float fixedDelta) {
    using Supersonic::Input;
    ++m_ticks;
    // The layer's own clock: what the original's absolute GetTimeF reads give.
    m_layerClockMs += static_cast<double>(fixedDelta) * 1000.0;
    // Asked for on the TICK, where a key press is an edge, and answered on the
    // frame, where the picture is.
    if (Input::TickWasPressed(kDump)) m_dumpRequested = true;
    const bool overLevel = m_screen == Screen::Finished || m_screen == Screen::Dead;
    // A menu is up instead of a level: it takes the tick, and nothing below
    // runs. The two are never both in the registry.
    if (m_screen != Screen::None && !overLevel) {
        menuTick(registry, fixedDelta);
        updateHud(registry);
        return;
    }
    // A pause is up over the level: it takes the tick, and the level does not
    // step. Only on the tick it resumes does the level take up its tick again,
    // from exactly where the tap left it.
    if (m_pause.open) {
        if (pauseTick(registry, fixedDelta) && m_loaded && m_screen == Screen::None) {
            // The tick the pause was opened on read its input and stopped short
            // of BeforeStep; this one runs that half now, with the walk the keys
            // ask for - the pointer is on the button that resumed it, and a tap
            // spent on a button is spent.
            stepLevel(registry, keyDirection(), fixedDelta);
        }
        tickPlaqueDismissal(false);
        updateHud(registry);
        return;
    }
    // A popup the same way. One a help block raised mid-tick owes that tick's
    // BeforeStep half, as the pause does; one raised as the level loaded owes
    // nothing, and the next tick is the level's first.
    if (m_popup.open) {
        if (popupTick(registry, fixedDelta) && m_loaded && m_screen == Screen::None) {
            stepLevel(registry, keyDirection(), fixedDelta);
        }
        tickPlaqueDismissal(false);
        updateHud(registry);
        return;
    }
    // The finished or lost screen is up over a level that GOES ON RUNNING (spec
    // D7): the screen's clock, counters and buttons first - a button that leaves
    // the level leaves it before it is stepped again - and then the level's own
    // tick below, which reads no input once it has ended.
    if (overLevel && endScreenTick(registry, fixedDelta)) {
        updateHud(registry);
        return;
    }
    // The bodies' boxes over the art, or not: the picture only.
    if (Input::TickWasPressed(kBoxes)) m_showBoxes = !m_showBoxes;
    if (!overLevel) {
        // Out of a level that is not being played - one the port refused, or a
        // chapter's end, which otherwise has nowhere to go - to its grid. A level
        // being played takes the back key as the pause (readInput).
        if (m_current >= 0 && !m_loaded && Input::TickWasPressed(kBack)) {
            m_menuWorld = m_chapters.levels[static_cast<std::size_t>(m_current)].world;
            openMenu(registry, Screen::Levels);
            updateHud(registry);
            return;
        }
        // Skip and retry first, so the tick that asks plays the level it lands on.
        if (m_current >= 0 && Input::TickWasPressed(kSkip)) {
            goTo(registry, m_chapters.Next(m_current));
        } else if (m_current >= 0 && !m_chapterComplete && Input::TickWasPressed(kRetry)) {
            loadLevel(registry, m_current);
        }
        // A level that raised its tutorial as it loaded stops HERE, as one loaded
        // from a menu does: not a tick older and not stepped under its popup, so a
        // retried 1-02 stands at age 0 as the first load of it does.
        if (GameTimeStopped()) {
            updateHud(registry);
            return;
        }
    }

    const double dtMs = static_cast<double>(fixedDelta) * 1000.0;
    if (m_loaded) {
        // The app has just stepped physics. So first what follows a step...
        Game::AfterStep(m_data, registry, m_level, fixedDelta);
        // What the tick just did, remembered for the frame to play. Before the
        // branches below, which may put a screen up.
        latchSimSounds();
        // ...and the moment the exit reports, the beat before the medal; the
        // moment the player dies, the beat before the lost screen. Should both
        // come on one tick, reaching the exit wins: the remake's order of two
        // triggers in a frame is not defined. And once either has begun, the
        // other cannot: the level runs on behind the beat and the screen, and a
        // hidden player neither finishes nor dies again.
        if (m_finishing) {
            // Already in the door: the level keeps running behind the effect,
            // as the original's does, and the score comes when the beat is up.
            m_finishClockMs += dtMs;
            m_padEndByte = LevelEnd::PadDecayByte(m_levelEndRules, m_padEndByte, 1);
            if (m_screen == Screen::None && m_finishClockMs >= m_levelEndRules.wonDelayMs) clearLevel(registry);
        } else if (m_dying) {
            // Killed, and the lost screen not up yet: the level goes on running
            // behind it exactly as it does behind the door's effect.
            m_dyingClockMs += dtMs;
            m_padEndByte = LevelEnd::PadDecayByte(m_levelEndRules, m_padEndByte, 1);
            if (m_screen == Screen::None && m_dyingClockMs >= m_levelEndRules.lostDelayMs) openDead(registry);
        } else if (m_level.goals.completed) {
            // GOING IN. Its sound is the door's, not the medal's - `level_finished`
            // now maps to playFinalDoorSound, and sounds.json carries the decode
            // of why the cue this once had is called by nothing in the original.
            latch("level_finished");
            // Hide(), and the velocity zeroed with it: the character is gone from
            // the doorway and cannot be left walking on the spot. Destroying the
            // two drawables IS the port's Hide - syncDrawables skips what is not
            // there - and the body stays in the world so nothing else that holds
            // it has to care.
            if (m_player != entt::null && registry.valid(m_player)) registry.destroy(m_player);
            m_player = entt::null;
            if (m_playerQuad != entt::null && registry.valid(m_playerQuad)) registry.destroy(m_playerQuad);
            m_playerQuad = entt::null;
            if (m_level.player != entt::null && registry.valid(m_level.player)) {
                if (auto* rigid = registry.try_get<Supersonic::RigidBodyComponent>(m_level.player)) {
                    rigid->velocity = glm::vec3(0.0f);
                }
            }
            m_direction = 0.0f;
            m_finishing = true;
            m_finishClockMs = 0.0;
            endHud();
            // Said with the tick, so a capture can be dated from the door.
            SUPERSONIC_LOG_INFO("Magic Portals") << "door reached on tick " << m_ticks << ", pads at "
                                                 << m_padEndByte << std::endl;
        } else if (m_level.hazards.playerDied) {
            // DYING, which is not the same moment as the lost screen, and was
            // the whole of this branch before: death was an instant retry, so
            // there was no screen, no beat and no sound but one played at the
            // wrong instant.
            //
            // Only a FALL has a cue here. checkGameLost plays
            // playDieByFallSound when the player leaves the level and plays
            // nothing at all for an hp death; playDeathSound belongs to the
            // screen, 1400 ms later. sounds.json carries that decode.
            if (m_level.hazards.diedByFalling) latch("player_fell");
            ++m_deaths;
            // Hide(), as the finish does it and as checkGameLost does it too -
            // it hides the character before the delay. Left drawn, a corpse
            // stood in the pit for 1400 ms, which is the same fault as the mage
            // walking on the spot in the doorway.
            if (m_player != entt::null && registry.valid(m_player)) registry.destroy(m_player);
            m_player = entt::null;
            if (m_playerQuad != entt::null && registry.valid(m_playerQuad)) registry.destroy(m_playerQuad);
            m_playerQuad = entt::null;
            if (m_level.player != entt::null && registry.valid(m_level.player)) {
                if (auto* rigid = registry.try_get<Supersonic::RigidBodyComponent>(m_level.player)) {
                    rigid->velocity = glm::vec3(0.0f);
                }
            }
            m_direction = 0.0f;
            m_dying = true;
            m_dyingClockMs = 0.0;
            endHud();
            SUPERSONIC_LOG_INFO("Magic Portals") << "player died on tick " << m_ticks << ", pads at "
                                                 << m_padEndByte << std::endl;
        }
    }
    if (m_loaded) {
        // The level is a tick older. BEFORE the input, because the pads slide
        // in and a tap is tested against where they are on this tick.
        m_levelAgeMs += dtMs;
        // GameLayer is updated on this tick: the plaque's colour is written.
        tickPlaqueDismissal(true);
        if (m_finishing || m_dying) {
            // NOTHING IS PRESSABLE from the door or the death on (spec 3.5):
            // GameLayer is dismissed and no screen is current yet, and once one
            // is its buttons are its own (endScreenTick). The level still runs,
            // with nobody walking it.
            stepLevel(registry, 0.0f, fixedDelta);
            updateHud(registry);
            return;
        }
        // Then this tick's input, and what comes before the next step.
        const float direction = readInput(registry);
        // A control may have opened a pause, or a help block a popup, which stops
        // the level HERE, after the step it has just taken and before the next:
        // the tick it resumes on runs the rest. Or it opened a screen that
        // unloaded the level, and nothing below has a level to step any more.
        if (!m_loaded || m_screen != Screen::None || GameTimeStopped()) {
            updateHud(registry);
            return;
        }
        stepLevel(registry, direction, fixedDelta);
    }
    updateHud(registry);
}

void MagicPortalsLayer::stepLevel(entt::registry& registry, float direction, float fixedDelta) {
    m_direction = direction; // for the picture: which way the player walks this tick
    m_aspect = viewportAspect(registry);
    // BEFORE the tick, not after it: the dragon's claw measures everything
    // from the camera's left edge, and the camera's left edge depends on how
    // wide the window is. Dragon.hpp says why that width is the only piece of
    // the camera the sim takes from outside.
    m_level.dragon.viewWidthPx = ViewPx().x;
    Game::BeforeStep(m_data, registry, m_level, direction, fixedDelta);
    if (m_level.dragon.present) {
        // level31a is the ONE level in all 128 that sets auto_camera, and it
        // sets it to dragon.ent. AutoCameraController::update is a hard lock
        // to that entity's x - no lag, no hold, y pinned to 0 - so the
        // follow's easing does not run here at all. dragon.json has the
        // decode, and Camera::Clamp is the same arithmetic as the original's
        // camMin/camMax expressed for a centre rather than a corner.
        const glm::dvec2 view = ViewPx();
        m_follow.centrePx =
            Camera::Clamp(glm::dvec2(m_level.dragon.atPx.x + view.x * 0.5, view.y * 0.5), view, m_boundsPx);
        m_follow.holdLeftS = 0.0;
    } else {
        const glm::dvec2 playerPx =
            Units::ToPixels(registry.get<Supersonic::TransformComponent>(m_level.player).position);
        m_follow.Tick(m_cameraRules, playerPx, ViewPx(), m_boundsPx, fixedDelta);
    }
    placeCamera(registry);
    layOutControls();
    // AFTER the camera: the sign is aimed at the corner this tick left it at,
    // with its frame time held back while the blacks are whole and handed
    // over in one piece when they start, as the original's load hands it.
    tickNoPortalSign(Hud::HandOver(m_hudRules, m_levelAgeMs, static_cast<double>(fixedDelta) * 1000.0,
                                   m_sign.heldMs));
    syncDrawables(registry);
}

// ---- the pause ----------------------------------------------------------------

bool MagicPortalsLayer::pauseAllowed() const {
    // Where the pause control is drawn and can be pressed: a level being played,
    // not going into its door and not dying.
    return m_hudReady && m_loaded && m_screen == Screen::None && !m_finishing && !m_dying && !GameTimeStopped();
}

Pause::Switches MagicPortalsLayer::PauseSwitches() const {
    Pause::Switches switches;
    switches.soundOn = m_soundOn;
    switches.musicOn = m_musicOn;
    switches.musicAddedMs = m_pause.musicAddedMs;
    switches.musicDismissedMs = m_pause.musicDismissedMs;
    return switches;
}

void MagicPortalsLayer::openPause(entt::registry& registry) {
    using namespace Supersonic;
    if (!pauseAllowed() || m_current < 0) return;
    const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(m_current)];
    m_pause = PauseScreen{};
    m_pause.open = true;
    // What CustomGameMenuLayer's constructor reads: the score recorded for this
    // level, which gates the current-score plaque, its medal and skip, and the
    // golden score its plaque names.
    m_pause.level.savedMedal = m_scores.Get(entry.world, entry.index);
    m_pause.level.goldenScore = entry.goldenScore;
    m_pause.level.index = entry.index;

    // GAME TIME STOPS (STimeManager::pause).
    freezeWorld(registry);
    // The pads go with game time.
    layOutControls();
}

void MagicPortalsLayer::freezeWorld(entt::registry& registry) {
    using namespace Supersonic;
    m_frozen = Frozen{};
    // The level is not stepped from here, but the app steps physics before every
    // tick regardless, so every body is held as it stands now and put back after
    // each of those steps: velocities, sleep and all, so the level resumes as
    // though game time had never stopped.
    for (auto [entity, transform, body] : registry.view<TransformComponent, RigidBodyComponent>().each()) {
        m_frozen.held.push_back(Frozen::Held{entity, transform, body});
    }
    // And the flipbooks, which the app turns on the tick: the world under a
    // popup measured still (temporal std 0.001, spec 0.3), so nothing animates.
    for (auto [entity, animation] : registry.view<SpriteAnimationComponent>().each()) {
        if (!animation.playing) continue;
        animation.playing = false;
        m_frozen.stoppedFlipbooks.push_back(entity);
    }
}

void MagicPortalsLayer::holdWorld(entt::registry& registry) {
    using namespace Supersonic;
    for (const Frozen::Held& held : m_frozen.held) {
        if (!registry.valid(held.entity)) continue;
        if (auto* transform = registry.try_get<TransformComponent>(held.entity)) *transform = held.transform;
        if (auto* body = registry.try_get<RigidBodyComponent>(held.entity)) *body = held.body;
    }
}

void MagicPortalsLayer::thawWorld(entt::registry& registry) {
    using namespace Supersonic;
    // The world as game time left it, this tick's physics step undone too.
    holdWorld(registry);
    for (const entt::entity entity : m_frozen.stoppedFlipbooks) {
        if (!registry.valid(entity)) continue;
        if (auto* animation = registry.try_get<SpriteAnimationComponent>(entity)) animation->playing = true;
    }
    m_frozen = Frozen{};
}

void MagicPortalsLayer::closePause(entt::registry& registry) {
    if (!m_pause.open) return;
    thawWorld(registry);
    // UILayer::hide(true): gone on this frame, no fade, and every element reset,
    // so the next pause plays its whole entrance again (spec 2.4).
    m_pause = PauseScreen{};
    layOutControls();
}

// ---- the popups ------------------------------------------------------------------

std::string MagicPortalsLayer::originalAsset(const std::string& relative) const {
    // THE HD TWIN, as the rest of the port's UI takes it (menuImage), but beside
    // the file wherever it lives: the hand and the stone are entities, the smoke a
    // particle, and those keep their twins in entities/hd/, not sprites/hd/.
    std::error_code ec;
    const std::filesystem::path path(relative);
    const std::string hd = m_paths.original + "/" + (path.parent_path() / "hd" / path.filename()).generic_string();
    if (std::filesystem::exists(hd, ec)) return hd;
    return m_paths.original + "/" + relative;
}

void MagicPortalsLayer::findHelpBlocks() {
    m_helpBlocks.clear();
    if (!m_loaded) return;
    for (const Tscn::Node& node : m_data.scene.nodes) {
        if (node.parent != ".") continue;
        const Tscn::Value* entity = node.Meta("entity_name");
        if (entity == nullptr || entity->kind != Tscn::Value::Kind::String || entity->text != m_popupRules.helpBlock.entity) {
            continue;
        }
        HelpBlock block;
        const Tscn::Value* box = node.Meta("trigger_size");
        if (!PositionOf(&node, block.atUnits) || box == nullptr || box->kind != Tscn::Value::Kind::Vector2) continue;
        block.boxUnits = glm::dvec2(box->numbers[0], box->numbers[1]);
        m_helpBlocks.push_back(block);
    }
}

std::vector<Hud::Rect> MagicPortalsLayer::HelpBlockRects() const {
    std::vector<Hud::Rect> rects;
    const glm::dvec2 corner = m_follow.centrePx - ViewPx() * 0.5;
    for (const HelpBlock& block : m_helpBlocks) {
        rects.push_back(Popup::HelpBlockRect(m_popupRules, block.atUnits, block.boxUnits, corner));
    }
    return rects;
}

void MagicPortalsLayer::ScheduleDevTap(int tick, std::optional<glm::dvec2> viewFraction, int releaseTick) {
    DevTap tap;
    tap.tick = tick;
    tap.releaseTick = std::max(releaseTick, tick + 1);
    tap.onHelpBlock = !viewFraction.has_value();
    tap.viewFraction = viewFraction.value_or(glm::dvec2(0.0));
    m_devTaps.push_back(tap);
}

void MagicPortalsLayer::ScheduleDevDrag(int tick, const glm::dvec2& from, const glm::dvec2& to, int releaseTick) {
    DevTap tap;
    tap.tick = tick;
    tap.releaseTick = std::max(releaseTick, tick + 1);
    tap.viewFraction = from;
    tap.toFraction = to;
    m_devTaps.push_back(tap);
}

MagicPortalsLayer::Touch MagicPortalsLayer::touchThisTick(const entt::registry& registry) {
    using Supersonic::Input;
    Touch touch;
    const glm::dvec2 view = ViewPx();
    // DEV ONLY first: a scheduled tap is pressed on its tick and released on the
    // next, where it stood.
    for (auto it = m_devTaps.begin(); it != m_devTaps.end();) {
        if (it->tick > m_ticks) {
            ++it;
            continue;
        }
        glm::dvec2 at = it->viewFraction * view;
        if (it->toFraction && it->pressed) {
            // A drag, at an even pace from where it went down to where it is let go.
            const double span = static_cast<double>(std::max(it->releaseTick - it->pressedTick, 1));
            const double along = std::clamp(static_cast<double>(m_ticks - it->pressedTick) / span, 0.0, 1.0);
            at = (it->viewFraction + (*it->toFraction - it->viewFraction) * along) * view;
        }
        if (it->onHelpBlock) {
            // Only a block the view shows: a player cannot touch one off the
            // screen. A tap with none is dropped, so it does not hold the pointer
            // for the rest of the run.
            const std::vector<Hud::Rect> rects = HelpBlockRects();
            if (!rects.empty()) at = rects.front().Centre();
            if (rects.empty() || at.x < 0.0 || at.y < 0.0 || at.x > view.x || at.y > view.y) {
                SUPERSONIC_LOG_WARN("Magic Portals") << "DEV tap on tick " << m_ticks
                                                     << " dropped: no help block on the view" << std::endl;
                it = m_devTaps.erase(it);
                continue;
            }
        }
        touch.over = true;
        touch.atView = at;
        touch.atPx = at / view * glm::dvec2(1280.0, 720.0);
        if (!it->pressed) {
            it->pressed = true;
            it->viewFraction = at / view;
            it->onHelpBlock = false; // released where it went down
            touch.pressed = true;
            touch.held = true;
            // Its release is counted from the tick it went down on, which may be
            // later than the one it asked for; a drag keeps its length.
            const int late = std::max(m_ticks - it->tick, 0);
            it->releaseTick = std::max(it->releaseTick + (it->toFraction ? late : 0), m_ticks + 1);
            it->pressedTick = m_ticks;
            SUPERSONIC_LOG_INFO("Magic Portals") << "DEV tap down on tick " << m_ticks << " at (" << at.x << ", "
                                                 << at.y << ") units" << std::endl;
        } else if (m_ticks < it->releaseTick) {
            // Held where it went down, until its release tick.
            touch.held = true;
        } else {
            touch.released = true;
            SUPERSONIC_LOG_INFO("Magic Portals") << "DEV tap up on tick " << m_ticks << std::endl;
            m_devTaps.erase(it);
        }
        return touch;
    }
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport == nullptr || !viewport->pointerOverGame) return touch;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return touch;
    const glm::vec2 local = viewport->ToLocal(Input::MousePosition());
    touch.over = true;
    touch.atPx = glm::dvec2(local);
    touch.atView = glm::dvec2(local / size) * view;
    touch.pressed = Input::TickWasPressed(kTap);
    touch.released = Input::TickWasReleased(kTap);
    touch.held = Input::IsDown(kTap);
    return touch;
}

bool MagicPortalsLayer::helpBlockInput(entt::registry& registry) {
    // HelpBlockController::update (bytes 226317..228622): a touch down in a
    // block's rectangle disables the level's touches, so it fires no portal; the
    // move it makes is kept; its release, not having moved 12, opens the level's
    // popup. Not while game time is stopped, which the caller already is not.
    if (m_helpBlocks.empty()) {
        m_helpTouch = HelpTouch{};
        return false;
    }
    const Touch touch = touchThisTick(registry);
    const std::vector<Hud::Rect> rects = HelpBlockRects();
    bool taken = false;
    if (touch.pressed && touch.over) {
        for (std::size_t i = 0; i < rects.size(); ++i) {
            if (!rects[i].Contains(touch.atView)) continue;
            m_helpTouch = HelpTouch{true, static_cast<int>(i), touch.atPx, 0.0};
            taken = true;
            break;
        }
    }
    if (!m_helpTouch.armed) return taken;
    taken = true;
    if (touch.over) m_helpTouch.maxMovePx = std::max(m_helpTouch.maxMovePx, glm::length(touch.atPx - m_helpTouch.downPx));
    if (!touch.released && touch.held) return taken;
    // Released, or gone without a release this tick saw: either way it is over.
    const HelpTouch done = m_helpTouch;
    m_helpTouch = HelpTouch{};
    if (!touch.released || done.block < 0 || static_cast<std::size_t>(done.block) >= rects.size() ||
        !rects[static_cast<std::size_t>(done.block)].Contains(touch.atView) ||
        !(done.maxMovePx < m_popupRules.helpBlock.maxMovePx)) {
        return taken;
    }
    const Chapters::Level* level = Current();
    bool listed = false;
    const Popup::Class* popup = level != nullptr ? Popup::HelpBlockClass(m_popupRules, level->name, listed) : nullptr;
    if (popup == nullptr) {
        // 4-02's SpaceEasterEggHelpPopup, which the port does not build (spec U6).
        SUPERSONIC_LOG_INFO("Magic Portals") << "help block released on tick " << m_ticks << ": "
                                             << (listed ? "its popup is not built" : "no popup is listed") << std::endl;
        return taken;
    }
    openPopup(registry, *popup, true);
    return taken;
}

void MagicPortalsLayer::openPopup(entt::registry& registry, const Popup::Class& cls, bool stepOnResume) {
    if (m_popup.open || m_pause.open) return;
    // A popup nobody can see must not stop the level: without its card and its
    // close button - the original's assets absent - it is said once and not
    // raised, as a HUD control whose picture is missing is not built.
    const auto drawable = [this](const std::string& file) {
        const auto found = m_popupImages.find(file);
        return found != m_popupImages.end() && !found->second.empty();
    };
    if (!drawable(cls.card.sprite) || !drawable(m_popupRules.closeButton.sprite)) {
        SUPERSONIC_LOG_WARN("Magic Portals") << "popup " << cls.name << " not raised: its card or close button "
                                             << "could not be read" << std::endl;
        return;
    }
    m_popup = PopupScreen{};
    m_popup.open = true;
    m_popup.cls = &cls;
    m_popup.state = Popup::Start(cls);
    m_popup.stepOnResume = stepOnResume;
    m_helpTouch = HelpTouch{};
    // Popup::Popup pauses g_timeManager (instructions 147..166).
    freezeWorld(registry);
    layOutControls();
    SUPERSONIC_LOG_INFO("Magic Portals") << "popup " << cls.name << " opened on tick " << m_ticks << std::endl;
}

bool MagicPortalsLayer::ClosePopup() {
    if (!m_popup.open || Popup::Closing(m_popup.state)) return false;
    Popup::Close(m_popup.state);
    SUPERSONIC_LOG_INFO("Magic Portals") << "popup closed on tick " << m_ticks << std::endl;
    return true;
}

bool MagicPortalsLayer::popupTick(entt::registry& registry, float fixedDelta) {
    using Supersonic::Input;
    // The step the app just ran is undone before anything looks at the world.
    holdWorld(registry);
    const double dtMs = static_cast<double>(fixedDelta) * 1000.0;
    m_stoppedMs += dtMs;
    Popup::Tick(*m_popup.cls, m_popup.state, dtMs);
    m_aspect = viewportAspect(registry);
    layOutControls();

    // Popup::hasReceivedCloseCommand: a touch DOWN anywhere, the close button
    // included, or the back key. The touch is spent on the close and reaches
    // nothing under the popup.
    const Touch touch = touchThisTick(registry);
    if ((touch.pressed && touch.over) || Input::TickWasPressed(kBack)) ClosePopup();

    // Popup::update: once every sprite and the button are dismissed, the last layer
    // is current again and game time resumes - on this tick.
    if (!Popup::Gone(m_popupRules, m_popup.state)) return false;
    const bool step = m_popup.stepOnResume;
    thawWorld(registry);
    m_popup = PopupScreen{};
    layOutControls();
    SUPERSONIC_LOG_INFO("Magic Portals") << "popup gone, game time resumed on tick " << m_ticks << std::endl;
    return step;
}

void MagicPortalsLayer::tickPlaqueDismissal(bool updated) {
    // Game::loop -> dismissCurrentMedalSprite (bytes 115930..116151): once
    // getUiTime() > 2000 - a frame clock neither a pause nor a popup stops - the
    // plaque and its medal are dismissed. The fade that follows runs on GameLayer's
    // updates, so it starts from the age the level had when that happened, which
    // under a stop is the age the stop holds.
    if (m_plaqueDismissAgeMs < 0.0 && LevelFrameMs() > m_hudRules.plaque.dismissAfterMs) {
        m_plaqueDismissAgeMs =
            GameTimeStopped() ? m_levelAgeMs
                              : std::max(0.0, m_levelAgeMs - (LevelFrameMs() - m_hudRules.plaque.dismissAfterMs));
    }
    if (updated) m_plaqueAlpha = Hud::PlaqueAlphaFrom(m_hudRules, m_levelAgeMs, m_plaqueDismissAgeMs);
}

bool MagicPortalsLayer::devPressDue(DevPress press) {
    for (auto it = m_devPresses.begin(); it != m_devPresses.end(); ++it) {
        if (it->second != press || it->first > m_ticks) continue;
        SUPERSONIC_LOG_INFO("Magic Portals") << "DEV press " << static_cast<int>(press) << " taken on tick "
                                             << m_ticks << " (asked for " << it->first << ")" << std::endl;
        m_devPresses.erase(it);
        return true;
    }
    return false;
}

void MagicPortalsLayer::ScheduleDevPress(int tick, DevPress press) {
    m_devPresses.emplace_back(tick, press);
}

void MagicPortalsLayer::ScheduleDevHold(int from, int to, float direction) {
    m_devHolds.push_back(DevHold{from, to, direction});
}

// ---- how a level ends ------------------------------------------------------------

void MagicPortalsLayer::endHud() {
    // The pads decay from the byte they were last drawn with: the pulse stops
    // where it was (ScreenPad::draw, a <- uint(a * 0.98)). A weightless level
    // has none to decay.
    m_padEndByte = m_level.portals.noGravity ? 0 : Hud::PadAlphaByte(m_hudRules, m_levelAgeMs, tutorialPads());
    m_clearShownAtEnd = m_level.portals.budget > 0 && !m_level.portals.placed.empty();
}

std::vector<LevelEnd::Piece> MagicPortalsLayer::endPieces(const glm::dvec2& viewUnits, double ms) const {
    if (m_screen == Screen::Finished) {
        return LevelEnd::Finished(m_levelEndRules, m_end.play, m_end.portals.current, m_end.crystals.current,
                                  viewUnits, ms);
    }
    if (m_screen == Screen::Dead) return LevelEnd::Lost(m_levelEndRules, viewUnits, ms);
    return {};
}

bool MagicPortalsLayer::endScreenTick(entt::registry& registry, float fixedDelta) {
    using Supersonic::Input;
    // UI frame time, which this layer being current is what advances; and the
    // ScoreCounters, which count only while it is.
    const double dtMs = static_cast<double>(fixedDelta) * 1000.0;
    m_end.clockMs += dtMs;
    if (m_screen == Screen::Finished) {
        m_end.portals.Tick(dtMs, m_levelEndRules.finished.counterStrideMs);
        m_end.crystals.Tick(dtMs, m_levelEndRules.finished.crystalStrideMs);
    }
    // The window may have changed shape, and the camera moved with the level.
    m_aspect = viewportAspect(registry);
    layOutMenu();

    // A tap, tested against where each button is on this tick, in view space as
    // the controls are: Button::isPointInButton, its sprite's rectangle - a
    // button still sliding in is pressable where it is.
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport == nullptr || !viewport->pointerOverGame || !Input::TickWasPressed(kTap)) return false;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return false;
    const glm::dvec2 view = ViewPx();
    const glm::dvec2 at = glm::dvec2(viewport->ToLocal(Input::MousePosition()) / size) * view;
    const std::optional<LevelEnd::Button> pressed = LevelEnd::ButtonAt(endPieces(view, m_end.clockMs), at);
    if (!pressed) return false;
    MenuButton button;
    switch (*pressed) {
    case LevelEnd::Button::Restart:
        button.kind = MenuButton::Kind::Retry;
        break;
    case LevelEnd::Button::Next:
        button.kind = MenuButton::Kind::Next;
        break;
    case LevelEnd::Button::List:
        button.kind = MenuButton::Kind::List;
        break;
    }
    PressMenu(registry, button);
    return true;
}

bool MagicPortalsLayer::pauseTick(entt::registry& registry, float fixedDelta) {
    using Supersonic::Input;
    // The step the app just ran is undone before anything looks at the world.
    holdWorld(registry);
    // UI frame time runs; game time does not, and the level's frame clock counts
    // the difference.
    const double dtMs = static_cast<double>(fixedDelta) * 1000.0;
    m_pause.clockMs += dtMs;
    m_stoppedMs += dtMs;
    // The window may change shape under a pause; restart and pause, drawn frozen
    // under it, stay on their corner.
    m_aspect = viewportAspect(registry);
    layOutControls();

    // The back key resumes: handleBackButton hides the pause when GameMenuLayer
    // is current.
    std::optional<Pause::Button> pressed;
    if (Input::TickWasPressed(kBack)) pressed = Pause::Button::Resume;
    const struct {
        DevPress dev;
        Pause::Button button;
    } devs[] = {{DevPress::Resume, Pause::Button::Resume},
                {DevPress::Levels, Pause::Button::Levels},
                {DevPress::Skip, Pause::Button::Skip},
                {DevPress::Achievements, Pause::Button::Achievements},
                {DevPress::Sound, Pause::Button::Sound},
                {DevPress::Music, Pause::Button::Music}};
    for (const auto& dev : devs) {
        if (!pressed && devPressDue(dev.dev)) pressed = dev.button;
    }

    // A tap, tested against where each button is on this tick, in view space as
    // the controls are: Button::isPointInButton, its sprite's rectangle.
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (!pressed && viewport != nullptr && viewport->pointerOverGame && Input::TickWasPressed(kTap)) {
        const glm::vec2 size = viewport->Size();
        if (size.x > 0.0f && size.y > 0.0f) {
            const glm::dvec2 view = ViewPx();
            const glm::dvec2 at = glm::dvec2(viewport->ToLocal(Input::MousePosition()) / size) * view;
            pressed = Pause::ButtonAt(m_pauseRules, m_pause.level, PauseSwitches(), view, m_pause.clockMs, at);
        }
    }
    if (!pressed) return false;
    const bool resuming = *pressed == Pause::Button::Resume;
    PressPause(registry, *pressed);
    return resuming && !m_pause.open;
}

bool MagicPortalsLayer::PressPause(entt::registry& registry, Pause::Button button) {
    if (!m_pause.open) return false;
    switch (button) {
    case Pause::Button::Resume:
        // GameState::hideMenuPopup: the layer hidden, GameLayer current, game time
        // resumed. Its button makes the menu's own noise.
        latch("menu_button");
        closePause(registry);
        return true;
    case Pause::Button::Levels:
        // returnToLevelSelect: createLevelSelectState, which is the grid this
        // level's world is on - the medal screen's list button does exactly that.
        closePause(registry);
        return PressMenu(registry, MenuButton{MenuButton::Kind::List});
    case Pause::Button::Skip:
        // goToNextLevel(levelIndex), and only on a level already finished: the
        // button is not there otherwise.
        if (m_pause.level.savedMedal <= 0) return false;
        closePause(registry);
        return PressMenu(registry, MenuButton{MenuButton::Kind::Next});
    case Pause::Button::Achievements:
        // OWNER RULING: kept, and drawn - but the AchievementsPopup it opens is
        // not built (spec section 7, U10), so pressing it changes nothing but
        // the noise a button makes.
        latch("menu_button");
        return false;
    case Pause::Button::Sound: {
        // GlobalSoundSwitch::manageSoundSwitch: SetGlobalVolume 1 or 0. And
        // SoundPanelLayer::manageMusicSwitch dismisses the music switch while the
        // sound is off, and adds a fresh one - a fresh entrance - once it is on
        // and the old one is gone.
        const double now = m_pause.clockMs;
        const double dismissMs = m_pauseRules.layer.buttonDismissMs;
        const bool dismissing = m_pause.musicDismissedMs >= 0.0 && now - m_pause.musicDismissedMs < dismissMs;
        m_soundOn = !m_soundOn;
        if (!m_soundOn) {
            if (!dismissing && now >= m_pause.musicAddedMs) m_pause.musicDismissedMs = now;
        } else {
            m_pause.musicAddedMs = dismissing ? m_pause.musicDismissedMs + dismissMs : now;
        }
        latch("menu_button");
        return true;
    }
    case Pause::Button::Music: {
        const Pause::Switches switches = PauseSwitches();
        const bool shown = switches.soundOn && m_pause.clockMs >= switches.musicAddedMs &&
                           !(switches.musicDismissedMs >= 0.0 &&
                             m_pause.clockMs - switches.musicDismissedMs < m_pauseRules.layer.buttonDismissMs);
        if (!shown) return false;
        m_musicOn = !m_musicOn;
        latch("menu_button");
        return true;
    }
    }
    return false;
}

// ---- per frame: nothing the level or a replay depends on --------------------

void MagicPortalsLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    // The entities' particles. They belong to the FRAME and not to the tick,
    // because a particle is a picture: nothing below may reach Game::Level,
    // the simulation's clock or the state hash. Carried before the camera's
    // early return, so a level drawn without one does not freeze them.
    if (!GameTimeStopped()) updateEmitters(registry, deltaTime);
    // And the lights and halos those particles brighten and dim.
    syncLights(registry);
    // And the sounds the tick latched, played here for the same reason: a
    // sound is a picture with a speaker. Both are before the camera's early
    // return, so a level drawn without one is not also silent.
    playLatched(registry, deltaTime);
    updateMusic(registry);
    // And what this frame believes it is drawing. Before the camera's early
    // return, like the rest of it.
    reportSprites(registry);
    // And the HUD, once a FRAME: the screen overlay is drawn and cleared every
    // frame, so a HUD emitted on the tick would vanish from a frame with no tick
    // and double on one with two. It reads the last tick's state and changes
    // nothing.
    EmitHud(registry);
    // And the menu states', which never share a frame with the HUD.
    EmitMenu(registry);
    // Only the camera's SHAPE, for the viewport this frame is drawn into.
    if (m_camera == entt::null || !registry.valid(m_camera)) return;
    registry.get<Supersonic::CameraComponent>(m_camera).aspect = viewportAspect(registry);
}

} // namespace MagicPortals
