// Magic Portals as the port plays it. MagicPortalsLayer.hpp says what it is and
// what it is not.

#include "MagicPortalsLayer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Raycast.hpp"
// For RenderSystem::Stats, which the app publishes into the registry context:
// what the last frame actually drew, culled and refused.
#include "core/RenderSystem.hpp"
#include "core/ScreenOverlay.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Roles.hpp"
#include "sim/Units.hpp"

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

    bindInput();
    loadSounds();

    // The medals earned before this run, if this build was told where they are
    // kept. An empty saveDir is the ordinary case for a test and means this
    // never opens a file; a file that exists and will not parse is reported and
    // then left alone rather than overwritten.
    if (std::string why; !m_scores.Open(m_paths.saveDir, why)) {
        SUPERSONIC_LOG_WARN("Magic Portals") << "medals not loaded: " << why << std::endl;
    }

    std::string error;
    const bool read = Chapters::Load(m_paths.chapters, m_chapters, error) &&
                      Camera::LoadRules(m_paths.data + "/portals.json", m_cameraRules, error) &&
                      Camera::LoadViewHeight(m_paths.portData + "/view.json", m_viewHeightPx, error) &&
                      Art::LoadRules(m_paths.portData + "/art.json", m_artRules, error) &&
                      Hud::LoadRules(m_paths.portData + "/ui.json", m_hudRules, error);
    // ui.json is the port's own and committed, so a HUD that will not read is a
    // fault in this repository and stops the start as loudly as a missing level.
    m_hudReady = read;
    if (!read) {
        m_loadError = error;
        m_hudError = error;
    } else if (m_startLevel.empty()) {
        // No level named: the menu, which is what the game itself opens with.
        // A named level is entered directly, so --level and every suite reach
        // the game exactly as they did before the menu existed.
        buildCamera(registry);
        openMenu(registry, Screen::Main);
    } else if (const int start = m_chapters.Find(m_startLevel); start < 0) {
        m_loadError = m_startLevel + " is not a level of " + m_paths.chapters;
    } else {
        buildCamera(registry);
        loadLevel(registry, start);
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
    unloadLevel(registry);
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
    // And the level's age, which everything it opens with is timed by. A retry
    // is a new level to the original - its restart button builds a fresh Game
    // state, whose preLoop starts the black, "Part N" and the plaque again
    // (GameLayer::update, bytes 100358..100663) - so it is zero here too.
    m_levelAgeMs = 0.0;
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
        if (!Game::LoadData(m_paths.levels + "/" + entry.name + ".tscn", m_paths.data, m_paths.prisms, m_data,
                            error, m_paths.portData)) {
            return refuse(error);
        }
        m_dataIndex = index;
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
    return true;
}

void MagicPortalsLayer::unloadLevel(entt::registry& registry) {
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
    // The particles the level's entities were emitting go with them; a retry
    // would otherwise pile a second pool on the first.
    unloadEmitters(registry);
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

// Defined below, beside MedalShown, which computes the same thing from the
// counter's current value. Declared here because clearLevel records the FINAL
// medal and comes first in this file.
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
    // MedalShown() climbs bronze to gold as the counter rises, which is a
    // picture; this is the result. The original writes it at this same moment
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
// bytes 363979..364204): 3 is gold, 2 silver, anything else bronze.
//
//   - a level with crystals, none of them collected, is bronze whatever else;
//   - within the golden score: gold with every crystal, silver without;
//   - within the golden score and two more: silver;
//   - beyond that: bronze.
int MedalFor(const MagicPortalsLayer::Cleared& cleared) {
    if (cleared.crystalsTotal > 0 && cleared.crystals == 0) return 1;
    if (cleared.portalsUsed <= cleared.goldenScore) {
        return cleared.crystals < cleared.crystalsTotal ? 2 : 3;
    }
    if (cleared.portalsUsed <= cleared.goldenScore + 2) return 2;
    return 1;
}

int MagicPortalsLayer::MedalShown() const {
    if (!m_lastCleared) return 0;

    // The SAME computeScore the final medal uses, with the counter's current
    // value standing in for the portals spent. One implementation rather than a
    // second that agrees with it today: the original's draw does exactly this -
    // computeScore(golden, numCrystals, maxCrystals, counter.getCurrent()) - so
    // the medal it shows is a function of the number on screen, not of the
    // number the play ended on.
    Cleared asCounted = *m_lastCleared;
    asCounted.portalsUsed = m_counterShown;
    return MedalFor(asCounted);
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

    for (const DrawnSprite& drawn : m_sprites) {
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
        for (const Particles::System& system : systems) {
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
            emitter.particles.resize(static_cast<std::size_t>(std::min(system.count, kMaxParticles)));
            m_emitters.push_back(std::move(emitter));
        }
    }
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
            particle.angle = system.angleStart + particleRandom(0.0, system.randAngleStart);
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
                if (system.repeat > 0 && particle.repeats >= system.repeat) {
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

    if (m_screen == Screen::Main) {
        MenuButton play;
        play.kind = MenuButton::Kind::Play;
        play.centrePx = at(0.5, 0.66);
        play.sizePx = sized("main_play_game_button.png", 0.16);
        m_menuButtons.push_back(play);
        return;
    }

    if (m_screen == Screen::Finished) {
        // Over the level, so placed against what the CAMERA shows rather than
        // the menu's own box: the level's own pixels are what a click maps to.
        const glm::dvec2 view = ViewPx();
        const glm::dvec2 centre = m_follow.centrePx;
        const auto onView = [&view, &centre](double nx, double ny) {
            return centre + glm::dvec2((nx - 0.5) * view.x, (ny - 0.5) * view.y);
        };
        // ONE COLUMN DOWN THE RIGHT, at x 0.75 and a quarter, a half and three
        // quarters down.
        //
        // This briefly became a row across the bottom, and that was my error.
        // LevelFinishedLayer's constructor calls addButton with vector2(0.25,
        // 0.75), (0.5, 0.75) and (0.75, 0.75), and I read those pairs as (x, y).
        // They are not: AngelScript pushes a call's arguments so that the LAST
        // pushed is the FIRST parameter, so `PshC4 A; PshC4 B; vector2()` builds
        // vector2(B, A) and the constant that varies here is the Y.
        //
        // What settles it is the veil in the same constructor (bytes 271777..,
        // instruction 253 onward): it pushes screenSize.y and then
        // screenSize.x * 1.5. A full-screen dimming veil is one and a half
        // screens WIDE; there is no reading in which it is screenSize.y wide and
        // one and a half screen-widths tall. Same rule, three corroborations:
        // the "level finished" banner lands top-centre, the level-select arrows
        // land on the left and right edges, and the column this port already had
        // by eye - x 0.82 - was very nearly this one.
        const MenuButton::Kind kinds[] = {MenuButton::Kind::Retry, MenuButton::Kind::Next,
                                          MenuButton::Kind::List};
        const double ys[] = {0.25, 0.50, 0.75};
        for (int i = 0; i < 3; ++i) {
            MenuButton button;
            button.kind = kinds[i];
            button.centrePx = onView(0.75, ys[i]);
            button.sizePx = glm::dvec2(view.y * 0.16);
            m_menuButtons.push_back(button);
        }
        return;
    }

    if (m_screen == Screen::Dead) {
        // A ROW, and this one genuinely is. LevelLostLayer's constructor (bytes
        // 276429..277344) calls addButton twice, building its vector2 from
        // `PshC4 0.6f; PshC4 0.4f` and then `PshC4 0.6f; PshC4 0.6f`. Last
        // pushed is the FIRST argument, so they are (0.4, 0.6) and (0.6, 0.6):
        // the constant that VARIES is the x, which is what makes this a row
        // where the medal screen's three - varying in y - are a column. Same
        // rule, opposite answer, which is why it is worth writing down twice.
        const glm::dvec2 view = ViewPx();
        const glm::dvec2 centre = m_follow.centrePx;
        const auto onView = [&view, &centre](double nx, double ny) {
            return centre + glm::dvec2((nx - 0.5) * view.x, (ny - 0.5) * view.y);
        };
        // Its two buttons ARE the medal screen's restart and list - the same
        // two files, button_restart.png and list_button.png, doing the same two
        // jobs - so they take the same kinds and PressMenu needs nothing new.
        const MenuButton::Kind kinds[] = {MenuButton::Kind::Retry, MenuButton::Kind::List};
        const double xs[] = {0.4, 0.6};
        for (int i = 0; i < 2; ++i) {
            MenuButton button;
            button.kind = kinds[i];
            button.centrePx = onView(xs[i], 0.6);
            button.sizePx = glm::dvec2(view.y * 0.16);
            m_menuButtons.push_back(button);
        }
        return;
    }

    if (m_screen == Screen::Worlds) {
        // The original pages four worlds two at a time (PageProperties:
        // numItems 4, columns 2, rows 1). A window is not a phone, so the port
        // shows all four at once rather than carrying a swipe for one page.
        int worlds = 0;
        for (const Chapters::Level& level : m_chapters.levels) worlds = std::max(worlds, level.world + 1);
        for (int w = 0; w < worlds; ++w) {
            MenuButton icon;
            icon.kind = MenuButton::Kind::World;
            icon.world = w;
            const double span = 0.66;
            const double x = worlds > 1
                                 ? 0.5 - span * 0.5 + span * (static_cast<double>(w) / static_cast<double>(worlds - 1))
                                 : 0.5;
            icon.centrePx = at(x, 0.55);
            icon.sizePx = sized(("world_icon" + std::to_string(w) + ".png").c_str(), 0.34);
            m_menuButtons.push_back(icon);
        }
        return;
    }

    // The grid, as the original builds it for levels: createLevelSelectState
    // (WorldSelector.angelscript, bytes 365279..365960) sets columns 4 and rows
    // 4, so SIXTEEN to a page. PageProperties' own defaults are 4 by 3, which
    // is what the port first shipped and what the owner's screenshot of the
    // original disproved - the level selector overrides them.
    constexpr int kColumns = 4;
    constexpr int kRows = 4;
    constexpr int kPerPage = kColumns * kRows;
    std::vector<int> entries;
    for (std::size_t i = 0; i < m_chapters.levels.size(); ++i) {
        if (m_chapters.levels[i].world == m_menuWorld) entries.push_back(static_cast<int>(i));
    }
    const int pages = std::max(1, (static_cast<int>(entries.size()) + kPerPage - 1) / kPerPage);
    m_menuPage = std::clamp(m_menuPage, 0, pages - 1);

    // A CENTRED BLOCK OF TOUCHING TILES, which is not what this first shipped.
    //
    // The original does not spread its buttons over a box at all. Page::Page
    // (PageManager.angelscript) walks a cursor: it starts at getScreenOffset(),
    // places a button, steps the cursor by getButtonSize().x, and wraps to the
    // offset with cursor.y += getButtonSize().y when the next one would pass
    // the screen's far edge. getButtonSize is GetSpriteSize(button) * g_scale -
    // the tile's OWN size, with no gap - and getScreenOffset is
    // ((screen - vector2(columns * buttonSize.x, rows * buttonSize.y)) / 2), so
    // the block is simply centred. `columns` and `rows` are a COUNT there
    // (numButtons = rows * columns), not a geometry.
    //
    // Spread over a 0.24..0.76 by 0.17..0.83 box at box.y * 0.14 each, as this
    // did, the tiles float apart and the grid sits squat in the middle of the
    // screen. That is the difference the owner saw against their capture.
    //
    // THE ONE NUMBER HERE IS MEASURED, NOT DECODED, and it says so. The
    // original's absolute tile size is GetSpriteSize * (GetScreenSize().y / 480)
    // - SGlobalScale's m_absoluteSize is 480 - and it picks an `hd` sprite at
    // twice the density above that height, so the size on screen depends on the
    // device and on which asset was loaded. Neither transfers to this port's
    // fixed 256 px menu box. What does transfer is the PROPORTION, and in the
    // owner's capture of the original the sixteen tiles are square at very near
    // 0.217 of the screen's height, making the block 4 * 0.217 = 0.867 of the
    // height and 54% of a 16:9 width. That is the fraction below, flagged the
    // way art.json flags a _guess.
    constexpr double kTileOfHeight = 0.217; // measured from the owner's capture
    const double tile = box.y * kTileOfHeight;
    const glm::dvec2 blockPx(tile * kColumns, tile * kRows);
    const glm::dvec2 originPx = (box - blockPx) * 0.5;
    for (int slot = 0; slot < kPerPage; ++slot) {
        const int index = m_menuPage * kPerPage + slot;
        if (index >= static_cast<int>(entries.size())) break;
        const int column = slot % kColumns;
        const int row = slot / kColumns;
        MenuButton button;
        button.kind = MenuButton::Kind::Level;
        button.world = m_menuWorld;
        button.level = entries[static_cast<std::size_t>(index)];
        // The cursor's step is the tile itself, and centrePx is the middle of
        // the tile the cursor's top-left corner opens.
        button.centrePx = originPx + glm::dvec2(tile * column, tile * row) + glm::dvec2(tile * 0.5);
        button.sizePx = glm::dvec2(tile);
        m_menuButtons.push_back(button);
    }
    if (pages > 1) {
        // Either side of the grid, level with its middle - which is where the
        // owner's screenshot of the original shows them. The decoded
        // normalized pair is ambiguous about which number is x, and a picture
        // of the game settles it better than a guess at the argument order.
        MenuButton back;
        back.kind = MenuButton::Kind::Back;
        back.centrePx = at(0.08, 0.5);
        back.sizePx = glm::dvec2(box.y * 0.12);
        m_menuButtons.push_back(back);
        MenuButton forward;
        forward.kind = MenuButton::Kind::Forward;
        forward.centrePx = at(0.92, 0.5);
        forward.sizePx = glm::dvec2(box.y * 0.12);
        m_menuButtons.push_back(forward);
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

    // The medal screen keeps the level behind it, so it takes no background of
    // its own; the others cover the screen with theirs.
    if (m_screen != Screen::Finished && m_screen != Screen::Dead) {
        m_menuBg = quadFor("Magic Portals Menu Background",
                           menuImage(m_screen == Screen::Main ? "main_menu_bg.png" : "world_select_bg.png"));
    }
    if (m_screen == Screen::Main) {
        m_menuTitle = quadFor("Magic Portals Title", menuImage("game_main_title.png"));
    }
    // THE TWO WAYS A SCREEN'S FURNITURE IS SIZED, out here because BOTH the
    // medal screen and the lost screen lay theirs down this way.
    //
    // Every position either one uses is the original's own, normalized on the
    // screen: it builds them against GetScreenSize, and both screens sit over
    // the level, so the port places them on the CAMERA'S VIEW rather than in
    // the menu's box. menuTick does the placing; this only makes them. Sized by
    // its own aspect at a stated height, which is what everything but a veil
    // wants. The heights are DERIVED, not decoded: the original draws these at
    // 1.5 x g_scale against its own reference height, and no fraction of the
    // screen is written down anywhere - so each is its image's height against
    // that reference, and marked here as derived for the same reason art.json
    // marks its numbers _guess.
    const auto byHeight = [this, &quadFor](const char* tag, const std::string& file, glm::dvec2 atView,
                                          double heightView, float z, glm::dvec2 offsetPx = glm::dvec2(0.0),
                                          glm::dvec2 pivot = glm::dvec2(0.5)) {
            Decoration decoration;
            decoration.image = menuImage(file);
            decoration.quad = quadFor(tag, decoration.image);
            decoration.atView = atView;
            decoration.sizing = Decoration::Sizing::ByHeight;
            decoration.heightView = heightView;
            decoration.offsetPx = offsetPx;
            decoration.pivot = pivot;
        decoration.z = z;
        if (decoration.quad != entt::null) m_menuDecor.push_back(decoration);
    };

    // And the one that is stretched, because it is a gradient rather than a
    // picture of anything.
    //
    // ALPHA IS THE CALLER'S, because the two screens do not share it: the medal
    // screen's veil is ARGB(200,255,255,255) and the lost screen's is 180. The
    // port drew the first opaque white once, so a gradient meant to sink the
    // level behind the medal was barely a tint; hardcoding 200 here would have
    // made the same mistake again on the second screen. The particles take
    // their colour the same way (albedoColor, above), so this is the
    // established path rather than a new one.
    const auto stretched = [this, &registry, &quadFor](const char* tag, const std::string& file,
                                                       glm::dvec2 atView, glm::dvec2 sizeView, float z,
                                                       double alpha) {
        Decoration decoration;
        decoration.image = menuImage(file);
        decoration.quad = quadFor(tag, decoration.image);
        decoration.atView = atView;
        decoration.sizing = Decoration::Sizing::Stretched;
        decoration.sizeView = sizeView;
        decoration.z = z;
        if (decoration.quad == entt::null) return;

        registry.get<Supersonic::MaterialComponent>(decoration.quad).albedoColor =
            glm::vec4(1.0f, 1.0f, 1.0f, static_cast<float>(alpha / 255.0));
        m_menuDecor.push_back(decoration);
    };

    if (m_screen == Screen::Finished && m_lastCleared) {

        // EVERY POSITION BELOW READS THE DECODED PAIRS AS (y, x).
        //
        // AngelScript pushes a call's arguments last-first, so the bytecode's
        // `PshC4 A; PshC4 B; vector2()` is vector2(B, A). The veil in this same
        // constructor proves it: it pushes screenSize.y then screenSize.x * 1.5,
        // and a dimming veil is one and a half screens WIDE, not that many
        // screen-widths tall. Read the other way round, every plaque on this
        // screen is transposed - which is how the banner came to sit out at the
        // left instead of over the middle.
        //
        // The veil, first and furthest back. fade_edge.png is a 200-odd byte
        // HORIZONTAL GRADIENT, not a flat panel, and the original stretches it
        // one and a half screens wide at ARGB(200,255,255,255) from a top-left
        // origin - so the light end falls off the right and what is seen is the
        // dark-to-middle part of it. Sized explicitly for that reason: sizing
        // this one from its own aspect, which is the rule the chapter icons
        // needed, would draw a hairline. A top-left origin at (0,0) one and a
        // half screens wide IS a centre of (0.75, 0.5), so this one position
        // needed no correcting.
        stretched("Magic Portals Finish Veil", "fade_edge.png", glm::dvec2(0.75, 0.5),
                  glm::dvec2(1.5, 1.0), 0.55f, 200.0);

        // "Level finished" over the middle, and the plaque naming the portals
        // spent BELOW the medal - addSprite puts it at medalPos + (0, 0.15) of
        // the screen, not out to its right.
        byHeight("Magic Portals Finish Banner", "level_finished.png", glm::dvec2(0.464, 0.278), 0.16,
                 0.58f);
        byHeight("Magic Portals Portals Plaque", "portals_created_plaque.png",
                 glm::dvec2(0.47, 0.55 + 0.15), 0.12, 0.58f);

        // The golden-score plaque only where the play earned one. The original
        // guards it on the score qualifying - `if (score >= 3)`, so gold alone -
        // and a plaque claiming a medal nobody won would be worse than no
        // plaque. Its origin is (0.5, 0.33) rather than centred, which is the
        // one pivot on this screen that is not the default.
        if (MedalFor(*m_lastCleared) >= 3) {
            byHeight("Magic Portals Golden Plaque", "golden_score_plaque.png", glm::dvec2(0.23, 0.5),
                     0.12, 0.58f, glm::dvec2(0.0), glm::dvec2(0.5, 0.33));
        }

        // And a crystal by the medal, for a level that had any. Its place is the
        // medal's plus (-30, 48) of the original's own pixels - left and down -
        // which is why it rides an offset rather than a fraction of the screen:
        // a fraction would be a different place at a different window shape.
        //
        // Drawn from its TOP-LEFT, which is the pivot of (0, 0): this one is a
        // drawScaledSprite with an explicit V2_ZERO origin, unlike the banner
        // and the plaques above it, which addSprite centres on V2_HALF. Centred
        // like them it sat half a crystal up and to the left of where the
        // original puts it.
        if (m_lastCleared->crystalsTotal > 0) {
            byHeight("Magic Portals Finish Crystal", "crystal.png", glm::dvec2(0.47, 0.55), 0.08, 0.60f,
                     glm::dvec2(-30.0, 48.0), glm::dvec2(0.0, 0.0));
        }

        // The medal itself, in the title's slot: the two screens are never up
        // together. Its image follows the COUNTER rather than the final score,
        // so it climbs as the number rises - menuTick swaps the texture when
        // the tier changes, and m_medalDrawn remembers which one is on.
        //
        // The counter is NOT reset here. buildMenu runs again on every window
        // resize, so resetting here would restart the count - and drop the
        // medal back to bronze - because somebody dragged the window edge.
        // openFinished owns that, because it runs once when the level is
        // cleared. Same mistake the decoration leak was: state set up in a
        // function that is not once per screen.
        m_medalDrawn = MedalShown();
        const char* file = m_medalDrawn == 3   ? "medal_gold_l.png"
                           : m_medalDrawn == 2 ? "medal_silver_l.png"
                                               : "medal_bronze_l.png";
        m_menuTitle = quadFor("Magic Portals Medal", menuImage(file));
    }

    if (m_screen == Screen::Dead) {
        // THE LOST SCREEN, as LevelLostLayer builds it (bytes 276429..277344).
        //
        // ITS VEIL IS NOT THE MEDAL SCREEN'S, which is the trap here. That one
        // is pulled one and a half screens wide at alpha 200; this one is
        // addSprite'd at V2_ZERO with size (screenSize.x * 0.9, screenSize.y)
        // at ARGB(180,255,255,255) - narrower, taller and thinner. A top-left
        // origin 0.9 of a screen wide and a full screen tall IS a centre of
        // (0.45, 0.5), which is the same origin-to-centre conversion the finish
        // veil's own note records; read as a centre it would sit off to the
        // right by a twentieth of the screen.
        stretched("Magic Portals Lost Veil", "fade_edge.png", glm::dvec2(0.45, 0.5), glm::dvec2(0.9, 1.0),
                  0.55f, 180.0);

        // And the banner over the middle, at screenSize * (0.5, 0.35): the pair
        // is pushed `PshC4 0.35f; PshC4 0.5f`, so last-pushed-first makes x the
        // 0.5. addSprite centres it on V2_HALF, this port's default pivot.
        byHeight("Magic Portals Game Over", "game_over.png", glm::dvec2(0.5, 0.35), 0.22, 0.58f);
    }

    for (const MenuButton& button : m_menuButtons) {
        std::string image;
        switch (button.kind) {
        case MenuButton::Kind::Play:
            image = menuImage("main_play_game_button.png");
            break;
        case MenuButton::Kind::World:
            image = menuImage("world_icon" + std::to_string(button.world) + ".png");
            break;
        case MenuButton::Kind::Level: {
            // The last level of a world is its boss, and the original gives it
            // its own button.
            const Chapters::Level& level = m_chapters.levels[static_cast<std::size_t>(button.level)];
            const bool boss = level.index == 31;
            image = menuImage(boss ? "boss_level_button.png" : "level_button.png");
            break;
        }
        // THE TWO ARROW FILES ARE NAMED THE OTHER WAY ROUND FROM THEIR ART, and
        // this port drew them by their names until the owner saw it.
        //
        // level_select_back.png is a sprite of a RIGHT-pointing triangle, and
        // level_select_forward.png a LEFT-pointing one. The original's own
        // createLevelSelectState hands `back` to PageProperties.backButton and
        // `forward` to forwardButton, and PageProperties puts backButton at
        // normalized (0, 0.5) and forwardButton at (1, 0.5) - so on screen the
        // LEFT edge draws the file called "back", whose picture points RIGHT.
        //
        // In the original that still looks correct, because the pictures were
        // authored for those slots and the names are simply misleading. Reading
        // the names as the direction, as this did, put a right-pointing arrow on
        // the left and a left-pointing one on the right: both aimed inward, which
        // is what the owner reported. The owner's capture of the original settles
        // it - a LEFT arrow on the left, a RIGHT arrow on the right - so each
        // side takes the file whose picture points the way that side goes.
        case MenuButton::Kind::Back:
            image = menuImage("level_select_forward.png");
            break;
        case MenuButton::Kind::Forward:
            image = menuImage("level_select_back.png");
            break;
        // The medal screen's three, which LevelFinishedLayer names.
        case MenuButton::Kind::Retry:
            image = menuImage("button_restart.png");
            break;
        case MenuButton::Kind::Next:
            image = menuImage("button_right.png");
            break;
        case MenuButton::Kind::List:
            image = menuImage("list_button.png");
            break;
        }
        m_menuQuads.push_back(quadFor("Magic Portals Menu Button", image));

        entt::entity label = entt::null;
        if (button.kind == MenuButton::Kind::Level) {
            const Chapters::Level& level = m_chapters.levels[static_cast<std::size_t>(button.level)];
            label = registry.create();
            registry.emplace<TagComponent>(label, "Magic Portals Menu Label");
            registry.emplace<TransformComponent>(label);
            auto& text = registry.emplace<UITextComponent>(label);
            text.worldSpace = true; // it follows the button it numbers
            // PageProperties' numberOffset is vector2(0, 0) and its fontScale is
            // 1, so the number sits on the button's own point rather than nudged
            // below it. The size rides THE BUTTON rather than being a fixed 26 px:
            // the tiles are now sized from the window, and a fixed number would
            // swim about on them as the window changed shape. Taken from the
            // button and not from the box, because that is what it sits on - and
            // because layOutMenu's tile fraction is layOutMenu's own business.
            text.fontSize = static_cast<float>(button.sizePx.y * 0.55);
            text.offset = glm::vec2(0.0f, 0.0f);
            text.text = std::to_string(level.index + 1);
        }
        m_menuLabels.push_back(label);

        // THE MEDAL THIS LEVEL WAS CLEARED WITH, where it has been.
        //
        // LevelChooser::itemDrawCallback asks ScoreManager::getScore for the
        // level and draws getSmallSpriteMedalName(score) only when that is not
        // zero. The port kept no score, so this drew nothing at all and every
        // button was bare - which is what the owner reported. `_m` is the
        // original's own middle size, as against the `_l` the medal screen uses.
        entt::entity medal = entt::null;
        if (button.kind == MenuButton::Kind::Level) {
            const Chapters::Level& cleared = m_chapters.levels[static_cast<std::size_t>(button.level)];
            const int tier = m_scores.Get(cleared.world, cleared.index);
            if (tier != Scores::kUnplayed) {
                // Bronze for anything unrecognised, which is what
                // getSmallSpriteMedalName does with a score it does not know.
                const char* file = tier == Scores::kGold     ? "medal_gold_m.png"
                                   : tier == Scores::kSilver ? "medal_silver_m.png"
                                                             : "medal_bronze_m.png";
                medal = quadFor("Magic Portals Menu Medal", menuImage(file));
            }
        }
        m_menuMedals.push_back(medal);
    }
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
    // The medal screen's furniture goes with the rest of it. Without this every
    // rebuild leaves its quads behind - and a rebuild is not rare: layOutMenu
    // and buildMenu run again on every window resize, so the veils would stack
    // one atop another and darken a shade at a time.
    for (Decoration& decoration : m_menuDecor) destroy(decoration.quad);
    m_menuDecor.clear();
    destroy(m_menuBg);
    destroy(m_menuTitle);
}

void MagicPortalsLayer::unloadMenu(entt::registry& registry) {
    unloadMenuDrawables(registry);
    m_menuButtons.clear();
}

void MagicPortalsLayer::openFinished(entt::registry& registry) {
    latch("medal_shown");
    // The count starts from nothing, HERE, because this runs once when the
    // level is cleared. buildMenu runs again on every window resize, so a reset
    // there would restart the count - and drop the medal back to bronze -
    // because somebody dragged the window edge. Without a reset anywhere, the
    // next level cleared would start counting from the last one's total and
    // show the wrong medal from its first frame.
    m_counterShown = 0;
    m_counterClockMs = 0.0;
    // The level STAYS: it is drawn behind the medal, and stops ticking because
    // OnFixedUpdate hands the tick to the menu whenever a screen is up.
    m_screen = Screen::Finished;
    // The level stays behind the medal, but its controls do not; and nothing
    // of the HUD is emitted while a screen is up (EmitHud), or the pads and
    // "Part N" of a quick finish would sit over the medal.
    hideHud();
    m_aspect = viewportAspect(registry);
    layOutMenu();
    buildMenu(registry);
}

void MagicPortalsLayer::openDead(entt::registry& registry) {
    // The SCREEN'S cue, and not the moment of death: checkGameEnd plays
    // playDeathSound here, where it raises levelLostLayer. A fall's own sound
    // played 1400 ms ago and an hp death made no sound at all.
    latch("player_died");
    // The level STAYS, as it does behind the medal: OnFixedUpdate hands the
    // tick to the menu whenever a screen is up, so it is drawn and frozen.
    m_screen = Screen::Dead;
    hideHud();
    m_aspect = viewportAspect(registry);
    layOutMenu();
    buildMenu(registry);
}

void MagicPortalsLayer::openMenu(entt::registry& registry, Screen screen) {
    // A level and a menu are never both in the registry.
    unloadLevel(registry);
    m_loaded = false;
    m_current = -1;
    m_chapterComplete = false;
    m_loadError.clear();
    m_screen = screen;
    m_aspect = viewportAspect(registry);
    layOutMenu();
    buildMenu(registry);
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
        m_menuWorld = button.world;
        m_menuPage = 0;
        openMenu(registry, Screen::Levels);
        return true;
    case MenuButton::Kind::Level:
        if (button.level < 0 || button.level >= static_cast<int>(m_chapters.levels.size())) return false;
        unloadMenu(registry);
        m_screen = Screen::None;
        loadLevel(registry, button.level);
        return true;
    case MenuButton::Kind::Back:
        if (m_menuPage <= 0) return false;
        --m_menuPage;
        openMenu(registry, Screen::Levels);
        return true;
    case MenuButton::Kind::Forward:
        ++m_menuPage; // layOutMenu clamps it to the last page
        openMenu(registry, Screen::Levels);
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

void MagicPortalsLayer::menuTick(entt::registry& registry) {
    using namespace Supersonic;
    m_aspect = viewportAspect(registry);
    layOutMenu(); // the window may have changed shape since the last tick
    const glm::dvec2 box = MenuBoxPx();

    // The medal screen leaves the camera where the level left it, so the level
    // stays framed as it was when it was finished.
    if (m_screen != Screen::Finished && m_screen != Screen::Dead && m_camera != entt::null &&
        registry.valid(m_camera)) {
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

    if (m_menuBg != entt::null && registry.valid(m_menuBg)) {
        placeSprite(registry, m_menuBg, box * 0.5, box, -1.0f, 0.0f);
    }
    // The medal screen's furniture, and the counter the medal follows.
    //
    // Placed here rather than in buildMenu because these sit on the CAMERA'S
    // view, which moves with the level behind them and changes shape with the
    // window - the same reason the buttons are laid out every tick.
    if ((m_screen == Screen::Finished && m_lastCleared) || m_screen == Screen::Dead) {
        const glm::dvec2 view = ViewPx();
        const glm::dvec2 centre = m_follow.centrePx;
        const auto onView = [&view, &centre](const glm::dvec2& atView) {
            return centre + glm::dvec2((atView.x - 0.5) * view.x, (atView.y - 0.5) * view.y);
        };

        for (const Decoration& decoration : m_menuDecor) {
            if (decoration.quad == entt::null || !registry.valid(decoration.quad)) continue;

            glm::dvec2 sizePx(0.0);
            if (decoration.sizing == Decoration::Sizing::Stretched) {
                sizePx = glm::dvec2(view.x * decoration.sizeView.x, view.y * decoration.sizeView.y);
            } else {
                const double height = view.y * decoration.heightView;
                const glm::dvec2 image = imageSizePx(decoration.image);
                const double aspect = image.y > 0.0 ? image.x / image.y : 1.0;
                sizePx = glm::dvec2(height * aspect, height);
            }
            // The pivot, as a shift of the CENTRE: placeSprite centres a quad on
            // the point it is given, and the original places a sprite by its
            // origin, so a sprite whose origin is p has its centre at
            // pos + size * (0.5 - p). The default (0.5, 0.5) makes that zero,
            // which is why only the golden-score plaque moves.
            const glm::dvec2 fromPivot = sizePx * (glm::dvec2(0.5) - decoration.pivot);
            placeSprite(registry, decoration.quad,
                        onView(decoration.atView) + decoration.offsetPx + fromPivot, sizePx,
                        decoration.z, 0.0f);
        }
    }

    // The counter and the medal belong to the MEDAL screen alone: the lost
    // screen has neither, and m_lastCleared may hold nothing at all while it
    // is up - the player need never have finished a level to die on one.
    if (m_screen == Screen::Finished && m_lastCleared) {
        // THE COUNTER, on the frame's clock: one step of one every 100 ms,
        // toward the portals the play spent. The original's ScoreCounter is a
        // Timer with that stride, and its draw reads getCurrent() every frame.
        m_counterClockMs += static_cast<double>(MagicPortalsLayer::kTick) * 1000.0;
        while (m_counterClockMs >= kCounterStrideMs && m_counterShown < m_lastCleared->portalsUsed) {
            m_counterClockMs -= kCounterStrideMs;
            ++m_counterShown;
        }

        // And the medal follows it. Rewriting the texture rather than rebuilding
        // the quad: makeSprite bakes the path into the material, and a quad
        // rebuilt every time the tier changed would lose its place in the
        // drawing order for a frame.
        if (const int shown = MedalShown(); shown != m_medalDrawn && m_menuTitle != entt::null &&
                                            registry.valid(m_menuTitle)) {
            m_medalDrawn = shown;
            const char* file = shown == 3   ? "medal_gold_l.png"
                               : shown == 2 ? "medal_silver_l.png"
                                            : "medal_bronze_l.png";
            registry.get<Supersonic::MaterialComponent>(m_menuTitle).albedoTexturePath = menuImage(file);
        }
    }

    if (m_menuTitle != entt::null && registry.valid(m_menuTitle)) {
        if (m_screen == Screen::Finished) {
            // At the original's own place for it: screenSize * (0.47, 0.55),
            // which the port reads on the camera's view. This was an eyeballed
            // offset from the centre before the constructor was decoded, and
            // then (0.55, 0.47) - the decoded pair read in the wrong order.
            const glm::dvec2 view = ViewPx();
            const glm::dvec2 at = m_follow.centrePx + glm::dvec2((0.47 - 0.5) * view.x, (0.55 - 0.5) * view.y);
            placeSprite(registry, m_menuTitle, at, glm::dvec2(view.y * 0.30), 0.6f, 0.0f);
        } else {
            placeSprite(registry, m_menuTitle, glm::dvec2(box.x * 0.5, box.y * 0.30),
                        glm::dvec2(box.y * 1.30, box.y * 0.30), 0.4f, 0.0f);
        }
    }
    for (std::size_t i = 0; i < m_menuButtons.size() && i < m_menuQuads.size(); ++i) {
        const MenuButton& button = m_menuButtons[i];
        if (m_menuQuads[i] != entt::null && registry.valid(m_menuQuads[i])) {
            placeSprite(registry, m_menuQuads[i], button.centrePx, button.sizePx, 0.5f, 0.0f);
        }
        if (i < m_menuLabels.size() && m_menuLabels[i] != entt::null && registry.valid(m_menuLabels[i])) {
            const glm::vec3 centre = Units::ToWorld(button.centrePx.x, button.centrePx.y);
            registry.get<TransformComponent>(m_menuLabels[i]).position = glm::vec3(centre.x, centre.y, 0.6f);
        }
        if (i < m_menuMedals.size() && m_menuMedals[i] != entt::null && registry.valid(m_menuMedals[i])) {
            // FROM THE BUTTON'S TOP-LEFT, and as a proportion of it.
            //
            // Two things the original does that a centred quad does not.
            //
            // The 3-argument drawScaledSprite (utilSprite.angelscript, bytes
            // 318043..318245) pushes vector2(0, 0) as the origin and hands on,
            // and drawSprite calls SetSpriteOrigin with it before
            // DrawShapedSprite - so `pos` is where the sprite's TOP-LEFT goes,
            // not its middle. level_button.png is 64x64 and medal_gold_m.png is
            // 32x32, so the medal covers 36..68 of the button in both axes: a
            // badge over its bottom-right corner, four pixels proud of it.
            //
            // Measured from the CENTRE instead, as this first did, the medal
            // landed a whole half-button further out and floated in the gap
            // beside the button, touching nothing.
            //
            // And the 36 is in the original's pixels against that 64px button,
            // while the port's buttons are a fraction of the menu box - so the
            // offset and the medal both ride the ratio between the two, which
            // is the same place on the button at any window shape.
            // AS FRACTIONS OF THE BUTTON, never off the file's own size.
            //
            // Both numbers come from the original's SD pair - a 64 px button and
            // a 32 px medal placed at (36, 36) - so as ratios they are 32/64 and
            // 36/64 whatever resolution was actually loaded. Measured against the
            // file instead, as this did, the HD switch moved the badge: the
            // button's native width doubled to 128, the ratio halved, and 36 *
            // ratio put the medal in the middle of the tile rather than over its
            // bottom-right corner. The medal's SIZE happened to survive, because
            // both files doubled together - which is precisely the kind of
            // accident that hides a bug until one of the pair has no hd twin.
            constexpr double kNativeButtonPx = 64.0;
            const glm::dvec2 sizePx = button.sizePx * (32.0 / kNativeButtonPx);
            const glm::dvec2 topLeft = button.centrePx - button.sizePx * 0.5;
            placeSprite(registry, m_menuMedals[i],
                        topLeft + button.sizePx * (36.0 / kNativeButtonPx) + sizePx * 0.5, sizePx, 0.55f, 0.0f);
        }
    }

    // Escape goes up a screen; from the first there is nowhere up to go.
    if (Input::TickWasPressed(kBack)) {
        if (m_screen == Screen::Levels) {
            openMenu(registry, Screen::Worlds);
        } else if (m_screen == Screen::Worlds) {
            openMenu(registry, Screen::Main);
        }
        return;
    }

    const auto* viewport = registry.ctx().find<ViewportInfo>();
    if (viewport == nullptr || !viewport->pointerOverGame || !Input::TickWasPressed(kTap)) return;
    glm::dvec2 atPx(0.0);
    if (!ScreenToLevelPx(registry, Input::MousePosition(), atPx)) return;
    for (const MenuButton& button : m_menuButtons) {
        const glm::dvec2 half = button.sizePx * 0.5;
        if (std::fabs(atPx.x - button.centrePx.x) > half.x) continue;
        if (std::fabs(atPx.y - button.centrePx.y) > half.y) continue;
        PressMenu(registry, button);
        return;
    }
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
    // Unlit, as the remake draws its canvas. The original's lights are not
    // ported, which the remaster's doc records.
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
}

void MagicPortalsLayer::syncSprites(entt::registry& registry) {
    using namespace Supersonic;
    for (DrawnSprite& drawn : m_sprites) {
        if (drawn.quad == entt::null) continue;
        const Sprites::Sprite& sprite = drawn.sprite;
        glm::dvec2 centrePx = Sprites::CentrePx(sprite);
        float rotation = Units::ToWorldRotation(sprite.rotation);
        bool gone = false;
        if (drawn.crystal >= 0) {
            const Goals::Crystal& crystal = m_level.goals.crystals[static_cast<std::size_t>(drawn.crystal)];
            gone = crystal.collected || crystal.expired;
            // A timed crystal fades as it runs out: the remake's guess, as the
            // box's is, and here as the alpha the remake fades.
            float alpha = 1.0f;
            if (crystal.timed && crystal.leftS < 2.0) {
                alpha = 0.4f + 0.6f * static_cast<float>(std::fabs(std::sin(crystal.leftS * 12.0)));
            }
            if (!gone) registry.get<MaterialComponent>(drawn.quad).albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
        } else if (drawn.staticPortal >= 0) {
            gone = !m_level.portals.statics[static_cast<std::size_t>(drawn.staticPortal)].live;
        } else if (drawn.zone >= 0) {
            // A patrolling zone carries its picture with it.
            const Portals::NoPortalZone& zone = m_level.portals.zones[static_cast<std::size_t>(drawn.zone)];
            centrePx += zone.CentreNowPx() - zone.centrePx;
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
            }
        }
        if (gone) {
            registry.destroy(drawn.quad);
            drawn.quad = entt::null;
            continue;
        }
        placeSprite(registry, drawn.quad, centrePx, sprite.sizePx, drawn.z, rotation);
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
    };
    std::vector<Loose> loose;
    for (const Launchers::Thrown& thrown : m_level.launchers.live) {
        loose.push_back({thrown.body, thrown.is.radiusPx, thrown.is.sprite});
    }
    for (const Boss::Rock& rock : m_level.boss.rocks) {
        loose.push_back({rock.body, m_level.boss.rock.radiusPx, m_level.boss.rock.sprite});
    }
    for (const Loose& thrown : loose) {
        if (!registry.valid(thrown.body)) continue;
        auto drawn = std::find_if(m_thrown.begin(), m_thrown.end(),
                                  [&thrown](const ThrownBox& d) { return d.body == thrown.body; });
        if (drawn == m_thrown.end()) {
            ThrownBox made;
            made.body = thrown.body;
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
            registry.get<MaterialComponent>(m_beholderQuad).albedoColor = glm::vec4(1.0f, left, left, 1.0f);
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
    if (m_screen == Screen::Main) {
        status = "Magic Portals";
    } else if (m_screen == Screen::Worlds) {
        status = "Choose a chapter";
    } else if (m_screen == Screen::Levels) {
        status = "Chapter " + Count(m_menuWorld + 1) + " - choose a level";
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
    const bool playing = m_loaded && m_screen == Screen::None;
    if (m_lastCleared && !playing) {
        const Cleared& c = *m_lastCleared;
        result = c.label + " cleared with " + Count(c.portalsUsed) + (c.portalsUsed == 1 ? " portal" : " portals") +
                 (c.crystalsTotal > 0 ? ", " + Count(c.crystals) + "/" + Count(c.crystalsTotal) + " crystals" : "") +
                 (c.Gold() ? " - gold" : "");
    }
    set(m_hud.result, result);
    // The keys are the port's, and a level has the original's own pads and
    // buttons on it now, so the help goes with the rest of the text.
    set(m_hud.controls, m_screen != Screen::None ? "Click a button.  Escape goes back." : "");
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
}

void MagicPortalsLayer::unloadControls() {
    m_controls.clear();
    m_ringImage.clear();
    m_plaqueImage.clear();
    m_medalImage.clear();
    m_captionText.clear();
}

bool MagicPortalsLayer::tutorialPads() const {
    const Chapters::Level* level = Current();
    return m_hudReady && level != nullptr && level->name == m_hudRules.pads.tutorialLevel;
}

void MagicPortalsLayer::layOutControls() {
    const glm::dvec2 view = ViewPx();
    // Only while a level is being played: the menu screens have their own
    // buttons, and neither going into the door nor dying leaves any up.
    const bool playing = m_loaded && m_screen == Screen::None && !m_finishing && !m_dying;
    // A weightless level has no walk pads at all: MainCharacter neither updates
    // nor draws them when noGravity is set.
    const bool pads = playing && !m_level.portals.noGravity;
    for (ControlButton& button : m_controls) {
        switch (button.kind) {
        case Control::Left:
            button.rect = Hud::PadRect(m_hudRules, Hud::Side::Left, view, m_levelAgeMs);
            button.shown = pads;
            break;
        case Control::Right:
            button.rect = Hud::PadRect(m_hudRules, Hud::Side::Right, view, m_levelAgeMs);
            button.shown = pads;
            break;
        case Control::Reset:
            button.rect = Hud::Place(m_hudRules.restart, view);
            button.shown = playing;
            break;
        case Control::Menu:
            button.rect = Hud::Place(m_hudRules.pause, view);
            button.shown = playing;
            break;
        case Control::Clear:
            // PortalManager::update: none in a level that grants no portal, and
            // otherwise exactly while a placed one is alive.
            button.rect = Hud::Place(m_hudRules.clearPortals, view);
            button.shown = playing && m_level.portals.budget > 0 && !m_level.portals.placed.empty();
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
    // Nothing over a screen: the menus and the medal are drawn in the level's
    // space, and the overlay is drawn after all of it.
    if (!m_hudReady || !m_loaded || m_screen != Screen::None) return;
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
    if (const Hud::Ring ring = Hud::RingAt(m_hudRules, m_levelAgeMs, tutorial);
        ring.shown && padsShown && !m_ringImage.empty()) {
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
    const double controls = Hud::Opacity(m_hudRules);
    for (const Control kind : {Control::Reset, Control::Menu}) {
        if (const ControlButton* button = find(kind)) add(button->rect, button->image, white(controls));
    }
    if (!m_plaqueImage.empty() && !m_medalImage.empty()) {
        const double plaque = Hud::PlaqueAlpha(m_hudRules, m_levelAgeMs);
        const auto centred = [](const glm::dvec2& centre, const glm::dvec2& size) {
            return Hud::Rect{centre - size * 0.5, size};
        };
        add(centred(m_hudRules.plaque.centreUnits, m_hudRules.plaque.sizeUnits), m_plaqueImage, white(plaque));
        add(centred(m_hudRules.plaque.medalCentreUnits, m_hudRules.plaque.medalSizeUnits), m_medalImage,
            white(plaque));
    }
    if (const ControlButton* clear = find(Control::Clear)) add(clear->rect, clear->image, white(controls));

    // 4. The two blacks, with the pads between them: BaseState's FadeInController,
    //    the character's pads, then Game's own FadeInController. In display
    //    values, as the original drew them, so each is its decoded alpha as it
    //    stands - two stacked blacks leave (t / 700)^2 of the picture, t being
    //    their own age: the level's less overlay.startAfterMs (step 40).
    const Hud::Rect whole{glm::dvec2(0.0), view};
    const int over = m_hudRules.overlay.layersOverPads;
    const int under = m_hudRules.overlay.layers - over;
    const auto black = [](double alpha) { return glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(alpha)); };
    add(whole, std::string(), black(Hud::OverlayLayersAlpha(m_hudRules, m_levelAgeMs, under)));
    const double pads = Hud::PadOpacity(m_hudRules, m_levelAgeMs, tutorial);
    for (const Control kind : {Control::Left, Control::Right}) {
        if (const ControlButton* button = find(kind)) add(button->rect, button->image, white(pads));
    }
    add(whole, std::string(), black(Hud::OverlayLayersAlpha(m_hudRules, m_levelAgeMs, over)));

    // 5. "Part N", over everything, laid out for this view's width.
    const double caption = Hud::CaptionAlpha(m_hudRules, m_levelAgeMs);
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

void MagicPortalsLayer::hideHud() {
    for (ControlButton& button : m_controls) button.shown = false;
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

float MagicPortalsLayer::readInput(entt::registry& registry) {
    using Supersonic::Input;
    float direction = 0.0f;
    if (Input::IsDown(kLeft) || Input::IsDown(kLeftAlt)) direction -= 1.0f;
    if (Input::IsDown(kRight) || Input::IsDown(kRightAlt)) direction += 1.0f;

    // The on-screen controls, laid out and asked about before anything else
    // reads the pointer.
    layOutControls();
    const ControlButton* under = controlUnderPointer(registry);
    const bool held = Input::IsDown(kTap);
    const bool pressed = Input::TickWasPressed(kTap);

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
        case Control::Menu:
            if (pressed) openMenu(registry, Screen::Worlds);
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
    if (m_screen == Screen::Main || m_screen == Screen::Worlds || m_screen == Screen::Levels) {
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
    // Asked for on the TICK, where a key press is an edge, and answered on the
    // frame, where the picture is.
    if (Input::TickWasPressed(kDump)) m_dumpRequested = true;
    // A menu is up instead of a level: it takes the tick, and nothing below
    // runs. The two are never both in the registry.
    if (m_screen != Screen::None) {
        menuTick(registry);
        updateHud(registry);
        return;
    }
    // The bodies' boxes over the art, or not: the picture only.
    if (Input::TickWasPressed(kBoxes)) m_showBoxes = !m_showBoxes;
    // Out of a level, to the grid it came from - a chapter's end included,
    // which otherwise has nowhere to go.
    if (m_current >= 0 && Input::TickWasPressed(kBack)) {
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

    if (m_loaded) {
        // The app has just stepped physics. So first what follows a step...
        Game::AfterStep(m_data, registry, m_level, fixedDelta);
        // What the tick just did, remembered for the frame to play. Before the
        // two branches below, which take the level away.
        latchSimSounds();
        // ...and the moment the exit reports, the next level (main.gd:161-168). A
        // death is a retry, at once (main.gd:155-158). Should both come on one
        // tick, reaching the exit wins: the remake's order of two triggers in a
        // frame is not defined.
        if (m_finishing) {
            // Already in the door: the level keeps running behind the effect,
            // as the original's does, and the score comes when the beat is up.
            m_finishClockMs += static_cast<double>(fixedDelta) * 1000.0;
            if (m_finishClockMs >= kFinishDelayMs) clearLevel(registry);
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
        } else if (m_dying) {
            // Killed, and the lost screen not up yet: the level goes on running
            // behind it exactly as it does behind the door's effect.
            m_dyingClockMs += static_cast<double>(fixedDelta) * 1000.0;
            if (m_dyingClockMs >= kDeathDelayMs) openDead(registry);
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
        }
    }
    if (m_loaded) {
        // The level is a tick older. BEFORE the input, because the pads slide
        // in and a tap is tested against where they are on this tick.
        m_levelAgeMs += static_cast<double>(fixedDelta) * 1000.0;
        // Then this tick's input, and what comes before the next step.
        const float direction = readInput(registry);
        // The pause button opens a screen, which unloads the level: nothing
        // below has a level to step or a player to follow any more.
        if (!m_loaded || m_screen != Screen::None) {
            updateHud(registry);
            return;
        }
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
    updateHud(registry);
}

// ---- per frame: nothing the level or a replay depends on --------------------

void MagicPortalsLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    // The entities' particles. They belong to the FRAME and not to the tick,
    // because a particle is a picture: nothing below may reach Game::Level,
    // the simulation's clock or the state hash. Carried before the camera's
    // early return, so a level drawn without one does not freeze them.
    updateEmitters(registry, deltaTime);
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
    // Only the camera's SHAPE, for the viewport this frame is drawn into.
    if (m_camera == entt::null || !registry.valid(m_camera)) return;
    registry.get<Supersonic::CameraComponent>(m_camera).aspect = viewportAspect(registry);
}

} // namespace MagicPortals
