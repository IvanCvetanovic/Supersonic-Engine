// The portal shot: a tap fires, and the portal opens where the shot arrives.
//
// What the owner said on 11 September is tested as it stands:
//   - the shot flies from the character toward the tap, and the portal opens
//     where it arrives;
//   - a wall on the way stops it, and so does a crate or a stone;
//   - a failed shot costs nothing.
// What the level files say is pinned: level6's projectile blocker and its box.
// shot.json's speed is a guess, carried and not pinned, so flight times are
// judged against whatever it holds. The segment test is checked on its own
// against a turned box, a hull's polygon and a circle, where a test by bounds
// would be wrong.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Shot.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::BoxColliderComponent;
using Supersonic::ConvexHullColliderComponent;
using Supersonic::RigidBodyComponent;
using Supersonic::SphereColliderComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-shot", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run) { Game::Tick(run.data, run.registry, run.level, 0.0f, kStep); }

void PutPlayer(Run& run, const glm::dvec2& atPx) {
    auto& transform = run.registry.get<TransformComponent>(run.level.player);
    const glm::vec3 at = Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);
    run.registry.get<RigidBodyComponent>(run.level.player).velocity = glm::vec3(0.0f);
}

glm::dvec2 PlayerPx(const Run& run) {
    return Units::ToPixels(run.registry.get<TransformComponent>(run.level.player).position);
}

std::string Px(const glm::dvec2& at) {
    char text[48];
    std::snprintf(text, sizeof text, "(%.1f, %.1f)", at.x, at.y);
    return text;
}

// Ticks until the shot in flight lands or fails. Returns how many it took.
int Fly(Run& run) {
    int ticks = 0;
    while (run.level.portals.flight && ticks < 600) {
        Tick(run);
        ++ticks;
    }
    return ticks;
}

// ---- What the levels say ------------------------------------------------------

void Level6sBlocker() {
    // Read off level6.tscn with grep: one anti_projectile_wall.ent, with a
    // trigger_size of 64 x 128 and no offset.
    Run run;
    if (!Begin("level6", run)) return;
    const auto& blockers = run.level.portals.blockers;
    CHECK_EQ(blockers.size(), std::size_t{1});
    if (blockers.size() != 1) return;
    CHECK_MSG(blockers[0].name == "anti_projectile_wall_ent_743", blockers[0].name);
    const glm::dvec2 centre = Units::ToPixels(glm::vec3(blockers[0].box.centre, 0.0f));
    const glm::dvec2 size = glm::dvec2(blockers[0].box.half) * 2.0 * Units::kPixelsPerMetre;
    CHECK_MSG(glm::distance(centre, glm::dvec2(313.0, 185.0)) < 1e-3 && std::fabs(size.x - 64.0) < 1e-3 &&
                  std::fabs(size.y - 128.0) < 1e-3,
              Px(size) + " at " + Px(centre));
}

// ---- Flying -------------------------------------------------------------------

void AShotOpensAPortalWhereItArrives() {
    // level1: straight up from the spawn, into open air.
    Run run;
    if (!Begin("level1", run)) return;
    const glm::dvec2 from = PlayerPx(run);
    const glm::dvec2 aim = from + glm::dvec2(0.0, -48.0);
    CHECK(run.level.portals.Shoot(run.registry, aim));
    CHECK(run.level.portals.flight.has_value());
    CHECK_EQ(run.level.portals.shotsFired, 1);
    // Nothing opens at the tap; the shot has to get there.
    CHECK(run.level.portals.placed.empty());
    CHECK_EQ(run.level.portals.portalsUsed, 0);
    // One at a time.
    CHECK(!run.level.portals.Shoot(run.registry, aim));
    CHECK_EQ(run.level.portals.shotsFired, 1);

    const int ticks = Fly(run);
    const int expected = static_cast<int>(std::ceil(48.0 / (run.data.shot.speedPx * kStep)));
    std::printf("  level1: a shot 48 px up landed after %d tick(s); shot.json says %d\n", ticks, expected);
    CHECK_MSG(std::abs(ticks - expected) <= 1, "landed after " + std::to_string(ticks) + " ticks");
    CHECK_MSG(run.level.portals.placed.size() == 1, "failed on " + run.level.portals.lastFailure);
    if (run.level.portals.placed.size() != 1) return;
    CHECK(glm::distance(run.level.portals.placed[0].atPx, aim) < 1e-9);
    CHECK_EQ(run.level.portals.portalsUsed, 1);
    CHECK_EQ(run.level.portals.shotsFailed, 0);
}

void AWallStopsIt() {
    // level1: straight down from the spawn, through the floor it stands on.
    Run run;
    if (!Begin("level1", run)) return;
    for (int tick = 0; tick < 60; ++tick) Tick(run);
    const glm::dvec2 from = PlayerPx(run);
    CHECK(run.level.portals.Shoot(run.registry, from + glm::dvec2(0.0, 120.0)));
    Fly(run);
    std::printf("  level1: a shot down through the floor stopped at %s\n", run.level.portals.lastFailure.c_str());
    CHECK(run.level.portals.placed.empty());
    CHECK_EQ(run.level.portals.shotsFailed, 1);
    CHECK_MSG(!run.level.portals.lastFailure.empty() && run.level.portals.lastFailure != "the tap, where no portal may open",
              run.level.portals.lastFailure);
    // It cost nothing: the level's one portal is still there to be had.
    CHECK_EQ(run.level.portals.portalsUsed, 0);
    CHECK(run.level.portals.Shoot(run.registry, PlayerPx(run) + glm::dvec2(0.0, -48.0)));
    Fly(run);
    CHECK_MSG(run.level.portals.placed.size() == 1, "the second failed on " + run.level.portals.lastFailure);
}

void AStoneStopsIt() {
    // level8: from the spawn at the hint arrow behind the stone. The stone,
    // rolling_stone_ent_597, is in the way.
    Run run;
    if (!Begin("level8", run)) return;
    CHECK(run.level.portals.Shoot(run.registry, glm::dvec2(30.0, 106.0)));
    Fly(run);
    CHECK(run.level.portals.placed.empty());
    CHECK_MSG(run.level.portals.lastFailure == "rolling_stone_ent_597", run.level.portals.lastFailure);
    CHECK_EQ(run.level.portals.portalsUsed, 0);
}

void TheBlockerStopsIt() {
    // level6: a shot level across anti_projectile_wall_ent_743 (x 281 to 345)
    // dies in it. With the blocker taken away the same shot lands, so it was the
    // blocker.
    for (const bool withBlocker : {true, false}) {
        Run run;
        if (!Begin("level6", run)) return;
        if (!withBlocker) run.level.portals.blockers.clear();
        PutPlayer(run, glm::dvec2(262.0, 150.0));
        CHECK(run.level.portals.Shoot(run.registry, glm::dvec2(380.0, 150.0)));
        Fly(run);
        const bool landed = run.level.portals.placed.size() == 1;
        CHECK_MSG(landed != withBlocker, std::string(withBlocker ? "with" : "without") + " the blocker: " +
                                             (landed ? "landed" : "stopped at " + run.level.portals.lastFailure));
        if (withBlocker) {
            CHECK_MSG(run.level.portals.lastFailure == "anti_projectile_wall_ent_743", run.level.portals.lastFailure);
        }
    }
}

void ANoPortalZoneAtTheTapFailsIt() {
    // level6's antiportal_687 at (288, 192): a shot down into it, from above and
    // with the blocker out of the way, arrives and opens nothing.
    Run run;
    if (!Begin("level6", run)) return;
    run.level.portals.blockers.clear();
    PutPlayer(run, glm::dvec2(288.0, 150.0));
    CHECK(run.level.portals.Shoot(run.registry, glm::dvec2(288.0, 192.0)));
    Fly(run);
    CHECK(run.level.portals.placed.empty());
    CHECK_EQ(run.level.portals.shotsFailed, 1);
    CHECK_MSG(run.level.portals.lastFailure == "the tap, where no portal may open", run.level.portals.lastFailure);
    CHECK_EQ(run.level.portals.portalsUsed, 0);
}

void NoPortalsNoShot() {
    // level0 grants no placement: a tap fires nothing.
    Run run;
    if (!Begin("level0", run)) return;
    CHECK(!run.level.portals.Shoot(run.registry, PlayerPx(run) + glm::dvec2(0.0, -48.0)));
    CHECK_EQ(run.level.portals.shotsFired, 0);
    CHECK(!run.level.portals.flight);
}

// ---- The segment test, on its own ---------------------------------------------

void ExactInThePlane() {
    // A 100 px square turned by 45 degrees, centred at the origin: a diamond
    // whose corners are 70.7 px out along the axes. Its bounds reach 70.7 px in
    // x and y, so a segment near (60, 60) lies inside them and outside it.
    entt::registry registry;
    const entt::entity square = registry.create();
    auto& transform = registry.emplace<TransformComponent>(square);
    transform.rotation.z = 0.785398163f;
    registry.emplace<BoxColliderComponent>(square).size = glm::vec3(2.0f, 2.0f, 2.0f);

    const auto hit = [&registry](const glm::dvec2& from, const glm::dvec2& to, entt::entity ignore = entt::null) {
        return Shot::FirstBody(registry, from, to, ignore);
    };
    CHECK_MSG(!hit({65.0, -65.0}, {50.0, -50.0}), "inside the diamond's bounds, outside the diamond: no hit");
    const auto into = hit({65.0, -65.0}, {30.0, -30.0});
    CHECK(into.has_value() && into->body == square);
    // |x| + |y| reaches 70.71 at x = 35.36, which is 29.64 px of the 35 along.
    if (into) CHECK_MSG(std::fabs(into->along - (65.0 - 35.3553) / 35.0) < 1e-4, std::to_string(into->along));
    CHECK_MSG(!hit({65.0, -65.0}, {30.0, -30.0}, square), "what the shot leaves from is not met");

    // A trigger stops nothing.
    registry.get<BoxColliderComponent>(square).isTrigger = true;
    CHECK(!hit({65.0, -65.0}, {0.0, 0.0}));

    // A hull, by the polygon LevelBuilder records for it: a triangle, (0, 0),
    // (100, 0) and (0, 100) in pixels, at (200, 0).
    const entt::entity triangle = registry.create();
    registry.emplace<TransformComponent>(triangle).position = Units::ToWorld(200.0, 0.0);
    registry.emplace<ConvexHullColliderComponent>(triangle);
    auto& outline = registry.emplace<LevelBuilder::PlanePolygon>(triangle);
    outline.points = {glm::vec2(0.0f, 0.0f), glm::vec2(2.0f, 0.0f), glm::vec2(0.0f, -2.0f)};
    CHECK_MSG(!hit({280.0, 80.0}, {260.0, 60.0}), "past the triangle's long side: no hit");
    const auto side = hit({280.0, 80.0}, {220.0, 20.0});
    CHECK(side.has_value() && side->body == triangle);
    // The long side is x + y = 300 (in pixels from the triangle's corner at 200):
    // (280, 80) - t(60, 60) meets x - 200 + y = 100 at t = 60/120.
    if (side) CHECK_MSG(std::fabs(side->along - 0.5) < 1e-4, std::to_string(side->along));

    // A circle of 30 px at (0, 200): a segment passing 31 px from its centre
    // misses, one passing through it meets it where it first reaches 30 px.
    const entt::entity ball = registry.create();
    registry.emplace<TransformComponent>(ball).position = Units::ToWorld(0.0, 200.0);
    registry.emplace<SphereColliderComponent>(ball).radius = Units::ToMetres(30.0);
    CHECK(!hit({-100.0, 231.0}, {100.0, 231.0}));
    const auto through = hit({-100.0, 200.0}, {100.0, 200.0});
    CHECK(through.has_value() && through->body == ball);
    if (through) CHECK_MSG(std::fabs(through->along - 70.0 / 200.0) < 1e-4, std::to_string(through->along));
}

// ---- Reflectors ------------------------------------------------------------------

std::string Where(const glm::dvec2& p) { return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")"; }

void TheReflectorsAreTheLevelsOwn() {
    // Pinned to the files: level 1-12's three, all horizontal, and 1-13's one,
    // vertical.
    Run eleven;
    if (Begin("level11", eleven)) {
        const std::vector<Portals::Reflector>& reflectors = eleven.level.portals.reflectors;
        CHECK_EQ(reflectors.size(), std::size_t{3});
        struct Expected {
            const char* name;
            double x;
            double y;
        };
        bool pinned = reflectors.size() == 3;
        for (const Expected& e : {Expected{"reflect_agent_675", 638.0, 26.0}, Expected{"reflect_agent_761", 180.0, 140.0},
                                  Expected{"reflect_agent_623", 422.0, 115.0}}) {
            bool found = false;
            for (const Portals::Reflector& r : reflectors) {
                if (r.name == e.name && r.centrePx == glm::dvec2(e.x, e.y) && !r.vertical) found = true;
            }
            if (!found) pinned = false;
        }
        CHECK_MSG(pinned, "level11's three reflectors, horizontal, where the file puts them");
    }
    Run twelve;
    if (Begin("level12", twelve)) {
        const std::vector<Portals::Reflector>& reflectors = twelve.level.portals.reflectors;
        CHECK_MSG(reflectors.size() == 1 && reflectors[0].name == "reflect_agent_584" &&
                      reflectors[0].centrePx == glm::dvec2(248.0, 149.0) && reflectors[0].vertical,
                  "level12's one, vertical");
    }
}

// A shot in an empty plane from (100, 200), with the rules a level loads and the
// reflectors a test puts down.
struct Rig {
    entt::registry registry;
    Portals::State state;
};

bool MakeRig(Rig& rig) {
    Run run;
    if (!Begin("level1", run)) return false;
    rig.state.rules = run.data.portals;
    rig.state.shot = run.data.shot;
    rig.state.budget = 2;
    rig.state.shooter = rig.registry.create();
    rig.registry.emplace<TransformComponent>(rig.state.shooter).position = Units::ToWorld(100.0, 200.0);
    return true;
}

void FlyOut(Rig& rig) {
    for (int tick = 0; tick < 600 && rig.state.flight; ++tick) rig.state.Tick(rig.registry, kStep);
}

void AReflectorTurnsAShotMirrored() {
    // A horizontal reflector at (200, 100), on the line from (100, 200) to a tap
    // at (300, 0). The shot comes within the catch radius at E, turns its y back,
    // and goes on for the rest of its way: the portal opens at the tap mirrored
    // across the line through E, at the end of a way as long as the one fired.
    Rig rig;
    if (!MakeRig(rig)) return;
    rig.state.shot.maxReflections = 1;
    rig.state.reflectors.push_back({"mirror", glm::dvec2(200.0, 100.0), false});
    CHECK(rig.state.Shoot(rig.registry, glm::dvec2(300.0, 0.0)));
    FlyOut(rig);
    const double k = rig.state.shot.reflectRadiusPx / std::sqrt(2.0);
    const glm::dvec2 entry(200.0 - k, 100.0 + k);
    const glm::dvec2 expected(300.0, 2.0 * entry.y);
    CHECK_EQ(rig.state.reflections, 1);
    CHECK_MSG(rig.state.placed.size() == 1, "it opened a portal: " + rig.state.lastFailure);
    if (rig.state.placed.empty()) return;
    const glm::dvec2 at = rig.state.placed[0].atPx;
    CHECK_MSG(glm::distance(at, expected) < 1e-6, Where(at) + " for " + Where(expected));
    const double way = glm::distance(glm::dvec2(100.0, 200.0), entry) + glm::distance(entry, at);
    const double fired = glm::distance(glm::dvec2(100.0, 200.0), glm::dvec2(300.0, 0.0));
    CHECK_MSG(std::fabs(way - fired) < 1e-6, "a way of " + std::to_string(way) + " for " + std::to_string(fired));

    // Without the reflector, the same tap opens at the tap.
    Rig bare;
    if (!MakeRig(bare)) return;
    CHECK(bare.state.Shoot(bare.registry, glm::dvec2(300.0, 0.0)));
    FlyOut(bare);
    CHECK_MSG(bare.state.placed.size() == 1 && glm::distance(bare.state.placed[0].atPx, glm::dvec2(300.0, 0.0)) < 1e-6,
              "no reflector, no turn");
}

void AShotComesOffOneReflectorAtMost() {
    // Two vertical reflectors on one line, at x 200 and 60, and a shot from 100
    // toward 400. The first turns it back toward the second. Allowed one
    // reflection - shot.json's reading of hasBeenReflected - it passes the second
    // and opens at x -32. Allowed two, the second turns it again, and it opens at
    // x 184, where its way runs out.
    for (const int allowed : {1, 2}) {
        Rig rig;
        if (!MakeRig(rig)) return;
        rig.state.shot.reflectRadiusPx = 16.0;
        rig.state.shot.maxReflections = allowed;
        rig.state.reflectors.push_back({"first", glm::dvec2(200.0, 200.0), true});
        rig.state.reflectors.push_back({"second", glm::dvec2(60.0, 200.0), true});
        CHECK(rig.state.Shoot(rig.registry, glm::dvec2(400.0, 200.0)));
        FlyOut(rig);
        CHECK_EQ(rig.state.reflections, allowed);
        const double expectedX = allowed == 1 ? -32.0 : 184.0;
        const bool landed = rig.state.placed.size() == 1 &&
                            glm::distance(rig.state.placed[0].atPx, glm::dvec2(expectedX, 200.0)) < 1e-6;
        CHECK_MSG(landed, std::to_string(allowed) + " allowed: " +
                              (rig.state.placed.empty() ? rig.state.lastFailure : Where(rig.state.placed[0].atPx)));
    }
    // shot.json allows one, as its note says; the test above holds either way.
    Rig rig;
    if (MakeRig(rig)) CHECK(rig.state.shot.maxReflections >= 0 && rig.state.shot.reflectRadiusPx > 0.0);
}

void AReflectorWithNoPlaneTakesTheDefault() {
    // level26a's reflect_agent_ent_961 is the one placement in the game that
    // gives no plane. The level starts, and the reflector takes shot.json's
    // default, whichever way it is set.
    for (const bool vertical : {true, false}) {
        Run run;
        std::string error;
        bool ok = Game::LoadData(kLevels + "/level26a.tscn", kData,
                                 std::filesystem::temp_directory_path() / "supersonic-test-mp-shot", run.data, error);
        if (ok) {
            run.data.shot.defaultPlaneVertical = vertical;
            ok = Game::Start(run.data, run.registry, run.level, error);
        }
        CHECK_MSG(ok, "level26a: " + error);
        if (!ok) continue;
        const Portals::Reflector* found = nullptr;
        for (const Portals::Reflector& r : run.level.portals.reflectors) {
            if (r.name == "reflect_agent_ent_961") found = &r;
        }
        CHECK_MSG(found != nullptr && found->vertical == vertical,
                  std::string("the default, ") + (vertical ? "vertical" : "horizontal"));
    }
}

void EnteringACircle() {
    const auto across = Shot::EntersCircle({0.0, 0.0}, {100.0, 0.0}, {50.0, 0.0}, 10.0);
    CHECK_MSG(across && std::fabs(*across - 0.4) < 1e-12, "met where it first comes within 10 px");
    CHECK_MSG(!Shot::EntersCircle({0.0, 0.0}, {100.0, 0.0}, {50.0, 11.0}, 10.0), "passing 11 px off, never");
    CHECK_MSG(!Shot::EntersCircle({0.0, 0.0}, {30.0, 0.0}, {50.0, 0.0}, 10.0), "stopping short, never");
    const auto inside = Shot::EntersCircle({50.0, 5.0}, {100.0, 0.0}, {50.0, 0.0}, 10.0);
    CHECK_MSG(inside && *inside == 0.0, "starting within it, at once");
}

void runTests() {
    Level6sBlocker();
    AShotOpensAPortalWhereItArrives();
    AWallStopsIt();
    AStoneStopsIt();
    TheBlockerStopsIt();
    ANoPortalZoneAtTheTapFailsIt();
    NoPortalsNoShot();
    ExactInThePlane();
    TheReflectorsAreTheLevelsOwn();
    AReflectorTurnsAShotMirrored();
    AShotComesOffOneReflectorAtMost();
    EnteringACircle();
    AReflectorWithNoPlaneTakesTheDefault();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_shot: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_shot", 30);
}
