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
//    walking alone, tapping only what the screen shows;
//  - every sprite of a level is drawn at its colour times min(1, ambient +
//    emissive): 1-1's arches at its ambient, a dark level's scenery at 0.01, and
//    a level's lightmaps are handed back when it goes (step 45).
//  - a level's <Light>s are 2D lights with their halos, following their owners,
//    a sprite takes the lights its mask lets through, and a shot carries its own
//    light while it flies (step 49).
//
// No window and no Vulkan. The app steps physics once before each layer tick at
// 60 Hz, so this suite does the same.

#include "TestHarness.hpp"

#include "MagicPortalsLayer.hpp"

#include "core/Application.hpp"
#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/InterpolationSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/RenderSettings.hpp"
// For the renderer's own cull: RenderSystem.hpp carries renderer/Frustum.hpp.
#include "core/RenderSystem.hpp"
#include "core/ScreenOverlay.hpp"
#include "core/TransformSystem.hpp"
#include "core/SimulationClock.hpp"
#include "core/SpriteAnimationSystem.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Art.hpp"
#include "sim/GravityWell.hpp"
#include "sim/LevelEnd.hpp"
#include "sim/Lighting.hpp"
#include "sim/Loading.hpp"
#include "sim/MainMenu.hpp"
#include "sim/MenuState.hpp"
#include "sim/Pause.hpp"
#include "sim/Popup.hpp"
#include "sim/Selector.hpp"
#include "sim/UiLayer.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace Supersonic;
using MagicPortals::MagicPortalsLayer;
namespace Selector = MagicPortals::Selector;
namespace Hud = MagicPortals::Hud;

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

// 1-02 and 1-03 raise their tutorial popup as they load, and it stops the level -
// no walk, no tap, no particle - until a touch closes it and its fade has run
// (sim/Popup.hpp). A case that means to play either level closes it first, as a
// player does, and the level then plays from an age of zero exactly as it did
// before popups were built. Returns the ticks it took.
int CloseTheLevelStartPopup(MagicPortalsLayer& layer, entt::registry& registry) {
    if (!layer.PopupOpen()) return 0;
    tap(layer, registry, kRest);
    int ticks = 1;
    while (layer.PopupOpen() && ticks < 180) {
        tickWith(layer, registry, kRest, {}, {});
        ++ticks;
    }
    CHECK_MSG(!layer.PopupOpen(), "the level-start popup closes on a touch and is gone after its fade");
    return ticks;
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

// Defined further down, beside the checks that first needed it.
bool EndsWith(const std::string& path, const std::string& file);

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

std::vector<ScreenOverlay::Quad> HudFrame(entt::registry& registry, const MagicPortalsLayer& layer);
int IndexOfImage(const std::vector<ScreenOverlay::Quad>& quads, const std::string& file);
int CountImage(const std::vector<ScreenOverlay::Quad>& quads, const std::string& file);
int CountCaption(const std::vector<ScreenOverlay::Quad>& quads, const std::string& font);
glm::vec2 ScreenOfView(const MagicPortalsLayer& layer, const glm::dvec2& onView);
bool NearD(double a, double b, double eps);

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
    // The camera starts ON THE PLAYER, which is the owner's choice and not the
    // original's: level30 places a camera_start at (540, 0) and the port used to
    // open there and ease over. It now opens at the spawn, (182, 203), held
    // inside the level - the view is view.json's 256 px tall and 455 px wide at
    // 16:9, and level30 is 768 x 256, so the centre clamps to (227.5, 128) and
    // the view shows x 0..455. MagicPortalsLayer::loadLevel says why.
    //
    // Its centre shows at the viewport's, the level's top is up the screen -
    // screen y grows down, as the remake's does, where the engine's grows up -
    // and a tap at a point on the screen is read on the tick, back through
    // ScreenPointToRay. Its shot flies from the player, and a portal opens where
    // it points. A tap whose shot meets a crate on the way opens nothing and
    // costs nothing.
    //
    // The aims moved with the camera. (600, 128) was the old blocked shot and is
    // no longer on screen at all; crate_969 stands at (352, 192) and still is,
    // so it is still what a shot dies on, and the two that land are picked from
    // what the new view shows. Every one of them is checked with onScreen before
    // it is tapped, which is what a player can do and the point of this suite.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level30");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);

    // DERIVED, not a literal. level30's spawn is at x 182, which is left of half
    // the view, so the clamp pins the camera to the level's left edge and the
    // centre is exactly half a view in. That is 227.5555... and not 227.5: the
    // view is 256 * (1280/720) = 455.111... px wide, and a rounded literal here
    // failed by five hundredths of a pixel.
    const glm::dvec2 expected(layer.ViewPx().x * 0.5, 128.0);
    CHECK_MSG(glm::length(layer.CameraCentrePx() - expected) < 0.01,
              "the camera starts at " + Point(layer.CameraCentrePx()) + ", wanted " + Point(expected));
    CHECK(layer.ViewPx().y == 256.0);
    const glm::vec2 centre = screenOf(registry, layer.CameraCentrePx());
    CHECK_MSG(glm::length(centre - glm::vec2(640.0f, 360.0f)) < 0.5f,
              "the camera's centre shows at the viewport's: at (" + std::to_string(centre.x) + ", " +
                  std::to_string(centre.y) + ")");
    // Projected coordinates, not visibility: these two only compare where points
    // land against the centre, so a point off the side of the view still orders
    // correctly. The x probe moved in with the camera all the same.
    CHECK_MSG(screenOf(registry, glm::dvec2(layer.CameraCentrePx().x, 64.0)).y < centre.y &&
                  screenOf(registry, glm::dvec2(100.0, 128.0)).x < centre.x,
              "the level's top is up the screen, and its left is to the left");

    // A level's first tap waits out the placement cooldown, as a player's does.
    waitForFirstTap(layer, registry);

    // From the spawn, (182, 203), a shot at crate_969 - a RigidBody2D at
    // (352, 192), just to the player's right and at nearly its own height -
    // runs into it and opens nothing.
    const glm::dvec2 blocked(352.0, 192.0);
    CHECK_MSG(onScreen(registry, blocked), Point(blocked) + " is on screen");
    tap(layer, registry, screenOf(registry, blocked));
    landShot(layer, registry);
    CHECK(layer.SimLevel()->portals.placed.empty());
    CHECK_MSG(lastFailure(layer) == "crate_969", "the shot stopped at " + lastFailure(layer));
    CHECK_EQ(layer.SimLevel()->portals.portalsUsed, 0);

    // Two that land, in the CORRIDOR between what is above and what is below.
    //
    // The first attempt aimed high, at (300, 40) and (340, 40), and both shots
    // died on block00_ent_981 - a 64 x 64 polygon centred on (256, 96), so it
    // fills x 224..288 and y 64..128, and a segment from the spawn up to y 40
    // goes straight through it. What matters is the whole PATH from (182, 203),
    // not whether the endpoint happens to be empty.
    //
    // These two run under that block and over the crates, whose tops are at
    // y 163 (58 x 58 boxes centred on (352, 192) and (416, 192), so x 323..381
    // and x 387..445). At x 288 - the block's right edge - the flatter of the
    // two is still at y 173, well below its y 128 underside, and both stop short
    // of x 323 where the first crate begins.
    const glm::dvec2 aims[] = {glm::dvec2(250.0, 150.0), glm::dvec2(300.0, 170.0)};
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
    // THE COUNTER, from the tick the screen came up (spec 3.3, A-F12): nothing
    // at t0, one 100 ms on and two at 200, and there it holds. 2 against a
    // golden score of 2 is gold throughout, so the medal never changes.
    if (clearedAt > 0) {
        CHECK_EQ(layer.EndScreenClockMs(), 0.0);
        CHECK_EQ(layer.PortalsCounted(), 0);
        std::string counted;
        for (int tick = 1; tick <= 30; ++tick) {
            tickWith(layer, registry, kRest, {}, {});
            if (tick == 3 || tick == 9 || tick == 15 || tick == 30) counted += std::to_string(layer.PortalsCounted());
        }
        CHECK_MSG(counted == "0122", "at 50, 150, 250 and 500 ms the count reads " + counted);
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        CHECK_MSG(IndexOfImage(frame, "medal_gold_l.png") >= 0 && IndexOfImage(frame, "golden_score_plaque.png") < 0,
                  "gold at 2 of 2, and so no golden plaque (D6)");
        // F11 and F12 through the layer: the crystal from entities/hd and its
        // count in Matura84_shadow, which on gold (no golden number) can only
        // be "3/3", counted up in 300 ms.
        CHECK_EQ(layer.CrystalsCounted(), 3);
        CHECK_MSG(IndexOfImage(frame, "crystal.png") >= 0, "the crystal is in the frame");
        CHECK_MSG(CountCaption(frame, "Matura84_shadow.fnt") == 3,
                  "and its count is three glyphs: " + std::to_string(CountCaption(frame, "Matura84_shadow.fnt")));
        CHECK_MSG(IndexOfImage(frame, "crystal.png") > IndexOfImage(frame, "medal_gold_l.png"),
                  "drawn after the medal it hangs from");
    }
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

    // level1 as it loads: the player at its spawn, (448, 110), under its popup.
    const glm::dvec2 spawn(448.0, 110.0);
    CHECK_MSG(glm::distance(playerPx(registry, layer), spawn) < 1.0, "at " + Point(playerPx(registry, layer)));
    CHECK_MSG(layer.PopupOpen(), "1-02 opens with its tutorial popup, reached from 1-1 as from anywhere");
    CloseTheLevelStartPopup(layer, registry);

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

void NSkipsToTheNextLevel() {
    // THERE IS NOTHING LEFT FOR THE PORT TO REFUSE.
    //
    // This test twice named a level the port turned away - level26c for
    // `darkest`, then level18c for `no_gravity` - and asserted both that the skip
    // got past it and that a refusal left nothing behind. All 128 levels start
    // now, so no such pair exists anywhere in the game and the refusal half of it
    // cannot be written at all. It is not re-pointed a third time; it is reshaped.
    //
    // The half that was always the real point survives: moving on UNLOADS what
    // came before. It is measured against a FRESH attach of the same level, so a
    // level left behind shows up as a count that does not match - which is what
    // the old "only the camera has a transform" line was reaching for, back when
    // a refused level built nothing to begin with.
    entt::registry alone;
    publishViewport(alone);
    MagicPortalsLayer byItself(TestPaths(), "level19c");
    byItself.OnAttach(alone);
    CHECK_MSG(byItself.SimLevel() != nullptr, byItself.LoadError());
    const std::size_t onItsOwn = alone.view<TransformComponent>().size();
    byItself.OnDetach(alone);

    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level18c");
    layer.OnAttach(registry);
    CHECK(IsAt(layer, "level18c"));
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    CHECK(registry.view<TransformComponent>().size() > std::size_t{1});

    press(layer, registry, MagicPortalsLayer::kSkip);
    CHECK(IsAt(layer, "level19c"));
    CHECK_MSG(layer.SimLevel() != nullptr && layer.LoadError().empty(), layer.LoadError());
    CHECK_EQ(registry.view<TransformComponent>().size(), onItsOwn);
    layer.OnDetach(registry);
}

void DeathIsABeatAndThenTheLostScreen() {
    // Into level5's death_area. This used to be an INSTANT RETRY - the tick it
    // happened the level was back as it loaded, with no screen and no beat -
    // and the owner's report was that there is no death screen at all.
    //
    // checkGameEnd counts gameEndElapsedTime against gameLostDelay before it
    // raises levelLostLayer, and gameLostDelay is the register gameWonDelay is
    // copied from: the same 1400 ms the finish already waits. So the death tick
    // hides the player and counts the death, the level keeps running behind it,
    // and only then does the screen go up - with LevelLostLayer's two buttons
    // and no third.
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

    const auto counted = [&layer](MagicPortalsLayer::MenuButton::Kind kind) {
        int found = 0;
        for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
            if (button.kind == kind) ++found;
        }
        return found;
    };

    // Counted at once, and the level NOT reloaded under it.
    CHECK_EQ(layer.Deaths(), 1);
    CHECK(IsAt(layer, "level5"));
    CHECK_MSG(layer.SimLevel() != nullptr && layer.SimLevel()->hazards.playerDied,
              "still dead, with the level still loaded behind the beat");
    CHECK_MSG(counted(MagicPortalsLayer::MenuButton::Kind::Retry) == 0, "and no screen one tick in");
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::None,
              "the level is still being played through the beat");

    // 1400 ms at the port's 60 Hz, and a tick over.
    for (int tick = 0; tick < 85; ++tick) tickWith(layer, registry, kRest, {}, {});

    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::Dead, "and then the lost screen is up");
    const int retries = counted(MagicPortalsLayer::MenuButton::Kind::Retry);
    const int lists = counted(MagicPortalsLayer::MenuButton::Kind::List);
    const int nexts = counted(MagicPortalsLayer::MenuButton::Kind::Next);
    CHECK_MSG(retries == 1 && lists == 1 && nexts == 0,
              "the lost screen's restart and list, and no 'go on': " + std::to_string(retries) + ", " +
                  std::to_string(lists) + ", " + std::to_string(nexts));
    std::printf("  level5: died, and the lost screen came up 1400 ms later with %d button(s)\n",
                static_cast<int>(layer.MenuButtons().size()));

    // And restart is a retry, which is what openDead following openFinished -
    // rather than openMenu - is for: openMenu would have cleared m_current and
    // left this button doing nothing at all.
    bool pressed = false;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::Retry) continue;
        layer.PressMenu(registry, button); // by value: pressing clears the list
        pressed = true;
        break;
    }
    CHECK_MSG(pressed, "the restart button is pressable");
    CHECK(IsAt(layer, "level5"));
    CHECK_MSG(layer.SimLevel() != nullptr && !layer.SimLevel()->hazards.playerDied, "and alive again");
    if (layer.SimLevel() != nullptr) {
        CHECK_MSG(glm::distance(playerPx(registry, layer), spawn) < 1.0,
                  "back at the spawn: " + Point(playerPx(registry, layer)));
    }
    layer.OnDetach(registry);
}

void AFallOutOfTheLevelIsADeath() {
    // The owner jumped into a pit and nothing happened. Nothing in the port
    // tested the level's own edge, so the player fell for ever; Hazards now
    // carries checkGameLost's bounds test, and this is it through the layer.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    if (layer.SimLevel() == nullptr) return;
    CHECK_MSG(layer.SimLevel()->hazards.haveBounds, "level1 knows its own extent");
    CloseTheLevelStartPopup(layer, registry);

    // Below the level and past the margin: (512, 256) plus (64, 96) in y.
    auto& transform = registry.get<TransformComponent>(layer.SimLevel()->player);
    const glm::vec3 below = MagicPortals::Units::ToWorld(256.0, 400.0);
    transform.position = glm::vec3(below.x, below.y, transform.position.z);
    tickWith(layer, registry, kRest, {}, {});

    CHECK_EQ(layer.Deaths(), 1);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.SimLevel()->hazards.playerDied, "the fall killed it");
    if (layer.SimLevel() != nullptr) {
        CHECK_MSG(layer.SimLevel()->hazards.diedByFalling, "and it says it was a fall");
    }
    const std::vector<std::string> sounds = layer.LatchedSounds();
    const bool fell = std::find(sounds.begin(), sounds.end(), "player_fell") != sounds.end();
    CHECK_MSG(fell, "and the fall's own cue played on the tick it happened");
    // D7 with a body still falling: the level runs on under the beat and the
    // lost screen, so the player goes on falling, but the follow is held inside
    // the level's bounds (Camera::Clamp, the original's camMin/camMax) and the
    // death is past them, so the camera does not pan under the screen.
    const auto fallenY = [&registry, &layer] {
        return registry.get<TransformComponent>(layer.SimLevel()->player).position.y;
    };
    for (int tick = 0; tick < 84; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::Dead, "the lost screen is up 84 ticks on");
    const glm::dvec2 atScreen = layer.CameraCentrePx();
    const float yAtScreen = fallenY();
    for (int tick = 0; tick < 316; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(fallenY() < yAtScreen - 1.0f, "the body falls on under the screen");
    // Not bit-equal: the follow is still closing the last fraction of a pixel
    // on the x this test teleported the body to, which is not a pan.
    const double moved = glm::length(layer.CameraCentrePx() - atScreen);
    CHECK_MSG(moved < 0.01, "and the camera stays where the screen found it, moved " + std::to_string(moved));
    std::printf("  level1: fell past the level's edge and died; the screen 84 ticks on, 316 more and the body is "
                "%.1f u lower, the camera %.5f px from where the screen found it\n",
                static_cast<double>(yAtScreen - fallenY()), moved);
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
    if (const entt::entity stone = FirstTagged(registry, "Magic Portals Thrown Sprite"); stone != entt::null) {
        // The converter's hd copy, 128 texels at density 2: 64 units (00_order 2.3).
        const std::string& texture = registry.get<MaterialComponent>(stone).albedoTexturePath;
        const glm::vec3 scale = registry.get<TransformComponent>(stone).scale;
        const double perMetre = MagicPortals::Units::kPixelsPerMetre;
        CHECK_MSG(EndsWith(texture, "/assets/entities/hd/rolling_stone.png") &&
                      std::fabs(scale.x * perMetre - 64.0) < 1e-3 && std::fabs(scale.y * perMetre - 64.0) < 1e-3,
                  "its hd file at 64 units: " + texture);
    }
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
    std::string skyPath;
    for (auto [entity, tag, material, transform] :
         registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals Sprite") continue;
        std::error_code ec;
        // Mixed as level8 says, which since step 47 is premultiplied: the lighting
        // adds a sprite's light at full weight over its alpha-weighted base.
        if (!material.unlit || !material.transparent ||
            material.blend != MaterialComponent::BlendMode::Premultiplied ||
            !std::filesystem::is_regular_file(material.albedoTexturePath, ec)) {
            drawnAsArt = false;
        }
        if (material.albedoTexturePath.find("icy_sky.png") != std::string::npos) {
            skyFound = true;
            skyZ = transform.position.z;
            skyScale = transform.scale;
            skyPath = material.albedoTexturePath;
        }
        if (transform.position.z < farthest) farthest = transform.position.z;
    }
    CHECK_MSG(drawnAsArt, "each unlit, mixed (premultiplied) as level8 says, with an image that is there");
    CHECK_MSG(skyFound && skyZ == farthest, "the sky is the farthest back");
    // Its image is the fullhd icy_sky.png the original draws (tiers.json, step 66):
    // 1024 x 512 texels at density 2, 512 x 256 units, one screen tall at scale 1.
    CHECK_MSG(skyFound && skyPath.find("fullhd") != std::string::npos, "the sky is drawn from its fullhd file: " + skyPath);
    CHECK_MSG(std::fabs(skyScale.x - 512.0f / 50.0f) < 1e-4f && std::fabs(skyScale.y - 256.0f / 50.0f) < 1e-4f,
              "at its image's own size in units");

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

    // And as ETHCallback_portal_static redraws them (art.json static_portal): the
    // halo at 0.8 of its 64 units, red (1, 0.3, 0.3) for 603 and 582, whose color is
    // "red", and (0.3, 0.3, 1) for the blue 604 and 583.
    CloseTheLevelStartPopup(layer, registry);
    tickWith(layer, registry, kRest, {}, {});
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    const float halo = MagicPortals::Units::ToMetres(64.0 * 0.8);
    int red = 0;
    int blue = 0;
    int otherSize = 0;
    for (auto [entity, tag, material, transform] :
         registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals Sprite" || material.albedoTexturePath.find("portal_halo.png") == std::string::npos) {
            continue;
        }
        if (std::fabs(transform.scale.x - halo) > 1e-6f || std::fabs(transform.scale.y - halo) > 1e-6f) ++otherSize;
        const glm::dvec2 at = MagicPortals::Units::ToPixels(transform.position);
        const bool shouldBeRed = glm::distance(at, glm::dvec2(150.0, 74.0)) < 0.5 ||
                                 glm::distance(at, glm::dvec2(334.0, 200.0)) < 0.5;
        if (shouldBeRed && material.albedoColor == glm::vec4(1.0f, 0.3f, 0.3f, 1.0f)) ++red;
        if (!shouldBeRed && material.albedoColor == glm::vec4(0.3f, 0.3f, 1.0f, 1.0f)) ++blue;
    }
    CHECK_MSG(red == 2 && blue == 2, "603 and 582 tinted red, 604 and 583 blue: " + std::to_string(red) + " red, " +
                                         std::to_string(blue) + " blue");
    CHECK_MSG(otherSize == 0, std::to_string(otherSize) + " halo(s) not at 51.2 units");
    layer.OnDetach(registry);
}

void AnAntiportalIsDrawnAtHalfItsNodesScale() {
    // AntiPortalManager's constructor calls Scale(0.5) on every antiportal it
    // collects, over the node's own scale (placement.json antiportal.manager), so
    // white_ring.png's 128 units are drawn 64 x scale across. 1-12 (level11) places
    // antiportal_762 at (180, 140), scale 1.8, and antiportal_574 at (464, 184),
    // scale 2.5, under anti_portal_agent_ent_599, which the manager does not collect.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level11");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    const MagicPortals::Portals::Rules& rules = layer.SimLevel()->portals.rules;
    CHECK_MSG(rules.antiportalEntity == "antiportal", "the manager collects antiportal: " + rules.antiportalEntity);
    CHECK_NEAR(static_cast<float>(rules.antiportalManagerScale), 0.5f);
    CHECK_NEAR(static_cast<float>(rules.antiportalRadiusPx), 16.0f); // the circle the port plays is not this step's
    CloseTheLevelStartPopup(layer, registry);
    tickWith(layer, registry, kRest, {}, {});
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);

    struct Seen {
        const char* image;
        glm::dvec2 atPx;
        double sizeU; // across, in the level's units
        int found;
    };
    std::array<Seen, 3> pictures{{{"white_ring.png", {180.0, 140.0}, 64.0 * 1.8, 0},
                                  {"white_ring.png", {464.0, 184.0}, 64.0 * 2.5, 0},
                                  {"anti_portal_agent.png", {464.0, 184.0}, 32.0, 0}}};
    for (auto [entity, tag, material, transform] :
         registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals Sprite") continue;
        const glm::dvec2 at = MagicPortals::Units::ToPixels(transform.position);
        for (Seen& seen : pictures) {
            if (material.albedoTexturePath.find(seen.image) == std::string::npos || glm::distance(at, seen.atPx) > 0.5) {
                continue;
            }
            const float across = MagicPortals::Units::ToMetres(seen.sizeU);
            CHECK_MSG(std::fabs(transform.scale.x - across) < 1e-5f && std::fabs(transform.scale.y - across) < 1e-5f,
                      std::string(seen.image) + " at (" + std::to_string(seen.atPx.x) + ", " +
                          std::to_string(seen.atPx.y) + ") is " + std::to_string(seen.sizeU) + " units across, not " +
                          std::to_string(transform.scale.x / MagicPortals::Units::ToMetres(1.0)));
            ++seen.found;
        }
    }
    for (const Seen& seen : pictures) {
        CHECK_MSG(seen.found == 1, std::string(seen.image) + " drawn once at (" + std::to_string(seen.atPx.x) + ", " +
                                       std::to_string(seen.atPx.y) + "): " + std::to_string(seen.found));
    }
    // The zones' boxes stay the circle the port plays, 16 x scale: the ring and the
    // box differ until the refusal radius is the original's.
    int boxesOnTheRule = 0;
    for (auto [entity, tag, transform] : registry.view<TagComponent, TransformComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals No-Portal Zone") continue;
        const glm::dvec2 at = MagicPortals::Units::ToPixels(transform.position);
        if (glm::distance(at, glm::dvec2(180.0, 140.0)) < 0.5 &&
            std::fabs(transform.scale.x - MagicPortals::Units::ToMetres(16.0 * 1.8 * 2.0)) < 1e-5f) {
            ++boxesOnTheRule;
        }
    }
    CHECK_EQ(boxesOnTheRule, 1);
    layer.OnDetach(registry);
}

// A RING BLINKS RED AND TURNS (art.json antiportal). ETHCallback_antiportal runs
// every frame: blinkColor(this, (0.2, 0, 0), (0.4, 0, 0), 300) writes the entity's
// colour, and AddToAngle(unitsPerSecond(10)) turns its picture. 1-12 (level11)
// places antiportal_762 and antiportal_574 under anti_portal_agent_ent_599, whose
// callback is another one: it neither blinks nor turns.
void AnAntiportalBlinksRedAndTurnsAndAPauseHoldsBoth() {
    namespace Art = MagicPortals::Art;
    Art::Rules art;
    std::string artError;
    CHECK_MSG(Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", art, artError), artError);
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level11");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    CloseTheLevelStartPopup(layer, registry);

    // The rings, by their picture and where their nodes stand, and the agent beside
    // one of them, by its own.
    const auto quadAt = [&registry](const char* image, glm::dvec2 atPx) {
        entt::entity found = entt::null;
        for (auto [entity, tag, material, transform] :
             registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
            if (tag.tag != "Magic Portals Sprite") continue;
            if (material.albedoTexturePath.find(image) == std::string::npos) continue;
            if (glm::distance(MagicPortals::Units::ToPixels(transform.position), atPx) > 0.5) continue;
            found = entity;
        }
        return found;
    };
    const auto colourOf = [&registry](entt::entity quad) {
        return registry.get<MaterialComponent>(quad).albedoColor;
    };
    const auto rotationOf = [&registry](entt::entity quad) {
        return registry.get<TransformComponent>(quad).rotation.z;
    };
    tickWith(layer, registry, kRest, {}, {});
    const entt::entity ring762 = quadAt("white_ring.png", {180.0, 140.0});
    const entt::entity ring574 = quadAt("white_ring.png", {464.0, 184.0});
    const entt::entity agent = quadAt("anti_portal_agent.png", {464.0, 184.0});
    CHECK_MSG(ring762 != entt::null && ring574 != entt::null && agent != entt::null,
              "1-12 draws two rings and an agent");
    if (ring762 == entt::null || ring574 == entt::null || agent == entt::null) return;

    // Over two whole periods: the colour is the blink's at the level's age, red
    // alone, at the alpha the level gave it; both rings share one blink; the agent
    // is left alone; and the turn is 10 degrees a second, counter-clockwise, which
    // the engine's +z is (the node's own angle is 0 on both).
    double lows = 0.0, highs = 0.0;
    int wrong = 0, turnWrong = 0, agentWrong = 0, sharedWrong = 0;
    // The turn is counted in the ticks that STEP the level, and the level's age in
    // the ticks that are not stopped; a tick that hands a popup or a pause back to
    // the level steps it without ageing it, so the two are compared a tick at a time.
    const float turnATick = static_cast<float>(art.antiportal.spinDegPerS * MagicPortalsLayer::kTick *
                                               3.14159265358979323846 / 180.0);
    float lastTurn = rotationOf(ring762);
    for (int tick = 0; tick < 80; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        const glm::dvec3 want = art.antiportal.ColourAt(layer.LevelAgeMs());
        const glm::vec4 got = colourOf(ring762);
        if (std::fabs(got.r - static_cast<float>(want.x)) > 1e-6f || got.g != 0.0f || got.b != 0.0f ||
            got.a != 1.0f) {
            ++wrong;
        }
        if (colourOf(ring574) != got) ++sharedWrong;
        if (colourOf(agent) != glm::vec4(1.0f)) ++agentWrong;
        // 10 degrees a second at the port's 1/60 s tick, counter-clockwise (the node's
        // own angle is 0 on both), and the same angle for every ring the level placed.
        if (std::fabs(rotationOf(ring762) - (lastTurn + turnATick)) > 1e-5f ||
            rotationOf(ring574) != rotationOf(ring762)) {
            ++turnWrong;
        }
        lastTurn = rotationOf(ring762);
        lows = (lows == 0.0 || want.x < lows) ? want.x : lows;
        highs = std::max(highs, want.x);
    }
    CHECK_MSG(wrong == 0, "every tick's ring is the blink's colour, red alone: " + std::to_string(wrong) + " wrong");
    CHECK_MSG(sharedWrong == 0, "both rings blink together: " + std::to_string(sharedWrong) + " ticks apart");
    CHECK_MSG(agentWrong == 0, "anti_portal_agent is another callback's and stays white");
    CHECK_MSG(turnWrong == 0, "10 degrees a second, both rings: " + std::to_string(turnWrong) + " ticks wrong");
    CHECK_MSG(std::fabs(lows - 0.2) < 1e-6 && std::fabs(highs - 0.4) < 1e-6,
              "over 80 ticks it reaches both ends: " + std::to_string(lows) + " to " + std::to_string(highs));

    // A PAUSE HOLDS BOTH (the original's m_factor 0): the level's age stands still,
    // so the blink does, and no tick steps the turn.
    const glm::vec4 heldColour = colourOf(ring762);
    const float heldTurn = rotationOf(ring762);
    const double heldAge = layer.LevelAgeMs();
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(layer.Paused(), "Escape pauses 1-12");
    for (int tick = 0; tick < 40; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.LevelAgeMs() <= heldAge + MagicPortalsLayer::kTick * 1000.0 + 1e-9,
              "the level's age stands still under the pause: " + std::to_string(layer.LevelAgeMs() - heldAge) + " ms");
    CHECK_MSG(colourOf(ring762) == heldColour && rotationOf(ring762) == heldTurn,
              "and so do the ring's colour and its angle");
    // And it takes up where it left off, rather than jumping the pause's length.
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(!layer.Paused(), "and Escape again resumes it");
    tickWith(layer, registry, kRest, {}, {});
    const glm::vec4 after = colourOf(ring762);
    const glm::dvec3 want = art.antiportal.ColourAt(layer.LevelAgeMs());
    CHECK_MSG(std::fabs(after.r - static_cast<float>(want.x)) < 1e-6f, "the blink goes on from where it stood");
    // Two ticks' worth, not forty: the tick the pause opened on aged the level before
    // it stopped and drew nothing, so the blink takes that tick up on the tick that
    // resumes, and this one is the next. (The original stops one clock with one
    // factor; the port splits a tick into ageing and stepping, and the turn, which is
    // stepped, skips that tick instead. A sixth of a degree and a hundredth of the
    // blink, once per pause.)
    const float perTick = static_cast<float>(0.2 * MagicPortalsLayer::kTick * 1000.0 / art.antiportal.strideMs);
    CHECK_MSG(std::fabs(after.r - heldColour.r) <= 2.5f * perTick,
              "two ticks' worth on, not 40: " + std::to_string(after.r - heldColour.r));
    CHECK_MSG(rotationOf(ring762) > heldTurn, "and the turn goes on too");
}

void AnAntiportalSpelledWithEntIsDrawnTheSame() {
    // The manager collects both spellings, GetEntityArray("antiportal") and
    // ("antiportal.ent"). 3-29 (level28b) places antiportal_ent_2204 at (372, 160),
    // scale 5, so its ring is 64 x 5 = 320 units across.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level28b");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    CloseTheLevelStartPopup(layer, registry);
    tickWith(layer, registry, kRest, {}, {});
    layer.OnUpdate(registry, MagicPortalsLayer::kTick);

    const glm::dvec2 nodePx(372.0, 160.0);
    const float across = MagicPortals::Units::ToMetres(64.0 * 5.0);
    int found = 0;
    for (auto [entity, tag, material, transform] :
         registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
        (void)entity;
        if (tag.tag != "Magic Portals Sprite" || material.albedoTexturePath.find("white_ring.png") == std::string::npos ||
            glm::distance(MagicPortals::Units::ToPixels(transform.position), nodePx) > 0.5) {
            continue;
        }
        CHECK_MSG(std::fabs(transform.scale.x - across) < 1e-5f && std::fabs(transform.scale.y - across) < 1e-5f,
                  "antiportal_ent_2204 is 320 units across, not " +
                      std::to_string(transform.scale.x / MagicPortals::Units::ToMetres(1.0)));
        ++found;
    }
    CHECK_EQ(found, 1);
    layer.OnDetach(registry);
}

// A GRAVITY WELL WEARS A GREEN RING (gravitywell.json `ring`), and no level file
// says so: ETHCallback_gravity_agent adds an antiportal.ent at the agent's own
// position the first time it runs, sizes it to radius * 2 - 24 and paints it
// (0.25, 0.5, 0.25). It is renamed `gravity_area` on the way in, so the callback it
// gets is the force and not ETHCallback_antiportal - this ring neither blinks nor
// turns, and the manager never halves it. Ten of them, across the five levels that
// place a well.
void AGravityWellWearsAGreenRing() {
    const struct Level {
        const char* name;
        std::size_t rings;
    } levels[] = {{"level16c", 1}, {"level17c", 4}, {"level18c", 3}, {"level20c", 1}, {"level27c", 1}};

    std::size_t total = 0;
    for (const Level& one : levels) {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), one.name);
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr) continue;
        CloseTheLevelStartPopup(layer, registry);
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);

        const MagicPortals::GravityWell::State& wells = layer.SimLevel()->wells;
        CHECK_MSG(wells.wells.size() == one.rings, std::string(one.name) + " places its wells");
        std::size_t found = 0;
        float ringZ = 0.0f;
        for (const MagicPortals::GravityWell::Well& well : wells.wells) {
            const float across = MagicPortals::Units::ToMetres(well.RingSizePx(wells.rules));
            for (auto [entity, tag, material, transform] :
                 registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
                (void)entity;
                if (tag.tag != "Magic Portals Well Ring") continue;
                if (glm::distance(MagicPortals::Units::ToPixels(transform.position), well.atPx) > 0.5) continue;
                CHECK_MSG(material.albedoTexturePath.find("white_ring.png") != std::string::npos,
                          well.name + " wears white_ring.png, not " + material.albedoTexturePath);
                CHECK_MSG(std::fabs(transform.scale.x - across) < 1e-5f && std::fabs(transform.scale.y - across) < 1e-5f,
                          well.name + " is drawn " + std::to_string(well.RingSizePx(wells.rules)) +
                              " units across, not " +
                              std::to_string(transform.scale.x / MagicPortals::Units::ToMetres(1.0)));
                CHECK_MSG(material.blend == MaterialComponent::BlendMode::Additive, well.name + "'s ring is added");
                CHECK_MSG(material.albedoColor == glm::vec4(0.25f, 0.5f, 0.25f, 1.0f),
                          well.name + "'s ring is green, not (" + std::to_string(material.albedoColor.r) + ", " +
                              std::to_string(material.albedoColor.g) + ", " + std::to_string(material.albedoColor.b) +
                              ")");
                ringZ = transform.position.z;
                ++found;
            }
        }
        CHECK_MSG(found == one.rings, std::string(one.name) + ": " + std::to_string(found) + " ring(s) of " +
                                          std::to_string(one.rings));
        CHECK_EQ(Tagged(registry, "Magic Portals Well Ring"), static_cast<int>(one.rings));
        total += found;

        // AND IT IS DRAWN AT THE DEPTH THE CALLBACK GIVES IT, -5: after the level's
        // own pictures at or below that and before the next, so the door and the two
        // backgrounds of 4-17 stay behind it and its eleven nearer pictures cover it.
        if (std::string(one.name) == "level16c" && found == 1) {
            int behind = 0;
            int inFront = 0;
            for (auto [entity, tag, transform] : registry.view<TagComponent, TransformComponent>().each()) {
                (void)entity;
                if (tag.tag != "Magic Portals Sprite") continue;
                if (transform.position.z < ringZ) ++behind;
                if (transform.position.z > ringZ) ++inFront;
            }
            CHECK_MSG(behind == 5 && inFront == 11,
                      "4-17 draws 5 of its 16 pictures behind the ring and 11 in front: " + std::to_string(behind) +
                          " and " + std::to_string(inFront));
        }
        layer.OnDetach(registry);
    }
    CHECK_EQ(total, std::size_t{10});

    // A level with no well has no ring, and nothing white_ring.png-shaped that the
    // wells' tag could have caught: level0 places neither.
    entt::registry none;
    publishViewport(none);
    MagicPortalsLayer plain(TestPaths(), "level0");
    plain.OnAttach(none);
    CHECK_EQ(Tagged(none, "Magic Portals Well Ring"), 0);
    plain.OnDetach(none);
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
    CHECK_MSG(!layer.LightingError().empty(), "and so is the lighting, which names the same missing files");
    CHECK_MSG(layer.AmbientNow() == glm::dvec3(1.0) && layer.HeldLightmaps().empty(),
              "so nothing is dimmed and no lightmap is held");
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
    CloseTheLevelStartPopup(layer, registry);
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
    CHECK_MSG(!layer.PopupOpen(), "a popup whose card and button cannot be read is not raised to stop the level");
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
    // dark_mage.ent's sheet. A level starts standing on the idle column of the
    // way GameCharacter is made facing: frame 8, the right row. It walks through
    // a row of four while a direction is held, on the row that direction reads,
    // turns when it turns, and stands on the idle column of the way it last
    // walked. Which row and column are art.json's; the start frame is pinned
    // here.
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
    CHECK_MSG(EndsWith(material.albedoTexturePath, "/entities/hd/magic_portals_hd.png") &&
                  material.blend == MaterialComponent::BlendMode::Premultiplied && animation().columns == 4 &&
                  animation().rows == 4,
              "dark_mage.ent's sheet, its hd file, cut 4 x 4, mixed (premultiplied): " + material.albedoTexturePath);
    {
        // 320 x 448 texels at density 2, cut 4 x 4: 40 x 56 units a frame, the 1x
        // file's (00_order 2.3).
        const glm::vec3 scale = registry.get<TransformComponent>(mageQuad).scale;
        const double perMetre = MagicPortals::Units::kPixelsPerMetre;
        CHECK_MSG(std::fabs(scale.x * perMetre - 40.0) < 1e-3 && std::fabs(scale.y * perMetre - 56.0) < 1e-3,
                  "a 40 x 56 frame of it");
    }
    CHECK_MSG(animation().firstFrame == 8 && animation().firstFrame == rowStart(mage.rightRow) &&
                  animation().frameCount == 1 && !animation().playing && animation().frame == 0 &&
                  animation().elapsed == 0.0f,
              "standing on frame 8, facing right, its walk not begun");
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

// One tick as SupersonicApp runs it when a flipbook matters: the physics step,
// then the sprite flipbooks, then the layer ("Before the layers",
// SupersonicApp.cpp), with `down` held for the tick.
void tickWithFlipbooks(MagicPortalsLayer& layer, entt::registry& registry, std::vector<std::string> down) {
    PhysicsSystem::Update(registry, MagicPortalsLayer::kTick);
    SpriteAnimationSystem::Update(registry, MagicPortalsLayer::kTick);
    Input::TickInput input;
    input.mousePosition = kRest;
    input.down = std::move(down);
    Input::BeginReplayedTick(input);
    layer.OnFixedUpdate(registry, MagicPortalsLayer::kTick);
    Input::EndReplayedTick();
}

void TheWalkKeepsItsColumnAndItsTime() {
    // SideScrollerCharacter::updateFrame steps its FrameTimer only on the updates
    // it walks, and always as set(0, 3, 90, true): the timer resets on the first
    // walk alone, and the column and its time carry across a stop and a turn, a
    // column every 90 ms (art.json character.animation). Ticked as the app ticks,
    // the flipbook before the layer.
    if (!OriginalArtIsThere("TheWalkKeepsItsColumnAndItsTime")) return;
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
    const auto run = [&](const char* key, int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            tickWithFlipbooks(layer, registry, key ? std::vector<std::string>{key} : std::vector<std::string>{});
        }
        CHECK_MSG(registry.valid(mageQuad), "the player is still there");
        return registry.valid(mageQuad);
    };
    const auto animation = [&registry, mageQuad]() -> const SpriteAnimationComponent& {
        return registry.get<SpriteAnimationComponent>(mageQuad);
    };

    // The first walking tick starts on column 0 with no time; the six after it
    // add 100 ms, one stride and 10 ms over.
    if (!run(MagicPortalsLayer::kRight, 7)) return;
    CHECK_MSG(animation().firstFrame == 8 && animation().frameCount == 4 && animation().playing &&
                  animation().frame == 1,
              "walking right, on column 1 after 100 ms: " + std::to_string(animation().frame));
    // The tick it stops on, the flipbook has already taken its tick (the app's
    // order): one tick of time the original does not add, which the tick the
    // walk resumes on does not add either.
    if (!run(nullptr, 1)) return;
    const uint32_t column = animation().frame;
    const float time = animation().elapsed;
    CHECK_MSG(animation().firstFrame == 8 && animation().frameCount == 1 && !animation().playing && column == 1,
              "standing on the idle column, the walk's column kept");
    if (!run(nullptr, 30)) return;
    CHECK_MSG(animation().frame == column && animation().elapsed == time, "and half a second standing adds no time");
    if (!run(MagicPortalsLayer::kRight, 1)) return;
    CHECK_MSG(animation().firstFrame == 8 && animation().frameCount == 4 && animation().frame == column &&
                  animation().elapsed == time,
              "walking again from the column it stopped on, not from column 0");
    if (!run(MagicPortalsLayer::kLeft, 1)) return;
    CHECK_MSG(animation().firstFrame == 4 && animation().frameCount == 4 && animation().frame == column,
              "turned left on the same column");

    // Eight ticks of walking time so far (6 + the stop's 1 + the turn's 1).
    // Back and forth for 55 more, turning once: 63 ticks, 1050 ms, eleven 90 ms
    // columns and 60 ms over, wherever it turned.
    if (!run(MagicPortalsLayer::kRight, 28) || !run(MagicPortalsLayer::kLeft, 27)) return;
    CHECK_MSG(animation().firstFrame == 4 && animation().frame == 3 &&
                  std::fabs(animation().elapsed - 0.060f) < 0.001f,
              "1050 ms of walking is 11 columns of 90 ms (column 3) and 60 ms: column " +
                  std::to_string(animation().frame) + ", " + std::to_string(animation().elapsed) + " s");
    layer.OnDetach(registry);
}

// A trigger box standing in the level at `centrePx`, as the converter's Area2D
// bodies are built (LevelBuilder: a box collider, isTrigger): something a player
// walks through, and a sensor fixture in the original.
entt::entity addTriggerBox(entt::registry& registry, const glm::dvec2& centrePx, const glm::dvec2& sizePx) {
    const entt::entity box = registry.create();
    registry.emplace<TransformComponent>(box).position = MagicPortals::Units::ToWorld(centrePx.x, centrePx.y);
    auto& collider = registry.emplace<BoxColliderComponent>(box);
    collider.size = glm::vec3(MagicPortals::Units::ToMetres(sizePx.x), MagicPortals::Units::ToMetres(sizePx.y), 1.0f);
    collider.isTrigger = true;
    return box;
}

// SideScrollerCharacter::detectPushing and findFinalDirection (art.json
// character.push): walked into a wall the player draws its arm out, row 3 facing
// right and row 0 facing left, on the walk's own column and time. updateFrame
// runs before applyForces, so the step the ray first meets the wall still draws
// the walk, and the step the walk stops on still draws the push.
void APlayerWalkedIntoAWallPushesIt() {
    if (!OriginalArtIsThere("APlayerWalkedIntoAWallPushesIt")) return;
    {
        // 1-22 (level21): its breakable wall stands 18 units right of the marker,
        // inside the ray's 24 from the first step.
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level21");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr) return;
        const entt::entity mageQuad = FirstTagged(registry, "Magic Portals Player Sprite");
        CHECK(mageQuad != entt::null);
        if (mageQuad == entt::null) return;
        const auto animation = [&registry, mageQuad]() -> const SpriteAnimationComponent& {
            return registry.get<SpriteAnimationComponent>(mageQuad);
        };
        const auto say = [&animation]() {
            return " (first " + std::to_string(animation().firstFrame) + ", count " +
                   std::to_string(animation().frameCount) + ", frame " + std::to_string(animation().frame) + ")";
        };
        for (int tick = 0; tick < 60; ++tick) tickWithFlipbooks(layer, registry, {});
        CHECK_MSG(animation().firstFrame == 8 && animation().frameCount == 1,
                  "settled, standing on frame 8 beside the wall, which a standing player does not push" + say());
        tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kRight});
        CHECK_MSG(animation().firstFrame == 8 && animation().frameCount == 4 && animation().playing,
                  "the first walking step draws the walk: the push is found after it is drawn" + say());
        tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kRight});
        CHECK_MSG(animation().firstFrame == 12 && animation().frameCount == 4 && animation().playing,
                  "the next draws row 3, arm out to the right, still walking its columns" + say());
        for (int tick = 0; tick < 5; ++tick) tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kRight});
        CHECK_MSG(animation().firstFrame == 12 && animation().frame == 1,
                  "7 walking steps in, column 1 as an open walk's: the row takes nothing from the column" + say());
        for (int tick = 0; tick < 60; ++tick) tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kRight});
        const glm::dvec2 at =
            MagicPortals::Units::ToPixels(registry.get<TransformComponent>(layer.SimLevel()->player).position);
        CHECK_MSG(animation().firstFrame == 12 && at.x < 285.0,
                  "a second against the wall, still pushing it, stopped short of its face at 285: x " +
                      std::to_string(at.x) + say());
        tickWithFlipbooks(layer, registry, {});
        CHECK_MSG(animation().firstFrame == 12 && animation().frameCount == 1 && !animation().playing,
                  "the step it stops on stands on row 3" + say());
        const uint32_t column = animation().frame;
        const float time = animation().elapsed;
        tickWithFlipbooks(layer, registry, {});
        CHECK_MSG(animation().firstFrame == 8 && animation().frameCount == 1 && animation().frame == column &&
                      animation().elapsed == time,
                  "and the step after stands facing right again, on row 2" + say());
        layer.OnDetach(registry);
    }
    {
        // 1-01 (level0): the level's left wall, its face at x 0, 36 units left of
        // the marker. Walking left the ray first meets it with the body at 24 or
        // less, and the step after draws row 0.
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level0");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr) return;
        const entt::entity mageQuad = FirstTagged(registry, "Magic Portals Player Sprite");
        CHECK(mageQuad != entt::null);
        if (mageQuad == entt::null) return;
        for (int tick = 0; tick < 60; ++tick) tickWithFlipbooks(layer, registry, {});
        std::vector<double> xs;
        int pushedOn = -1;
        bool walkedLeftFirst = true;
        for (int tick = 0; tick < 120 && pushedOn < 0; ++tick) {
            xs.push_back(
                MagicPortals::Units::ToPixels(registry.get<TransformComponent>(layer.SimLevel()->player).position).x);
            tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kLeft});
            const auto& animation = registry.get<SpriteAnimationComponent>(mageQuad);
            if (animation.firstFrame == 0) {
                pushedOn = tick;
            } else if (animation.firstFrame != 4) {
                walkedLeftFirst = false;
            }
        }
        CHECK_MSG(walkedLeftFirst, "walking left on row 1 until the push");
        CHECK_MSG(pushedOn >= 2, "row 0 once the wall is in reach: step " + std::to_string(pushedOn));
        if (pushedOn >= 2) {
            // xs[k] is where step k - 1's physics left the body, which is where
            // step k - 1 cast its ray; step k draws what that ray found.
            const double found = xs[static_cast<std::size_t>(pushedOn)];
            const double before = xs[static_cast<std::size_t>(pushedOn - 1)];
            CHECK_MSG(found <= 24.0 && before > 23.0,
                      "found with the body at " + std::to_string(found) + " and not a step before, at " +
                          std::to_string(before) + ": a reach of 24 to the wall's face at 0");
        }
        layer.OnDetach(registry);
    }
}

// GetClosestContact meets a sensor as it meets a wall - Box2D's ray asks no
// fixture whether it is one - and never a body the ray starts inside. A trigger
// box set in 1-09's open floor (level8): the arm goes out as it comes within
// reach, back in while the player walks through it, and stays in once past.
void APlayerPushesATriggerItHasNotEntered() {
    if (!OriginalArtIsThere("APlayerPushesATriggerItHasNotEntered")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level8");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    const entt::entity mageQuad = FirstTagged(registry, "Magic Portals Player Sprite");
    CHECK(mageQuad != entt::null);
    if (mageQuad == entt::null) return;
    for (int tick = 0; tick < 60; ++tick) tickWithFlipbooks(layer, registry, {});
    const auto bodyPx = [&registry, &layer]() {
        return MagicPortals::Units::ToPixels(registry.get<TransformComponent>(layer.SimLevel()->player).position);
    };
    const glm::dvec2 start = bodyPx();
    // 10 wide, from 30 to 40 ahead of the body, and 60 tall about its centre.
    const double nearFace = start.x + 30.0;
    const double farFace = start.x + 40.0;
    addTriggerBox(registry, glm::dvec2(start.x + 35.0, start.y), glm::dvec2(10.0, 60.0));
    bool pushedApproaching = false;
    bool pushedInside = false;
    bool pushedPast = false;
    bool sawInside = false;
    bool sawPast = false;
    double lastX = start.x;
    // Stopped well short of 1-09's breakable wall, 164 units right of the marker.
    for (int tick = 0; tick < 90 && lastX < start.x + 100.0; ++tick) {
        // What this step draws is what the step before found, where its physics
        // left the body.
        const double foundAt = lastX;
        tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kRight});
        lastX = bodyPx().x;
        const bool pushing = registry.get<SpriteAnimationComponent>(mageQuad).firstFrame == 12;
        if (tick == 0) continue; // the first step draws what standing found
        if (foundAt < nearFace - 0.5 && foundAt + 23.0 > nearFace) {
            pushedApproaching = pushedApproaching || pushing;
        } else if (foundAt > nearFace + 0.5 && foundAt < farFace - 0.5) {
            sawInside = true;
            pushedInside = pushedInside || pushing;
        } else if (foundAt > farFace + 0.5) {
            sawPast = true;
            pushedPast = pushedPast || pushing;
        }
    }
    CHECK_MSG(pushedApproaching, "the arm out as the trigger comes within the ray's reach");
    CHECK_MSG(sawInside && !pushedInside, "and in while the body is inside it, which the ray starts inside");
    CHECK_MSG(sawPast && !pushedPast, "and in once past it: walked to x " + std::to_string(lastX));
    layer.OnDetach(registry);
}

// MainCharacter passes noGravity to SideScrollerCharacter as forceNoImpulse, and
// update skips applyForces while it is set: pushing stays the constructor's
// false. On 4-02 (level1c) a trigger box within reach of the suit, and a walk held
// towards it, draws no arm; the same kind of box and walk on 1-09 does
// (APlayerPushesATriggerItHasNotEntered).
void AWeightlessPlayerNeverPushes() {
    if (!OriginalArtIsThere("AWeightlessPlayerNeverPushes")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1c");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr) return;
    CHECK(layer.SimLevel()->noGravity);
    const entt::entity mageQuad = FirstTagged(registry, "Magic Portals Player Sprite");
    CHECK(mageQuad != entt::null);
    if (mageQuad == entt::null) return;
    const glm::dvec2 at =
        MagicPortals::Units::ToPixels(registry.get<TransformComponent>(layer.SimLevel()->player).position);
    addTriggerBox(registry, glm::dvec2(at.x + 15.0, at.y), glm::dvec2(6.0, 80.0));
    bool pushed = false;
    for (int tick = 0; tick < 60; ++tick) {
        tickWithFlipbooks(layer, registry, {MagicPortalsLayer::kRight});
        const uint32_t first = registry.get<SpriteAnimationComponent>(mageQuad).firstFrame;
        pushed = pushed || first / 4 != 2;
    }
    CHECK_MSG(!pushed, "row 2 on every step, never the arm out");
    layer.OnDetach(registry);
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
    CHECK_MSG(EndsWith(material.albedoTexturePath, "/entities/hd/beholder.png"),
              "its hd sheet: " + material.albedoTexturePath);
    CHECK(material.albedoColor == glm::vec4(1.0f));
    const auto& animation = registry.get<SpriteAnimationComponent>(quad);
    CHECK(animation.columns == 2u && animation.rows == 1u && animation.firstFrame == 0u);
    {
        // 512 x 256 texels at density 2, cut 2 x 1: a 128 x 128-unit frame, times the
        // pulse bounce() gives each axis now (never 2x or 1/2 of it).
        const glm::vec3 scale = registry.get<TransformComponent>(quad).scale;
        const glm::dvec2 frame = glm::dvec2(scale.x, scale.y) * MagicPortals::Units::kPixelsPerMetre;
        CHECK_MSG(frame.x > 128.0 * 0.8 && frame.x < 128.0 * 1.25 && frame.y > 128.0 * 0.8 && frame.y < 128.0 * 1.25,
                  "a 128-unit frame at its pulse: " + Point(frame));
    }
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

// Ticks through the loading screen until the main menu is up: 143 ticks, and a
// bound well past them.
bool TickToTheMainMenu(MagicPortalsLayer& layer, entt::registry& registry) {
    for (int tick = 0; tick < 400 && layer.MenuScreen() != MagicPortalsLayer::Screen::Main; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
    }
    return layer.MenuScreen() == MagicPortalsLayer::Screen::Main;
}

// A tick on which a held touch is let go, where it is.
void releaseWith(MagicPortalsLayer& layer, entt::registry& registry, const glm::vec2& pointer) {
    PhysicsSystem::Update(registry, MagicPortalsLayer::kTick);
    Input::TickInput input;
    input.mousePosition = pointer;
    input.released = {MagicPortalsLayer::kTap};
    Input::BeginReplayedTick(input);
    layer.OnFixedUpdate(registry, MagicPortalsLayer::kTick);
    Input::EndReplayedTick();
}

// The back key, on one tick.
void pressBack(MagicPortalsLayer& layer, entt::registry& registry) {
    press(layer, registry, MagicPortalsLayer::kBack);
}

// What the menu states put into the overlay this frame.
std::vector<ScreenOverlay::Quad> MenuFrame(entt::registry& registry, const MagicPortalsLayer& layer) {
    static ScreenOverlay overlay;
    overlay.Clear();
    registry.ctx().insert_or_assign<ScreenOverlay*>(&overlay);
    layer.EmitMenu(registry);
    return overlay.Quads();
}

int MenuButtonsOfKind(const MagicPortalsLayer& layer, MagicPortalsLayer::MenuButton::Kind kind) {
    int count = 0;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind == kind) ++count;
    }
    return count;
}

// Started with no level named, the game opens its loading screen and then its
// menu, and the menu walks main -> chapters -> levels -> the level itself.
void TheMenuWalksToALevel() {
    using Screen = MagicPortalsLayer::Screen;
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    CHECK_MSG(layer.LoadError().empty(), layer.LoadError());
    CHECK_MSG(layer.MenuScreen() == Screen::Loading, "the loading screen first (owner ruling R4)");
    CHECK(layer.SimLevel() == nullptr);
    CHECK_MSG(TickToTheMainMenu(layer, registry), "and the main menu after it");
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Play), 1);
    // The main menu's own art is drawn - through the overlay, in display values,
    // and so not in the registry at all (ui3 spec D21). The backgrounds live among
    // the original's entities rather than its sprites, and the first cut of this
    // looked for every menu image in one place: the menu came up with nothing
    // behind it and said nothing about why.
    CHECK_EQ(Tagged(registry, "Magic Portals Menu Background"), 0);
    CHECK_EQ(Tagged(registry, "Magic Portals Title"), 0);
    if (OriginalArtIsThere("TheMenuWalksToALevel")) {
        // Its buttons are drawn once their entrance has begun: at alpha 0 on the
        // state's first tick, nothing of them is sent.
        for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, {}, {});
        const std::vector<ScreenOverlay::Quad> frame = MenuFrame(registry, layer);
        CHECK_MSG(IndexOfImage(frame, "main_menu_bg.png") == 0, "the background, first");
        CHECK_MSG(IndexOfImage(frame, "game_main_title.png") > IndexOfImage(frame, "main_play_game_button.png"),
                  "the title over TAP START");
    }

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
    // Since step 56 the buttons are every tile of the chapter, each at rest on its
    // own page (sim/Selector), and the pager says which page is up.
    CHECK_EQ(MenuButtonsOfKind(layer, Kind::Level), 32);
    CHECK_EQ(layer.SelectorBoard().perPage, 16);
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
    using Screen = MagicPortalsLayer::Screen;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    CHECK(TickToTheMainMenu(layer, registry));
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
    CHECK_MSG(layer.PressMenu(registry, *forward) && layer.SelectorState().page == 1,
              "the forward arrow turns the pager to page 2, in the same state");
    CHECK(layer.MenuScreen() == Screen::Levels);
    // Sixteen to a page, so the second page opens on the seventeenth level.
    const Selector::Board& board = layer.SelectorBoard();
    const int seventeenth = board.items > 16 ? board.tiles[16].level : -1;
    CHECK_MSG(seventeenth >= 0 && layer.ChapterTable().levels[static_cast<std::size_t>(seventeenth)].name == "level16",
              "the second page's first tile is level16");
    // And a fresh save has not opened it (owner ruling R3): the press does nothing.
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != Kind::Level || button.level != seventeenth) continue;
        CHECK_MSG(!layer.PressMenu(registry, button) && layer.MenuScreen() == Screen::Levels,
                  "a locked level does nothing");
        break;
    }
    // Back: page 1, and from page 1 up to chapter select.
    const MagicPortalsLayer::MenuButton* back = MenuButtonOf(layer, Kind::Back);
    if (back == nullptr) {
        CHECK_MSG(false, "the grid has no back button");
        return;
    }
    const MagicPortalsLayer::MenuButton up = *back;
    CHECK_MSG(layer.PressMenu(registry, up) && layer.SelectorState().page == 0 && layer.MenuScreen() == Screen::Levels,
              "back on page 2: page 1");
    CHECK_MSG(layer.PressMenu(registry, up) && layer.MenuScreen() == Screen::Worlds, "back on page 1: chapter select");
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

// Escape is the original's back key, GameState::handleBackButton: over a level
// being played it opens the pause, and from the pause it resumes. It used to
// leave the level for its grid, which is now the pause's own first button
// (ThePausesButtonsGoWhereTheOriginalsGo).
void EscapePausesALevelAndResumesIt() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    // Under 1-02's popup the back key closes the popup (Popup::hasReceivedCloseCommand,
    // GSK_BACK), not opens the pause over it.
    CHECK(layer.PopupOpen());
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(!layer.Paused() && layer.OpenPopup() != nullptr && MagicPortals::Popup::Closing(*layer.OpenPopup()),
              "Escape under a popup closes it");
    while (layer.PopupOpen()) tickWith(layer, registry, kRest, {}, {});
    tickWith(layer, registry, kRest, {}, {});
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(layer.Paused(), "Escape pauses the level");
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::None);
    CHECK_MSG(layer.SimLevel() != nullptr, "which stays loaded under the pause");
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(!layer.Paused() && layer.SimLevel() != nullptr, "and Escape again resumes it");
}

// A touch on the main menu is Button::update's: the button under a touch that
// went down inside it draws at 0.80 for as long as it is held there, nothing
// happens while it is, and the release asks for chapter select, which is up on the
// tick after - under its own black, every clock from nothing (A-S5, A-M8).
void TheMainMenuActsOnTheRelease() {
    namespace MainMenu = MagicPortals::MainMenu;
    using Screen = MagicPortalsLayer::Screen;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty() || !TickToTheMainMenu(layer, registry)) {
        CHECK_MSG(false, "no main menu: " + layer.LoadError());
        return;
    }
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const glm::vec2 play = ScreenOfView(layer, MainMenu::SettledPlayRect(layer.MainMenuRules(), layer.ViewPx()).Centre());
    tickWith(layer, registry, play, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
    // TAP START's centre is under the title's rectangle too, which follows the
    // touch as well (Button::update runs on both).
    const unsigned both = MainMenu::Bit(MainMenu::Button::Play) | MainMenu::Bit(MainMenu::Button::Title);
    CHECK_MSG(layer.MainMenuHeld() == both, "down on TAP START: held, and the title with it");
    for (int tick = 0; tick < 14; ++tick) tickWith(layer, registry, play, {MagicPortalsLayer::kTap}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Main && layer.MainMenuHeld() == both,
              "250 ms held: nothing happens, and it is still held");
    if (OriginalArtIsThere("TheMainMenuActsOnTheRelease")) {
        const std::vector<ScreenOverlay::Quad> frame = MenuFrame(registry, layer);
        const int index = IndexOfImage(frame, "main_play_game_button.png");
        const float blink = static_cast<float>(
            MagicPortals::MenuState::BlinkAt(layer.MainMenuRules().playBlink, layer.MenuStateMs()).colour);
        CHECK_MSG(index >= 0 && NearD(frame[static_cast<std::size_t>(index)].color.r, 0.8 * blink, 0.005),
                  "drawn at the press tint 0.80");
    }
    releaseWith(layer, registry, play);
    CHECK_MSG(layer.MenuScreen() == Screen::Main && layer.MainMenuHeld() == 0u,
              "the release tick is drawn in the old state, untinted");
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Worlds, "chapter select on the tick after the release");
    CHECK_EQ(layer.MenuStateTicks(), 0);
    const std::vector<ScreenOverlay::Quad> black = MenuFrame(registry, layer);
    CHECK_MSG(!black.empty() && black.back().texture.empty() && black.back().color.a == 1.0f,
              "under a whole black");
    const std::vector<std::string> sounds = layer.LatchedSounds();
    CHECK_MSG(std::find(sounds.begin(), sounds.end(), "menu_button") != sounds.end(), "with the menu's own noise");

    // A touch that goes down on TAP START and is let go off it presses nothing.
    const glm::vec2 away(1100.0f, 300.0f);
    pressBack(layer, registry);
    tickWith(layer, registry, kRest, {}, {});
    CHECK(layer.MenuScreen() == Screen::Main);
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    tickWith(layer, registry, play, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
    tickWith(layer, registry, away, {MagicPortalsLayer::kTap}, {});
    CHECK_MSG(layer.MainMenuHeld() == 0u, "dragged off it: no longer tinted");
    releaseWith(layer, registry, away);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Main, "and let go off it: no press");
}

// The loading screen: 81 ticks of walking, the character gone on the 81st, the
// 1000 ms hold, and the main menu's first tick the 143rd (owner ruling R4).
void TheLoadingScreenLeadsToTheMenu() {
    using Screen = MagicPortalsLayer::Screen;
    namespace Loading = MagicPortals::Loading;
    if (!OriginalArtIsThere("TheLoadingScreenLeadsToTheMenu")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    tickWith(layer, registry, kRest, {}, {});
    CHECK(layer.MenuScreen() == Screen::Loading && layer.MenuStateTicks() == 0);
    CHECK_EQ(Tagged(registry, "Magic Portals Loading Background"), 1);
    CHECK_EQ(Tagged(registry, "Magic Portals Loading Character"), 1);
    CHECK_EQ(Tagged(registry, "Magic Portals Loading Portal"), 1);
    if (const entt::entity mage = FirstTagged(registry, "Magic Portals Loading Character"); mage != entt::null) {
        // The level's sheet, as the level draws it: the hd file at a 40 x 56 frame.
        const std::string& texture = registry.get<MaterialComponent>(mage).albedoTexturePath;
        const glm::vec3 scale = registry.get<TransformComponent>(mage).scale;
        const double perMetre = MagicPortals::Units::kPixelsPerMetre;
        CHECK_MSG(EndsWith(texture, "/entities/hd/magic_portals_hd.png") &&
                      std::fabs(scale.x * perMetre - 40.0) < 1e-3 && std::fabs(scale.y * perMetre - 56.0) < 1e-3,
                  "the loading screen's character: " + texture);
    }
    {
        const std::vector<ScreenOverlay::Quad> frame = MenuFrame(registry, layer);
        const int logo = IndexOfImage(frame, "asanteegameslogo.png");
        CHECK_MSG(!frame.empty() && frame.front().texture.empty() && frame.front().color.a == 1.0f,
                  "the first frame is under a whole black");
        CHECK_MSG(logo == static_cast<int>(frame.size()) - 1, "and the logo is over it, last");
        CHECK_MSG(CountCaption(frame, "Matura84_shadow") >= 12, "with the dots written between");
    }
    const int menuFrame = Loading::MenuFrame(layer.LoadingRules(), 1000.0 / 60.0);
    for (int tick = 2; tick <= menuFrame; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        if (tick == 80) CHECK_EQ(Tagged(registry, "Magic Portals Loading Character"), 1);
        if (tick == 81) {
            CHECK_MSG(Tagged(registry, "Magic Portals Loading Character") == 0, "hidden as the last texture loads");
            CHECK_MSG(CountCaption(MenuFrame(registry, layer), "Matura84_shadow") >= 12, "the dots' last frame");
        }
        if (tick == 82) CHECK_MSG(CountCaption(MenuFrame(registry, layer), "Matura84_shadow") == 0, "then none");
    }
    CHECK_MSG(layer.MenuScreen() == Screen::Loading, "the hold's last frame is still the loading screen");
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Main && layer.MenuStateTicks() == 0, "the main menu on the next");
    CHECK_EQ(Tagged(registry, "Magic Portals Loading Background"), 0);
    CHECK(layer.LatchedSounds().empty());
}

// Every menu state opens under ONE black of its own, 1 -> 0 over 700 ms of the
// tick, over everything; another page of the same grid does not (A-S1). The back
// key goes up a state on the tick after, as a release does.
void TheMenuStatesOpenUnderABlack() {
    using Screen = MagicPortalsLayer::Screen;
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty() || !TickToTheMainMenu(layer, registry)) {
        CHECK_MSG(false, "no main menu: " + layer.LoadError());
        return;
    }
    const auto blackAlpha = [&]() {
        const std::vector<ScreenOverlay::Quad> frame = MenuFrame(registry, layer);
        return !frame.empty() && frame.back().texture.empty() && frame.back().color.r == 0.0f
                   ? frame.back().color.a
                   : 0.0f;
    };
    CHECK_EQ(blackAlpha(), 1.0f);
    for (int tick = 0; tick < 21; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(NearD(blackAlpha(), 127.0 / 255.0, 1e-6), "350 ms in: half, linear");
    for (int tick = 0; tick < 21; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(blackAlpha() == 0.0f, "700 ms in: gone");

    const MagicPortalsLayer::MenuButton* play = MenuButtonOf(layer, Kind::Play);
    if (play == nullptr) return;
    layer.PressMenu(registry, *play);
    CHECK(layer.MenuScreen() == Screen::Worlds);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuStateTicks() == 0 && blackAlpha() == 1.0f, "chapter select: its own black");
    for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, {}, {});
    const MagicPortalsLayer::MenuButton* world = MenuButtonOf(layer, Kind::World, 0);
    if (world == nullptr) return;
    layer.PressMenu(registry, *world);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Levels && blackAlpha() == 1.0f, "the grid: its own black");
    for (int tick = 0; tick < 50; ++tick) tickWith(layer, registry, kRest, {}, {});
    const MagicPortalsLayer::MenuButton* forward = MenuButtonOf(layer, Kind::Forward);
    if (forward == nullptr) return;
    const int before = layer.MenuStateTicks();
    layer.PressMenu(registry, *forward);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuStateTicks() == before + 1 && blackAlpha() == 0.0f, "another page is not another state");

    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(layer.MenuScreen() == Screen::Levels, "the back key's tick is still the grid");
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Worlds && blackAlpha() == 1.0f, "then chapter select, under a black");
    press(layer, registry, MagicPortalsLayer::kBack);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Main && layer.MenuStateTicks() == 0, "then the main menu");
}

// OWNER RULING R2: the back key on the main menu leaves the game, as
// MainMenuLayer::update's Exit does.
void TheBackKeyOnTheMainMenuQuits() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty() || !TickToTheMainMenu(layer, registry)) {
        CHECK_MSG(false, "no main menu: " + layer.LoadError());
        return;
    }
    Application::ClearQuitRequest();
    tickWith(layer, registry, kRest, {}, {});
    CHECK(!Application::QuitRequested());
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK_MSG(Application::QuitRequested(), "the back key asks the app to stop");
    // The latch is the process's: put it back for every case after this one.
    Application::ClearQuitRequest();
}

// The switches act on the release tick itself; info and Achievements make their
// noise and lead nowhere yet - their screens are ui3 sections 3 and 4.
void TheMainMenusSwitchesAndCornerButtons() {
    namespace MainMenu = MagicPortals::MainMenu;
    using Screen = MagicPortalsLayer::Screen;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty() || !TickToTheMainMenu(layer, registry)) {
        CHECK_MSG(false, "no main menu: " + layer.LoadError());
        return;
    }
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const auto touchAt = [&](double xPx, double yPx) {
        const glm::vec2 at(static_cast<float>(xPx), static_cast<float>(yPx));
        tickWith(layer, registry, at, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
        releaseWith(layer, registry, at);
    };
    CHECK(layer.SoundOn());
    touchAt(45.0, 675.0);
    CHECK_MSG(!layer.SoundOn() && layer.MenuScreen() == Screen::Main, "the sound switch, on its release tick");
    for (int tick = 0; tick < 43; ++tick) tickWith(layer, registry, kRest, {}, {});
    const unsigned musicBit = MainMenu::Bit(MainMenu::Button::Music);
    CHECK_MSG((MainMenu::ButtonsAt(layer.MainMenuRules(), layer.MainMenuSwitches(), layer.ViewPx(), layer.MenuStateMs(),
                                   glm::dvec2(135.0, 675.0) / 2.8125) &
               musicBit) == 0u,
              "A-M3: the music switch has gone with the sound");
    if (OriginalArtIsThere("TheMainMenusSwitchesAndCornerButtons")) {
        const std::vector<ScreenOverlay::Quad> frame = MenuFrame(registry, layer);
        CHECK(IndexOfImage(frame, "sound_mute.png") >= 0 && IndexOfImage(frame, "music_on.png") < 0 &&
              IndexOfImage(frame, "music_off.png") < 0);
    }
    touchAt(45.0, 675.0);
    CHECK(layer.SoundOn());
    for (int tick = 0; tick < 43; ++tick) tickWith(layer, registry, kRest, {}, {});
    touchAt(135.0, 675.0);
    CHECK_MSG(!layer.MusicOn(), "the music switch, back in afresh, turns the music off");
    touchAt(135.0, 675.0);
    CHECK(layer.MusicOn());

    touchAt(45.0, 45.0);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Credits, "info opens the credits (ui3 spec 3)");
    std::vector<std::string> sounds = layer.LatchedSounds();
    CHECK_MSG(std::count(sounds.begin(), sounds.end(), "level_button") == 1,
              "with getItemSelectButtonSoundName's noise");
    press(layer, registry, MagicPortalsLayer::kBack);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Main, "the back key goes back to the main menu");
    for (int tick = 0; tick < 43; ++tick) tickWith(layer, registry, kRest, {}, {});
    touchAt(1100.0, 675.0);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.MenuScreen() == Screen::Achievements, "Achievements opens the dashboard (ui3 spec 4)");
    sounds = layer.LatchedSounds();
    CHECK_MSG(std::count(sounds.begin(), sounds.end(), "level_button") == 2,
              "with the same noise: the latch keeps both presses'");
}

// A-S6: a page tile refuses a touch that travelled more than 48 u while held.
void ATileRefusesATouchThatTravelled() {
    using Screen = MagicPortalsLayer::Screen;
    using Kind = MagicPortalsLayer::MenuButton::Kind;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty() || !TickToTheMainMenu(layer, registry)) {
        CHECK_MSG(false, "no main menu: " + layer.LoadError());
        return;
    }
    const MagicPortalsLayer::MenuButton* play = MenuButtonOf(layer, Kind::Play);
    if (play == nullptr) return;
    layer.PressMenu(registry, *play);
    for (int tick = 0; tick < 5; ++tick) tickWith(layer, registry, kRest, {}, {});
    const MagicPortalsLayer::MenuButton* icon = MenuButtonOf(layer, Kind::World, 0);
    if (icon == nullptr) return;
    const glm::dvec2 centre = icon->centrePx;
    const double half = icon->sizePx.y * 0.5;
    CHECK_MSG(half > 31.0, "the icon is tall enough to drag 60 u inside it");
    const auto drag = [&](double travel) {
        const glm::vec2 from = ScreenOfView(layer, centre - glm::dvec2(0.0, half - 1.0));
        const glm::vec2 to = ScreenOfView(layer, centre - glm::dvec2(0.0, half - 1.0 - travel));
        tickWith(layer, registry, from, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
        tickWith(layer, registry, to, {MagicPortalsLayer::kTap}, {});
        releaseWith(layer, registry, to);
        tickWith(layer, registry, kRest, {}, {});
    };
    drag(60.0);
    CHECK_MSG(layer.MenuScreen() == Screen::Worlds, "dragged 60 u and let go inside: no action");
    drag(40.0);
    CHECK_MSG(layer.MenuScreen() == Screen::Levels, "dragged 40 u: it acts");
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
    // And each is DRAWN, not merely listed - through the screen overlay now, as
    // the original's UI is drawn, so nothing of the screen is in the registry.
    // A button with no picture would be an invisible button that counting the
    // buttons themselves would never catch.
    CHECK_EQ(Tagged(registry, "Magic Portals Menu Button"), 0);
    {
        // A tick into its entrance: on its first they are all at alpha 0, which
        // the overlay is not sent.
        tickWith(layer, registry, kRest, {}, {});
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        CHECK_MSG(IndexOfImage(frame, "button_restart.png") >= 0 && IndexOfImage(frame, "button_right.png") >= 0 &&
                      IndexOfImage(frame, "list_button.png") >= 0,
                  "the three buttons are in the frame");
    }

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
    // Under the tutorial popup the level stands still, its particles with it.
    for (int frame = 0; frame < 30; ++frame) layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    CHECK_MSG(Tagged(registry, "Magic Portals Particle") == 0, "no particle is carried while a popup has time stopped");
    CloseTheLevelStartPopup(layer, registry);
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
    CloseTheLevelStartPopup(layer, registry);
    for (int frame = 0; frame < 120; ++frame) layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    const int before = Tagged(registry, "Magic Portals Particle");
    CHECK(before > 0);

    // A retry rebuilds the level from what was read when it loaded - and raises
    // 1-02's popup again, as Game::preLoop does on every load.
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK_MSG(layer.PopupOpen(), "a retry of 1-02 raises its popup again");
    CHECK_MSG(Tagged(registry, "Magic Portals Particle") == 0,
              "a retry left " + std::to_string(Tagged(registry, "Magic Portals Particle")) + " particle(s) behind");
    CloseTheLevelStartPopup(layer, registry);
    for (int frame = 0; frame < 120; ++frame) layer.OnUpdate(registry, MagicPortalsLayer::kTick);
    CHECK_MSG(Tagged(registry, "Magic Portals Particle") <= before,
              "the level came back with more particles than it had");
}

// ---- the emitters conform (step 59, plan_port System 2) ------------------------

// Defined further down, beside the checks that first needed them.
entt::entity SpriteOf(entt::registry& registry, const char* tag, const std::string& file);

// The first report of `node`'s system `slot`, or null.
const MagicPortalsLayer::EmitterReport* ReportOf(const std::vector<MagicPortalsLayer::EmitterReport>& reports,
                                                 const std::string& node, int slot) {
    for (const MagicPortalsLayer::EmitterReport& report : reports) {
        if (report.node == node && report.slot == slot) return &report;
    }
    return nullptr;
}

void PutPlayerAt(entt::registry& registry, const MagicPortalsLayer& layer, const glm::dvec2& atPx) {
    auto& transform = registry.get<TransformComponent>(layer.SimLevel()->player);
    const glm::vec3 world = MagicPortals::Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(world.x, world.y, transform.position.z);
}

// 1-1's torch and static portals, as the original places and draws their systems:
//  - 2a: the flame starts at the torch's NODE plus (0, -12), (288, 52), not at
//    the centre of its picture 16 lower;
//  - 2b: the rings turn counter-clockwise, the engine's rotation growing 1.8
//    degrees a frame;
//  - 2c: every added particle is drawn at alpha 1, the rings' fading colour
//    notwithstanding;
//  - 2d: each system in its entity's own slot, after the halo's quarter, the
//    iris after the rings;
//  - 2e: every quad square.
void TheTorchAndThePortalsEmitAsTheOriginalDoes() {
    if (!OriginalArtIsThere("TheTorchAndThePortalsEmitAsTheOriginalDoes")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    CloseTheLevelStartPopup(layer, registry);

    const float slotZ = 0.004f; // kSpriteSlotZ: one sprite's slot
    const double turnPerFrame = 1.8 * 3.14159265358979323846 / 180.0;
    std::vector<std::pair<entt::entity, float>> lastRings;
    int turnedLeft = 0;  // counter-clockwise: the engine's rotation grew by 1.8 degrees
    int turnedRight = 0; // clockwise by the same
    int addedNotWhole = 0;
    int notSquare = 0;
    int flameBirths = 0;
    int flameBirthsOff = 0;
    float widestRing = 0.0f;
    for (int frame = 0; frame < 240; ++frame) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);

        const std::vector<MagicPortalsLayer::EmitterReport> reports = layer.EmitterReports();
        if (const auto* flame = ReportOf(reports, "light_ent_696", 0); flame != nullptr && flame->anyReleased &&
                                                                        flame->newestAgeMs == 0.0) {
            ++flameBirths;
            if (flame->newestBornPx != glm::dvec2(288.0, 52.0)) ++flameBirthsOff;
        }

        std::vector<std::pair<entt::entity, float>> rings;
        for (auto [entity, tag, material, transform] :
             registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
            if (tag.tag != "Magic Portals Particle") continue;
            if (transform.scale.x != transform.scale.y) ++notSquare;
            if (material.blend == MaterialComponent::BlendMode::Additive && material.albedoColor.a != 1.0f) {
                ++addedNotWhole;
            }
            if (!EndsWith(material.albedoTexturePath, "/portal_particle.png")) continue;
            widestRing = std::max(widestRing, transform.scale.x);
            rings.emplace_back(entity, transform.rotation.z);
            for (const auto& [was, rotation] : lastRings) {
                if (was != entity) continue;
                const double d = static_cast<double>(transform.rotation.z - rotation);
                if (std::fabs(d - turnPerFrame) < 1e-4) ++turnedLeft;
                if (std::fabs(d + turnPerFrame) < 1e-4) ++turnedRight;
            }
        }
        lastRings = std::move(rings);
    }
    std::printf("  1-1's rings: %d frame pairs turned counter-clockwise, %d clockwise; %d flame births\n", turnedLeft,
                turnedRight, flameBirths);
    CHECK_MSG(turnedLeft > 200 && turnedRight == 0,
              "the rings turn counter-clockwise: " + std::to_string(turnedLeft) + " left, " + std::to_string(turnedRight) +
                  " right");
    CHECK_MSG(addedNotWhole == 0, std::to_string(addedNotWhole) + " added particle draw(s) with an alpha other than 1");
    CHECK_MSG(notSquare == 0, std::to_string(notSquare) + " particle draw(s) not square");
    CHECK_MSG(widestRing <= MagicPortals::Units::ToMetres(88.0) + 1e-6f &&
                  widestRing > MagicPortals::Units::ToMetres(80.0),
              "no ring drawn wider than the 88 units it is born at: " + std::to_string(widestRing));
    CHECK_MSG(flameBirths > 20 && flameBirthsOff == 0,
              "the flame starts at (288, 52): " + std::to_string(flameBirthsOff) + " of " + std::to_string(flameBirths) +
                  " births elsewhere");

    const std::vector<MagicPortalsLayer::EmitterReport> reports = layer.EmitterReports();
    const auto* flame = ReportOf(reports, "light_ent_696", 0);
    CHECK(flame != nullptr);
    if (flame != nullptr) {
        CHECK_MSG(flame->owner.atPx == glm::dvec2(288.0, 64.0) && flame->owner.angleDeg == 0.0,
                  "the flame's owner is the torch's node: " + Point(flame->owner.atPx));
        CHECK_MSG(std::fabs(flame->z - (flame->ownerZ + 0.375f * slotZ)) < 1e-6f,
                  "the torch's one system three eighths of a slot in front of it: " + std::to_string(flame->z) + " over " +
                      std::to_string(flame->ownerZ));
        const entt::entity halo = FirstTagged(registry, "Magic Portals Halo");
        CHECK(halo != entt::null);
        if (halo != entt::null) {
            const float haloZ = registry.get<TransformComponent>(halo).position.z;
            CHECK_MSG(haloZ > flame->ownerZ && haloZ < flame->z, "the halo between the torch and its flame");
        }
    }
    int portals = 0;
    for (const MagicPortalsLayer::EmitterReport& ring : reports) {
        if (ring.slot != 0 || ring.bitmap != "portal_particle.png") continue;
        const auto* iris = ReportOf(reports, ring.node, 1);
        CHECK_MSG(iris != nullptr && iris->bitmap == "portal.png" && !iris->additive && ring.additive && ring.systems == 2,
                  ring.node + ": the rings added, the iris mixed");
        if (iris == nullptr) continue;
        // Scaled with their entity by its script's 0.8: rings born at 88, not 110,
        // and the iris 51.2, not 64.
        CHECK_MSG(std::fabs(ring.systemSize - 88.0) < 1e-9 && std::fabs(iris->systemSize - 51.2) < 1e-9,
                  ring.node + ": rings " + std::to_string(ring.systemSize) + ", iris " + std::to_string(iris->systemSize));
        ++portals;
        CHECK_MSG(ring.z > ring.ownerZ + 0.25f * slotZ && ring.z < iris->z && iris->z < ring.ownerZ + 0.5f * slotZ,
                  ring.node + ": halo quarter < rings < iris < half a slot: " + std::to_string(ring.z - ring.ownerZ) +
                      ", " + std::to_string(iris->z - ring.ownerZ));
    }
    CHECK_EQ(portals, 4);
    layer.OnDetach(registry);
}

// 2f: an emitter follows what moves its picture, and its newest life starts where
// the owner is on the frame it starts. A swinging shock ring (4-15's, the body
// Fields moves), a carried key (3-08's, the key Keys trails behind the player),
// and a crystal taken, whose sparkle goes with it.
void AnEmitterFollowsItsOwner() {
    if (!OriginalArtIsThere("AnEmitterFollowsItsOwner")) return;
    {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level14c");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
        CloseTheLevelStartPopup(layer, registry);
        const std::string ring = "shock_agent_2353";
        double lowest = 1e9;
        double highest = -1e9;
        int off = 0;
        int births = 0;
        int birthsOff = 0;
        for (int frame = 0; frame < 300; ++frame) {
            tickWith(layer, registry, kRest, {}, {});
            layer.OnUpdate(registry, MagicPortalsLayer::kTick);
            const auto* field = layer.SimLevel()->fields.FindField(ring);
            const std::vector<MagicPortalsLayer::EmitterReport> reports = layer.EmitterReports();
            const auto* emitter = ReportOf(reports, ring, 0);
            if (field == nullptr || emitter == nullptr) {
                ++off;
                continue;
            }
            if (glm::distance(emitter->owner.atPx, field->atPx) > 0.01) ++off;
            lowest = std::min(lowest, emitter->owner.atPx.y);
            highest = std::max(highest, emitter->owner.atPx.y);
            if (emitter->anyReleased && emitter->newestAgeMs == 0.0) {
                ++births;
                // shock_agent.ent: StartPoint (0, 0), RandStartPoint (12, 18).
                const glm::dvec2 d = emitter->newestBornPx - emitter->owner.atPx;
                if (std::fabs(d.x) > 6.0 + 1e-6 || std::fabs(d.y) > 9.0 + 1e-6) ++birthsOff;
            }
        }
        std::printf("  4-15's ring: emitter y %.2f..%.2f px, %d births\n", lowest, highest, births);
        CHECK_MSG(off == 0, std::to_string(off) + " frame(s) the ring's emitter was not on the ring");
        CHECK_MSG(highest - lowest > 60.0, "and the ring swung its 64 px: " + std::to_string(highest - lowest));
        CHECK_MSG(births > 20 && birthsOff == 0,
                  std::to_string(birthsOff) + " of " + std::to_string(births) + " births away from the ring");
        layer.OnDetach(registry);
    }
    {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level7b");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
        CloseTheLevelStartPopup(layer, registry);
        const std::string name = "key_ent_1335";
        const auto* lying = layer.SimLevel()->keys.FindKey(name);
        CHECK(lying != nullptr);
        if (lying == nullptr) return;
        const glm::dvec2 lyingAt = lying->atPx;
        PutPlayerAt(registry, layer, lyingAt);
        tickWith(layer, registry, kRest, {}, {});
        CHECK_MSG(layer.SimLevel()->keys.FindKey(name)->owner == layer.SimLevel()->player, "the player takes the key");
        PutPlayerAt(registry, layer, glm::dvec2(lyingAt.x + 300.0, lyingAt.y));

        const entt::entity picture = SpriteOf(registry, "Magic Portals Sprite", "/key.png");
        CHECK(picture != entt::null);
        int off = 0;
        int births = 0;
        int birthsOff = 0;
        for (int frame = 0; frame < 300; ++frame) {
            tickWith(layer, registry, kRest, {}, {});
            layer.OnUpdate(registry, MagicPortalsLayer::kTick);
            const glm::dvec2 keyAt = layer.SimLevel()->keys.FindKey(name)->atPx;
            if (picture != entt::null && registry.valid(picture)) {
                const glm::dvec2 drawnAt = MagicPortals::Units::ToPixels(registry.get<TransformComponent>(picture).position);
                if (glm::distance(drawnAt, keyAt) > 1e-3) ++off;
            }
            const std::vector<MagicPortalsLayer::EmitterReport> reports = layer.EmitterReports();
            for (int slot = 0; slot < 2; ++slot) {
                const auto* sparkle = ReportOf(reports, name, slot);
                if (sparkle == nullptr || sparkle->owner.atPx != keyAt) {
                    ++off;
                    continue;
                }
                if (!sparkle->anyReleased || sparkle->newestAgeMs != 0.0) continue;
                ++births;
                // key.ent: StartPoint (-2, -6) then (2, 6), RandStartPoint (8, 8) both.
                const glm::dvec2 start = slot == 0 ? glm::dvec2(-2.0, -6.0) : glm::dvec2(2.0, 6.0);
                const glm::dvec2 d = sparkle->newestBornPx - (keyAt + start);
                if (std::fabs(d.x) > 4.0 + 1e-6 || std::fabs(d.y) > 4.0 + 1e-6) ++birthsOff;
            }
        }
        const glm::dvec2 carriedTo = layer.SimLevel()->keys.FindKey(name)->atPx;
        std::printf("  3-08's key: carried from %s to %s, %d sparkle births\n", Point(lyingAt).c_str(),
                    Point(carriedTo).c_str(), births);
        CHECK_MSG(glm::distance(carriedTo, lyingAt) > 100.0, "the key was carried: " + Point(carriedTo));
        CHECK_MSG(off == 0, std::to_string(off) + " frame(s) the key's picture or a sparkle was not on the key");
        CHECK_MSG(births > 10 && birthsOff == 0,
                  std::to_string(birthsOff) + " of " + std::to_string(births) + " sparkle births away from the key");

        // The key carried to its keyhole: the keyhole holds, fades in its alpha as
        // Keys fades it, and goes with its sparkle (keys.json: 1000 ms, then 500).
        const std::string hole = "keyhole_1338";
        const auto* keyhole = layer.SimLevel()->keys.FindKeyhole(hole);
        CHECK(keyhole != nullptr);
        if (keyhole != nullptr) {
            const glm::dvec2 holeAt = keyhole->atPx;
            for (int tick = 0; tick < 30; ++tick) {
                PutPlayerAt(registry, layer, holeAt);
                tickWith(layer, registry, kRest, {}, {});
                layer.OnUpdate(registry, MagicPortalsLayer::kTick);
            }
            CHECK_MSG(layer.SimLevel()->keys.FindKeyhole(hole)->unlocked, "the key opened its keyhole");
            CHECK_MSG(layer.SimLevel()->keys.FindKey(name)->spent, "and is spent");
            int fading = 0;
            int offAlpha = 0;
            int keyGoneEarly = 0;
            bool goneWithSparkle = false;
            bool keyGoneWithIt = false;
            for (int tick = 0; tick < 120; ++tick) {
                tickWith(layer, registry, kRest, {}, {});
                layer.OnUpdate(registry, MagicPortalsLayer::kTick);
                const auto* now = layer.SimLevel()->keys.FindKeyhole(hole);
                const entt::entity quad = SpriteOf(registry, "Magic Portals Sprite", "/keyhole.png");
                const std::vector<MagicPortalsLayer::EmitterReport> reports = layer.EmitterReports();
                if (now->gone) {
                    const auto* sparkle = ReportOf(reports, hole, 0);
                    goneWithSparkle = quad == entt::null && sparkle != nullptr && sparkle->ownerGone &&
                                      sparkle->drawn == 0;
                    // The spent key, whose fly-in is not built, goes with the keyhole it
                    // opened, both its sparkles with it, rather than lie where the door was.
                    const auto* first = ReportOf(reports, name, 0);
                    const auto* second = ReportOf(reports, name, 1);
                    keyGoneWithIt = !registry.valid(picture) && first != nullptr && second != nullptr &&
                                    first->ownerGone && first->drawn == 0 && second->ownerGone && second->drawn == 0;
                    break;
                }
                // Until then it holds on the keyhole, drawn.
                if (!registry.valid(picture)) ++keyGoneEarly;
                if (quad == entt::null) {
                    ++offAlpha;
                    continue;
                }
                const float alpha = registry.get<MaterialComponent>(quad).albedoColor.a;
                if (std::fabs(alpha - static_cast<float>(now->alpha)) > 1e-6f) ++offAlpha;
                if (now->alpha > 0.0 && now->alpha < 1.0) ++fading;
            }
            std::printf("  3-08's keyhole: %d fading ticks, then gone\n", fading);
            CHECK_MSG(fading >= 25 && offAlpha == 0,
                      std::to_string(fading) + " fading ticks, " + std::to_string(offAlpha) +
                          " with the picture's alpha not the keyhole's");
            CHECK_MSG(goneWithSparkle, "the faded keyhole's picture and sparkle went");
            CHECK_MSG(keyGoneEarly == 0, std::to_string(keyGoneEarly) + " tick(s) the spent key went before its keyhole");
            CHECK_MSG(keyGoneWithIt, "and the spent key's picture and both its sparkles went with the keyhole");
        }

        // A crystal taken takes its sparkle.
        const auto& crystals = layer.SimLevel()->goals.crystals;
        CHECK(!crystals.empty());
        if (!crystals.empty()) {
            const std::string crystal = crystals.front().name;
            const auto* before = ReportOf(layer.EmitterReports(), crystal, 0);
            CHECK(before != nullptr && !before->ownerGone);
            if (before != nullptr) {
                PutPlayerAt(registry, layer, before->owner.atPx);
                for (int tick = 0; tick < 5 && !layer.SimLevel()->goals.crystals.front().collected; ++tick) {
                    tickWith(layer, registry, kRest, {}, {});
                    layer.OnUpdate(registry, MagicPortalsLayer::kTick);
                }
                layer.OnUpdate(registry, MagicPortalsLayer::kTick);
                const auto* after = ReportOf(layer.EmitterReports(), crystal, 0);
                CHECK_MSG(layer.SimLevel()->goals.crystals.front().collected, "the crystal is taken");
                CHECK_MSG(after != nullptr && after->ownerGone && after->drawn == 0 && after->active == 0,
                          "and its sparkle went with it");
            }
        }
        layer.OnDetach(registry);
    }
    {
        // 3-16's shock diamond, taken and struck into a minion (test_mp_diamonds'
        // two frames): its picture and its spark go with it, and its twin stays.
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level15b");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
        CloseTheLevelStartPopup(layer, registry);
        const std::string name = "shock_diamond_ent_1741";
        const auto* diamond = layer.SimLevel()->diamonds.Find(name);
        CHECK(diamond != nullptr && !layer.SimLevel()->minions.minions.empty());
        if (diamond == nullptr || layer.SimLevel()->minions.minions.empty()) return;
        const glm::dvec2 lyingAt = diamond->atPx;
        const entt::entity minion = layer.SimLevel()->minions.minions[0].body;
        const auto pictures = [&registry]() {
            int n = 0;
            for (auto [entity, tag, material] : registry.view<TagComponent, MaterialComponent>().each()) {
                (void)entity;
                if (tag.tag == "Magic Portals Sprite" && EndsWith(material.albedoTexturePath, "/shock_diamond.png")) ++n;
            }
            return n;
        };
        CHECK_EQ(pictures(), 2);
        for (int frame = 0; frame < 2 && registry.valid(minion); ++frame) {
            PutPlayerAt(registry, layer, lyingAt);
            auto& at = registry.get<TransformComponent>(minion);
            const glm::vec3 world = MagicPortals::Units::ToWorld(lyingAt.x + 8.0, lyingAt.y);
            at.position = glm::vec3(world.x, world.y, at.position.z);
            tickWith(layer, registry, kRest, {}, {});
            layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        }
        const auto* spent = layer.SimLevel()->diamonds.Find(name);
        const auto* spark = ReportOf(layer.EmitterReports(), name, 0);
        CHECK_MSG(spent != nullptr && spent->gone, "the diamond struck the minion and spent itself");
        CHECK_MSG(pictures() == 1, "its picture went and its twin's stayed: " + std::to_string(pictures()));
        CHECK_MSG(spark != nullptr && spark->ownerGone && spark->drawn == 0, "and its spark went with it");
        layer.OnDetach(registry);
    }
}

// 2e on a cell that is NOT square: 3-24's shock agents draw tesla_shock_black_bg.png,
// cut 5 x 2 from 256 x 64, cells of 51.2 x 32 - and every quad is still size by size
// (ETHParticleManager.cpp:413). 1-1's cells are all square, so this is where a quad
// taking its cell's shape would show.
void AParticleOnANonSquareCellIsDrawnSquare() {
    if (!OriginalArtIsThere("AParticleOnANonSquareCellIsDrawnSquare")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level23b");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    CloseTheLevelStartPopup(layer, registry);

    int shocks = 0;
    for (const MagicPortalsLayer::EmitterReport& report : layer.EmitterReports()) {
        if (report.bitmap != "tesla_shock_black_bg.png") continue;
        ++shocks;
        CHECK_MSG(std::fabs(report.cellPx.x - 51.2) < 1e-9 && std::fabs(report.cellPx.y - 32.0) < 1e-9,
                  report.node + ": a 51.2 x 32 cell, " + std::to_string(report.cellPx.x) + " x " +
                      std::to_string(report.cellPx.y));
        // No tier file: drawn from the bitmap named (00_order 2.3).
        CHECK_MSG(EndsWith(report.image, "/particles/tesla_shock_black_bg.png"), report.image);
    }
    CHECK_MSG(shocks > 0, "3-24 places a system on tesla_shock_black_bg.png");

    int shockDraws = 0;
    int notSquare = 0;
    for (int frame = 0; frame < 120; ++frame) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        for (auto [entity, tag, material, transform] :
             registry.view<TagComponent, MaterialComponent, TransformComponent>().each()) {
            (void)entity;
            if (tag.tag != "Magic Portals Particle") continue;
            if (transform.scale.x != transform.scale.y) ++notSquare;
            if (EndsWith(material.albedoTexturePath, "/tesla_shock_black_bg.png")) ++shockDraws;
        }
    }
    std::printf("  3-24: %d shock-cell particle draws over 120 frames, %d not square\n", shockDraws, notSquare);
    CHECK_MSG(shockDraws > 0, "a shock particle was drawn");
    CHECK_MSG(notSquare == 0, std::to_string(notSquare) + " particle draw(s) not square");
    layer.OnDetach(registry);
}

// 2a/2b's owner angle where the layer converts it: 4-32's light wall, the one turned
// carrier of the 128 levels (the converter wrote rotation -1.5708, Ethanon's 90), a
// body, so its angle comes from its TransformComponent. Its sparkles' RandStartPoint
// (32, 128) is turned with it: births lie within 64 of the wall along x and 16 along y.
void ATurnedOwnerTurnsItsParticles() {
    if (!OriginalArtIsThere("ATurnedOwnerTurnsItsParticles")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level31c");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    CloseTheLevelStartPopup(layer, registry);

    const std::string wall = "light_wall_ent_2079";
    int births = 0;
    int outside = 0;
    int alongWall = 0; // further along x than an unturned box's 16 reaches
    int angleOff = 0;
    for (int frame = 0; frame < 240; ++frame) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const std::vector<MagicPortalsLayer::EmitterReport> reports = layer.EmitterReports();
        const auto* sparkle = ReportOf(reports, wall, 0);
        if (sparkle == nullptr || sparkle->ownerGone) {
            ++angleOff;
            continue;
        }
        if (std::fabs(sparkle->owner.angleDeg - 90.0) > 0.01) ++angleOff;
        if (!sparkle->anyReleased || sparkle->newestAgeMs != 0.0) continue;
        ++births;
        const glm::dvec2 d = sparkle->newestBornPx - sparkle->owner.atPx;
        if (std::fabs(d.x) > 64.0 + 1e-3 || std::fabs(d.y) > 16.0 + 1e-3) ++outside;
        if (std::fabs(d.x) > 16.0 + 1e-3) ++alongWall;
    }
    const auto* last = ReportOf(layer.EmitterReports(), wall, 0);
    std::printf("  4-32's light wall: angle %.4f, %d births, %d along the wall past 16\n",
                last != nullptr ? last->owner.angleDeg : -1.0, births, alongWall);
    CHECK_MSG(angleOff == 0, std::to_string(angleOff) + " frame(s) the wall's emitter was not at 90 degrees");
    CHECK_MSG(births > 3 && outside == 0,
              std::to_string(outside) + " of " + std::to_string(births) + " births outside the turned box");
    CHECK_MSG(alongWall > 0, "and the spread lies along the wall, not across it");
    layer.OnDetach(registry);
}

// K16: 2-32's dragon carries dragon.ent's one system, whose bitmap is
// particles/explosion.JPG. While the header reader knew only PNGs and BMPs it
// was dropped ("particle image could not be read"); it is built now, on its
// 32 x 32 cell, added, a pool of 5 at its entity's node, and it draws.
void TheDragonsFireIsBuiltFromItsJpeg() {
    if (!OriginalArtIsThere("TheDragonsFireIsBuiltFromItsJpeg")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level31a");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    CloseTheLevelStartPopup(layer, registry);

    const std::string dragon = "dragon_ent_1279";
    const std::vector<MagicPortalsLayer::EmitterReport> built = layer.EmitterReports();
    const auto* fire = ReportOf(built, dragon, 0);
    CHECK_MSG(fire != nullptr, "2-32's dragon has its explosion.JPG system");
    if (fire != nullptr) {
        CHECK_MSG(fire->bitmap == "explosion.JPG" && fire->cellPx == glm::dvec2(32.0, 32.0),
                  fire->bitmap + " on a cell of " + Point(fire->cellPx));
        CHECK_MSG(fire->additive && fire->systems == 1 && fire->count == 5 && std::fabs(fire->systemSize - 24.0) < 1e-9,
                  "dragon.ent's one system: added, a pool of 5, size 24");
    }
    const int jpegSystems = static_cast<int>(std::count_if(
        built.begin(), built.end(), [](const MagicPortalsLayer::EmitterReport& r) { return r.bitmap == "explosion.JPG"; }));
    CHECK_MSG(jpegSystems == 1, "one explosion.JPG system on 2-32: " + std::to_string(jpegSystems));

    int fireDraws = 0;
    for (int frame = 0; frame < 120; ++frame) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        for (auto [entity, tag, material] : registry.view<TagComponent, MaterialComponent>().each()) {
            (void)entity;
            if (tag.tag == "Magic Portals Particle" && EndsWith(material.albedoTexturePath, "/explosion.JPG")) ++fireDraws;
        }
    }
    std::printf("  2-32's dragon: %d explosion.JPG particle draws over 120 frames\n", fireDraws);
    CHECK_MSG(fireDraws > 0, "the dragon's fire is drawn");
    layer.OnDetach(registry);
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

    // 1-02 and 1-03 are walked, so their tutorial popups are closed first.
    CloseTheLevelStartPopup(layer, registry);

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
    //
    // BUT A LEVEL CAN BE TOO NARROW TO PAN AT ALL, and level1 now is. It is
    // 512 px against a 455 px view - 57 px of travel in total - and since the
    // camera opens on the player rather than easing in from camera_start, that
    // is all the movement there will ever be: not enough to carry any sprite
    // across an edge. The suite used to get its crossings there from the opening
    // pan, which is gone by the owner's choice.
    //
    // So the crossing is required where the geometry can produce one and stated
    // where it cannot. level2 has 313 px of slack and still reports its 30 and
    // 22, which is what keeps this check honest rather than merely quiet.
    const double slackPx = bounds.x - viewPx.x;
    if (slackPx > 100.0) {
        CHECK_MSG(entered + left > 0, std::string(levelName) +
                                          ": no sprite ever crossed the edge of the view, so this proves nothing");
    } else {
        std::printf("  %s: only %.0f px of pan, too narrow for a crossing - the blink count stands on the wider level\n",
                    levelName, slackPx);
    }
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

    // The screen's furniture is drawn, not just the medal - through the screen
    // overlay, in display values (spec D9), and nothing of it in the registry.
    // A second in, so every piece is whole.
    CHECK_EQ(Tagged(registry, "Magic Portals Finish Veil"), 0);
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
    const int veil = IndexOfImage(frame, "fade_edge.png");
    const int title = IndexOfImage(frame, "level_finished.png");
    const int plaque = IndexOfImage(frame, "portals_created_plaque.png");
    const int restart = IndexOfImage(frame, "button_restart.png");
    const int list = IndexOfImage(frame, "list_button.png");
    const int medal = IndexOfImage(frame, "medal_gold_l.png");
    CHECK_MSG(veil >= 0 && title > veil && plaque > title && restart > plaque && list > restart && medal > list,
              "the veil, the banner, the plaque, the buttons, then the medal: " + std::to_string(veil) + " " +
                  std::to_string(title) + " " + std::to_string(plaque) + " " + std::to_string(restart) + " " +
                  std::to_string(list) + " " + std::to_string(medal));
    // THE VEIL DIMS, and it is a GRADIENT: fade_edge.png at 200 of 255, three
    // strips of a clamped texture one and a half views wide from the top-left.
    CHECK_EQ(CountImage(frame, "fade_edge.png"), 3);
    if (veil >= 0 && CountImage(frame, "fade_edge.png") == 3) {
        const ScreenOverlay::Quad& left = frame[static_cast<std::size_t>(veil)];
        const ScreenOverlay::Quad& right = frame[static_cast<std::size_t>(veil + 2)];
        CHECK_MSG(::test::nearly(left.color.a, 200.0f / 255.0f) && left.min == glm::vec2(0.0f) &&
                      ::test::nearly(right.max.x, 1.5f) && ::test::nearly(right.max.y, 1.0f),
                  "the veil carries the original's 200, from (0, 0) to 1.5 views wide");
    }
    // The count, "0", in Matura128_shadow after the medal; no golden plaque on gold.
    CHECK_MSG(CountCaption(frame, "Matura128_shadow.fnt") == 1, "the counter is one glyph");
    CHECK(IndexOfImage(frame, "golden_score_plaque.png") < 0);
    // Restart and pause went at the door, and the pads have faded out.
    CHECK(IndexOfImage(frame, layer.HudRules().restart.sprite) < 0 && IndexOfImage(frame, layer.HudRules().pause.sprite) < 0);

    // AND THEY GO WHEN THE SCREEN DOES.
    layer.OnDetach(registry);
    CHECK_MSG(HudFrame(registry, layer).empty(), "nothing of the screen is left once the level is gone");
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
    const Selector::Board& grid = layer.SelectorBoard();
    CHECK_MSG(std::count_if(grid.tiles.begin(), grid.tiles.end(),
                            [](const Selector::Tile& tile) { return tile.medal > 0; }) == 1 &&
                  !grid.tiles.empty() && grid.tiles[0].medal > 0,
              "the level just cleared wears a medal on the grid, and only it does");

    // ON THE BUTTON, not beside it.
    //
    // buildMenu creates the medal but menuTick places it, so this needs a tick
    // before there is a position to read at all - without one the transform is
    // still at the origin and the check below would be measuring nothing.
    tickWith(layer, registry, kRest, {}, {});

    // Drawn through the overlay since step 56: the medal is the grid's piece, and
    // sim/Selector's suite pins it at the tile's top-left + (36, 36).
    const std::vector<Selector::Piece> drawn = Selector::Pieces(layer.SelectorRules(), grid, layer.SelectorState(),
                                                               layer.MenuBoxPx(), 1.0e9, 0.0, Selector::Control::None, -1);
    const auto medalPiece = std::find_if(drawn.begin(), drawn.end(), [](const Selector::Piece& piece) {
        return piece.file.rfind("medal_", 0) == 0;
    });
    const MagicPortalsLayer::MenuButton* wearer = nullptr;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::Level || button.level != 0) continue;
        wearer = &button;
        break;
    }
    CHECK_MSG(wearer != nullptr, "level0 has a button on the grid");
    CHECK_MSG(medalPiece != drawn.end(), "the grid draws the medal");
    if (medalPiece != drawn.end() && wearer != nullptr) {
        // The original draws it from the button's TOP-LEFT plus (36, 36) of a
        // 64px button, so its centre lands 20 in from the button's own centre
        // against a half-width of 32 - a badge on the corner. Measured from the
        // centre instead it came out at 52, which is a medal floating in the
        // gap beside the button touching nothing. This tells those two apart,
        // which counting the medals cannot.
        // MagicPortals::Units, qualified: this file brings in Supersonic and
        // the layer by name, not the whole of the game's namespace.
        const glm::dvec2 at = medalPiece->rect.Centre();
        const double dx = std::fabs(at.x - wearer->centrePx.x);
        const double dy = std::fabs(at.y - wearer->centrePx.y);
        CHECK_MSG(dx < wearer->sizePx.x * 0.5 && dy < wearer->sizePx.y * 0.5,
                  "the medal sits on its button rather than floating beside it");
    }

    layer.OnDetach(registry);
    CHECK_MSG(Tagged(registry, "Magic Portals Menu Medal") == 0,
              "and the medal goes with the screen, or a resize stacks another");
}

// ---- The HUD and the start of a level (steps 38 and 39) ---------------------
//
// sim/Hud is pinned number by number in test_mp_hud. What is held here is that
// the layer sends the engine's screen overlay what those numbers say: each
// picture where its rectangle is and at its alpha, in the ORIGINAL'S ORDER OF
// DRAWING - which, with no depth in the overlay, is all the layering there is;
// shown and hidden when the original's are; and a tap on one spent on it.

// This frame's HUD, as the layer sends it: the same call OnUpdate makes, into
// an overlay the suite publishes, with none of the rest of a frame.
std::vector<ScreenOverlay::Quad> HudFrame(entt::registry& registry, const MagicPortalsLayer& layer) {
    static ScreenOverlay overlay;
    overlay.Clear();
    registry.ctx().insert_or_assign<ScreenOverlay*>(&overlay);
    layer.EmitHud(registry);
    return overlay.Quads();
}

bool EndsWith(const std::string& path, const std::string& file) {
    return path.size() >= file.size() && path.compare(path.size() - file.size(), file.size(), file) == 0;
}

// The first quad showing `file`, and where it is in the frame's order; -1 when
// the frame has none.
int IndexOfImage(const std::vector<ScreenOverlay::Quad>& quads, const std::string& file) {
    for (std::size_t i = 0; i < quads.size(); ++i) {
        if (EndsWith(quads[i].texture, "/" + file)) return static_cast<int>(i);
    }
    return -1;
}

int CountImage(const std::vector<ScreenOverlay::Quad>& quads, const std::string& file) {
    return static_cast<int>(std::count_if(quads.begin(), quads.end(), [&file](const ScreenOverlay::Quad& quad) {
        return EndsWith(quad.texture, "/" + file);
    }));
}

// Where the opening's blacks are in the frame: untextured quads painted black.
std::vector<int> BlackIndices(const std::vector<ScreenOverlay::Quad>& quads) {
    std::vector<int> found;
    for (std::size_t i = 0; i < quads.size(); ++i) {
        if (quads[i].texture.empty() && quads[i].color.r == 0.0f && quads[i].color.g == 0.0f &&
            quads[i].color.b == 0.0f) {
            found.push_back(static_cast<int>(i));
        }
    }
    return found;
}

int CountCaption(const std::vector<ScreenOverlay::Quad>& quads, const std::string& font) {
    const std::string stem = std::filesystem::path(font).stem().string();
    return static_cast<int>(std::count_if(quads.begin(), quads.end(), [&stem](const ScreenOverlay::Quad& quad) {
        return quad.texture.find(stem) != std::string::npos;
    }));
}

// A quad's rectangle on the view, in design units.
Hud::Rect OnView(const MagicPortalsLayer& layer, const ScreenOverlay::Quad& quad) {
    const glm::dvec2 view = layer.ViewPx();
    Hud::Rect rect;
    rect.min = glm::dvec2(quad.min) * view;
    rect.size = glm::dvec2(quad.max - quad.min) * view;
    return rect;
}

bool SameRect(const Hud::Rect& a, const Hud::Rect& b, double eps = 1e-3) {
    return glm::length(a.min - b.min) < eps && glm::length(a.size - b.size) < eps;
}

std::string ShowRect(const Hud::Rect& rect) {
    return Point(rect.min) + " " + std::to_string(rect.size.x) + " x " + std::to_string(rect.size.y);
}

// Where a point on the view shows on the 1280x720 viewport the suites publish.
glm::vec2 ScreenOfView(const MagicPortalsLayer& layer, const glm::dvec2& onView) {
    const glm::dvec2 view = layer.ViewPx();
    return glm::vec2(static_cast<float>(onView.x / view.x * 1280.0), static_cast<float>(onView.y / view.y * 720.0));
}

bool NearD(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) <= eps;
}

void TheHudIsTheOriginals() {
    if (!OriginalArtIsThere("TheHudIsTheOriginals")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level4");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const Hud::Rules& rules = layer.HudRules();
    CHECK_MSG(layer.HudError().empty(), "ui.json read: " + layer.HudError());
    CHECK_EQ(layer.LevelAgeMs(), 0.0);

    // The first tick: the level is a tick old, all black, "Part 5" over it.
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(NearD(layer.LevelAgeMs(), 1000.0 / 60.0, 1e-3), "a tick old: " + std::to_string(layer.LevelAgeMs()));
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const std::vector<int> blacks = BlackIndices(frame);
        CHECK_MSG(blacks.size() == 2, "a level opens under two blacks: " + std::to_string(blacks.size()));
        for (const int at : blacks) {
            const ScreenOverlay::Quad& black = frame[static_cast<std::size_t>(at)];
            CHECK_MSG(black.min == glm::vec2(0.0f) && black.max == glm::vec2(1.0f) && black.color.a > 0.97f,
                      "each covering the whole image, nearly opaque: " + std::to_string(black.color.a));
        }
        const int left = IndexOfImage(frame, rules.pads.leftSprite);
        const int right = IndexOfImage(frame, rules.pads.rightSprite);
        const int pause = IndexOfImage(frame, rules.pause.sprite);
        const int restart = IndexOfImage(frame, rules.restart.sprite);
        if (blacks.size() == 2) {
            CHECK_MSG(blacks[0] < left && left < blacks[1] && blacks[0] < right && right < blacks[1],
                      "the pads are drawn between the two blacks, as the original's controllers are");
            CHECK_MSG(restart >= 0 && pause >= 0 && restart < blacks[0] && pause < blacks[0],
                      "restart and pause are under both");
        }
        const std::string font = std::string(MAGICPORTALS_ORIGINAL_DIR) + "/data/" + rules.caption.font;
        std::error_code ec;
        if (std::filesystem::is_regular_file(font, ec)) {
            // "Part 5": five letters and the space, each its own quad, over all of it.
            CHECK_EQ(CountCaption(frame, rules.caption.font), 6);
            CHECK_MSG(!blacks.empty() && !frame.empty() &&
                          frame.back().texture.find("Matura84") != std::string::npos,
                      "the caption is the last thing drawn");
        }
    }
    // Both pads are still outside their corners at the first tick.
    Hud::Rect left;
    CHECK(layer.ControlRect(MagicPortalsLayer::Control::Left, left));
    CHECK_MSG(left.Max().x <= 0.0, "the left pad starts off the screen: " + ShowRect(left));

    // Just before the blacks' own clock starts (ui.json's start_after_ms, 465 ms
    // on the level's age), the pads have slid nearly home and the plaque would be
    // coming up - and all of it is still under two whole blacks, which is why
    // rec11's first lit frame already has them settled.
    for (int tick = 1; tick < 27; ++tick) tickWith(layer, registry, kRest, {}, {});
    {
        CHECK_MSG(layer.LevelAgeMs() < rules.overlay.startAfterMs, "27 ticks is before the black starts");
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const std::vector<int> blacks = BlackIndices(frame);
        CHECK_MSG(blacks.size() == 2, "still two blacks at 450 ms: " + std::to_string(blacks.size()));
        for (const int at : blacks) {
            CHECK_MSG(frame[static_cast<std::size_t>(at)].color.a == 1.0f,
                      "each still whole: " + std::to_string(frame[static_cast<std::size_t>(at)].color.a));
        }
    }

    // 1.2 s in: the black (465 + 700 ms) and the slide are over, the HUD is settled.
    for (int tick = 27; tick < 72; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::vector<ScreenOverlay::Quad> settled = HudFrame(registry, layer);
    CHECK_MSG(BlackIndices(settled).empty(), "no black once the fade is over");
    const glm::dvec2 view = layer.ViewPx();
    const struct {
        MagicPortalsLayer::Control control;
        const std::string& file;
        Hud::Rect want;
    } expected[] = {
        {MagicPortalsLayer::Control::Menu, rules.pause.sprite, {glm::dvec2(view.x - 32.0, 0.0), glm::dvec2(32.0)}},
        {MagicPortalsLayer::Control::Reset, rules.restart.sprite, {glm::dvec2(view.x - 64.0, 0.0), glm::dvec2(32.0)}},
        {MagicPortalsLayer::Control::Left, rules.pads.leftSprite, {glm::dvec2(0.0, 192.0), glm::dvec2(64.0)}},
        {MagicPortalsLayer::Control::Right, rules.pads.rightSprite, {glm::dvec2(view.x - 64.0, 192.0), glm::dvec2(64.0)}},
    };
    for (const auto& one : expected) {
        Hud::Rect laidOut;
        CHECK_MSG(layer.ControlRect(one.control, laidOut) && SameRect(laidOut, one.want),
                  one.file + " laid out at " + ShowRect(laidOut) + ", wanted " + ShowRect(one.want));
        const int at = IndexOfImage(settled, one.file);
        CHECK_MSG(at >= 0 && CountImage(settled, one.file) == 1, one.file + " is drawn, once");
        if (at < 0) continue;
        const ScreenOverlay::Quad& quad = settled[static_cast<std::size_t>(at)];
        CHECK_MSG(SameRect(OnView(layer, quad), one.want, 1e-3), one.file + " drawn at " + ShowRect(OnView(layer, quad)));
        CHECK_MSG(quad.color.r == 1.0f && quad.color.g == 1.0f && quad.color.b == 1.0f, one.file + " untinted");
        // 120 of 255. The pads are still in their short pulse, which runs 4.2 s.
        if (one.control == MagicPortalsLayer::Control::Menu || one.control == MagicPortalsLayer::Control::Reset) {
            CHECK_MSG(NearD(quad.color.a, 120.0 / 255.0, 1e-6), one.file + " at 120 of 255");
        }
    }
    Hud::Rect clear;
    CHECK_MSG(!layer.ControlRect(MagicPortalsLayer::Control::Clear, clear) &&
                  IndexOfImage(settled, rules.clearPortals.sprite) < 0,
              "no clear-portals button with no portal placed");
    CHECK_MSG(IndexOfImage(settled, rules.pads.ringSprite) < 0, "rings are the tutorial's alone");
    CHECK_MSG(IndexOfImage(settled, rules.plaque.sprite) < 0,
              "a bare layer has no medal recorded, so no plaque - as on a fresh save");

    // The text the port used to lay over a level is gone while it is played.
    int spoken = 0;
    for (auto [entity, text] : registry.view<UITextComponent>().each()) {
        (void)entity;
        if (!text.text.empty()) ++spoken;
    }
    CHECK_MSG(spoken == 0, "no text over a level being played: " + std::to_string(spoken) + " line(s) say something");

    // Nothing of the HUD is a quad in the level any more: the scene target's
    // tone map is what made it wrong.
    CHECK_EQ(Tagged(registry, "Magic Portals Control Left"), 0);
    CHECK_EQ(Tagged(registry, "Magic Portals Level Start Black"), 0);

    // Past the pads' pulse and the caption: flat 120, nothing left of the opening.
    for (int tick = 72; tick < 260; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::vector<ScreenOverlay::Quad> later = HudFrame(registry, layer);
    if (const int pad = IndexOfImage(later, rules.pads.leftSprite); pad >= 0) {
        CHECK_MSG(NearD(later[static_cast<std::size_t>(pad)].color.a, 120.0 / 255.0, 1e-6),
                  "a pad flat at 120 of 255 once its pulse is over");
    }
    CHECK_EQ(CountCaption(later, rules.caption.font), 0);
    layer.OnDetach(registry);
}

void TheHudStaysOnTheViewAsTheCameraMoves() {
    if (!OriginalArtIsThere("TheHudStaysOnTheViewAsTheCameraMoves")) return;
    // level30 is 768 wide, so walking right pans the camera. The overlay is laid
    // out on the screen, so the pause button cannot move with the camera at all.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level30");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const std::string& pause = layer.HudRules().pause.sprite;
    const double startX = layer.CameraCentrePx().x;
    double worst = 0.0;
    int seen = 0;
    for (int tick = 0; tick < 240; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
        if (tick < 60) continue;
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int at = IndexOfImage(frame, pause);
        if (at < 0) break;
        ++seen;
        const Hud::Rect drawn = OnView(layer, frame[static_cast<std::size_t>(at)]);
        worst = std::max(worst, glm::length(drawn.min - glm::dvec2(layer.ViewPx().x - 32.0, 0.0)));
    }
    CHECK_MSG(layer.CameraCentrePx().x > startX + 20.0,
              "the camera panned: " + std::to_string(startX) + " -> " + std::to_string(layer.CameraCentrePx().x));
    CHECK_MSG(seen == 180 && worst < 1e-3,
              "the pause button never left its corner while it did: worst " + std::to_string(worst));
    layer.OnDetach(registry);
}

// The sky is not drawn where the level puts it: StaticSky pins it to the camera
// every frame (sim/Sky.hpp, step 58). level30 is 768 wide and places its
// icy_sky.png at (228.5, 128), 0.94 px right of the view's centre at the level's
// corner, so drawn where the level said it fails on the first tick. Walking
// right pans the camera, and the sky must be the view's centre on every tick AND
// on every frame drawn between ticks, where both are interpolated.
void TheSkyStaysOnTheViewAsTheCameraMoves() {
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level30");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty() || !layer.ArtError().empty()) {
        CHECK_MSG(false, layer.LoadError() + layer.ArtError());
        return;
    }
    const MagicPortals::Sky::Controller& sky = layer.SkyController();
    CHECK_MSG(sky.running && sky.skies.size() == 1 && !sky.scroll && !sky.hasSatellite,
              "level30 has one still sky and no satellite");
    entt::entity quad = entt::null;
    for (auto [entity, tag, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        if (tag.tag == "Magic Portals Sprite" && material.albedoTexturePath.find("icy_sky.png") != std::string::npos) {
            quad = entity;
        }
    }
    CHECK(quad != entt::null);
    if (quad == entt::null) return;
    CHECK_MSG(registry.all_of<InterpolatedTransformComponent>(quad), "interpolated, as the camera is");
    const auto near = [](const glm::vec3& a, const glm::vec3& b) {
        return std::fabs(a.x - b.x) <= 1e-5f && std::fabs(a.y - b.y) <= 1e-5f;
    };
    const glm::dvec2 opened = MagicPortals::Units::ToPixels(registry.get<TransformComponent>(quad).position);
    CHECK_MSG(glm::length(opened - layer.CameraCentrePx()) < 1e-3,
              "at load, the view's centre " + Point(layer.CameraCentrePx()) + ", not the level's (228.5, 128): " +
                  Point(opened));

    const double startX = layer.CameraCentrePx().x;
    int ticksOff = 0;
    int framesOff = 0;
    int frames = 0;
    for (int tick = 0; tick < 240; ++tick) {
        InterpolationSystem::BeginTick(registry);
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight},
                 tick == 0 ? std::vector<std::string>{MagicPortalsLayer::kRight} : std::vector<std::string>{});
        InterpolationSystem::EndTick(registry);
        if (layer.MenuScreen() != MagicPortalsLayer::Screen::None) break;
        const glm::vec3 camera = registry.get<CameraComponent>(primaryCamera(registry)).position;
        if (!near(registry.get<TransformComponent>(quad).position, camera)) ++ticksOff;
        for (int f = 1; f <= 4; ++f) {
            InterpolationSystem::Apply(registry, static_cast<float>(f) / 4.0f);
            ++frames;
            const glm::vec3 drawnCamera = registry.get<CameraComponent>(primaryCamera(registry)).position;
            if (!near(registry.get<TransformComponent>(quad).position, drawnCamera)) ++framesOff;
        }
    }
    std::printf("  level30: camera %.1f -> %.1f px, sky off the view's centre on %d tick(s) and %d of %d frame(s)\n",
                startX, layer.CameraCentrePx().x, ticksOff, framesOff, frames);
    CHECK_MSG(layer.CameraCentrePx().x > startX + 20.0, "the camera panned");
    CHECK_EQ(ticksOff, 0);
    CHECK_EQ(framesOff, 0);
    layer.OnDetach(registry);
}

void AClearPortalsButtonComesWithAPortal() {
    if (!OriginalArtIsThere("AClearPortalsButtonComesWithAPortal")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level30");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const std::string& file = layer.HudRules().clearPortals.sprite;
    waitForFirstTap(layer, registry);
    Hud::Rect clear;
    CHECK(!layer.ControlRect(MagicPortalsLayer::Control::Clear, clear));

    // ATapLandsWhereItPoints' first landing aim.
    const glm::dvec2 aim(250.0, 150.0);
    tap(layer, registry, screenOf(registry, aim));
    landShot(layer, registry);
    CHECK_EQ(layer.SimLevel()->portals.placed.size(), std::size_t{1});
    CHECK_EQ(layer.SimLevel()->portals.portalsUsed, 1);
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int at = IndexOfImage(frame, file);
        CHECK_MSG(layer.ControlRect(MagicPortalsLayer::Control::Clear, clear) &&
                      SameRect(clear, Hud::Rect{glm::dvec2(0.0), glm::dvec2(32.0)}) && at >= 0 &&
                      SameRect(OnView(layer, frame[static_cast<std::size_t>(at < 0 ? 0 : at)]), clear),
                  "a placed portal puts the clear-portals button up, flush top-left: " + ShowRect(clear));
        CHECK_MSG(at >= 0 && at > IndexOfImage(frame, layer.HudRules().pause.sprite),
                  "drawn after restart and pause, as PortalManager adds it after GameLayer's");
    }

    // A tap on it: the portal goes, the count it cost comes back - it never
    // carried anything - and no shot is fired into the corner under it.
    const int fired = layer.SimLevel()->portals.shotsFired;
    tap(layer, registry, ScreenOfView(layer, clear.Centre()));
    CHECK_MSG(layer.SimLevel()->portals.placed.empty(), "pressing it clears the placed portals");
    CHECK_MSG(layer.SimLevel()->portals.portalsUsed == 0,
              "and gives back the one that carried nothing: " +
                  std::to_string(layer.SimLevel()->portals.portalsUsed));
    CHECK_MSG(layer.SimLevel()->portals.shotsFired == fired, "the tap was spent on the button");
    const std::vector<std::string> sounds = layer.LatchedSounds();
    CHECK_MSG(std::find(sounds.begin(), sounds.end(), "portal_spent") != sounds.end(),
              "with playPortalKilledSound");
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(!layer.ControlRect(MagicPortalsLayer::Control::Clear, clear) &&
                  IndexOfImage(HudFrame(registry, layer), file) < 0,
              "and with no portal left the button goes");
    layer.OnDetach(registry);
}

void APlaqueForALevelWithAMedal() {
    if (!OriginalArtIsThere("APlaqueForALevelWithAMedal")) return;
    // Memory-only medals, earned the way a player earns one: level0 by holding
    // right, then the medal screen's own retry. The level it reopens has a medal
    // recorded, so it opens with the plaque, as the original's preLoop does.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const Hud::Rules& rules = layer.HudRules();
    tickWith(layer, registry, kRest, {}, {});
    CHECK_EQ(IndexOfImage(HudFrame(registry, layer), rules.plaque.sprite), -1);
    for (int tick = 0; tick < 600 && layer.MenuScreen() != MagicPortalsLayer::Screen::Finished; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    }
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::Finished);
    if (layer.MenuScreen() != MagicPortalsLayer::Screen::Finished) return;
    {
        // The medal screen is drawn over the level through the overlay, with
        // nothing of GameLayer under it: restart and pause went at the door, and
        // the level-start plaque was never up on a fresh save. A tick in, since
        // nothing of the screen is sent at its alpha 0.
        tickWith(layer, registry, kRest, {}, {});
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        CHECK_MSG(IndexOfImage(frame, "level_finished.png") >= 0 && IndexOfImage(frame, rules.restart.sprite) < 0 &&
                      IndexOfImage(frame, rules.pause.sprite) < 0 && IndexOfImage(frame, rules.plaque.sprite) < 0,
                  "the medal screen puts nothing of GameLayer over the level");
    }
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::Retry) continue;
        layer.PressMenu(registry, button);
        break;
    }
    CHECK(IsAt(layer, "level0") && layer.MenuScreen() == MagicPortalsLayer::Screen::None);
    CHECK_EQ(layer.LevelAgeMs(), 0.0);

    // A tick in: the plaque and its medal are coming in, under both blacks.
    tickWith(layer, registry, kRest, {}, {});
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int plaque = IndexOfImage(frame, rules.plaque.sprite);
        int medal = -1;
        for (std::size_t i = 0; i < frame.size(); ++i) {
            if (frame[i].texture.find("medal_") != std::string::npos && EndsWith(frame[i].texture, "_l.png")) {
                medal = static_cast<int>(i);
            }
        }
        const std::vector<int> blacks = BlackIndices(frame);
        CHECK_MSG(plaque >= 0 && medal > plaque, "a level with a medal recorded opens with the plaque, medal over it");
        CHECK_MSG(!blacks.empty() && plaque < blacks.front(), "and both are under the black");
    }

    for (int tick = 1; tick < 90; ++tick) tickWith(layer, registry, kRest, {}, {});
    const auto plaqueAlpha = [&registry, &layer, &rules]() {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int at = IndexOfImage(frame, rules.plaque.sprite);
        return at < 0 ? 0.0 : static_cast<double>(frame[static_cast<std::size_t>(at)].color.a);
    };
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int at = IndexOfImage(frame, rules.plaque.sprite);
        const Hud::Rect want{rules.plaque.centreUnits - rules.plaque.sizeUnits * 0.5, rules.plaque.sizeUnits};
        CHECK_MSG(at >= 0 && SameRect(OnView(layer, frame[static_cast<std::size_t>(at < 0 ? 0 : at)]), want),
                  "the plaque at (8, 4) 64 x 128");
    }
    CHECK_MSG(plaqueAlpha() > 0.99, "held whole at 1.5 s");
    for (int tick = 90; tick < 150; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(plaqueAlpha() < 0.99 && plaqueAlpha() > 0.0, "going at 2.5 s: " + std::to_string(plaqueAlpha()));
    for (int tick = 150; tick < 185; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(plaqueAlpha() == 0.0, "and gone by 3 s");
    layer.OnDetach(registry);
}

void TheTutorialRingsAndAWeightlessLevelHasNoPads() {
    if (!OriginalArtIsThere("TheTutorialRingsAndAWeightlessLevelHasNoPads")) return;
    {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level0");
        layer.OnAttach(registry);
        if (!layer.LoadError().empty()) {
            CHECK_MSG(false, layer.LoadError());
            return;
        }
        const Hud::Rules& rules = layer.HudRules();
        // 300 ms: the long pulse's first peak, 210 of 255 - to a byte, because
        // eighteen float ticks come to a hair over 300 ms and uint() truncates.
        for (int tick = 0; tick < 18; ++tick) tickWith(layer, registry, kRest, {}, {});
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int pad = IndexOfImage(frame, rules.pads.rightSprite);
        if (pad >= 0) {
            CHECK_MSG(NearD(frame[static_cast<std::size_t>(pad)].color.a, 210.0 / 255.0, 1.01 / 255.0),
                      "the tutorial pad at its first peak: " + std::to_string(frame[static_cast<std::size_t>(pad)].color.a));
        }
        const int ring = IndexOfImage(frame, rules.pads.ringSprite);
        CHECK_EQ(CountImage(frame, rules.pads.ringSprite), 2);
        CHECK_MSG(ring >= 0 && ring < IndexOfImage(frame, rules.restart.sprite) && ring < pad,
                  "the rings are drawn first, from the pads' update, under everything");
        // A retry is a new level to the original, and to its clock.
        press(layer, registry, MagicPortalsLayer::kRetry);
        CHECK_MSG(layer.LevelAgeMs() < 20.0, "a retry starts the level's clock again: " +
                                                 std::to_string(layer.LevelAgeMs()));
        CHECK_EQ(BlackIndices(HudFrame(registry, layer)).size(), std::size_t{2});
        layer.OnDetach(registry);
    }
    {
        // test_mp_zerog's weightless level: MainCharacter neither updates nor
        // draws the pads when noGravity is set.
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level1c");
        layer.OnAttach(registry);
        if (!layer.LoadError().empty()) {
            CHECK_MSG(false, layer.LoadError());
            return;
        }
        const Hud::Rules& rules = layer.HudRules();
        for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        Hud::Rect rect;
        CHECK_MSG(layer.SimLevel()->portals.noGravity && !layer.ControlRect(MagicPortalsLayer::Control::Left, rect) &&
                      IndexOfImage(frame, rules.pads.leftSprite) < 0 && IndexOfImage(frame, rules.pads.rightSprite) < 0,
                  "a weightless level has no walk pads");
        CHECK_MSG(CountImage(frame, rules.pause.sprite) == 1 && CountImage(frame, rules.restart.sprite) == 1,
                  "and keeps restart and pause");
        layer.OnDetach(registry);
    }
}

void TheNoPortalSignIsPinnedToTheCorner() {
    if (!OriginalArtIsThere("TheNoPortalSignIsPinnedToTheCorner")) return;
    {
        // 1-15 places no_portal_sign.ent off the level, at (-61, -24).
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level14");
        layer.OnAttach(registry);
        if (!layer.LoadError().empty()) {
            CHECK_MSG(false, layer.LoadError());
            return;
        }
        Hud::Rect sign;
        CHECK_MSG(layer.NoPortalSignRect(sign), "level14 has the no-portal sign");
        // It is no longer one of the level's own pictures.
        int inLevel = 0;
        for (auto [entity, material] : registry.view<MaterialComponent>().each()) {
            (void)entity;
            if (material.albedoTexturePath.find("no_portal_symbol_small") != std::string::npos) ++inLevel;
        }
        CHECK_MSG(inLevel == 0, "and does not draw it where the level put it: " + std::to_string(inLevel));

        // Held while the blacks are whole, then handed that time in one piece
        // (Hud::HandOver): 3 units from its corner by 700 ms, not 13.
        const Hud::Rect atLoad = sign;
        for (int tick = 0; tick < 27; ++tick) tickWith(layer, registry, kRest, {}, {}); // 450 ms
        CHECK(layer.NoPortalSignRect(sign));
        CHECK_MSG(SameRect(sign, atLoad), "unmoved under the whole blacks: " + ShowRect(sign));
        for (int tick = 27; tick < 42; ++tick) tickWith(layer, registry, kRest, {}, {}); // 700 ms
        CHECK(layer.NoPortalSignRect(sign));
        CHECK_MSG(glm::length(sign.min) < 3.5, "nearly in its corner as the picture comes through: " + ShowRect(sign));
        const Hud::Rect atSevenHundred = sign;

        for (int tick = 42; tick < 270; ++tick) tickWith(layer, registry, kRest, {}, {}); // 4.5 s
        CHECK(layer.NoPortalSignRect(sign));
        CHECK_MSG(glm::length(sign.min) < 0.36 && SameRect(sign, Hud::Rect{sign.min, glm::dvec2(64.0)}),
                  "at 4.5 s it sits flush in the top-left corner, 64 units: " + ShowRect(sign));
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        CHECK_MSG(!frame.empty() && frame.front().texture.find("no_portal_symbol_small") != std::string::npos,
                  "drawn first, under everything the HUD draws");
        if (!frame.empty()) {
            CHECK_MSG(frame.front().color == glm::vec4(1.0f), "at full opacity, untinted");
            std::error_code ec;
            const std::string hd = std::string(MAGICPORTALS_ORIGINAL_DIR) + "/entities/hd/no_portal_symbol_small.png";
            if (std::filesystem::is_regular_file(hd, ec)) {
                CHECK_MSG(frame.front().texture.find("entities/hd/") != std::string::npos,
                          "with the hd art the original draws: " + frame.front().texture);
            }
        }

        // A retry is a new level to the sign as well. Pressed while the blacks
        // are still whole, it must not hand the attempt before's held time to the
        // next one's chase: at 700 ms it is where a first attempt has it.
        press(layer, registry, MagicPortalsLayer::kRetry);
        for (int tick = 1; tick < 20; ++tick) tickWith(layer, registry, kRest, {}, {});
        press(layer, registry, MagicPortalsLayer::kRetry);
        for (int tick = 1; tick < 42; ++tick) tickWith(layer, registry, kRest, {}, {});
        CHECK(layer.NoPortalSignRect(sign));
        CHECK_MSG(NearD(layer.LevelAgeMs(), 700.0, 1e-3) && SameRect(sign, atSevenHundred),
                  "a retry during the hold starts the sign afresh: " + ShowRect(sign) + " against " +
                      ShowRect(atSevenHundred));
        layer.OnDetach(registry);
    }
    {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level0");
        layer.OnAttach(registry);
        if (!layer.LoadError().empty()) {
            CHECK_MSG(false, layer.LoadError());
            return;
        }
        Hud::Rect sign;
        CHECK_MSG(!layer.NoPortalSignRect(sign),
                  "level0 grants no portal either, but places no sign, and shows none - the entity is the trigger");
        layer.OnDetach(registry);
    }
}

// ---- the pause ------------------------------------------------------------------
//
// sim/Pause is pinned number by number in test_mp_hud. What is held here is the
// layer's side of CustomGameMenuLayer: the pause control and Escape open it OVER
// the level, which stops - nothing moves, the pads go, the level's age stands
// still - while the pause comes in on its own clock; it is drawn after restart
// and pause and before the blacks; it resumes the level exactly where the tap
// left it; and each of its buttons goes where the original's goes.

// A tap on the in-level pause control, where it is drawn.
bool TapThePauseControl(MagicPortalsLayer& layer, entt::registry& registry, std::vector<std::string> down = {}) {
    Hud::Rect control;
    if (!layer.ControlRect(MagicPortalsLayer::Control::Menu, control)) return false;
    down.push_back(MagicPortalsLayer::kTap);
    tickWith(layer, registry, ScreenOfView(layer, control.Centre()), std::move(down), {MagicPortalsLayer::kTap});
    return true;
}

// A tap on one of the pause's buttons, where the pause draws it on this tick.
bool TapPauseButton(MagicPortalsLayer& layer, entt::registry& registry, MagicPortals::Pause::Button button) {
    const std::vector<MagicPortals::Pause::Sprite> sprites =
        MagicPortals::Pause::Sprites(layer.PauseRules(), layer.PausedLevel(), layer.PauseSwitches(), layer.ViewPx(),
                                     layer.PauseClockMs() + 1000.0 / 60.0);
    for (const MagicPortals::Pause::Sprite& sprite : sprites) {
        if (sprite.element != MagicPortals::Pause::Element::Button || sprite.button != button) continue;
        tap(layer, registry, ScreenOfView(layer, sprite.rect.Centre()));
        return true;
    }
    return false;
}

// From a level out to its world's level grid, the way a player goes since the
// pause was built: the level's popup closed if it raised one, the back key to the
// pause, its entrance run, and its Levels button. True once the grid is up.
bool OutToTheGrid(MagicPortalsLayer& layer, entt::registry& registry) {
    CloseTheLevelStartPopup(layer, registry);
    press(layer, registry, MagicPortalsLayer::kBack);
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    if (!TapPauseButton(layer, registry, MagicPortals::Pause::Button::Levels)) return false;
    for (int tick = 0; tick < 60 && layer.MenuScreen() != MagicPortalsLayer::Screen::Levels; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
    }
    return layer.MenuScreen() == MagicPortalsLayer::Screen::Levels;
}

// The untextured black quads of a frame that are the pause's dim: whole-view,
// and the only black drawn once the level's own blacks have lifted.
int CountBlacks(const std::vector<ScreenOverlay::Quad>& quads) {
    return static_cast<int>(BlackIndices(quads).size());
}

void ThePauseStopsTheLevelUnderIt() {
    if (!OriginalArtIsThere("ThePauseStopsTheLevelUnderIt")) return;
    namespace Pause = MagicPortals::Pause;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    CHECK_MSG(layer.PauseRules().dimAlphaByte == 200, "ui.json's pause read at attach");
    // Walking right, half a second in: the caption is still fading and the player
    // is moving, which is what a stopped world has to be told apart from.
    for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    CHECK_MSG(TapThePauseControl(layer, registry, {MagicPortalsLayer::kRight}), "the pause control is there to tap");
    CHECK_MSG(layer.Paused(), "the pause control opens the pause");
    CHECK_MSG(layer.SimLevel() != nullptr && layer.MenuScreen() == MagicPortalsLayer::Screen::None &&
                  IsAt(layer, "level0"),
              "over the level, which stays loaded: it no longer leaves for the grid");
    if (!layer.Paused() || layer.SimLevel() == nullptr) return;
    CHECK_EQ(layer.PauseClockMs(), 0.0);
    CHECK_EQ(layer.PausedLevel().savedMedal, 0);
    CHECK_EQ(layer.PausedLevel().index, 0);

    const double age = layer.LevelAgeMs();
    const double frame = layer.LevelFrameMs();
    const glm::dvec2 at = playerPx(registry, layer);
    const glm::vec3 velocity = registry.get<RigidBodyComponent>(layer.SimLevel()->player).velocity;
    CHECK_MSG(std::fabs(velocity.x) > 0.0f, "the player was walking when the pause came");

    // Two seconds under it, the right arrow still held.
    for (int tick = 0; tick < 120; ++tick) tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    CHECK(layer.Paused());
    CHECK_MSG(glm::length(playerPx(registry, layer) - at) == 0.0,
              "the world does not move: the player is where the tap left it, " + Point(playerPx(registry, layer)));
    CHECK_MSG(registry.get<RigidBodyComponent>(layer.SimLevel()->player).velocity == velocity,
              "and still carries the speed it had, to take up again");
    CHECK_EQ(layer.LevelAgeMs(), age);
    // The tick is a float's 1/60 s, so 120 of them are 2000.0001 ms.
    CHECK_MSG(NearD(layer.PauseClockMs(), 2000.0, 1e-3), "the pause's own clock runs: " +
                                                             std::to_string(layer.PauseClockMs()));
    CHECK_MSG(NearD(layer.LevelFrameMs(), frame + 2000.0, 1e-3), "and the level's frame clock with it");

    Hud::Rect rect;
    CHECK_MSG(!layer.ControlRect(MagicPortalsLayer::Control::Left, rect) &&
                  !layer.ControlRect(MagicPortalsLayer::Control::Right, rect),
              "the walk pads are not drawn under a pause");
    CHECK_MSG(layer.ControlRect(MagicPortalsLayer::Control::Reset, rect) &&
                  layer.ControlRect(MagicPortalsLayer::Control::Menu, rect),
              "restart and pause are, frozen under its dim");

    const Hud::Rules& hud = layer.HudRules();
    const Pause::Rules& rules = layer.PauseRules();
    const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
    const int restart = IndexOfImage(quads, hud.restart.sprite);
    const int pause = IndexOfImage(quads, hud.pause.sprite);
    const std::vector<int> blacks = BlackIndices(quads);
    CHECK_MSG(IndexOfImage(quads, hud.pads.leftSprite) < 0 && IndexOfImage(quads, hud.pads.rightSprite) < 0,
              "no pad in the frame");
    CHECK_MSG(blacks.size() == 1, "one black, the pause's dim, once the level's blacks have lifted: " +
                                      std::to_string(blacks.size()));
    if (blacks.size() != 1 || restart < 0 || pause < 0) return;
    const ScreenOverlay::Quad& dim = quads[static_cast<std::size_t>(blacks[0])];
    CHECK_MSG(restart < blacks[0] && pause < blacks[0], "restart and pause are drawn under the dim");
    // Half a second into the level GameLayer's two are still coming in as UIButtons
    // (700 ms, linear): their own 120 times fTOu(516.7 / 700 * 255) = 188, frozen there.
    CHECK_MSG(NearD(quads[static_cast<std::size_t>(restart)].color.a,
                    120.0 / 255.0 *
                        MagicPortals::UiLayer::ButtonAlphaByte(layer.LevelEndRules().layer, layer.LevelAgeMs()) / 255.0,
                    1e-6),
              "at their own 120 times the entrance they had reached, which the dim takes lower still");
    CHECK_MSG(dim.min == glm::vec2(0.0f) && dim.max == glm::vec2(1.0f) && NearD(dim.color.a, 200.0 / 255.0, 1e-6),
              "the dim covers the view at 200 of 255 once it is in");
    const int golden = IndexOfImage(quads, rules.goldenPlaque.sprite);
    const int resume = IndexOfImage(quads, rules.resume.sprite);
    const int levels = IndexOfImage(quads, rules.levels.sprite);
    const int scores = IndexOfImage(quads, rules.achievements.sprite);
    const int sound = IndexOfImage(quads, rules.sound.sprite);
    const int music = IndexOfImage(quads, rules.music.sprite);
    CHECK_MSG(golden > blacks[0] && resume > golden && levels > golden && scores > golden && sound > golden &&
                  music > golden,
              "the golden plaque over the dim, and the buttons over the sprites");
    CHECK_MSG(IndexOfImage(quads, rules.currentPlaque.sprite) < 0 && IndexOfImage(quads, rules.skip.sprite) < 0,
              "A-P1: a level never finished has no current plaque and no skip");
    if (resume >= 0) {
        const Hud::Rect want = MagicPortals::UiLayer::RectAt(rules.resume, glm::dvec2(0.58, 0.44) * layer.ViewPx());
        CHECK_MSG(SameRect(OnView(layer, quads[static_cast<std::size_t>(resume)]), want) &&
                      quads[static_cast<std::size_t>(resume)].color.a == 1.0f,
              "resume at its place, whole");
    }
    const std::string font = std::string(MAGICPORTALS_ORIGINAL_DIR) + "/data/" + rules.title.font;
    std::error_code ec;
    if (std::filesystem::is_regular_file(font, ec)) {
        // "Part 1" and "0" over the pause, then the level-start caption - still
        // fading on the frame clock, 2.5 s into it - last of all.
        CHECK_EQ(CountCaption(quads, rules.title.font), 6 + 1 + 6);
        const ScreenOverlay::Quad& last = quads.back();
        CHECK_MSG(last.texture.find("Matura84") != std::string::npos &&
                      NearD(last.color.a, Hud::CaptionAlpha(hud, layer.LevelFrameMs()), 1e-6) &&
                      last.color.a < quads[quads.size() - 7].color.a,
                  "the level-start caption goes on fading above the pause: " + std::to_string(last.color.a));
    }

    // Under the dim, restart is not pressed: nothing below the pause reads a tap.
    if (layer.ControlRect(MagicPortalsLayer::Control::Reset, rect)) {
        tap(layer, registry, ScreenOfView(layer, rect.Centre()));
        CHECK_MSG(layer.Paused() && layer.LevelAgeMs() == age, "a tap on restart under the pause does nothing");
    }
}

void ThePauseResumesWhereTheTapLeftIt() {
    if (!OriginalArtIsThere("ThePauseResumesWhereTheTapLeftIt")) return;
    // Two runs of level0. The first walks right, is paused for a second and a
    // half with the arrow held, and resumed; the second walks right straight
    // through. After as many ticks of game time, the two players stand in the
    // same place: the pause took exactly nothing from the level.
    const auto run = [](bool paused, double& ageOut, glm::dvec2& atOut) {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level0");
        layer.OnAttach(registry);
        if (!layer.LoadError().empty()) return false;
        const std::vector<std::string> right = {MagicPortalsLayer::kRight};
        for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
        for (int tick = 0; tick < 40; ++tick) tickWith(layer, registry, kRest, right, {});
        if (paused) {
            tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight, MagicPortalsLayer::kBack},
                     {MagicPortalsLayer::kBack});
            CHECK_MSG(layer.Paused(), "Escape pauses a level being played");
            for (int tick = 0; tick < 90; ++tick) tickWith(layer, registry, kRest, right, {});
            tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight, MagicPortalsLayer::kBack},
                     {MagicPortalsLayer::kBack});
            CHECK_MSG(!layer.Paused(), "and Escape resumes it");
            // A-P9: the frame after, the whole layer is gone - no fade - and the
            // pads are back.
            const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
            CHECK_MSG(IndexOfImage(quads, layer.PauseRules().resume.sprite) < 0 && CountBlacks(quads) == 0 &&
                          IndexOfImage(quads, layer.HudRules().pads.leftSprite) >= 0,
                      "A-P9: the frame after resume has no pause in it, and has the pads");
        } else {
            tickWith(layer, registry, kRest, right, {});
        }
        for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, right, {});
        ageOut = layer.LevelAgeMs();
        atOut = playerPx(registry, layer);

        if (paused) {
            // A second pause plays its whole entrance again: UILayer::hide(true)
            // reset every element. A-P8, 350 ms in.
            tickWith(layer, registry, kRest, {MagicPortalsLayer::kBack}, {MagicPortalsLayer::kBack});
            CHECK(layer.Paused());
            {
                const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
                const std::vector<int> blacks = BlackIndices(quads);
                CHECK_MSG(blacks.empty() && IndexOfImage(quads, layer.PauseRules().resume.sprite) < 0,
                          "the tick a pause opens it has come in by nothing");
            }
            for (int tick = 0; tick < 21; ++tick) tickWith(layer, registry, kRest, {}, {});
            CHECK_MSG(NearD(layer.PauseClockMs(), 350.0, 1e-3), "21 ticks is 350 ms");
            const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
            const std::vector<int> blacks = BlackIndices(quads);
            const int levels = IndexOfImage(quads, layer.PauseRules().levels.sprite);
            CHECK_MSG(blacks.size() == 1 && NearD(1.0 - quads[static_cast<std::size_t>(blacks.front())].color.a,
                                                  0.590, 0.03),
                      "A-P8: the world at 0.590 +-0.03 under the dim");
            CHECK_MSG(levels >= 0 && NearD(quads[static_cast<std::size_t>(levels)].color.a, 0.50, 0.03),
                      "A-P8: back-to-levels at alpha 0.50 +-0.03");
            if (levels >= 0) {
                const Hud::Rect home =
                    MagicPortals::UiLayer::RectAt(layer.PauseRules().levels, glm::dvec2(0.42, 0.44) * layer.ViewPx());
                const double out = glm::length(OnView(layer, quads[static_cast<std::size_t>(levels)]).min - home.min);
                CHECK_MSG(NearD(out, 9.37, 0.5), "A-P8: and 9.37 +-0.5 units out: " + std::to_string(out));
            }
        }
        return true;
    };
    double pausedAge = 0.0;
    double straightAge = 0.0;
    glm::dvec2 pausedAt(0.0);
    glm::dvec2 straightAt(0.0);
    CHECK(run(true, pausedAge, pausedAt));
    CHECK(run(false, straightAge, straightAt));
    CHECK_MSG(NearD(pausedAge, straightAge, 1e-6), "the level is as old either way: " + std::to_string(pausedAge) +
                                                       " against " + std::to_string(straightAge));
    CHECK_MSG(glm::length(pausedAt - straightAt) < 1e-3,
              "and its player is in the same place: " + Point(pausedAt) + " against " + Point(straightAt));
    CHECK_MSG(glm::length(straightAt - glm::dvec2(0.0)) > 0.0, "having walked somewhere");
}

// ---- the tutorial and help popups (spec section 5) ---------------------------------

void TheTutorialPopupStopsALevelAsItLoads() {
    if (!OriginalArtIsThere("TheTutorialPopupStopsALevelAsItLoads")) return;
    namespace Popup = MagicPortals::Popup;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const Hud::Rules& hud = layer.HudRules();
    const Popup::Rules& rules = layer.PopupRules();
    CHECK_MSG(layer.PopupOpen() && layer.OpenPopupClass() != nullptr &&
                  layer.OpenPopupClass()->name == "LevelHelp1Popup" && layer.GameTimeStopped(),
              "1-02 opens LevelHelp1Popup in its load frame (Game::managePopups)");
    CHECK_EQ(static_cast<int>(layer.HelpBlockRects().size()), 1);
    if (!layer.PopupOpen()) return;
    const glm::dvec2 spawn = playerPx(registry, layer);

    // A-H3, 1.6 s in with the arrow held: game time stands at zero, the frame clock
    // does not, and nothing walks.
    for (int tick = 0; tick < 96; ++tick) tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    CHECK_MSG(layer.LevelAgeMs() == 0.0 && NearD(layer.LevelFrameMs(), 1600.0, 1e-3),
              "the level's age stands at 0 under it while its frame clock runs: " + std::to_string(layer.LevelFrameMs()));
    CHECK_MSG(glm::length(playerPx(registry, layer) - spawn) < 1e-6, "and the player is still at its spawn");
    CHECK_MSG(NearD(layer.OpenPopup()->clockMs, 1600.0, 1e-3), "the popup's own clock runs");
    {
        const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
        CHECK_MSG(IndexOfImage(quads, hud.pads.leftSprite) < 0 && IndexOfImage(quads, hud.pads.rightSprite) < 0 &&
                      IndexOfImage(quads, hud.restart.sprite) < 0 && IndexOfImage(quads, hud.pause.sprite) < 0,
                  "A-H3: no pad, and restart and pause not come in: GameLayer has not had an update");
        const std::vector<int> blacks = BlackIndices(quads);
        const int card = IndexOfImage(quads, "help_popup.png");
        const int close = IndexOfImage(quads, "popup_close_button.png");
        const int stone = IndexOfImage(quads, "single_stone_sprite.png");
        CHECK_MSG(blacks.size() == 1 && card > blacks[0] && close > card && stone > close,
                  "the dim, the card, the button, then the demonstration");
        if (!blacks.empty()) {
            const ScreenOverlay::Quad& dim = quads[static_cast<std::size_t>(blacks[0])];
            CHECK_MSG(dim.min == glm::vec2(0.0f) && dim.max == glm::vec2(1.0f) && NearD(dim.color.a, 150.0 / 255.0, 1e-6),
                      "H1: the view in black at 150 of 255");
        }
        CHECK_EQ(CountImage(quads, "single_stone_sprite.png"), 2);
        if (card >= 0) {
            const Hud::Rect onView = OnView(layer, quads[static_cast<std::size_t>(card)]);
            CHECK_MSG(SameRect(onView, Hud::Rect{layer.ViewPx() * 0.5 - glm::dvec2(170.0, 128.0), glm::dvec2(340.0, 256.0)}),
                      "H2: 340 x 256 u about the view's centre: " + ShowRect(onView));
        }
        // The caption over it all, on the frame clock: 1 - 1600 / 3000.
        const int glyphs = CountCaption(quads, hud.caption.font);
        CHECK_MSG(glyphs > 0 && NearD(quads.back().color.a, Hud::CaptionAlpha(hud, layer.LevelFrameMs()), 1e-6) &&
                      NearD(quads.back().color.a, 1.0 - 1600.0 / 3000.0, 0.03) &&
                      quads.back().texture.find("Matura84") != std::string::npos,
                  "A-H3: \"Part 2\" drawn last, at the byte of 1 - 1600 / 3000 (A-H3's 0.03): " +
                      std::to_string(quads.back().color.a));
        // The arrow is turned: its quad carries a basis that is not the identity.
        CHECK_MSG(IndexOfImage(quads, "teleport_arrow.png") < 0, "the arrow not in yet at 1.6 s");
    }
    // Later in its loop the arrow is up, turned 23 degrees.
    for (int tick = 0; tick < 42; ++tick) tickWith(layer, registry, kRest, {}, {});
    {
        const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
        const int arrow = IndexOfImage(quads, "teleport_arrow.png");
        const float aspect = static_cast<float>(layer.ViewPx().x / layer.ViewPx().y);
        const glm::mat2 want = ScreenOverlay::Rotation(glm::radians(23.0f), aspect);
        CHECK_MSG(arrow >= 0 && glm::length(quads[static_cast<std::size_t>(arrow)].basis[0] - want[0]) < 1e-6f &&
                      glm::length(quads[static_cast<std::size_t>(arrow)].basis[1] - want[1]) < 1e-6f,
                  "the arrow at 2.3 s, turned 23 degrees on the overlay");
    }
    // A-H10 / A-H11: a touch down on the card, away from the button, closes it on its
    // tick; the demonstration goes that frame, no portal is fired, and the popup is
    // gone a second later.
    const glm::vec2 onCard(640.0f, 200.0f);
    tap(layer, registry, onCard);
    CHECK_MSG(layer.PopupOpen() && Popup::Closing(*layer.OpenPopup()), "the close starts on the touch-down tick");
    {
        const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
        CHECK_MSG(IndexOfImage(quads, "single_stone_sprite.png") < 0 && IndexOfImage(quads, "tap_icon.png") < 0 &&
                      IndexOfImage(quads, "help_popup.png") >= 0,
                  "A-H11: the demonstration vanishes on the close frame, the card still there");
    }
    int ticks = 0;
    while (layer.PopupOpen() && ticks < 120) {
        tickWith(layer, registry, kRest, {}, {});
        ++ticks;
    }
    CHECK_MSG(ticks == 60, "gone 60 ticks - the 1000 ms of its fade - after the close: " + std::to_string(ticks));
    CHECK_MSG(layer.SimLevel() != nullptr && layer.SimLevel()->portals.portalsUsed == 0 &&
                  !layer.SimLevel()->portals.flight.has_value(),
              "A-H10: the closing touch placed no portal");
    CHECK_MSG(layer.LevelAgeMs() == 0.0 && !layer.GameTimeStopped(), "game time resumes from the age it stood at");
    // A-H9, level start: GameLayer comes in from here, restart and pause as UIButtons.
    tickWith(layer, registry, kRest, {}, {});
    {
        const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
        const int restart = IndexOfImage(quads, hud.restart.sprite);
        const double want = 120.0 / 255.0 *
                            MagicPortals::UiLayer::ButtonAlphaByte(layer.LevelEndRules().layer, 1000.0 / 60.0) / 255.0;
        CHECK_MSG(restart >= 0 && NearD(quads[static_cast<std::size_t>(restart)].color.a, want, 1e-6),
                  "a tick after, restart is a tick into its entrance");
        CHECK_MSG(IndexOfImage(quads, "help_popup.png") < 0 && CountBlacks(quads) == 0, "and nothing of the popup is drawn");
    }
    for (int tick = 1; tick < 42; ++tick) tickWith(layer, registry, kRest, {}, {});
    Hud::Rect rect;
    CHECK_MSG(layer.ControlRect(MagicPortalsLayer::Control::Reset, rect) &&
                  SameRect(rect, Hud::Place(hud.restart, layer.ViewPx())),
              "restart home 700 ms after the popup went");
    CHECK_MSG(layer.ControlRect(MagicPortalsLayer::Control::Left, rect) &&
                  SameRect(rect, Hud::PadRect(hud, Hud::Side::Left, layer.ViewPx(), 700.0)),
              "and the left pad slid in with it");
    // And the level plays: the arrow walks the player.
    for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    CHECK_MSG(glm::length(playerPx(registry, layer) - spawn) > 1.0, "after the popup the level plays");
    // A retry raises it again, every element from nothing.
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK_MSG(layer.PopupOpen() && layer.OpenPopup()->clockMs == 0.0 && rules.dimAlphaByte == 150,
              "a retry of 1-02 raises its popup again from its start");
    CHECK_MSG(layer.LevelAgeMs() == 0.0 && layer.LevelFrameMs() == 0.0,
              "and the retried level stands at age 0 under it, as its first load does: " +
                  std::to_string(layer.LevelAgeMs()));
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.LevelAgeMs() == 0.0 && NearD(layer.OpenPopup()->clockMs, 1000.0 / 60.0, 1e-6),
              "a tick on, only the popup's clock has moved");
    layer.OnDetach(registry);
}

void AHelpBlockOpensItsPopupAndTheLevelTakesUpWhereItStopped() {
    if (!OriginalArtIsThere("AHelpBlockOpensItsPopupAndTheLevelTakesUpWhereItStopped")) return;
    namespace Popup = MagicPortals::Popup;
    // Two runs of 1-13 (level12), whose block the view shows from the start. The first
    // walks right, taps the block, holds the arrow under its popup for two seconds,
    // closes it and walks on; the second walks right straight through. After as many
    // ticks of game time, the two players stand in the same place.
    const auto run = [](bool tapped, double& ageOut, glm::dvec2& atOut) {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level12");
        layer.OnAttach(registry);
        if (!layer.LoadError().empty()) return false;
        const Hud::Rules& hud = layer.HudRules();
        const std::vector<std::string> right = {MagicPortalsLayer::kRight};
        for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
        for (int tick = 0; tick < 20; ++tick) tickWith(layer, registry, kRest, right, {});
        const std::vector<Hud::Rect> blocks = layer.HelpBlockRects();
        CHECK_MSG(blocks.size() == 1, "level12 places one help block");
        if (blocks.empty()) return false;
        const glm::vec2 onBlock = ScreenOfView(layer, blocks[0].Centre());
        if (tapped) {
            // The touch goes down on the block: no portal, no popup yet.
            tickWith(layer, registry, onBlock, {MagicPortalsLayer::kRight, MagicPortalsLayer::kTap},
                     {MagicPortalsLayer::kTap});
            CHECK_MSG(!layer.PopupOpen() && !layer.SimLevel()->portals.flight.has_value() &&
                          layer.SimLevel()->portals.portalsUsed == 0,
                      "a touch down on a help block fires no portal and opens nothing yet");
            // And comes up on it: HelpBlockController's release.
            Input::TickInput input;
            input.mousePosition = onBlock;
            input.down = right;
            input.released = {MagicPortalsLayer::kTap};
            PhysicsSystem::Update(registry, MagicPortalsLayer::kTick);
            Input::BeginReplayedTick(input);
            layer.OnFixedUpdate(registry, MagicPortalsLayer::kTick);
            Input::EndReplayedTick();
            CHECK_MSG(layer.PopupOpen() && layer.OpenPopupClass() != nullptr &&
                          layer.OpenPopupClass()->name == "LevelHelp15Popup",
                      "its release opens LevelHelp15Popup");
            const double age = layer.LevelAgeMs();
            for (int tick = 0; tick < 120; ++tick) tickWith(layer, registry, kRest, right, {});
            CHECK_MSG(layer.LevelAgeMs() == age, "game time stands still under it");
            {
                // A-H4 / A-H5: restart and pause frozen at their 120 under the dim, no pads.
                const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
                const std::vector<int> blacks = BlackIndices(quads);
                const int restart = IndexOfImage(quads, hud.restart.sprite);
                CHECK_MSG(IndexOfImage(quads, hud.pads.leftSprite) < 0 && IndexOfImage(quads, hud.pads.rightSprite) < 0,
                          "A-H5: no pads under a help popup");
                CHECK_MSG(blacks.size() == 1 && restart >= 0 && restart < blacks[0] &&
                              NearD(quads[static_cast<std::size_t>(restart)].color.a, 120.0 / 255.0, 1e-6),
                          "A-H5: restart drawn, whole at its 120, under the dim");
                CHECK_MSG(IndexOfImage(quads, "rolling_stone.png") >= 0, "and the demonstration's stone is the hd one");
            }
            // A tap on restart, under it, closes the popup and restarts nothing.
            Hud::Rect restartRect;
            CHECK(layer.ControlRect(MagicPortalsLayer::Control::Reset, restartRect));
            tap(layer, registry, ScreenOfView(layer, restartRect.Centre()));
            CHECK_MSG(Popup::Closing(*layer.OpenPopup()) && layer.LevelAgeMs() == age,
                      "a touch on restart under the popup closes it and nothing else");
            for (int tick = 0; tick < 59; ++tick) tickWith(layer, registry, kRest, right, {});
            CHECK_MSG(layer.PopupOpen(), "still fading a tick before its second is up");
            tickWith(layer, registry, kRest, right, {});
            CHECK_MSG(!layer.PopupOpen(), "gone on its 60th tick, and the level takes up that tick's walk");
            const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
            Hud::Rect pad;
            CHECK_MSG(IndexOfImage(quads, hud.pads.leftSprite) >= 0 && layer.ControlRect(MagicPortalsLayer::Control::Left, pad) &&
                          SameRect(pad, Hud::PadRect(hud, Hud::Side::Left, layer.ViewPx(), 10000.0)),
                      "A-H9: the pads drawn on the first frame after, at their settled place");
        } else {
            tickWith(layer, registry, kRest, right, {});
            tickWith(layer, registry, kRest, right, {});
        }
        for (int tick = 0; tick < 30; ++tick) tickWith(layer, registry, kRest, right, {});
        ageOut = layer.LevelAgeMs();
        atOut = playerPx(registry, layer);
        return true;
    };
    double tappedAge = 0.0;
    double straightAge = 0.0;
    glm::dvec2 tappedAt(0.0);
    glm::dvec2 straightAt(0.0);
    CHECK(run(true, tappedAge, tappedAt));
    CHECK(run(false, straightAge, straightAt));
    CHECK_MSG(NearD(tappedAge, straightAge, 1e-6),
              "the level is as old either way: " + std::to_string(tappedAge) + " against " + std::to_string(straightAge));
    CHECK_MSG(glm::length(tappedAt - straightAt) < 1e-3,
              "and its player in the same place: " + Point(tappedAt) + " against " + Point(straightAt));

    // A touch that moves more than 12 px before it comes up opens nothing.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level12");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) return;
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::vector<Hud::Rect> blocks = layer.HelpBlockRects();
    if (blocks.empty()) return;
    const glm::vec2 onBlock = ScreenOfView(layer, blocks[0].Centre());
    tickWith(layer, registry, onBlock, {MagicPortalsLayer::kTap}, {MagicPortalsLayer::kTap});
    tickWith(layer, registry, onBlock + glm::vec2(14.0f, 0.0f), {MagicPortalsLayer::kTap}, {});
    Input::TickInput input;
    input.mousePosition = onBlock;
    input.released = {MagicPortalsLayer::kTap};
    PhysicsSystem::Update(registry, MagicPortalsLayer::kTick);
    Input::BeginReplayedTick(input);
    layer.OnFixedUpdate(registry, MagicPortalsLayer::kTick);
    Input::EndReplayedTick();
    CHECK_MSG(!layer.PopupOpen(), "a touch that moved 14 px opens no popup, even let go on the block");
    layer.OnDetach(registry);
}

void ThePausesButtonsGoWhereTheOriginalsGo() {
    if (!OriginalArtIsThere("ThePausesButtonsGoWhereTheOriginalsGo")) return;
    namespace Pause = MagicPortals::Pause;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level4");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    for (int tick = 0; tick < 90; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK(TapThePauseControl(layer, registry));
    CHECK(layer.Paused());
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});

    // Skip is not there on a level never finished, and pressing it leads nowhere.
    CHECK_MSG(!layer.PressPause(registry, Pause::Button::Skip) && layer.Paused(), "no skip on a fresh level");
    // Achievements: kept by the owner's ruling, and pressing it changes nothing
    // yet - the popup it opens is not built.
    CHECK_MSG(TapPauseButton(layer, registry, Pause::Button::Achievements) && layer.Paused() &&
                  IsAt(layer, "level4") && layer.MenuScreen() == MagicPortalsLayer::Screen::None,
              "Achievements does nothing visible");

    // The sound switch: off, sound_mute in its place, the music switch dismissed
    // and gone 700 ms later; and no music is wanted.
    const std::string& muteFile = layer.PauseRules().soundOffSprite;
    CHECK(layer.SoundOn());
    CHECK(TapPauseButton(layer, registry, Pause::Button::Sound));
    CHECK_MSG(!layer.SoundOn(), "the sound switch turns the sound off");
    CHECK_MSG(IndexOfImage(HudFrame(registry, layer), muteFile) >= 0, "and shows sound_mute");
    for (int tick = 0; tick < 43; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(IndexOfImage(HudFrame(registry, layer), layer.PauseRules().music.sprite) < 0,
              "A-P7: the music switch has gone with the sound");
    const bool musicBefore = layer.MusicOn();
    CHECK_MSG(!layer.PressPause(registry, Pause::Button::Music) && layer.MusicOn() == musicBefore,
              "and cannot be pressed while it is gone");
    CHECK(TapPauseButton(layer, registry, Pause::Button::Sound));
    CHECK(layer.SoundOn());
    {
        const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
        CHECK_MSG(IndexOfImage(quads, layer.PauseRules().music.sprite) < 0,
                  "the music switch comes back from nothing, a fresh entrance");
    }
    for (int tick = 0; tick < 43; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK(TapPauseButton(layer, registry, Pause::Button::Music));
    CHECK_MSG(!layer.MusicOn() && IndexOfImage(HudFrame(registry, layer), layer.PauseRules().musicOffSprite) >= 0,
              "the music switch turns the music off, and shows music_off");
    CHECK(TapPauseButton(layer, registry, Pause::Button::Music));
    CHECK(layer.MusicOn());

    // Back to levels: the level grid of this level's world, the level gone.
    CHECK(TapPauseButton(layer, registry, Pause::Button::Levels));
    CHECK_MSG(!layer.Paused() && layer.MenuScreen() == MagicPortalsLayer::Screen::Levels &&
                  layer.SimLevel() == nullptr,
              "back to levels opens the grid");
    CHECK_EQ(MenuButtonsOfKind(layer, MagicPortalsLayer::MenuButton::Kind::Level), 32);
}

void SkipOnALevelAlreadyFinished() {
    if (!OriginalArtIsThere("SkipOnALevelAlreadyFinished")) return;
    namespace Pause = MagicPortals::Pause;
    // Memory-only medals, earned as a player earns one: level0 by holding right,
    // then the medal screen's own retry.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    for (int tick = 0; tick < 600 && layer.MenuScreen() != MagicPortalsLayer::Screen::Finished; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
    }
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::Finished);
    if (layer.MenuScreen() != MagicPortalsLayer::Screen::Finished) return;
    for (const MagicPortalsLayer::MenuButton& button : layer.MenuButtons()) {
        if (button.kind != MagicPortalsLayer::MenuButton::Kind::Retry) continue;
        layer.PressMenu(registry, button);
        break;
    }
    CHECK(IsAt(layer, "level0"));
    for (int tick = 0; tick < 90; ++tick) tickWith(layer, registry, kRest, {}, {});
    press(layer, registry, MagicPortalsLayer::kBack);
    CHECK(layer.Paused());
    CHECK_MSG(layer.PausedLevel().savedMedal > 0, "the pause reads the medal just recorded");
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::vector<ScreenOverlay::Quad> quads = HudFrame(registry, layer);
    const Pause::Rules& rules = layer.PauseRules();
    const int plaque = IndexOfImage(quads, rules.currentPlaque.sprite);
    const int medal = IndexOfImage(quads, Pause::MedalSprite(rules, layer.PausedLevel().savedMedal));
    CHECK_MSG(plaque >= 0 && medal > plaque && IndexOfImage(quads, rules.skip.sprite) > medal,
              "A-P2: the current plaque, its medal over it, and skip");
    CHECK(TapPauseButton(layer, registry, Pause::Button::Skip));
    CHECK_MSG(!layer.Paused() && IsAt(layer, "level1") && layer.MenuScreen() == MagicPortalsLayer::Screen::None &&
                  layer.SimLevel() != nullptr,
              "skip goes on to the next level");
    CHECK_EQ(layer.LevelFrameMs(), 0.0);
}

// ---- What the scene's numbers are ------------------------------------------------
//
// The original drew into an 8-bit framebuffer and blended on its bytes, so the
// port's scene says its numbers are display values (step 44, the lighting
// design's G2): colour textures sampled undecoded, no tone map, no bloom, and a
// flat black ground, so no sky pass. The setting is the layer's, not a level's,
// so the menu and a start that fails get it too. Quantised to 5/6/5 since step
// 55 (G6), as lighting.json says the original's surface was.
void SceneSettingsSayDisplayValues(entt::registry& registry, const std::string& start) {
    const RenderSettings* rendering = registry.ctx().find<RenderSettings>();
    CHECK_MSG(rendering != nullptr, "the layer puts RenderSettings in the context, starting at '" + start + "'");
    if (rendering == nullptr) return;
    CHECK_MSG(rendering->encoding == RenderSettings::SceneEncoding::DisplayEncoded, "display-encoded at '" + start + "'");
    CHECK_MSG(!rendering->decodesColourTextures(), "so a colour texture is its file's bytes");
    CHECK_MSG(rendering->background == RenderSettings::Background::Color && !rendering->drawsSky(),
              "a flat ground, and no sky pass behind the level");
    CHECK_EQ(rendering->backgroundColor[0], 0.0f);
    CHECK_EQ(rendering->backgroundColor[1], 0.0f);
    CHECK_EQ(rendering->backgroundColor[2], 0.0f);
    const std::array<float, 3> clear = RenderSettings::SceneClearColor(rendering);
    CHECK_MSG(clear[0] == 0.0f && clear[1] == 0.0f && clear[2] == 0.0f,
              "and the target is cleared to that black, not to the linear literal");
    CHECK_EQ(rendering->bloomIntensity, 0.0f);
    CHECK_MSG(rendering->quantize == RenderSettings::OutputQuantize::Rgb565, "quantised to 5/6/5 at '" + start + "'");
    CHECK_EQ(rendering->exposure, 1.0f);
}

void TheSceneHoldsDisplayValues() {
    for (const char* name : {"level0", "", "level99"}) {
        const std::string start = name;
        entt::registry registry;
        publishViewport(registry);
        CHECK(registry.ctx().find<RenderSettings>() == nullptr);
        MagicPortalsLayer layer(TestPaths(), start);
        layer.OnAttach(registry);
        if (start == "level0") CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
        if (start.empty()) CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::Loading);
        if (start == "level99") CHECK(layer.Current() == nullptr);
        SceneSettingsSayDisplayValues(registry, start);
        // The next level and a retry keep it: nothing per level writes it.
        if (start == "level0") {
            press(layer, registry, MagicPortalsLayer::kSkip);
            CHECK(IsAt(layer, "level1"));
            press(layer, registry, MagicPortalsLayer::kRetry);
            SceneSettingsSayDisplayValues(registry, "level1, retried");
        }
        layer.OnDetach(registry);
    }

    // A --scene load has already put a scene's own settings in the context, sky
    // and bloom included. The layer's replace them rather than deferring.
    entt::registry registry;
    publishViewport(registry);
    RenderSettings loaded;
    loaded.bloomIntensity = 2.0f;
    loaded.backgroundColor[0] = 0.5f;
    registry.ctx().insert_or_assign(loaded);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    SceneSettingsSayDisplayValues(registry, "level0 over a loaded scene");
    layer.OnDetach(registry);
}

// ---- the ambient light (step 45) ---------------------------------------------------
//
// The original draws every sprite at its colour times min(1, ambient + emissive)
// (the lighting design's G3): the ambient its level file gives, or darkest's in a
// level that sets it; the emissive its node or its .ent gives.

// The colour every entity wearing `tag` whose image is `file` is DRAWN with: what
// the engine packs for its draw. Since step 47 the layer writes C as the albedo
// colour and min(1, A + E) as the sprite's 2D ambient, and RenderSystem::
// ApplySprite2D multiplies the two; step 45 folded them itself. Read through the
// engine's own packing, so every pin below holds the product either way, and a
// layer that folded the ambient AND handed it to the engine (ambient squared)
// fails them.
std::vector<glm::vec4> ColoursOf(entt::registry& registry, const char* tag, const std::string& file) {
    std::vector<glm::vec4> out;
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (t.tag != tag || !EndsWith(material.albedoTexturePath, file)) continue;
        PushConstantData push{};
        push.albedoColor = material.albedoColor; // what buildPushConstants' material branch writes
        RenderSystem::ApplySprite2D(material, push);
        out.push_back(push.albedoColor);
    }
    return out;
}

bool AllAre(const std::vector<glm::vec4>& colours, float r, float g, float b, float a = 1.0f) {
    if (colours.empty()) return false;
    return std::all_of(colours.begin(), colours.end(), [=](const glm::vec4& c) {
        return std::fabs(c.r - r) < 1e-5f && std::fabs(c.g - g) < 1e-5f && std::fabs(c.b - b) < 1e-5f &&
               std::fabs(c.a - a) < 1e-5f;
    });
}

std::string Show(const std::vector<glm::vec4>& colours) {
    std::string out;
    for (const glm::vec4& c : colours) {
        out += "(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ", " +
               std::to_string(c.a) + ") ";
    }
    return out.empty() ? "none" : out;
}

void EverySpriteIsDrawnAtItsAmbient() {
    // 1-1 (level0), at its file's ambient (0.35, 0.3, 0.35).
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    CHECK_MSG(layer.LightingError().empty(), layer.LightingError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty() || !layer.LightingError().empty()) return;
    CHECK_MSG(layer.AmbientNow() == glm::dvec3(0.35, 0.3, 0.35), "level0's own ambient");

    const std::vector<glm::vec4> arches = ColoursOf(registry, "Magic Portals Sprite", "arch_with_base_blur.png");
    CHECK_EQ(arches.size(), std::size_t{2});
    CHECK_MSG(AllAre(arches, 0.35f, 0.30f, 0.35f), "the arches, emissive 0, at the ambient: " + Show(arches));
    // Since step 47 that product is the engine's, not the layer's: the arches'
    // own colour stays whole and the ambient rides in their 2D sprite record.
    int archRecords = 0;
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (t.tag != "Magic Portals Sprite" || !EndsWith(material.albedoTexturePath, "arch_with_base_blur.png")) continue;
        ++archRecords;
        const glm::vec3 ambient = material.sprite2D.ambient;
        CHECK_MSG(material.albedoColor == glm::vec4(1.0f) && material.sprite2D.enabled &&
                      std::fabs(ambient.r - 0.35f) < 1e-5f && std::fabs(ambient.g - 0.30f) < 1e-5f &&
                      std::fabs(ambient.b - 0.35f) < 1e-5f,
                  "an arch: colour (1, 1, 1, 1), its 2D sprite on, at ambient (" + std::to_string(ambient.r) + ", " +
                      std::to_string(ambient.g) + ", " + std::to_string(ambient.b) + ")");
    }
    CHECK_EQ(archRecords, 2);
    const std::vector<glm::vec4> platforms =
        ColoursOf(registry, "Magic Portals Sprite", "STONE03A4x10_contrast.png");
    CHECK_EQ(platforms.size(), std::size_t{3});
    CHECK_MSG(AllAre(platforms, 1.0f, 1.0f, 1.0f), "the platforms, emissive 1, whole: " + Show(platforms));
    const std::vector<glm::vec4> torch = ColoursOf(registry, "Magic Portals Sprite", "torch_small.png");
    CHECK_MSG(AllAre(torch, 0.35f, 0.30f, 0.35f), "the torch's own sprite is emissive 0 too: " + Show(torch));
    const std::vector<glm::vec4> door = ColoursOf(registry, "Magic Portals Sprite", "window01.png");
    CHECK_MSG(AllAre(door, 1.0f, 1.0f, 1.0f), "the door, emissive 0.7: min(1, 0.35 + 0.7) is 1: " + Show(door));
    // Added sprites are dimmed like mixed ones; the static portals are emissive 1,
    // so min(1, ambient + 1) leaves them whole - times the colour their script sets
    // (art.json static_portal): two red (1, 0.3, 0.3), two blue (0.3, 0.3, 1).
    const std::vector<glm::vec4> halos = ColoursOf(registry, "Magic Portals Sprite", "portal_halo.png");
    CHECK_EQ(halos.size(), std::size_t{4});
    const auto tinted = [&halos](float r, float g, float b) {
        return std::count_if(halos.begin(), halos.end(), [&](const glm::vec4& c) { return AllAre({c}, r, g, b); });
    };
    CHECK_MSG(tinted(1.0f, 0.3f, 0.3f) == 2 && tinted(0.3f, 0.3f, 1.0f) == 2,
              "the static portals' halos, two red and two blue: " + Show(halos));
    // The boxes are placeholders the PBR path draws, never dimmed: a static
    // body's stays the layer's grey (0.42, 0.44, 0.50).
    int greyBoxes = 0;
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (t.tag == "Magic Portals Body" && material.albedoColor == glm::vec4(0.42f, 0.44f, 0.50f, 1.0f)) ++greyBoxes;
    }
    CHECK_MSG(greyBoxes > 0, "the static bodies' boxes keep their own grey");
    if (OriginalArtIsThere("EverySpriteIsDrawnAtItsAmbient")) {
        const std::vector<glm::vec4> mage =
            ColoursOf(registry, "Magic Portals Player Sprite", "magic_portals_hd.png");
        CHECK_MSG(AllAre(mage, 1.0f, 1.0f, 1.0f), "the player, dark_mage.ent's emissive 1, whole: " + Show(mage));
    }

    // Its lightmaps, held until it goes: level0's nine, in its own directory.
    const std::vector<std::string> held = layer.HeldLightmaps();
    CHECK_EQ(held.size(), std::size_t{9});
    CHECK_MSG(std::all_of(held.begin(), held.end(),
                          [](const std::string& p) { return p.find("/assets/lightmaps/level0/add") != std::string::npos; }),
              "each in level0's own directory");
    CHECK_MSG(std::any_of(held.begin(), held.end(), [](const std::string& p) { return EndsWith(p, "/add696.png"); }),
              "the torch's wall among them");
    CHECK_MSG(std::is_sorted(held.begin(), held.end()), "in a fixed order");
    CHECK_EQ(layer.LightmapsHandedBack(), std::size_t{0});

    // A retry draws the same level again, so it keeps them, and colours again.
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK_MSG(layer.HeldLightmaps() == held, "a retry holds the lightmaps it draws again");
    CHECK_MSG(layer.LightmapsHandedBack() == 0, "and handed none back to do it");
    CHECK_MSG(AllAre(ColoursOf(registry, "Magic Portals Sprite", "arch_with_base_blur.png"), 0.35f, 0.30f, 0.35f),
              "and the rebuilt arches are dimmed again");

    // The next level gives them back and holds its own.
    press(layer, registry, MagicPortalsLayer::kSkip);
    CHECK(IsAt(layer, "level1"));
    const std::vector<std::string>& next = layer.HeldLightmaps();
    CHECK_EQ(next.size(), std::size_t{9});
    CHECK_MSG(!next.empty() && std::all_of(next.begin(), next.end(), [](const std::string& p) {
                  return p.find("/assets/lightmaps/level1/add") != std::string::npos;
              }),
              "level1's, and none of level0's");
    CHECK_MSG(layer.LightmapsHandedBack() == 9, "level0's nine handed back on the way");

    // Out to the grid: no level, nothing held, nothing dimmed.
    CHECK(OutToTheGrid(layer, registry));
    CHECK_MSG(layer.HeldLightmaps().empty(), "the menu holds no lightmap");
    CHECK_MSG(layer.LightmapsHandedBack() == 18, "and level1's went back too");
    CHECK_MSG(layer.AmbientNow() == glm::dvec3(1.0), "and colours nothing");
    layer.OnDetach(registry);
}

void AShotIsDimmedAndADarkLevelIsDark() {
    // Only the shot needs the original's extracted assets (projectile.png is an
    // Art.hpp image); the dark level's scenery is the converter's art beside the
    // levels, so its pins run wherever the levels do.
    if (OriginalArtIsThere("AShotIsDimmedAndADarkLevelIsDark (the shot)")) {
        // The shot is projectile.ent's, emissive (0.6, 0.6, 1): under level1's
        // (0.35, 0.3, 0.35), min(1, A + E) is (0.95, 0.9, 1).
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level1");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.LightingError().empty(), layer.LoadError() + layer.LightingError());
        if (layer.SimLevel() != nullptr) {
            CloseTheLevelStartPopup(layer, registry);
            waitForFirstTap(layer, registry);
            tap(layer, registry, screenOf(registry, playerPx(registry, layer) + glm::dvec2(0.0, -48.0)));
            CHECK_MSG(layer.SimLevel()->portals.flight.has_value(), "the tap fired: " + lastFailure(layer));
            const std::vector<glm::vec4> shot = ColoursOf(registry, "Magic Portals Shot Sprite", "projectile.png");
            CHECK_MSG(AllAre(shot, 0.95f, 0.90f, 1.0f), "the shot, dimmed on the channels it is not emissive on: " +
                                                            Show(shot));
        }
        layer.OnDetach(registry);
    }

    // 4-22 (level21c) sets `darkest`: drawn at 0.01, whatever its file's 0.5.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level21c");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.LightingError().empty(), layer.LoadError() + layer.LightingError());
    if (layer.SimLevel() == nullptr) return;
    CHECK_MSG(layer.AmbientNow() == glm::dvec3(0.01, 0.01, 0.01), "darkest's ambient, not the file's 0.5");
    for (const char* scenery : {"pilar.png", "wall_w3.png", "STONE03A4x10_contrast.png", "bar3_contrast.png"}) {
        const std::vector<glm::vec4> colours = ColoursOf(registry, "Magic Portals Sprite", scenery);
        CHECK_MSG(AllAre(colours, 0.01f, 0.01f, 0.01f), std::string(scenery) + ", emissive 0: " + Show(colours));
    }
    const std::vector<glm::vec4> crystals = ColoursOf(registry, "Magic Portals Sprite", "crystal.png");
    CHECK_EQ(crystals.size(), std::size_t{3});
    CHECK_MSG(AllAre(crystals, 1.0f, 1.0f, 1.0f), "the crystals, emissive 1, bright: " + Show(crystals));
    const std::vector<glm::vec4> lift = ColoursOf(registry, "Magic Portals Sprite", "metal_door.png");
    CHECK_MSG(AllAre(lift, 1.0f, 1.0f, 1.0f), "and the lift's door: " + Show(lift));
    CHECK_MSG(layer.HeldLightmaps().empty(), "darkest levels ship no lightmap");
    layer.OnDetach(registry);
}

// ---- the timed crystals' dials (the visuals plan's 3.1) -------------------------
//
// The original never fades a timed crystal. addTimerToCrystal adds timer.ent 2
// behind it at alpha 0.5, and ETHCallback_timer shows the time elapsed in eight
// cells, pulses it faster as the time runs out, and shrinks it away where it
// stood: from the frame the time is up, or from the frame after the one that
// finds the crystal taken (art.json timer).

// A timed crystal's dial as the layer reports it after the last tick, or none.
// A copy: TimerReports() returns by value, so a pointer into it would dangle.
std::optional<MagicPortalsLayer::TimerReport> DialOf(const MagicPortalsLayer& layer, const std::string& crystal) {
    for (const MagicPortalsLayer::TimerReport& report : layer.TimerReports()) {
        if (report.crystal == crystal) return report;
    }
    return std::nullopt;
}

MagicPortals::Art::Timer TimerRules() {
    MagicPortals::Art::Rules rules;
    std::string error;
    CHECK_MSG(MagicPortals::Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", rules, error), error);
    return rules.timer;
}

void EveryTimedCrystalHasADialBehindIt() {
    // Every level whose file carries a `time`, as the layer loads it: 35 timed
    // crystals in 20 levels (the brief's census), one dial each, behind its
    // crystal, at alpha 0.5 and whole on every level - chapter 4's dark ones too,
    // since timer.ent's emissive is 1.
    if (!OriginalArtIsThere("EveryTimedCrystalHasADialBehindIt")) return;
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kLevels, ec)) {
        if (entry.path().extension() != ".tscn") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        if (text.str().find("metadata/time") != std::string::npos) names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    int crystals = 0;
    int levels = 0;
    int misplaced = 0;
    std::string first;
    for (const std::string& name : names) {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), name);
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
                  name + ": " + layer.LoadError() + layer.ArtError() + layer.LightingError());
        if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) {
            layer.OnDetach(registry);
            continue;
        }
        int timed = 0;
        for (const MagicPortals::Goals::Crystal& crystal : layer.SimLevel()->goals.crystals) {
            if (crystal.timed) ++timed;
        }
        const std::vector<MagicPortalsLayer::TimerReport> dials = layer.TimerReports();
        CHECK_MSG(static_cast<int>(dials.size()) == timed && Tagged(registry, "Magic Portals Timer") == timed,
                  name + ": " + std::to_string(timed) + " timed crystals, " + std::to_string(dials.size()) + " dials");
        for (const MagicPortalsLayer::TimerReport& dial : dials) {
            const MagicPortals::Goals::Crystal* crystal = layer.SimLevel()->goals.FindCrystal(dial.crystal);
            const bool ok = crystal != nullptr && crystal->timed && dial.drawn && !dial.shrinking &&
                            dial.z < dial.crystalZ && dial.frame == 0 && dial.alpha == 0.5 &&
                            dial.scale == glm::dvec2(1.0);
            if (!ok && misplaced++ == 0) {
                first = name + " " + dial.crystal + ": z " + std::to_string(dial.z) + " against its crystal's " +
                        std::to_string(dial.crystalZ) + ", cell " + std::to_string(dial.frame);
            }
        }
        const std::vector<glm::vec4> colours = ColoursOf(registry, "Magic Portals Timer", "timer.png");
        CHECK_MSG(static_cast<int>(colours.size()) == timed && (timed == 0 || AllAre(colours, 1.0f, 1.0f, 1.0f, 0.5f)),
                  name + "'s dials at (1, 1, 1, 0.5): " + Show(colours));
        crystals += timed;
        if (timed > 0) ++levels;
        layer.OnDetach(registry);
        CHECK_MSG(Tagged(registry, "Magic Portals Timer") == 0, name + ": no dial outlives the layer");
    }
    std::printf("  %d timed crystals with a dial in %d levels\n", crystals, levels);
    CHECK_MSG(crystals == 35 && levels == 20,
              std::to_string(crystals) + " timed crystals in " + std::to_string(levels) + " levels");
    CHECK_MSG(misplaced == 0, std::to_string(misplaced) + " dials not whole and behind their crystals; the first " + first);
}

void ATimedCrystalCountsDownOnItsDialAndNeverFades() {
    // REPLACES step 45's ATimedCrystalFadesInItsAlphaAlone, deliberately: that pin
    // held the remake's guessed fade, 0.4 + 0.6 |sin(12 t)| over the last 2 s,
    // which the original does not have. 1-27 (level26): crystal_1264 goes at 10 s.
    if (!OriginalArtIsThere("ATimedCrystalCountsDownOnItsDialAndNeverFades")) return;
    const MagicPortals::Art::Timer timer = TimerRules();
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level26");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
              layer.LoadError() + layer.ArtError() + layer.LightingError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    const std::string name = "crystal_1264";
    const entt::entity quad = SpriteOf(registry, "Magic Portals Timer", "timer.png");
    CHECK_MSG(quad != entt::null && layer.TimerReports().size() == 1, "one dial, crystal_1264's");
    if (quad == entt::null || !DialOf(layer, name)) return;
    const std::size_t pictures = ColoursOf(registry, "Magic Portals Sprite", "crystal.png").size();

    int tick = 0;
    int cellsOff = 0;
    int pulseOff = 0;
    int quadOff = 0;
    int dimmed = 0;
    int lastTwoSeconds = 0;
    glm::dvec2 lastScale(1.0);
    std::string first;
    bool expired = false;
    while (tick < 700) {
        tickWith(layer, registry, kRest, {}, {});
        ++tick;
        const MagicPortals::Goals::Crystal* crystal =
            layer.SimLevel() != nullptr ? layer.SimLevel()->goals.FindCrystal(name) : nullptr;
        const std::optional<MagicPortalsLayer::TimerReport> dial = DialOf(layer, name);
        if (crystal == nullptr || !dial || crystal->collected) break;
        if (crystal->expired) {
            expired = true;
            break;
        }
        const double elapsedMs = (crystal->lifeS - crystal->leftS) * 1000.0;
        if (dial->frame != tick * 8 / 600 || dial->frame != timer.FrameAt(elapsedMs, 10000.0)) {
            if (cellsOff++ == 0) first = "tick " + std::to_string(tick) + " cell " + std::to_string(dial->frame);
        }
        const glm::dvec2 pulse = timer.PulseAt(elapsedMs, 10000.0);
        if (dial->scale != pulse || pulse.x < 1.0 || pulse.x > 1.15 + 1e-9 || dial->alpha != 0.5 || dial->shrinking) {
            ++pulseOff;
        }
        // The quad itself: the cell, 32 units times the pulse, at the dial's depth.
        const auto& animation = registry.get<SpriteAnimationComponent>(quad);
        const auto& transform = registry.get<TransformComponent>(quad);
        const float sizeM = MagicPortals::Units::ToMetres(32.0 * dial->scale.x);
        if (animation.firstFrame != static_cast<uint32_t>(dial->frame) || std::fabs(transform.scale.x - sizeM) > 1e-6f ||
            transform.position.z != dial->z) {
            ++quadOff;
        }
        // The crystal stays whole to its last tick.
        if (crystal->leftS < 2.0) {
            ++lastTwoSeconds;
            const std::vector<glm::vec4> colours = ColoursOf(registry, "Magic Portals Sprite", "crystal.png");
            if (colours.size() != pictures || !AllAre(colours, 1.0f, 1.0f, 1.0f, 1.0f)) ++dimmed;
        }
        lastScale = dial->scale;
    }
    CHECK_MSG(expired && tick == 600, "crystal_1264 expired on tick " + std::to_string(tick) + ", the 600th");
    CHECK_MSG(cellsOff == 0, std::to_string(cellsOff) + " ticks off the cell (tick x 8) / 600; the first " + first);
    CHECK_MSG(pulseOff == 0, std::to_string(pulseOff) + " ticks off the pulse, in 1..1.15 at alpha 0.5");
    CHECK_MSG(quadOff == 0, std::to_string(quadOff) + " ticks the quad did not draw the dial's cell and size");
    CHECK_MSG(lastTwoSeconds >= 119 && dimmed == 0,
              "no crystal dimmed on " + std::to_string(lastTwoSeconds) + " ticks of its last 2 s: " +
                  std::to_string(dimmed) + " were");
    if (!expired) return;

    // The tick it expires: its picture gone, the dial shrinking from its last pulse,
    // on its last cell, and gone once that pulse x 2.8125 x 0.9 a tick is below 0.1
    // (ins 184-199 test the stored scale): from 1.1497, drawn shrinking on 32 ticks.
    const std::optional<MagicPortalsLayer::TimerReport> dial = DialOf(layer, name);
    CHECK_MSG(ColoursOf(registry, "Magic Portals Sprite", "crystal.png").size() == pictures - 1,
              "the crystal's picture goes at its time");
    CHECK(dial.has_value());
    if (!dial) return;
    CHECK_MSG(dial->drawn && dial->shrinking && dial->frame == 7, "the dial shrinks on its last cell");
    CHECK_MSG(std::fabs(dial->scale.x - lastScale.x * 0.9) < 1e-6 && std::fabs(dial->alpha - 0.45) < 1e-6,
              "x 0.9 on the tick: " + std::to_string(dial->scale.x) + " from " + std::to_string(lastScale.x) +
                  ", alpha " + std::to_string(dial->alpha));
    int shrinking = 1;
    int grew = 0;
    double scale = dial->scale.x;
    while (shrinking < 60) {
        tickWith(layer, registry, kRest, {}, {});
        const std::optional<MagicPortalsLayer::TimerReport> now = DialOf(layer, name);
        if (!now || !now->drawn) break;
        if (now->scale.x >= scale) ++grew;
        scale = now->scale.x;
        ++shrinking;
    }
    std::printf("  crystal_1264's dial: drawn shrinking on %d ticks from %.4f\n", shrinking, lastScale.x);
    CHECK_MSG(shrinking == 32 && grew == 0,
              "gone after " + std::to_string(shrinking) + " shrinking ticks (32), none growing");
    CHECK_MSG(Tagged(registry, "Magic Portals Timer") == 0, "and its quad with it");
    layer.OnDetach(registry);
}

void ATakenCrystalsDialShrinksAway() {
    // A crystal taken early. ETHCallback_timer has already read elapsedTime, a frame
    // on, into a local when it finds the crystal gone (ins 1-27). It adds the whole
    // time to the datum (ins 80-91) but compares the local (ins 102-103), so that
    // frame still pulses and sets the cell, where the dial stood, and the x 0.9
    // starts on the NEXT frame. Then it shrinks away with no burst.
    if (!OriginalArtIsThere("ATakenCrystalsDialShrinksAway")) return;
    const MagicPortals::Art::Timer timer = TimerRules();
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level26");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    const std::string name = "crystal_1264";
    const std::size_t pictures = ColoursOf(registry, "Magic Portals Sprite", "crystal.png").size();
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::optional<MagicPortalsLayer::TimerReport> before = DialOf(layer, name);
    CHECK(before.has_value() && before->drawn && !before->shrinking);
    if (!before) return;
    const glm::dvec2 atPx = before->atPx;
    PutPlayerAt(registry, layer, atPx);
    int taken = 60;
    for (int ticks = 0; ticks < 5; ++ticks) {
        tickWith(layer, registry, kRest, {}, {});
        ++taken;
        const MagicPortals::Goals::Crystal* crystal = layer.SimLevel()->goals.FindCrystal(name);
        if (crystal != nullptr && crystal->collected) break;
    }
    const MagicPortals::Goals::Crystal* crystal = layer.SimLevel()->goals.FindCrystal(name);
    CHECK_MSG(crystal != nullptr && crystal->collected && !crystal->expired, "the player takes crystal_1264");
    std::optional<MagicPortalsLayer::TimerReport> dial = DialOf(layer, name);
    CHECK(dial.has_value());
    if (crystal == nullptr || !crystal->collected || !dial) return;
    // THE TICK IT IS TAKEN: its picture goes, and the dial takes one more live
    // frame. Goals' clock stopped on this tick; the dial's own is a tick on.
    const double elapsedMs =
        (crystal->lifeS - crystal->leftS) * 1000.0 + static_cast<double>(MagicPortalsLayer::kTick) * 1000.0;
    const glm::dvec2 pulse = timer.PulseAt(elapsedMs, 10000.0);
    CHECK_MSG(std::fabs(elapsedMs - taken * 1000.0 / 60.0) < 1e-3,
              "the dial's clock on the tick it is taken, tick " + std::to_string(taken) + ": " +
                  std::to_string(elapsedMs) + " ms");
    CHECK_MSG(ColoursOf(registry, "Magic Portals Sprite", "crystal.png").size() == pictures - 1,
              "the crystal's picture goes on the tick it is taken");
    CHECK_MSG(dial->drawn && !dial->shrinking && glm::length(dial->scale - pulse) < 1e-12 && dial->scale.x >= 1.0 &&
                  dial->alpha == 0.5 && dial->frame == timer.FrameAt(elapsedMs, 10000.0) && dial->atPx == atPx,
              "on the tick it is taken the dial pulses once more where it stood, unshrunk: scale " +
                  std::to_string(dial->scale.x) + " against " + std::to_string(pulse.x) + ", alpha " +
                  std::to_string(dial->alpha));
    const glm::dvec2 takenScale = dial->scale;
    const int takenFrame = dial->frame;
    // THE NEXT TICK: x 0.9 on that pulse and on alpha 0.5, on the same cell, still there.
    tickWith(layer, registry, kRest, {}, {});
    dial = DialOf(layer, name);
    CHECK(dial.has_value());
    if (!dial) return;
    CHECK_MSG(dial->drawn && dial->shrinking && std::fabs(dial->scale.x - takenScale.x * 0.9) < 1e-6 &&
                  std::fabs(dial->alpha - 0.45) < 1e-6 && dial->frame == takenFrame && dial->atPx == atPx,
              "on the next tick it shrinks where it stood: " + std::to_string(dial->scale.x) + " from " +
                  std::to_string(takenScale.x) + ", alpha " + std::to_string(dial->alpha));
    int shrinking = 1;
    while (shrinking < 60) {
        tickWith(layer, registry, kRest, {}, {});
        const std::optional<MagicPortalsLayer::TimerReport> now = DialOf(layer, name);
        if (!now || !now->drawn) break;
        ++shrinking;
    }
    std::printf("  crystal_1264 taken on tick %d: its dial pulses at %.4f, then drawn shrinking on %d ticks\n", taken,
                takenScale.x, shrinking);
    // From 1.0265, under 0.1 / (2.8125 x 0.9^32) = 1.0355: gone on its 32nd x 0.9,
    // drawn shrinking on 31 ticks.
    CHECK_MSG(shrinking == 31, "and is gone after " + std::to_string(shrinking) + " shrinking ticks (31)");
    CHECK_MSG(Tagged(registry, "Magic Portals Timer") == 0, "with its quad");
    layer.OnDetach(registry);
}

// ---- the crystals' and keys' bob (the visuals plan's 3.2) -------------------------
//
// ETHCallback_crystal and ETHCallback_key call linearMotion once a frame: speed 2,
// stride 1.5, vertical, from a start angle of randF(PI) (motions.json). The layer
// makes that call once a tick for a crystal while it lives and a key while no one
// carries it and it is not spent, BEFORE it places the pictures, and moves the
// picture and what its entity carries by the offset: never Game::Level.

MagicPortals::Motion::Rules MotionRules() {
    MagicPortals::Motion::Rules rules;
    std::string error;
    CHECK_MSG(MagicPortals::Motion::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/motions.json", rules, error),
              error);
    return rules;
}

std::optional<MagicPortalsLayer::MotionReport> MotionOf(const MagicPortalsLayer& layer, const std::string& node) {
    for (const MagicPortalsLayer::MotionReport& report : layer.MotionReports()) {
        if (report.node == node) return report;
    }
    return std::nullopt;
}

glm::dvec2 QuadPx(entt::registry& registry, entt::entity quad) {
    if (quad == entt::null || !registry.valid(quad)) return glm::dvec2(-1e9);
    return MagicPortals::Units::ToPixels(registry.get<TransformComponent>(quad).position);
}

void ACrystalBobsAsItsScriptDoes() {
    if (!OriginalArtIsThere("ACrystalBobsAsItsScriptDoes")) return;
    namespace Motion = MagicPortals::Motion;
    const Motion::Rules rules = MotionRules();
    // 1-06 (level5): five crystals, the capture gate's level.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level5");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;

    // At load no call has run: every crystal where the level put it, each with a
    // start angle of its own on [0, PI].
    const std::vector<MagicPortalsLayer::MotionReport> atLoad = layer.MotionReports();
    CHECK_MSG(atLoad.size() == 5 && layer.SimLevel()->goals.crystals.size() == 5,
              std::to_string(atLoad.size()) + " bobbing pictures");
    std::set<double> starts;
    int wrongAtLoad = 0;
    for (const MagicPortalsLayer::MotionReport& report : atLoad) {
        starts.insert(report.motion.startAngle);
        const bool ok = report.crystal && report.drawn && report.moving && !report.motion.started &&
                        report.offsetPx == glm::dvec2(0.0) && report.motion.startAngle >= 0.0 &&
                        report.motion.startAngle <= static_cast<double>(3.141592654f) &&
                        glm::distance(QuadPx(registry, report.quad), report.centrePx) < 1e-3;
        if (!ok) ++wrongAtLoad;
    }
    CHECK_MSG(wrongAtLoad == 0, std::to_string(wrongAtLoad) + " crystals not still, unstarted and in range at load");
    CHECK_MSG(starts.size() == atLoad.size(), "a start angle each: " + std::to_string(starts.size()) + " distinct");
    CloseTheLevelStartPopup(layer, registry);

    // Every tick the level runs: one call, the tick's step, and the picture and the
    // sparkles moved by its offset from where the level put them.
    std::vector<MagicPortalsLayer::MotionReport> previous = layer.MotionReports();
    std::vector<double> lowest(atLoad.size(), 1e9);
    std::vector<double> highest(atLoad.size(), -1e9);
    int stepped = 0;
    int held = 0;
    int wrong = 0;
    std::string first;
    for (int tick = 0; tick < 420; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const std::vector<MagicPortalsLayer::MotionReport> now = layer.MotionReports();
        const std::vector<MagicPortalsLayer::EmitterReport> emitters = layer.EmitterReports();
        if (now.size() != atLoad.size()) {
            ++wrong;
            break;
        }
        for (std::size_t i = 0; i < now.size(); ++i) {
            const MagicPortalsLayer::MotionReport& report = now[i];
            Motion::Linear expected = previous[i].motion;
            Motion::Advance(rules, expected, static_cast<double>(MagicPortalsLayer::kTick) * 1000.0);
            const bool didStep = report.motion.angle != previous[i].motion.angle || report.motion.started != previous[i].motion.started;
            if (didStep) {
                ++stepped;
                if (report.motion.angle != expected.angle || !report.motion.started) {
                    if (wrong++ == 0) first = report.node + " on tick " + std::to_string(tick) + ": angle";
                }
            } else {
                ++held;
            }
            const glm::dvec2 offset = Motion::OffsetPx(report.motion);
            const auto* sparkle = ReportOf(emitters, report.node, 0);
            const bool placed = report.offsetPx == offset && std::fabs(offset.x) == 0.0 &&
                                glm::distance(report.centrePx, atLoad[i].centrePx + offset) < 1e-9 &&
                                glm::distance(report.ownerPx, atLoad[i].ownerPx + offset) < 1e-9 &&
                                glm::distance(QuadPx(registry, report.quad), report.centrePx) < 1e-3 &&
                                sparkle != nullptr && sparkle->owner.atPx == report.ownerPx;
            if (!placed && wrong++ == 0) {
                first = report.node + " on tick " + std::to_string(tick) + ": drawn at " + Point(report.centrePx) +
                        ", offset " + Point(report.offsetPx);
            }
            lowest[i] = std::min(lowest[i], offset.y);
            highest[i] = std::max(highest[i], offset.y);
        }
        previous = now;
    }
    int fullSwing = 0;
    for (std::size_t i = 0; i < atLoad.size(); ++i) {
        if (highest[i] > 1.49 && lowest[i] < -1.49 && highest[i] <= 1.5 && lowest[i] >= -1.5) ++fullSwing;
    }
    std::printf("  1-06's crystals: %d steps, %d held, %d of %zu swung 1.5 either way over 7 s\n", stepped, held,
                fullSwing, atLoad.size());
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " wrong; the first " + first);
    CHECK_MSG(stepped >= 5 * 400, "a call a tick: " + std::to_string(stepped));
    CHECK_EQ(fullSwing, static_cast<int>(atLoad.size()));

    // THE PAUSE: m_factor 0, so the angle holds and nothing moves under it.
    CHECK_MSG(TapThePauseControl(layer, registry), "the pause control is there to tap");
    CHECK(layer.Paused());
    const std::vector<MagicPortalsLayer::MotionReport> paused = layer.MotionReports();
    int movedUnderThePause = 0;
    for (int tick = 0; tick < 120; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const std::vector<MagicPortalsLayer::MotionReport> now = layer.MotionReports();
        for (std::size_t i = 0; i < now.size() && i < paused.size(); ++i) {
            if (now[i].motion.angle != paused[i].motion.angle || now[i].centrePx != paused[i].centrePx ||
                QuadPx(registry, now[i].quad) != QuadPx(registry, paused[i].quad)) {
                ++movedUnderThePause;
            }
        }
    }
    CHECK_MSG(layer.Paused() && movedUnderThePause == 0,
              std::to_string(movedUnderThePause) + " crystal-ticks moved under the pause");
    layer.OnDetach(registry);
}

void ALyingKeyBobsAndACarriedOneDoesNot() {
    if (!OriginalArtIsThere("ALyingKeyBobsAndACarriedOneDoesNot")) return;
    namespace Motion = MagicPortals::Motion;
    // 3-08 (level7b): key_ent_1335, taken up by the player.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level7b");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    CloseTheLevelStartPopup(layer, registry);
    const std::string name = "key_ent_1335";
    int keys = 0;
    for (const MagicPortalsLayer::MotionReport& report : layer.MotionReports()) {
        if (!report.crystal) ++keys;
    }
    CHECK_MSG(keys == static_cast<int>(layer.SimLevel()->keys.keys.size()) && keys == 1, "the level's one key bobs");
    const auto* key = layer.SimLevel()->keys.FindKey(name);
    CHECK(key != nullptr);
    if (key == nullptr) return;
    const glm::dvec2 lyingAt = key->atPx;

    // Lying: about where it lies, sparkles and all.
    int wrong = 0;
    for (int tick = 0; tick < 200; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const std::optional<MagicPortalsLayer::MotionReport> report = MotionOf(layer, name);
        const auto* sparkle = ReportOf(layer.EmitterReports(), name, 0);
        const glm::dvec2 offset = report ? Motion::OffsetPx(report->motion) : glm::dvec2(0.0);
        if (!report || !report->moving || !report->motion.started || report->offsetPx != offset ||
            glm::distance(report->ownerPx, lyingAt + offset) > 1e-9 ||
            glm::distance(QuadPx(registry, report->quad), report->centrePx) > 1e-3 || sparkle == nullptr ||
            sparkle->owner.atPx != report->ownerPx) {
            ++wrong;
        }
    }
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " ticks the lying key was not drawn about where it lies, bobbed");

    // Taken: no call, so no offset, and its angle holds for when it is dropped.
    PutPlayerAt(registry, layer, lyingAt);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.SimLevel()->keys.FindKey(name)->owner == layer.SimLevel()->player, "the player takes the key");
    const std::optional<MagicPortalsLayer::MotionReport> taken = MotionOf(layer, name);
    CHECK(taken.has_value());
    if (!taken) return;
    int carriedWrong = 0;
    for (int tick = 0; tick < 120; ++tick) {
        PutPlayerAt(registry, layer, glm::dvec2(lyingAt.x + 2.0 * tick, lyingAt.y));
        tickWith(layer, registry, kRest, {}, {});
        const std::optional<MagicPortalsLayer::MotionReport> report = MotionOf(layer, name);
        const auto* carried = layer.SimLevel()->keys.FindKey(name);
        if (!report || report->moving || report->offsetPx != glm::dvec2(0.0) ||
            report->motion.angle != taken->motion.angle || report->ownerPx != carried->atPx ||
            glm::distance(QuadPx(registry, report->quad), report->centrePx) > 1e-3) {
            ++carriedWrong;
        }
    }
    CHECK_MSG(carriedWrong == 0, std::to_string(carriedWrong) + " carried ticks bobbed or moved the angle");
    layer.OnDetach(registry);
}

void ABobsPhasesAreTheLevelsOwn() {
    if (!OriginalArtIsThere("ABobsPhasesAreTheLevelsOwn")) return;
    const auto startsOf = [](const MagicPortalsLayer& layer) {
        std::vector<std::pair<std::string, double>> starts;
        for (const MagicPortalsLayer::MotionReport& report : layer.MotionReports()) {
            starts.emplace_back(report.node, report.motion.startAngle);
        }
        return starts;
    };
    std::vector<std::pair<std::string, double>> fresh[2];
    for (int run = 0; run < 2; ++run) {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level5");
        layer.OnAttach(registry);
        fresh[run] = startsOf(layer);
        layer.OnDetach(registry);
    }
    CHECK_MSG(fresh[0].size() == 5 && fresh[0] == fresh[1], "1-06 draws the same phases on every run");
    std::set<double> firstFive;
    for (const auto& [node, start] : fresh[0]) firstFive.insert(start);
    // After another level: re-seeded, so what was played before does not move them.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level4");
    layer.OnAttach(registry);
    CHECK_MSG(!layer.MotionReports().empty(), "1-05 has crystals of its own");
    // Its own phases, not 1-06's: each level's stream is seeded with its name.
    int shared = 0;
    for (const MagicPortalsLayer::MotionReport& report : layer.MotionReports()) {
        if (firstFive.count(report.motion.startAngle) != 0) ++shared;
    }
    CHECK_MSG(shared == 0, std::to_string(shared) + " of 1-05's start angles are 1-06's");
    press(layer, registry, MagicPortalsLayer::kSkip);
    CHECK(IsAt(layer, "level5"));
    CHECK_MSG(startsOf(layer) == fresh[0], "1-06 after 1-05 draws 1-06's phases");
    layer.OnDetach(registry);
}

void ATimedCrystalsDialBobsWithIt() {
    // The dial takes its crystal's drawn centre every tick (ETHCallback_timer ins
    // 93-100, SetPositionXY(crystal.GetPositionXY())), which is the bobbed one.
    if (!OriginalArtIsThere("ATimedCrystalsDialBobsWithIt")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level26");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    const std::string name = "crystal_1264";
    double lowest = 1e9;
    double highest = -1e9;
    int apart = 0;
    for (int tick = 0; tick < 240; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        const std::optional<MagicPortalsLayer::TimerReport> dial = DialOf(layer, name);
        const std::optional<MagicPortalsLayer::MotionReport> crystal = MotionOf(layer, name);
        if (!dial || !crystal || dial->atPx != crystal->centrePx) {
            ++apart;
            continue;
        }
        lowest = std::min(lowest, dial->atPx.y);
        highest = std::max(highest, dial->atPx.y);
    }
    std::printf("  1-27's dial: y %.3f..%.3f with its crystal\n", lowest, highest);
    CHECK_MSG(apart == 0, std::to_string(apart) + " ticks the dial was not on its bobbing crystal");
    CHECK_MSG(highest - lowest > 2.99 && highest - lowest <= 3.0 + 1e-9,
              "and swung the crystal's 3 units: " + std::to_string(highest - lowest));
    layer.OnDetach(registry);
}

// ---- the lightmaps (step 47) ------------------------------------------------------
//
// A static sprite that applies light adds its baked lightmap, the level's
// add<id>.png, over its colour times its ambient (the lighting design's G4). The
// layer names it as the overlay of that sprite's own material, and the engine's 2D
// sprite path adds it.

struct Overlaid {
    std::string albedo;
    std::string overlay;
    glm::dvec2 centrePx{0.0};
    MaterialComponent::Sprite2DLight sprite;
    MaterialComponent::BlendMode blend{MaterialComponent::BlendMode::Alpha};
};

// Every material in the registry that names an overlay, whatever it is.
std::vector<Overlaid> OverlaysOf(entt::registry& registry) {
    std::vector<Overlaid> out;
    for (auto [entity, material, transform] : registry.view<MaterialComponent, TransformComponent>().each()) {
        (void)entity;
        if (material.overlayTexturePath.empty()) continue;
        out.push_back(Overlaid{material.albedoTexturePath, material.overlayTexturePath,
                               MagicPortals::Units::ToPixels(transform.position), material.sprite2D, material.blend});
    }
    return out;
}

std::vector<std::string> SortedOverlayPaths(const std::vector<Overlaid>& overlays) {
    std::vector<std::string> out;
    for (const Overlaid& o : overlays) out.push_back(o.overlay);
    std::sort(out.begin(), out.end());
    return out;
}

// ---- 3.3: the placed pictures' own callbacks (motions.json `placed`) -----------------
//
// ETHCallback_hand_drawn_arrow sways the hint arrow along its own angle by its node's
// stride and fades it to 0.55; ETHCallback_dashed_circle bobs the circle up and down,
// fades it and turns it 8 degrees a second; ETHCallback_dashed_square only fades;
// ETHCallback_hand_tap sways the hand along its angle less 90, at the alpha the level
// gives it. Once a tick, before the draw, as the crystals' bob.

std::optional<MagicPortalsLayer::PlacedReport> PlacedOf(const MagicPortalsLayer& layer, const std::string& node) {
    for (const MagicPortalsLayer::PlacedReport& report : layer.PlacedReports()) {
        if (report.node == node) return report;
    }
    return std::nullopt;
}

float QuadAlpha(entt::registry& registry, entt::entity quad) {
    if (quad == entt::null || !registry.valid(quad)) return -1.0f;
    return registry.get<MaterialComponent>(quad).albedoColor.a;
}

float QuadTurn(entt::registry& registry, entt::entity quad) {
    if (quad == entt::null || !registry.valid(quad)) return -1e9f;
    return registry.get<TransformComponent>(quad).rotation.z;
}

void AnArrowSwaysAlongItsAngleAtItsAlpha() {
    if (!OriginalArtIsThere("AnArrowSwaysAlongItsAngleAtItsAlpha")) return;
    namespace Motion = MagicPortals::Motion;
    const Motion::Rules rules = MotionRules();
    // 1-08 (level7): the capture gate's arrow at angle 0, and the dashed square beside it.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level7");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    const std::string arrowNode = "hand_drawn_arrow_ent_600";
    const std::string squareNode = "dashed_square_ent_676";
    CHECK_EQ(layer.PlacedReports().size(), std::size_t{2});
    const std::optional<MagicPortalsLayer::PlacedReport> arrow = PlacedOf(layer, arrowNode);
    const std::optional<MagicPortalsLayer::PlacedReport> square = PlacedOf(layer, squareNode);
    CHECK(arrow.has_value() && square.has_value());
    if (!arrow || !square) return;
    // At load: no call yet, where the level put them, both at the alpha their scripts set.
    CHECK(arrow->entity == "hand_drawn_arrow" && arrow->drawn && arrow->moves && !arrow->motion.started &&
          arrow->offsetPx == glm::dvec2(0.0) && arrow->motion.speed == 4.0 && arrow->motion.stride == 2.5 &&
          !arrow->motion.vertical && arrow->motion.axisDeg == 0.0 && arrow->alpha == 0.55f);
    CHECK(square->entity == "dashed_square" && square->drawn && !square->moves && square->alpha == 0.55f);
    const glm::dvec2 arrowAt = arrow->centrePx;
    const glm::dvec2 squareAt = square->centrePx;
    const float arrowTurn = QuadTurn(registry, arrow->quad);
    CloseTheLevelStartPopup(layer, registry);

    Motion::Linear expected = arrow->motion;
    double lowest = 1e9;
    double highest = -1e9;
    int wrong = 0;
    std::string first;
    double firstOffset = 0.0;
    for (int tick = 0; tick < 480; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const std::optional<MagicPortalsLayer::PlacedReport> now = PlacedOf(layer, arrowNode);
        const std::optional<MagicPortalsLayer::PlacedReport> still = PlacedOf(layer, squareNode);
        if (!now || !still) {
            ++wrong;
            break;
        }
        Motion::Advance(rules, expected, static_cast<double>(MagicPortalsLayer::kTick) * 1000.0);
        const glm::dvec2 offset = Motion::OffsetPx(expected);
        if (tick == 0) firstOffset = now->offsetPx.x;
        const bool placed = now->motion.angle == expected.angle && now->offsetPx == offset && offset.y == 0.0 &&
                            glm::distance(now->centrePx, arrowAt + offset) < 1e-9 &&
                            glm::distance(QuadPx(registry, now->quad), now->centrePx) < 1e-3 &&
                            std::fabs(QuadAlpha(registry, now->quad) - 0.55f) < 1e-6f &&
                            QuadTurn(registry, now->quad) == arrowTurn && now->turnedDeg == 0.0;
        const bool fades = still->centrePx == squareAt && std::fabs(QuadAlpha(registry, still->quad) - 0.55f) < 1e-6f &&
                           glm::distance(QuadPx(registry, still->quad), squareAt) < 1e-3;
        if ((!placed || !fades) && wrong++ == 0) {
            first = "tick " + std::to_string(tick) + ": arrow at " + Point(now->centrePx) + " offset " +
                    Point(now->offsetPx) + " alpha " + std::to_string(QuadAlpha(registry, now->quad)) + ", square at " +
                    Point(still->centrePx) + " alpha " + std::to_string(QuadAlpha(registry, still->quad));
        }
        lowest = std::min(lowest, offset.x);
        highest = std::max(highest, offset.x);
    }
    std::printf("  1-08's arrow: first tick +%.4f, x %.4f..%.4f over 8 s; the square still\n", firstOffset, lowest,
                highest);
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " wrong; the first " + first);
    // cos(0 + one step): a whole stride forward on the first tick.
    CHECK_MSG(firstOffset > 2.49 && firstOffset <= 2.5, "the first tick's swing: " + std::to_string(firstOffset));
    CHECK(highest > 2.49 && highest <= 2.5 && lowest < -2.49 && lowest >= -2.5);

    // THE PAUSE: m_factor 0, so neither moves nor fades back.
    CHECK_MSG(TapThePauseControl(layer, registry), "the pause control is there to tap");
    CHECK(layer.Paused());
    const std::optional<MagicPortalsLayer::PlacedReport> paused = PlacedOf(layer, arrowNode);
    int moved = 0;
    for (int tick = 0; tick < 120 && paused; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const std::optional<MagicPortalsLayer::PlacedReport> now = PlacedOf(layer, arrowNode);
        if (!now || now->motion.angle != paused->motion.angle || now->centrePx != paused->centrePx ||
            QuadPx(registry, now->quad) != QuadPx(registry, paused->quad) ||
            std::fabs(QuadAlpha(registry, now->quad) - 0.55f) > 1e-6f) {
            ++moved;
        }
    }
    CHECK_MSG(paused && layer.Paused() && moved == 0, std::to_string(moved) + " ticks the arrow moved under the pause");
    layer.OnDetach(registry);
}

void AnArrowAndAHandSwayAlongTheirOwnAngles() {
    if (!OriginalArtIsThere("AnArrowAndAHandSwayAlongTheirOwnAngles")) return;
    // The direction a swing takes on the +y-down screen for an axis of `deg`,
    // counter-clockwise: (cos, -sin).
    const auto along = [](double deg) {
        return glm::dvec2(std::cos(glm::radians(deg)), -std::sin(glm::radians(deg)));
    };
    const auto crossOf = [](const glm::dvec2& a, const glm::dvec2& b) { return a.x * b.y - a.y * b.x; };
    struct Case {
        const char* level;
        const char* node;
        double axisDeg;   // the swing's axis: the node's angle, less 90 for a hand
        double stride;
        bool fades;
    };
    const Case cases[] = {
        {"level9", "hand_drawn_arrow_ent_623", 45.0, 2.5, true},   // 1-10, rotation -0.7854
        {"level11", "hand_drawn_arrow_ent_655", -50.0, 2.5, true}, // 1-12, rotation 0.8727
        {"level15", "hand_tap_ent_768", 5.0, 4.0, false},          // 1-16, at 95 degrees
        {"level15", "hand_tap_ent_765", -90.0, 4.0, false},        // 1-16, at 0: down the screen
    };
    for (const Case& one : cases) {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), one.level);
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
        const std::optional<MagicPortalsLayer::PlacedReport> atLoad = PlacedOf(layer, one.node);
        CHECK_MSG(atLoad.has_value() && atLoad->moves, std::string(one.node) + " swings");
        if (!atLoad) continue;
        CHECK_MSG(std::fabs(atLoad->motion.axisDeg - one.axisDeg) < 0.01,
                  std::string(one.node) + "'s axis " + std::to_string(atLoad->motion.axisDeg));
        CloseTheLevelStartPopup(layer, registry);
        const glm::dvec2 axis = along(atLoad->motion.axisDeg);
        double far = 0.0;
        double across = 0.0;
        int wrong = 0;
        for (int tick = 0; tick < 150; ++tick) {
            tickWith(layer, registry, kRest, {}, {});
            layer.OnUpdate(registry, MagicPortalsLayer::kTick);
            const std::optional<MagicPortalsLayer::PlacedReport> now = PlacedOf(layer, one.node);
            if (!now) {
                ++wrong;
                break;
            }
            const glm::dvec2 moved = now->centrePx - atLoad->centrePx;
            far = std::max(far, std::fabs(glm::dot(moved, axis)));
            across = std::max(across, std::fabs(crossOf(axis, moved)));
            if (tick == 0 && glm::dot(moved, axis) < one.stride * 0.99) ++wrong; // a whole stride forward first
            const float alpha = QuadAlpha(registry, now->quad);
            if (one.fades ? std::fabs(alpha - 0.55f) > 1e-6f : alpha != 1.0f) ++wrong;
            if (glm::distance(QuadPx(registry, now->quad), now->centrePx) > 1e-3) ++wrong;
        }
        std::printf("  %s %s: along %.4f, across %.2g\n", one.level, one.node, far, across);
        CHECK_MSG(wrong == 0 && far > one.stride * 0.99 && far <= one.stride + 1e-9 && across < 1e-9,
                  std::string(one.level) + " " + one.node + ": " + std::to_string(wrong) + " wrong, along " +
                      std::to_string(far) + ", across " + std::to_string(across));
        layer.OnDetach(registry);
    }
}

void ADashedCircleBobsAndTurns() {
    if (!OriginalArtIsThere("ADashedCircleBobsAndTurns")) return;
    namespace Motion = MagicPortals::Motion;
    const Motion::Rules rules = MotionRules();
    // 1-05 (level4): the capture gate's two circles, and its crystals.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level4");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty()) return;
    const std::vector<MagicPortalsLayer::PlacedReport> atLoad = layer.PlacedReports();
    CHECK_EQ(atLoad.size(), std::size_t{2});
    if (atLoad.size() != 2) return;

    // K8: a placed row draws no start angle, so 1-05's crystals take the stream's
    // draws in order exactly as before.
    MagicPortals::Motion::Phases phases(Motion::LevelSeed(20260917u, "level4"));
    int drift = 0;
    for (const MagicPortalsLayer::MotionReport& report : layer.MotionReports()) {
        if (report.motion.startAngle != phases.Next(report.crystal ? rules.crystal : rules.key)) ++drift;
    }
    CHECK_MSG(!layer.MotionReports().empty() && drift == 0,
              std::to_string(drift) + " crystal start angles not the stream's draws in order");

    std::vector<float> turnAtLoad;
    for (const MagicPortalsLayer::PlacedReport& report : atLoad) {
        CHECK(report.entity == "dashed_circle" && report.moves && report.motion.vertical && report.motion.axisDeg == 0.0 &&
              report.motion.speed == 1.5 && report.motion.stride == 0.6 && report.turnedDeg == 0.0 &&
              report.alpha == 0.55f);
        turnAtLoad.push_back(QuadTurn(registry, report.quad));
    }
    CloseTheLevelStartPopup(layer, registry);

    Motion::Turn turn;
    turn.degPerS = 8.0;
    std::vector<Motion::Linear> expected;
    for (const MagicPortalsLayer::PlacedReport& report : atLoad) expected.push_back(report.motion);
    double lowest = 1e9;
    double highest = -1e9;
    int wrong = 0;
    std::string first;
    for (int tick = 0; tick < 600; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        Motion::Advance(rules, turn, static_cast<double>(MagicPortalsLayer::kTick) * 1000.0);
        const std::vector<MagicPortalsLayer::PlacedReport> now = layer.PlacedReports();
        if (now.size() != atLoad.size()) {
            ++wrong;
            break;
        }
        for (std::size_t i = 0; i < now.size(); ++i) {
            Motion::Advance(rules, expected[i], static_cast<double>(MagicPortalsLayer::kTick) * 1000.0);
            const glm::dvec2 offset = Motion::OffsetPx(expected[i]);
            // Counter-clockwise: the engine's +z, as Ethanon's angle.
            const float turned = turnAtLoad[i] + static_cast<float>(glm::radians(turn.turnedDeg));
            const bool ok = now[i].offsetPx == offset && offset.x == 0.0 &&
                            glm::distance(now[i].centrePx, atLoad[i].centrePx + offset) < 1e-9 &&
                            glm::distance(QuadPx(registry, now[i].quad), now[i].centrePx) < 1e-3 &&
                            now[i].turnedDeg == turn.turnedDeg &&
                            std::fabs(now[i].ownerAngleDeg - (atLoad[i].ownerAngleDeg + turn.turnedDeg)) < 1e-9 &&
                            std::fabs(QuadTurn(registry, now[i].quad) - turned) < 1e-5f &&
                            std::fabs(QuadAlpha(registry, now[i].quad) - 0.55f) < 1e-6f;
            if (!ok && wrong++ == 0) {
                first = now[i].node + " on tick " + std::to_string(tick) + ": offset " + Point(now[i].offsetPx) +
                        ", turned " + std::to_string(now[i].turnedDeg) + ", quad turn " +
                        std::to_string(QuadTurn(registry, now[i].quad));
            }
            lowest = std::min(lowest, offset.y);
            highest = std::max(highest, offset.y);
        }
    }
    std::printf("  1-05's circles: y %.4f..%.4f, turned %.3f degrees over 10 s\n", lowest, highest, turn.turnedDeg);
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " wrong; the first " + first);
    CHECK(highest > 0.599 && highest <= 0.6 && lowest < -0.599 && lowest >= -0.6);
    CHECK_MSG(std::fabs(turn.turnedDeg - 80.0) < 1e-3, "10 s at 8 degrees a second: " + std::to_string(turn.turnedDeg));
    layer.OnDetach(registry);
}

void LightmapsAreDrawnOverTheirSprites() {
    // 1-1 (level0): nine lightmaps, one of them the torch's.
    {
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), "level0");
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
                  layer.LoadError() + layer.ArtError() + layer.LightingError());
        if (layer.SimLevel() == nullptr || !layer.ArtError().empty() || !layer.LightingError().empty()) return;

        const std::vector<Overlaid> overlays = OverlaysOf(registry);
        CHECK_EQ(overlays.size(), std::size_t{9});
        CHECK_MSG(SortedOverlayPaths(overlays) == layer.HeldLightmaps(),
                  "one sprite for each lightmap the layer holds, and nothing else names one");
        // Four are emissive 0 and so at the ambient (the arches, the wall, the
        // torch); five are emissive 1 and whole (the platforms, the bar and the
        // stone): the lightmap is added over either.
        int dimmed = 0;
        int whole = 0;
        for (const Overlaid& o : overlays) {
            const glm::vec3 a = o.sprite.ambient;
            CHECK_MSG(o.sprite.enabled && o.sprite.overlayStrength == 1.0f &&
                          o.blend == MaterialComponent::BlendMode::Premultiplied,
                      o.overlay + ": on the 2D sprite path, all of it added, premultiplied");
            const bool emissive0 = EndsWith(o.albedo, "/arch_with_base_blur.png") || EndsWith(o.albedo, "/wall_w3.png") ||
                                   EndsWith(o.albedo, "/torch_small.png");
            const glm::vec3 want = emissive0 ? glm::vec3(0.35f, 0.30f, 0.35f) : glm::vec3(1.0f);
            const bool at = std::fabs(a.r - want.r) < 1e-5f && std::fabs(a.g - want.g) < 1e-5f &&
                            std::fabs(a.b - want.b) < 1e-5f;
            CHECK_MSG(at, o.overlay + " on " + o.albedo + ": ambient (" + std::to_string(a.r) + ", " +
                              std::to_string(a.g) + ", " + std::to_string(a.b) + ")");
            if (at) ++(emissive0 ? dimmed : whole);
        }
        CHECK_EQ(dimmed, 4);
        CHECK_EQ(whole, 5);
        // light_ent_696 stands at (288, 64) and hangs its sprite, torch_small.png,
        // 16 px below: its lightmap is add696.png, on that sprite and only there.
        const auto torch = std::find_if(overlays.begin(), overlays.end(), [](const Overlaid& o) {
            return EndsWith(o.overlay, "/lightmaps/level0/add696.png");
        });
        CHECK_MSG(torch != overlays.end(), "add696.png is drawn");
        if (torch != overlays.end()) {
            CHECK_MSG(EndsWith(torch->albedo, "/torch_small.png"), "over light_ent_696's own image: " + torch->albedo);
            CHECK_MSG(glm::distance(torch->centrePx, glm::dvec2(288.0, 80.0)) < 1e-3,
                      "where light_ent_696 draws it: " + Point(torch->centrePx));
        }
        CHECK_EQ(std::count_if(overlays.begin(), overlays.end(),
                               [](const Overlaid& o) { return EndsWith(o.albedo, "/torch_small.png"); }),
                 std::ptrdiff_t{1});

        // The static portals' halos, the door and the player name none: the first
        // two do not apply light, and nothing the player moves was baked.
        CHECK_MSG(std::none_of(overlays.begin(), overlays.end(), [](const Overlaid& o) {
                      return EndsWith(o.albedo, "portal_halo.png") || EndsWith(o.albedo, "window01.png") ||
                             EndsWith(o.albedo, "magic_portals_hd.png");
                  }),
                  "no halo, door or player among them");
        // Added sprites stay added, lit or not: no blendMode-1 instance applies light.
        int halos = 0;
        for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
            (void)entity;
            if (t.tag != "Magic Portals Sprite" || !EndsWith(material.albedoTexturePath, "portal_halo.png")) continue;
            ++halos;
            CHECK_MSG(material.blend == MaterialComponent::BlendMode::Additive && material.sprite2D.enabled,
                      "a static portal's halo: added, on the 2D sprite path");
        }
        CHECK_EQ(halos, 4);

        // A retry draws the same nine again.
        const std::vector<std::string> held = layer.HeldLightmaps();
        press(layer, registry, MagicPortalsLayer::kRetry);
        CHECK_MSG(SortedOverlayPaths(OverlaysOf(registry)) == held, "a retry draws the same nine");

        // The next level draws its own, and none of level0's.
        press(layer, registry, MagicPortalsLayer::kSkip);
        CHECK(IsAt(layer, "level1"));
        const std::vector<std::string> next = SortedOverlayPaths(OverlaysOf(registry));
        CHECK_EQ(next.size(), std::size_t{9});
        CHECK_MSG(next == layer.HeldLightmaps() && std::all_of(next.begin(), next.end(), [](const std::string& p) {
                      return p.find("/assets/lightmaps/level1/add") != std::string::npos;
                  }),
                  "level1's nine, as the layer holds them");

        // Out to the grid: nothing names an overlay.
        CHECK(OutToTheGrid(layer, registry));
        CHECK_MSG(OverlaysOf(registry).empty(), "the menu draws no lightmap");
        layer.OnDetach(registry);
    }

    // 2-05 (level4a) names none: lit, and nothing added.
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level4a");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
              layer.LoadError() + layer.ArtError() + layer.LightingError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty() || !layer.LightingError().empty()) return;
    CHECK_MSG(OverlaysOf(registry).empty() && layer.HeldLightmaps().empty(), "level4a draws no lightmap");
    int sprites = 0;
    int lit = 0;
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (t.tag != "Magic Portals Sprite") continue;
        ++sprites;
        if (material.sprite2D.enabled) ++lit;
    }
    CHECK_MSG(sprites > 0 && lit == sprites, "though every one of its sprites is on the 2D sprite path");
    layer.OnDetach(registry);
}

// ---- the lights and their halos (step 49) ------------------------------------------
//
// A level's <Light>s are Light2DComponents at their owners, and their halos added
// quads; a sprite that applies light takes the lights its mask lets through
// (Lighting::ReceiverMask); the shot carries projectile.ent's light while it flies
// (the lighting design's G5, sections 5.2 to 5.5).

std::vector<entt::entity> LightsOf(entt::registry& registry) {
    std::vector<entt::entity> out;
    for (auto [entity, light] : registry.view<Light2DComponent>().each()) {
        (void)light;
        out.push_back(entity);
    }
    return out;
}

bool NearV(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

std::string ShowV(const glm::vec3& v) {
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

// The one sprite of the level drawn with this image.
entt::entity SpriteOf(entt::registry& registry, const char* tag, const std::string& file) {
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        if (t.tag == tag && EndsWith(material.albedoTexturePath, file)) return entity;
    }
    return entt::null;
}

MagicPortals::Lighting::Rules LightingRules() {
    MagicPortals::Lighting::Rules rules;
    std::string error;
    CHECK_MSG(MagicPortals::Lighting::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/lighting.json", rules, error),
              error);
    return rules;
}

void TheTorchIsALightAndAHalo() {
    namespace Lighting = MagicPortals::Lighting;
    using MagicPortals::Units::ToMetres;
    const Lighting::Rules rules = LightingRules();
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
              layer.LoadError() + layer.ArtError() + layer.LightingError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty() || !layer.LightingError().empty()) return;

    // 1-1 places one light, light_ent_696's: static, at (288, 64) + (0, -12), 24
    // above its z of -18, range 300, colour (1, 0.5, 0.1) at the level's intensity 3.
    std::vector<entt::entity> lights = LightsOf(registry);
    CHECK_EQ(lights.size(), std::size_t{1});
    if (lights.size() != 1) return;
    {
        const Light2DComponent& light = registry.get<Light2DComponent>(lights[0]);
        CHECK_MSG(light.layers == Lighting::kStaticLights, "a static owner's light is on the static layer");
        CHECK_MSG(NearV(light.color * light.intensity, glm::vec3(3.0f, 1.5f, 0.3f)),
                  "colour (1, 0.5, 0.1) x intensity 3: " + ShowV(light.color * light.intensity));
        CHECK_MSG(::test::nearly(light.range, 6.0f, 1e-5f), "range 300 units, 6 m: " + std::to_string(light.range));
        CHECK_MSG(::test::nearly(light.height, 0.12f, 1e-6f), "height -18 + 24 = 6 units, 0.12 m: " + std::to_string(light.height));
        CHECK_MSG(light.enabled, "on");
        const glm::vec3 at = registry.get<TransformComponent>(lights[0]).position;
        const glm::vec3 want = MagicPortals::Units::ToWorld(288.0, 52.0);
        CHECK_MSG(std::fabs(at.x - want.x) < 1e-5f && std::fabs(at.y - want.y) < 1e-5f,
                  "at (288, 52) px, not turned with anything: " + ShowV(at));
    }

    // Its halo: halo.bmp, added, 300 units square at the same point, off the 2D
    // sprite path, a quarter slot in front of the torch's picture and behind the
    // flame the torch emits.
    CHECK_EQ(Tagged(registry, "Magic Portals Halo"), 1);
    const entt::entity halo = FirstTagged(registry, "Magic Portals Halo");
    const entt::entity torch = SpriteOf(registry, "Magic Portals Sprite", "/torch_small.png");
    CHECK(halo != entt::null && torch != entt::null);
    if (halo == entt::null || torch == entt::null) return;
    {
        const MaterialComponent& material = registry.get<MaterialComponent>(halo);
        CHECK_MSG(EndsWith(material.albedoTexturePath, "/assets/entities/halo.bmp") && material.unlit &&
                      material.blend == MaterialComponent::BlendMode::Additive && !material.sprite2D.enabled,
                  "halo.bmp, added, taking no ambient: " + material.albedoTexturePath);
        const TransformComponent& transform = registry.get<TransformComponent>(halo);
        CHECK_MSG(std::fabs(transform.scale.x - 6.0f) < 1e-5f && std::fabs(transform.scale.y - 6.0f) < 1e-5f,
                  "300 units square: " + ShowV(transform.scale));
        CHECK_MSG(glm::distance(MagicPortals::Units::ToPixels(transform.position), glm::dvec2(288.0, 52.0)) < 1e-3,
                  "centred on the light, the offset unscaled: " + Point(MagicPortals::Units::ToPixels(transform.position)));
        const float torchZ = registry.get<TransformComponent>(torch).position.z;
        CHECK_MSG(transform.position.z > torchZ && transform.position.z < torchZ + 0.004f,
                  "in front of the torch and behind the next slot: " + std::to_string(transform.position.z) + " over " +
                      std::to_string(torchZ));
        // Before any frame has run no particle of the flame is live, and the halo's
        // brightness is their share (ETHRenderEntity.cpp:372-376): black.
        CHECK_MSG(material.albedoColor == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), "no live flame yet: a black halo");
    }

    // The flame lives on the frame. Its live share is a count of twelve, the halo
    // is (1, 0.5, 0.1) x 0.7 x that share x lighting.json's scale, and the static
    // torch's LIGHT does not follow it (ComputeLightIntensity is 1 for a static owner).
    double sum = 0.0;
    int frames = 0;
    int offTwelfths = 0;
    int offColour = 0;
    for (int frame = 0; frame < 1260; ++frame) {
        layer.OnUpdate(registry, MagicPortalsLayer::kTick);
        const glm::vec4 c = registry.get<MaterialComponent>(halo).albedoColor;
        const double share = static_cast<double>(c.r) / (0.7 * rules.haloBrightnessScale);
        if (std::fabs(share * 12.0 - std::round(share * 12.0)) > 1e-3 || share < -1e-6 || share > 1.0 + 1e-6) ++offTwelfths;
        if (std::fabs(c.g - 0.5f * c.r) > 1e-6f || std::fabs(c.b - 0.1f * c.r) > 1e-6f || c.a != 1.0f) ++offColour;
        if (frame >= 60) {
            sum += share;
            ++frames;
        }
    }
    const double mean = frames > 0 ? sum / frames : 0.0;
    CHECK_MSG(offTwelfths == 0, std::to_string(offTwelfths) + " frame(s) whose halo is not a whole number of twelfths");
    CHECK_MSG(offColour == 0, std::to_string(offColour) + " frame(s) whose halo is not the light's colour");
    // The share is GetNumActiveParticles over the pool (the halo's, ETHRenderEntity.cpp:
    // 372-376; the count, ETHParticleManager.cpp:208-213), which the visuals plan's
    // System 8 puts at 0.74 +- 0.05 for this flame (particles.md 7.1): each of its
    // particles spends about a quarter of its life at size 0.
    CHECK_MSG(mean >= 0.69 && mean <= 0.79, "the flame's mean live share over 20 s: " + std::to_string(mean));
    std::printf("  the torch flame's live share over 20 s of frames: %.4f of 12 particles\n", mean);
    CHECK_MSG(NearV(registry.get<Light2DComponent>(lights[0]).color, glm::vec3(3.0f, 1.5f, 0.3f)),
              "and the static torch's light stays whole");

    // Who takes it. The arches are static and apply light: the live layer only,
    // their own normal map, their own depth. The sky applies none. The static
    // portals' added halos apply none.
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (t.tag != "Magic Portals Sprite") continue;
        const auto& s = material.sprite2D;
        if (EndsWith(material.albedoTexturePath, "/arch_with_base_blur.png")) {
            CHECK_MSG(s.lightMask == Lighting::kLiveLights && s.normalYDown &&
                          EndsWith(material.normalTexturePath, "/normalmaps/arch_with_base_nm.png") &&
                          ::test::nearly(s.height, ToMetres(-20.0), 1e-6f),
                      "an arch: live lights only, arch_with_base_nm.png, height -20 units: mask " +
                          std::to_string(s.lightMask) + ", " + material.normalTexturePath);
        } else if (EndsWith(material.albedoTexturePath, "/portal_halo.png") ||
                   EndsWith(material.albedoTexturePath, "/sky.png")) {
            CHECK_MSG(s.lightMask == 0 && material.normalTexturePath.empty(),
                      material.albedoTexturePath + " applies no light: no mask, no normal map");
        }
    }
    if (OriginalArtIsThere("TheTorchIsALightAndAHalo (the player)")) {
        const entt::entity mage = FirstTagged(registry, "Magic Portals Player Sprite");
        CHECK(mage != entt::null);
        if (mage != entt::null) {
            const MaterialComponent& material = registry.get<MaterialComponent>(mage);
            CHECK_MSG(material.sprite2D.lightMask == (Lighting::kLiveLights | Lighting::kStaticLights),
                      "the player is not static: every light reaches it, the torch's included");
            CHECK_MSG(EndsWith(material.normalTexturePath, "/entities/normalmaps/normalmap_77.png"),
                      "through dark_mage.ent's normal map: " + material.normalTexturePath);
            CHECK_MSG(material.sprite2D.height == 0.0f && material.sprite2D.normalYDown,
                      "at main_char's depth, 0, the map's green down");
        }
    }

    // The debug switch takes every mask away and nothing else.
    layer.ForceLightMasksOff(true);
    tickWith(layer, registry, kRest, {}, {});
    int masked = 0;
    for (auto [entity, material] : registry.view<MaterialComponent>().each()) {
        (void)entity;
        if (material.sprite2D.lightMask != 0) ++masked;
    }
    CHECK_MSG(masked == 0, std::to_string(masked) + " sprite(s) still take a light with the masks forced off");
    CHECK_EQ(LightsOf(registry).size(), std::size_t{1});
    CHECK_EQ(Tagged(registry, "Magic Portals Halo"), 1);
    layer.ForceLightMasksOff(false);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(registry.get<MaterialComponent>(SpriteOf(registry, "Magic Portals Sprite", "/arch_with_base_blur.png"))
                      .sprite2D.lightMask == Lighting::kLiveLights,
              "and gives them back");

    // A retry draws the same light and halo again, not a second set.
    press(layer, registry, MagicPortalsLayer::kRetry);
    CHECK_EQ(LightsOf(registry).size(), std::size_t{1});
    CHECK_EQ(Tagged(registry, "Magic Portals Halo"), 1);

    // 1-2 (level1) places two: its torch's, with a halo, and a static portal's
    // blue one of range 90, with none.
    press(layer, registry, MagicPortalsLayer::kSkip);
    CHECK(IsAt(layer, "level1"));
    lights = LightsOf(registry);
    CHECK_EQ(lights.size(), std::size_t{2});
    int torches = 0;
    int portals = 0;
    for (const entt::entity e : lights) {
        const Light2DComponent& light = registry.get<Light2DComponent>(e);
        CHECK_MSG(light.layers == Lighting::kStaticLights, "both owners are static");
        if (NearV(light.color, glm::vec3(3.0f, 1.5f, 0.3f)) && ::test::nearly(light.height, ToMetres(8.0), 1e-6f)) ++torches;
        if (NearV(light.color, glm::vec3(1.8f, 1.8f, 3.0f)) && ::test::nearly(light.range, ToMetres(90.0), 1e-6f) &&
            light.height == 0.0f) {
            ++portals;
        }
    }
    CHECK_EQ(torches, 1);
    CHECK_EQ(portals, 1);
    CHECK_EQ(Tagged(registry, "Magic Portals Halo"), 1);

    // The grid has none.
    CHECK(OutToTheGrid(layer, registry));
    CHECK_MSG(LightsOf(registry).empty() && Tagged(registry, "Magic Portals Halo") == 0, "the menu has no light");
    layer.OnDetach(registry);

    // 2-05 (level4a) places none.
    entt::registry second;
    publishViewport(second);
    MagicPortalsLayer dark(TestPaths(), "level4a");
    dark.OnAttach(second);
    CHECK_MSG(dark.SimLevel() != nullptr && dark.LightingError().empty(), dark.LoadError() + dark.LightingError());
    CHECK_MSG(LightsOf(second).empty() && Tagged(second, "Magic Portals Halo") == 0, "level4a places no light");
    dark.OnDetach(second);
}

// Step 55 (the lighting design's G6): the shot that lights a torch adds
// light_from_projectile.ent's static light there, and the level bakes at run
// time from then on - every sprite that applies light takes the static lights
// live, and nothing draws a file lightmap.
void ALitTorchBakesTheLevelAtRunTime() {
    namespace Lighting = MagicPortals::Lighting;
    using MagicPortals::Units::ToMetres;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level21c");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
              layer.LoadError() + layer.ArtError() + layer.LightingError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty() || !layer.LightingError().empty()) return;
    if (layer.SimLevel()->torch.lights.empty()) {
        CHECK_MSG(false, "level21c (4-22) places a torch");
        return;
    }
    CloseTheLevelStartPopup(layer, registry);
    waitForFirstTap(layer, registry);
    const auto masks = [&registry]() {
        std::vector<std::uint8_t> out;
        for (auto [entity, tag, material] : registry.view<TagComponent, MaterialComponent>().each()) {
            (void)entity;
            if (tag.tag == "Magic Portals Sprite" && material.sprite2D.lightMask != 0) out.push_back(material.sprite2D.lightMask);
        }
        return out;
    };
    const std::vector<std::uint8_t> before = masks();
    CHECK_MSG(std::count(before.begin(), before.end(), Lighting::kLiveLights) > 0,
              "unlit: its static walls take only the live lights");
    CHECK_EQ(Tagged(registry, "Magic Portals Torch Light"), 0);

    const glm::dvec2 torchPx = layer.SimLevel()->torch.lights[0].atPx;
    tap(layer, registry, screenOf(registry, torchPx));
    for (int tick = 0; tick < 240 && !layer.SimLevel()->torch.lights[0].lit; ++tick) {
        tickWith(layer, registry, kRest, {}, {});
    }
    CHECK_MSG(layer.SimLevel()->torch.lights[0].lit, "a shot at the torch lit it: " + lastFailure(layer));
    if (!layer.SimLevel()->torch.lights[0].lit) return;
    tickWith(layer, registry, kRest, {}, {});

    CHECK_EQ(Tagged(registry, "Magic Portals Torch Light"), 1);
    const entt::entity lamp = FirstTagged(registry, "Magic Portals Torch Light");
    if (lamp != entt::null) {
        const Light2DComponent& light = registry.get<Light2DComponent>(lamp);
        const glm::vec3 colour = light.color * light.intensity;
        CHECK_MSG(light.layers == Lighting::kStaticLights && light.enabled, "light_from_projectile.ent's light is static");
        CHECK_MSG(::test::nearly(light.range, ToMetres(512.0), 1e-6f), "range 512 units");
        CHECK_MSG(colour.r > 0.0f && ::test::nearly(colour.g / colour.r, 0.5f, 1e-5f) &&
                      ::test::nearly(colour.b / colour.r, 0.2f, 1e-5f),
                  "(1, 0.5, 0.2) x the level's intensity: " + ShowV(colour));
        const glm::vec3 at = registry.get<TransformComponent>(lamp).position;
        const glm::vec3 want = MagicPortals::Units::ToWorld(torchPx.x, torchPx.y - 12.0);
        CHECK_MSG(std::fabs(at.x - want.x) < 1e-5f && std::fabs(at.y - want.y) < 1e-5f, "12 above the torch");
    }
    const std::vector<std::uint8_t> after = masks();
    CHECK_MSG(!after.empty() && std::all_of(after.begin(), after.end(), [](std::uint8_t mask) {
                  return mask == (Lighting::kLiveLights | Lighting::kStaticLights);
              }),
              "lit: every sprite that applies light takes every light");
    CHECK_MSG(OverlaysOf(registry).empty(), "and nothing draws a file lightmap");
    CHECK_MSG(layer.AmbientNow() == glm::dvec3(0.1, 0.1, 0.25), "at lighting.json's lit-torch ambient");
    layer.OnDetach(registry);
}

void AShotCarriesItsOwnLight() {
    namespace Lighting = MagicPortals::Lighting;
    using MagicPortals::Units::ToMetres;
    const Lighting::Rules rules = LightingRules();
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level1");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty() && layer.LightingError().empty(),
              layer.LoadError() + layer.ArtError() + layer.LightingError());
    if (layer.SimLevel() == nullptr || !layer.ArtError().empty() || !layer.LightingError().empty()) return;
    CloseTheLevelStartPopup(layer, registry);
    waitForFirstTap(layer, registry);
    CHECK_EQ(LightsOf(registry).size(), std::size_t{2});
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Light"), 0);

    tap(layer, registry, screenOf(registry, playerPx(registry, layer) + glm::dvec2(0.0, -48.0)));
    CHECK_MSG(layer.SimLevel()->portals.flight.has_value(), "the tap fired: " + lastFailure(layer));
    if (!layer.SimLevel()->portals.flight) return;
    CHECK_EQ(LightsOf(registry).size(), std::size_t{3});
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Light"), 1);
    const entt::entity shot = FirstTagged(registry, "Magic Portals Shot Light");
    if (shot == entt::null) return;
    // projectile.ent's: not static, so the live layer, which static walls take;
    // range 70; (0.6, 0.6, 1) at level1's intensity 3, whole (no particle system);
    // 12 below the shot's depth.
    const auto followsTheShot = [&]() {
        const glm::vec3 at = registry.get<TransformComponent>(shot).position;
        const glm::vec3 want = MagicPortals::Units::ToWorld(layer.SimLevel()->portals.flight->atPx.x,
                                                            layer.SimLevel()->portals.flight->atPx.y);
        return std::fabs(at.x - want.x) < 1e-5f && std::fabs(at.y - want.y) < 1e-5f;
    };
    {
        const Light2DComponent& light = registry.get<Light2DComponent>(shot);
        CHECK_MSG(light.layers == Lighting::kLiveLights && light.enabled, "the shot's light is live");
        CHECK_MSG(NearV(light.color * light.intensity, glm::vec3(1.8f, 1.8f, 3.0f)),
                  "(0.6, 0.6, 1) x 3: " + ShowV(light.color * light.intensity));
        CHECK_MSG(::test::nearly(light.range, ToMetres(70.0), 1e-6f), "range 70 units");
        CHECK_MSG(::test::nearly(light.height, ToMetres(-12.0), 1e-6f), "12 below the shot's depth of 0");
        CHECK_MSG(followsTheShot(), "where the shot is");
    }
    const glm::dvec2 from = layer.SimLevel()->portals.flight->atPx;
    tickWith(layer, registry, kRest, {}, {});
    if (layer.SimLevel()->portals.flight) {
        CHECK_MSG(layer.SimLevel()->portals.flight->atPx != from, "the shot moved");
        CHECK_MSG(followsTheShot(), "and its light with it");
    }
    if (OriginalArtIsThere("AShotCarriesItsOwnLight (the halo)")) {
        CHECK_EQ(Tagged(registry, "Magic Portals Shot Halo"), 1);
        const entt::entity halo = FirstTagged(registry, "Magic Portals Shot Halo");
        if (halo != entt::null) {
            const MaterialComponent& material = registry.get<MaterialComponent>(halo);
            const glm::vec3 want = glm::vec3(0.6f, 0.6f, 1.0f) * 0.65f * static_cast<float>(rules.haloBrightnessScale);
            CHECK_MSG(EndsWith(material.albedoTexturePath, "/entities/halo.bmp") &&
                          material.blend == MaterialComponent::BlendMode::Additive && !material.sprite2D.enabled &&
                          NearV(glm::vec3(material.albedoColor), want),
                      "halo.bmp, added, (0.6, 0.6, 1) x 0.65 x the scale: " + ShowV(glm::vec3(material.albedoColor)));
            const glm::vec3 scale = registry.get<TransformComponent>(halo).scale;
            CHECK_MSG(std::fabs(scale.x - 1.0f) < 1e-5f && std::fabs(scale.y - 1.0f) < 1e-5f, "50 units: " + ShowV(scale));
        }
        // The shot's own picture takes no light: projectile.ent applies none.
        const entt::entity picture = FirstTagged(registry, "Magic Portals Shot Sprite");
        CHECK_MSG(picture != entt::null && registry.get<MaterialComponent>(picture).sprite2D.lightMask == 0,
                  "the shot's picture takes no light");
    }

    // A static wall that applies light takes the live layer the shot is on.
    int walls = 0;
    for (auto [entity, t, material] : registry.view<TagComponent, MaterialComponent>().each()) {
        (void)entity;
        if (t.tag != "Magic Portals Sprite" || !material.sprite2D.enabled) continue;
        if ((material.sprite2D.lightMask & Lighting::kLiveLights) != 0 && !material.overlayTexturePath.empty()) ++walls;
    }
    CHECK_MSG(walls > 0, "level1's lightmapped walls take the live layer");

    landShot(layer, registry);
    CHECK_EQ(LightsOf(registry).size(), std::size_t{2});
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Light"), 0);
    CHECK_EQ(Tagged(registry, "Magic Portals Shot Halo"), 0);
    layer.OnDetach(registry);
    CHECK_MSG(LightsOf(registry).empty() && Tagged(registry, "Magic Portals Halo") == 0, "detached, none left");
}

void NothingBlinksWhileWalking() {
    NoSpriteBlinksWhileWalking("level1"); // 1-2, where the owner saw one go
    NoSpriteBlinksWhileWalking("level2"); // 1-3, where several do
}

void NothingOnScreenIsCulledWhileWalking() {
    NoSpriteOnScreenIsCulled("level1"); // 1-2, where the owner saw one go
    NoSpriteOnScreenIsCulled("level2"); // 1-3, where several do
}

// ---- how a level ends -------------------------------------------------------------
//
// sim/LevelEnd is pinned number by number in test_mp_levelend. What is held here
// is the layer's side: the level GOES ON RUNNING under both screens (spec D7);
// the screen comes up exactly the beat after the door or the death; restart and
// pause are cut at the door and dismissed at a death (D8); the pads decay a tick
// at a time from the byte they had; and a tap on a screen's button is taken
// where that button is.

// Holds right through level0 until the door, and returns the tick it was reached
// on, or -1.
int WalkLevel0IntoItsDoor(MagicPortalsLayer& layer, entt::registry& registry) {
    for (int tick = 1; tick <= 600; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
        if (layer.Finishing()) return tick;
    }
    return -1;
}

void TheDoorCutsTheHudAndTheLevelRunsOnUnderTheMedal() {
    if (!OriginalArtIsThere("TheDoorCutsTheHudAndTheLevelRunsOnUnderTheMedal")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level0");
    layer.OnAttach(registry);
    if (!layer.LoadError().empty()) {
        CHECK_MSG(false, layer.LoadError());
        return;
    }
    const Hud::Rules& hud = layer.HudRules();
    const MagicPortals::LevelEnd::Rules& rules = layer.LevelEndRules();
    const int door = WalkLevel0IntoItsDoor(layer, registry);
    CHECK_MSG(door > 0, "holding right reaches level0's door");
    if (door < 0) return;

    // A-F6: on the door tick restart and pause are gone, the player with them,
    // and the pads are drawn at the byte the pulse left them at.
    Hud::Rect rect;
    CHECK_MSG(!layer.ControlRect(MagicPortalsLayer::Control::Reset, rect) &&
                  !layer.ControlRect(MagicPortalsLayer::Control::Menu, rect),
              "restart and pause are cut on the door tick");
    const int start = layer.EndPadAlphaByte();
    CHECK_MSG(start >= hud.alphaByte && start <= hud.alphaByte + hud.pads.tutorialVariationByte,
              "the pads decay from their pulse: " + std::to_string(start));
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int pad = IndexOfImage(frame, hud.pads.leftSprite);
        CHECK_MSG(IndexOfImage(frame, hud.restart.sprite) < 0 && IndexOfImage(frame, hud.pause.sprite) < 0 && pad >= 0 &&
                      NearD(frame[static_cast<std::size_t>(pad < 0 ? 0 : pad)].color.a, start / 255.0, 1e-6),
                  "the frame has the pads at that byte and no restart or pause");
    }

    // A-F7 and A-F8: a tick at a time, uint(a * 0.98), and the screen current
    // exactly 1400 ms of game time after the door.
    int screenAt = -1;
    int padsGoneAt = -1;
    bool decayed = true;
    for (int tick = 1; tick <= 200; ++tick) {
        tickWith(layer, registry, kRest, {MagicPortalsLayer::kRight}, {});
        decayed = decayed && layer.EndPadAlphaByte() == MagicPortals::LevelEnd::PadDecayByte(rules, start, tick);
        if (screenAt < 0 && layer.MenuScreen() == MagicPortalsLayer::Screen::Finished) screenAt = tick;
        if (padsGoneAt < 0 && !layer.ControlRect(MagicPortalsLayer::Control::Left, rect)) padsGoneAt = tick;
        if (tick == 84) CHECK_MSG(NearD(layer.EndScreenClockMs(), 0.0), "the screen's clock starts at its t0");
    }
    std::printf("  level0: door on tick %d, pads from %d gone %d ticks on, medal %d ticks on\n", door, start,
                padsGoneAt, screenAt);
    CHECK_MSG(decayed, "the pads' byte is uint(a * 0.98) every tick from the door");
    CHECK_EQ(screenAt, 84);
    int zeroAt = 0;
    while (MagicPortals::LevelEnd::PadDecayByte(rules, start, zeroAt) > 0) ++zeroAt;
    CHECK_MSG(padsGoneAt == zeroAt, "the pads go on the tick their byte reaches 0: " + std::to_string(zeroAt));

    // D7: THE LEVEL RUNS ON. Its age keeps counting under the screen, as game
    // time does in the original, and the screen's own clock with it.
    const double age = layer.LevelAgeMs();
    const double clock = layer.EndScreenClockMs();
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(NearD(layer.LevelAgeMs() - age, 1000.0, 1e-3) && NearD(layer.EndScreenClockMs() - clock, 1000.0, 1e-3),
              "a second under the medal is a second of the level's age: " +
                  std::to_string(layer.LevelAgeMs() - age));
    CHECK(layer.MenuScreen() == MagicPortalsLayer::Screen::Finished && layer.SimLevel() != nullptr);

    // Escape does nothing here, and a tap beside the buttons neither.
    press(layer, registry, MagicPortalsLayer::kBack);
    tap(layer, registry, ScreenOfView(layer, glm::dvec2(0.1, 0.1) * layer.ViewPx()));
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::Finished && !layer.Paused(),
              "no pause over a finished level, and a tap on nothing is nothing");
    // A tap on restart, where it is drawn, plays the level again.
    const std::vector<MagicPortals::LevelEnd::Piece> pieces = MagicPortals::LevelEnd::Finished(
        rules, layer.EndPlay(), layer.PortalsCounted(), layer.CrystalsCounted(), layer.ViewPx(), 5000.0);
    for (const MagicPortals::LevelEnd::Piece& piece : pieces) {
        if (piece.element != MagicPortals::LevelEnd::Element::Button ||
            piece.button != MagicPortals::LevelEnd::Button::Restart) {
            continue;
        }
        tap(layer, registry, ScreenOfView(layer, piece.rect.Centre()));
    }
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::None && IsAt(layer, "level0") &&
                  !layer.Finishing() && layer.LevelAgeMs() == 0.0,
              "the medal's restart, tapped, starts level0 again");
    layer.OnDetach(registry);
}

void ADeathDismissesTheHudAndTheLostScreenComesIn() {
    if (!OriginalArtIsThere("ADeathDismissesTheHudAndTheLostScreenComesIn")) return;
    entt::registry registry;
    publishViewport(registry);
    MagicPortalsLayer layer(TestPaths(), "level5");
    layer.OnAttach(registry);
    CHECK_MSG(layer.SimLevel() != nullptr, layer.LoadError());
    if (layer.SimLevel() == nullptr) return;
    const Hud::Rules& hud = layer.HudRules();
    const MagicPortals::LevelEnd::Rules& rules = layer.LevelEndRules();
    // Past the pulse, so the pads decay from their 120 as 2-10's did.
    for (int tick = 0; tick < 300; ++tick) tickWith(layer, registry, kRest, {}, {});
    const MagicPortals::Hazards::Hazard* hazard = layer.SimLevel()->hazards.FindHazard("death_area_ent_800");
    CHECK(hazard != nullptr);
    if (hazard == nullptr) return;
    auto& transform = registry.get<TransformComponent>(layer.SimLevel()->player);
    transform.position = glm::vec3(hazard->box.centre, transform.position.z);
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.Dying() && layer.EndedMs() == 0.0, "the death tick");
    CHECK_EQ(layer.EndPadAlphaByte(), 120);

    const glm::dvec2 view = layer.ViewPx();
    const Hud::Rect home = Hud::Place(hud.pause, view);
    Hud::Rect rect;
    // A-G4: dismissed, not cut - whole on the death tick, half gone and moving
    // out at 350 ms, and gone before 700.
    CHECK_MSG(layer.ControlRect(MagicPortalsLayer::Control::Menu, rect) && SameRect(rect, home),
              "the pause control is still home on the death tick");
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int pause = IndexOfImage(frame, hud.pause.sprite);
        CHECK_MSG(pause >= 0 && NearD(frame[static_cast<std::size_t>(pause < 0 ? 0 : pause)].color.a, 120.0 / 255.0, 1e-6),
                  "at its own 120");
    }
    for (int tick = 0; tick < 21; ++tick) tickWith(layer, registry, kRest, {}, {});
    {
        const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
        const int pause = IndexOfImage(frame, hud.pause.sprite);
        const double want = 120.0 / 255.0 * (1.0 - std::sin(3.14159265358979 / 4.0));
        CHECK_MSG(pause >= 0 && NearD(frame[static_cast<std::size_t>(pause < 0 ? 0 : pause)].color.a, want, 0.01),
                  "350 ms in the pause control is at " +
                      std::to_string(pause < 0 ? -1.0 : frame[static_cast<std::size_t>(pause)].color.a));
        CHECK_MSG(layer.ControlRect(MagicPortalsLayer::Control::Menu, rect) && rect.min.x > home.min.x &&
                      rect.min.y < home.min.y,
                  "and moving out, right and up");
        CHECK_EQ(layer.EndPadAlphaByte(), MagicPortals::LevelEnd::PadDecayByte(rules, 120, 21));
    }
    for (int tick = 0; tick < 21; ++tick) tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(!layer.ControlRect(MagicPortalsLayer::Control::Menu, rect) &&
                  !layer.ControlRect(MagicPortalsLayer::Control::Reset, rect),
              "gone 700 ms after the death");

    // A-G6: the lost screen 1400 ms after the death, to the tick.
    int screenAt = 42;
    while (screenAt < 200 && layer.MenuScreen() != MagicPortalsLayer::Screen::Dead) {
        tickWith(layer, registry, kRest, {}, {});
        ++screenAt;
    }
    CHECK_EQ(screenAt, 84);
    CHECK_MSG(!layer.ControlRect(MagicPortalsLayer::Control::Left, rect), "the pads from 120 are gone by then (81)");
    for (int tick = 0; tick < 60; ++tick) tickWith(layer, registry, kRest, {}, {});
    const std::vector<ScreenOverlay::Quad> frame = HudFrame(registry, layer);
    const int veil = IndexOfImage(frame, "fade_edge.png");
    const int title = IndexOfImage(frame, "game_over.png");
    const int restart = IndexOfImage(frame, "button_restart.png");
    const int list = IndexOfImage(frame, "list_button.png");
    CHECK_MSG(veil >= 0 && title > veil && restart > title && list > restart && CountImage(frame, "fade_edge.png") == 3,
              "the veil, game over, restart and list, in that order");
    if (veil >= 0) {
        CHECK_MSG(::test::nearly(frame[static_cast<std::size_t>(veil)].color.a, 180.0f / 255.0f) &&
                      ::test::nearly(frame[static_cast<std::size_t>(veil + 2)].max.x, 0.9f),
                  "at 180, 0.9 of the view wide");
    }
    CHECK(IndexOfImage(frame, "button_right.png") < 0);
    const double age = layer.LevelAgeMs();
    tickWith(layer, registry, kRest, {}, {});
    CHECK_MSG(layer.LevelAgeMs() > age, "and the level runs on under it (D7)");

    // A tap on L3 where it is drawn: level5 again, alive.
    const std::vector<MagicPortals::LevelEnd::Piece> pieces = MagicPortals::LevelEnd::Lost(rules, view, 5000.0);
    for (const MagicPortals::LevelEnd::Piece& piece : pieces) {
        if (piece.element == MagicPortals::LevelEnd::Element::Button &&
            piece.button == MagicPortals::LevelEnd::Button::Restart) {
            tap(layer, registry, ScreenOfView(layer, piece.rect.Centre()));
        }
    }
    CHECK_MSG(layer.MenuScreen() == MagicPortalsLayer::Screen::None && IsAt(layer, "level5") && !layer.Dying(),
              "the lost screen's restart, tapped, plays level5 again");
    layer.OnDetach(registry);
}

// MainCharacter on a no_gravity level (art.json character.no_gravity_sprite and
// no_gravity_motion): dark_mage_in_space.png, lit through dark_mage.ent's own
// normal map, standing on the column its FrameTimer turns every 280 ms - the one
// the update before left - with its pivot hovering 1.2 screen pixels on a
// cosine. level0c sets space_bg and not no_gravity: the normal sheet, on column
// 0, unhovered.
void AWeightlessPlayerWearsTheSuit() {
    if (!OriginalArtIsThere("AWeightlessPlayerWearsTheSuit")) return;
    MagicPortals::Art::Rules rules;
    std::string error;
    CHECK_MSG(MagicPortals::Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", rules, error),
              error);
    const MagicPortals::Art::Character& mage = rules.character;
    const double tickMs = static_cast<double>(MagicPortalsLayer::kTick) * 1000.0;
    for (const std::string name : {"level1c", "level0c"}) {
        const bool weightless = name == "level1c";
        entt::registry registry;
        publishViewport(registry);
        MagicPortalsLayer layer(TestPaths(), name);
        layer.OnAttach(registry);
        CHECK_MSG(layer.SimLevel() != nullptr && layer.ArtError().empty(), layer.LoadError() + layer.ArtError());
        if (layer.SimLevel() == nullptr) continue;
        CHECK_MSG(layer.SimLevel()->noGravity == weightless, name + ": no_gravity as the level sets it");
        const entt::entity mageQuad = FirstTagged(registry, "Magic Portals Player Sprite");
        CHECK(mageQuad != entt::null);
        if (mageQuad == entt::null) continue;
        {
            const MaterialComponent& material = registry.get<MaterialComponent>(mageQuad);
            const char* sheet =
                weightless ? "/entities/hd/dark_mage_in_space.png" : "/entities/hd/magic_portals_hd.png";
            CHECK_MSG(EndsWith(material.albedoTexturePath, sheet) &&
                          EndsWith(material.normalTexturePath, "/entities/normalmaps/normalmap_77.png"),
                      name + ": " + material.albedoTexturePath + " through " + material.normalTexturePath);
            const glm::vec3 scale = registry.get<TransformComponent>(mageQuad).scale;
            const double perMetre = MagicPortals::Units::kPixelsPerMetre;
            CHECK_MSG(std::fabs(scale.x * perMetre - 40.0) < 1e-3 && std::fabs(scale.y * perMetre - 56.0) < 1e-3,
                      name + ": a 40 x 56 frame of its sheet");
        }
        MagicPortals::Art::NoGravityPlayer mirror;
        std::array<int, 4> firstTick{-1, -1, -1, -1};
        int offColumn = 0;
        double worst = 0.0;
        double highest = -1.0;
        double lowest = 1.0;
        for (int tick = 1; tick <= 140; ++tick) {
            tickWith(layer, registry, kRest, {}, {});
            if (!registry.valid(mageQuad)) break;
            int column = 0;
            double hover = 0.0;
            if (weightless) {
                column = MagicPortals::Art::UpdateNoGravity(mage, mirror, false, tickMs);
                hover = mage.noGravity.HoverUnits(mirror.angle, layer.ViewPx().y);
            }
            const SpriteAnimationComponent& animation = registry.get<SpriteAnimationComponent>(mageQuad);
            const int shown = static_cast<int>(animation.firstFrame) - mage.rightRow * mage.columns;
            if (shown != column || animation.frameCount != 1 || animation.playing) ++offColumn;
            if (shown >= 0 && shown < 4 && firstTick[static_cast<std::size_t>(shown)] < 0) {
                firstTick[static_cast<std::size_t>(shown)] = tick;
            }
            const glm::dvec2 drawn = MagicPortals::Units::ToPixels(registry.get<TransformComponent>(mageQuad).position);
            const glm::dvec2 body = playerPx(registry, layer);
            worst = std::max(worst, std::fabs(drawn.x - body.x) + std::fabs(drawn.y - (body.y - 2.0 - hover)));
            highest = std::max(highest, body.y - 2.0 - drawn.y);
            lowest = std::min(lowest, body.y - 2.0 - drawn.y);
        }
        CHECK_MSG(offColumn == 0, name + ": " + std::to_string(offColumn) + " tick(s) off the timer's column");
        CHECK_MSG(worst < 2e-3, name + ": the pivot, hovered, on the body: worst " + std::to_string(worst));
        if (weightless) {
            CHECK_MSG(firstTick == (std::array<int, 4>{1, 19, 36, 53}),
                      name + ": columns 0, 1, 2, 3 first drawn on ticks " + std::to_string(firstTick[0]) + ", " +
                          std::to_string(firstTick[1]) + ", " + std::to_string(firstTick[2]) + ", " +
                          std::to_string(firstTick[3]));
            const double amplitude = 1.2 * 256.0 / 720.0;
            CHECK_MSG(std::fabs(highest - amplitude) < 2e-3 && std::fabs(lowest + amplitude) < 2e-3,
                      name + ": a hover of 0.4267 units each way over 2.33 s: " + std::to_string(highest) + " / " +
                          std::to_string(lowest));
            std::printf("  %s: dark_mage_in_space.png, columns first drawn on ticks 1, %d, %d, %d; hover %+.4f / %+.4f units\n",
                        name.c_str(), firstTick[1], firstTick[2], firstTick[3], highest, lowest);
        } else {
            CHECK_MSG(firstTick == (std::array<int, 4>{1, -1, -1, -1}) && highest < 1e-3 && lowest > -1e-3,
                      name + ": standing on column 0, unhovered");
        }
        layer.OnDetach(registry);
    }
}

void runTests() {
    TheLayerPlaysLevel30();
    Level31DrawsTheBeholder();
    ATapLandsWhereItPoints();
    Level8FromTheSpawnWithTapsAndWalking();
    LevelsFollowInOrderAndRetryIsInstant();
    NSkipsToTheNextLevel();
    DeathIsABeatAndThenTheLostScreen();
    AFallOutOfTheLevelIsADeath();
    TheLevelsArtIsDrawn();
    AStaticPortalGlows();
    AnAntiportalIsDrawnAtHalfItsNodesScale();
    AnAntiportalSpelledWithEntIsDrawnTheSame();
    AGravityWellWearsAGreenRing();
    AnAntiportalBlinksRedAndTurnsAndAPauseHoldsBoth();
    WithoutTheArtTheLevelIsBoxes();
    APortalAndAShotAreTheOriginals();
    WithoutTheOriginalThePortalIsABox();
    ThePlayerIsTheDarkMage();
    TheWalkKeepsItsColumnAndItsTime();
    APlayerWalkedIntoAWallPushesIt();
    APlayerPushesATriggerItHasNotEntered();
    AWeightlessPlayerNeverPushes();
    AWeightlessPlayerWearsTheSuit();
    ABrokenWallTakesItsBoxWithIt();
    ARetryTakesTheThrownStonesAway();
    TheChapterEnds();
    AStartThatIsNoLevelSaysSo();
    TheMenuWalksToALevel();
    TheGridPagesThroughAWorld();
    NamingALevelSkipsTheMenu();
    EscapePausesALevelAndResumesIt();
    TheMainMenuActsOnTheRelease();
    TheLoadingScreenLeadsToTheMenu();
    TheMenuStatesOpenUnderABlack();
    TheBackKeyOnTheMainMenuQuits();
    TheMainMenusSwitchesAndCornerButtons();
    ATileRefusesATouchThatTravelled();
    ALevelsEntitiesEmit();
    ParticlesGoWithTheirLevel();
    TheTorchAndThePortalsEmitAsTheOriginalDoes();
    AnEmitterFollowsItsOwner();
    AParticleOnANonSquareCellIsDrawnSquare();
    ATurnedOwnerTurnsItsParticles();
    TheDragonsFireIsBuiltFromItsJpeg();
    FinishingALevelShowsTheMedal();
    TheMedalScreenIsTheOriginals();
    TheGridShowsTheMedalsEarned();
    ALevelLatchesTheSoundsItEarns();
    ACarrancasFireballIsSeenAndHeard();
    NothingOnScreenIsCulledWhileWalking();
    NothingBlinksWhileWalking();
    TheHudIsTheOriginals();
    TheHudStaysOnTheViewAsTheCameraMoves();
    TheSkyStaysOnTheViewAsTheCameraMoves();
    AClearPortalsButtonComesWithAPortal();
    APlaqueForALevelWithAMedal();
    TheTutorialRingsAndAWeightlessLevelHasNoPads();
    TheNoPortalSignIsPinnedToTheCorner();
    ThePauseStopsTheLevelUnderIt();
    ThePauseResumesWhereTheTapLeftIt();
    TheTutorialPopupStopsALevelAsItLoads();
    AHelpBlockOpensItsPopupAndTheLevelTakesUpWhereItStopped();
    ThePausesButtonsGoWhereTheOriginalsGo();
    SkipOnALevelAlreadyFinished();
    TheDoorCutsTheHudAndTheLevelRunsOnUnderTheMedal();
    ADeathDismissesTheHudAndTheLostScreenComesIn();
    TheSceneHoldsDisplayValues();
    EverySpriteIsDrawnAtItsAmbient();
    AShotIsDimmedAndADarkLevelIsDark();
    EveryTimedCrystalHasADialBehindIt();
    ATimedCrystalCountsDownOnItsDialAndNeverFades();
    ATakenCrystalsDialShrinksAway();
    ACrystalBobsAsItsScriptDoes();
    ALyingKeyBobsAndACarriedOneDoesNot();
    ABobsPhasesAreTheLevelsOwn();
    ATimedCrystalsDialBobsWithIt();
    AnArrowSwaysAlongItsAngleAtItsAlpha();
    AnArrowAndAHandSwayAlongTheirOwnAngles();
    ADashedCircleBobsAndTurns();
    LightmapsAreDrawnOverTheirSprites();
    TheTorchIsALightAndAHalo();
    AShotCarriesItsOwnLight();
    ALitTorchBakesTheLevelAtRunTime();
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
