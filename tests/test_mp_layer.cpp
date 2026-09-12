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
#include "core/PhysicsSystem.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Art.hpp"
#include "sim/Units.hpp"

#include <cmath>
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

// Ticks until the shot a tap fired has landed or failed (Portals.hpp).
void landShot(MagicPortalsLayer& layer, entt::registry& registry) {
    for (int tick = 0; tick < 240 && layer.SimLevel() != nullptr && layer.SimLevel()->portals.flight; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
    }
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

int Tagged(entt::registry& registry, const char* tag) {
    int count = 0;
    for (auto [entity, t] : registry.view<TagComponent>().each()) {
        (void)entity;
        if (t.tag == tag) ++count;
    }
    return count;
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
        if (!IsAt(layer, "level8")) clearedAt = tick;
    }
    const auto& cleared = layer.LastCleared();
    std::printf("  played through: 1-9 cleared %.2f s after right went down, %d of %d crystals, %d portals\n",
                clearedAt * static_cast<double>(MagicPortalsLayer::kTick), cleared ? cleared->crystals : -1,
                cleared ? cleared->crystalsTotal : -1, cleared ? cleared->portalsUsed : -1);
    CHECK_MSG(clearedAt > 0, "the player reaches the exit");
    CHECK_MSG(cleared && cleared->name == "level8" && cleared->label == "1-9" && cleared->portalsUsed == 2 &&
                  cleared->traversals >= 1,
              "through the pair the two shots opened");
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
    for (int tick = 0; tick < 600 && IsAt(layer, "level0"); ++tick) {
        std::vector<std::string> pressed;
        if (tick == 0) pressed.push_back(MagicPortalsLayer::kRight);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, std::move(pressed));
        if (IsAt(layer, "level0")) inside = inside && viewInside(layer);
    }
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
