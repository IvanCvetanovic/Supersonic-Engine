#include "WolfBrigadeLayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Components.hpp"
#include "core/Log.hpp"
#include "core/SimulationClock.hpp"

#include "sim/ResourceNode.hpp"
#include "sim/Snapshot.hpp"
#include "sim/Unit.hpp"
#include "sim/Building.hpp"
#include "sim/BuildPlacement.hpp"
#include "sim/GameState.hpp"
#include "sim/Progression.hpp"
#include "sim/Selection.hpp"
#include "sim/WaveDirector.hpp"

using namespace Supersonic;

namespace WolfBrigade {

namespace {

// Godot pixels to engine units.
//
// The ported data is in the original's coordinates - the lane is 6000 px wide
// and the ground sits at y = 800 - because changing them would have meant
// re-deriving every number the twenty-two harnesses check. So the conversion
// lives here, in the view, which is the only place that cares what a metre is.
//
// A hundred to one puts the lane at sixty units and a soldier at about a third
// of one, which is a sensible size in an engine whose default cube is one unit.
constexpr float kPixelsPerUnit = 100.0f;

// Godot's Y grows DOWNWARD and the engine's grows up, so the ground line is a
// mirror rather than an offset. Getting this wrong does not look like an error:
// the lane renders upside down and every unit stands on the sky.
constexpr float kGroundPixels = 800.0f;

glm::vec3 toWorld(const glm::vec2& simPosition, float z) {
    return glm::vec3(simPosition.x / kPixelsPerUnit,
                     (kGroundPixels - simPosition.y) / kPixelsPerUnit,
                     z);
}

// "#3b6fa0" as the engine's linear-ish colour.
//
// Passed straight through without an sRGB conversion, deliberately: these are
// drawn UNLIT, so the value in the file is the value on screen, which is what
// makes a ported ColorRect look like the one it was ported from. A colour that
// went through a transfer function here would be subtly wrong against every
// screenshot of the original.
glm::vec3 parseHex(const std::string& text, const glm::vec3& fallback) {
    if (text.size() < 7 || text[0] != '#') return fallback;

    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    float channels[3]{};
    for (int i = 0; i < 3; ++i) {
        const int high = nibble(text[static_cast<size_t>(1 + i * 2)]);
        const int low = nibble(text[static_cast<size_t>(2 + i * 2)]);
        if (high < 0 || low < 0) return fallback;
        channels[i] = static_cast<float>(high * 16 + low) / 255.0f;
    }
    return glm::vec3(channels[0], channels[1], channels[2]);
}

// Draw order within the lane. Everything is coplanar, so this is the only thing
// separating them - see RenderableComponent::sortKey.
constexpr int32_t kLayerGround = 0;
constexpr int32_t kLayerNode = 1;
constexpr int32_t kLayerBuilding = 2;
constexpr int32_t kLayerUnit = 3;
constexpr int32_t kLayerBarBack = 4;
constexpr int32_t kLayerBarFill = 5;

// The bar's buttons, from bottom_bar.gd. 230x96 is a touch target, not a
// guess: the original says so and the number is load-bearing on a phone.
constexpr float kBarButtonWidth = 230.0f;
constexpr float kBarButtonHeight = 96.0f;
constexpr float kBarFontSize = 22.0f;
constexpr float kBarSpacing = 16.0f;

} // namespace

void WolfBrigadeLayer::OnAttach(entt::registry& registry) {
    m_data = std::make_unique<GameData>();
    if (!m_data->LoadAll(WOLFBRIGADE_DATA_DIR)) {
        SUPERSONIC_LOG_ERROR("WolfBrigade")
            << "could not load the game data from " << WOLFBRIGADE_DATA_DIR
            << "; the lane will be empty." << std::endl;
        return;
    }

    m_profile = std::make_unique<Profile>();
    m_match = std::make_unique<Match>(*m_data, *m_profile, "");
    m_match->BootFresh();
    m_booted = true;

    SUPERSONIC_LOG_INFO("WolfBrigade")
        << "Booted a match: " << m_match->Units().size() << " unit(s), "
        << m_match->Buildings().size() << " building(s)." << std::endl;

    // The camera looks straight down -Z at the lane, orthographic, because a
    // side-on 1D strip through a perspective camera is a strip with perspective
    // in it - the far end of the lane is smaller than the near end, and the
    // original has no such thing.
    m_camera = registry.create();
    registry.emplace<TagComponent>(m_camera, "Lane Camera");
    auto& camera = registry.emplace<CameraComponent>(m_camera);
    camera.projection = CameraComponent::Projection::Orthographic;
    // Framed on the player's end of the lane. The town hall is at 1500 px and
    // the spawn at 1680, so a six-unit-tall window centred just above the
    // ground line puts the base and its workers on screen at a readable size.
    camera.orthoHeight = 6.0f;
    camera.position = glm::vec3(18.0f, 1.4f, 20.0f);
    camera.yaw = -90.0f;
    camera.pitch = 0.0f;
    camera.isPrimary = true;

    // The fly controls are the editor's, and this is a game. Without this,
    // W/A/S/D would fly the lane camera at the same time as the player's own
    // bindings read them.
    camera.flyControlsEnabled = false;
    camera.updateCameraVectors();

    auto& transform = registry.emplace<TransformComponent>(m_camera);
    transform.position = camera.position;

    // A ground strip, so the lane has a floor rather than sitting in the sky.
    const entt::entity ground = registry.create();
    registry.emplace<TagComponent>(ground, "Ground");
    auto& groundTransform = registry.emplace<TransformComponent>(ground);
    groundTransform.position = glm::vec3(30.0f, -1.2f, 0.0f);
    groundTransform.scale = glm::vec3(62.0f, 2.4f, 1.0f);
    registry.emplace<MeshComponent>(ground).primitiveType = "Quad";
    auto& groundMaterial = registry.emplace<MaterialComponent>(ground);
    groundMaterial.albedoColor = glm::vec4(0.16f, 0.19f, 0.14f, 1.0f);
    groundMaterial.unlit = true;
    auto& groundRenderable = registry.emplace<RenderableComponent>(ground);
    groundRenderable.sortKey = kLayerGround;

    buildHud(registry);
}

void WolfBrigadeLayer::buildHud(entt::registry& registry) {
    // Authored in the original's pixels, which is also the canvas's reference
    // height, so every offset below is the number in main.tscn unchanged. That
    // is worth more than it looks: it makes the two screens comparable by eye,
    // which is the only check this port has.
    auto label = [&registry](const char* name, UIAnchor anchor, glm::vec2 offset,
                             float fontSize) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, name);
        auto& text = registry.emplace<UITextComponent>(entity);
        text.anchor = anchor;
        text.offset = offset;
        text.fontSize = fontSize;
        text.text = "";
        return entity;
    };

    m_hud.wood = label("HUD Wood", UIAnchor::TopLeft, glm::vec2(28.0f, 20.0f), 40.0f);
    m_hud.food = label("HUD Food", UIAnchor::TopLeft, glm::vec2(28.0f, 74.0f), 40.0f);

    // Top-RIGHT, because the offset runs inward from the anchored edge: the
    // original spells the same thing as anchor_left = 1 with offset_right =
    // -28, and writing it as a right anchor is what keeps it in the corner on a
    // screen that is not 1920 wide.
    m_hud.wave = label("HUD Wave", UIAnchor::TopRight, glm::vec2(28.0f, 20.0f), 34.0f);

    m_hud.pause = registry.create();
    registry.emplace<TagComponent>(m_hud.pause, "HUD Pause");
    auto& pause = registry.emplace<UIButtonComponent>(m_hud.pause);
    pause.label = "Pause";
    pause.anchor = UIAnchor::TopCenter;
    pause.offset = glm::vec2(0.0f, 20.0f);
    pause.size = glm::vec2(180.0f, 96.0f);
    pause.fontSize = 30.0f;

    // The strip the contextual buttons hang off, created ONCE. Its children
    // come and go with the selection; it does not, so nothing about the
    // container itself can be lost mid-gesture.
    //
    // A horizontal stack anchored to the bottom centre is main.tscn's
    // HBoxContainer with alignment = 1, and the strip it sits on is the Panel
    // above it - as wide as the screen and 132 tall, which is what fillWidth
    // is for.
    const entt::entity backdrop = registry.create();
    registry.emplace<TagComponent>(backdrop, "WB Bar Panel");
    auto& strip = registry.emplace<UIPanelComponent>(backdrop);
    strip.anchor = UIAnchor::BottomCenter;
    strip.offset = glm::vec2(0.0f, 0.0f);
    strip.size = glm::vec2(0.0f, 132.0f);
    strip.fillWidth = true;
    strip.color = glm::vec4(0.09f, 0.10f, 0.13f, 0.92f);
    strip.cornerRadius = 0.0f;
    m_barStack = registry.create();
    registry.emplace<TagComponent>(m_barStack, "WB Bar");
    auto& bar = registry.emplace<UIStackComponent>(m_barStack);
    bar.horizontal = true;
    bar.anchor = UIAnchor::BottomCenter;
    bar.offset = glm::vec2(0.0f, 18.0f);
    bar.spacing = kBarSpacing;
}

void WolfBrigadeLayer::updateHud(entt::registry& registry) {
    if (!m_match) return;

    const GameState& state = m_match->Run();
    const WaveDirector& director = m_match->Director();

    char buffer[128];

    std::snprintf(buffer, sizeof(buffer), "Wood: %d", state.Amount("wood"));
    registry.get<UITextComponent>(m_hud.wood).text = buffer;

    std::snprintf(buffer, sizeof(buffer), "Food: %d", state.Amount("food"));
    registry.get<UITextComponent>(m_hud.food).text = buffer;

    // Two lines, exactly as hud.gd builds it. The countdown is dropped rather
    // than shown as a negative when no wave is scheduled, which is the campaign
    // on its final wave.
    const int number = state.CurrentWave();
    const double seconds = director.SecondsToNextWave();
    if (director.IsEndless()) {
        std::snprintf(buffer, sizeof(buffer), "Wave %d - Endless\nNext in %ds", number,
                      static_cast<int>(std::ceil(seconds)));
    } else if (seconds >= 0.0) {
        std::snprintf(buffer, sizeof(buffer), "Wave %d / %d\nNext in %ds", number,
                      director.TotalWaves(), static_cast<int>(std::ceil(seconds)));
    } else {
        std::snprintf(buffer, sizeof(buffer), "Wave %d / %d\n(final wave)", number,
                      director.TotalWaves());
    }
    registry.get<UITextComponent>(m_hud.wave).text = buffer;

    // THE TICK's click, not the frame's. A frame that runs no tick would
    // otherwise lose the press, and one that runs three would pause, unpause
    // and pause again from a single tap.
    auto& pause = registry.get<UIButtonComponent>(m_hud.pause);
    if (pause.clickedThisTick) m_paused = !m_paused;
    pause.label = m_paused ? "Resume" : "Pause";
}

namespace {

// "75 wood, 20 food" from a cost map, or "free" when there is nothing to pay.
// _format_cost in bottom_bar.gd, including the word.
std::string formatCost(const Supersonic::Json::Value& cost) {
    if (!cost.IsObject() || cost.AsObject().empty()) return "free";

    std::string out;
    for (const auto& [resource, amount] : cost.AsObject()) {
        if (!out.empty()) out += ", ";
        out += std::to_string(static_cast<int>(amount.AsNumber()));
        out += ' ';
        out += resource;
    }
    return out;
}

// A Json cost object as the simulation's own Cost map.
Cost toCost(const Supersonic::Json::Value& cost) {
    Cost out;
    if (!cost.IsObject()) return out;
    for (const auto& [resource, amount] : cost.AsObject()) {
        out[resource] = static_cast<int>(amount.AsNumber());
    }
    return out;
}

} // namespace

std::vector<WolfBrigadeLayer::BarButton> WolfBrigadeLayer::desiredBar() const {
    std::vector<BarButton> wanted;
    if (!m_match) return wanted;

    // A placement in progress takes the whole strip. On touch there is no
    // right-click and no Escape, so this is the only way out of it - which is
    // why it replaces the bar rather than sitting beside it.
    if (m_match->Placement().IsActive()) {
        wanted.push_back(BarButton{entt::null, BarAction::Cancel, {}});
        return wanted;
    }

    const Building* selected = m_match->Picked().SelectedBuilding();
    if (selected != nullptr && selected->IsAlive() && selected->IsComplete()) {
        for (const std::string& unit : selected->Stats().trains) {
            wanted.push_back(BarButton{entt::null, BarAction::Train, unit});
        }
        for (const std::string& upgrade : selected->Stats().researches) {
            // Available, not affordable: an upgrade you cannot pay for yet is
            // shown greyed, and one whose prerequisites are unmet is not shown
            // at all. Collapsing those two would make the tree invisible.
            if (Upgrades::IsAvailable(*m_data, m_match->Run(), upgrade)) {
                wanted.push_back(BarButton{entt::null, BarAction::Research, upgrade});
            }
        }
        return wanted;
    }

    for (const auto& [id, building] : m_data->Buildings().AsObject()) {
        if (building["buildable"].AsBool()) {
            wanted.push_back(BarButton{entt::null, BarAction::Build, id});
        }
    }
    return wanted;
}

std::string WolfBrigadeLayer::barLabel(const BarButton& button) const {
    switch (button.action) {
    case BarAction::Cancel:
        return "Cancel";
    case BarAction::Build: {
        const auto& data = m_data->Building(button.id);
        return "Build " + data["display_name"].AsString(button.id) + "\n(" +
               formatCost(data["cost"]) + ")";
    }
    case BarAction::Train: {
        const auto& data = m_data->Unit(button.id);
        return "Train " + data["display_name"].AsString(button.id) + "\n(" +
               formatCost(data["cost"]) + ")";
    }
    case BarAction::Research: {
        const auto& data = m_data->Upgrade(button.id);
        return "Research " + data["display_name"].AsString(button.id) + "\n(" +
               formatCost(data["cost"]) + ")";
    }
    }
    return {};
}

bool WolfBrigadeLayer::barEnabled(const BarButton& button) const {
    if (!m_match) return false;
    switch (button.action) {
    case BarAction::Cancel:
        return true;
    case BarAction::Build:
        return m_match->Run().CanAfford(toCost(m_data->Building(button.id)["cost"]));
    case BarAction::Train:
        return m_match->Run().CanAfford(toCost(m_data->Unit(button.id)["cost"]));
    case BarAction::Research:
        return Upgrades::CanResearch(*m_data, m_match->Run(), button.id);
    }
    return false;
}

void WolfBrigadeLayer::updateBar(entt::registry& registry) {
    if (!m_match) return;

    const std::vector<BarButton> wanted = desiredBar();

    // THE SIGNATURE, and only the signature, decides whether entities move.
    // Affordability is deliberately not part of it: a worker depositing wood
    // flips several buttons between affordable and not every few seconds, and
    // if that recreated them, a press held across one deposit would be lost.
    bool sameSet = wanted.size() == m_bar.size();
    for (std::size_t i = 0; sameSet && i < wanted.size(); ++i) {
        sameSet = wanted[i].action == m_bar[i].action && wanted[i].id == m_bar[i].id;
    }

    if (!sameSet) {
        for (const BarButton& old : m_bar) {
            if (registry.valid(old.entity)) registry.destroy(old.entity);
        }
        m_bar.clear();

        int32_t order = 0;
        for (const BarButton& button : wanted) {
            BarButton made = button;
            made.entity = registry.create();
            registry.emplace<TagComponent>(made.entity, "WB Bar Button");
            auto& widget = registry.emplace<UIButtonComponent>(made.entity);
            widget.size = glm::vec2(kBarButtonWidth, kBarButtonHeight);
            widget.fontSize = kBarFontSize;
            registry.emplace<HierarchyComponent>(made.entity).parent = m_barStack;
            registry.emplace<UIOrderComponent>(made.entity).order = order++;
            m_bar.push_back(made);
        }
    }

    // And this runs every tick either way, which is the point of the split.
    for (const BarButton& button : m_bar) {
        auto& widget = registry.get<UIButtonComponent>(button.entity);
        widget.label = barLabel(button);
        widget.enabled = barEnabled(button);
    }
}

void WolfBrigadeLayer::applyBarClicks(entt::registry& registry) {
    if (!m_match) return;

    for (const BarButton& button : m_bar) {
        auto& widget = registry.get<UIButtonComponent>(button.entity);
        if (!widget.clickedThisTick) continue;

        // A disabled button still draws and still covers what is under it, but
        // it must not act. UIInput already refuses to mark it clicked; this is
        // the second lock, because a button that became unaffordable between
        // the press and the tick that consumes it would otherwise still fire.
        if (!widget.enabled) continue;

        switch (button.action) {
        case BarAction::Cancel:
            m_match->Placement().Cancel();
            break;
        case BarAction::Build:
            m_match->Placement().Begin(button.id);
            break;
        case BarAction::Train: {
            Building* selected = m_match->Picked().SelectedBuilding();
            if (selected == nullptr || !selected->IsComplete()) break;
            // Paid here, enqueued after - Building::EnqueueTraining says it
            // does not check the cost because whoever enqueues has paid.
            if (m_match->Run().TrySpend(toCost(m_data->Unit(button.id)["cost"]))) {
                selected->EnqueueTraining(button.id);
            }
            break;
        }
        case BarAction::Research: {
            std::vector<Building*> existing;
            existing.reserve(m_match->Buildings().size());
            for (const auto& building : m_match->Buildings()) existing.push_back(building.get());
            Upgrades::Research(*m_data, m_match->Run(), button.id, existing);
            break;
        }
        }
    }
}

void WolfBrigadeLayer::OnDetach(entt::registry& registry) {
    // The match owns nothing in the registry, and the registry owns nothing in
    // the match. Dropping them in this order is not load-bearing; saying so is,
    // because the next person will look for a dependency that is not there.
    (void)registry;
    m_match.reset();
    m_profile.reset();
    m_data.reset();
    m_booted = false;
}

entt::entity WolfBrigadeLayer::claim(entt::registry& registry, std::size_t& cursor,
                                     const glm::vec2& simPosition, const glm::vec2& simSize,
                                     const glm::vec3& colour, int32_t layer) {
    if (cursor >= m_pool.size()) {
        Quad made;
        made.entity = registry.create();
        registry.emplace<TagComponent>(made.entity, "WB Quad");
        registry.emplace<TransformComponent>(made.entity);
        registry.emplace<MeshComponent>(made.entity).primitiveType = "Quad";
        auto& material = registry.emplace<MaterialComponent>(made.entity);
        material.unlit = true;
        registry.emplace<RenderableComponent>(made.entity);
        m_pool.push_back(made);
    }

    Quad& quad = m_pool[cursor++];
    quad.live = true;

    auto& transform = registry.get<TransformComponent>(quad.entity);
    transform.position = toWorld(simPosition, 0.0f);
    transform.scale = glm::vec3(std::max(simSize.x, 1.0f) / kPixelsPerUnit,
                                std::max(simSize.y, 1.0f) / kPixelsPerUnit,
                                1.0f);

    auto& material = registry.get<MaterialComponent>(quad.entity);
    material.albedoColor = glm::vec4(colour, 1.0f);

    auto& renderable = registry.get<RenderableComponent>(quad.entity);
    renderable.sortKey = layer;
    renderable.isVisible = true;

    return quad.entity;
}

void WolfBrigadeLayer::retire(entt::registry& registry, std::size_t used) {
    // HIDDEN, not destroyed. A wave dying is the ordinary case, and destroying
    // forty entities on the tick it happens would recycle their indices - which
    // the state hash is seeded on, so a run would stop comparing against its own
    // recording for a reason that is about drawing.
    for (std::size_t i = used; i < m_pool.size(); ++i) {
        if (!m_pool[i].live) continue;
        registry.get<RenderableComponent>(m_pool[i].entity).isVisible = false;
        m_pool[i].live = false;
    }
}

void WolfBrigadeLayer::OnFixedUpdate(entt::registry& registry, float fixedDelta) {
    if (!m_booted || !m_match) return;

    updateHud(registry);

    // Read what the player pressed BEFORE the match steps, so an order given
    // this tick takes effect this tick rather than one later, and then make the
    // strip agree with whatever that changed.
    applyBarClicks(registry);
    updateBar(registry);

    if (m_paused) return;

    m_match->Step(static_cast<double>(fixedDelta));

    const Snapshot::Scene scene = m_match->View();
    std::size_t cursor = 0;

    for (const ResourceNode* node : scene.resourceNodes) {
        if (!node) continue;
        claim(registry, cursor, node->position, node->bodySize,
              parseHex(node->color, glm::vec3(0.25f, 0.42f, 0.20f)), kLayerNode);
    }

    for (const Building* building : scene.buildings) {
        if (!building || !building->IsAlive()) continue;
        const BuildingStats& stats = building->Stats();

        // A building under construction is drawn dimmer rather than differently,
        // which is what the original does with modulate.
        glm::vec3 colour = parseHex(stats.color, glm::vec3(0.23f, 0.44f, 0.63f));
        if (!building->IsComplete()) colour *= 0.55f;

        claim(registry, cursor, building->Position(), stats.bodySize, colour, kLayerBuilding);
    }

    for (const Unit* unit : scene.units) {
        if (!unit || !unit->IsAlive()) continue;
        const UnitStats& stats = unit->Stats();
        const glm::vec2 position = unit->Position();

        claim(registry, cursor, position, stats.bodySize,
              parseHex(stats.color, glm::vec3(0.8f)), kLayerUnit);

        // The health bar, which is the one piece of the original's unit scene
        // that carries information rather than identity. Forty by six pixels
        // above the body, exactly as unit.gd places it.
        const float barY = position.y - stats.bodySize.y * 0.5f - 10.0f;
        const glm::vec2 barSize(40.0f, 6.0f);
        claim(registry, cursor, glm::vec2(position.x, barY), barSize,
              glm::vec3(0.09f, 0.09f, 0.11f), kLayerBarBack);

        const float fraction = stats.maxHp > 0
                                   ? std::clamp(static_cast<float>(unit->Hp()) /
                                                    static_cast<float>(stats.maxHp), 0.0f, 1.0f)
                                   : 1.0f;
        if (fraction > 0.0f) {
            // Anchored left rather than centred, so a bar at half health empties
            // from one end instead of shrinking towards its middle.
            const float filled = barSize.x * fraction;
            const float left = position.x - barSize.x * 0.5f + filled * 0.5f;
            claim(registry, cursor, glm::vec2(left, barY), glm::vec2(filled, barSize.y),
                  unit->IsPlayer() ? glm::vec3(0.35f, 0.78f, 0.35f)
                                   : glm::vec3(0.82f, 0.31f, 0.27f),
                  kLayerBarFill);
        }
    }

    retire(registry, cursor);
}

} // namespace WolfBrigade
