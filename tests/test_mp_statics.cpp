// Static portals, on the levels that ship them: level0's two pairs, and level1's
// single portal whose partner is the one the player places.
//
// What the files say is pinned exactly: each static portal's index, destiny,
// colour and place, and the level's portal budget. What happens is judged by
// threshold: a traveller comes out within reach of its exit. That static portals
// stay after use is owner testimony (11 September) and portals.json's
// static_portals_persist; the other setting is run too.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Portals.hpp"
#include "sim/Roles.hpp"
#include "sim/Units.hpp"

#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// Where a traveller comes out: portals.json's exit_offset_px (12, a guess) from
// the exit, plus what one physics step moves it. Judged, not pinned.
constexpr double kArrivalPx = 20.0;

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-statics", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run, float direction = 0.0f) {
    Game::Tick(run.data, run.registry, run.level, direction, kStep);
}

void PutPlayer(Run& run, const glm::dvec2& atPx) {
    auto& transform = run.registry.get<TransformComponent>(run.level.player);
    const glm::vec3 at = Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);
    run.registry.get<RigidBodyComponent>(run.level.player).velocity = glm::vec3(0.0f);
}

glm::dvec2 PlayerPx(const Run& run) {
    return Units::ToPixels(run.registry.get<TransformComponent>(run.level.player).position);
}

const Portals::Static* StaticWithIndex(const Run& run, int index) {
    for (const Portals::Static& portal : run.level.portals.statics) {
        if (portal.index == index) return &portal;
    }
    return nullptr;
}

std::string Px(const glm::dvec2& at) {
    char text[48];
    std::snprintf(text, sizeof text, "(%.1f, %.1f)", at.x, at.y);
    return text;
}

// ---- What the levels say ------------------------------------------------------

void Level0ShipsTwoPairs() {
    Run run;
    if (!Begin("level0", run)) return;
    // Read off level0.tscn by grep: four portal_static nodes, every one active,
    // and a properties node with max_portals "0".
    struct Expected {
        const char* name;
        int index;
        int destiny;
        const char* colour;
        glm::dvec2 atPx;
    };
    const Expected expected[] = {
        {"portal_static_604", 1, 0, "blue", {74.0, 166.0}},
        {"portal_static_603", 0, 1, "red", {150.0, 74.0}},
        {"portal_static_582", 2, 3, "red", {334.0, 200.0}},
        {"portal_static_583", 3, 2, "blue", {498.0, 200.0}},
    };
    const auto& statics = run.level.portals.statics;
    CHECK_EQ(statics.size(), std::size(expected));
    for (std::size_t i = 0; i < statics.size() && i < std::size(expected); ++i) {
        CHECK_MSG(statics[i].name == expected[i].name, statics[i].name);
        CHECK_EQ(statics[i].index, expected[i].index);
        CHECK(statics[i].hasDestiny);
        CHECK_EQ(statics[i].destiny, expected[i].destiny);
        CHECK_MSG(statics[i].colour == expected[i].colour, statics[i].name + " is " + statics[i].colour);
        CHECK(statics[i].atPx == expected[i].atPx);
        CHECK(statics[i].live);
    }
    // No placement at all: level0 is solved with the portals it ships.
    CHECK_EQ(run.level.portals.budget, 0);
    CHECK(!run.level.portals.TryPlace(glm::dvec2(400.0, 150.0)));
    CHECK_EQ(run.level.portals.portalsUsed, 0);
}

// ---- Travelling ----------------------------------------------------------------

void AStaticPortalSendsThePlayerToItsPartnerAndStays() {
    Run run;
    if (!Begin("level0", run)) return;
    const Portals::Static* from = StaticWithIndex(run, 2);
    const Portals::Static* to = StaticWithIndex(run, 3);
    if (from == nullptr || to == nullptr) return;
    const glm::dvec2 toPx = to->atPx;

    PutPlayer(run, from->atPx);
    Tick(run);
    CHECK_EQ(run.level.portals.traversals, 1);
    const double arrived = glm::distance(PlayerPx(run), toPx);
    CHECK_MSG(arrived < kArrivalPx, "the player is at " + Px(PlayerPx(run)) + ", the exit at " + Px(toPx));
    for (const Portals::Static& portal : run.level.portals.statics) CHECK_MSG(portal.live, portal.name);

    // Put down inside its exit, it does not go back while it stays there - an
    // entry is an edge, as body_entered is - nor once the lockout has run out.
    for (int tick = 0; tick < 90; ++tick) Tick(run);
    CHECK_EQ(run.level.portals.traversals, 1);
}

void WithStaticPortalsSpentBothEndsGo() {
    Run run;
    if (!Begin("level0", run)) return;
    run.level.portals.rules.staticPortalsPersist = false;
    const Portals::Static* from = StaticWithIndex(run, 2);
    if (from == nullptr) return;
    PutPlayer(run, from->atPx);
    Tick(run);
    CHECK_EQ(run.level.portals.traversals, 1);
    CHECK(!StaticWithIndex(run, 2)->live);
    CHECK(!StaticWithIndex(run, 3)->live);
    CHECK(StaticWithIndex(run, 0)->live);
    CHECK(StaticWithIndex(run, 1)->live);
}

void Level1sPortalLeadsToThePlacedOne() {
    // level1 ships one static portal, index 0 with destiny 1, and grants one
    // placement. No static portal has index 1.
    {
        Run run;
        if (!Begin("level1", run)) return;
        const auto& statics = run.level.portals.statics;
        CHECK_EQ(statics.size(), std::size_t{1});
        if (statics.size() != 1) return;
        CHECK_EQ(statics[0].index, 0);
        CHECK_EQ(statics[0].destiny, 1);
        CHECK(StaticWithIndex(run, 1) == nullptr);
        CHECK_EQ(run.level.portals.budget, 1);

        // With nothing placed it leads nowhere.
        PutPlayer(run, statics[0].atPx);
        Tick(run);
        CHECK_EQ(run.level.portals.traversals, 0);
        CHECK(glm::distance(PlayerPx(run), statics[0].atPx) < kArrivalPx);
    }
    {
        Run run;
        if (!Begin("level1", run)) return;
        const Portals::Static& portal = run.level.portals.statics.front();
        // Above the spawn, clear of the player standing there.
        const glm::dvec2 spawnPx = PlayerPx(run);
        const glm::dvec2 placedPx = spawnPx + glm::dvec2(0.0, -48.0);
        CHECK(run.level.portals.TryPlace(placedPx));

        PutPlayer(run, portal.atPx);
        Tick(run);
        CHECK_EQ(run.level.portals.traversals, 1);
        CHECK_MSG(glm::distance(PlayerPx(run), placedPx) < kArrivalPx,
                  "the player is at " + Px(PlayerPx(run)) + ", the placed portal at " + Px(placedPx));
        // The placed end is spent, the static end stays.
        CHECK(run.level.portals.placed.empty());
        CHECK(run.level.portals.statics.front().live);
    }
}

// ---- level0, played ------------------------------------------------------------

void Level0FromTheSpawnHoldingRight() {
    Run run;
    if (!Begin("level0", run)) return;
    std::string trace;
    int tick = 0;
    for (; tick < 1200 && !run.level.goals.completed; ++tick) {
        Tick(run, 1.0f);
        if (tick % 30 == 0) {
            trace += " " + std::to_string(tick) + ":" + Px(PlayerPx(run)) + "/" +
                     std::to_string(run.level.portals.traversals);
        }
    }
    std::printf("  level0 holding right: %s after %.2f s, %d traversal(s)\n",
                run.level.goals.completed ? "completed" : "NOT completed", tick * kStep,
                run.level.portals.traversals);
    CHECK_MSG(run.level.goals.completed, "tick:(x, y)/traversals" + trace);
    CHECK(run.level.portals.traversals >= 1);
    CHECK_EQ(run.level.portals.portalsUsed, 0);
}

void runTests() {
    Level0ShipsTwoPairs();
    AStaticPortalSendsThePlayerToItsPartnerAndStays();
    WithStaticPortalsSpentBothEndsGo();
    Level1sPortalLeadsToThePlacedOne();
    Level0FromTheSpawnHoldingRight();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_statics: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_statics", 40);
}
