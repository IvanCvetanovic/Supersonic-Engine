// Magic Portals as the port plays it. MagicPortalsLayer.hpp says what it is and
// what it is not.

#include "MagicPortalsLayer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Raycast.hpp"
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
    std::string error;
    if (!Chapters::Load(m_paths.chapters, m_chapters, error) ||
        !Camera::LoadRules(m_paths.data + "/portals.json", m_cameraRules, error) ||
        !Camera::LoadViewHeight(m_paths.portData + "/view.json", m_viewHeightPx, error) ||
        !Art::LoadRules(m_paths.portData + "/art.json", m_artRules, error)) {
        m_loadError = error;
    } else if (const int start = m_chapters.Find(m_startLevel); start < 0) {
        m_loadError = m_startLevel + " is not a level of " + m_paths.chapters;
    } else {
        buildCamera(registry);
        loadLevel(registry, start);
    }
    if (m_current < 0) {
        SUPERSONIC_LOG_ERROR("Magic Portals") << "Could not start: " << m_loadError << std::endl;
    }
    buildHud(registry);
    updateHud(registry);
}

void MagicPortalsLayer::OnDetach(entt::registry& registry) {
    unloadLevel(registry);
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
}

// ---- levels -----------------------------------------------------------------

bool MagicPortalsLayer::loadLevel(entt::registry& registry, int index) {
    unloadLevel(registry);
    m_current = index;
    m_chapterComplete = false;
    m_loadError.clear();
    const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(index)];

    std::string error;
    if (index != m_dataIndex) {
        m_dataIndex = -1;
        if (!Game::LoadData(m_paths.levels + "/" + entry.name + ".tscn", m_paths.data, m_paths.prisms, m_data,
                            error, m_paths.portData)) {
            m_loadError = error;
            return false;
        }
        m_dataIndex = index;
    }
    if (!Game::Start(m_data, registry, m_level, error)) {
        m_loadError = error;
        // Start may have built some of the level before it refused.
        unloadLevel(registry);
        return false;
    }
    if (!PositionOf(FirstOfRole(m_data, Roles::kLevelBounds), m_boundsPx) || m_boundsPx.x <= 0.0 ||
        m_boundsPx.y <= 0.0) {
        m_loadError = entry.name + " has no level_bounds";
        unloadLevel(registry);
        return false;
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
    m_follow.Start(m_cameraRules, cameraStartPx, ViewPx(), m_boundsPx);
    placeCamera(registry);
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
    m_artReady = false;
    for (auto& e : m_portalQuads) destroy(e);
    m_portalQuads.clear();
    destroy(m_shotQuad);
    destroy(m_playerQuad);
    destroy(m_beholderBox);
    destroy(m_beholderQuad);
    for (auto& e : m_spikes) destroy(e);
    m_spikes.clear();
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
    goTo(registry, m_chapters.Next(m_current));
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
    if (!m_loaded || viewport == nullptr || m_camera == entt::null || !registry.valid(m_camera)) return false;
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
    if (!Sprites::Find(m_data.scene, m_paths.art, sprites, m_artError)) {
        // Played anyway, as boxes: the art is the original's, and a machine
        // without it can still play the port.
        SUPERSONIC_LOG_WARN("Magic Portals") << "Drawing the level as boxes: " << m_artError << std::endl;
        m_playerSlot = 0;
        return;
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

    // The player in its slot among the art - or, with the boxes shown or no art
    // to show, where the boxes are.
    const bool artOnly = m_artReady && !m_showBoxes;
    if (m_level.player != entt::null && registry.valid(m_level.player)) {
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
    const double diameterPx = m_level.portals.rules.collisionRadiusPx * 2.0;
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
        const double sizePx = m_level.portals.rules.collisionRadiusPx * zones[i].scale * 2.0;
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
    if (m_current < 0) {
        status = "Magic Portals could not start: " + m_loadError;
    } else {
        const Chapters::Level& entry = m_chapters.levels[static_cast<std::size_t>(m_current)];
        if (m_chapterComplete) {
            status = "Chapter " + Count(entry.world + 1) + " complete";
        } else if (!m_loaded) {
            status = Chapters::Label(entry) + " is not playable yet: " + m_loadError;
        } else {
            const Goals::State& goals = m_level.goals;
            const int total = static_cast<int>(goals.crystals.size());
            status = Chapters::Label(entry) + "     Crystals " + Count(total - goals.Remaining()) + "/" +
                     Count(total) + "     Portals " + Count(m_level.portals.portalsUsed) + "     Gold: " +
                     Count(entry.goldenScore) + " or fewer";
            if (m_deaths > 0) status += "     Deaths " + Count(m_deaths);
        }
    }
    set(m_hud.status, status);

    std::string result;
    if (m_lastCleared) {
        const Cleared& c = *m_lastCleared;
        result = c.label + " cleared with " + Count(c.portalsUsed) + (c.portalsUsed == 1 ? " portal" : " portals") +
                 (c.crystalsTotal > 0 ? ", " + Count(c.crystals) + "/" + Count(c.crystalsTotal) + " crystals" : "") +
                 (c.Gold() ? " - gold" : "");
    }
    set(m_hud.result, result);
    set(m_hud.controls,
        "Left/Right or A/D to walk.  Click to fire a portal.  R retries.  N skips a level.  B shows the bodies.");
}

// ---- the tick ----------------------------------------------------------------

float MagicPortalsLayer::readInput(entt::registry& registry) {
    using Supersonic::Input;
    float direction = 0.0f;
    if (Input::IsDown(kLeft) || Input::IsDown(kLeftAlt)) direction -= 1.0f;
    if (Input::IsDown(kRight) || Input::IsDown(kRightAlt)) direction += 1.0f;

    // A pointer over a panel or another window is not the game's.
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport != nullptr && viewport->pointerOverGame && Input::TickWasPressed(kTap)) {
        glm::dvec2 atPx(0.0);
        if (ScreenToLevelPx(registry, Input::MousePosition(), atPx)) m_level.portals.Shoot(registry, atPx);
    }
    return direction;
}

void MagicPortalsLayer::OnFixedUpdate(entt::registry& registry, float fixedDelta) {
    using Supersonic::Input;
    // The bodies' boxes over the art, or not: the picture only.
    if (Input::TickWasPressed(kBoxes)) m_showBoxes = !m_showBoxes;
    // Skip and retry first, so the tick that asks plays the level it lands on.
    if (m_current >= 0 && Input::TickWasPressed(kSkip)) {
        goTo(registry, m_chapters.Next(m_current));
    } else if (m_current >= 0 && !m_chapterComplete && Input::TickWasPressed(kRetry)) {
        loadLevel(registry, m_current);
    }

    if (m_loaded) {
        // The app has just stepped physics. So first what follows a step...
        Game::AfterStep(registry, m_level, fixedDelta);
        // ...and the moment the exit reports, the next level (main.gd:161-168). A
        // death is a retry, at once (main.gd:155-158). Should both come on one
        // tick, reaching the exit wins: the remake's order of two triggers in a
        // frame is not defined.
        if (m_level.goals.completed) {
            clearLevel(registry);
        } else if (m_level.hazards.playerDied) {
            ++m_deaths;
            loadLevel(registry, m_current);
        }
    }
    if (m_loaded) {
        // Then this tick's input, and what comes before the next step.
        const float direction = readInput(registry);
        m_direction = direction; // for the picture: which way the player walks this tick
        Game::BeforeStep(m_data, registry, m_level, direction, fixedDelta);
        m_aspect = viewportAspect(registry);
        const glm::dvec2 playerPx =
            Units::ToPixels(registry.get<Supersonic::TransformComponent>(m_level.player).position);
        m_follow.Tick(m_cameraRules, playerPx, ViewPx(), m_boundsPx, fixedDelta);
        placeCamera(registry);
        syncDrawables(registry);
    }
    updateHud(registry);
}

// ---- per frame: nothing the level or a replay depends on --------------------

void MagicPortalsLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    (void)deltaTime;
    // Only the camera's SHAPE, for the viewport this frame is drawn into.
    if (m_camera == entt::null || !registry.valid(m_camera)) return;
    registry.get<Supersonic::CameraComponent>(m_camera).aspect = viewportAspect(registry);
}

} // namespace MagicPortals
