#include "WolfBrigadeLayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>

#include "core/Application.hpp"
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

WolfBrigadeLayer::WolfBrigadeLayer(std::string saveDir) : m_saveDir(std::move(saveDir)) {
    if (m_saveDir.empty()) return;

    // The original's own filenames, so a player's save reads the same either
    // side of the port. Built here rather than at each use so there is one
    // place that decides, and so a test can see them by constructing a layer.
    const std::filesystem::path directory(m_saveDir);
    m_profilePath = (directory / "wolf_brigade_save.json").string();
    m_runPath = (directory / "wolf_brigade_run.json").string();
}

void WolfBrigadeLayer::applySavedRules() {
    if (!m_match || !m_profile || !m_data) return;

    // `main_menu.gd:32-33` writing the chosen mode and difficulty into
    // GameState, which in Godot survives the scene change so `main._ready`
    // finds it. Here the GameState is built WITH the Match, so every place that
    // builds one has to push them in - and this is that one place, because
    // there are three such sites and the third is the one that gets forgotten.
    //
    // BEFORE Boot, and the order is the whole of whether it takes effect:
    // GameState::Reset scales the opening resources by the difficulty and
    // WaveDirector::Setup reads IsEndless to pick the schedule. Applied after,
    // it gives a run that says "hard" and was dealt an easy hand.
    //
    // The fallbacks are the caller's, as everywhere: never chosen means the
    // difficulty the DATA declares, not one this layer invented.
    m_match->Run().SetMode(m_profile->Mode(GameState::kCampaign));
    m_match->Run().SetDifficulty(m_profile->Difficulty(m_data->DifficultyDefault()));
}

void WolfBrigadeLayer::saveProfileIfDirty() {
    if (!m_profile || m_profilePath.empty() || !m_profile->IsDirty()) return;

    if (m_profile->Save(m_profilePath)) {
        m_reportedSaveFailure = false;
        return;
    }

    // ONCE. A directory that refuses writes would otherwise print a line every
    // tick for the rest of the session, and the profile stays dirty either way
    // so the next tick tries again.
    if (!m_reportedSaveFailure) {
        m_reportedSaveFailure = true;
        SUPERSONIC_LOG_ERROR("WolfBrigade")
            << "could not write the profile to " << m_profilePath
            << "; progress will not persist." << std::endl;
    }
}

void WolfBrigadeLayer::OnAttach(entt::registry& registry) {
    m_data = std::make_unique<GameData>();
    if (!m_data->LoadAll(WOLFBRIGADE_DATA_DIR)) {
        SUPERSONIC_LOG_ERROR("WolfBrigade")
            << "could not load the game data from " << WOLFBRIGADE_DATA_DIR
            << "; the lane will be empty." << std::endl;
        return;
    }

    m_profile = std::make_unique<Profile>();

    // A profile that is not there is a new player, not a failure, so the return
    // value is deliberately not checked - see Profile::Load. What matters is
    // that this happens BEFORE the match boots: GameState::Reset reads the
    // owned meta levels to add Deeper Coffers to the opening balance, and a
    // match booted over an unloaded profile would silently start a returning
    // player at a new player's resources.
    if (!m_profilePath.empty()) m_profile->Load(m_profilePath);

    m_match = std::make_unique<Match>(*m_data, *m_profile, m_runPath);
    applySavedRules();
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
    buildPauseMenu(registry);
    buildGameOver(registry);
    buildMenu(registry);
    buildArmory(registry);
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
    // Refused outright while a result is up, rather than toggled and undone
    // later: a finished game is not pausable, and that is a rule about the
    // press rather than about the menu.
    if (pause.clickedThisTick && !m_showingResult) m_paused = !m_paused;
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

void WolfBrigadeLayer::buildPauseMenu(entt::registry& registry) {
    // Layer 9, which is pause_menu.tscn's own number. Everything in the overlay
    // carries it, and nothing else in this game does - so the whole menu sits
    // above the HUD and the bar without either of them knowing.
    constexpr int32_t kPauseLayer = 9;

    m_pause.backdrop = registry.create();
    registry.emplace<TagComponent>(m_pause.backdrop, "Pause Backdrop");
    auto& dim = registry.emplace<UIPanelComponent>(m_pause.backdrop);
    dim.anchor = UIAnchor::Center;
    dim.offset = glm::vec2(0.0f, 0.0f);
    dim.fillWidth = true;
    dim.fillHeight = true;
    dim.cornerRadius = 0.0f;
    dim.color = glm::vec4(0.05f, 0.06f, 0.09f, 0.86f);
    registry.emplace<UIOrderComponent>(m_pause.backdrop).layer = kPauseLayer;

    m_pause.column = registry.create();
    registry.emplace<TagComponent>(m_pause.column, "Pause Menu");
    auto& column = registry.emplace<UIStackComponent>(m_pause.column);
    column.horizontal = false;
    column.anchor = UIAnchor::Center;
    column.spacing = 18.0f;

    int32_t order = 0;
    auto title = registry.create();
    registry.emplace<TagComponent>(title, "Pause Title");
    auto& heading = registry.emplace<UITextComponent>(title);
    heading.text = "PAUSED";
    heading.fontSize = 88.0f;
    registry.emplace<HierarchyComponent>(title).parent = m_pause.column;
    {
        auto& ordering = registry.emplace<UIOrderComponent>(title);
        ordering.order = order++;
        ordering.layer = kPauseLayer;
    }

    // The title is IN the column rather than at its own fixed offset above it,
    // which is a small departure from the original's layout and a deliberate
    // one: a stack that measures its own contents keeps the heading and the
    // buttons together at any font size, where two independent anchors drift
    // apart the moment either changes.
    auto item = [&](const char* tag, const char* label) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& button = registry.emplace<UIButtonComponent>(entity);
        button.label = label;
        button.size = glm::vec2(440.0f, 96.0f);
        button.fontSize = 34.0f;
        registry.emplace<HierarchyComponent>(entity).parent = m_pause.column;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = order++;
        ordering.layer = kPauseLayer;
        return entity;
    };

    m_pause.resume = item("Pause Resume", "Resume");
    m_pause.restart = item("Pause Restart", "Restart");
    m_pause.mainMenu = item("Pause Main Menu", "Main Menu");
    m_pause.quit = item("Pause Quit", "Quit");

    setPauseMenuVisible(registry, false);
}

// --- The Armory ------------------------------------------------------------

namespace {

// The order `meta.json` declares them in, which is NOT the order they come out
// of the data.
//
// `Supersonic::Json` holds an object as a std::map, so walking MetaUpgrades()
// gives deeper_coffers, fortified_halls, sharper_axes, veteran_soldiers - and
// the original shows sharper_axes, veteran_soldiers, deeper_coffers,
// fortified_halls, because a Godot Dictionary keeps insertion order.
//
// Written here rather than as an "order" array in the data, and that is a
// deliberate trade: `games/wolfbrigade/data/meta.json` is byte-identical with
// the oracle's copy, and so are difficulty, waves and units. Keeping the two
// games reading one file is worth more than the generality - and the difficulty
// row shows what the alternative looks like when the ORIGINAL needed it, which
// is an explicit "order" array the original itself carries.
//
// It is a display preference and not a filter: anything the data declares that
// is missing from this list is appended rather than dropped, so a fifth upgrade
// appears at the end instead of silently vanishing.
constexpr const char* kArmoryOrder[] = {
    "sharper_axes", "veteran_soldiers", "deeper_coffers", "fortified_halls",
};

} // namespace

void WolfBrigadeLayer::buildArmory(entt::registry& registry) {
    constexpr int32_t kArmoryLayer = 20;

    m_armory.backdrop = registry.create();
    registry.emplace<TagComponent>(m_armory.backdrop, "Armory Backdrop");
    auto& back = registry.emplace<UIPanelComponent>(m_armory.backdrop);
    back.anchor = UIAnchor::Center;
    back.offset = glm::vec2(0.0f, 0.0f);
    back.fillWidth = true;
    back.fillHeight = true;
    back.cornerRadius = 0.0f;
    back.color = glm::vec4(0.05f, 0.06f, 0.09f, 1.0f);
    registry.emplace<UIOrderComponent>(m_armory.backdrop).layer = kArmoryLayer;

    m_armory.column = registry.create();
    registry.emplace<TagComponent>(m_armory.column, "Armory");
    auto& column = registry.emplace<UIStackComponent>(m_armory.column);
    column.horizontal = false;
    column.anchor = UIAnchor::Center;
    column.spacing = 18.0f;

    int32_t order = 0;
    auto label = [&](entt::entity parent, const std::string& tag, const std::string& text,
                     float fontSize, const glm::vec4& colour, int32_t& counter) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& item = registry.emplace<UITextComponent>(entity);
        item.text = text;
        item.fontSize = fontSize;
        item.color = colour;
        registry.emplace<HierarchyComponent>(entity).parent = parent;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = counter++;
        ordering.layer = kArmoryLayer;
        return entity;
    };

    label(m_armory.column, "Armory Title", "ARMORY", 80.0f, glm::vec4(1.0f), order);
    m_armory.renown = label(m_armory.column, "Armory Renown", "Renown: 0", 40.0f,
                            glm::vec4(0.95f, 0.83f, 0.4f, 1.0f), order);
    label(m_armory.column, "Armory Hint",
          "Permanent upgrades - they apply to every run.", 24.0f,
          glm::vec4(0.62f, 0.68f, 0.78f, 1.0f), order);

    // The upgrades the data declares, in the original's order, with anything it
    // does not name appended - see kArmoryOrder.
    std::vector<std::string> ids;
    for (const char* id : kArmoryOrder) {
        if (m_data->MetaUpgrade(id).IsObject() && !m_data->MetaUpgrade(id).AsObject().empty()) {
            ids.emplace_back(id);
        }
    }
    for (const auto& [id, definition] : m_data->MetaUpgrades().AsObject()) {
        (void)definition;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }

    // NO SCROLL VIEW, and it is worth saying why rather than leaving it as an
    // omission. The original wraps this list in a ScrollContainer that never
    // scrolls: four rows at a hundred pixels plus fourteen of separation is 442
    // in a 560 viewport. A fifth upgrade would be the first to overflow, and
    // the honest place to build one is the commit that authors it.
    for (const std::string& id : ids) {
        const Supersonic::Json::Value& definition = m_data->MetaUpgrade(id);

        const entt::entity row = registry.create();
        registry.emplace<TagComponent>(row, "Armory Row " + id);
        auto& rowStack = registry.emplace<UIStackComponent>(row);
        rowStack.horizontal = true;
        rowStack.spacing = 24.0f;
        registry.emplace<HierarchyComponent>(row).parent = m_armory.column;
        {
            auto& ordering = registry.emplace<UIOrderComponent>(row);
            ordering.order = order++;
            ordering.layer = kArmoryLayer;
        }

        const entt::entity info = registry.create();
        registry.emplace<TagComponent>(info, "Armory Info " + id);
        auto& infoStack = registry.emplace<UIStackComponent>(info);
        infoStack.horizontal = false;
        infoStack.spacing = 4.0f;
        registry.emplace<HierarchyComponent>(info).parent = row;
        {
            auto& ordering = registry.emplace<UIOrderComponent>(info);
            ordering.order = 0;
            ordering.layer = kArmoryLayer;
        }

        // A stack nested in a stack nested in a stack - info inside the row
        // inside the column - which is the deepest this game goes and what
        // `layoutStacksImpl`'s recursive measure-and-place is for.
        int32_t infoOrder = 0;
        ArmoryRow built;
        built.id = id;
        built.title = label(info, "Armory Level " + id, "", 30.0f, glm::vec4(1.0f), infoOrder);
        label(info, "Armory Desc " + id, definition["description"].AsString(""), 22.0f,
              glm::vec4(0.62f, 0.68f, 0.78f, 1.0f), infoOrder);

        built.buy = registry.create();
        registry.emplace<TagComponent>(built.buy, "Armory Buy " + id);
        auto& buy = registry.emplace<UIButtonComponent>(built.buy);
        buy.size = glm::vec2(320.0f, 100.0f);
        buy.fontSize = 24.0f;
        registry.emplace<HierarchyComponent>(built.buy).parent = row;
        {
            auto& ordering = registry.emplace<UIOrderComponent>(built.buy);
            ordering.order = 1;
            ordering.layer = kArmoryLayer;
        }

        m_armory.rows.push_back(built);
    }

    m_armory.back = registry.create();
    registry.emplace<TagComponent>(m_armory.back, "Armory Back");
    auto& backButton = registry.emplace<UIButtonComponent>(m_armory.back);
    backButton.label = "Back";
    backButton.size = glm::vec2(440.0f, 104.0f);
    backButton.fontSize = 40.0f;
    registry.emplace<HierarchyComponent>(m_armory.back).parent = m_armory.column;
    {
        auto& ordering = registry.emplace<UIOrderComponent>(m_armory.back);
        ordering.order = order++;
        ordering.layer = kArmoryLayer;
    }

    setArmoryVisible(registry, false);
}

void WolfBrigadeLayer::setArmoryVisible(entt::registry& registry, bool shown) {
    if (!registry.valid(m_armory.column)) return;
    registry.get<UIStackComponent>(m_armory.column).visible = shown;
    registry.get<UIPanelComponent>(m_armory.backdrop).visible = shown;
}

void WolfBrigadeLayer::updateArmory(entt::registry& registry) {
    if (m_screen != Screen::Armory || !registry.valid(m_armory.column)) return;
    if (!m_profile || !m_data) return;

    if (registry.get<UIButtonComponent>(m_armory.back).clickedThisTick) {
        goTo(registry, Screen::Menu);
        return;
    }

    // READ EVERY ROW'S CLICK BEFORE WRITING ANY ROW'S TEXT. A purchase changes
    // the renown balance, which changes what every OTHER row can afford - so
    // buying and then repainting in one pass would leave the rows above the
    // bought one describing a balance that no longer exists until the next
    // tick.
    for (const ArmoryRow& row : m_armory.rows) {
        if (!registry.get<UIButtonComponent>(row.buy).clickedThisTick) continue;

        // Refused twice, as the bottom bar's buttons are: once by the disabled
        // flag UIInput honours, and again here by Meta::Buy re-checking cost
        // and cap. The second is what covers a button that went unaffordable
        // between the press and the tick that reads it.
        Meta::Buy(*m_data, *m_profile, row.id);
    }

    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "Renown: %d", m_profile->Renown());
    registry.get<UITextComponent>(m_armory.renown).text = buffer;

    for (const ArmoryRow& row : m_armory.rows) {
        const std::string name =
            m_data->MetaUpgrade(row.id)["display_name"].AsString(row.id);
        std::snprintf(buffer, sizeof(buffer), "%s   -   Lv %d/%d", name.c_str(),
                      m_profile->MetaLevel(row.id), Meta::MaxLevel(*m_data, row.id));
        registry.get<UITextComponent>(row.title).text = buffer;

        auto& buy = registry.get<UIButtonComponent>(row.buy);
        if (Meta::IsMaxed(*m_data, *m_profile, row.id)) {
            buy.label = "MAX";
            buy.enabled = false;
        } else {
            std::snprintf(buffer, sizeof(buffer), "Buy\n(%d renown)",
                          Meta::NextCost(*m_data, *m_profile, row.id));
            buy.label = buffer;
            buy.enabled = Meta::CanBuy(*m_data, *m_profile, row.id);
        }
    }
}

// --- The main menu ---------------------------------------------------------

void WolfBrigadeLayer::buildMenu(entt::registry& registry) {
    // Above the result overlay's 10, and above it unconditionally - see the
    // header. Everything on this screen carries it.
    constexpr int32_t kMenuLayer = 20;

    // Every size and font here is `main_menu.tscn`'s own number, unchanged,
    // for the same reason the HUD's are: the original authors in 1080-tall
    // pixels and so does UICanvas, which makes the two screens comparable by
    // eye. That is the only check this port has for a layout.
    m_menu.backdrop = registry.create();
    registry.emplace<TagComponent>(m_menu.backdrop, "Menu Backdrop");
    auto& back = registry.emplace<UIPanelComponent>(m_menu.backdrop);
    back.anchor = UIAnchor::Center;
    back.offset = glm::vec2(0.0f, 0.0f);
    back.fillWidth = true;
    back.fillHeight = true;
    back.cornerRadius = 0.0f;

    // OPAQUE, where the pause menu's backdrop is a dim. The pause menu is a
    // modal over a match you can still see; this is a different screen, and a
    // menu you can see the lane through would say the run is still there.
    back.color = glm::vec4(0.05f, 0.06f, 0.09f, 1.0f);
    registry.emplace<UIOrderComponent>(m_menu.backdrop).layer = kMenuLayer;

    m_menu.column = registry.create();
    registry.emplace<TagComponent>(m_menu.column, "Main Menu");
    auto& column = registry.emplace<UIStackComponent>(m_menu.column);
    column.horizontal = false;
    column.anchor = UIAnchor::Center;
    column.spacing = 20.0f;   // theme_override_constants/separation

    int32_t order = 0;
    auto label = [&](const char* tag, const char* text, float fontSize,
                     const glm::vec4& colour) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& item = registry.emplace<UITextComponent>(entity);
        item.text = text;
        item.fontSize = fontSize;
        item.color = colour;
        registry.emplace<HierarchyComponent>(entity).parent = m_menu.column;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = order++;
        ordering.layer = kMenuLayer;
        return entity;
    };

    auto button = [&](const char* tag, const char* text) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& item = registry.emplace<UIButtonComponent>(entity);
        item.label = text;
        item.size = glm::vec2(440.0f, 104.0f);
        item.fontSize = 40.0f;
        registry.emplace<HierarchyComponent>(entity).parent = m_menu.column;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = order++;
        ordering.layer = kMenuLayer;
        return entity;
    };

    label("Menu Title", "WOLF BRIGADE", 104.0f, glm::vec4(1.0f));
    label("Menu Subtitle", "Lane RTS", 30.0f, glm::vec4(0.62f, 0.68f, 0.78f, 1.0f));

    // The two radio rows. Each is an HBoxContainer in the original and a
    // NESTED horizontal stack here - a stack measured from its own contents and
    // placed in its parent's slot, which is what `layoutStacksImpl` does.
    const glm::vec4 rowLabelColour(0.62f, 0.68f, 0.78f, 1.0f);
    auto radioRow = [&](const char* rowTag, const char* headingTag, const char* heading) {
        label(headingTag, heading, 26.0f, rowLabelColour);

        const entt::entity row = registry.create();
        registry.emplace<TagComponent>(row, rowTag);
        auto& stack = registry.emplace<UIStackComponent>(row);
        stack.horizontal = true;
        stack.spacing = 16.0f;   // theme_override_constants/separation
        registry.emplace<HierarchyComponent>(row).parent = m_menu.column;
        auto& ordering = registry.emplace<UIOrderComponent>(row);
        ordering.order = order++;
        ordering.layer = kMenuLayer;
        return row;
    };

    // A radio option. The COLOURS are not set here: paintRadios writes all
    // three of them every tick, and a default written once would be overwritten
    // on the first one anyway.
    int32_t optionOrder = 0;
    auto radio = [&](entt::entity row, const std::string& tag, const std::string& text,
                     const std::string& id, const glm::vec2& size) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& item = registry.emplace<UIButtonComponent>(entity);
        item.label = text;
        item.size = size;
        item.fontSize = 30.0f;
        registry.emplace<HierarchyComponent>(entity).parent = row;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = optionOrder++;
        ordering.layer = kMenuLayer;
        return Radio{entity, id};
    };

    m_menu.modeRow = radioRow("Menu Mode Row", "Menu Mode Label", "Mode");
    optionOrder = 0;
    m_menu.modes.push_back(radio(m_menu.modeRow, "Menu Mode Campaign", "Campaign",
                                 GameState::kCampaign, glm::vec2(230.0f, 96.0f)));
    m_menu.modes.push_back(radio(m_menu.modeRow, "Menu Mode Endless", "Endless",
                                 GameState::kEndless, glm::vec2(230.0f, 96.0f)));

    // IN DATA ORDER, from `difficulty.json`'s own "order" array, which is what
    // `DataLoader.difficulty_order()` is for. Iterating the presets object
    // instead would give a std::map's alphabetical order - easy, hard, normal -
    // and put Hard in the middle of the row.
    m_menu.difficultyRow = radioRow("Menu Difficulty Row", "Menu Difficulty Label",
                                    "Difficulty");
    optionOrder = 0;
    for (const auto& entry : m_data->DifficultyOrder()) {
        const std::string id = entry.AsString();
        if (id.empty()) continue;
        const std::string display =
            m_data->DifficultyPreset(id)["display_name"].AsString(id);
        m_menu.difficulties.push_back(
            radio(m_menu.difficultyRow, "Menu Difficulty " + display, display, id,
                  glm::vec2(150.0f, 96.0f)));
    }

    m_menu.best = label("Menu Best", "Best: no runs yet", 24.0f,
                        glm::vec4(0.55f, 0.61f, 0.72f, 1.0f));
    m_menu.renown = label("Menu Renown", "Renown: 0", 26.0f,
                          glm::vec4(0.95f, 0.83f, 0.4f, 1.0f));

    m_menu.resume = button("Menu Continue", "Continue");
    m_menu.newGame = button("Menu New Game", "New Game");
    m_menu.armory = button("Menu Armory", "Armory");
    m_menu.settings = button("Menu Settings", "Settings");
    m_menu.quit = button("Menu Quit", "Quit");

    // ONE BUTTON THAT CANNOT WORK YET, greyed rather than left out, which is
    // what this game does everywhere else. Settings needs a screen of its own,
    // and behind it the audio state that gives its volume and mute controls
    // something to move.
    registry.get<UIButtonComponent>(m_menu.settings).enabled = false;

    // --- The New Game confirmation -----------------------------------------
    //
    // `main_menu.gd:71-72` says why it exists: New Game silently wiping an
    // in-progress run is a footgun. It is a screen over a screen - the original
    // hides $Center and $SoundButton while it is up, so that keyboard focus is
    // TRAPPED rather than merely covered.
    //
    // Layer 21, one above the menu it covers, for the same reason the menu is
    // above the result: the ordering has to be true on its own rather than
    // because of what happens to be on screen.
    constexpr int32_t kConfirmLayer = kMenuLayer + 1;

    m_menu.confirmBackdrop = registry.create();
    registry.emplace<TagComponent>(m_menu.confirmBackdrop, "Menu Confirm Backdrop");
    auto& dim = registry.emplace<UIPanelComponent>(m_menu.confirmBackdrop);
    dim.anchor = UIAnchor::Center;
    dim.offset = glm::vec2(0.0f, 0.0f);
    dim.fillWidth = true;
    dim.fillHeight = true;
    dim.cornerRadius = 0.0f;
    dim.color = glm::vec4(0.05f, 0.06f, 0.09f, 0.86f);
    registry.emplace<UIOrderComponent>(m_menu.confirmBackdrop).layer = kConfirmLayer;

    m_menu.confirmColumn = registry.create();
    registry.emplace<TagComponent>(m_menu.confirmColumn, "Menu Confirm");
    auto& confirm = registry.emplace<UIStackComponent>(m_menu.confirmColumn);
    confirm.horizontal = false;
    confirm.anchor = UIAnchor::Center;
    confirm.spacing = 24.0f;

    int32_t confirmOrder = 0;
    {
        const entt::entity question = registry.create();
        registry.emplace<TagComponent>(question, "Menu Confirm Question");
        auto& text = registry.emplace<UITextComponent>(question);
        text.text = "Start a new game?\nThe run in progress will be lost.";
        text.fontSize = 34.0f;
        registry.emplace<HierarchyComponent>(question).parent = m_menu.confirmColumn;
        auto& ordering = registry.emplace<UIOrderComponent>(question);
        ordering.order = confirmOrder++;
        ordering.layer = kConfirmLayer;
    }

    // A ROW NESTED IN THE COLUMN, as the result overlay's buttons are: Yes and
    // No sit side by side under the question.
    const entt::entity confirmRow = registry.create();
    registry.emplace<TagComponent>(confirmRow, "Menu Confirm Row");
    auto& rowStack = registry.emplace<UIStackComponent>(confirmRow);
    rowStack.horizontal = true;
    rowStack.spacing = 24.0f;
    registry.emplace<HierarchyComponent>(confirmRow).parent = m_menu.confirmColumn;
    {
        auto& ordering = registry.emplace<UIOrderComponent>(confirmRow);
        ordering.order = confirmOrder++;
        ordering.layer = kConfirmLayer;
    }

    int32_t answerOrder = 0;
    auto answer = [&](const char* tag, const char* text) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& item = registry.emplace<UIButtonComponent>(entity);
        item.label = text;
        item.size = glm::vec2(220.0f, 96.0f);
        item.fontSize = 34.0f;
        registry.emplace<HierarchyComponent>(entity).parent = confirmRow;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = answerOrder++;
        ordering.layer = kConfirmLayer;
        return entity;
    };

    m_menu.confirmYes = answer("Menu Confirm Yes", "New Game");
    m_menu.confirmNo = answer("Menu Confirm No", "Cancel");

    setConfirmVisible(registry, false);
    setMenuVisible(registry, false);
}

void WolfBrigadeLayer::paintRadios(entt::registry& registry, const std::vector<Radio>& row,
                                   const std::string& selected) const {
    // The unselected look is the button default; the selected one is the
    // engine's own hover colour raised, so a chosen option reads as lit rather
    // than as a different kind of control.
    constexpr glm::vec4 kIdle(0.16f, 0.17f, 0.21f, 0.96f);
    constexpr glm::vec4 kIdleHover(0.24f, 0.26f, 0.32f, 0.98f);
    constexpr glm::vec4 kIdlePress(0.10f, 0.11f, 0.14f, 1.0f);
    constexpr glm::vec4 kChosen(0.27f, 0.42f, 0.60f, 1.0f);
    constexpr glm::vec4 kChosenHover(0.33f, 0.50f, 0.70f, 1.0f);
    constexpr glm::vec4 kChosenPress(0.22f, 0.35f, 0.52f, 1.0f);

    for (const Radio& option : row) {
        if (!registry.valid(option.entity)) continue;
        auto& button = registry.get<UIButtonComponent>(option.entity);
        const bool chosen = option.id == selected;

        // ALL THREE. UISystem picks the fill fresh each frame in the order
        // disabled, pressed, hovered, colour - so setting only `color` gives a
        // selection that disappears under the pointer, on the one button the
        // player is most likely to be pointing at.
        button.color = chosen ? kChosen : kIdle;
        button.hoverColor = chosen ? kChosenHover : kIdleHover;
        button.pressColor = chosen ? kChosenPress : kIdlePress;
    }
}

void WolfBrigadeLayer::setConfirmVisible(entt::registry& registry, bool shown) {
    m_confirmingNewGame = shown;

    // Re-applied through the ONE function that owns this screen's visibility,
    // rather than written here. Two setters both assigning the menu column's
    // `visible` is two answers to one question, and whichever ran last would
    // win - so opening the confirmation and then re-entering the screen would
    // show the menu underneath it.
    setMenuVisible(registry, m_screen == Screen::Menu);
}

void WolfBrigadeLayer::refreshMenu(entt::registry& registry) {
    if (!registry.valid(m_menu.column) || !m_profile) return;

    char buffer[96];
    const int best = m_profile->BestWave();
    if (best > 0) {
        std::snprintf(buffer, sizeof(buffer), "Best: wave %d", best);
    } else {
        std::snprintf(buffer, sizeof(buffer), "Best: no runs yet");
    }
    registry.get<UITextComponent>(m_menu.best).text = buffer;

    std::snprintf(buffer, sizeof(buffer), "Renown: %d", m_profile->Renown());
    registry.get<UITextComponent>(m_menu.renown).text = buffer;

    // CONTINUE IS SHOWN ONLY WHEN THERE IS A RUN TO CONTINUE, and validity is
    // asked of the document rather than of the file's mere existence -
    // `main_menu.gd:40` calls Snapshot.is_valid, and `Snapshot::LoadRun`
    // deliberately does not validate. A run written by an older build parses
    // and must not be offered.
    const bool resumable =
        !m_runPath.empty() && Snapshot::IsValid(Snapshot::LoadRun(m_runPath));
    registry.get<UIButtonComponent>(m_menu.resume).visible = resumable;

    // PAINTED ON THE WAY IN, not only from the tick loop. updateMenu runs
    // BEFORE the transition that arrives here - the pause menu is read later in
    // the same tick - so a row painted only there is unpainted for the first
    // frame the screen is visible, and the player sees a menu with nothing
    // selected before it corrects itself.
    if (m_data) {
        paintRadios(registry, m_menu.modes, m_profile->Mode(GameState::kCampaign));
        paintRadios(registry, m_menu.difficulties,
                    m_profile->Difficulty(m_data->DifficultyDefault()));
    }

    // A confirmation left up from the last visit is not a state to arrive in.
    setConfirmVisible(registry, false);
}

void WolfBrigadeLayer::setMenuVisible(entt::registry& registry, bool shown) {
    if (!registry.valid(m_menu.column)) return;

    // ONE PLACE decides what this screen shows, from `shown` and one flag,
    // because the menu and the confirmation both want to write the column's
    // visibility and the last writer would win.
    //
    // The backdrop stays up under the confirmation: it is the screen, and what
    // the confirmation covers is the menu on it rather than the screen itself.
    registry.get<UIPanelComponent>(m_menu.backdrop).visible = shown;
    registry.get<UIStackComponent>(m_menu.column).visible = shown && !m_confirmingNewGame;

    if (registry.valid(m_menu.confirmColumn)) {
        const bool confirming = shown && m_confirmingNewGame;
        registry.get<UIStackComponent>(m_menu.confirmColumn).visible = confirming;
        registry.get<UIPanelComponent>(m_menu.confirmBackdrop).visible = confirming;
    }
}

void WolfBrigadeLayer::setMatchVisible(entt::registry& registry, bool shown) {
    if (registry.valid(m_hud.wood)) {
        for (const entt::entity entity : { m_hud.wood, m_hud.food, m_hud.wave }) {
            registry.get<UITextComponent>(entity).visible = shown;
        }
        registry.get<UIButtonComponent>(m_hud.pause).visible = shown;
    }

    if (registry.valid(m_barStack)) {
        registry.get<UIStackComponent>(m_barStack).visible = shown;
    }

    // AND THE LANE. The quad pool is not UI - it is entities with a
    // RenderableComponent - so nothing about hiding a UI stack reaches it, and
    // without this the last frame of the match stays drawn behind an opaque
    // menu that happens to cover it. "Happens to" is the problem: the backdrop
    // is what would be hiding it, and that is not a thing to depend on.
    for (Quad& quad : m_pool) {
        if (!registry.valid(quad.entity)) continue;
        registry.get<RenderableComponent>(quad.entity).isVisible = shown && quad.live;
    }
}

void WolfBrigadeLayer::goTo(entt::registry& registry, Screen next) {
    // BOTH modal states, at every transition. The original throws the whole
    // scene away and gets this for free; here they are fields, and a
    // transition that cleared only one is the same class of bug as the pause
    // menu that could be raised over a finished game.
    m_paused = false;
    if (m_showingResult) {
        m_showingResult = false;
        if (registry.valid(m_over.column)) {
            registry.get<UIStackComponent>(m_over.column).visible = false;
            registry.get<UIPanelComponent>(m_over.backdrop).visible = false;
        }
    }
    setPauseMenuVisible(registry, false);

    m_screen = next;

    if (next == Screen::Menu) {
        // THE RUN IS SAVED ON THE WAY OUT, unconditionally, and the single call
        // covers both paths that arrive here. `pause_menu.gd:59-62` saves
        // before leaving a live game; `game_over_overlay.gd:53-54` does not,
        // because the run is over - and Match::AutosaveRun refuses a finished
        // game itself. So the branch the original writes twice is already
        // inside the thing being called.
        if (m_match) m_match->AutosaveRun();

        // Dropped, not paused. A match kept alive behind the menu would keep
        // its buildings and its wave director, and New Game would then have to
        // remember to replace it - which is the state the original cannot get
        // into because change_scene destroys the tree.
        m_match.reset();

        // The bar's buttons point at the match that just went away, exactly as
        // in a Restart, and for the same reason are destroyed rather than
        // forgotten: a cleared vector leaves them drawn and clickable.
        for (const BarButton& old : m_bar) {
            if (registry.valid(old.entity)) registry.destroy(old.entity);
        }
        m_bar.clear();

        setMatchVisible(registry, false);
        setArmoryVisible(registry, false);
        refreshMenu(registry);
        setMenuVisible(registry, true);
        return;
    }

    if (next == Screen::Armory) {
        // No match to drop: you can only get here from the menu, which dropped
        // it on the way in. The Armory reads the Profile and nothing else.
        setMenuVisible(registry, false);
        setArmoryVisible(registry, true);
        return;
    }

    setArmoryVisible(registry, false);
    setMenuVisible(registry, false);
    setMatchVisible(registry, true);
}

void WolfBrigadeLayer::startGame(entt::registry& registry, bool fromSave) {
    if (!m_data || !m_profile) return;

    // Read BEFORE the match exists, because building one is what will clear it:
    // Match::OnGameOver clears the file, and a fresh boot over a stale document
    // is the case Boot's fork is for.
    const Supersonic::Json::Value pending =
        (fromSave && !m_runPath.empty()) ? Snapshot::LoadRun(m_runPath)
                                         : Supersonic::Json::Value{};

    // `_start_new_game`: starting fresh abandons any saved run. Done before the
    // Match is built rather than after, so a restore that is refused cannot
    // leave the file it was refused from lying around.
    if (!fromSave && !m_runPath.empty()) Snapshot::ClearRun(m_runPath);

    m_match = std::make_unique<Match>(*m_data, *m_profile, m_runPath);

    // UNCONDITIONAL, and it does not need a fork - which was worth finding out
    // rather than assuming. A restore path guard was written here first and a
    // mutation run could not make it fail: `GameState::FromSave`
    // (`GameState.cpp:150-151`) sets the difficulty and the mode from the saved
    // document, and it runs INSIDE Boot, after this. So a resumed run keeps the
    // rules it was played on whatever the menu currently shows, and the branch
    // was a second way of saying something the order already said.
    //
    // Kept before Boot rather than moved after it, because on the fresh fork it
    // is the only thing that sets them and Reset reads them.
    applySavedRules();

    // Boot takes the fork itself - a valid pending document restores, anything
    // else starts fresh - which is `main.gd::_ready`. Passing a null value on
    // the New Game path is therefore not a special case, it is the same call.
    m_match->Boot(pending);

    // A restore consumed the file it restored from. Leaving it would mean the
    // menu still offering to continue a run that is now the live one, and a
    // second Continue would rewind the player to where they resumed.
    if (fromSave && !m_runPath.empty()) Snapshot::ClearRun(m_runPath);

    goTo(registry, Screen::Match);
}

void WolfBrigadeLayer::updateMenu(entt::registry& registry) {
    if (m_screen != Screen::Menu || !registry.valid(m_menu.column) || !m_profile) return;

    // THE CONFIRMATION FIRST, and nothing else while it is up. The menu's own
    // buttons are hidden under it, so UIInput will not deliver them a click -
    // but this is the layer's half of the same refusal, and it is the half that
    // holds if the two ever disagree about what "hidden" means. The bar answers
    // the pause menu the same way.
    if (m_confirmingNewGame) {
        if (registry.get<UIButtonComponent>(m_menu.confirmYes).clickedThisTick) {
            startGame(registry, false);
        } else if (registry.get<UIButtonComponent>(m_menu.confirmNo).clickedThisTick) {
            setConfirmVisible(registry, false);
        }
        return;
    }

    // The radio rows, read before anything that leaves the screen so a mode
    // chosen and a New Game pressed in the same tick take effect in that order.
    //
    // BOTH HOMES, exactly as `_on_mode` and `_on_difficulty` write both: the
    // profile is where the choice persists, and it is pushed into the run's
    // GameState by startGame. Writing only the profile would remember a setting
    // that never reached a match; writing only the state would lose it on exit.
    for (const Radio& option : m_menu.modes) {
        if (registry.get<UIButtonComponent>(option.entity).clickedThisTick) {
            m_profile->SetMode(option.id);
        }
    }
    for (const Radio& option : m_menu.difficulties) {
        if (registry.get<UIButtonComponent>(option.entity).clickedThisTick) {
            m_profile->SetDifficulty(option.id);
        }
    }

    // Repainted every tick, which is what makes a selection a look rather than
    // a component field. Cheap: six buttons, three colours each.
    paintRadios(registry, m_menu.modes, m_profile->Mode(GameState::kCampaign));
    paintRadios(registry, m_menu.difficulties,
                m_profile->Difficulty(m_data->DifficultyDefault()));

    if (registry.get<UIButtonComponent>(m_menu.resume).clickedThisTick) {
        startGame(registry, true);
        return;
    }

    if (registry.get<UIButtonComponent>(m_menu.newGame).clickedThisTick) {
        // ASK FIRST when there is something to lose. `main_menu.gd:73-83`: with
        // no resumable run there is nothing to confirm, so it starts
        // immediately - a dialog that always appears is a dialog people learn
        // to dismiss without reading.
        const bool resumable =
            !m_runPath.empty() && Snapshot::IsValid(Snapshot::LoadRun(m_runPath));
        if (resumable) {
            setConfirmVisible(registry, true);
        } else {
            startGame(registry, false);
        }
        return;
    }

    if (registry.get<UIButtonComponent>(m_menu.armory).clickedThisTick) {
        goTo(registry, Screen::Armory);
        return;
    }

    if (registry.get<UIButtonComponent>(m_menu.quit).clickedThisTick) {
        Supersonic::Application::RequestQuit();
    }
}

void WolfBrigadeLayer::setPauseMenuVisible(entt::registry& registry, bool shown) {
    if (!registry.valid(m_pause.column)) return;
    registry.get<UIStackComponent>(m_pause.column).visible = shown;
    registry.get<UIPanelComponent>(m_pause.backdrop).visible = shown;
}

void WolfBrigadeLayer::restartMatch(entt::registry& registry) {
    if (!m_data || !m_profile) return;

    // A FRESH RESTART ABANDONS THE SAVED RUN, which is `pause_menu.gd:45` in
    // its own words. Nothing else would do it: Match clears the run file from
    // OnGameOver, and a restart is the path that does not end a run - so the
    // Continue the player just restarted out of would still be on the menu.
    //
    // Clearing a file that is already gone is a no-op, so the game-over
    // Restart - which arrives here having been cleared by OnGameOver already -
    // needs no branch of its own.
    if (!m_runPath.empty()) Snapshot::ClearRun(m_runPath);

    // THE SAME PATH the first match got, not an empty one. A Restart that built
    // a filesystem-free Match could never autosave or clear again.
    m_match = std::make_unique<Match>(*m_data, *m_profile, m_runPath);

    // A RESTART KEEPS THE SAME DIFFICULTY AND MODE, which `game_state.gd:22-31`
    // says twice in its own words: neither is cleared by reset(), because the
    // autoload survives the scene reload and only the run state is rebuilt.
    // Here the GameState goes with the Match, so keeping them is an act rather
    // than the default - without this a Restart on Hard silently drops to
    // whatever the data calls normal.
    applySavedRules();
    m_match->BootFresh();

    // The bar's buttons point at the OLD match's selection and buildings, so
    // they are DESTROYED rather than merely forgotten and the next updateBar
    // builds a fresh set against the new run.
    //
    // Clearing the vector alone was not enough, and the failure was quiet:
    // the entities stayed in the registry, still parented to the bar's stack
    // and still drawn and clickable, beside the new set. A restarted game grew
    // a second row of buttons wired to a match that no longer existed.
    for (const BarButton& old : m_bar) {
        if (registry.valid(old.entity)) registry.destroy(old.entity);
    }
    m_bar.clear();
}

void WolfBrigadeLayer::updatePauseMenu(entt::registry& registry) {
    if (!registry.valid(m_pause.column)) return;

    // Resume, and the Pause button in the HUD, are the same action from two
    // places. Both are read here so there is one place that decides.
    if (registry.get<UIButtonComponent>(m_pause.resume).clickedThisTick) {
        m_paused = false;
    }

    if (registry.get<UIButtonComponent>(m_pause.restart).clickedThisTick) {
        restartMatch(registry);
        m_paused = false;
    }

    // `pause_menu.gd:51-53`. The save it does first lives inside goTo, which is
    // the one place every transition passes through.
    if (registry.get<UIButtonComponent>(m_pause.mainMenu).clickedThisTick) {
        goTo(registry, Screen::Menu);
        return;
    }

    // Asked for rather than done. The run loop reads the latch at the top of
    // the next frame, so the shutdown happens with no tick half-finished and
    // no frame half-built around it - see Application.hpp.
    if (registry.get<UIButtonComponent>(m_pause.quit).clickedThisTick) {
        Supersonic::Application::RequestQuit();
    }

    setPauseMenuVisible(registry, m_paused);
}


void WolfBrigadeLayer::buildGameOver(entt::registry& registry) {
    // Ten, above the pause menu's nine. You cannot pause a finished game, so a
    // result that could be covered by something you can still open would be a
    // result you could hide from.
    constexpr int32_t kResultLayer = 10;

    m_over.backdrop = registry.create();
    registry.emplace<TagComponent>(m_over.backdrop, "Result Backdrop");
    auto& dim = registry.emplace<UIPanelComponent>(m_over.backdrop);
    dim.anchor = UIAnchor::Center;
    dim.offset = glm::vec2(0.0f, 0.0f);
    dim.fillWidth = true;
    dim.fillHeight = true;
    dim.cornerRadius = 0.0f;
    dim.color = glm::vec4(0.05f, 0.06f, 0.09f, 0.86f);
    registry.emplace<UIOrderComponent>(m_over.backdrop).layer = kResultLayer;

    m_over.column = registry.create();
    registry.emplace<TagComponent>(m_over.column, "Result Menu");
    auto& column = registry.emplace<UIStackComponent>(m_over.column);
    column.anchor = UIAnchor::Center;
    column.spacing = 40.0f;

    m_over.message = registry.create();
    registry.emplace<TagComponent>(m_over.message, "Result Message");
    auto& message = registry.emplace<UITextComponent>(m_over.message);
    message.text = "VICTORY";
    message.fontSize = 96.0f;
    registry.emplace<HierarchyComponent>(m_over.message).parent = m_over.column;
    {
        auto& ordering = registry.emplace<UIOrderComponent>(m_over.message);
        ordering.order = 0;
        ordering.layer = kResultLayer;
    }

    // A ROW inside the column, which is the nesting the layout pass was
    // rewritten for: two buttons side by side under a heading, measured as one
    // block and centred as one.
    m_over.row = registry.create();
    registry.emplace<TagComponent>(m_over.row, "Result Buttons");
    auto& row = registry.emplace<UIStackComponent>(m_over.row);
    row.horizontal = true;
    row.spacing = 40.0f;
    registry.emplace<HierarchyComponent>(m_over.row).parent = m_over.column;
    {
        auto& ordering = registry.emplace<UIOrderComponent>(m_over.row);
        ordering.order = 1;
        ordering.layer = kResultLayer;
    }

    auto item = [&](const char* tag, const char* label, int32_t order) {
        const entt::entity entity = registry.create();
        registry.emplace<TagComponent>(entity, tag);
        auto& button = registry.emplace<UIButtonComponent>(entity);
        button.label = label;
        button.size = glm::vec2(300.0f, 96.0f);
        button.fontSize = 36.0f;
        registry.emplace<HierarchyComponent>(entity).parent = m_over.row;
        auto& ordering = registry.emplace<UIOrderComponent>(entity);
        ordering.order = order;
        ordering.layer = kResultLayer;
        return entity;
    };

    m_over.restart = item("Result Restart", "Restart", 0);
    m_over.mainMenu = item("Result Main Menu", "Main Menu", 1);
    registry.get<UIButtonComponent>(m_over.mainMenu).enabled = false;

    registry.get<UIStackComponent>(m_over.column).visible = false;
    registry.get<UIPanelComponent>(m_over.backdrop).visible = false;
}

void WolfBrigadeLayer::updateGameOver(entt::registry& registry) {
    if (!m_match || !registry.valid(m_over.column)) return;

    const GameState& state = m_match->Run();
    const bool finished = !state.IsPlaying();

    // WRITTEN ONCE, on the transition. The wave is read before anything that
    // ends a run can reset it, exactly as game_over_overlay.gd does, and a
    // line recomputed every tick would be a different claim about the same run.
    if (finished && !m_showingResult) {
        const bool won = state.CurrentPhase() == GameState::Phase::Won;

        // Recomputed rather than captured from the award. Match banks the
        // renown itself and discards the amount, and RunEndRenown is pure - it
        // depends on the wave and the outcome and nothing else - so asking it
        // again gives the number that was banked without a second source of
        // truth for it.
        const int earned = Meta::RunEndRenown(*m_data, state.CurrentWave(), won);

        char buffer[160];
        if (earned <= 0) {
            std::snprintf(buffer, sizeof(buffer), "%s", won ? "VICTORY" : "DEFEAT");
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s\n+%d renown  (total %d)",
                          won ? "VICTORY" : "DEFEAT", earned, m_profile->Renown());
        }
        registry.get<UITextComponent>(m_over.message).text = buffer;

        registry.get<UIStackComponent>(m_over.column).visible = true;
        registry.get<UIPanelComponent>(m_over.backdrop).visible = true;
        m_showingResult = true;
    }

    if (m_showingResult && registry.get<UIButtonComponent>(m_over.restart).clickedThisTick) {
        restartMatch(registry);
        m_showingResult = false;
        m_paused = false;
        registry.get<UIStackComponent>(m_over.column).visible = false;
        registry.get<UIPanelComponent>(m_over.backdrop).visible = false;
        return;
    }

    // `game_over_overlay.gd:53-54`, which does NOT save on the way out - the
    // run is over and Meta::AwardRunEnd has already banked it. goTo calls
    // AutosaveRun unconditionally and that is still right: it refuses a
    // finished game itself, so this path writes nothing.
    if (m_showingResult && registry.get<UIButtonComponent>(m_over.mainMenu).clickedThisTick) {
        goTo(registry, Screen::Menu);
    }
}

void WolfBrigadeLayer::OnDetach(entt::registry& registry) {
    // THE PORT OF `main.gd:98-100` - the close-request autosave - and this is
    // where it goes because this is the only inbound notification the engine
    // gives a layer. Both ways out of the run loop, the window closing and
    // Application::RequestQuit, leave through the same path, and
    // ~SupersonicApp calls LayerStack::Clear before the registry is destroyed
    // on purpose. So there is exactly one shutdown and this is inside it.
    //
    // What it does NOT cover, said rather than assumed: a crash, and Android's
    // APPLICATION_PAUSED, which fires without exiting. Neither has a delivery
    // path today - the second because GLFW has no Android backend at all - and
    // the answer to the first is a periodic autosave rather than a callback.
    //
    // AutosaveRun refuses a finished game itself, so a player who quits from
    // the result screen does not get a Continue for a run that is over.
    if (m_match) m_match->AutosaveRun();

    // And the profile, which the last tick may have dirtied after its own
    // write - a wave started, or renown banked, on the way out.
    saveProfileIfDirty();

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
    tick(registry, fixedDelta);

    // AFTER the tick, whichever way the tick left. The body below returns early
    // in three places - not booted, and paused - and a save written at the end
    // of it would be skipped by all three. Wrapping is two lines; remembering
    // to write before each return is a thing the fourth return forgets.
    saveProfileIfDirty();
}

void WolfBrigadeLayer::tick(entt::registry& registry, float fixedDelta) {
    if (!m_booted) return;

    // THE MENU FIRST, and before the match test rather than after it, because
    // there is no match while the menu is up - it was dropped on the way in.
    // A New Game pressed this tick creates one, and everything below then runs
    // against it in the same tick, which is what a click reaching the
    // simulation immediately means everywhere else in this file.
    updateMenu(registry);
    updateArmory(registry);

    if (!m_match) return;

    // THE RESULT FIRST, and the order is load-bearing. "You cannot pause a
    // finished game" has to be known before the Pause button is read, or the
    // press is honoured, the menu is raised over the result, and only the tick
    // AFTER that takes it down again - which is a frame of a pause menu on top
    // of a game that has already ended.
    updateGameOver(registry);

    // LEAVING FOR THE MENU DROPS THE MATCH, so every step after one that can
    // transition has to ask again whether there is still one.
    //
    // This is the price of switching where the click is read instead of
    // deferring it to a drain point, and it is worth paying here: a Restart
    // already replaces the match synchronously from inside these same
    // handlers. But a Restart leaves a match behind and a Main Menu does not,
    // which is the difference that made this a crash rather than a subtlety -
    // updateBar dereferences the match, and goTo clears both modal flags on the
    // way out, so the guard that would have skipped the bar was cleared by the
    // same call that removed what the bar reads.
    if (!m_match) return;

    updateHud(registry);

    // Read what the player pressed BEFORE the match steps, so an order given
    // this tick takes effect this tick rather than one later, and then make the
    // strip agree with whatever that changed.
    updatePauseMenu(registry);
    if (!m_match) return;

    // The bar is UNDER the overlay, so while it is up the bar neither acts nor
    // rebuilds. UIInput already refuses a click that landed on the backdrop,
    // and this is the matching half: a bar that kept rebuilding behind a pause
    // menu would change under the player while they could not see it.
    if (!m_paused && !m_showingResult) {
        applyBarClicks(registry);
        updateBar(registry);
    }

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
