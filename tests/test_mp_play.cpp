// The port's behaviours, on level30: the acceptance items of
// docs/planning/2026-09-10-magic-portals-port.md, one at a time as they land.
//
// Reads the converted level and the remake's own data from outside this
// repository (SUPERSONIC_MAGICPORTALS_LEVELS and _DATA), and skips, saying where
// it looked, when either is absent. What it pins exactly is what the files say -
// roles, strides, positions - and what it judges by threshold is physics.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/Game.hpp"
#include "sim/Goals.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Mover.hpp"
#include "sim/Player.hpp"
#include "sim/Portals.hpp"
#include "sim/Puzzle.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>

using namespace MagicPortals;
using namespace Supersonic;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// Read once: level30 and the remake's data, as Game::LoadData reads them for the
// layer. The short names are the suite's own.
Game::Data g_data;
const Tscn::Scene& g_level30 = g_data.scene;
const Roles::Table& g_roles = g_data.roles;
const Player::Tuning& g_tuning = g_data.tuning;
const Portals::Rules& g_portalRules = g_data.portals;

bool LoadInputs(std::string& error) {
    return Game::LoadData(kLevels + "/level30.tscn", kData,
                          std::filesystem::temp_directory_path() / "supersonic-test-mp-play", g_data, error);
}

LevelBuilder::Options PortOptions() {
    LevelBuilder::Options options;
    options.prismDirectory = g_data.prisms;
    options.roles = &g_roles;
    return options;
}

// The port's world: the remake's gravity, 980 px/s^2 (Units.hpp says why).
void UseRemakeGravity(entt::registry& registry) {
    Game::UseRemakeGravity(registry);
}

// level30's small crate, its size off its RectangleShape2D.
glm::dvec2 CrateSmallSizePx() {
    const Tscn::Node* shape = g_level30.FindNode("crate_small_ent_973/Body/Shape");
    const Tscn::Value* ref = shape != nullptr ? shape->Find("shape") : nullptr;
    const Tscn::Resource* rect = ref != nullptr ? g_level30.Embedded(ref->text) : nullptr;
    const Tscn::Value* size = rect != nullptr ? rect->Find("size") : nullptr;
    if (size == nullptr || size->kind != Tscn::Value::Kind::Vector2) return glm::dvec2(0.0);
    return glm::dvec2(size->numbers[0], size->numbers[1]);
}

// ---- The role table, on level30 ---------------------------------------------

void Level30HasTheRolesItsPuzzleNeeds() {
    // Counted with grep off level30.tscn against entity_roles.json, independently
    // of the reader: three buttons and the three door_lifts they open, five
    // crystals, the exit, the spawn, the bounds. No static portals and no
    // properties entity, so the portal budget is portals.json's default.
    std::map<std::string, int> count;
    for (const Tscn::Node& node : g_level30.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(g_roles, node);
        if (!role.empty()) ++count[role];
    }
    CHECK_EQ(count[Roles::kSwitch], 3);
    CHECK_EQ(count[Roles::kSwitchedDoor], 3);
    CHECK_EQ(count[Roles::kCollectible], 5);
    CHECK_EQ(count[Roles::kExitDoor], 1);
    CHECK_EQ(count[Roles::kPlayerSpawn], 1);
    CHECK_EQ(count[Roles::kLevelBounds], 1);
    CHECK_EQ(count[Roles::kStaticPortal], 0);
    CHECK_EQ(count[Roles::kLevelProperties], 0);
}

// ---- Movers -----------------------------------------------------------------

void TheMoversAreKinematic() {
    // The remake turns every moving role into an AnimatableBody2D so it carries
    // riders (level_runtime.gd:24-27, :166-181); the port makes it a kinematic
    // body for the same reason. level30's movers are its three door_lifts, which
    // leaves 12 of the spike's 15 statics static.
    entt::registry registry;
    LevelBuilder::Built built;
    std::string error;
    CHECK_MSG(LevelBuilder::Build(g_level30, registry, PortOptions(), built, error), error);
    CHECK_EQ(built.movers, 3);
    CHECK_EQ(built.statics, 12);
    for (const char* name : {"door_lift_976", "door_lift_977", "door_lift_978"}) {
        const auto found = built.entities.find(name);
        const bool present = found != built.entities.end();
        const auto* rigid = present ? registry.try_get<RigidBodyComponent>(found->second) : nullptr;
        CHECK_MSG(rigid != nullptr && rigid->isKinematic, std::string(name) + " is a kinematic body");
        CHECK_MSG(present && !registry.all_of<PhysicsMaterialComponent>(found->second),
                  std::string(name) + " carries its material on its rigid body, not as a static");
    }
}

void DoorsTakeTheirStrideFromTheLevel() {
    // Off level30.tscn: door_lift_976 at (592, 192) with stride "1000", 977 at
    // (632, 74) and 978 at (600, 74) with stride "3000".
    const struct {
        const char* name;
        double x;
        double y;
        float seconds;
    } doors[] = {
        {"door_lift_976", 592.0, 192.0, 1.0f},
        {"door_lift_977", 632.0, 74.0, 3.0f},
        {"door_lift_978", 600.0, 74.0, 3.0f},
    };
    for (const auto& expected : doors) {
        Mover::Door door;
        std::string error;
        const Tscn::Node* node = g_level30.FindNode(expected.name);
        CHECK_MSG(node != nullptr && Mover::DoorFromNode(*node, door, error), std::string(expected.name) + ": " + error);
        CHECK_NEAR(door.durationS, expected.seconds);
        CHECK_MSG(door.closed == Units::ToWorld(expected.x, expected.y) &&
                      door.open == Units::ToWorld(expected.x, expected.y - Mover::kDoorRisePx),
                  std::string(expected.name) + " closes where it stands and opens 126 px above");
    }
}

void ACrateRidesADoorUpAndDown() {
    // The mover helper's reason for being: a door moved by MoveKinematic writes
    // its velocity, so what stands on it is carried (F1). door_lift_976 on its
    // own - in the level it rises into the space under 977 and 978, which would
    // crush its rider and measure the level instead of the door - with level30's
    // small crate on top.
    entt::registry registry;
    UseRemakeGravity(registry);
    LevelBuilder::Built built;
    std::string error;
    const entt::entity door =
        LevelBuilder::BuildEntity(g_level30, "door_lift_976", registry, PortOptions(), built, error);
    CHECK_MSG(door != entt::null, error);
    if (door == entt::null) return;
    Mover::Door motion;
    CHECK_MSG(Mover::DoorFromNode(*g_level30.FindNode("door_lift_976"), motion, error), error);

    const auto& box = registry.get<BoxColliderComponent>(door);
    const auto doorTop = [&] {
        return registry.get<TransformComponent>(door).position.y + box.center.y + box.size.y * 0.5f;
    };

    const glm::dvec2 sizePx = CrateSmallSizePx();
    CHECK_MSG(sizePx.x > 0.0 && sizePx.y > 0.0, "level30's crate_small has a size");
    const float half = static_cast<float>(Units::ToMetres(sizePx.y) * 0.5);
    const entt::entity crate = registry.create();
    registry.emplace<TransformComponent>(crate).position =
        glm::vec3(registry.get<TransformComponent>(door).position.x + box.center.x, doorTop() + half, 0.0f);
    auto& collider = registry.emplace<BoxColliderComponent>(crate);
    collider.size = glm::vec3(Units::ToMetres(sizePx.x), Units::ToMetres(sizePx.y),
                              static_cast<float>(LevelBuilder::kBodyDepthMetres));
    auto& rigid = registry.emplace<RigidBodyComponent>(crate);
    rigid.friction = LevelBuilder::kBodyFriction;
    rigid.restitution = LevelBuilder::kBodyRestitution;
    rigid.lockPosition = LevelBuilder::kPlaneLockPosition;
    rigid.lockRotation = LevelBuilder::kPlaneLockRotation;
    rigid.allowSleep = false;

    const auto crateY = [&] { return registry.get<TransformComponent>(crate).position.y; };
    const auto gapPx = [&] { return static_cast<double>(crateY() - half - doorTop()) * Units::kPixelsPerMetre; };
    // The mover first, then the step - the order the app runs a layer's tick
    // against the next physics step.
    const auto step = [&] {
        motion.Tick(registry, door, kStep);
        PhysicsSystem::Update(registry, kStep);
    };

    for (int i = 0; i < 30; ++i) step(); // settle, closed
    const float startY = crateY();

    // Up. The door starts and stops dead - behaviours.gd lerps it at one speed -
    // so while it moves the crate has to ride it, and when it stops the crate,
    // still rising at the door's speed, flies on by v^2/2g and comes back down
    // onto it. That flight is the proof it was carried: a crate the door merely
    // shoved along would have no speed of its own to fly with.
    motion.opening = true;
    double worstGap = 0.0;
    for (int i = 0; i < 600 && motion.progress < 1.0f; ++i) {
        step();
        worstGap = std::max(worstGap, std::fabs(gapPx()));
    }
    CHECK_MSG(motion.progress == 1.0f, "the door finished opening");
    CHECK_MSG(worstGap < 2.0, "the crate rode the door up: worst gap " + std::to_string(worstGap) + " px");

    const double speedPx = Mover::kDoorRisePx / motion.durationS;
    const double flightPx = speedPx * speedPx / (2.0 * Units::kRemakeWorldGravityPx);
    double highestGap = 0.0;
    for (int i = 0; i < 90; ++i) {
        step();
        highestGap = std::max(highestGap, gapPx());
    }
    CHECK_MSG(std::fabs(highestGap - flightPx) < 3.0,
              "and flew on when it stopped: " + std::to_string(highestGap) + " px, where v^2/2g says " +
                  std::to_string(flightPx));
    const double risePx = static_cast<double>(crateY() - startY) * Units::kPixelsPerMetre;
    CHECK_MSG(std::fabs(risePx - Mover::kDoorRisePx) < 2.0 && std::fabs(gapPx()) < 2.0,
              "then came to rest on it: " + std::to_string(risePx) + " px up of 126, gap " +
                  std::to_string(gapPx()) + " px");

    motion.opening = false;
    for (int i = 0; i < 100; ++i) step();
    const double backPx = static_cast<double>(crateY() - startY) * Units::kPixelsPerMetre;
    CHECK_MSG(motion.progress == 0.0f, "and closed again");
    CHECK_MSG(std::fabs(backPx) < 2.0 && std::fabs(gapPx()) < 2.0,
              "bringing the crate back down onto it: " + std::to_string(backPx) + " px from where it started, gap " +
                  std::to_string(gapPx()) + " px");
}

// ---- The player -------------------------------------------------------------

// level30 as the port plays it, started and ticked by Game: the same code the
// layer runs.
struct Play : Game::Level {
    entt::registry registry;
};

bool StartLevel30(Play& play, bool withStatics, std::string& error) {
    return Game::Start(g_data, play.registry, play, error, withStatics);
}

// One tick as the port runs it (Game.hpp says in what order).
void Tick(Play& play, float direction) {
    Game::Tick(g_data, play.registry, play, direction, kStep);
}

glm::dvec2 PlayerPx(Play& play) {
    return Units::ToPixels(play.registry.get<TransformComponent>(play.player).position);
}

// The top of one of level30's polygon platforms, in the remake's pixels.
double PlatformTopPx(const std::string& entity) {
    const Tscn::Node* node = g_level30.FindNode(entity);
    const Tscn::Node* shape = g_level30.FindNode(entity + "/Body/Shape");
    const Tscn::Value* polygon = shape != nullptr ? shape->Find("polygon") : nullptr;
    const Tscn::Value* at = node != nullptr ? node->Find("position") : nullptr;
    if (polygon == nullptr || at == nullptr || at->kind != Tscn::Value::Kind::Vector2) return 0.0;
    double least = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < polygon->numbers.size(); i += 2) least = std::min(least, polygon->numbers[i]);
    return at->numbers[1] + least;
}

// Landed: something solid under the feet and the fall stopped, within 45 ticks -
// verify_gameplay's window. -1 when it never did.
int LandingTick(Play& play) {
    for (int tick = 1; tick <= 45; ++tick) {
        Tick(play, 0.0f);
        const bool grounded = Player::Grounded(play.registry, play.player, g_tuning);
        const float fall = play.registry.get<RigidBodyComponent>(play.player).velocity.y;
        if (grounded && std::fabs(fall) < 0.05f) return tick;
    }
    return -1;
}

void ThePlayerLandsOnLevel30() {
    // The remake's own check (verify_gameplay.gd:259): the player comes to rest
    // on geometry. It spawns with its feet a pixel into platform_ent_895, as the
    // level places it, and settles on the top - and with every static taken
    // away it must not, which is what makes the first half mean something.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    const int landed = LandingTick(play);
    const double restY = PlayerPx(play).y;
    const double wantY = PlatformTopPx("platform_ent_895") - g_tuning.heightPx * 0.5;
    CHECK_MSG(landed > 0, "the player landed within 45 ticks");
    CHECK_MSG(std::fabs(restY - wantY) < 1.0,
              "on platform_ent_895's top: at " + std::to_string(restY) + " px, want " + std::to_string(wantY));

    Play bare;
    CHECK_MSG(StartLevel30(bare, false, error), error);
    if (bare.player == entt::null) return;
    CHECK_MSG(LandingTick(bare) < 0, "and with every static removed it lands on nothing");
}

// Past the seam and short of crate_969, whose 58 px face is at x 323 - which the
// capsule's reaches with its centre at 313.
constexpr double kSeamWalkEndPx = 300.0;

struct Walk {
    double reachedAtS = -1.0; // when x first reached kSeamWalkEndPx
    double slowestPx = 0.0;   // the slowest tick after it reached walking speed, button held
    double endXPx = 0.0;
};

Walk WalkTheSeam(float friction) {
    Walk walk;
    Play play;
    std::string error;
    if (!StartLevel30(play, true, error)) {
        CHECK_MSG(false, error);
        return walk;
    }
    play.registry.get<RigidBodyComponent>(play.player).friction = friction;
    for (int i = 0; i < 30; ++i) Tick(play, 0.0f); // land and settle

    bool atSpeed = false;
    walk.slowestPx = std::numeric_limits<double>::infinity();
    for (int tick = 1; tick <= 120 && walk.reachedAtS < 0.0; ++tick) {
        Tick(play, 1.0f);
        const double vx = play.registry.get<RigidBodyComponent>(play.player).velocity.x * Units::kPixelsPerMetre;
        if (vx >= 0.95 * g_tuning.walkSpeedPx) atSpeed = true;
        if (atSpeed) walk.slowestPx = std::min(walk.slowestPx, vx);
        if (PlayerPx(play).x >= kSeamWalkEndPx) walk.reachedAtS = tick * static_cast<double>(kStep);
    }
    if (!atSpeed) walk.slowestPx = 0.0;
    walk.endXPx = PlayerPx(play).x;
    return walk;
}

void ThePlayerWalksAcrossTheSeam() {
    // From the spawn (x 182) right across x 256, where platform_ent_895 and
    // platform_ent_785 meet in a 3.2 px notch, to x 300. Two things: it gets
    // there, and no tick after it reaches walking speed drops near zero while
    // the button is held. A capsule that catches for a fifth of a second and pops
    // free gets there too.
    //
    // "Near zero" is under half walking speed. The notch's chamfers slope 26.6
    // degrees, and sliding up one keeps cos^2 of the speed - 128 of 160 px/s, what
    // Godot's own move_and_slide would keep - so a threshold near that would fail
    // the geometry rather than a catch, and a catch reads 0.
    //
    // Walked gripping the floor (friction 1) and not (0), the two readings a
    // CharacterBody2D's missing friction leaves open. The port is judged on
    // Player::kFriction; the other is printed for the planning doc.
    for (const float friction : {0.0f, 1.0f}) {
        const Walk walk = WalkTheSeam(friction);
        std::printf("  seam walk, friction %.0f: x %.0f reached at %.3f s, slowest held tick %.1f px/s, ended at x %.1f\n",
                    static_cast<double>(friction), kSeamWalkEndPx, walk.reachedAtS, walk.slowestPx, walk.endXPx);
        if (friction != Player::kFriction) continue;
        CHECK_MSG(walk.reachedAtS > 0.0, "the player gets across the seam");
        CHECK_MSG(walk.slowestPx > 0.5 * g_tuning.walkSpeedPx,
                  "without catching: the slowest held tick was " + std::to_string(walk.slowestPx) + " px/s");
    }
}

// ---- Buttons and doors ------------------------------------------------------

// A body the level built, put somewhere else by the test, at rest.
void PutBody(Play& play, const std::string& entity, const glm::dvec2& atPx) {
    const entt::entity body = play.built.entities.at(entity);
    play.registry.get<TransformComponent>(body).position = Units::ToWorld(atPx.x, atPx.y);
    play.registry.get<RigidBodyComponent>(body).velocity = glm::vec3(0.0f);
}

glm::dvec2 BodyPx(Play& play, const std::string& entity) {
    return Units::ToPixels(play.registry.get<TransformComponent>(play.built.entities.at(entity)).position);
}

bool DoorShut(Play& play, const std::string& name) {
    const Puzzle::SwitchedDoor* door = play.channels.FindDoor(name);
    return door != nullptr && door->motion.progress == 0.0f &&
           play.registry.get<TransformComponent>(door->entity).position == door->motion.closed;
}

void Level30sButtonsAreWiredToItsDoors() {
    // Off level30.tscn:
    // - button_975 is idx 0 at (94, 220), button_980 idx 1 at (64, 108), and
    //   button_982 idx 2 at (256, 60);
    // - door_lift_976 is switchIdx 0, 978 is 1, and 977 is 2.
    // None of the buttons has a trigger_size, so each is pressed through the
    // remake's 16 px fallback at its node.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    CHECK_EQ(static_cast<int>(play.channels.buttons.size()), 3);
    CHECK_EQ(static_cast<int>(play.channels.doors.size()), 3);
    const struct {
        const char* name;
        int channel;
        double x;
        double y;
    } buttons[] = {{"button_975", 0, 94.0, 220.0}, {"button_980", 1, 64.0, 108.0}, {"button_982", 2, 256.0, 60.0}};
    for (const auto& expected : buttons) {
        const Puzzle::Button* button = play.channels.FindButton(expected.name);
        const glm::vec3 at = Units::ToWorld(expected.x, expected.y);
        CHECK_MSG(button != nullptr && button->channel == expected.channel &&
                      button->box.centre == glm::vec2(at.x, at.y) &&
                      button->box.half == glm::vec2(Units::ToMetres(Trigger::kFallbackSizePx * 0.5)),
                  std::string(expected.name) + " is channel " + std::to_string(expected.channel) +
                      ", a 16 px box at its node");
    }
    const struct {
        const char* name;
        int channel;
    } doors[] = {{"door_lift_976", 0}, {"door_lift_978", 1}, {"door_lift_977", 2}};
    for (const auto& expected : doors) {
        const Puzzle::SwitchedDoor* door = play.channels.FindDoor(expected.name);
        CHECK_MSG(door != nullptr && door->channel == expected.channel,
                  std::string(expected.name) + " is on channel " + std::to_string(expected.channel));
    }
    // What Trigger::Overlaps can see: every dynamic body in the level has a box,
    // sphere or capsule. The crates have boxes and the player a capsule.
    int unseen = 0;
    for (const entt::entity entity : play.registry.view<RigidBodyComponent>()) {
        if (play.registry.get<RigidBodyComponent>(entity).isKinematic) continue;
        if (!play.registry.any_of<BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent>(entity)) {
            ++unseen;
        }
    }
    CHECK_EQ(unseen, 0);
}

void NothingStaticPressesAButton() {
    // The remake, probed. Each of level30's button triggers reaches 4 px into the
    // static beneath it, yet in 120 frames with nothing dynamic nearby none is
    // pressed and no door moves. So the geometry is checked here first: what
    // keeps the buttons up must be the rule that only dynamic bodies press, not a
    // gap under them.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    const struct {
        const char* button;
        const char* under;
    } pairs[] = {{"button_975", "platform_ent_895"},
                 {"button_980", "double_block_plat_ent_971"},
                 {"button_982", "block00_ent_981"}};
    for (const auto& pair : pairs) {
        const Puzzle::Button* button = play.channels.FindButton(pair.button);
        const double topPx =
            button != nullptr ? -(button->box.centre.y + button->box.half.y) * Units::kPixelsPerMetre : 0.0;
        const double bottomPx =
            button != nullptr ? -(button->box.centre.y - button->box.half.y) * Units::kPixelsPerMetre : 0.0;
        const double staticTopPx = PlatformTopPx(pair.under);
        CHECK_MSG(button != nullptr && staticTopPx > topPx && staticTopPx < bottomPx,
                  std::string(pair.under) + "'s top, at " + std::to_string(staticTopPx) + " px, is inside " +
                      pair.button + "'s trigger");
    }
    bool anyPressed = false;
    for (int tick = 0; tick < 120; ++tick) {
        Tick(play, 0.0f);
        for (const Puzzle::Button& button : play.channels.buttons) anyPressed = anyPressed || button.pressed;
    }
    CHECK_MSG(!anyPressed, "and in 120 ticks none of them is pressed");
    for (const char* door : {"door_lift_976", "door_lift_977", "door_lift_978"}) {
        CHECK_MSG(DoorShut(play, door), std::string(door) + " stays shut");
    }
}

void ACrateOnAButtonOpensItsDoor() {
    // The probe's control, ported. level30's small crate, 30 px like the probe's,
    // is dropped at (94, 190) onto button_975. In the remake, channel 0 was
    // pressed 8 frames after the drop, and door_lift_976 rose its full 126 px
    // while 977 and 978 stayed shut. Here it is the whole level, with all three
    // doors present.
    // - Pressed within 8 +- 3 ticks, and held every tick after: a crate
    //   chattering on the plate would flicker its door.
    // - That door open, and the other two shut.
    // - The crate taken away: the plate lets go at once, since it is not a latch,
    //   and the door comes back down.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    PutBody(play, "crate_small_ent_973", glm::dvec2(94.0, 190.0));
    int pressedAt = -1;
    bool held = true;
    for (int tick = 1; tick <= 120; ++tick) {
        Tick(play, 0.0f);
        const bool pressed = play.channels.Pressed(0);
        if (pressed && pressedAt < 0) pressedAt = tick;
        if (pressedAt > 0 && !pressed) held = false;
    }
    CHECK_MSG(pressedAt >= 5 && pressedAt <= 11,
              "channel 0 pressed at tick " + std::to_string(pressedAt) + " after the drop; the remake took 8");
    CHECK_MSG(held, "and held every tick after");
    const double doorY = BodyPx(play, "door_lift_976").y;
    CHECK_MSG(std::fabs(doorY - (192.0 - Mover::kDoorRisePx)) < 0.5,
              "door_lift_976 open, at y " + std::to_string(doorY) + " px");
    CHECK_MSG(DoorShut(play, "door_lift_977") && DoorShut(play, "door_lift_978") && !play.channels.Pressed(1) &&
                  !play.channels.Pressed(2),
              "977 and 978 shut, their channels up");

    play.registry.destroy(play.built.entities.at("crate_small_ent_973"));
    Tick(play, 0.0f);
    CHECK_MSG(!play.channels.Pressed(0), "with the crate gone, the plate lets go the next tick");
    for (int tick = 0; tick < 70; ++tick) Tick(play, 0.0f);
    CHECK_MSG(DoorShut(play, "door_lift_976"), "and door_lift_976 comes back down");
}

void ThePlayerPressesAButtonAndItLetsGo() {
    // A button counts the player too, since in the remake a CharacterBody2D is not
    // a static, and it lets go when the player walks off. From the spawn the
    // player walks left onto button_975, stands on it, and walks back off.
    //
    // The capsule's core runs down to y 214 and the trigger's top is at 212, so
    // side on the capsule reaches the trigger's right edge, x 102, with its centre
    // at x 112. The button counts a tick's positions before it moves, so that is
    // where the player must be standing when it presses, and again when it lets
    // go.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    for (int i = 0; i < 30; ++i) Tick(play, 0.0f);

    double pressedX = -1.0;
    for (int tick = 1; tick <= 90 && pressedX < 0.0; ++tick) {
        const double before = PlayerPx(play).x;
        Tick(play, -1.0f);
        if (play.channels.Pressed(0)) pressedX = before;
    }
    CHECK_MSG(pressedX > 109.0 && pressedX < 112.01,
              "the player presses button_975 as it reaches it: counted at x " + std::to_string(pressedX));

    bool standing = true;
    for (int i = 0; i < 30; ++i) {
        Tick(play, 0.0f);
        standing = standing && play.channels.Pressed(0);
    }
    CHECK_MSG(standing, "held while the player stands on it");

    double releasedX = -1.0;
    for (int tick = 1; tick <= 60 && releasedX < 0.0; ++tick) {
        const double before = PlayerPx(play).x;
        Tick(play, 1.0f);
        if (!play.channels.Pressed(0)) releasedX = before;
    }
    CHECK_MSG(releasedX > 111.99 && releasedX < 115.0,
              "and lets go as it walks off: counted at x " + std::to_string(releasedX));
}

void ThePlayerPushesACrateOntoAButton() {
    // Acceptance item 3, with level30's own crate.ent, crate_ent_968 (58 px),
    // moved to the floor left of the spawn at x 140. The player walks left into it
    // and pushes until it covers button_975. Then the player walks away, and the
    // crate holds the plate, and door_lift_976, by itself. With friction 0 the
    // player can push only through the contact. This is the measurement step 3
    // left for step 4.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    PutBody(play, "crate_ent_968", glm::dvec2(140.0, 194.0));
    for (int i = 0; i < 30; ++i) Tick(play, 0.0f);
    const double startX = BodyPx(play, "crate_ent_968").x;

    int pushedFor = -1;
    for (int tick = 1; tick <= 180 && pushedFor < 0; ++tick) {
        Tick(play, -1.0f);
        if (play.channels.Pressed(0)) pushedFor = tick;
    }
    std::printf("  crate push: button_975 pressed after %.3f s of pushing, the crate %.1f px along\n",
                pushedFor * static_cast<double>(kStep), startX - BodyPx(play, "crate_ent_968").x);
    CHECK_MSG(pushedFor > 0, "the player pushes the crate onto button_975");

    // Away to the right for 40 ticks, about 100 px and well short of crate_969,
    // then stand.
    bool held = true;
    for (int tick = 0; tick < 80; ++tick) {
        Tick(play, tick < 40 ? 1.0f : 0.0f);
        held = held && play.channels.Pressed(0);
    }
    CHECK_MSG(held, "and the crate holds it by itself once the player has walked away");
    const Puzzle::SwitchedDoor* door = play.channels.FindDoor("door_lift_976");
    CHECK_MSG(door != nullptr && door->motion.progress == 1.0f, "with door_lift_976 open");
}

// ---- Crystals and the exit --------------------------------------------------

// The player put somewhere by the test, at rest. Reaching most of level30 needs
// portals, which come in step 6.
void PutPlayer(Play& play, const glm::dvec2& atPx) {
    play.registry.get<TransformComponent>(play.player).position = Units::ToWorld(atPx.x, atPx.y);
    play.registry.get<RigidBodyComponent>(play.player).velocity = glm::vec3(0.0f);
}

// A pixel above standing on platform_ent_966, whose top is y 136: under
// crystal_ent_998 at x 664, with the exit to the right.
glm::dvec2 On966(double x) {
    return glm::dvec2(x, PlatformTopPx("platform_ent_966") - g_tuning.heightPx * 0.5 - 1.0);
}

void Level30sCrystalsAndExitAreWhereItPutsThem() {
    // Off level30.tscn: five crystal.ent, each with a 28x24 trigger_size at its
    // node, and door.ent's 8x24 trigger at trigger_offset (0, 37) from (712, 88).
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    CHECK_EQ(static_cast<int>(play.goals.crystals.size()), 5);
    const struct {
        const char* name;
        double x;
        double y;
    } crystals[] = {{"crystal_ent_998", 664.0, 118.0},
                    {"crystal_ent_999", 400.0, 208.0},
                    {"crystal_ent_1001", 432.0, 208.0},
                    {"crystal_ent_1031", 482.0, 106.0},
                    {"crystal_ent_1032", 480.0, 146.0}};
    for (const auto& expected : crystals) {
        const Goals::Crystal* crystal = play.goals.FindCrystal(expected.name);
        const glm::vec3 at = Units::ToWorld(expected.x, expected.y);
        CHECK_MSG(crystal != nullptr && crystal->box.centre == glm::vec2(at.x, at.y) &&
                      crystal->box.half == glm::vec2(Units::ToMetres(14.0), Units::ToMetres(12.0)),
                  std::string(expected.name) + " is a 28x24 box at its node");
    }
    const glm::vec3 exitAt = Units::ToWorld(712.0, 125.0);
    CHECK_MSG(play.goals.exit.centre == glm::vec2(exitAt.x, exitAt.y) &&
                  play.goals.exit.half == glm::vec2(Units::ToMetres(4.0), Units::ToMetres(12.0)),
              "the exit is an 8x24 box at (712, 125)");
    CHECK_MSG(play.goals.Remaining() == 5 && play.goals.exitEntries == 0 && !play.goals.completed,
              "nothing collected or entered before the first tick");
}

void OnlyThePlayerCollects() {
    // level30 has the case itself: crate_ent_968 stands on crystal_ent_999 and
    // crystal_ent_1001 from the first frame. In the remake only the player
    // triggers a crystal, so after two seconds both are still there. The crate is
    // checked to cover them, so what keeps them is the rule, not a gap.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    const entt::entity crate = play.built.entities.at("crate_ent_968");
    for (int tick = 0; tick < 120; ++tick) Tick(play, 0.0f);
    for (const char* name : {"crystal_ent_999", "crystal_ent_1001"}) {
        const Goals::Crystal* crystal = play.goals.FindCrystal(name);
        CHECK_MSG(crystal != nullptr && Trigger::Overlaps(play.registry, crate, crystal->box) && !crystal->collected,
                  std::string("crate_ent_968 covers ") + name + ", and it stays uncollected");
    }
}

void ThePlayerCollectsACrystal() {
    // The player is put on platform_ent_966 under crystal_ent_998, at x 664. The
    // crystal is collected on the first tick, and nothing else is: no other
    // crystal is within reach there, and neither is the exit.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    PutPlayer(play, On966(664.0));
    Tick(play, 0.0f);
    const Goals::Crystal* crystal = play.goals.FindCrystal("crystal_ent_998");
    CHECK_MSG(crystal != nullptr && crystal->collected, "crystal_ent_998 collected on the first tick");
    for (int tick = 0; tick < 30; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.goals.Remaining() == 4 && play.goals.exitEntries == 0,
              "and only it: 4 remain, and the exit is not entered");
}

void TheExitReportsTheEntry() {
    // From under crystal_ent_998 the player walks right into the exit. With the
    // switch off, the remake's default, the entry completes the level with
    // crystals still out.
    //
    // Standing on platform_ent_966 the capsule's core spans the trigger's height,
    // so its side meets the trigger's left edge, x 708, with its centre at 698.
    // The exit sees where the step left the player, so the entry is counted
    // between x 698 and one tick's walk past it.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    play.goals.rules.exitRequiresAllCrystals = false;
    PutPlayer(play, On966(664.0));
    for (int tick = 0; tick < 20; ++tick) Tick(play, 0.0f);
    double enteredX = -1.0;
    for (int tick = 1; tick <= 60 && enteredX < 0.0; ++tick) {
        Tick(play, 1.0f);
        if (play.goals.exitEntries > 0) enteredX = PlayerPx(play).x;
    }
    CHECK_MSG(enteredX > 697.99 && enteredX < 701.0, "the player enters the exit at x " + std::to_string(enteredX));
    CHECK_MSG(play.goals.completed && play.goals.Remaining() == 4,
              "and, switched off, that completes the level with 4 crystals out");
}

void AClosedExitWaitsForANewEntry() {
    // The switch on. The player stands in the exit with one crystal out, so the
    // entry reports and does not complete. Then the last crystal goes. The test
    // takes it, since reaching it needs portals. Still the level does not
    // complete, because the remake completes on an entry, not on standing there.
    // The player walks out and back in, and the level completes.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    play.goals.rules.exitRequiresAllCrystals = true;
    for (Goals::Crystal& crystal : play.goals.crystals) crystal.collected = crystal.name != "crystal_ent_1031";
    PutPlayer(play, On966(712.0));
    for (int tick = 0; tick < 20; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.goals.exitEntries == 1 && !play.goals.completed,
              "in the exit with a crystal out: entered, not completed");

    for (Goals::Crystal& crystal : play.goals.crystals) crystal.collected = true;
    for (int tick = 0; tick < 30; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.goals.exitEntries == 1 && !play.goals.completed,
              "the last crystal gone while it stands there: still not completed");

    for (int tick = 0; tick < 20; ++tick) Tick(play, -1.0f);
    CHECK_MSG(!play.goals.playerInExit, "it walks out");
    for (int tick = 0; tick < 60 && !play.goals.completed; ++tick) Tick(play, 1.0f);
    CHECK_MSG(play.goals.exitEntries == 2 && play.goals.completed, "and back in, and the level completes");
}

// ---- Portals ----------------------------------------------------------------

void TriggersAreExactAgainstATurnedBox() {
    // A 1 m box turned 45 degrees about the origin reaches its bounding box's
    // corners nowhere. A test against the bounding box would find (0.6, 0.6) m
    // inside it; in the box's own frame that point is 0.85 m out along an axis,
    // 0.35 m past the face. level30's crates stand square, so only a rig shows
    // this.
    entt::registry registry;
    const entt::entity box = registry.create();
    registry.emplace<TransformComponent>(box).rotation.z = 0.785398163f;
    registry.emplace<BoxColliderComponent>(box).size = glm::vec3(1.0f);
    CHECK_MSG(!Trigger::Overlaps(registry, box, Trigger::Box{glm::vec2(0.6f), glm::vec2(0.05f)}),
              "a box trigger inside the turned box's bounding box, off its face, is outside it");
    CHECK_MSG(Trigger::Overlaps(registry, box, Trigger::Box{glm::vec2(0.3f), glm::vec2(0.05f)}),
              "and one within it is inside");
    CHECK_MSG(!Trigger::Overlaps(registry, box, Trigger::Circle{glm::vec2(0.6f), 0.1f}),
              "a circle there is outside too");
    CHECK_MSG(Trigger::Overlaps(registry, box, Trigger::Circle{glm::vec2(0.4f), 0.1f}),
              "and a circle reaching over the face is inside");
}

void Level30sPortalsAndWhoTravels() {
    // level30 has no properties entity, so its budget is portals.json's default,
    // capped at a pair: 2. It has no no-portal zones. Of its bodies, the player
    // travels, and so do crate.ent and crate_small.ent, which say teleportable 1.
    // The inline crate says 0 and does not.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    CHECK_EQ(play.portals.budget, 2);
    CHECK_EQ(static_cast<int>(play.portals.zones.size()), 0);
    const auto travels = [&](entt::entity entity) {
        return std::find(play.portals.travellers.begin(), play.portals.travellers.end(), entity) !=
               play.portals.travellers.end();
    };
    CHECK_MSG(travels(play.player) && travels(play.built.entities.at("crate_ent_968")) &&
                  travels(play.built.entities.at("crate_small_ent_973")) &&
                  !travels(play.built.entities.at("crate_969")) && play.portals.travellers.size() == 3u,
              "the player, crate_ent_968 and crate_small_ent_973 travel, and crate_969 does not");
}

void AtTheCapTheOldestGivesWay() {
    // Three taps in open air. At the cap the oldest portal gives way, which
    // portals.json marks _guess, so this runs both ways. Recycling: two live, and
    // the first gone. Refusing: the third tap is turned down. The golden score's
    // count rises only with portals actually placed.
    for (const bool recycle : {true, false}) {
        Play play;
        std::string error;
        CHECK_MSG(StartLevel30(play, true, error), error);
        if (play.player == entt::null) return;
        play.portals.rules.recycleOldestAtCap = recycle;
        const bool first = play.portals.TryPlace(glm::dvec2(300.0, 40.0));
        const bool second = play.portals.TryPlace(glm::dvec2(400.0, 40.0));
        const bool third = play.portals.TryPlace(glm::dvec2(500.0, 40.0));
        const std::string how = recycle ? "recycling: " : "refusing: ";
        CHECK_MSG(first && second && third == recycle,
                  how + "the first two placed, the third " + (recycle ? "placed" : "refused"));
        CHECK_MSG(play.portals.placed.size() == 2u && play.portals.placed[0].atPx.x == (recycle ? 400.0 : 300.0) &&
                      play.portals.portalsUsed == (recycle ? 3 : 2),
                  how + "two live, the oldest " + (recycle ? "gone" : "kept"));
    }
}

void ANoPortalZoneRefusesATap() {
    // level30 has no no-portal zone, so this one is the test's: at (300, 100)
    // with scale 2, refusing taps within twice the ANTIPORTAL radius - the circle
    // the port plays, whose decoded value placement.json records as 32, and not
    // the portal's own 14. It is not checked here against a level that has a
    // zone; test_mp_movers does that on level10.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    Portals::NoPortalZone zone;
    zone.name = "test_zone";
    zone.centrePx = glm::dvec2(300.0, 100.0);
    zone.scale = 2.0;
    play.portals.zones.push_back(zone);
    const double reach = g_portalRules.antiportalRadiusPx * 2.0;
    CHECK_MSG(!play.portals.TryPlace(glm::dvec2(300.0 + reach - 1.0, 100.0)), "a tap just inside it is refused");
    CHECK_MSG(play.portals.TryPlace(glm::dvec2(300.0 + reach + 1.0, 100.0)) && play.portals.portalsUsed == 1,
              "and one just outside is placed");
}

// The two radii are decoded, and they are NOT the same number.
//
// The port carried one - the remake's collision_radius_px, which its own
// portals.json marks _guess and annotates "value unknown" - and used it both for
// a placed portal's trigger and for the circle an antiportal refuses a tap in.
// They are unrelated:
//
//   - a portal's own is g_portalCollisionRadius, 14 (Portal.angelscript, bytes
//     297250..297291);
//   - an antiportal's is half the FIELD ENTITY'S OWN SIZE - GetSize().x * 0.5 in
//     isPointInAntiPortalField - and GetCurrentSize is the sprite's frame times
//     the entity's scale. white_ring.png is 128 px and its SpriteCut is 1x1, and
//     AntiPortalManager's constructor scales every field by 0.5 over the node's
//     own scale, so the radius is 32 times the node's scale.
//
// So the port's 16 makes every no-portal field half the original's, on the 52
// levels that carry one. placement.json holds the derivation.
//
// Asserting the VALUES alone would not catch the bug this fixes: one field doing
// both jobs passes that. What catches it is that the two differ, and that the
// old 16 x scale point now falls INSIDE the refusal.
void ThePlacementRadiiAreDecodedAndDistinct() {
    // A portal's own radius is DECODED and played: 14.
    CHECK_NEAR(static_cast<float>(g_portalRules.entryRadiusPx), 14.0f);

    // An antiportal's is the remake's 16, still played. The decode says 32 - half
    // white_ring.png's 128 px frame, times the manager's 0.5 and the node's scale -
    // and footage F3 brackets the original's refusal on 1-7 at 31.6-32.4 x scale;
    // placement.json carries it beside this one, and the owner's ruling R1 takes it
    // in a step of its own. Asserting 16 here is asserting what the port PLAYS, so
    // that changing it is a deliberate act rather than a silent one.
    CHECK_NEAR(static_cast<float>(g_portalRules.antiportalRadiusPx), 16.0f);

    // The manager's Scale, which the ring is drawn by (64 x scale across) and the
    // decoded radius comes from, read with the entity it collects.
    CHECK_NEAR(static_cast<float>(g_portalRules.antiportalManagerScale), 0.5f);
    CHECK_MSG(g_portalRules.antiportalEntity == "antiportal", "the manager collects antiportal");

    // The point of the split: two numbers, not one. The port carried a single
    // collision_radius_px for both, which is the bug this undoes, and a rename
    // alone would not keep them apart.
    CHECK_MSG(g_portalRules.entryRadiusPx != g_portalRules.antiportalRadiusPx,
              "a portal's radius and an antiportal's are two numbers, not one");
}

// A tap is refused until the level is old enough, and then for a while after
// each one taken.
//
// PortalManager keeps two Timers and a manager is built per level, so both start
// at zero: update skips the whole tap path while gameTimer is below
// FIRST_PORTAL_MIN_TIME (300), and managePortalInsertion takes a tap only when
// lastPortalTimer is above NEXT_PORTAL_MIN_TIME (400). The remake carried 0.0 s
// and 0.25 s and admits inventing them.
//
// So the FIRST tap of a level waits on the larger of the two - 400 ms, not 300 -
// which is worth pinning because it is the surprising half and the one a reader
// would "simplify" to 300.
//
// Gated on Shoot rather than TryPlace: TryPlace is what a dozen cases here call
// to put a portal where they need one, and a clock there would refuse them for a
// reason none of them is about.
void ThePortalCooldownsHoldATapOff() {
    CHECK_NEAR(static_cast<float>(g_portalRules.firstPortalMinMs), 300.0f);
    CHECK_NEAR(static_cast<float>(g_portalRules.nextPortalMinMs), 400.0f);

    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    const glm::dvec2 aim = PlayerPx(play) + glm::dvec2(0.0, -48.0);

    // A level that has only just been built takes nothing.
    CHECK_MSG(!play.portals.Shoot(play.registry, aim), "a tap on a level's first tick is refused");
    CHECK_EQ(play.portals.shotsFired, 0);

    // 400 ms of ticks - the larger gate - and then it is taken.
    const int ticks = static_cast<int>(0.4f / kStep) + 2;
    for (int tick = 0; tick < ticks; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.portals.Shoot(play.registry, aim), "and one after 400 ms is taken");
    CHECK_EQ(play.portals.shotsFired, 1);

    // And the next is held off again. Let the shot finish first, so what refuses
    // the second tap is the cooldown and not the one-shot-at-a-time rule.
    for (int tick = 0; tick < 240 && play.portals.flight; ++tick) Tick(play, 0.0f);
    CHECK_MSG(!play.portals.flight, "the first shot has landed or failed");
    CHECK_MSG(!play.portals.Shoot(play.registry, aim), "a second tap straight after the first is refused");
}

void ThePlayerGoesThroughAndThePairIsSpent() {
    // A portal on the floor ahead of the spawn at (230, 208), and its partner in
    // the air at (300, 120). The player walks right into the first and comes out
    // of the second, at it: the original puts a traveller at the exit itself and
    // has a character keep its walk and turn its fall back (transit.json, step
    // 11a). Then both portals are gone.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    for (int tick = 0; tick < 30; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.portals.TryPlace(glm::dvec2(230.0, 208.0)) && play.portals.TryPlace(glm::dvec2(300.0, 120.0)),
              "both placed");
    for (int tick = 1; tick <= 90 && play.portals.traversals == 0; ++tick) Tick(play, 1.0f);
    CHECK_EQ(play.portals.traversals, 1);

    const auto& rigid = play.registry.get<RigidBodyComponent>(play.player);
    const glm::dvec2 exitVelocityPx(rigid.velocity.x * Units::kPixelsPerMetre,
                                    -rigid.velocity.y * Units::kPixelsPerMetre);
    const glm::dvec2 expected = Portal::ExitPosition(glm::dvec2(300.0, 120.0), exitVelocityPx, g_portalRules.transit);
    const glm::dvec2 at = PlayerPx(play);
    CHECK_MSG(glm::length(at - expected) < 0.01, "out of the second portal, as transit.json puts it: at (" +
                                                     std::to_string(at.x) + ", " + std::to_string(at.y) + ")");
    CHECK_MSG(glm::length(at - glm::dvec2(300.0, 120.0)) < 0.01, "and that is the exit itself");
    CHECK_MSG(exitVelocityPx.x > 0.0, "still walking right, at " + std::to_string(exitVelocityPx.x) + " px/s");
    const double speedPx = glm::length(exitVelocityPx);
    const double wantPx = g_portalRules.transit.momentumMode == "reset"
                              ? 0.0
                              : g_tuning.walkSpeedPx * g_portalRules.transit.exitSpeedScale;
    CHECK_MSG(std::fabs(speedPx - wantPx) < 2.0,
              "its walk carried through: " + std::to_string(speedPx) + " px/s, want " + std::to_string(wantPx));
    CHECK_MSG(play.portals.placed.empty() && play.portals.portalsUsed == 2, "and the pair is spent");
}

void OnlyTeleportablesTravel() {
    // A portal over the inline crate, crate_969, which says teleportable 0, with a
    // partner in the air. For a second nothing happens: the crate does not go
    // through, and the pair is not spent.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    const glm::dvec2 crateAt = BodyPx(play, "crate_969");
    CHECK_MSG(play.portals.TryPlace(glm::dvec2(300.0, 40.0)) && play.portals.TryPlace(crateAt), "both placed");
    for (int tick = 0; tick < 60; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.portals.traversals == 0 && play.portals.placed.size() == 2u,
              "crate_969 does not go through, and the pair stays");
    CHECK_MSG(glm::length(BodyPx(play, "crate_969") - crateAt) < 4.0, "crate_969 stays where it was");
}

void ACratePortalledOntoAButtonOpensItsDoor() {
    // The move level30 is built around, as far as step 6 goes. One portal goes
    // over crate_ent_968, and its partner in the air above button_980, which
    // stands on a block the floor cannot reach. The crate comes out still, so
    // straight down, falls onto the block, and presses button_980: channel 1.
    // That opens door_lift_978, while 977 on channel 2 stays shut. This is the
    // whole level, with every door present.
    Play play;
    std::string error;
    CHECK_MSG(StartLevel30(play, true, error), error);
    if (play.player == entt::null) return;
    for (int tick = 0; tick < 30; ++tick) Tick(play, 0.0f);
    CHECK_MSG(play.portals.TryPlace(glm::dvec2(64.0, 60.0)) && play.portals.TryPlace(BodyPx(play, "crate_ent_968")),
              "a portal above button_980, and one over crate_ent_968");
    Tick(play, 0.0f);
    CHECK_MSG(play.portals.traversals == 1 && play.portals.placed.empty(),
              "the crate goes through on the next tick, and the pair is spent");

    int pressedAt = -1;
    for (int tick = 1; tick <= 60 && pressedAt < 0; ++tick) {
        Tick(play, 0.0f);
        if (play.channels.Pressed(1)) pressedAt = tick;
    }
    CHECK_MSG(pressedAt > 0, "it lands on button_980 and presses channel 1, at tick " + std::to_string(pressedAt));
    bool held = true;
    for (int tick = 0; tick < 200; ++tick) {
        Tick(play, 0.0f);
        held = held && play.channels.Pressed(1);
    }
    const Puzzle::SwitchedDoor* door = play.channels.FindDoor("door_lift_978");
    CHECK_MSG(held && door != nullptr && door->motion.progress == 1.0f, "held, and door_lift_978 opens");
    CHECK_MSG(DoorShut(play, "door_lift_977") && DoorShut(play, "door_lift_976"), "977 and 976 stay shut");
}

void runTests() {
    Level30HasTheRolesItsPuzzleNeeds();
    TheMoversAreKinematic();
    DoorsTakeTheirStrideFromTheLevel();
    ACrateRidesADoorUpAndDown();
    ThePlayerLandsOnLevel30();
    ThePlayerWalksAcrossTheSeam();
    Level30sButtonsAreWiredToItsDoors();
    NothingStaticPressesAButton();
    ACrateOnAButtonOpensItsDoor();
    ThePlayerPressesAButtonAndItLetsGo();
    ThePlayerPushesACrateOntoAButton();
    Level30sCrystalsAndExitAreWhereItPutsThem();
    OnlyThePlayerCollects();
    ThePlayerCollectsACrystal();
    TheExitReportsTheEntry();
    AClosedExitWaitsForANewEntry();
    TriggersAreExactAgainstATurnedBox();
    Level30sPortalsAndWhoTravels();
    AtTheCapTheOldestGivesWay();
    ANoPortalZoneRefusesATap();
    ThePlacementRadiiAreDecodedAndDistinct();
    ThePortalCooldownsHoldATapOff();
    ThePlayerGoesThroughAndThePairIsSpent();
    OnlyTeleportablesTravel();
    ACratePortalledOntoAButtonOpensItsDoor();
}

} // namespace

int main() {
    std::error_code ec;
    const std::string roles = kData + "/entity_roles.json";
    if (!std::filesystem::is_directory(kLevels, ec) || !std::filesystem::is_regular_file(roles, ec) ||
        !std::filesystem::is_regular_file(kData + "/player.json", ec) ||
        !std::filesystem::is_regular_file(kData + "/portals.json", ec)) {
        std::printf("test_mp_play: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's role table and player.json at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), roles.c_str());
        return 77;
    }
    std::string error;
    if (!LoadInputs(error)) {
        std::printf("test_mp_play: %s\n", error.c_str());
        return 1;
    }
    runTests();
    return ::test::summary("test_mp_play", 125);
}
