// Magic Portals' layer: the seam between the port and the picture.
//
// test_mp_play holds the port to the remake on level30, through Game::Tick.
// What this suite holds is the layer's side:
//  - it plays at the port's 60 Hz on the engine's clock, and runs the suites'
//    tick with the app's physics step between the two halves;
//  - a tap read on the tick becomes a portal at the point in the level under
//    it, through the camera and ScreenPointToRay, which no other suite touches;
//  - level30 can be played from the spawn to the exit with taps and walking
//    alone.
//
// No window and no Vulkan. The app steps physics once before each layer tick at
// 60 Hz, so this suite does the same.

#include "TestHarness.hpp"

#include "MagicPortalsLayer.hpp"

#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Units.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace Supersonic;
using MagicPortals::MagicPortalsLayer;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;

std::filesystem::path Prisms() {
    return std::filesystem::temp_directory_path() / "supersonic-test-mp-layer";
}

// The viewport a frame would publish: the whole 1280x720 window, with the
// pointer over the game.
void publishViewport(entt::registry& registry) {
    ViewportInfo viewport;
    viewport.rect.min = glm::vec2(0.0f, 0.0f);
    viewport.rect.max = glm::vec2(1280.0f, 720.0f);
    viewport.pointerOverGame = true;
    registry.ctx().insert_or_assign(viewport);
}

entt::entity primaryCamera(entt::registry& registry) {
    for (auto [entity, camera] : registry.view<CameraComponent>().each()) {
        if (camera.isPrimary) return entity;
    }
    return entt::null;
}

// Where a point in the level shows on the 1280x720 viewport, by the camera's own
// matrices: the renderer's way forward. The layer reads a tap back the other
// way, through ScreenPointToRay, so the two paths check each other.
glm::vec2 screenOf(entt::registry& registry, const glm::dvec2& px) {
    const auto& camera = registry.get<CameraComponent>(primaryCamera(registry));
    const glm::vec3 world = MagicPortals::Units::ToWorld(px.x, px.y);
    const glm::vec4 clip = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return glm::vec2((ndc.x + 1.0f) * 0.5f * 1280.0f, (ndc.y + 1.0f) * 0.5f * 720.0f);
}

// One tick as the app runs it: the physics step, then the layer, with this
// tick's input fed through Input's replay path. `down` is what is held for
// the tick, and `pressed` the edges it owns.
void tickWith(MagicPortalsLayer& layer, entt::registry& registry, const glm::vec2& pointer,
              std::vector<std::string> down, std::vector<std::string> pressed) {
    PhysicsSystem::Update(registry, MagicPortalsLayer::kTick);
    Input::TickInput input;
    input.mousePosition = pointer;
    input.down = std::move(down);
    input.pressed = std::move(pressed);
    Input::BeginReplayedTick(input);
    layer.OnFixedUpdate(registry, MagicPortalsLayer::kTick);
    Input::EndReplayedTick();
}

void tap(MagicPortalsLayer& layer, entt::registry& registry, const glm::vec2& pointer) {
    tickWith(layer, registry, pointer, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
}

std::string Point(const glm::dvec2& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

void TheLayerPlaysLevel30() {
    // Attached to a bare registry, the layer loads level30 and the remake's data
    // from where the build says they are. It builds the level as test_mp_play
    // does, and plays at 60 Hz.
    entt::registry registry;
    MagicPortalsLayer layer(kLevels + "/level30.tscn", kData, Prisms());
    layer.OnAttach(registry);
    CHECK_MSG(layer.LoadError().empty(), "level30 loads: " + layer.LoadError());
    const MagicPortals::Game::Level* level = layer.SimLevel();
    CHECK_MSG(level != nullptr && level->built.statics == 12 && level->built.movers == 3 &&
                  level->player != entt::null && level->portals.budget == 2,
              "level30 built as test_mp_play builds it: 12 statics, 3 doors, the player, a budget of 2");
    CHECK_NEAR(registry.ctx().get<SimulationClock>().fixedDelta, MagicPortalsLayer::kTick);
    layer.OnDetach(registry);
}

void ATapLandsWhereItPoints() {
    // The camera is fitted to the level. So the level's centre, (384, 128), shows
    // at the viewport's centre, its top edge above it and its left edge to the
    // left: screen y grows down, as the remake's does, where the engine's grows
    // up. Then a tap at a point is read on the tick, back through
    // ScreenPointToRay, and a portal lands where it points.
    entt::registry registry;
    MagicPortalsLayer layer(kLevels + "/level30.tscn", kData, Prisms());
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    publishViewport(registry);
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);

    const glm::vec2 centre = screenOf(registry, glm::dvec2(384.0, 128.0));
    CHECK_MSG(glm::length(centre - glm::vec2(640.0f, 360.0f)) < 0.5f,
              "the level's centre shows at the viewport's: at (" + std::to_string(centre.x) + ", " +
                  std::to_string(centre.y) + ")");
    CHECK_MSG(screenOf(registry, glm::dvec2(384.0, 0.0)).y < centre.y &&
                  screenOf(registry, glm::dvec2(0.0, 128.0)).x < centre.x,
              "its top is up the screen, and its left edge is to the left");

    const glm::dvec2 aims[] = {glm::dvec2(384.0, 128.0), glm::dvec2(100.0, 60.0)};
    for (const glm::dvec2& aim : aims) {
        tap(layer, registry, screenOf(registry, aim));
        tickWith(layer, registry, centre, {}, {});
    }
    const auto& placed = layer.SimLevel()->portals.placed;
    CHECK_MSG(placed.size() == 2u && glm::length(placed[0].atPx - aims[0]) < 0.5 &&
                  glm::length(placed[1].atPx - aims[1]) < 0.5,
              "each tap put a portal where it pointed: " +
                  (placed.size() == 2u ? Point(placed[0].atPx) + " and " + Point(placed[1].atPx)
                                       : std::to_string(placed.size()) + " placed"));
    layer.OnDetach(registry);
}

void Level30FromTheSpawnWithTapsAndWalking() {
    // Played through as a player would. A portal is tapped onto the floor ahead
    // of the spawn, its partner above platform_ent_966 near the exit, and right
    // is held down. The player walks into the first portal and comes out of the
    // second, over crystal_ent_998. It falls onto the platform, collecting the
    // crystal, and walks on into the exit. level30's exit switch is off, so that
    // completes the level.
    //
    // This is not the designed solve. A pair of portals skips the doors, because
    // the remake refuses a portal only inside a no-portal zone, and level30 has
    // none.
    entt::registry registry;
    MagicPortalsLayer layer(kLevels + "/level30.tscn", kData, Prisms());
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    publishViewport(registry);
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    const glm::vec2 rest(640.0f, 360.0f);
    for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, rest, {}, {});

    tap(layer, registry, screenOf(registry, glm::dvec2(240.0, 208.0)));
    tap(layer, registry, screenOf(registry, glm::dvec2(660.0, 100.0)));
    const MagicPortals::Game::Level* level = layer.SimLevel();
    CHECK_MSG(level->portals.placed.size() == 2u, "two portals tapped in");

    int completedAt = -1;
    for (int tick = 1; tick <= 360 && completedAt < 0; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 1) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, rest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (level->goals.completed) completedAt = tick;
    }
    std::printf("  played through: the exit reached %.2f s after right went down, %d of %d crystals, %d portals\n",
                completedAt * static_cast<double>(MagicPortalsLayer::kTick),
                static_cast<int>(level->goals.crystals.size()) - level->goals.Remaining(),
                static_cast<int>(level->goals.crystals.size()), level->portals.portalsUsed);
    CHECK_MSG(completedAt > 0, "the player reaches the exit");
    CHECK_MSG(level->portals.traversals == 1 && level->portals.portalsUsed == 2,
              "through one pair, with two portals placed");
    const MagicPortals::Goals::Crystal* crystal = level->goals.FindCrystal("crystal_ent_998");
    CHECK_MSG(crystal != nullptr && crystal->collected, "collecting crystal_ent_998 on the way");
    layer.OnDetach(registry);
}

void runTests() {
    TheLayerPlaysLevel30();
    ATapLandsWhereItPoints();
    Level30FromTheSpawnWithTapsAndWalking();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec) ||
        !std::filesystem::is_regular_file(kData + "/player.json", ec) ||
        !std::filesystem::is_regular_file(kData + "/portals.json", ec)) {
        std::printf("test_mp_layer: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_layer", 9);
}
