// The bouncers of chapter 4's weightless rooms: entity `bounce`, role `bouncer`.
//
// Four slabs across three levels, each bobbing one pixel up and one pixel down
// every 5.24 seconds. The motion is decoration; what is NOT decoration is that
// the slab is solid and moves, so it has to carry what stands on it.
//
// WHAT THIS SUITE IS REALLY FOR. The travel is ONE PIXEL, so no test of position
// could ever tell a bouncer that carries the player from one that does not - a
// static body would sit at the same coordinates to within a pixel and pass every
// check below about where it is. So the assertion that matters here is that the
// body is KINEMATIC, which is a build-time property of Roles::Moves and nothing
// the tick can repair. test_mp_torch watches a body for the same reason.
//
// And the PHASE: cos(0) is 1, so a bouncer starts at full displacement rather
// than at the node the level places it on - the node is the MIDDLE of the bob.
// That is the trap fields.json records for the shock rings, and it is the one a
// sine would get wrong while looking right in motion.
//
// Reads the converted levels from outside this repository, and skips, saying
// where it looked, when they are absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Bounce.hpp"
#include "sim/Game.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstddef>
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
constexpr double kPi = 3.14159265358979323846;

bool Open(const std::string& name, Game::Data& data, entt::registry& registry, Game::Level& level) {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-bounce";
    std::string error;
    if (!Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) ||
        !Game::Start(data, registry, level, error)) {
        CHECK_MSG(false, name + ": " + error);
        return false;
    }
    return true;
}

void TheRulesRead() {
    Bounce::Rules rules;
    std::string error;
    const bool ok = Bounce::LoadRules(kPortData + "/bounce.json", rules, error);
    CHECK_MSG(ok, error);
    if (!ok) return;
    CHECK(::test::nearly(static_cast<float>(rules.speedRadPerSec), 1.2f));
    CHECK(::test::nearly(static_cast<float>(rules.stridePx), 1.0f));
    CHECK(::test::nearly(static_cast<float>(rules.frameClampMs), 200.0f));
    const double periodS = 2.0 * kPi / rules.speedRadPerSec;
    std::printf("  %.1f rad/s, %.1f px either way, a full bob every %.2f s\n", rules.speedRadPerSec, rules.stridePx,
                periodS);
    CHECK(periodS > 5.0 && periodS < 5.5);
}

// The census, counted as NODES.
void FourSlabsAcrossThreeLevels() {
    const struct Level {
        const char* name;
        std::size_t count;
    } levels[] = {{"level1c", 1}, {"level6c", 2}, {"level16c", 1}};

    std::size_t total = 0;
    for (const Level& one : levels) {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (!Open(one.name, data, registry, level)) continue;
        CHECK_MSG(level.bounce.bobs.size() == one.count,
                  std::string(one.name) + " places " + std::to_string(one.count) + " bouncer(s)");
        total += level.bounce.bobs.size();
        // Every one of them is in a level that sets no_gravity, which is where
        // its numbers come from: setNoGravityLinearMotionProperties.
        CHECK_MSG(level.noGravity, std::string(one.name) + " is a zero-gravity level");
    }
    CHECK_EQ(total, std::size_t{4});
    std::printf("  %zu bouncer(s) across 3 levels\n", total);

    // And a level with none has none - without this, a Wire that found every
    // node in the game would pass every count above.
    {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (Open("level0", data, registry, level)) CHECK(level.bounce.bobs.empty());
    }
}

// THE ONE THAT MATTERS. A slab that moves and does not carry is not built.
void EverySlabIsKinematic() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open("level6c", data, registry, level)) return;

    for (const Bounce::Bob& bob : level.bounce.bobs) {
        CHECK_MSG(registry.valid(bob.entity), bob.name + " was built");
        if (!registry.valid(bob.entity)) continue;
        const auto* rigid = registry.try_get<RigidBodyComponent>(bob.entity);
        CHECK_MSG(rigid != nullptr, bob.name + " has a body to be moved by");
        if (rigid == nullptr) continue;
        CHECK_MSG(rigid->isKinematic, bob.name + " is KINEMATIC, so it carries what stands on it");
    }
}

// cos(0) = 1: it begins a whole stride from where the level places it.
void ItBeginsAtFullDisplacementNotAtItsNode() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open("level1c", data, registry, level)) return;
    if (level.bounce.bobs.empty()) {
        CHECK_MSG(false, "level1c places a bouncer");
        return;
    }

    const Bounce::Bob& bob = level.bounce.bobs.front();
    // The node's own place, which the level file gives as (140, 202).
    CHECK(::test::nearly(static_cast<float>(bob.originPx.x), 140.0f));
    CHECK(::test::nearly(static_cast<float>(bob.originPx.y), 202.0f));

    const glm::dvec2 at = bob.AtPx(level.bounce.rules);
    CHECK(::test::nearly(static_cast<float>(at.x), static_cast<float>(bob.originPx.x)));
    CHECK(::test::nearly(static_cast<float>(at.y - bob.originPx.y), static_cast<float>(level.bounce.rules.stridePx)));
}

// Half a period away it is a whole stride the other side, and the travel from
// end to end is twice the stride - one pixel each way.
void ItBobsAndComesBack() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open("level1c", data, registry, level)) return;
    if (level.bounce.bobs.empty()) return;

    const std::string name = level.bounce.bobs.front().name;
    const glm::dvec2 originPx = level.bounce.bobs.front().originPx;
    const double stridePx = level.bounce.rules.stridePx;

    double lowest = originPx.y;
    double highest = originPx.y;
    // Half a bob: PI / 1.2 = 2.618 s, which is 157 ticks at 60 a second.
    for (int tick = 0; tick < 157; ++tick) {
        Game::Tick(data, registry, level, 0.0f, kStep);
        const Bounce::Bob* bob = level.bounce.Find(name);
        CHECK_MSG(bob != nullptr, name + " is still there");
        if (bob == nullptr) return;
        const double y = bob->AtPx(level.bounce.rules).y;
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
    }

    const Bounce::Bob* bob = level.bounce.Find(name);
    if (bob == nullptr) return;
    const double endY = bob->AtPx(level.bounce.rules).y;
    std::printf("  level1c: from %+.4f to %+.4f px about its node, ending %+.4f\n", lowest - originPx.y,
                highest - originPx.y, endY - originPx.y);

    // A whole stride the other side of the node.
    CHECK(endY < originPx.y - stridePx * 0.99);
    // And it never went further than a stride either way.
    CHECK(highest - originPx.y <= stridePx + 0.001);
    CHECK(originPx.y - lowest <= stridePx + 0.001);
}

// The body in the registry goes where the bob says, not merely the arithmetic.
void TheBodyItselfMoves() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open("level1c", data, registry, level)) return;
    if (level.bounce.bobs.empty()) return;

    const std::string name = level.bounce.bobs.front().name;
    for (int tick = 0; tick < 30; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);

    const Bounce::Bob* bob = level.bounce.Find(name);
    CHECK(bob != nullptr);
    if (bob == nullptr || !registry.valid(bob->entity)) return;
    const glm::dvec2 saidPx = bob->AtPx(level.bounce.rules);
    const glm::dvec2 isPx = Units::ToPixels(registry.get<TransformComponent>(bob->entity).position);
    CHECK(::test::nearly(static_cast<float>(isPx.x), static_cast<float>(saidPx.x)));
    CHECK(::test::nearly(static_cast<float>(isPx.y), static_cast<float>(saidPx.y)));
}

// The role is played now, so no level reports it inert.
void TheRoleIsPlayed() {
    CHECK(Roles::IsPorted(Roles::kBouncer));
    CHECK(Roles::Moves(Roles::kBouncer));
    // And a role that is NOT played is still not, so admitting this one did not
    // admit everything.
    //
    // This named `gravity_well` until that was built a step later, which is a
    // guard doing its job: it failed the moment the thing it was pinning stopped
    // being true. `boss_spawn` takes its place because the port plays two of the
    // four bosses and leaves level31a's dragon and level31c's dark dragon inert -
    // Roles::IsPorted is per-role and cannot say "for two of them".
    CHECK(!Roles::IsPorted(Roles::kBossSpawn));
}

void runTests() {
    TheRulesRead();
    FourSlabsAcrossThreeLevels();
    EverySlabIsKinematic();
    ItBeginsAtFullDisplacementNotAtItsNode();
    ItBobsAndComesBack();
    TheBodyItselfMoves();
    TheRoleIsPlayed();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_bounce: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_bounce", 40);
}
