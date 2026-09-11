// What moves in a level besides its doors: moving platforms and lifts, and the
// no-portal zones that patrol.
//
// Pinned exactly, because the files say it: which of chapter 1's levels have
// which, where each stands, its speed, its stride and its end markers. Judged by
// threshold: a platform swings by its stride with the velocity that gets it
// there, a lift reaches its far marker when its speed says, and a patrolling
// zone refuses a tap where it is and not where it has left. The numbers behind
// the speeds - movers.json's rate scale and lift speed - are the remake's
// guesses, carried as data, so the thresholds are computed from them.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Mover.hpp"
#include "sim/Portals.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;
constexpr double kPi = 3.14159265358979323846;

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-movers", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

glm::dvec2 BodyPx(const Run& run, entt::entity entity) {
    return Units::ToPixels(run.registry.get<TransformComponent>(entity).position);
}

std::string Px(const glm::dvec2& p) {
    char text[48];
    std::snprintf(text, sizeof text, "(%.2f, %.2f)", p.x, p.y);
    return text;
}

// ---- What the levels say ------------------------------------------------------

void MoversJsonCarriesTheRemakesGuesses() {
    Mover::Rules rules;
    std::string error;
    CHECK_MSG(Mover::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/movers.json", rules, error), error);
    // Guesses, carried and not pinned: only that there is one of each.
    CHECK(rules.oscillationRateScale > 0.0);
    CHECK(rules.liftSpeedPx > 0.0);
}

void ChapterOnesMovers() {
    // Read off the level files with grep.
    {
        Run run;
        if (Begin("level5", run)) {
            const auto& platforms = run.level.movers.platforms;
            CHECK_EQ(platforms.size(), std::size_t{1});
            if (platforms.size() == 1) {
                CHECK(platforms[0].name == "moving_platform_764");
                CHECK(platforms[0].motion.originPx == glm::dvec2(416.0, 144.0));
                CHECK(platforms[0].motion.speed == 0.7);
                CHECK(platforms[0].motion.stridePx == 64.0);
                CHECK(platforms[0].motion.axis == glm::dvec2(0.0, 1.0));
            }
            CHECK(run.level.movers.lifts.empty());
        }
    }
    {
        Run run;
        if (Begin("level10", run)) {
            const Mover::Platform* platform = run.level.movers.FindPlatform("moving_platform_707");
            CHECK(platform != nullptr && platform->motion.originPx == glm::dvec2(32.0, 184.0) &&
                  platform->motion.speed == 1.0 && platform->motion.stridePx == 70.0);
            const Portals::State& portals = run.level.portals;
            const Portals::NoPortalZone* up = portals.FindZone("anti_portal_agent_639");
            const Portals::NoPortalZone* side = portals.FindZone("anti_portal_agent_599");
            const Portals::NoPortalZone* still = portals.FindZone("anti_portal_agent_710");
            CHECK(up != nullptr && up->centrePx == glm::dvec2(674.0, 60.0) && up->moving &&
                  up->motion.axis == glm::dvec2(0.0, 1.0) && up->motion.speed == 3.0 && up->motion.stridePx == 48.0);
            CHECK(side != nullptr && side->centrePx == glm::dvec2(384.0, 80.0) && side->moving &&
                  side->motion.axis == glm::dvec2(1.0, 0.0) && side->motion.speed == 2.0 &&
                  side->motion.stridePx == 36.0);
            CHECK(still != nullptr && still->centrePx == glm::dvec2(128.0, 100.0) && !still->moving);
            // And each agent stands on a static antiportal of its own.
            struct Field {
                const char* name;
                glm::dvec2 at;
                double scale;
            };
            const Field fields[] = {
                {"antiportal_638", {674.0, 60.0}, 3.5},
                {"antiportal_574", {384.0, 80.0}, 3.5},
                {"antiportal_709", {128.0, 100.0}, 6.0},
            };
            for (const Field& expected : fields) {
                const Portals::NoPortalZone* field = portals.FindZone(expected.name);
                CHECK_MSG(field != nullptr && field->centrePx == expected.at && field->scale == expected.scale &&
                              !field->moving,
                          expected.name);
            }
        }
    }
    struct Shuttles {
        const char* level;
        const char* lift;
        glm::dvec2 a;
        glm::dvec2 b;
    };
    const Shuttles lifts[] = {
        {"level26", "lift_1186", {544.0, 240.0}, {544.0, 144.0}},
        {"level26", "lift_ent_1122", {96.0, 80.0}, {96.0, 240.0}},
        {"level27", "lift_ent_694", {96.0, 240.0}, {96.0, 112.0}},
        {"level28", "lift_ent_796", {96.0, 240.0}, {96.0, 112.0}},
    };
    for (const Shuttles& expected : lifts) {
        Run run;
        if (!Begin(expected.level, run)) continue;
        const Mover::Lift* lift = run.level.movers.FindLift(expected.lift);
        CHECK_MSG(lift != nullptr, std::string(expected.level) + " has " + expected.lift);
        if (lift == nullptr) continue;
        CHECK_MSG(lift->motion.aPx == expected.a && lift->motion.bPx == expected.b && lift->motion.atPx == expected.a,
                  std::string(expected.lift) + " runs " + Px(lift->motion.aPx) + " to " + Px(lift->motion.bPx));
        CHECK(lift->motion.speedPx == run.data.movers.liftSpeedPx);
    }
}

// ---- Moving -------------------------------------------------------------------

void APlatformSwingsByItsStride() {
    Run run;
    if (!Begin("level5", run) || run.level.movers.platforms.empty()) return;
    const Mover::Platform& platform = run.level.movers.platforms.front();
    const double period = 2.0 * kPi / (run.data.movers.oscillationRateScale * platform.motion.speed);
    const int ticks = static_cast<int>(std::ceil(period / kStep)) + 1;
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    bool where = true;
    bool upright = true;
    bool velocity = true;
    for (int i = 0; i < ticks; ++i) {
        const glm::vec3 before = run.registry.get<TransformComponent>(platform.entity).position;
        Tick(run);
        const glm::vec3 after = run.registry.get<TransformComponent>(platform.entity).position;
        const glm::dvec2 at = BodyPx(run, platform.entity);
        lo = std::min(lo, at.y);
        hi = std::max(hi, at.y);
        where = where && glm::distance(at, platform.motion.AtPx()) < 0.01;
        upright = upright && std::fabs(at.x - 416.0) < 0.01;
        // It moves with the velocity that gets it there, as a mover must to carry
        // what stands on it (Mover.hpp).
        const glm::vec3 v = run.registry.get<RigidBodyComponent>(platform.entity).velocity;
        velocity = velocity && glm::length(v - (after - before) / kStep) < 1e-3f;
    }
    std::printf("  level5's platform: swung %.2f px over %.2f s, from y %.2f to %.2f\n", hi - lo, period, lo, hi);
    CHECK_MSG(hi - lo > 63.0 && hi - lo <= 64.0 + 1e-6, "a swing of its stride, 64 px");
    CHECK(where);
    CHECK(upright);
    CHECK(velocity);
}

void ALiftGoesToItsFarMarkerAndBack() {
    Run run;
    if (!Begin("level27", run) || run.level.movers.lifts.empty()) return;
    const Mover::Lift& lift = run.level.movers.lifts.front();
    const glm::dvec2 a = lift.motion.aPx;
    const glm::dvec2 b = lift.motion.bPx;
    const double legTicks = glm::distance(a, b) / lift.motion.speedPx / kStep;
    int atB = -1;
    int backAtA = -1;
    bool where = true;
    for (int tick = 1; tick <= static_cast<int>(legTicks * 3.0) && backAtA < 0; ++tick) {
        Tick(run);
        where = where && glm::distance(BodyPx(run, lift.entity), lift.motion.atPx) < 0.01;
        if (atB < 0 && lift.motion.atPx == b) atB = tick;
        if (atB > 0 && backAtA < 0 && lift.motion.atPx == a) backAtA = tick;
    }
    std::printf("  level27's lift: at its far marker after %d ticks and back after %d, for %.1f a leg\n", atB, backAtA,
                legTicks);
    CHECK_MSG(atB > 0 && std::fabs(atB - legTicks) <= 1.0, "at b when its speed says");
    CHECK_MSG(backAtA > 0 && std::fabs(backAtA - 2.0 * legTicks) <= 2.0, "and back at a");
    CHECK(where);
}

void APatrollingZoneRefusesWhereItIs() {
    Run run;
    if (!Begin("level10", run)) return;
    const Portals::NoPortalZone* agent = run.level.portals.FindZone("anti_portal_agent_639");
    const Portals::NoPortalZone* field = run.level.portals.FindZone("antiportal_638");
    CHECK(agent != nullptr && agent->moving);
    CHECK(field != nullptr && !field->moving);
    if (agent == nullptr || field == nullptr) return;
    const double radius = run.level.portals.rules.collisionRadiusPx;
    const glm::dvec2 stood(674.0, 60.0);

    // It swings: a quarter of its period on, it is half its stride from where
    // the level placed it, down the screen.
    const double quarter = kPi / 2.0 / (run.data.movers.oscillationRateScale * agent->motion.speed);
    const int ticks = static_cast<int>(std::lround(quarter / kStep));
    for (int i = 0; i < ticks; ++i) Tick(run);
    const glm::dvec2 now = agent->CentreNowPx();
    CHECK_MSG(now.x == 674.0 && std::fabs(now.y - (60.0 + 24.0)) < 0.5, "at " + Px(now));

    // A tap meets it where it is now, not where it stood: with this zone alone,
    // where it is is refused and where it stood is free.
    Portals::State alone;
    alone.rules = run.level.portals.rules;
    alone.budget = Portals::kPairSize;
    alone.zones.push_back(*agent);
    CHECK(!alone.TryPlace(now));
    CHECK(alone.TryPlace(stood));

    // In level10 itself the patrol changes nothing. The agent stands on
    // antiportal_638, whose field holds its whole swing and its own radius, so
    // where it stood is refused all the while. That is the remake's data, as it is.
    CHECK(agent->motion.stridePx * 0.5 + radius * agent->scale < radius * field->scale);
    CHECK(!run.level.portals.TryPlace(stood));

    // The sideways one moves along x and only x.
    const Portals::NoPortalZone* side = run.level.portals.FindZone("anti_portal_agent_599");
    CHECK(side != nullptr && side->CentreNowPx().y == 80.0 && side->CentreNowPx().x != 384.0);
}

void runTests() {
    MoversJsonCarriesTheRemakesGuesses();
    ChapterOnesMovers();
    APlatformSwingsByItsStride();
    ALiftGoesToItsFarMarkerAndBack();
    APatrollingZoneRefusesWhereItIs();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_movers: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_movers", 30);
}
