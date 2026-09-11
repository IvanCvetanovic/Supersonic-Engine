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
        !Camera::LoadViewHeight(m_paths.portData + "/view.json", m_viewHeightPx, error)) {
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
    for (ThrownBox& thrown : m_thrown) destroy(thrown.box);
    m_thrown.clear();
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

    // One box per body a launcher threw, made and unmade to match.
    for (ThrownBox& drawn : m_thrown) {
        if (registry.valid(drawn.body)) continue;
        if (drawn.box != entt::null && registry.valid(drawn.box)) registry.destroy(drawn.box);
        drawn.box = entt::null;
    }
    std::erase_if(m_thrown, [](const ThrownBox& drawn) { return drawn.box == entt::null; });
    for (const Launchers::Thrown& thrown : m_level.launchers.live) {
        if (!registry.valid(thrown.body)) continue;
        auto drawn = std::find_if(m_thrown.begin(), m_thrown.end(),
                                  [&thrown](const ThrownBox& d) { return d.body == thrown.body; });
        if (drawn == m_thrown.end()) {
            ThrownBox made;
            made.body = thrown.body;
            made.box = makeBox(registry, "Magic Portals Thrown", glm::vec3(0.0f), glm::vec3(1.0f), kStoneColour);
            registry.emplace<InterpolatedTransformComponent>(made.box);
            m_thrown.push_back(made);
            drawn = m_thrown.end() - 1;
        }
        const auto& body = registry.get<TransformComponent>(thrown.body);
        placeBox(registry, drawn->box, Units::ToPixels(body.position), glm::dvec2(thrown.is.radiusPx * 2.0), 0.0f,
                 0.4f, body.rotation.z);
    }

    if (m_level.player != entt::null && registry.valid(m_level.player)) {
        const glm::dvec2 at = Units::ToPixels(registry.get<TransformComponent>(m_level.player).position);
        placeBox(registry, m_player, at, glm::dvec2(m_data.tuning.widthPx, m_data.tuning.heightPx), 0.1f, 0.4f, 0.0f);
    }

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

    // The shot in flight, made when one is fired and unmade when it lands or fails.
    if (m_level.portals.flight) {
        if (m_shot == entt::null) {
            m_shot = makeBox(registry, "Magic Portals Shot", glm::vec3(0.0f), glm::vec3(1.0f), kShotColour);
        }
        placeBox(registry, m_shot, m_level.portals.flight->atPx, glm::dvec2(kShotSizePx), kMarkerZ, kMarkerDepth,
                 0.0f);
    } else if (m_shot != entt::null) {
        if (registry.valid(m_shot)) registry.destroy(m_shot);
        m_shot = entt::null;
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
    set(m_hud.controls, "Left/Right or A/D to walk.  Click to place a portal.  R retries.  N skips a level.");
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
