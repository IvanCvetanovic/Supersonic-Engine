// The diamonds: two pickups that share a role and a carry path and do entirely
// different things.
//
// What is pinned here is the DECODE, and above all three things a careless port
// gets wrong:
//
//   selection  shock_diamond.ent and fire_diamond.ent share the `pickup` role in
//              entity_roles.json and are different mechanisms. Selecting on the
//              role would hand a fire diamond the shock one's behaviour, so the
//              four chapter-3 levels that place a fire diamond are asserted to
//              yield a diamond that is FIRE and strikes nothing.
//   the frame  the pickup and the payload are a frame apart. Both callbacks
//              branch on ownerID at the top; the unowned arm polls for a carrier,
//              writes ownerID and RETURNS, and the payload lives in the carried
//              arm, which that branch reaches on the next frame. A key is picked
//              up, trails and opens a keyhole all in one tick.
//   the trade  holding a fire diamond does NOT move where a portal opens - the
//              reading the 64x in computePortalFinalPos invites. It means NO
//              portal opens: turnProjectilesIntoFireBalls deletes the shot and
//              puts a fireball where it was. A test that only checked "the shot
//              behaved differently" would pass under either reading, so what is
//              asserted is that nothing was placed AND a fireball exists AND it
//              cannot hurt the player.
//
// And the filter that separates the two carried things: only a character may take
// a diamond, so a minion put on one does not pick it up - where a minion put on a
// KEY does, which test_mp_keys pins from the other side.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Diamonds.hpp"
#include "sim/Game.hpp"
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
constexpr float kStep = 1.0f / 60.0f;

// level15b holds two shock diamonds and three minions, and nothing is in range of
// anything at the start: the nearest diamond/minion pair is some 75 px apart, so
// a strike has to be carried into range rather than happening where the level
// puts things.
constexpr const char* kLevel = "level15b";
constexpr const char* kDiamond = "shock_diamond_ent_1741"; // at (204, 124)
constexpr const char* kOther = "shock_diamond_ent_1757";   // at (512, 146)

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-diamonds", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + std::string(": ") + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

void PutAt(entt::registry& registry, entt::entity body, const glm::dvec2& atPx) {
    const glm::vec3 world = Units::ToWorld(atPx.x, atPx.y);
    auto& transform = registry.get<TransformComponent>(body);
    transform.position = glm::vec3(world.x, world.y, transform.position.z);
    if (registry.all_of<RigidBodyComponent>(body)) {
        registry.get<RigidBodyComponent>(body).velocity = glm::vec3(0.0f);
    }
}

void theRulesAndWhatLevelFifteenHolds() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Diamonds::Rules& rules = run.level.diamonds.rules;
    CHECK(::test::nearly(static_cast<float>(rules.rangePx), 30.0f));
    CHECK_MSG(rules.rangePx > 26.0, "a shock diamond reaches further than a key, which is 26");
    CHECK(::test::nearly(static_cast<float>(rules.leashPx), 40.0f));
    CHECK(::test::nearly(static_cast<float>(rules.reaimMs), 20.0f));
    CHECK(::test::nearly(static_cast<float>(rules.strideMs), 60.0f));
    CHECK_MSG(rules.shockName == "shock_diamond.ent", "the entity this module plays");
    CHECK_MSG(rules.fireName == "fire_diamond.ent", "and the one it does not");
    CHECK_MSG(rules.shockName != rules.fireName, "which are two different entities");

    CHECK_EQ(run.level.diamonds.diamonds.size(), static_cast<std::size_t>(2));
    CHECK_EQ(run.level.diamonds.Standing(), static_cast<std::size_t>(2));
    CHECK_EQ(run.level.diamonds.picked, 0);
    CHECK_EQ(run.level.diamonds.struck, 0);

    const Diamonds::Diamond* one = run.level.diamonds.Find(kDiamond);
    CHECK(one != nullptr);
    if (one != nullptr) {
        CHECK(one->owner == entt::null);
        CHECK(!one->gone);
        CHECK_MSG(std::fabs(one->atPx.x - 204.0) < 1.0 && std::fabs(one->atPx.y - 124.0) < 1.0,
                  "where the level puts it");
    }
    const Diamonds::Diamond* two = run.level.diamonds.Find(kOther);
    CHECK(two != nullptr);
    if (two != nullptr) {
        CHECK_MSG(std::fabs(two->atPx.x - 512.0) < 1.0 && std::fabs(two->atPx.y - 146.0) < 1.0,
                  "and where it puts the other");
    }
}

// The guard against selecting on the role, from the fire side. These four levels
// each place a fire diamond and no shock one: each must yield exactly one
// diamond, and that one must be marked fire, because everything below turns on
// the distinction the role table cannot make.
void aFireDiamondIsFoundAndIsNotAShockOne() {
    for (const char* name : {"level28b", "level29b", "level30b", "level31b"}) {
        Run run;
        if (!Begin(name, run)) continue;
        CHECK_MSG(run.level.diamonds.diamonds.size() == static_cast<std::size_t>(1),
                  std::string(name) + " places one fire diamond");
        if (run.level.diamonds.diamonds.empty()) continue;
        CHECK_MSG(run.level.diamonds.diamonds[0].fire,
                  std::string(name) + "'s diamond is the fire one, not the shock one");
        CHECK_MSG(run.level.diamonds.diamonds[0].name.find("fire_diamond") != std::string::npos,
                  std::string(name) + ": and it answers to its node's name");
    }
}

// A fire diamond kills no minion. level28b places one AND minions, so this is the
// selection error made visible: were the role the selector, the minion 8 px away
// would die exactly as level15b's does.
void aFireDiamondStrikesNothing() {
    Run run;
    if (!Begin("level28b", run)) return;
    if (run.level.player == entt::null) return;
    if (run.level.diamonds.diamonds.empty() || run.level.minions.minions.empty()) return;

    const glm::dvec2 lyingAt = run.level.diamonds.diamonds[0].atPx;
    const entt::entity minion = run.level.minions.minions[0].body;
    if (minion == entt::null) return;

    // Exactly the arrangement that kills in level15b: the player on the diamond,
    // a minion 8 px off, held there for several frames so the frame separation
    // cannot be what spares it.
    for (int tick = 0; tick < 5; ++tick) {
        PutAt(run.registry, run.level.player, lyingAt);
        PutAt(run.registry, minion, glm::dvec2(lyingAt.x + 8.0, lyingAt.y));
        Tick(run);
    }

    CHECK_MSG(run.level.diamonds.picked == 1, "the player took the fire diamond");
    CHECK_MSG(run.level.diamonds.struck == 0, "and it struck nothing");
    CHECK_MSG(run.registry.valid(minion), "the minion 8 px away is alive");
    CHECK_EQ(run.level.minions.taken, 0);
    CHECK_MSG(run.level.diamonds.carrierHasFire, "and the carrier holds the fire");
}

// THE MECHANIC. A tap while carrying one opens no portal at all: the shot is
// deleted and a fireball stands where it was.
void holdingOneTurnsAPortalShotIntoAFireball() {
    Run run;
    if (!Begin("level28b", run)) return;
    if (run.level.player == entt::null) return;
    if (run.level.diamonds.diamonds.empty()) return;
    const glm::dvec2 lyingAt = run.level.diamonds.diamonds[0].atPx;

    // Taken, and held past the tap cooldowns - the first tap of a level is
    // refused until the larger of the two, which is 400 ms.
    for (int tick = 0; tick < 40; ++tick) {
        PutAt(run.registry, run.level.player, lyingAt);
        Tick(run);
    }
    CHECK_MSG(run.level.diamonds.carrierHasFire, "it is carried");
    const int placedBefore = static_cast<int>(run.level.portals.placed.size());
    const int usedBefore = run.level.portals.portalsUsed;

    PutAt(run.registry, run.level.player, lyingAt);
    const bool taken = run.level.portals.Shoot(run.registry, glm::dvec2(lyingAt.x + 120.0, lyingAt.y));
    CHECK_MSG(taken, "the tap is TAKEN - the original runs addProjectile and plays its launch sound");
    CHECK_MSG(run.level.portals.flight.has_value(), "and a shot leaves");

    PutAt(run.registry, run.level.player, lyingAt);
    Tick(run);

    CHECK_MSG(!run.level.portals.flight.has_value(), "which is gone a frame later, deleted rather than landed");
    CHECK_MSG(static_cast<int>(run.level.portals.placed.size()) == placedBefore, "NO portal opened");
    CHECK_MSG(run.level.portals.portalsUsed == usedBefore, "and none was counted against the budget");
    CHECK_MSG(run.level.turrets.converted == 1, "one shot was converted");

    // And what stands in its place is a fireball that cannot hurt the player -
    // burnProjectile passes killMainCharacter as immediate 0.
    bool found = false;
    for (const Turrets::Fireball& ball : run.level.turrets.fireballs) {
        if (ball.killsPlayer) continue;
        found = true;
        CHECK_MSG(ball.velocityPx.x > 0.0, "flying the way the tap pointed");
        CHECK_MSG(std::fabs(ball.velocityPx.y) < 1.0, "and level with it");
    }
    CHECK_MSG(found, "a fireball stands where the shot was");

    // The player sits on it for a second and is not burned by its own shot.
    for (int tick = 0; tick < 60; ++tick) {
        PutAt(run.registry, run.level.player, lyingAt);
        Tick(run);
    }
    CHECK_MSG(!run.level.turrets.playerKilled, "a fireball your own tap bought cannot burn you");
    CHECK_MSG(!run.level.hazards.playerDied, "and nothing else killed the player either");
}

// Without the diamond the same tap opens a portal, so the test above is measuring
// the diamond and not some other refusal.
void withoutOneTheSameTapOpensAPortal() {
    Run run;
    if (!Begin("level28b", run)) return;
    if (run.level.player == entt::null) return;

    // The player kept well clear of the diamond, which level28b puts at (192, 112).
    const glm::dvec2 standing(62.0, 208.0);
    for (int tick = 0; tick < 40; ++tick) {
        PutAt(run.registry, run.level.player, standing);
        Tick(run);
    }
    CHECK_MSG(!run.level.diamonds.carrierHasFire, "nothing is carried");

    PutAt(run.registry, run.level.player, standing);
    CHECK(run.level.portals.Shoot(run.registry, glm::dvec2(standing.x, standing.y - 48.0)));
    for (int tick = 0; tick < 20; ++tick) {
        PutAt(run.registry, run.level.player, standing);
        Tick(run);
    }
    CHECK_MSG(run.level.portals.placed.size() == static_cast<std::size_t>(1),
              "the same tap opens a portal: " + run.level.portals.lastFailure);
    CHECK_EQ(run.level.turrets.converted, 0);
}

// The drain, and the whole reason those levels can be finished. level30b puts its
// gutter mouth directly under its diamond.
void aGutterMouthTakesItAndGivesThePortalsBack() {
    Run run;
    if (!Begin("level30b", run)) return;
    if (run.level.player == entt::null) return;
    if (run.level.diamonds.diamonds.empty()) return;

    CHECK_MSG(run.level.diamonds.gutters.size() == static_cast<std::size_t>(1), "level30b places one gutter mouth");
    if (run.level.diamonds.gutters.empty()) return;

    // The mouth is the node's trigger_size at its trigger_offset: 10 x 51 hung 46
    // px BELOW the node at (384, 182), not a box on the node.
    const Diamonds::Gutter& gutter = run.level.diamonds.gutters[0];
    const glm::vec3 mouth = Units::ToWorld(384.0, 182.0 + 46.0);
    CHECK_MSG(std::fabs(gutter.box.centre.x - mouth.x) < 0.05f && std::fabs(gutter.box.centre.y - mouth.y) < 0.05f,
              "the mouth hangs below the node");
    CHECK(::test::nearly(gutter.box.half.x, static_cast<float>(Units::ToMetres(5.0))));
    CHECK(::test::nearly(gutter.box.half.y, static_cast<float>(Units::ToMetres(25.5))));

    const glm::dvec2 lyingAt = run.level.diamonds.diamonds[0].atPx;
    for (int tick = 0; tick < 4; ++tick) {
        PutAt(run.registry, run.level.player, lyingAt);
        Tick(run);
    }
    CHECK_MSG(run.level.diamonds.carrierHasFire, "taken, and the fire is held");

    // Carried into the drain. The diamond trails its owner, so standing in the
    // mouth brings it in after it.
    const glm::dvec2 inTheMouth(384.0, 182.0 + 46.0);
    for (int tick = 0; tick < 120 && !run.level.diamonds.diamonds[0].gone; ++tick) {
        PutAt(run.registry, run.level.player, inTheMouth);
        Tick(run);
    }

    CHECK_MSG(run.level.diamonds.diamonds[0].gone, "the drain took the diamond");
    CHECK_EQ(run.level.diamonds.drowned, 1);
    CHECK_MSG(!run.level.diamonds.carrierHasFire, "and the fire went with it");
    CHECK_EQ(run.level.diamonds.Standing(), static_cast<std::size_t>(0));

    // And the portals are back. The loop above STOPS as soon as the drain has the
    // diamond, which is about a dozen ticks in - short of both tap cooldowns - so
    // the clock has to be run on before a tap can be taken at all.
    for (int tick = 0; tick < 40; ++tick) {
        PutAt(run.registry, run.level.player, inTheMouth);
        Tick(run);
    }

    // What is asserted is that the tap resolves AS A SHOT rather than being
    // deleted and replaced - not where it lands. level30b's geometry is not this
    // test's subject and the level carries an antiportal, so a shot that fails on
    // one is still a shot and still proves the fire is gone.
    const int converted = run.level.turrets.converted;
    const int resolved = static_cast<int>(run.level.portals.placed.size()) + run.level.portals.shotsFailed;
    PutAt(run.registry, run.level.player, inTheMouth);
    CHECK_MSG(run.level.portals.Shoot(run.registry, glm::dvec2(inTheMouth.x, inTheMouth.y - 48.0)),
              "a tap is taken again once the drain has the diamond");
    for (int tick = 0; tick < 20; ++tick) {
        PutAt(run.registry, run.level.player, inTheMouth);
        Tick(run);
    }
    CHECK_MSG(run.level.turrets.converted == converted, "and it was NOT turned into a fireball");
    CHECK_MSG(static_cast<int>(run.level.portals.placed.size()) + run.level.portals.shotsFailed > resolved,
              "it resolved as a shot does, rather than being deleted: " + run.level.portals.lastFailure);
}

// The whole of the difference from a key, in one test: taken on one frame, and
// striking only on the next.
void takenOnOneFrameAndStrikingOnTheNext() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;

    const Diamonds::Diamond* diamond = run.level.diamonds.Find(kDiamond);
    CHECK(diamond != nullptr);
    if (diamond == nullptr) return;
    // By VALUE: Find hands back a pointer into the state, which the ticks below
    // move.
    const glm::dvec2 lyingAt = diamond->atPx;

    CHECK_MSG(!run.level.minions.minions.empty(), "level15b spawns minions");
    if (run.level.minions.minions.empty()) return;
    const entt::entity minion = run.level.minions.minions[0].body;
    CHECK(minion != entt::null);
    if (minion == entt::null) return;

    // The player on it, and a minion 8 px away - well inside the 30 px the strike
    // uses, so nothing but the frame separation can keep it alive.
    PutAt(run.registry, run.level.player, lyingAt);
    PutAt(run.registry, minion, glm::dvec2(lyingAt.x + 8.0, lyingAt.y));
    Tick(run);

    const Diamonds::Diamond* taken = run.level.diamonds.Find(kDiamond);
    CHECK(taken != nullptr);
    if (taken == nullptr) return;
    CHECK_MSG(taken->owner == run.level.player, "the player took it");
    CHECK_EQ(run.level.diamonds.picked, 1);
    CHECK_MSG(!taken->gone, "and it has not spent itself on the frame it was taken");
    CHECK_EQ(run.level.diamonds.struck, 0);
    CHECK_MSG(run.registry.valid(minion), "the minion 8 px away is alive after the frame it was taken on");
    CHECK_EQ(run.level.minions.taken, 0);

    // The next frame is the carried arm, which is where the payload lives.
    PutAt(run.registry, run.level.player, lyingAt);
    PutAt(run.registry, minion, glm::dvec2(lyingAt.x + 8.0, lyingAt.y));
    Tick(run);

    CHECK_EQ(run.level.diamonds.struck, 1);
    CHECK_MSG(!run.registry.valid(minion), "and on the next frame it is struck");
    CHECK_EQ(run.level.minions.taken, 1);
    const Diamonds::Diamond* spent = run.level.diamonds.Find(kDiamond);
    CHECK(spent != nullptr);
    if (spent != nullptr) CHECK_MSG(spent->gone, "the diamond deleted itself with it");
    CHECK_EQ(run.level.diamonds.Standing(), static_cast<std::size_t>(1));

    // And Minions owns the removal: the minion is gone from its own list, not
    // merely destroyed behind its back.
    CHECK_MSG(run.level.minions.minions[0].gone, "Minions knows the minion it lost");
}

// Only a character may take one. A minion put straight onto a diamond leaves it
// lying, where a minion put onto a KEY picks it up - test_mp_keys pins that, and
// this is the same instant from the other side.
void aMinionCannotCarryOne() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;

    const Diamonds::Diamond* diamond = run.level.diamonds.Find(kDiamond);
    if (diamond == nullptr) return;
    const glm::dvec2 lyingAt = diamond->atPx;

    if (run.level.minions.minions.empty()) return;
    const entt::entity minion = run.level.minions.minions[0].body;
    if (minion == entt::null) return;

    // The player well clear of BOTH diamonds, so nothing it does can be mistaken
    // for the minion's doing.
    for (int tick = 0; tick < 2; ++tick) {
        PutAt(run.registry, run.level.player, glm::dvec2(700.0, 208.0));
        PutAt(run.registry, minion, lyingAt);
        Tick(run);
    }

    const Diamonds::Diamond* after = run.level.diamonds.Find(kDiamond);
    CHECK(after != nullptr);
    if (after == nullptr) return;
    CHECK_MSG(after->owner == entt::null, "a minion standing on a diamond does not take it");
    CHECK_MSG(after->owner != minion, "and is not its owner");
    CHECK_MSG(!after->gone, "and nothing struck");
    CHECK_MSG(run.registry.valid(minion), "the minion is alive, having stood on it for two frames");
}

// Carried, it trails its owner and stays inside the leash - the path it shares
// with a key, exercised through this module's own numbers.
void aCarriedDiamondTrailsItsOwner() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;

    const Diamonds::Diamond* diamond = run.level.diamonds.Find(kDiamond);
    if (diamond == nullptr) return;
    const glm::dvec2 lyingAt = diamond->atPx;

    PutAt(run.registry, run.level.player, lyingAt);
    Tick(run);
    const Diamonds::Diamond* taken = run.level.diamonds.Find(kDiamond);
    CHECK(taken != nullptr);
    if (taken == nullptr || taken->owner == entt::null) return;

    // Carried well away. Nothing is put in reach of a minion, so it survives to
    // be measured rather than spending itself on the way.
    PutAt(run.registry, run.level.player, glm::dvec2(lyingAt.x + 200.0, lyingAt.y));
    for (int tick = 0; tick < 30; ++tick) {
        PutAt(run.registry, run.level.player, glm::dvec2(lyingAt.x + 200.0, lyingAt.y));
        Tick(run);
    }

    const Diamonds::Diamond* carried = run.level.diamonds.Find(kDiamond);
    CHECK(carried != nullptr);
    if (carried == nullptr || carried->gone) return;
    const glm::dvec2 playerPx = Units::ToPixels(run.registry.get<TransformComponent>(run.level.player).position);
    const double gap = std::sqrt((carried->atPx.x - playerPx.x) * (carried->atPx.x - playerPx.x) +
                                 (carried->atPx.y - playerPx.y) * (carried->atPx.y - playerPx.y));
    CHECK_MSG(gap <= run.level.diamonds.rules.leashPx + 1.0, "a carried diamond stays inside its leash");
    CHECK_MSG(std::fabs(carried->atPx.x - lyingAt.x) > 100.0, "and it has left where it was lying");
}

// The census, over the chapter-3 levels that place one. level29c places one too
// and is left out because this census is chapter 3's. It is no longer left out
// for the reason written here before - that no_gravity and darkest stopped all
// but two of chapter 4 starting - because all 32 of them start now.
void everyChapterThreeLevelThatPlacesOneFindsIt() {
    const struct Level {
        const char* name;
        std::size_t count;
    } levels[] = {{"level15b", 2}, {"level16b", 2}, {"level17b", 1}, {"level19b", 1}, {"level26b", 1}};

    std::size_t total = 0;
    for (const Level& one : levels) {
        Run run;
        if (!Begin(one.name, run)) continue;
        CHECK_MSG(run.level.diamonds.diamonds.size() == one.count,
                  std::string(one.name) + " places " + std::to_string(one.count) + " shock diamond(s)");
        total += run.level.diamonds.diamonds.size();
        for (const Diamonds::Diamond& diamond : run.level.diamonds.diamonds) {
            CHECK_MSG(!diamond.name.empty(), std::string(one.name) + ": a diamond answers to its node's name");
            CHECK(diamond.owner == entt::null);
        }
    }
    CHECK_MSG(total == 7, "7 of the game's 8 shock diamonds are in chapter 3; the eighth is level29c's");
    std::printf("  chapter 3 places %zu shock diamond(s) across %zu level(s)\n", total,
                sizeof(levels) / sizeof(levels[0]));
}

void runTests() {
    theRulesAndWhatLevelFifteenHolds();
    aFireDiamondIsFoundAndIsNotAShockOne();
    aFireDiamondStrikesNothing();
    holdingOneTurnsAPortalShotIntoAFireball();
    withoutOneTheSameTapOpensAPortal();
    aGutterMouthTakesItAndGivesThePortalsBack();
    takenOnOneFrameAndStrikingOnTheNext();
    aMinionCannotCarryOne();
    aCarriedDiamondTrailsItsOwner();
    everyChapterThreeLevelThatPlacesOneFindsIt();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_diamonds SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_diamonds", 40);
}
