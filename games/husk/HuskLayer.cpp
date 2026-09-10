// HUSK's first view. HuskLayer.hpp says what it is and what it is not.
//
// The client-side numbers below are the Rust client's, named as it names them,
// so the two can be compared by eye: camera.rs for the rig, select.rs for
// clicks and boxes, input.rs for which order a right-click becomes, terrain.rs
// for the height of a tier.

#include "HuskLayer.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <tuple>
#include <utility>

#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Raycast.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Mission.hpp"

namespace husk {

namespace {

// ---- camera.rs ------------------------------------------------------------
constexpr float kDistMin = 16.0f;
constexpr float kDistMax = 80.0f;
constexpr float kPitchMinDeg = 50.0f;
constexpr float kPitchMaxDeg = 62.0f;
constexpr float kPanSpeed = 1.1f; // a fraction of the camera distance per second
constexpr float kEdgeMarginPx = 14.0f;
constexpr float kWheelZoomStep = 0.07f;
constexpr float kDragPan = 0.0011f; // world units per pixel per unit of distance

// ---- terrain.rs: one tier of height ----------------------------------------
constexpr float kCliffHeight = 2.4f;

// ---- input.rs and select.rs ------------------------------------------------
constexpr float kPickPx = 18.0f;      // right-click target
constexpr float kClickPickPx = 16.0f; // left-click select
constexpr float kClickMaxPx = 6.0f;   // a shorter drag is a click

constexpr std::size_t kMessageLines = 4;

glm::vec3 rgb(const std::array<float, 3>& c) { return glm::vec3(c[0], c[1], c[2]); }

std::string whole(float v) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.0f", static_cast<double>(v));
    return buffer;
}

glm::vec3 affinityColour(Affinity a) {
    static const glm::vec3 palette[] = {
        {0.55f, 0.60f, 0.72f},
        {0.30f, 0.78f, 0.36f},
        {0.62f, 0.50f, 0.38f},
        {0.62f, 0.42f, 0.82f},
    };
    return palette[static_cast<std::size_t>(a) % (sizeof palette / sizeof palette[0])];
}

bool contains(const std::vector<uint32_t>& ids, uint32_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

HuskLayer::HuskLayer(std::string mission, uint64_t seed) : m_mission(std::move(mission)), m_seed(seed) {}

// ---- attach and detach ------------------------------------------------------

void HuskLayer::OnAttach(entt::registry& registry) {
    // HUSK thinks at 20 Hz. The engine's clock is authored per game, and this
    // game has no scene to author it in, so the layer states it.
    auto& clock = registry.ctx().contains<Supersonic::SimulationClock>()
                      ? registry.ctx().get<Supersonic::SimulationClock>()
                      : registry.ctx().emplace<Supersonic::SimulationClock>();
    clock.fixedDelta = kSimDt;

    m_world = std::make_unique<World>(sharedCatalogs(), m_seed);
    if (m_mission == kSandbox) {
        spawnM2MacroScenario(*m_world);
    } else {
        // Refused, not thrown: a mission with a typo in its name should open
        // an empty map that says why, not close the window.
        try {
            if (auto error = loadMission(*m_world, m_mission)) m_loadError = *error;
        } catch (const std::exception& e) {
            m_loadError = e.what();
        }
        if (!m_loadError.empty()) {
            SUPERSONIC_LOG_ERROR("HUSK") << "Mission '" << m_mission << "' did not load: " << m_loadError
                                         << std::endl;
        }
    }

    bindInput();
    buildTerrain(registry);
    buildCamera(registry);
    buildHud(registry);
    syncDrawables(registry);
    updateHud(registry);

    SUPERSONIC_LOG_INFO("HUSK") << "Running '" << m_mission << "' (seed " << m_seed << "): "
                                << m_world->entities.size() << " entities on a " << m_world->grid.dim
                                << "x" << m_world->grid.dim << " grid." << std::endl;
}

void HuskLayer::OnDetach(entt::registry& registry) {
    auto destroy = [&registry](entt::entity& e) {
        if (e != entt::null && registry.valid(e)) registry.destroy(e);
        e = entt::null;
    };
    for (auto& [id, e] : m_drawables) destroy(e);
    m_drawables.clear();
    for (auto& e : m_static) destroy(e);
    m_static.clear();
    destroy(m_camera);
    destroy(m_hud.resources);
    destroy(m_hud.selection);
    destroy(m_hud.mission);
    destroy(m_hud.status);
    m_selection.clear();
    m_messages.clear();
    m_world.reset();
}

void HuskLayer::bindInput() {
    using namespace Supersonic;
    Input::BindActionMouseButton(kSelect, MouseButton::Left);
    Input::BindActionMouseButton(kCommand, MouseButton::Right);
    Input::BindActionKey(kQueue, Key::LeftShift);
    Input::BindActionKey(kStop, Key::S);
    Input::BindActionKey(kHold, Key::H);
}

entt::entity HuskLayer::makeBox(entt::registry& registry, const char* tag, const glm::vec3& centre,
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

// The map as boxes: a ground plane, each raised tier as the fewest rectangles
// the grid allows, ramps in their own colour, and the authored obstacles.
void HuskLayer::buildTerrain(entt::registry& registry) {
    using namespace Supersonic;
    const NavGrid& g = m_world->grid;
    const float half = g.halfExtent();
    const glm::vec2 centre(g.origin.x + half, g.origin.y + half);

    const entt::entity ground = registry.create();
    registry.emplace<TagComponent>(ground, "HUSK Ground");
    auto& groundTransform = registry.emplace<TransformComponent>(ground);
    groundTransform.position = glm::vec3(centre.x, 0.0f, centre.y);
    groundTransform.scale = glm::vec3(2.0f * half, 1.0f, 2.0f * half);
    registry.emplace<MeshComponent>(ground).primitiveType = "Plane";
    auto& groundMaterial = registry.emplace<MaterialComponent>(ground);
    groundMaterial.albedoColor = glm::vec4(0.20f, 0.24f, 0.17f, 1.0f);
    groundMaterial.roughness = 0.95f;
    groundMaterial.metallic = 0.0f;
    auto& groundRenderable = registry.emplace<RenderableComponent>(ground);
    groundRenderable.castsShadow = false;
    m_static.push_back(ground);

    // Runs of one row extended down the rows while they repeat exactly, which
    // turns a rectangular plateau into one box and a polygon into a staircase.
    struct Run {
        uint32_t x0, x1, y0, y1;
        uint8_t level;
        bool ramp;
    };
    std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, Run> open;
    std::vector<Run> done;
    for (uint32_t cy = 0; cy < g.dim; ++cy) {
        std::map<std::tuple<uint32_t, uint32_t, uint8_t, bool>, Run> next;
        uint32_t cx = 0;
        while (cx < g.dim) {
            const uint8_t level = g.level[g.idx(cx, cy)];
            const bool ramp = g.ramp[g.idx(cx, cy)] != 0;
            if (level == 0 && !ramp) {
                ++cx;
                continue;
            }
            const uint32_t start = cx;
            while (cx < g.dim && g.level[g.idx(cx, cy)] == level && (g.ramp[g.idx(cx, cy)] != 0) == ramp) ++cx;
            const auto key = std::make_tuple(start, cx, level, ramp);
            auto it = open.find(key);
            if (it != open.end()) {
                Run run = it->second;
                run.y1 = cy + 1;
                open.erase(it);
                next.emplace(key, run);
            } else {
                next.emplace(key, Run{start, cx, cy, cy + 1, level, ramp});
            }
        }
        for (auto& [key, run] : open) done.push_back(run);
        open = std::move(next);
    }
    for (auto& [key, run] : open) done.push_back(run);

    for (const Run& run : done) {
        const float height = (static_cast<float>(run.level) + (run.ramp ? 0.5f : 0.0f)) * kCliffHeight;
        if (height <= 0.0f) continue;
        const float x0 = g.origin.x + static_cast<float>(run.x0) * g.cell;
        const float x1 = g.origin.x + static_cast<float>(run.x1) * g.cell;
        const float z0 = g.origin.y + static_cast<float>(run.y0) * g.cell;
        const float z1 = g.origin.y + static_cast<float>(run.y1) * g.cell;
        const float shade = std::min(static_cast<float>(run.level), 4.0f) * 0.05f;
        const glm::vec3 colour = run.ramp ? glm::vec3(0.46f, 0.39f, 0.28f)
                                          : glm::vec3(0.28f + shade, 0.31f + shade, 0.22f);
        m_static.push_back(makeBox(registry, run.ramp ? "HUSK Ramp" : "HUSK Tier",
                                   glm::vec3((x0 + x1) * 0.5f, height * 0.5f, (z0 + z1) * 0.5f),
                                   glm::vec3(x1 - x0, height, z1 - z0), colour));
    }

    for (const Rect2& r : m_world->map.obstacles) {
        const Vec2 mid((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f);
        const float base = groundHeight(mid);
        m_static.push_back(makeBox(registry, "HUSK Obstacle", glm::vec3(mid.x, base + 1.5f, mid.y),
                                   glm::vec3(r.max.x - r.min.x, 3.0f, r.max.y - r.min.y),
                                   glm::vec3(0.15f, 0.15f, 0.17f)));
    }

    const entt::entity sun = registry.create();
    registry.emplace<TagComponent>(sun, "HUSK Sun");
    auto& light = registry.emplace<LightComponent>(sun);
    light.type = 0;
    light.direction = glm::vec3(0.45f, 1.0f, 0.35f);
    light.intensity = 1.3f;
    m_static.push_back(sun);
}

void HuskLayer::buildCamera(entt::registry& registry) {
    using namespace Supersonic;
    m_camera = registry.create();
    registry.emplace<TagComponent>(m_camera, "HUSK Camera");
    auto& camera = registry.emplace<CameraComponent>(m_camera);
    camera.projection = CameraComponent::Projection::Perspective;
    camera.fov = 45.0f;
    camera.nearPlane = 0.5f;
    camera.farPlane = 400.0f;
    camera.isPrimary = true;
    // The editor's flycam would read WASD while the player's hotkeys do.
    camera.flyControlsEnabled = false;
    registry.emplace<TransformComponent>(m_camera);

    // Start over the player: the hero if there is one, else the middle of
    // the player's units, else the middle of their buildings.
    const World& w = *m_world;
    if (w.heroState.kind == HeroState::Kind::Alive) {
        if (const Entity* hero = w.get(w.heroState.id)) m_focus = glm::vec2(hero->pos.cur.x, hero->pos.cur.y);
    } else {
        glm::vec2 sum(0.0f);
        int count = 0;
        for (const bool wantUnits : {true, false}) {
            for (const auto& [id, e] : w.entities) {
                if (!e.hasTeam() || e.team != 0 || e.isUnit() != wantUnits) continue;
                sum += glm::vec2(e.pos.cur.x, e.pos.cur.y);
                ++count;
            }
            if (count > 0) break;
        }
        if (count > 0) m_focus = sum / static_cast<float>(count);
    }
    const float half = w.grid.halfExtent();
    m_focus = glm::clamp(m_focus, glm::vec2(-half), glm::vec2(half));
    m_focusHeight = groundHeight(Vec2(m_focus.x, m_focus.y));
    placeCamera(registry);
}

void HuskLayer::placeCamera(entt::registry& registry) {
    using namespace Supersonic;
    if (m_camera == entt::null || !registry.valid(m_camera)) return;
    auto& camera = registry.get<CameraComponent>(m_camera);
    const float dist = kDistMin + (kDistMax - kDistMin) * m_zoom;
    // Zoomed out, steeper (camera.rs). Render-side trig; the sim has none.
    const float pitchDeg = kPitchMinDeg + (kPitchMaxDeg - kPitchMinDeg) * m_zoom;
    const float pitch = glm::radians(pitchDeg);
    const glm::vec3 focus(m_focus.x, m_focusHeight, m_focus.y);
    camera.position = focus + glm::vec3(0.0f, std::sin(pitch), std::cos(pitch)) * dist;
    // Yaw locked: screen-up is north, which is -Z.
    camera.yaw = -90.0f;
    camera.pitch = -pitchDeg;
    camera.updateCameraVectors();
    registry.get<TransformComponent>(m_camera).position = camera.position;
}

void HuskLayer::buildHud(entt::registry& registry) {
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
    m_hud.resources = label("HUSK Resources", UIAnchor::TopLeft, glm::vec2(24.0f, 18.0f), 28.0f);
    m_hud.mission = label("HUSK Mission", UIAnchor::TopLeft, glm::vec2(24.0f, 58.0f), 22.0f);
    m_hud.status = label("HUSK Status", UIAnchor::TopRight, glm::vec2(24.0f, 18.0f), 22.0f);
    m_hud.selection = label("HUSK Selection", UIAnchor::BottomLeft, glm::vec2(24.0f, 24.0f), 24.0f);
}

// ---- the tick ------------------------------------------------------------------

void HuskLayer::OnFixedUpdate(entt::registry& registry, float fixedDelta) {
    // HUSK's step is 1/20 s by construction and the clock was set to match, so
    // the delta carries nothing the sim does not already know.
    (void)fixedDelta;
    if (!m_world) return;

    // Input first, as in the Rust client: its Update systems push orders and
    // the next SimStep drains them.
    applyInput(registry);

    // SimPaused gates the host, not step(); this layer is the host.
    if (!m_world->paused) step(*m_world);

    readMissionEvents();
    drainEvents();

    // A unit that died leaves the selection with it.
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                     [this](uint32_t id) { return m_world->indexed(id) == nullptr; }),
                      m_selection.end());

    syncDrawables(registry);
    updateHud(registry);
}

// What the Rust client drains every frame, and ONLY that: attacks, deaths and
// casts are for its effects and `mission` for its UI. `kills` is not the
// client's - the sim's own xp and loot systems consume it - and clearing it
// here would change the game. test_husk_layer holds the layer's world to a bare
// one for exactly this reason.
void HuskLayer::drainEvents() {
    m_world->events.attacks.clear();
    m_world->events.deaths.clear();
    m_world->events.casts.clear();
    m_world->events.mission.clear();
}

void HuskLayer::readMissionEvents() {
    for (const MissionFx& fx : m_world->events.mission) {
        std::string line;
        switch (fx.kind) {
        case MissionFx::Kind::Message: line = fx.text; break;
        case MissionFx::Kind::Say: line = fx.speaker + ": " + fx.text; break;
        case MissionFx::Kind::ObjectiveAdded: line = "New objective: " + fx.text; break;
        case MissionFx::Kind::ObjectiveComplete: line = "Done: " + fx.text; break;
        case MissionFx::Kind::ObjectiveFailed: line = "Failed: " + fx.text; break;
        case MissionFx::Kind::Outcome: line = fx.victory ? "VICTORY" : "DEFEAT"; break;
        }
        if (line.empty()) continue;
        m_messages.push_back(std::move(line));
        while (m_messages.size() > kMessageLines) m_messages.pop_front();
    }
}

glm::vec3 HuskLayer::baseOf(const Entity& e) const {
    return glm::vec3(e.pos.cur.x, groundHeight(e.pos.cur), e.pos.cur.y);
}

HuskLayer::Box HuskLayer::boxFor(const Entity& e, bool selected) const {
    const Catalogs& c = m_world->cat();
    Box box;
    const float radius = e.mover.radius > 0.0f ? e.mover.radius : 0.4f;
    switch (e.kind) {
    case EntityKind::Unit: {
        const UnitDef& def = c.units.def(e.unitKind);
        const float width = def.size[0] > 0.0f ? def.size[0] : radius * 2.0f;
        const float height = def.size[1] > 0.0f ? def.size[1] : radius * 2.0f;
        box.size = glm::vec3(width, height, width);
        box.colour = e.hero ? glm::vec3(1.0f, 0.78f, 0.25f) : rgb(def.color);
        break;
    }
    case EntityKind::Building: {
        const BuildingDef& def = c.buildings.def(e.building.kind);
        const float fx = def.footprint[0] > 0 ? static_cast<float>(def.footprint[0]) : radius * 2.0f;
        const float fz = def.footprint[1] > 0 ? static_cast<float>(def.footprint[1]) : radius * 2.0f;
        const float height = def.size[1] > 0.0f ? def.size[1] : 1.5f;
        // A site rises as it is built.
        const float built = e.building.complete() ? 1.0f : std::max(e.building.progress, 0.08f);
        box.size = glm::vec3(fx * 0.92f, height * built, fz * 0.92f);
        box.colour = rgb(def.color) * (e.building.complete() ? 1.0f : 0.55f);
        break;
    }
    case EntityKind::Source: {
        const SourceDef& def = c.sources.def(e.source.kind);
        const float fx = def.footprint[0] > 0 ? static_cast<float>(def.footprint[0]) : radius * 2.0f;
        const float fz = def.footprint[1] > 0 ? static_cast<float>(def.footprint[1]) : radius * 2.0f;
        const float height = def.size[1] > 0.0f ? def.size[1] : 1.5f;
        box.size = glm::vec3(fx * 0.8f, height, fz * 0.8f);
        box.colour = e.source.husked() ? glm::vec3(0.22f) : affinityColour(e.source.affinity);
        break;
    }
    case EntityKind::Item:
        box.size = glm::vec3(0.45f);
        box.colour = glm::vec3(1.0f, 0.9f, 0.3f);
        break;
    }
    // The enemy reads as red whatever it is; the hero keeps his gold.
    if (e.hasTeam() && e.team != 0) box.colour = glm::mix(box.colour, glm::vec3(0.9f, 0.18f, 0.12f), 0.55f);
    if (selected) box.colour = glm::mix(box.colour, glm::vec3(1.0f), 0.45f);
    return box;
}

void HuskLayer::syncDrawables(entt::registry& registry) {
    using namespace Supersonic;
    const World& w = *m_world;

    for (auto it = m_drawables.begin(); it != m_drawables.end();) {
        if (w.entities.count(it->first) == 0) {
            if (registry.valid(it->second)) registry.destroy(it->second);
            it = m_drawables.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& [id, e] : w.entities) {
        const Box box = boxFor(e, contains(m_selection, id));
        const glm::vec3 centre = baseOf(e) + glm::vec3(0.0f, box.size.y * 0.5f, 0.0f);
        auto it = m_drawables.find(id);
        if (it == m_drawables.end()) {
            const entt::entity d = makeBox(registry, "HUSK Entity", centre, box.size, box.colour);
            // Written on the tick and drawn between ticks: the engine lerps
            // from the last tick's transform to this one's.
            registry.emplace<InterpolatedTransformComponent>(d);
            m_drawables.emplace(id, d);
            continue;
        }
        auto& transform = registry.get<TransformComponent>(it->second);
        transform.position = centre;
        transform.scale = box.size;
        registry.get<MaterialComponent>(it->second).albedoColor = glm::vec4(box.colour, 1.0f);
    }
}

// ---- input, on the tick ------------------------------------------------------------

HuskLayer::Split HuskLayer::splitSelection() const {
    Split s;
    for (const uint32_t id : m_selection) {
        const Entity* e = m_world->indexed(id);
        if (e == nullptr || !e->isUnit() || e->team != 0) continue;
        (m_world->cat().units.def(e->unitKind).worker ? s.workers : s.fighters).push_back(id);
        if (e->hero) s.heroes.push_back(id);
    }
    s.channelers = s.workers;
    s.channelers.insert(s.channelers.end(), s.heroes.begin(), s.heroes.end());
    std::sort(s.channelers.begin(), s.channelers.end());
    s.channelers.erase(std::unique(s.channelers.begin(), s.channelers.end()), s.channelers.end());
    s.all = s.workers;
    s.all.insert(s.all.end(), s.fighters.begin(), s.fighters.end());
    return s;
}

void HuskLayer::applyInput(entt::registry& registry) {
    using Supersonic::Input;

    // Stop and Hold need no pointer, only a selection (input.rs).
    const Split split = splitSelection();
    if (!split.all.empty()) {
        if (Input::TickWasPressed(kStop)) m_world->orderQueue.push_back(OrderMsg::stop(split.all));
        if (Input::TickWasPressed(kHold)) m_world->orderQueue.push_back(OrderMsg::hold(split.all));
    }

    // A pointer over a panel, a menu or another window is not the game's, and
    // a drag that wandered there is abandoned rather than resolved later.
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport == nullptr || !viewport->pointerOverGame) {
        m_selectHeld = false;
        return;
    }
    const glm::vec2 pointer = Input::MousePosition();
    const bool queued = Input::IsDown(kQueue);

    if (Input::TickWasPressed(kSelect)) {
        m_selectHeld = true;
        m_pressAt = pointer;
    }
    if (Input::TickWasReleased(kSelect) && m_selectHeld) {
        m_selectHeld = false;
        resolveSelection(registry, m_pressAt, pointer, queued);
    }
    if (Input::TickWasPressed(kCommand)) issueCommand(registry, pointer, queued);
}

// select.rs box_select, on release.
void HuskLayer::resolveSelection(entt::registry& registry, const glm::vec2& start, const glm::vec2& end,
                                 bool additive) {
    const auto map = screenMap(registry);
    if (!map) return;
    const World& w = *m_world;
    const glm::vec2 lo = glm::min(start, end);
    const glm::vec2 hi = glm::max(start, end);
    const glm::vec2 extent = hi - lo;
    const bool isClick = std::max(extent.x, extent.y) < kClickMaxPx;

    if (!additive) m_selection.clear();

    if (isClick) {
        // Own things select normally; a click can also pick ONE hostile or one
        // source, for a read-only look, never mixed with your own.
        std::optional<std::pair<float, uint32_t>> own, enemy, source;
        for (const auto& [id, e] : w.entities) {
            if (!e.indexed || e.isItem()) continue;
            const float radius = e.mover.radius > 0.0f ? e.mover.radius : 0.4f;
            const float d = pickDistancePx(*map, end, baseOf(e), boxFor(e, false).size.y, radius);
            if (d > kClickPickPx) continue;
            auto& slot = e.isSource() ? source : (e.team == 0 ? own : enemy);
            if (!slot || d < slot->first) slot = std::make_pair(d, id);
        }
        if (own) {
            // Picking your own kicks any hostile out of the set.
            m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                             [&w](uint32_t s) {
                                                 const Entity* x = w.get(s);
                                                 return x != nullptr && x->hasTeam() && x->team != 0;
                                             }),
                              m_selection.end());
            auto it = std::find(m_selection.begin(), m_selection.end(), own->second);
            if (additive && it != m_selection.end()) {
                m_selection.erase(it);
            } else if (it == m_selection.end()) {
                m_selection.push_back(own->second);
            }
        } else if (source) {
            m_selection = {source->second};
        } else if (enemy) {
            m_selection = {enemy->second};
        }
        return;
    }

    // A drag takes the player's units, never buildings (the WC3 convention).
    for (const auto& [id, e] : w.entities) {
        if (!e.indexed || !e.isUnit() || e.team != 0) continue;
        glm::vec2 screen;
        if (!map->project(baseOf(e), screen)) continue;
        if (screen.x >= lo.x && screen.x <= hi.x && screen.y >= lo.y && screen.y <= hi.y &&
            !contains(m_selection, id)) {
            m_selection.push_back(id);
        }
    }
}

// input.rs, the Direct right-click.
void HuskLayer::issueCommand(entt::registry& registry, const glm::vec2& pointer, bool queued) {
    const auto map = screenMap(registry);
    if (!map) return;
    World& w = *m_world;
    const Split split = splitSelection();

    // What is under the pointer: anything with a team, a source or an item.
    std::optional<std::pair<float, uint32_t>> best;
    for (const auto& [id, e] : w.entities) {
        if (!e.indexed) continue;
        const float radius = e.mover.radius > 0.0f ? e.mover.radius : 0.4f;
        const float d = pickDistancePx(*map, pointer, baseOf(e), boxFor(e, false).size.y, radius);
        if (d <= kPickPx && (!best || d < best->first)) best = std::make_pair(d, id);
    }
    const Entity* picked = best ? w.get(best->second) : nullptr;
    Vec2 ground;
    const bool onGround = ScreenToGround(registry, pointer, ground);
    auto push = [&w](OrderMsg msg) { w.orderQueue.push_back(std::move(msg)); };

    if (split.all.empty()) {
        // Only a building selected: a right-click on open ground sets its rally.
        if (picked == nullptr && onGround) {
            for (const uint32_t id : m_selection) {
                const Entity* e = w.indexed(id);
                if (e != nullptr && e->isBuilding() && e->team == 0) {
                    push(OrderMsg::setRally(id, ground));
                    break;
                }
            }
        }
        return;
    }

    auto escorts = [&split]() {
        std::vector<uint32_t> out;
        for (const uint32_t id : split.fighters) {
            if (!contains(split.channelers, id)) out.push_back(id);
        }
        return out;
    };

    if (picked != nullptr && picked->isItem()) {
        // The hero fetches it; everyone else walks over.
        if (!split.heroes.empty()) {
            push(OrderMsg::pickup(split.heroes.front(), picked->id, queued));
        } else {
            push(OrderMsg::point(split.all, false, picked->pos.cur, queued));
        }
    } else if (picked != nullptr && picked->hasTeam() && picked->team != 0) {
        // Hostile: fighters attack, workers walk along.
        if (!split.fighters.empty()) push(OrderMsg::attackUnit(split.fighters, picked->id, queued));
        if (!split.workers.empty()) push(OrderMsg::point(split.workers, false, picked->pos.cur, queued));
    } else if (picked != nullptr && picked->isSource()) {
        // Workers and the hero channel; other fighters escort.
        if (!split.channelers.empty()) push(OrderMsg::extract(split.channelers, picked->id, queued));
        const auto others = escorts();
        if (!others.empty()) push(OrderMsg::point(others, false, picked->pos.cur, queued));
    } else if (picked != nullptr && picked->isBuilding() && !picked->building.complete()) {
        if (!split.channelers.empty()) push(OrderMsg::build(split.channelers, picked->id, queued));
    } else if (picked != nullptr && picked->hasTeam() && picked->team == 0 &&
               picked->health.cur < picked->health.max) {
        if (!split.channelers.empty()) push(OrderMsg::repair(split.channelers, picked->id, queued));
        const auto others = escorts();
        if (!others.empty()) push(OrderMsg::point(others, false, picked->pos.cur, queued));
    } else if (onGround) {
        push(OrderMsg::point(split.all, false, ground, queued));
    }
}

// ---- screen and ground ---------------------------------------------------------------

std::optional<HuskLayer::ScreenMap> HuskLayer::screenMap(const entt::registry& registry) const {
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (viewport == nullptr || m_camera == entt::null || !registry.valid(m_camera)) return std::nullopt;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return std::nullopt;
    // Projected for THIS viewport's shape, whatever aspect the camera was
    // last drawn with.
    Supersonic::CameraComponent camera = registry.get<Supersonic::CameraComponent>(m_camera);
    camera.aspect = size.x / size.y;
    ScreenMap map;
    map.viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    map.origin = viewport->rect.min;
    map.size = size;
    return map;
}

bool HuskLayer::ScreenMap::project(const glm::vec3& world, glm::vec2& outScreen) const {
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-6f) return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    // Y is already flipped for Vulkan in the projection, and screen Y grows
    // downward too, so both axes are a straight remap.
    outScreen = origin + glm::vec2((ndc.x + 1.0f) * 0.5f * size.x, (ndc.y + 1.0f) * 0.5f * size.y);
    return true;
}

float HuskLayer::pickDistancePx(const ScreenMap& map, const glm::vec2& cursor, const glm::vec3& base,
                                float height, float radius) {
    float best = FLT_MAX;
    for (const float t : {0.1f, 0.5f, 0.9f}) {
        glm::vec2 screen;
        if (map.project(base + glm::vec3(0.0f, height * t, 0.0f), screen)) {
            best = std::min(best, glm::distance(screen, cursor));
        }
    }
    float footprint = 0.0f;
    glm::vec2 a, b;
    if (map.project(base, a) && map.project(base + glm::vec3(radius, 0.0f, 0.0f), b)) footprint = glm::distance(a, b);
    return std::max(best - footprint, 0.0f);
}

bool HuskLayer::ScreenToGround(const entt::registry& registry, const glm::vec2& screenPoint,
                               Vec2& outGround) const {
    const auto* viewport = registry.ctx().find<Supersonic::ViewportInfo>();
    if (!m_world || viewport == nullptr || m_camera == entt::null || !registry.valid(m_camera)) return false;
    const glm::vec2 size = viewport->Size();
    if (size.x <= 0.0f || size.y <= 0.0f) return false;
    Supersonic::CameraComponent camera = registry.get<Supersonic::CameraComponent>(m_camera);
    camera.aspect = size.x / size.y;
    const Supersonic::Ray ray = Supersonic::Raycast::ScreenPointToRay(viewport->ToLocal(screenPoint), size, camera);
    if (ray.direction.y >= 0.0f) return false;

    // March to the first point under the terrain, then bisect. The terrain is
    // tiers of a flat height, so the first cell whose top the ray passes below
    // is the one it hits - a cliff face included.
    auto below = [this](const glm::vec3& p) { return p.y <= groundHeight(Vec2(p.x, p.z)); };
    const float step = 0.25f;
    float t0 = 0.0f;
    for (float t = step; t <= camera.farPlane; t += step) {
        if (!below(ray.origin + ray.direction * t)) {
            t0 = t;
            continue;
        }
        float lo = t0, hi = t;
        for (int i = 0; i < 16; ++i) {
            const float mid = (lo + hi) * 0.5f;
            (below(ray.origin + ray.direction * mid) ? hi : lo) = mid;
        }
        const glm::vec3 hit = ray.origin + ray.direction * hi;
        outGround = Vec2(hit.x, hit.z);
        return true;
    }
    return false;
}

float HuskLayer::groundHeight(Vec2 p) const {
    if (!m_world || m_world->grid.dim == 0) return 0.0f;
    return static_cast<float>(m_world->grid.levelAt(p)) * kCliffHeight;
}

entt::entity HuskLayer::DrawableFor(uint32_t simId) const {
    const auto it = m_drawables.find(simId);
    return it == m_drawables.end() ? entt::null : it->second;
}

// ---- per frame: the camera, and nothing that touches the sim -------------------------

void HuskLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    using namespace Supersonic;
    if (!m_world || m_camera == entt::null || !registry.valid(m_camera)) return;
    const auto* viewport = registry.ctx().find<ViewportInfo>();
    const bool overGame = viewport != nullptr && viewport->pointerOverGame;

    if (overGame) {
        const float scroll = Input::Scroll();
        if (scroll != 0.0f) m_zoom = std::clamp(m_zoom - scroll * kWheelZoomStep, 0.0f, 1.0f);
    }
    const float dist = kDistMin + (kDistMax - kDistMin) * m_zoom;

    glm::vec2 pan(0.0f);
    if (Input::IsKeyDown(Key::Left)) pan.x -= 1.0f;
    if (Input::IsKeyDown(Key::Right)) pan.x += 1.0f;
    if (Input::IsKeyDown(Key::Up)) pan.y -= 1.0f; // screen-up is north, -Z
    if (Input::IsKeyDown(Key::Down)) pan.y += 1.0f;
    if (overGame) {
        const glm::vec2 cursor = Input::MousePosition();
        const UIRect& rect = viewport->rect;
        if (cursor.x <= rect.min.x + kEdgeMarginPx) {
            pan.x -= 1.0f;
        } else if (cursor.x >= rect.max.x - kEdgeMarginPx) {
            pan.x += 1.0f;
        }
        if (cursor.y <= rect.min.y + kEdgeMarginPx) {
            pan.y -= 1.0f;
        } else if (cursor.y >= rect.max.y - kEdgeMarginPx) {
            pan.y += 1.0f;
        }
        if (Input::IsMouseButtonDown(MouseButton::Middle)) m_focus -= Input::MouseDelta() * dist * kDragPan;
    }
    if (pan != glm::vec2(0.0f)) m_focus += glm::normalize(pan) * kPanSpeed * dist * deltaTime;

    const float half = m_world->grid.halfExtent();
    m_focus = glm::clamp(m_focus, glm::vec2(-half), glm::vec2(half));
    const float target = groundHeight(Vec2(m_focus.x, m_focus.y));
    m_focusHeight += (target - m_focusHeight) * std::min(deltaTime * 7.0f, 1.0f);

    if (viewport != nullptr && viewport->Size().y > 0.0f) {
        registry.get<CameraComponent>(m_camera).aspect = viewport->Size().x / viewport->Size().y;
    }
    placeCamera(registry);
}

// ---- the HUD -------------------------------------------------------------------------

void HuskLayer::updateHud(entt::registry& registry) {
    using namespace Supersonic;
    auto set = [&registry](entt::entity e, std::string text) {
        if (e != entt::null && registry.valid(e)) registry.get<UITextComponent>(e).text = std::move(text);
    };
    const World& w = *m_world;
    const Catalogs& c = w.cat();

    set(m_hud.resources, "Essence " + whole(w.economy.essence) + "     Anima " + whole(w.economy.anima) +
                             "     Will " + std::to_string(w.will.used) + "/" + std::to_string(w.will.cap));

    std::string status = "Tick " + std::to_string(w.tick);
    if (w.paused) status += "  (paused)";
    if (!m_loadError.empty()) status = "'" + m_mission + "' did not load: " + m_loadError;
    set(m_hud.status, status);

    std::string mission;
    if (w.mission) {
        for (const ObjectiveState& o : w.mission->objectives) {
            if (o.status == ObjectiveStatus::Active) mission += "- " + o.text + "\n";
        }
    }
    for (const std::string& line : m_messages) mission += line + "\n";
    set(m_hud.mission, mission);

    auto nameOf = [&c](const Entity& e) -> std::string {
        switch (e.kind) {
        case EntityKind::Unit: return c.units.def(e.unitKind).name;
        case EntityKind::Building: return c.buildings.def(e.building.kind).name;
        case EntityKind::Source: return c.sources.def(e.source.kind).name;
        case EntityKind::Item: return c.items.def(e.itemKind).name;
        }
        return {};
    };
    std::string selection = "Nothing selected";
    if (m_selection.size() == 1) {
        if (const Entity* e = w.get(m_selection.front())) {
            selection = nameOf(*e);
            if (e->hasTeam()) selection += "   " + whole(e->health.cur) + " / " + whole(e->health.max);
            if (e->isSource()) selection += "   essence " + whole(e->source.essence);
            if (e->hero) selection += "   level " + std::to_string(e->hero->level);
        }
    } else if (!m_selection.empty()) {
        std::map<std::string, int> counts;
        for (const uint32_t id : m_selection) {
            if (const Entity* e = w.get(id)) ++counts[nameOf(*e)];
        }
        selection = std::to_string(m_selection.size()) + " selected:";
        for (const auto& [name, n] : counts) selection += "  " + name + " x" + std::to_string(n);
    }
    set(m_hud.selection, selection);
}

} // namespace husk
