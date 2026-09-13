// Zero gravity: chapter 4's first eighteen levels, level1c to level18c, where
// the level property `no_gravity` is set.
//
// The last of the four things that stopped a converted level starting, and the
// only one of them that is a MECHANIC rather than a gap. One flag turns on three
// separate things in the original, and this suite asserts all three, because a
// level with any one of them missing still starts and cannot be finished:
//
//   - the world's gravity is zero, so nothing falls;
//   - the walking buttons do nothing at all;
//   - every tap the portals TAKE shoves the player away from where it aimed,
//     added onto the velocity it already had.
//
// It also asserts the contrast in both directions - that a normal level still
// falls, still walks, and is NOT shoved - because each of the three is a branch
// on a flag, and a branch stuck on is as wrong as one stuck off.
//
// Reads the converted levels from outside this repository, and skips, saying
// where it looked, when they are absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Player.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"
#include "sim/Zerog.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// The first zero-gravity level, and normal ones to hold it against.
constexpr const char* kWeightless = "level1c";
constexpr const char* kNormal = "level0";
// A normal level THAT GRANTS A PLACEMENT. level0 is metadata/max_portals = "0" -
// it ships static portals and hands out no placements at all - so Shoot refuses
// there before any recoil could be reached, and a tap test on it would prove
// nothing. level1 grants one.
constexpr const char* kNormalWithPortals = "level1";

// How far the solver may move a player ONCE as it settles. Some of these levels
// place the player overlapping what is under it, and being pushed out of that is
// not a fall. Bounded by a tile; a real fall is some 4400 px in three seconds.
constexpr double kSettlePx = 16.0;

glm::dvec2 WhereIs(entt::registry& registry, entt::entity body) {
    return Units::ToPixels(registry.get<TransformComponent>(body).position);
}

double Length(const glm::dvec2& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

bool Open(const std::string& name, Game::Data& data, entt::registry& registry, Game::Level& level) {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-zerog";
    std::string error;
    if (!Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) ||
        !Game::Start(data, registry, level, error)) {
        CHECK_MSG(false, name + ": " + error);
        return false;
    }
    return true;
}

void TheRulesRead() {
    Zerog::Rules rules;
    std::string error;
    const bool ok = Zerog::LoadRules(kPortData + "/zerog.json", rules, error);
    CHECK_MSG(ok, error);
    if (!ok) return;
    CHECK(rules.flagName == "no_gravity");
    CHECK(::test::nearly(static_cast<float>(rules.recoilMetresPerSecond), 1.4f));
    CHECK_EQ(rules.worldGravityPx, 0.0);
    std::printf("  recoil %.2f m/s (%.0f px/s at %.0f px per metre), world gravity %.0f\n",
                rules.recoilMetresPerSecond, rules.recoilMetresPerSecond * Units::kPixelsPerMetre,
                Units::kPixelsPerMetre, rules.worldGravityPx);
}

// The arithmetic on its own, away from any level.
void TheRecoilPushesAwayFromWhereYouAimed() {
    const glm::dvec2 player(100.0, 100.0);
    const double mps = 1.4;

    // Aimed to the RIGHT, so pushed to the left.
    const glm::vec3 pushedLeft = Zerog::Impulse(player, glm::dvec2(200.0, 100.0), mps);
    CHECK(pushedLeft.x < 0.0f);
    CHECK(::test::nearly(pushedLeft.x, -1.4f));
    CHECK(::test::nearly(pushedLeft.y, 0.0f));

    // Aimed BELOW - a larger y in the remake's pixels, which count down - so
    // pushed UP, which is a positive y in the engine's metres. This is the one
    // the sign error would hide in, and it is why it has its own check.
    const glm::vec3 pushedUp = Zerog::Impulse(player, glm::dvec2(100.0, 200.0), mps);
    CHECK(pushedUp.y > 0.0f);
    CHECK(::test::nearly(pushedUp.y, 1.4f));
    CHECK(::test::nearly(pushedUp.x, 0.0f));

    // And aimed ABOVE, pushed down.
    const glm::vec3 pushedDown = Zerog::Impulse(player, glm::dvec2(100.0, 0.0), mps);
    CHECK(pushedDown.y < 0.0f);
    CHECK(::test::nearly(pushedDown.y, -1.4f));

    // normalize: how FAR away the tap was makes no difference to the shove. A
    // tap one pixel away and one nine thousand pixels away push identically.
    const glm::vec3 close = Zerog::Impulse(player, glm::dvec2(101.0, 100.0), mps);
    const glm::vec3 distant = Zerog::Impulse(player, glm::dvec2(9000.0, 100.0), mps);
    CHECK(::test::nearly(close.x, distant.x));
    CHECK(::test::nearly(close.y, distant.y));

    // A diagonal is still 1.4 long, not 1.4 in each axis.
    const glm::vec3 diagonal = Zerog::Impulse(player, glm::dvec2(200.0, 200.0), mps);
    CHECK(::test::nearly(std::sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y), 1.4f));
    CHECK(diagonal.x < 0.0f);
    CHECK(diagonal.y > 0.0f);

    // A tap exactly ON the player is the one case normalize cannot answer: it
    // gives nothing, rather than a NaN that would poison the body's velocity
    // for the rest of the level.
    const glm::vec3 nothing = Zerog::Impulse(player, player, mps);
    CHECK(::test::nearly(nothing.x, 0.0f));
    CHECK(::test::nearly(nothing.y, 0.0f));
    CHECK(::test::nearly(nothing.z, 0.0f));
}

void EveryOneOfTheEighteenStartsAndCarriesTheFlag() {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-zerog";
    int started = 0;
    for (int i = 1; i <= 18; ++i) {
        const std::string name = "level" + std::to_string(i) + "c";
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        std::string error;
        const bool ok = Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) &&
                        Game::Start(data, registry, level, error);
        CHECK_MSG(ok, name + ": " + error);
        if (!ok) continue;
        CHECK_MSG(level.noGravity, name + " sets no_gravity, and Game::Level says so");
        if (level.noGravity) ++started;
    }
    CHECK_EQ(started, 18);
    std::printf("  %d of 18 zero-gravity levels start and carry the flag\n", started);

    // And the two chapter-4 levels that do NOT set it still say so - without
    // this, a flag true everywhere would pass every check above.
    for (const char* name : {"level0c", "level27c"}) {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        std::string error;
        if (Game::LoadData(kLevels + "/" + std::string(name) + ".tscn", kData, prisms, data, error) &&
            Game::Start(data, registry, level, error)) {
            CHECK_MSG(!level.noGravity, std::string(name) + " is not a zero-gravity level");
        }
    }
}

void NothingFallsInThere() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;

    // THE FIRST SECOND IS A SETTLE, NOT A FALL, and telling those apart is the
    // whole of this test. level1c places its player overlapping what is under it;
    // the solver pushes it out once - 3.75 px - and then stops. Measured across
    // three seconds that reads as drift. Measured over the two seconds AFTER it,
    // it is nothing at all, which a fall never is: at the remake's 980 px/s^2,
    // three seconds is some 4400 px.
    const glm::dvec2 from = WhereIs(registry, level.player);
    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    const glm::dvec2 settled = WhereIs(registry, level.player);
    for (int tick = 0; tick < 120; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    const glm::dvec2 to = WhereIs(registry, level.player);
    std::printf("  %s: settles %.4f px in the first second, then moves %.4f px in two more\n", kWeightless,
                Length(settled - from), Length(to - settled));
    CHECK(Length(settled - from) < kSettlePx);
    CHECK(Length(to - settled) < 0.01);

    // Not merely still: its velocity is nothing, so there is no fall being
    // cancelled by a floor it happens to have spawned on.
    const glm::vec3 velocity = registry.get<RigidBodyComponent>(level.player).velocity;
    CHECK(::test::nearly(velocity.y, 0.0f));
    CHECK(::test::nearly(velocity.x, 0.0f));
}

void AndEverywhereElseItStillFalls() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kNormal, data, registry, level)) return;
    CHECK(!level.noGravity);

    // BeforeStep rather than a whole Tick, because BeforeStep is where the branch
    // is. Player::Steer puts the fall INTO the velocity, and the physics step
    // that follows hands it straight back to the floor level0's player stands on
    // - which is why a whole Tick shows nothing here. What is asserted is that
    // Steer RAN, since not running it is the entire mechanism being tested.
    Game::BeforeStep(data, registry, level, 0.0f, kStep);
    CHECK(registry.get<RigidBodyComponent>(level.player).velocity.y < 0.0f);
}

// And the dual, on that same seam: in a zero-gravity level BeforeStep leaves the
// velocity exactly as it found it, in x and in y, with RIGHT held down.
void AndInThereBeforeStepLeavesItAlone() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;

    registry.get<RigidBodyComponent>(level.player).velocity = glm::vec3(0.0f);
    Game::BeforeStep(data, registry, level, 1.0f, kStep);
    const glm::vec3 velocity = registry.get<RigidBodyComponent>(level.player).velocity;
    CHECK(::test::nearly(velocity.y, 0.0f));
    CHECK(::test::nearly(velocity.x, 0.0f));
}

void TheWalkingButtonsDoNothing() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;

    // A second to settle FIRST, so that what is measured afterwards is the
    // buttons and nothing else. Then a second of RIGHT held down:
    // MainCharacter::update skips ScreenPad::update entirely in these levels, so
    // this is not a slow walk - it is NO walk, and the tolerance says so.
    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    const glm::dvec2 from = WhereIs(registry, level.player);
    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 1.0f, kStep);
    const glm::dvec2 to = WhereIs(registry, level.player);
    std::printf("  %s: a second of RIGHT moves the player %.4f px\n", kWeightless, Length(to - from));
    CHECK(Length(to - from) < 0.01);
    CHECK(::test::nearly(registry.get<RigidBodyComponent>(level.player).velocity.x, 0.0f));
}

void AndEverywhereElseTheyStillWalk() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kNormal, data, registry, level)) return;

    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 1.0f, kStep);
    CHECK(registry.get<RigidBodyComponent>(level.player).velocity.x > 0.1f);
}

void ATapShovesThePlayerAwayFromIt() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;

    // Both cooldowns count in milliseconds from the start of the level, and the
    // first tap is held off by the larger of them. A tap before that is refused
    // for a reason this test is not about.
    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);

    const glm::dvec2 aimed = WhereIs(registry, level.player) + glm::dvec2(200.0, 0.0);
    const glm::vec3 before = registry.get<RigidBodyComponent>(level.player).velocity;
    const bool fired = level.portals.Shoot(registry, aimed);
    CHECK(fired);
    if (!fired) return;
    const glm::vec3 after = registry.get<RigidBodyComponent>(level.player).velocity;

    // Aimed to the right, so shoved to the left, by exactly the recoil.
    CHECK(after.x < before.x);
    CHECK(::test::nearly(after.x - before.x, -1.4f));
    CHECK(::test::nearly(after.y - before.y, 0.0f));

    // AT TAP TIME, not when the shot arrives: the shot is still in the air. That
    // is the original's own order, and it is why a tap that goes on to hit a
    // wall still moves the player.
    CHECK(level.portals.flight.has_value());
}

void TheShoveIsAddedToWhatYouAlreadyHad() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;
    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);

    // applyImpulse is SetLinearVelocity(GetLinearVelocity() + impulse). A player
    // already moving keeps that motion and gains the recoil on top - which is
    // what makes drift the mechanic. Were it a set, every tap would reset the
    // player and the levels would be a different game.
    registry.get<RigidBodyComponent>(level.player).velocity = glm::vec3(3.0f, 0.0f, 0.0f);
    const glm::dvec2 aimed = WhereIs(registry, level.player) + glm::dvec2(200.0, 0.0);
    CHECK(level.portals.Shoot(registry, aimed));
    CHECK(::test::nearly(registry.get<RigidBodyComponent>(level.player).velocity.x, 3.0f - 1.4f));
}

void AndNoTapShovesAnyoneInANormalLevel() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kNormalWithPortals, data, registry, level)) return;
    for (int tick = 0; tick < 60; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);

    const glm::dvec2 aimed = WhereIs(registry, level.player) + glm::dvec2(200.0, 0.0);
    const glm::vec3 before = registry.get<RigidBodyComponent>(level.player).velocity;
    const bool fired = level.portals.Shoot(registry, aimed);
    CHECK(fired);
    if (!fired) return;
    const glm::vec3 after = registry.get<RigidBodyComponent>(level.player).velocity;
    CHECK(::test::nearly(after.x, before.x));
    CHECK(::test::nearly(after.y, before.y));
}

// Honesty, not a mechanic: what these levels hold that the port still does not
// play. gravity_well is the next step and this counts what it will be worth.
void ThreeOfThemHoldAGravityWell() {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-zerog";
    int levels = 0;
    int placements = 0;
    for (int i = 1; i <= 18; ++i) {
        const std::string name = "level" + std::to_string(i) + "c";
        Game::Data data;
        std::string error;
        if (!Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error)) continue;
        int here = 0;
        for (const Tscn::Node& node : data.scene.nodes) {
            if (node.parent != ".") continue;
            if (Roles::RoleOf(data.roles, node) == "gravity_well") ++here;
        }
        if (here == 0) continue;
        ++levels;
        placements += here;
        std::printf("    %-8s %d gravity well(s)\n", name.c_str(), here);
    }
    std::printf("  %d gravity wells across %d of the eighteen, and none of them played\n", placements, levels);
    CHECK_EQ(levels, 3);
}

void runTests() {
    TheRulesRead();
    TheRecoilPushesAwayFromWhereYouAimed();
    EveryOneOfTheEighteenStartsAndCarriesTheFlag();
    NothingFallsInThere();
    AndEverywhereElseItStillFalls();
    AndInThereBeforeStepLeavesItAlone();
    TheWalkingButtonsDoNothing();
    AndEverywhereElseTheyStillWalk();
    ATapShovesThePlayerAwayFromIt();
    TheShoveIsAddedToWhatYouAlreadyHad();
    AndNoTapShovesAnyoneInANormalLevel();
    ThreeOfThemHoldAGravityWell();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_zerog: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_zerog", 55);
}
