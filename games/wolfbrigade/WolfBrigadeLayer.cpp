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

    // ONE BUTTON THAT CANNOT WORK YET, shown greyed rather than left out.
    //
    // "Main Menu" needs a menu to go to and a way to route between screens, and
    // neither is ported - game_flow.gd has no counterpart here. Greyed rather
    // than absent because that is what this game does everywhere else, and
    // because a menu missing one of its four items looks finished and is not.
    registry.get<UIButtonComponent>(m_pause.mainMenu).enabled = false;

    setPauseMenuVisible(registry, false);
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
    if (!m_booted || !m_match) return;

    // THE RESULT FIRST, and the order is load-bearing. "You cannot pause a
    // finished game" has to be known before the Pause button is read, or the
    // press is honoured, the menu is raised over the result, and only the tick
    // AFTER that takes it down again - which is a frame of a pause menu on top
    // of a game that has already ended.
    updateGameOver(registry);

    updateHud(registry);

    // Read what the player pressed BEFORE the match steps, so an order given
    // this tick takes effect this tick rather than one later, and then make the
    // strip agree with whatever that changed.
    updatePauseMenu(registry);

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
