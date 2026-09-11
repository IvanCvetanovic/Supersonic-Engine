// Magic Portals' first view. MagicPortalsLayer.hpp says what it is and what it
// is not.

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

#include "sim/Units.hpp"

namespace MagicPortals {

namespace {

// The level fills the viewport one way, with this much to spare.
constexpr double kFitMargin = 1.05;

// How far in front of the level the camera stands. It is orthographic, so only
// the ordering matters: everything drawn lies between it and the far plane.
constexpr float kCameraDistance = 20.0f;

// Markers - buttons, crystals, the exit, portals - stand in front of the bodies.
constexpr float kMarkerZ = 0.5f;
constexpr float kMarkerDepth = 0.1f;

const glm::vec3 kStaticColour(0.42f, 0.44f, 0.50f);
const glm::vec3 kDoorColour(0.30f, 0.45f, 0.75f);
const glm::vec3 kCrateColour(0.72f, 0.50f, 0.26f);      // teleportable
const glm::vec3 kFixedCrateColour(0.42f, 0.28f, 0.16f); // teleportable 0
const glm::vec3 kPlayerColour(1.00f, 0.78f, 0.25f);
const glm::vec3 kButtonUpColour(0.80f, 0.22f, 0.18f);
const glm::vec3 kButtonDownColour(0.25f, 0.85f, 0.30f);
const glm::vec3 kCrystalColour(0.35f, 0.90f, 1.00f);
const glm::vec3 kExitColour(0.25f, 0.70f, 0.35f);
const glm::vec3 kExitReachedColour(0.60f, 1.00f, 0.60f);
const glm::vec3 kPortalColour(0.90f, 0.30f, 0.90f);
const glm::vec3 kStaticRedColour(0.90f, 0.30f, 0.25f);  // a static portal the level colours red
const glm::vec3 kStaticBlueColour(0.30f, 0.50f, 1.00f); // and blue

// The box a body's shape takes up, relative to its entity node, in the remake's
// pixels: a rectangle's size, a circle's diameter, or a polygon's bounds. False
// for a node with no Body/Shape.
bool ShapeBoxPx(const Tscn::Scene& scene, const Tscn::Node& node, glm::dvec2& offsetPx, glm::dvec2& sizePx) {
    const Tscn::Node* body = scene.Child(node, "Body");
    const Tscn::Node* shape = body != nullptr ? scene.Child(*body, "Shape") : nullptr;
    if (shape == nullptr) return false;
    offsetPx = glm::dvec2(0.0);
    if (const Tscn::Value* position = shape->Find("position");
        position != nullptr && position->kind == Tscn::Value::Kind::Vector2) {
        offsetPx = glm::dvec2(position->numbers[0], position->numbers[1]);
    }
    if (shape->type == "CollisionPolygon2D") {
        const Tscn::Value* polygon = shape->Find("polygon");
        if (polygon == nullptr || polygon->numbers.size() < 2) return false;
        glm::dvec2 lo(polygon->numbers[0], polygon->numbers[1]);
        glm::dvec2 hi = lo;
        for (std::size_t i = 0; i + 1 < polygon->numbers.size(); i += 2) {
            const glm::dvec2 point(polygon->numbers[i], polygon->numbers[i + 1]);
            lo = glm::min(lo, point);
            hi = glm::max(hi, point);
        }
        offsetPx += (lo + hi) * 0.5;
        sizePx = hi - lo;
        return true;
    }
    const Tscn::Value* ref = shape->Find("shape");
    const Tscn::Resource* resource = ref != nullptr ? scene.Embedded(ref->text) : nullptr;
    if (resource == nullptr) return false;
    if (resource->type == "RectangleShape2D") {
        const Tscn::Value* size = resource->Find("size");
        if (size == nullptr || size->kind != Tscn::Value::Kind::Vector2) return false;
        sizePx = glm::dvec2(size->numbers[0], size->numbers[1]);
        return true;
    }
    if (resource->type == "CircleShape2D") {
        double radius = 0.0;
        const Tscn::Value* value = resource->Find("radius");
        if (value == nullptr || !value->AsNumber(radius)) return false;
        sizePx = glm::dvec2(radius * 2.0);
        return true;
    }
    return false;
}

bool IsTrigger(const entt::registry& registry, entt::entity entity) {
    using namespace Supersonic;
    if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) return box->isTrigger;
    if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) return sphere->isTrigger;
    if (const auto* hull = registry.try_get<ConvexHullColliderComponent>(entity)) return hull->isTrigger;
    return false;
}

std::string Count(int n) { return std::to_string(n); }

} // namespace

MagicPortalsLayer::MagicPortalsLayer(std::string levelPath, std::string dataDirectory,
                                     std::filesystem::path prismDirectory)
    : m_levelPath(std::move(levelPath)),
      m_dataDirectory(std::move(dataDirectory)),
      m_prismDirectory(std::move(prismDirectory)) {}

// ---- attach and detach ------------------------------------------------------

void MagicPortalsLayer::OnAttach(entt::registry& registry) {
    // The port thinks at 60 Hz, and the layer states it: there is no scene to
    // author the clock in.
    auto& clock = registry.ctx().contains<Supersonic::SimulationClock>()
                      ? registry.ctx().get<Supersonic::SimulationClock>()
                      : registry.ctx().emplace<Supersonic::SimulationClock>();
    clock.fixedDelta = kTick;

    std::string error;
    if (!Game::LoadData(m_levelPath, m_dataDirectory, m_prismDirectory, m_data, error) ||
        !Game::Start(m_data, registry, m_level, error)) {
        m_loadError = error;
    } else {
        for (const Tscn::Node& node : m_data.scene.nodes) {
            if (node.parent != "." || Roles::RoleOf(m_data.roles, node) != Roles::kLevelBounds) continue;
            if (const Tscn::Value* at = node.Find("position");
                at != nullptr && at->kind == Tscn::Value::Kind::Vector2) {
                m_boundsPx = glm::dvec2(at->numbers[0], at->numbers[1]);
            }
        }
        if (m_boundsPx.x <= 0.0 || m_boundsPx.y <= 0.0) {
            m_loadError = m_levelPath + " has no level_bounds";
        } else {
            m_loaded = true;
        }
    }
    if (!m_loadError.empty()) {
        SUPERSONIC_LOG_ERROR("Magic Portals") << "The level did not load: " << m_loadError << std::endl;
    }

    bindInput();
    if (m_loaded) {
        buildCamera(registry);
        buildDrawables(registry);
        syncDrawables(registry);
    }
    buildHud(registry);
    updateHud(registry);
}

void MagicPortalsLayer::OnDetach(entt::registry& registry) {
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
    destroy(m_player);
    destroy(m_exit);
    destroy(m_camera);
    destroy(m_light);
    destroy(m_hud.status);
    destroy(m_hud.controls);
    // And the level's own bodies.
    for (auto& [name, entity] : m_level.built.entities) {
        entt::entity e = entity;
        destroy(e);
    }
    destroy(m_level.player);
    m_level = Game::Level{};
    m_loaded = false;
}

void MagicPortalsLayer::bindInput() {
    using namespace Supersonic;
    Input::BindActionKey(kLeft, Key::Left);
    Input::BindActionKey(kLeftAlt, Key::A);
    Input::BindActionKey(kRight, Key::Right);
    Input::BindActionKey(kRightAlt, Key::D);
    Input::BindActionMouseButton(kTap, MouseButton::Left);
}

// ---- the camera -------------------------------------------------------------

void MagicPortalsLayer::fitCamera(Supersonic::CameraComponent& camera, const glm::vec2& viewportSize) const {
    camera.aspect = viewportSize.x / viewportSize.y;
    const double fitPx = std::max(m_boundsPx.y, m_boundsPx.x / static_cast<double>(camera.aspect)) * kFitMargin;
    camera.orthoHeight = Units::ToMetres(fitPx);
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
    // Over the level's centre, looking down -z at the plane the level lies in,
    // with the engine's +y up the screen.
    const glm::vec3 centre = Units::ToWorld(m_boundsPx.x * 0.5, m_boundsPx.y * 0.5);
    camera.position = glm::vec3(centre.x, centre.y, kCameraDistance);
    camera.yaw = -90.0f;
    camera.pitch = 0.0f;
    camera.updateCameraVectors();
    fitCamera(camera, glm::vec2(1280.0f, 720.0f)); // until a viewport says otherwise
    registry.emplace<TransformComponent>(m_camera).position = camera.position;

    m_light = registry.create();
    registry.emplace<TagComponent>(m_light, "Magic Portals Light");
    auto& light = registry.emplace<LightComponent>(m_light);
    light.type = 0;
    light.direction = glm::vec3(0.35f, 0.6f, 1.0f);
    light.intensity = 1.3f;
}

bool MagicPortalsLayer::ScreenToLevelPx(const entt::registry& registry, const glm::vec2& screenPoint,
                                        glm::dvec2& outPx) const {
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (!m_loaded || viewport == nullptr || m_camera == entt::null || !registry.valid(m_camera)) return false;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return false;
    // The camera as THIS viewport would draw it, whatever shape it last had.
    Supersonic::CameraComponent camera = registry.get<Supersonic::CameraComponent>(m_camera);
    fitCamera(camera, size);
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
        if (!ShapeBoxPx(m_data.scene, *node, drawn.offsetPx, drawn.sizePx)) continue;
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
}

void MagicPortalsLayer::syncDrawables(entt::registry& registry) {
    using namespace Supersonic;
    for (const Drawn& drawn : m_bodies) {
        if (!registry.valid(drawn.body)) continue;
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
        if (crystal.collected) {
            registry.destroy(m_crystals[i]);
            m_crystals[i] = entt::null;
            continue;
        }
        glm::dvec2 centrePx, sizePx;
        boxPx(crystal.box, centrePx, sizePx);
        placeBox(registry, m_crystals[i], centrePx, glm::dvec2(14.0), kMarkerZ, kMarkerDepth, 0.785398f);
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
    m_hud.controls = label("Magic Portals Controls", UIAnchor::BottomLeft, glm::vec2(24.0f, 24.0f), 20.0f);
}

void MagicPortalsLayer::updateHud(entt::registry& registry) {
    using namespace Supersonic;
    auto set = [&registry](entt::entity e, std::string text) {
        if (e != entt::null && registry.valid(e)) registry.get<UITextComponent>(e).text = std::move(text);
    };
    if (!m_loaded) {
        set(m_hud.status, "The level did not load: " + m_loadError);
        set(m_hud.controls, "");
        return;
    }
    const Goals::State& goals = m_level.goals;
    const Portals::State& portals = m_level.portals;
    std::string status = "Crystals " + Count(static_cast<int>(goals.crystals.size()) - goals.Remaining()) + "/" +
                         Count(static_cast<int>(goals.crystals.size())) + "     Portals placed " +
                         Count(portals.portalsUsed) + "     Live " + Count(static_cast<int>(portals.placed.size())) +
                         "/" + Count(portals.budget);
    if (goals.completed) status += "     EXIT REACHED";
    set(m_hud.status, status);
    set(m_hud.controls, "Left/Right or A/D to walk.  Click to place a portal.");
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
        if (ScreenToLevelPx(registry, Input::MousePosition(), atPx)) m_level.portals.TryPlace(atPx);
    }
    return direction;
}

void MagicPortalsLayer::OnFixedUpdate(entt::registry& registry, float fixedDelta) {
    if (!m_loaded) return;
    // The app has just stepped physics. So first what follows a step, then this
    // tick's input, then what comes before the next step.
    Game::AfterStep(registry, m_level, fixedDelta);
    const float direction = readInput(registry);
    Game::BeforeStep(m_data, registry, m_level, direction, fixedDelta);
    syncDrawables(registry);
    updateHud(registry);
}

// ---- per frame: nothing the level or a replay depends on --------------------

void MagicPortalsLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    (void)deltaTime;
    // Only the camera's SHAPE, for the viewport this frame is drawn into.
    if (m_camera == entt::null || !registry.valid(m_camera)) return;
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport != nullptr && viewport->Size().x > 0.0f && viewport->Size().y > 0.0f) {
        fitCamera(registry.get<Supersonic::CameraComponent>(m_camera), viewport->Size());
    }
}

} // namespace MagicPortals
