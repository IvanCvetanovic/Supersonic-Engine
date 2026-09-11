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
#include "sim/LevelBuilder.hpp"
#include "sim/Mover.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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

Tscn::Scene g_level30;
Roles::Table g_roles;
std::filesystem::path g_prisms;

bool LoadInputs(std::string& error) {
    if (!Tscn::Load(kLevels + "/level30.tscn", g_level30, error)) return false;
    if (!Roles::Load(kData + "/entity_roles.json", g_roles, error)) return false;
    g_prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-play";
    std::error_code ec;
    std::filesystem::create_directories(g_prisms, ec);
    return true;
}

LevelBuilder::Options PortOptions() {
    LevelBuilder::Options options;
    options.prismDirectory = g_prisms;
    options.roles = &g_roles;
    return options;
}

void UseOriginalGravity(entt::registry& registry) {
    PhysicsSettings settings;
    settings.gravity = glm::vec3(0.0f, -static_cast<float>(Units::kGravity), 0.0f);
    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));
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
    UseOriginalGravity(registry);
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
    const double flightPx = speedPx * speedPx / (2.0 * Units::kGravity * Units::kPixelsPerMetre);
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

void runTests() {
    Level30HasTheRolesItsPuzzleNeeds();
    TheMoversAreKinematic();
    DoorsTakeTheirStrideFromTheLevel();
    ACrateRidesADoorUpAndDown();
}

} // namespace

int main() {
    std::error_code ec;
    const std::string roles = kData + "/entity_roles.json";
    if (!std::filesystem::is_directory(kLevels, ec) || !std::filesystem::is_regular_file(roles, ec)) {
        std::printf("test_mp_play: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's role table at %s.\n"
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
    return ::test::summary("test_mp_play", 32);
}
