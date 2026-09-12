// Magic Portals' layer: the seam between the port and the picture.
//
// test_mp_play holds the port to the remake on level30, through Game::Tick.
// What this suite holds is the layer's side:
//  - it plays at the port's 60 Hz on the engine's clock, and runs the suites'
//    tick with the app's physics step between the two halves;
//  - a tap read on the tick fires a portal shot at the point in the level under
//    it, through the camera and ScreenPointToRay, which no other suite touches;
//  - the camera starts at camera_start, follows the player, and never shows past
//    the level;
//  - levels come in chapters.json's order: the exit loads the next, R retries,
//    N skips one the port refuses, and a world's end is the chapter's end;
//  - 1-9 (level8) can be played from the spawn to the exit with taps and
//    walking alone, tapping only what the screen shows.
//
// No window and no Vulkan. The app steps physics once before each layer tick at
// 60 Hz, so this suite does the same.

#include "TestHarness.hpp"

#include "MagicPortalsLayer.hpp"

#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/InterpolationSystem.hpp"
#include "core/PhysicsSystem.hpp"
// For the renderer's own cull: RenderSystem.hpp carries renderer/Frustum.hpp.
#include "core/RenderSystem.hpp"
#include "core/TransformSystem.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Art.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstdint>
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
const std::string kChapters = MAGICPORTALS_CHAPTERS_FILE;
const glm::vec2 kRest(640.0f, 360.0f);

MagicPortalsLayer::Paths TestPaths() {
    MagicPortalsLayer::Paths paths;
    paths.prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-layer";
    return paths;
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

// A player can only tap what the screen shows.
bool onScreen(entt::registry& registry, const glm::dvec2& px) {
    const glm::vec2 s = screenOf(registry, px);
    return s.x > 1.0f && s.x < 1279.0f && s.y > 1.0f && s.y < 719.0f;
}

// The whole view inside the level, as the camera last left it.
bool viewInside(const MagicPortalsLayer& layer) {
    const glm::dvec2 c = layer.CameraCentrePx();
    const glm::dvec2 half = layer.ViewPx() * 0.5;
    const glm::dvec2 bounds = layer.BoundsPx();
    const double eps = 1e-6;
    return c.x - half.x >= -eps && c.x + half.x <= bounds.x + eps && c.y - half.y >= -eps &&
           c.y + half.y <= bounds.y + eps;
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

void press(MagicPortalsLayer& layer, entt::registry& registry, const char* action) {
    tickWith(layer, registry, kRest, {action}, {action});
}

void tap(MagicPortalsLayer& layer, entt::registry& registry, const glm::vec2& pointer) {
    tickWith(layer, registry, pointer, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
}

// Ticks until the shot a tap fired has landed or failed, AND until another tap
// would be taken (Portals.hpp).
//
// The second half is not padding. The original refuses a tap for 400 ms after
// the last one it took - placement.json's next_ms, decoded - and a shot that
// runs into something a few pixels away is over in two ticks. Without the wait,
// every case here that taps twice in a row had its second tap refused, which
// read as "the shot did not land" rather than "you tapped too soon".
//
// A player cannot tap again instantly either, so waiting is what this helper
// always meant.
void landShot(MagicPortalsLayer& layer, entt::registry& registry) {
    for (int tick = 0; tick < 240 && layer.SimLevel() != nullptr && layer.SimLevel()->portals.flight; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
    }
    // 400 ms and a tick over, at the port's 60 Hz.
    for (int tick = 0; tick < 26; ++tick) tickWith(layer, registry, kRest, {}, {});
}

// Ticks until a level is old enough for its first tap to be taken.
//
// PortalManager holds a whole level's taps off for 300 ms, and both of its
// timers start at zero, so the first one actually waits on the LARGER gate of
// 400 ms (placement.json). A case that attaches a layer and taps at once is
// refused - correctly - so the ones that mean to test the shot say so here.
void waitForFirstTap(MagicPortalsLayer& layer, entt::registry& registry) {
    for (int tick = 0; tick < 26; ++tick) tickWith(layer, registry, kRest, {}, {});
}

std::string lastFailure(const MagicPortalsLayer& layer) {
    return layer.SimLevel() != nullptr ? layer.SimLevel()->portals.lastFailure : std::string("no level");
}

glm::dvec2 playerPx(entt::registry& registry, const MagicPortalsLayer& layer) {
    return MagicPortals::Units::ToPixels(registry.get<TransformComponent>(layer.SimLevel()->player).position);
}

std::string Point(const glm::dvec2& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

bool IsAt(const MagicPortalsLayer& layer, const char* name) {
    return layer.Current() != nullptr && layer.Current()->name == name;
}

// Press "go on" on the medal screen a finished level puts up. Returns false
// when no such button is there, which is a finished level that did not finish.
//
// A level no longer advances on its own: reaching the exit stops on its medal,
// as the original's LevelFinishedLayer does, so a test that walks to an exit
// has to dismiss it exactly as a player does.
bool GoOnFromTheMedal(MagicPortalsLayer& layer, entt::registry& registry) {
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::Next) continue;
        layer.PressMenu(registry, button); // by value: pressing clears the list
        return true;
    }
    return false;
}

int Tagged(entt::registry& registry, const char* tag) {
    int count = 0;
    for (auto [entity, t] : registry.view<TagComponent>().each()) {
        (void)entity;
        if (t.tag == tag) ++count;
    }
    return count;
}

// The first entity wearing this tag, or null. Tagged answers "how many", which
// is the right question for furniture that must not stack; this answers "which
// one", for the checks that need to look at what it is wearing.
entt::entity FirstTagged(entt::registry& registry, const char* tag) {
    for (auto [entity, t] : registry.view<TagComponent>().each()) {
        if (t.tag == tag) return entity;
    }
    return entt::null;
}

// The original's extracted images - what no level pictures is drawn with - are
// outside this repository like the levels, but a machine can have the levels and
// not them. What needs them says so and is skipped.
bool OriginalArtIsThere(const char* test) {
    std::error_code ec;
    const std::string entities = std::string(MAGICPORTALS_ORIGINAL_DIR) + "/entities";
    if (std::filesystem::is_directory(entities, ec)) return true;
    std::printf("  %s SKIPPED - needs the original's extracted assets at %s\n", test, MAGICPORTALS_ORIGINAL_DIR);
    return false;
}

// How many entities with this tag are drawn.
int Shown(entt::registry& registry, const char* tag) {
    int count = 0;
    for (auto [entity, t, renderable] : registry.view<TagComponent, RenderableComponent>().each()) {
        (void)entity;
        if (t.tag == tag && renderable.isVisible) ++count;
    }
    return count;
}

// ---- One level --------------------------------------------------------------

void TheLayerPlaysLevel30() {
    // Attached to a bare registry, the layer loads level30 and the remake's data
    // from where the build says they are. It builds the level as test_mp_play
    // does, and plays at 60 Hz.
    entt::registry registry;
    MagicPortalsLayer layer(TestPaths(), "level30");
    layer.OnAttach(registry);
    CHECK_MSG(layer.LoadError().empty(), "level30 loads: " + layer.LoadError());
    CHECK(IsAt(layer, "level30"));
    const MagicPortals::Game::Level* level = layer.SimLevel();
    CHECK_MSG(level != nullptr && level->built.statics == 12 && level->built.movers == 3 &&
                  level->player != entt::null && level->portals.budget == 2,
              "level30 built as test_mp_play builds it: 12 statics, 3 doors, the player, a budget of 2");
    CHECK_NEAR(registry.ctx().get<SimulationClock>().fixedDelta, MagicPortalsLayer::kTick);
    layer.OnDetach(registry);
}

void ATapLandsWhereItPoints() {
    // The camera starts at level30's camera_start, (540, 0), held inside the
    // level: the view is view.json's 256 px tall and 455 px wide at 16:9, and
    // level30 is 768 x 256, so it sits at (540, 128). Its centre shows at the
    // viewport's, the level's top is up the screen - screen y grows down, as the
    // remake's does, where the engine's grows up - and a tap at a point on the
    // screen is read on the tick, back through ScreenPointToRay. Its shot flies
    // from the player, and a portal opens where it points. A tap whose shot
    // meets a crate on the way opens nothing and costs nothing.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level30");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);

    CHECK_MSG(layer.CameraCentrePx() == glm::dvec2(540.0, 128.0), "the camera starts at " + Point(layer.CameraCentrePx()));
    CHECK(layer.ViewPx().y == 256.0);
    const glm::vec2 centre = screenOf(registry, layer.CameraCentrePx());
    CHECK_MSG(glm::length(centre - glm::vec2(640.0f, 360.0f)) < 0.5f,
              "the camera's centre shows at the viewport's: at (" + std::to_string(centre.x) + ", " +
                  std::to_string(centre.y) + ")");
    CHECK_MSG(screenOf(registry, glm::dvec2(540.0, 64.0)).y < centre.y &&
                  screenOf(registry, glm::dvec2(400.0, 128.0)).x < centre.x,
              "the level's top is up the screen, and its left is to the left");

    // A level's first tap waits out the placement cooldown, as a player's does.
    waitForFirstTap(layer, registry);

    // From the spawn, (182, 203), a shot at (600, 128) runs into crate_969.
    const glm::dvec2 blocked(600.0, 128.0);
    CHECK_MSG(onScreen(registry, blocked), Point(blocked) + " is on screen");
    tap(layer, registry, screenOf(registry, blocked));
    landShot(layer, registry);
    CHECK(layer.SimLevel()->portals.placed.empty());
    CHECK_MSG(lastFailure(layer) == "crate_969", "the shot stopped at " + lastFailure(layer));
    CHECK_EQ(layer.SimLevel()->portals.portalsUsed, 0);

    // Two over the crates land. Both stay on screen while the camera, after its
    // hold, goes over to the player.
    const glm::dvec2 aims[] = {glm::dvec2(400.0, 60.0), glm::dvec2(440.0, 60.0)};
    for (const glm::dvec2& aim : aims) {
        CHECK_MSG(onScreen(registry, aim), Point(aim) + " is on screen");
        tap(layer, registry, screenOf(registry, aim));
        landShot(layer, registry);
    }
    const auto& placed = layer.SimLevel()->portals.placed;
    CHECK_MSG(placed.size() == 2u && glm::length(placed[0].atPx - aims[0]) < 0.5 &&
                  glm::length(placed[1].atPx - aims[1]) < 0.5,
              "each tap's shot opened a portal where it pointed: " +
                  (placed.size() == 2u ? Point(placed[0].atPx) + " and " + Point(placed[1].atPx)
                                       : std::to_string(placed.size()) + " placed, the last shot stopped at " +
                                             lastFailure(layer)));
    layer.OnDetach(registry);
}

void Level8FromTheSpawnWithTapsAndWalking() {
    // 1-9 played through as a player would, tapping only what the screen shows.
    // test_mp_demolish plays the same route through Game::Tick.
    //  - A shot from the spawn to behind the stone meets the stone, so the player
    //    walks right onto the slope first and lets go.
    //  - From there, once the camera shows it, a shot over the stone to
    //    (30, 30), and then one just ahead of the player.
    //  - Right is held. The player walks into the near portal and comes out of
    //    the far one, behind the stone. It pushes the stone off the ledge, and
    //    the stone rolls into breakable_wall_625 and breaks it. The player walks
    //    on to the exit.
    // Reaching the exit clears level8 and loads level9. The route waits for what
    // the screen shows, not for how long the camera holds, which is a guess.
    //
    // It was level30 until the shot. There, a pair of portals skipped the doors,
    // but the doors and crates stand between the spawn and anywhere past them,
    // so a shot cannot reach; only the designed solve is left.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level8");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);

    for (int tick = 0; tick < 180 && playerPx(registry, layer).x < 195.0; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
    }
    for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, {}, {});

    const glm::dvec2 far(30.0, 30.0);
    int waited = 0;
    while (!onScreen(registry, far) && waited < 600) {
        tickWith(layer, registry, kRest, {}, {});
        ++waited;
    }
    CHECK_MSG(onScreen(registry, far), "the spot over the stone comes into view");
    tap(layer, registry, screenOf(registry, far));
    landShot(layer, registry);
    const glm::dvec2 near = playerPx(registry, layer) + glm::dvec2(40.0, 0.0);
    CHECK_MSG(onScreen(registry, near), Point(near) + " is on screen");
    tap(layer, registry, screenOf(registry, near));
    landShot(layer, registry);
    const MagicPortals::Game::Level* level = layer.SimLevel();
    CHECK_MSG(level != nullptr && level->portals.placed.size() == 2u,
              "two portals shot in; the last shot stopped at " + lastFailure(layer));

    int clearedAt = -1;
    for (int tick = 1; tick <= 1800 && clearedAt < 0; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 1) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        // Reaching the exit stops on the level's medal rather than going
        // straight on, so THAT is what says it was cleared.
        if (layer.MenuScreen() == MagicPortalsLayer::Screen::Finished) clearedAt = tick;
    }
    const auto& cleared = layer.LastCleared();
    std::printf("  played through: 1-9 cleared %.2f s after right went down, %d of %d crystals, %d portals\n",
                clearedAt * static_cast<double>(MagicPortalsLayer::kTick), cleared ? cleared->crystals : -1,
                cleared ? cleared->crystalsTotal : -1, cleared ? cleared->portalsUsed : -1);
    CHECK_MSG(clearedAt > 0, "the player reaches the exit");
    CHECK_MSG(cleared && cleared->name == "level8" && cleared->label == "1-9" && cleared->portalsUsed == 2 &&
                  cleared->traversals >= 1,
              "through the pair the two shots opened");
    CHECK_MSG(GoOnFromTheMedal(layer, registry), "the medal screen offers to go on");
    CHECK_MSG(IsAt(layer, "level9") && layer.SimLevel() != nullptr, "and level9 is loaded");
    layer.OnDetach(registry);
}

// ---- The levels in order ------------------------------------------------------

void LevelsFollowInOrderAndRetryIsInstant() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    // level0 is solved by walking right, through its two static pairs
    // (test_mp_statics). The camera follows, and never shows past the level.
    bool inside = viewInside(layer);
    for (int tick = 0; tick < 600 && layer.MenuScreen() == MagicPortalsLayer::Screen::None; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (layer.MenuScreen() == MagicPortalsLayer::Screen::None) inside = inside && viewInside(layer);
    }
    // level0 is finished, and its medal is up; going on from it loads level1.
    CHECK_MSG(GoOnFromTheMedal(layer, registry), "clearing level0 puts its medal up");
    CHECK_MSG(IsAt(layer, "level1"), "clearing level0 loads level1");
    CHECK_MSG(inside, "the camera never showed past level0");
    const auto& cleared = layer.LastCleared();
    CHECK_MSG(cleared && cleared->label == "1-1" && cleared->portalsUsed == 0 && cleared->Gold(),
              "1-1 cleared with no portal: its golden score is 0, so that is gold");
    if (layer.SimLevel() == nullptr) return;

    // level1 as it loads: the player at its spawn, (448, 110).
    const glm::dvec2 spawn(448.0, 110.0);
    CHECK_MSG(glm::distance(playerPx(registry, layer), spawn) < 1.0, "at " + Point(playerPx(registry, layer)));

    // A portal placed, then R: level1 as it loaded, again.
    //
    // level1 was reached by CLEARING level0, and its portal manager is as new as
    // any other level's - the cooldown clocks start again with it. So this tap
    // waits too, which is the same rule arriving by a different door.
    waitForFirstTap(layer, registry);
    const glm::dvec2 aim = spawn + glm::dvec2(0.0, -48.0);
    CHECK_MSG(onScreen(registry, aim), Point(aim) + " is on screen");
    tap(layer, registry, screenOf(registry, aim));
    landShot(layer, registry);
    CHECK_MSG(layer.SimLevel()->portals.portalsUsed == 1, "the shot stopped at " + lastFailure(layer));
    for (int tick = 0; tick < 20; ++tick) tickWith(layer, registry, kRest, {}, {});
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK(IsAt(layer, "level1"));
    CHECK(layer.SimLevel() != nullptr);
    if (layer.SimLevel() == nullptr) return;
    CHECK_EQ(layer.SimLevel()->portals.portalsUsed, 0);
    CHECK(layer.SimLevel()->portals.placed.empty());
    CHECK_MSG(glm::distance(playerPx(registry, layer), spawn) < 1.0,
              "back at the spawn: " + Point(playerPx(registry, layer)));
    layer.OnDetach(registry);
}

void NSkipsWhatThePortRefuses() {
    // level26c is one of chapter 4's dark levels, which the port refuses, and
    // level27c after it starts.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level26c");
    layer.OnAttach(registry);
    CHECK(IsAt(layer, "level26c"));
    CHECK(layer.SimLevel() == nullptr);
    CHECK_MSG(layer.LoadError().find("darkest") != std::string::npos, layer.LoadError());
    // Nothing of a refused level is left: the camera is the one thing with a
    // transform. Every refusal in the data now comes before anything is built,
    // so this holds for the later ones too only because unloadLevel runs on
    // every refusal.
    CHECK_EQ(registry.view<TransformComponent>().size(), std::size_t{1});

    press(layer, registry, MagicPortalsLayer::kSkip);
    CHECK(IsAt(layer, "level27c"));
    CHECK_MSG(layer.SimLevel() != nullptr && layer.LoadError().empty(), layer.LoadError());
    layer.OnDetach(registry);
}

void DeathIsAnInstantRetry() {
    // Into level5's death_area, and on that same tick the level is as it loaded:
    // the player at its spawn, one death counted, no load in between.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level5");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    if (layer.SimLevel() == nullptr) return;
    const glm::dvec2 spawn = playerPx(registry, layer);
    const MagicPortals::Hazards::Hazard* hazard = layer.SimLevel()->hazards.FindHazard("death_area_ent_800");
    CHECK(hazard != nullptr);
    if (hazard == nullptr) return;
    auto& transform = registry.get<TransformComponent>(layer.SimLevel()->player);
    transform.position = glm::vec3(hazard->box.centre, transform.position.z);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_EQ(layer.Deaths(), 1);
    CHECK(IsAt(layer, "level5"));
    CHECK(layer.SimLevel() != nullptr);
    if (layer.SimLevel() == nullptr) return;
    CHECK(!layer.SimLevel()->hazards.playerDied);
    CHECK_MSG(glm::distance(playerPx(registry, layer), spawn) < 1.0,
              "back at the spawn: " + Point(playerPx(registry, layer)));
    layer.OnDetach(registry);
}

void ABrokenWallTakesItsBoxWithIt() {
    // level8's stone thrown at its wall: on the tick the wall breaks, its box goes
    // with its body, and no other box does.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level8");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    if (layer.SimLevel() == nullptr || layer.SimLevel()->demolish.stones.size() != 1) return;
    const auto boxes = [&registry] {
        int count = 0;
        for (auto [entity, tag] : registry.view<TagComponent>().each()) {
            (void)entity;
            if (tag.tag == "Magic Portals Body") ++count;
        }
        return count;
    };
    const int before = boxes();
    const int pictures = Tagged(registry, "Magic Portals Sprite");
    const entt::entity stone = layer.SimLevel()->demolish.stones.front().body;
    auto& transform = registry.get<TransformComponent>(stone);
    const glm::vec3 at = MagicPortals::Units::ToWorld(305.0 - 30.0 - 6.0, 95.0);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);
    registry.get<RigidBodyComponent>(stone).velocity = glm::vec3(MagicPortals::Units::ToMetres(300.0), 0.0f, 0.0f);
    for (int tick = 0; tick < 30 && layer.SimLevel()->demolish.Broken() == 0; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
    }
    CHECK_EQ(layer.SimLevel()->demolish.Broken(), 1);
    CHECK_EQ(boxes(), before - 1);
    CHECK_MSG(Tagged(registry, "Magic Portals Sprite") == pictures - 1, "and its picture with it");
    layer.OnDetach(registry);
}

void ARetryTakesTheThrownStonesAway() {
    // level23's launcher throws on its own. After its first stone, R: the level
    // comes back as it loaded, with no thrown stone, no box for one and the
    // launcher's count at nothing. Its next throw is a whole first delay from
    // the retry.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level23");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    if (layer.SimLevel() == nullptr || layer.SimLevel()->launchers.launchers.size() != 1) return;
    const auto spheres = [&registry] {
        int count = 0;
        for (const entt::entity entity : registry.view<SphereColliderComponent>()) {
            (void)entity;
            ++count;
        }
        return count;
    };
    const auto thrownBoxes = [&registry] {
        int count = 0;
        for (auto [entity, tag] : registry.view<TagComponent>().each()) {
            (void)entity;
            if (tag.tag == "Magic Portals Thrown") ++count;
        }
        return count;
    };
    const auto thrown = [&layer] { return layer.SimLevel()->launchers.launchers.front().thrown; };
    const int loadedSpheres = spheres();
    int first = 0;
    while (first < 400 && thrown() < 1) {
        tickWith(layer, registry, kRest, {}, {});
        ++first;
    }
    CHECK_EQ(layer.SimLevel()->launchers.live.size(), std::size_t{1});
    CHECK_EQ(thrownBoxes(), 1);
    CHECK_MSG(Tagged(registry, "Magic Portals Thrown Sprite") == 1, "drawn as the rolling stone it is");
    CHECK_EQ(spheres(), loadedSpheres + 1);

    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK(IsAt(layer, "level23"));
    if (layer.SimLevel() == nullptr) return;
    CHECK(layer.SimLevel()->launchers.live.empty());
    CHECK_EQ(thrown(), 0);
    CHECK_EQ(thrownBoxes(), 0);
    CHECK_EQ(Tagged(registry, "Magic Portals Thrown Sprite"), 0);
    CHECK_EQ(spheres(), loadedSpheres);
    // The tick that pressed R played the reloaded level's first tick.
    int again = 1;
    while (again < 400 && thrown() < 1) {
        tickWith(layer, registry, kRest, {}, {});
        ++again;
    }
    CHECK_MSG(again == first, "first thrown " + std::to_string(first) + " ticks in, and " + std::to_string(again) +
                                  " after the retry");
    layer.OnDetach(registry);
}

void TheChapterEnds() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level31");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    CHECK(!layer.ChapterComplete());
    press(layer, registry, MagicPortalsLayer::kSkip);
    CHECK(layer.ChapterComplete());
    CHECK(layer.SimLevel() == nullptr);
    // Nothing to retry at a chapter's end.
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK(layer.ChapterComplete());
    layer.OnDetach(registry);
}

void AStartThatIsNoLevelSaysSo() {
    entt::registry registry;
    MagicPortalsLayer layer(TestPaths(), "level99");
    layer.OnAttach(registry);
    CHECK(layer.Current() == nullptr);
    CHECK_MSG(layer.LoadError().find("level99") != std::string::npos, layer.LoadError());
    layer.OnDetach(registry);
}

// ---- The level's art ----------------------------------------------------------

void TheLevelsArtIsDrawn() {
    // level8's 23 sprites, each a quad drawn unlit and blended with the image the
    // level names, the sky farthest back. The bodies' boxes are hidden behind
    // them and B shows them; the player, which no level pictures, stays a box.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level8");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    CHECK_MSG(layer.ArtError().empty(), layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    CHECK_EQ(Tagged(registry, "Magic Portals Sprite"), 23);

    bool drawnAsArt = true;
    bool skyFound = false;
    float skyZ = 0.0f;
    float farthest = 1e9f;
    glm::vec3 skyScale(0.0f);
    for (auto [entity, tag, material, transform] :
         registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals Sprite") continue;
        std::error_code ec;
        if (!material.unlit || !material.transparent || material.blend != MaterialComponent::BlendMode::Alpha ||
            !std::filesystem::is_regular_file(material.albedoTexturePath, ec)) {
            drawnAsArt = false;
        }
        if (material.albedoTexturePath.find("icy_sky.png") != std::string::npos) {
            skyFound = true;
            skyZ = transform.position.z;
            skyScale = transform.scale;
        }
        if (transform.position.z < farthest) farthest = transform.position.z;
    }
    CHECK_MSG(drawnAsArt, "each unlit, mixed as level8 says, with an image that is there");
    CHECK_MSG(skyFound && skyZ == farthest, "the sky is the farthest back");
    CHECK_MSG(std::fabs(skyScale.x - 455.0f / 50.0f) < 1e-4f && std::fabs(skyScale.y - 256.0f / 50.0f) < 1e-4f,
              "at its image's own size");

    const int bodies = Tagged(registry, "Magic Portals Body");
    CHECK(bodies > 0);
    CHECK_MSG(Shown(registry, "Magic Portals Body") == 0, "the art stands in for the bodies' boxes");
    // No level pictures the player: it is the original's character when those
    // images are there (9d), and its box when they are not - one or the other.
    CHECK_MSG(Shown(registry, "Magic Portals Player") + Shown(registry, "Magic Portals Player Sprite") == 1,
              "and the player is drawn once, as the character or as its box");
    press(layer, registry, MagicPortalsLayer::kBoxes);
    CHECK(layer.ShowingBoxes());
    CHECK_MSG(Shown(registry, "Magic Portals Body") == bodies, "B shows every body's box");
    press(layer, registry, MagicPortalsLayer::kBoxes);
    CHECK_EQ(Shown(registry, "Magic Portals Body"), 0);

    // A crystal taken takes its picture with it.
    const int pictures = Tagged(registry, "Magic Portals Sprite");
    const MagicPortals::Goals::Crystal* crystal = nullptr;
    for (const MagicPortals::Goals::Crystal& c : layer.SimLevel()->goals.crystals) {
        if (c.name == "crystal_ent_790") crystal = &c;
    }
    CHECK(crystal != nullptr);
    if (crystal == nullptr) return;
    auto& transform = registry.get<TransformComponent>(layer.SimLevel()->player);
    transform.position = glm::vec3(crystal->box.centre, transform.position.z);
    tickWith(layer, registry, kRest, {}, {});
    const bool taken = crystal->collected;
    CHECK_MSG(taken, "the player put in crystal_ent_790 takes it");
    CHECK_MSG(Tagged(registry, "Magic Portals Sprite") == pictures - 1, "and its picture goes");
    layer.OnDetach(registry);
    CHECK_MSG(Tagged(registry, "Magic Portals Sprite") == 0, "and none outlives the layer");
}

void AStaticPortalGlows() {
    // level0's four static portals are the halo, added: the one blend besides
    // mixing that the levels use.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    int added = 0;
    for (auto [entity, tag, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (tag.tag == "Magic Portals Sprite" && material.blend == MaterialComponent::BlendMode::Additive &&
            material.albedoTexturePath.find("portal_halo.png") != std::string::npos) {
            ++added;
        }
    }
    CHECK_EQ(Tagged(registry, "Magic Portals Sprite"), 18);
    CHECK_EQ(added, 4);
    layer.OnDetach(registry);
}

void WithoutTheArtTheLevelIsBoxes() {
    // The art is the original's and lives outside the repository. Where it
    // cannot be read, the level is still played, drawn as boxes, and says why.
    const std::filesystem::path empty = std::filesystem::temp_directory_path() / "supersonic-test-mp-layer-no-art";
    std::error_code ec;
    std::filesystem::create_directories(empty, ec);
    MagicPortalsLayer::Paths paths = TestPaths();
    paths.art = empty.string();
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(paths, "level8");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    CHECK_MSG(!layer.ArtError().empty(), "the missing art is reported");
    CHECK_EQ(Tagged(registry, "Magic Portals Sprite"), 0);
    const int bodies = Tagged(registry, "Magic Portals Body");
    CHECK_MSG(bodies > 0 && Shown(registry, "Magic Portals Body") == bodies, "and every body is a box");
    layer.OnDetach(registry);
}

void APortalAndAShotAreTheOriginals() {
    // What no level pictures is drawn as the original's own entities draw it:
    // the shot as projectile.ent's six frames, added, and the portal it opens as
    // portal.ent's halo, added. Their boxes stand behind them.
    if (!OriginalArtIsThere("APortalAndAShotAreTheOriginals")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    // A level's first tap waits out the placement cooldown, as a player's does.
    waitForFirstTap(layer, registry);
    const glm::dvec2 target = playerPx(registry, layer) + glm::dvec2(0.0, -48.0);
    CHECK(onScreen(registry, target));
    tap(layer, registry, screenOf(registry, target));
    CHECK_MSG(layer.SimLevel()->portals.flight.has_value(), "the tap fired: " + lastFailure(layer));
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Sprite"), 1);
    bool shotIsTheOriginal = false;
    for (auto [entity, tag, material, animation] :
         registry.view<TagComponent, MaterialComponent, SpriteAnimationComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals Shot Sprite") continue;
        shotIsTheOriginal = material.blend == MaterialComponent::BlendMode::Additive &&
                            material.albedoTexturePath.find("projectile.png") != std::string::npos &&
                            animation.columns == 6 && animation.rows == 1 && animation.loop;
    }
    CHECK_MSG(shotIsTheOriginal, "projectile.ent's six frames, added, played round");
    CHECK_MSG(Shown(registry, "Magic Portals Shot") == 0, "and its box stands behind it");

    landShot(layer, registry);
    CHECK_MSG(layer.SimLevel()->portals.placed.size() == 1, "it landed: " + lastFailure(layer));
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Sprite"), 0);
    CHECK_EQ(Tagged(registry, "Magic Portals Portal Sprite"), 1);
    bool haloIsTheOriginal = false;
    for (auto [entity, tag, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (tag.tag == "Magic Portals Portal Sprite") {
            haloIsTheOriginal = material.blend == MaterialComponent::BlendMode::Additive &&
                                material.albedoTexturePath.find("portal_halo.png") != std::string::npos;
        }
    }
    CHECK_MSG(haloIsTheOriginal, "portal.ent's halo, added");
    CHECK_EQ(Shown(registry, "Magic Portals Portal"), 0);
    press(layer, registry, MagicPortalsLayer::kBoxes);
    CHECK_MSG(Shown(registry, "Magic Portals Portal") == 1, "B shows the portal's box too");
    layer.OnDetach(registry);
    CHECK_EQ(Tagged(registry, "Magic Portals Portal Sprite"), 0);
}

void WithoutTheOriginalThePortalIsABox() {
    // The level's art is there and the original's is not: the portal a shot
    // opens is its box, shown.
    const std::filesystem::path empty =
        std::filesystem::temp_directory_path() / "supersonic-test-mp-layer-no-original";
    std::error_code ec;
    std::filesystem::create_directories(empty, ec);
    MagicPortalsLayer::Paths paths = TestPaths();
    paths.original = empty.string();
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(paths, "level1");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    if (layer.SimLevel() == nullptr) return;
    waitForFirstTap(layer, registry);
    tap(layer, registry, screenOf(registry, playerPx(registry, layer) + glm::dvec2(0.0, -48.0)));
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Sprite"), 0);
    landShot(layer, registry);
    CHECK_MSG(layer.SimLevel()->portals.placed.size() == 1, "it landed: " + lastFailure(layer));
    CHECK_EQ(Tagged(registry, "Magic Portals Portal Sprite"), 0);
    CHECK_MSG(Shown(registry, "Magic Portals Portal") == 1, "the portal is its box");
    layer.OnDetach(registry);
}

void ThePlayerIsTheDarkMage() {
    // dark_mage.ent's sheet. A level starts on its start frame, standing. It
    // walks through a row of four while a direction is held, on the row that
    // direction reads, turns when it turns, and stands on the idle column of
    // the way it last walked. Which row and column are art.json's; only the
    // start frame is pinned here.
    if (!OriginalArtIsThere("ThePlayerIsTheDarkMage")) return;
    MagicPortals::Art::Rules rules;
    std::string error;
    CHECK_MSG(MagicPortals::Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", rules, error),
              error);
    const MagicPortals::Art::Character& mage = rules.character;
    const auto rowStart = [&mage](int row) { return static_cast<uint32_t>(row * mage.columns); };

    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level8");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    entt::entity mageQuad = entt::null;
    for (auto [entity, tag] : registry.view<TagComponent>().each()) {
        if (tag.tag == "Magic Portals Player Sprite") mageQuad = entity;
    }
    CHECK(mageQuad != entt::null);
    if (mageQuad == entt::null) return;
    const auto animation = [&registry, mageQuad]() -> const SpriteAnimationComponent& {
        return registry.get<SpriteAnimationComponent>(mageQuad);
    };
    const MaterialComponent& material = registry.get<MaterialComponent>(mageQuad);
    CHECK_MSG(material.albedoTexturePath.find("magic_portals_hd.png") != std::string::npos &&
                  material.blend == MaterialComponent::BlendMode::Alpha && animation().columns == 4 &&
                  animation().rows == 4,
              "dark_mage.ent's sheet, cut 4 x 4, mixed");
    CHECK_MSG(animation().firstFrame == 4 && animation().frameCount == 1 && !animation().playing,
              "standing on the start frame");
    CHECK_MSG(Shown(registry, "Magic Portals Player") == 0, "and its box stands behind it");
    const glm::dvec2 drawn = MagicPortals::Units::ToPixels(registry.get<TransformComponent>(mageQuad).position);
    const glm::dvec2 body = playerPx(registry, layer);
    CHECK_MSG(std::fabs(drawn.x - body.x) < 0.01 && std::fabs(drawn.y - (body.y - 2.0)) < 0.01,
              "its pivot, 2 px below the middle, on the body: " + Point(drawn) + " for " + Point(body));

    tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    CHECK_MSG(animation().firstFrame == rowStart(mage.rightRow) && animation().frameCount == 4 &&
                  animation().playing,
              "walking right, through the right row");
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(animation().firstFrame == rowStart(mage.rightRow) + static_cast<uint32_t>(mage.idleColumn) &&
                  animation().frameCount == 1 && !animation().playing,
              "standing, still facing right");
    tickWith(layer, registry, kRest, {MagicPortalsLayer::kLeft}, {});
    CHECK_MSG(animation().firstFrame == rowStart(mage.leftRow) && animation().frameCount == 4,
              "and turned to walk left");
    layer.OnDetach(registry);
    CHECK_EQ(Tagged(registry, "Magic Portals Player Sprite"), 0);
}

void Level31DrawsTheBeholder() {
    // beholder.ent's sheet where the beholder is, its eye open and its colour
    // whole, with the box of its reach behind the art. The player walks under
    // it: it shuts its eye, and the rock it drops is drawn as a stone.
    if (!OriginalArtIsThere("Level31DrawsTheBeholder")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level31");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.SimLevel()->boss.beholder) return;
    for (int tick = 0; tick < 5; ++tick) tickWith(layer, registry, kRest, {}, {});
    entt::entity quad = entt::null;
    for (auto [entity, tag] : registry.view<TagComponent>().each()) {
        if (tag.tag == "Magic Portals Beholder Sprite") quad = entity;
    }
    CHECK(quad != entt::null);
    if (quad == entt::null) return;
    const auto& material = registry.get<MaterialComponent>(quad);
    CHECK_MSG(material.albedoTexturePath.find("beholder.png") != std::string::npos, material.albedoTexturePath);
    CHECK(material.albedoColor == glm::vec4(1.0f));
    const auto& animation = registry.get<SpriteAnimationComponent>(quad);
    CHECK(animation.columns == 2u && animation.rows == 1u && animation.firstFrame == 0u);
    const glm::dvec2 at = layer.SimLevel()->boss.beholder->atPx;
    const glm::dvec2 drawn = MagicPortals::Units::ToPixels(registry.get<TransformComponent>(quad).position);
    CHECK_MSG(glm::distance(drawn, at) < 1e-3, "drawn at " + Point(drawn) + ", the beholder at " + Point(at));
    CHECK_EQ(Shown(registry, "Magic Portals Beholder"), 0);

    bool shut = false;
    for (int tick = 0; tick < 1800 && !shut && layer.SimLevel() != nullptr; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
        shut = layer.SimLevel()->boss.beholder->phase == MagicPortals::Boss::Phase::ThrowRock;
    }
    // The turn that shuts its eye is still a seeking turn, which draws the eye
    // open; the frame follows on the next one.
    tickWith(layer, registry, kRest, {}, {});
    const uint32_t frame = registry.get<SpriteAnimationComponent>(quad).firstFrame;
    CHECK_MSG(shut && frame == 1u, "under it, it shuts its eye: frame " + std::to_string(frame));
    bool dropped = false;
    for (int tick = 0; tick < 240 && !dropped && layer.SimLevel() != nullptr; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        dropped = !layer.SimLevel()->boss.rocks.empty();
    }
    CHECK_MSG(dropped && Tagged(registry, "Magic Portals Thrown Sprite") == 1, "its rock is drawn as a stone");
    layer.OnDetach(registry);
}

// ---- the menu ----------------------------------------------------------------

const MagicPortalsLayer::MenuButton* MenuButtonOf(const MagicPortalsLayer& layer,
                                                  MagicPortalsLayer::MenuButton::Kind kind, int which = 0) {
    int seen = 0;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != kind) continue;
        if (seen++ == which) return &button;
    }
    return nullptr;
}

int MenuButtonsOfKind(const MagicPortalsLayer& layer, MagicPortalsLayer::MenuButton::Kind kind) {
    int count = 0;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind == kind) ++count;
    }
    return count;
}

// Started with no level named, the game opens its menu, and the menu walks
// main -> chapters -> levels -> the level itself.
void TheMenuWalksToALevel() {
    using Screen = MagicPortalsLayer::Screen;
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    CHECK_MSG(layer.LoadError().empty(), layer.LoadError());
    CHECK(layer.MenuScreen() == Screen::Main);
    CHECK(layer.SimLevel() == nullptr);
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Play), 1);
    // The screen's own art is drawn, not quietly left out. The backgrounds live
    // among the original's entities rather than its sprites, and the first cut
    // of this looked for every menu image in one place: the menu came up with
    // nothing behind it and said nothing about why.
    CHECK_EQ(Tagged(registry, "Magic Portals Menu Background"), 1);
    CHECK_EQ(Tagged(registry, "Magic Portals Title"), 1);

    const MagicPortalsLayer::MenuButton* play = MenuButtonOf(layer, Kind::Play);
    if (play == nullptr) {
        CHECK_MSG(false, "the main screen has no play button");
        return;
    }
    layer.PressMenu(registry, *play);
    CHECK(layer.MenuScreen() == Screen::Worlds);
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::World), 4);

    const MagicPortalsLayer::MenuButton* world = MenuButtonOf(layer, Kind::World, 0);
    if (world == nullptr) {
        CHECK_MSG(false, "the chapter screen has no world button");
        return;
    }
    layer.PressMenu(registry, *world);
    CHECK(layer.MenuScreen() == Screen::Levels);
    // Four columns and FOUR rows to a page - sixteen - which is what
    // createLevelSelectState sets for levels and what the owner's screenshot of
    // the original shows. PageProperties' own 4 by 3 defaults are overridden.
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Level), 16);
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Forward), 1);

    const MagicPortalsLayer::MenuButton* first = MenuButtonOf(layer, Kind::Level, 0);
    if (first == nullptr) {
        CHECK_MSG(false, "the grid has no level button");
        return;
    }
    layer.PressMenu(registry, *first);
    CHECK(layer.MenuScreen() == Screen::None);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    CHECK(IsAt(layer, "level0"));
}

// The second page holds what the first does not, and the page buttons reach it.
void TheGridPagesThroughAWorld() {
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const MagicPortalsLayer::MenuButton* play = MenuButtonOf(layer, Kind::Play);
    if (play == nullptr) return;
    layer.PressMenu(registry, *play);
    const MagicPortalsLayer::MenuButton* world = MenuButtonOf(layer, Kind::World, 0);
    if (world == nullptr) return;
    layer.PressMenu(registry, *world);

    const MagicPortalsLayer::MenuButton* forward = MenuButtonOf(layer, Kind::Forward);
    if (forward == nullptr) {
        CHECK_MSG(false, "the grid has no forward button");
        return;
    }
    layer.PressMenu(registry, *forward);
    const MagicPortalsLayer::MenuButton* thirteenth = MenuButtonOf(layer, Kind::Level, 0);
    if (thirteenth == nullptr) {
        CHECK_MSG(false, "the second page has no level button");
        return;
    }
    layer.PressMenu(registry, *thirteenth);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    // Sixteen to a page, so the second page opens on the seventeenth level.
    CHECK_MSG(IsAt(layer, "level16"), std::string("the second page's first level is ") +
                                          (layer.Current() != nullptr ? layer.Current()->name : "none"));
}

// A named level is entered directly: --level, and every suite in this file,
// never see the menu.
void NamingALevelSkipsTheMenu() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::None);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    CHECK(layer.MenuButtons().empty());
}

// Escape leaves a level for the grid it came from.
void EscapeLeavesALevelForItsGrid() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::Levels);
    CHECK(layer.SimLevel() == nullptr);
    CHECK_EQ(MenuButtonsOfKind(layer, MagicPortalsLayer::MenuButton::Kind::Level), 16);
}

// And a click lands on the button under it, through the same camera mapping a
// tap in a level goes through.
void AClickOnTheMenuPressesWhatIsUnderIt() {
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    // One tick puts the camera on the menu's box, which is what a click is
    // read through.
    tickWith(layer, registry, kRest, {}, {});
    const MagicPortalsLayer::MenuButton* play = MenuButtonOf(layer, Kind::Play);
    if (play == nullptr) {
        CHECK_MSG(false, "the main screen has no play button");
        return;
    }
    // Read before the press: laying the screen out again clears the button.
    const glm::vec2 at = screenOf(registry, play->centrePx);
    tap(layer, registry, at);
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::Worlds);
}

// ---- the medal a finished level earns -----------------------------------------

// Finishing a level puts the medal screen up over it, rather than going
// straight on to the next - which is what the original does
// (GameStateController::writeScore raising a LevelFinishedLayer).
void FinishingALevelShowsTheMedal() {
    using Screen = MagicPortalsLayer::Screen;
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    // level0 is walked from its spawn to its exit, as test_mp_statics walks it.
    int tick = 0;
    for (; tick < 600 && layer.MenuScreen() == Screen::None; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    }
    CHECK_MSG(layer.MenuScreen() == Screen::Finished,
              "after " + std::to_string(tick) + " tick(s) the screen is not the medal");
    CHECK(layer.LastCleared().has_value());
    // The level stays behind the medal rather than being taken away.
    CHECK(layer.SimLevel() != nullptr);
    // Three buttons: play it again, go on, or pick another.
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Retry), 1);
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Next), 1);
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::List), 1);
    // And each is DRAWN, not merely listed. A kind buildMenu's switch does not
    // name gets no image and so no quad - an invisible button that counting
    // the buttons themselves would never catch. GCC's -Wswitch caught it once;
    // this catches it without a compiler's help.
    CHECK_EQ(Tagged(registry, "Magic Portals Menu Button"), 3);

    // And going on reaches the next level, with the screen gone.
    const MagicPortalsLayer::MenuButton* next = MenuButtonOf(layer, Kind::Next);
    if (next == nullptr) {
        CHECK_MSG(false, "the medal screen has no next button");
        return;
    }
    layer.PressMenu(registry, *next);
    CHECK(layer.MenuScreen() == Screen::None);
    CHECK_MSG(IsAt(layer, "level1"), std::string("it went on to ") +
                                         (layer.Current() != nullptr ? layer.Current()->name : "none"));
}

// ---- the entities' particles --------------------------------------------------

// A level's entities emit, and they emit on the FRAME rather than on the tick.
//
// The tick half matters as much as the drawing: a particle is a picture, and
// the port's determinism rests on nothing of the sort reaching the simulation.
// So a level that has ticked but never been drawn holds no particle at all.
void ALevelsEntitiesEmit() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    // Ticking alone draws none of them.
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(Tagged(registry, "Magic Portals Particle") == 0,
              "the tick drew " + std::to_string(Tagged(registry, "Magic Portals Particle")) + " particle(s)");

    // Frames do. level1 places a torch, a door and a static portal, and all
    // three carry particle systems in their .ent files.
    for (int frame = 0; frame < 120; ++frame) layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    const int drawn = Tagged(registry, "Magic Portals Particle");
    CHECK_MSG(drawn > 0, "no particle was drawn after two seconds of frames");
}

// And they go when the level does, rather than piling a second pool on the
// first - which is what a retry would otherwise do every time.
void ParticlesGoWithTheirLevel() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    for (int frame = 0; frame < 120; ++frame) layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    const int before = Tagged(registry, "Magic Portals Particle");
    CHECK(before > 0);

    // A retry rebuilds the level from what was read when it loaded.
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK_MSG(Tagged(registry, "Magic Portals Particle") == 0,
              "a retry left " + std::to_string(Tagged(registry, "Magic Portals Particle")) + " particle(s) behind");
    for (int frame = 0; frame < 120; ++frame) layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    CHECK_MSG(Tagged(registry, "Magic Portals Particle") <= before,
              "the level came back with more particles than it had");
}

// ---- what the camera drops while it pans --------------------------------------

// The owner reported sprites appearing and disappearing WHILE WALKING, with the
// window never moved or resized: one sprite on 1-2, several on 1-3.
//
// Eight readings of the renderer have been refused by the evidence, so this
// stops reading it and runs its OWN cull instead - the same frustum, the same
// bounds transform, the same intersection RenderSystem gathers with - and asks
// the one question the renderer cannot ask itself: was anything dropped while
// it was wholly on screen?
//
// The second opinion is independent of the frustum code, which is what makes a
// disagreement mean something: an orthographic camera shows a RECTANGLE, and a
// box inside that rectangle must be drawn.
void NoSpriteOnScreenIsCulled(const char* levelName) {
    using namespace Supersonic;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), levelName);
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }

    int inspected = 0;    // sprite-ticks showing on screen at all
    int wronglyCulled = 0;
    std::string firstWrong;

    for (int tick = 0; tick < 240; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (layer.MenuScreen() != MagicPortalsLayer::Screen::None) break; // it finished
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);

        // The world matrices the renderer would cull against. The app resolves
        // these before rendering; a test has to do it itself.
        TransformSystem::UpdateWorldTransforms(registry);

        const entt::entity cameraEntity = primaryCamera(registry);
        if (cameraEntity == entt::null) continue;
        CameraComponent camera = registry.get<CameraComponent>(cameraEntity);
        camera.aspect = 1280.0f / 720.0f;
        const Frustum frustum =
            Frustum::FromMatrix(camera.getProjectionMatrix() * camera.getViewMatrix());

        // What an orthographic camera shows, in world units.
        const float halfHeight = camera.orthoHeight * 0.5f;
        const float halfWidth = halfHeight * camera.aspect;
        const glm::vec2 viewMin(camera.position.x - halfWidth, camera.position.y - halfHeight);
        const glm::vec2 viewMax(camera.position.x + halfWidth, camera.position.y + halfHeight);

        auto view = registry.view<WorldTransformComponent, RenderableComponent>();
        for (auto entity : view) {
            const auto& world = view.get<WorldTransformComponent>(entity);
            const auto& renderable = view.get<RenderableComponent>(entity);
            if (!renderable.isVisible) continue;

            glm::vec3 worldMin;
            glm::vec3 worldMax;
            Frustum::TransformAABB(world.matrix, renderable.localBoundsMin, renderable.localBoundsMax,
                                   worldMin, worldMax);

            // SHOWING AT ALL: any overlap of the view rectangle, which is what
            // the renderer must not cull, with a margin of about a pixel at 50
            // px to the metre so nothing on the boundary is called a fault.
            //
            // This asked for WHOLLY INSIDE until it was rewritten, and that was
            // worse than useless: a sprite too big to fit on screen is never
            // wholly inside, so every one of them was passed over silently.
            // Most of a level's scenery is exactly that big, and scenery is
            // what the owner watched disappear. The test reported thousands of
            // clean sprite-ticks while never once looking at the suspects.
            const float margin = 0.02f;
            const bool overlaps = worldMax.x >= viewMin.x + margin && worldMin.x <= viewMax.x - margin &&
                                  worldMax.y >= viewMin.y + margin && worldMin.y <= viewMax.y - margin;
            if (!overlaps) continue;
            ++inspected;
            if (frustum.IntersectsAABB(worldMin, worldMax)) continue;

            ++wronglyCulled;
            if (firstWrong.empty()) {
                const auto* tag = registry.try_get<TagComponent>(entity);
                // The local bounds go in the message rather than a verdict on
                // them: a renderable whose mesh was not ready when it was
                // gathered keeps whatever box it was built with, and seeing the
                // numbers tells that apart from a frustum fault without this
                // test having to assume what the default is.
                const auto& lo = renderable.localBoundsMin;
                const auto& hi = renderable.localBoundsMax;
                firstWrong = (tag != nullptr ? tag->tag : std::string("unnamed")) + " on tick " +
                             std::to_string(tick) + ", local bounds (" + std::to_string(lo.x) + ", " +
                             std::to_string(lo.y) + ", " + std::to_string(lo.z) + ")..(" +
                             std::to_string(hi.x) + ", " + std::to_string(hi.y) + ", " +
                             std::to_string(hi.z) + ")";
            }
        }
    }

    std::printf("  %s: %d sprite-tick(s) on screen, %d culled\n", levelName, inspected, wronglyCulled);
    // The fixture must have looked at something, or this passes by seeing
    // nothing - which is how the menu's invisible buttons got through.
    CHECK_MSG(inspected > 50, std::string(levelName) + ": only " + std::to_string(inspected) +
                                  " sprite-tick(s) were on screen");
    CHECK_MSG(wronglyCulled == 0, std::string(levelName) + ": " + std::to_string(wronglyCulled) +
                                      " culled while on screen, first " + firstWrong);
}

// The sounds, with no audio device anywhere in sight.
//
// That is not an awkward corner to test around - it is how every suite runs,
// and it is the whole design: the TICK latches an event's name and the FRAME
// plays it, so a level must run identically whether or not anything can make a
// noise. What is asserted here is the latching, which is the half that has to
// be right for the other half to have anything to play.
// A carranca's fireball is seen and heard.
//
// The sim suite already pins that one is spat and that it burns the player
// (test_mp_turrets). What that cannot see is the half a player actually meets:
// a fireball that kills while drawing nothing and making no sound is not a
// missing feature, it is an invisible one, which is worse - it looks exactly
// like a bug in the simulation.
//
// The ORDER here is the point. tickWith runs the fixed update only, so what the
// ticks latch piles up; OnUpdate is the frame, and it both draws and drains that
// queue. Read the queue first and draw second, or the assertion lands on a queue
// the frame has already emptied and passes for the wrong reason.
void ACarrancasFireballIsSeenAndHeard() {
    entt::registry registry;
    publishViewport(registry);
    // level0a is chapter 2's first level: one carranca, a stride of 1500 ms and
    // a startStride of 0, so its first fireball comes 90 ticks in.
    MagicPortalsLayer layer(TestPaths(), "level0a");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }

    // Just past the first shot, and no further: the fireball flies right from
    // the gargoyle, and a run long enough for it to reach the player would
    // retry the level and reset everything this asserts.
    for (int tick = 0; tick < 100; ++tick) tickWith(layer, registry, kRest, {}, {});

    const std::vector<std::string> latched = layer.LatchedSounds();
    bool heard = false;
    for (const std::string& name : latched) {
        if (name == "fireball_spat") heard = true;
    }
    CHECK_MSG(heard, "a carranca spitting is a sound, and sounds.json names it playFireballSound");

    // THEN the frame, which is what draws it.
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    CHECK_MSG(Tagged(registry, "Magic Portals Fireball") > 0,
              "and a fireball in flight is drawn, or it kills out of nowhere");

    // And it goes with the level, as every other drawable does.
    layer.OnDetach(registry);
    CHECK_MSG(Tagged(registry, "Magic Portals Fireball") == 0, "the fireballs go with the level");
}

void ALevelLatchesTheSoundsItEarns() {
    entt::registry registry;
    publishViewport(registry);
    // No AudioEngine is ever put in the registry's context.
    //
    // level0 rather than level1, and not arbitrarily: test_mp_statics already
    // reports "level0 holding right: completed after 3.20 s, 2 traversal(s)",
    // so this fixture is known to reach its exit AND to go through a portal on
    // the way. Asserting a teleport in a level that might not have one would be
    // a guess dressed up as a test.
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    CHECK_MSG(layer.SoundsError().empty(), "the port's sounds.json should read: " + layer.SoundsError());

    // Walk right to the exit, WITHOUT a frame in between: nothing plays, so
    // what the ticks latched piles up to be looked at.
    for (int tick = 0; tick < 600; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (layer.MenuScreen() != MagicPortalsLayer::Screen::None) break; // the medal is up
    }

    const std::vector<std::string> latched = layer.LatchedSounds();
    const auto heard = [&latched](const char* event) {
        for (const std::string& name : latched) {
            if (name == event) return true;
        }
        return false;
    };
    std::printf("  level0 walked to its exit: %d sound(s) latched\n", static_cast<int>(latched.size()));
    CHECK_MSG(!latched.empty(), "a level played through should latch something");
    CHECK_MSG(heard("traversal"), "going through a portal is a teleport sound");
    CHECK_MSG(heard("level_finished"), "reaching the exit is a sound");
    CHECK_MSG(heard("medal_shown"), "the medal screen is a sound");

    // And the frame drops them rather than keeping them: a queue nothing ever
    // plays is a leak with a delay on it.
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    CHECK_MSG(layer.LatchedSounds().empty(), "a frame with no device should still clear what it cannot play");
}

// Walking, drawn the way the APP draws it - which no test here has ever done.
//
// The cull test above runs one OnUpdate per OnFixedUpdate and never calls
// InterpolationSystem at all, so every frame it inspects is drawn from a raw
// tick pose with no interpolation in it. The app's loop is not that:
//
//   BeginTick -> physics -> OnFixedUpdate -> EndTick     (only when a tick is due)
//   alpha = the remainder
//   Apply(alpha) -> OnUpdate -> world transforms         (EVERY frame)
//
// At any frame rate above the port's 60 Hz tick, MOST frames run no tick at
// all and are drawn entirely by Apply from a lerped pose. That is the path the
// owner actually watches, and until this test it was the one path nothing
// exercised. A fault living there is invisible to the cull test by
// construction, and would show only while things move - which is exactly the
// report: sprites that come and go WHILE WALKING.
//
// So this asserts the symptom itself rather than a proxy for it: a sprite on
// screen in one frame, gone the next, and back the frame after. Position jumps
// are measured too but only reported - a crate or the player going through a
// portal is entitled to jump, and an assertion that cannot tell the two apart
// would cry wolf.
void NoSpriteBlinksWhileWalking(const char* levelName) {
    using namespace Supersonic;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), levelName);
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }

    // Four frames a tick: 240 Hz against 60, so three frames in four are drawn
    // from an interpolated pose and none of them from a tick's own.
    constexpr int kFramesPerTick = 4;
    constexpr float kFrameDelta = MagicPortalsLayer::kTick / static_cast<float>(kFramesPerTick);

    std::vector<entt::entity> onScreen;     // this frame
    std::vector<entt::entity> lastFrame;    // the one before
    std::vector<entt::entity> frameBefore;  // and the one before that
    std::vector<std::pair<entt::entity, glm::vec3>> lastCentre;

    const auto has = [](const std::vector<entt::entity>& list, entt::entity e) {
        for (const entt::entity other : list) {
            if (other == e) return true;
        }
        return false;
    };

    int frames = 0;
    int inspected = 0; // live sprite quads seen, summed over frames - the population, not the view
    int blinked = 0;
    int jumped = 0;
    int entered = 0; // times a sprite came into view
    int left = 0;    // and went out of it
    std::string firstBlink;

    // The geometry this runs in, printed because the crossing counts below
    // cannot be read without it: a level not much wider than the view has
    // nowhere for a sprite to go.
    const glm::dvec2 bounds = layer.BoundsPx();
    const glm::dvec2 viewPx = layer.ViewPx();
    std::printf("  %s: level %.0f x %.0f px, view %.0f x %.0f px\n", levelName, bounds.x, bounds.y, viewPx.x,
                viewPx.y);

    // SWEEPING, not walking one way, and the first run of this test is why.
    // Holding right gave level1 two crossings of the view's edge in four
    // seconds - because the player walks into something and stops, and a
    // stopped player is a stopped camera. Nothing crosses an edge after that,
    // so a blink detector has almost nothing to detect and reports a
    // comfortable zero.
    //
    // Two seconds each way instead. The camera pans back and forth over the
    // same scenery, which is what the owner was doing in the dungeon, and it
    // manufactures the one thing a blink needs: a sprite that leaves the view
    // and comes back.
    constexpr int kLeg = 120;
    const char* held = MagicPortalsLayer::kRight;
    for (int tick = 0; tick < 1200; ++tick) {
        const bool turn = (tick % kLeg) == 0;
        if (turn) {
            held = ((tick / kLeg) % 2 == 0) ? MagicPortalsLayer::kRight : MagicPortalsLayer::kLeft;
        }
        InterpolationSystem::BeginTick(registry);
        std::vector<std::string> pressed;
        if (turn) pressed.emplace_back(held);
        tickWith(layer, registry, kRest, {held}, std::move(pressed));
        InterpolationSystem::EndTick(registry);
        if (layer.MenuScreen() != MagicPortalsLayer::Screen::None) break; // it finished

        for (int f = 1; f <= kFramesPerTick; ++f) {
            InterpolationSystem::Apply(registry, static_cast<float>(f) / static_cast<float>(kFramesPerTick));
            layer.OnUpdate(registry, kFrameDelta);
            TransformSystem::UpdateWorldTransforms(registry);
            ++frames;

            const entt::entity cameraEntity = primaryCamera(registry);
            if (cameraEntity == entt::null) continue;
            CameraComponent camera = registry.get<CameraComponent>(cameraEntity);
            camera.aspect = 1280.0f / 720.0f;
            const float halfHeight = camera.orthoHeight * 0.5f;
            const float halfWidth = halfHeight * camera.aspect;
            const glm::vec2 viewMin(camera.position.x - halfWidth, camera.position.y - halfHeight);
            const glm::vec2 viewMax(camera.position.x + halfWidth, camera.position.y + halfHeight);

            onScreen.clear();
            auto view = registry.view<WorldTransformComponent, RenderableComponent, TagComponent>();
            for (auto entity : view) {
                // The level's own art only. The boxes are shown and hidden
                // deliberately, and the particles come and go by design.
                if (view.get<TagComponent>(entity).tag != std::string("Magic Portals Sprite")) continue;
                const auto& renderable = view.get<RenderableComponent>(entity);
                if (!renderable.isVisible) continue;

                glm::vec3 worldMin;
                glm::vec3 worldMax;
                Frustum::TransformAABB(view.get<WorldTransformComponent>(entity).matrix,
                                       renderable.localBoundsMin, renderable.localBoundsMax, worldMin, worldMax);
                ++inspected;

                const glm::vec3 centre = (worldMin + worldMax) * 0.5f;
                bool found = false;
                for (auto& [known, was] : lastCentre) {
                    if (known != entity) continue;
                    found = true;
                    // Half a metre is 25 px, and nothing here moves that fast:
                    // the player walks at 112 px/s, a stone flies at 300, which
                    // is about a pixel a frame at this rate.
                    if (glm::length(centre - was) > 0.5f) ++jumped;
                    was = centre;
                    break;
                }
                if (!found) lastCentre.emplace_back(entity, centre);

                if (worldMax.x >= viewMin.x && worldMin.x <= viewMax.x && worldMax.y >= viewMin.y &&
                    worldMin.y <= viewMax.y) {
                    onScreen.push_back(entity);
                }
            }

            // HOW OFTEN A SPRITE CROSSED THE EDGE AT ALL, which decides whether
            // the count below means anything. A blink is on-off-on, so a walk
            // where nothing ever leaves or re-enters the view cannot produce
            // one however broken the renderer is - and would report a
            // reassuring zero. Counted from the second frame, because the first
            // has nothing to be compared against and every sprite would read as
            // having just entered.
            if (frames > 1) {
                for (const entt::entity entity : onScreen) {
                    if (!has(lastFrame, entity)) ++entered;
                }
                for (const entt::entity entity : lastFrame) {
                    if (!has(onScreen, entity)) ++left;
                }
            }

            // On, off, on again: the owner's report, stated as an assertion.
            for (const entt::entity entity : onScreen) {
                if (has(lastFrame, entity) || !has(frameBefore, entity)) continue;
                ++blinked;
                if (firstBlink.empty()) {
                    firstBlink = "entity " + std::to_string(static_cast<unsigned>(entt::to_integral(entity))) +
                                 " on frame " + std::to_string(frames);
                }
            }
            frameBefore = lastFrame;
            lastFrame = onScreen;
        }
    }

    std::printf("  %s: %d frame(s), %d live sprite-frame(s), %d entered, %d left, %d blink(s), %d jump(s)\n",
                levelName, frames, inspected, entered, left, blinked, jumped);
    CHECK_MSG(inspected > 50, std::string(levelName) + ": only " + std::to_string(inspected) +
                                  " sprite-frame(s) were drawn");
    // Without a crossing there is no blink to find, and a zero above would be
    // the fixture's silence rather than the renderer's health.
    CHECK_MSG(entered + left > 0, std::string(levelName) +
                                      ": no sprite ever crossed the edge of the view, so this proves nothing");
    CHECK_MSG(blinked == 0, std::string(levelName) + ": " + std::to_string(blinked) +
                                " sprite(s) went away and came back, first " + firstBlink);
}

// The medal screen, as LevelFinishedLayer builds it.
//
// What is pinned here is the DECODE, not a preference. The port drew three
// buttons down the right at x 0.82, read off a screenshot; the original's
// constructor states addButton at (0.25, 0.75), (0.5, 0.75) and (0.75, 0.75) -
// one row across the bottom. And its draw recomputes the medal from the
// counter's current value every frame, so the medal CLIMBS as the number rises
// rather than being stamped at the end. A static final medal is wrong in a way
// no screenshot would show.
void TheMedalScreenIsTheOriginals() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }

    // level0 is the fixture test_mp_statics proves reaches its exit by holding
    // right: "completed after 3.20 s, 2 traversal(s)".
    for (int tick = 0; tick < 600; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (layer.MenuScreen() == MagicPortalsLayer::Screen::Finished) break;
    }
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::Finished,
              "holding right through level0 must finish it and raise the medal");
    if (layer.MenuScreen() != MagicPortalsLayer::Screen::Finished) return;

    // THREE BUTTONS, IN ONE COLUMN. The x is the decoded 0.75 of the view and
    // the three y's are a quarter, a half and three quarters of it.
    //
    // This test asserted the OPPOSITE until the argument order was settled: it
    // demanded a row and called the column "exactly what the column got wrong".
    // The layout and the test were both mine and both wrong the same way -
    // LevelFinishedLayer's addButton pairs read (y, x), because AngelScript
    // pushes a call's arguments last-first. The veil in that same constructor
    // is the proof: it pushes screenSize.y and then screenSize.x * 1.5, and a
    // dimming veil is one and a half screens WIDE, not that many tall.
    //
    // Asserted as invariants rather than as absolute pixels: a shared x, a
    // strictly increasing y, and an EQUAL gap between them - which is what a
    // quarter, a half and three quarters are, and what a row cannot satisfy.
    const std::vector<MagicPortalsLayer::MenuButton>& buttons = layer.MenuButtons();
    CHECK_MSG(buttons.size() == std::size_t{3}, "the medal screen has three buttons, got " +
                                                    std::to_string(buttons.size()));
    if (buttons.size() == std::size_t{3}) {
        CHECK_MSG(::test::nearly(static_cast<float>(buttons[0].centrePx.x),
                                 static_cast<float>(buttons[1].centrePx.x)) &&
                      ::test::nearly(static_cast<float>(buttons[1].centrePx.x),
                                     static_cast<float>(buttons[2].centrePx.x)),
                  "all three share a column, or the row is back");
        CHECK_MSG(buttons[0].centrePx.y < buttons[1].centrePx.y &&
                      buttons[1].centrePx.y < buttons[2].centrePx.y,
                  "and run top to bottom: restart, next, list");
        CHECK_MSG(::test::nearly(static_cast<float>(buttons[1].centrePx.y - buttons[0].centrePx.y),
                                 static_cast<float>(buttons[2].centrePx.y - buttons[1].centrePx.y)),
                  "evenly spaced, as a quarter, a half and three quarters are");
    }

    // The screen's furniture is drawn, not just the medal. The veil, the
    // banner and the portals plaque are there whatever the play earned.
    CHECK_MSG(Tagged(registry, "Magic Portals Finish Veil") == 1, "the dimming veil is drawn");
    CHECK_MSG(Tagged(registry, "Magic Portals Finish Banner") == 1, "and the level-finished banner");
    CHECK_MSG(Tagged(registry, "Magic Portals Portals Plaque") == 1, "and the portals-spent plaque");
    CHECK_MSG(Tagged(registry, "Magic Portals Medal") == 1, "and the medal itself");

    // AND THE VEIL ACTUALLY DIMS. The original draws it at ARGB(200,255,255,255)
    // and the port drew it opaque white, so a gradient meant to sink the level
    // behind the medal read as barely a tint - a fidelity bug that looks exactly
    // like a deliberately subtle design and so would never be reported as one.
    // Checked on the material rather than by eye for that reason.
    if (const entt::entity veil = FirstTagged(registry, "Magic Portals Finish Veil");
        veil != entt::null && registry.all_of<MaterialComponent>(veil)) {
        CHECK_MSG(::test::nearly(registry.get<MaterialComponent>(veil).albedoColor.a, 200.0f / 255.0f),
                  "the veil carries the original's alpha of 200, not an opaque white");
    }

    // AND THEY GO WHEN THE SCREEN DOES. buildMenu runs again on every window
    // resize, so a screen whose decorations are not torn down stacks another
    // veil each time and darkens a shade at a time.
    layer.OnDetach(registry);
    CHECK_MSG(Tagged(registry, "Magic Portals Finish Veil") == 0,
              "the veil goes with the screen, or a resize stacks another");
    CHECK_MSG(Tagged(registry, "Magic Portals Medal") == 0, "and so does the medal");
}

// A level that has been cleared wears its medal on the grid. Nothing else does.
//
// This is the owner's own report - "i dont see any mdels at the level select" -
// as a test. It is worth an end-to-end case rather than a unit one because the
// symptom is invisible: a grid of bare buttons looks exactly like a grid nobody
// has played yet, so the feature can disappear without anyone noticing.
//
// The layer is built BARE, as every layer suite is: Paths::saveDir is empty, so
// the store is memory-only and this test writes nothing to any disk. What it
// proves is the path from clearing a level to a medal on a button, which does
// not need a file to exist.
void TheGridShowsTheMedalsEarned() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }

    for (int tick = 0; tick < 600; ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (layer.MenuScreen() == MagicPortalsLayer::Screen::Finished) break;
    }
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::Finished,
              "holding right through level0 must finish it");
    if (layer.MenuScreen() != MagicPortalsLayer::Screen::Finished) return;

    // Out to the list, by the medal screen's own third button.
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::List) continue;
        layer.PressMenu(registry, button);
        break;
    }
    // Through the chapters, if that is where it landed: which screen the list
    // button opens is not what this case is about.
    if (layer.MenuScreen() == MagicPortalsLayer::Screen::Worlds) {
        for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
            if (button.kind != MagicPortalsLayer::MenuButton::Kind::World || button.world != 0) continue;
            layer.PressMenu(registry, button);
            break;
        }
    }
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::Levels,
              "the level grid is reachable from the medal screen");
    if (layer.MenuScreen() != MagicPortalsLayer::Screen::Levels) return;

    // EXACTLY ONE. That it is one rather than "at least one" is the point: it
    // says the level just cleared gained a medal AND that the fifteen nobody
    // has finished did not, which is the guard the original draws on
    // `getScore != 0`.
    CHECK_MSG(Tagged(registry, "Magic Portals Menu Medal") == 1,
              "the level just cleared wears a medal on the grid, and only it does");

    // ON THE BUTTON, not beside it.
    //
    // buildMenu creates the medal but menuTick places it, so this needs a tick
    // before there is a position to read at all - without one the transform is
    // still at the origin and the check below would be measuring nothing.
    tickWith(layer, registry, kRest, {}, {});

    const entt::entity medal = FirstTagged(registry, "Magic Portals Menu Medal");
    const MagicPortalsLayer::MenuButton* wearer = nullptr;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::Level || button.level != 0) continue;
        wearer = &button;
        break;
    }
    CHECK_MSG(wearer != nullptr, "level0 has a button on the grid");
    if (medal != entt::null && wearer != nullptr && registry.all_of<TransformComponent>(medal)) {
        // The original draws it from the button's TOP-LEFT plus (36, 36) of a
        // 64px button, so its centre lands 20 in from the button's own centre
        // against a half-width of 32 - a badge on the corner. Measured from the
        // centre instead it came out at 52, which is a medal floating in the
        // gap beside the button touching nothing. This tells those two apart,
        // which counting the medals cannot.
        // MagicPortals::Units, qualified: this file brings in Supersonic and
        // the layer by name, not the whole of the game's namespace.
        const glm::dvec2 at =
            MagicPortals::Units::ToPixels(registry.get<TransformComponent>(medal).position);
        const double dx = std::fabs(at.x - wearer->centrePx.x);
        const double dy = std::fabs(at.y - wearer->centrePx.y);
        CHECK_MSG(dx < wearer->sizePx.x * 0.5 && dy < wearer->sizePx.y * 0.5,
                  "the medal sits on its button rather than floating beside it");
    }

    layer.OnDetach(registry);
    CHECK_MSG(Tagged(registry, "Magic Portals Menu Medal") == 0,
              "and the medal goes with the screen, or a resize stacks another");
}

void NothingBlinksWhileWalking() {
    NoSpriteBlinksWhileWalking("level1"); // 1-2, where the owner saw one go
    NoSpriteBlinksWhileWalking("level2"); // 1-3, where several do
}

void NothingOnScreenIsCulledWhileWalking() {
    NoSpriteOnScreenIsCulled("level1"); // 1-2, where the owner saw one go
    NoSpriteOnScreenIsCulled("level2"); // 1-3, where several do
}

void runTests() {
    TheLayerPlaysLevel30();
    Level31DrawsTheBeholder();
    ATapLandsWhereItPoints();
    Level8FromTheSpawnWithTapsAndWalking();
    LevelsFollowInOrderAndRetryIsInstant();
    NSkipsWhatThePortRefuses();
    DeathIsAnInstantRetry();
    TheLevelsArtIsDrawn();
    AStaticPortalGlows();
    WithoutTheArtTheLevelIsBoxes();
    APortalAndAShotAreTheOriginals();
    WithoutTheOriginalThePortalIsABox();
    ThePlayerIsTheDarkMage();
    ABrokenWallTakesItsBoxWithIt();
    ARetryTakesTheThrownStonesAway();
    TheChapterEnds();
    AStartThatIsNoLevelSaysSo();
    TheMenuWalksToALevel();
    TheGridPagesThroughAWorld();
    NamingALevelSkipsTheMenu();
    EscapeLeavesALevelForItsGrid();
    AClickOnTheMenuPressesWhatIsUnderIt();
    ALevelsEntitiesEmit();
    ParticlesGoWithTheirLevel();
    FinishingALevelShowsTheMedal();
    TheMedalScreenIsTheOriginals();
    TheGridShowsTheMedalsEarned();
    ALevelLatchesTheSoundsItEarns();
    ACarrancasFireballIsSeenAndHeard();
    NothingOnScreenIsCulledWhileWalking();
    NothingBlinksWhileWalking();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) || !std::filesystem::is_regular_file(kChapters, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec) ||
        !std::filesystem::is_regular_file(kData + "/player.json", ec) ||
        !std::filesystem::is_regular_file(kData + "/portals.json", ec)) {
        std::printf("test_mp_layer: SKIPPED - needs the converted levels at %s,\n"
                    "  chapters.json at %s and the remake's data at %s.\n"
                    "  All live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=..., _CHAPTERS=... and _DATA=...\n",
                    kLevels.c_str(), kChapters.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_layer", 35);
}
