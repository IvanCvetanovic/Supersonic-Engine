// Chapter 3's boss: the ghost of level 3-32, and the fight the fire diamond
// exists for.
//
// What is pinned here is the DECODE, and above all the thing that makes this
// boss different from every other hazard in the port: FIRE is the only thing
// that hurts it. Its SHOOTING arm ends with isBurned(this), and nothing else in
// level31b can burn anything - so the fire diamond, whose fireballs a converted
// portal shot becomes, IS the weapon. A port that built this boss without that
// step would have built one nothing could kill, and every test below would still
// pass except the ones that burn it.
//
// The other three a careless port gets wrong:
//
//   the gate    it shoots only at a player whose x is past the level's red_line
//               (262 in level31b), so a player on the left of the arena is left
//               alone. The suite uses that as a lever: parked behind the line,
//               the boss never shoots, and everything else can be measured
//               without the player being killed mid-test.
//   the escort  RAGE_MODE waits for the minion it summoned to be killed, and it
//               seeks that one BY NAME. level31b spawns a patroller of its own at
//               the start, and counting that one too would leave the count never
//               zero and the boss in rage for ever.
//   the key     level31b holds a keyhole and a locked door and NO KEY. The only
//               key in the level is the one this boss drops, so the fight is the
//               lock, and the key is yellow because key.ent's own custom data
//               says so - not because level31b's keyhole is.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
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

constexpr const char* kLevel = "level31b";

// Behind the red line at x 262, so the boss never shoots and a test can run for
// twenty seconds without the player being killed in the middle of it.
const glm::dvec2 kBehindTheLine(100.0, 208.0);
// And past it, for the one test that is about the shooting.
const glm::dvec2 kPastTheLine(400.0, 208.0);

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-ghost", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + std::string(": ") + error);
    return ok;
}

void PutAt(entt::registry& registry, entt::entity body, const glm::dvec2& atPx) {
    const glm::vec3 world = Units::ToWorld(atPx.x, atPx.y);
    auto& transform = registry.get<TransformComponent>(body);
    transform.position = glm::vec3(world.x, world.y, transform.position.z);
    if (registry.all_of<RigidBodyComponent>(body)) {
        registry.get<RigidBodyComponent>(body).velocity = glm::vec3(0.0f);
    }
}

// Ticks with the player held where it is put, which is what keeps it out of the
// boss's reach for as long as a test needs.
void Hold(Run& run, const glm::dvec2& atPx, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) {
        if (run.level.player != entt::null) PutAt(run.registry, run.level.player, atPx);
        Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
    }
}

// Past the appearance and into SHOOTING: 3750 ms of fade, which is 225 ticks.
void ReachShooting(Run& run) {
    Hold(run, kBehindTheLine, 260);
}

// One round of the fight: burn it, let the damage run into rage, kill the escort
// it calls, and come back to shooting.
void BurnAndLetItRecover(Run& run) {
    run.level.ghost.burned = true;
    Hold(run, kBehindTheLine, 2); // DAMAGE

    // 4000 ms of damage, then RAGE_MODE and the summon on the next tick.
    Hold(run, kBehindTheLine, 250);

    // Kill whatever it called, which is what rage is waiting for.
    for (const Minions::Minion& minion : run.level.minions.minions) {
        if (minion.gone || minion.body == entt::null) continue;
        if (minion.name.rfind(run.level.ghost.name + "#", 0) != 0) continue;
        run.level.minions.Take(run.registry, {minion.body});
        break;
    }
    Hold(run, kBehindTheLine, 30);
}

void theRulesAndWhatLevelThirtyOneBHolds() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Ghost::Rules& rules = run.level.ghost.rules;
    CHECK_EQ(rules.maxHp, 3);
    CHECK(::test::nearly(static_cast<float>(rules.radiusPx), 64.0f));
    CHECK(::test::nearly(static_cast<float>(rules.intervalMs), 3000.0f));
    CHECK(::test::nearly(static_cast<float>(rules.toRageMs), 4000.0f));
    CHECK(::test::nearly(static_cast<float>(rules.burstMs), 3550.0f));
    CHECK(::test::nearly(static_cast<float>(rules.shotSpeedPxS), 120.0f));
    CHECK(::test::nearly(static_cast<float>(rules.shotHitPx), 16.0f));
    CHECK_MSG(rules.keyColour == "yellow", "key.ent's own custom data, not level31b's keyhole");
    CHECK_MSG(rules.anchorName == "ghost_pos", "the boss_spawn node, matched by name");

    CHECK_MSG(run.level.ghost.present, "level31b places a ghost");
    CHECK_EQ(run.level.ghost.hp, 3);
    CHECK(run.level.ghost.phase == Ghost::Phase::Appearing);
    CHECK_MSG(std::fabs(run.level.ghost.atPx.x - 488.0) < 1.0 && std::fabs(run.level.ghost.atPx.y - 154.0) < 1.0,
              "where the level puts it");

    // The three markers it reads by name, none of which carries a role the port
    // plays - minion_spawn_point carries no role at all.
    CHECK_MSG(run.level.ghost.hasRedLine, "it found the red line");
    CHECK(::test::nearly(static_cast<float>(run.level.ghost.redLineX), 262.0f));
    CHECK_MSG(run.level.ghost.hasSummonAnchor, "and its summon anchor");
    CHECK_MSG(std::fabs(run.level.ghost.summonPx.x - 436.0) < 1.0 &&
                  std::fabs(run.level.ghost.summonPx.y - 96.0) < 1.0,
              "at minion_spawn_point");
    CHECK_MSG(run.level.ghost.hasDiamondSpawn, "and where a fire diamond goes back");

    // The escort's patrol, resolved from the wayA prefix at Find time because
    // AfterStep has no scene to resolve it from.
    CHECK_EQ(run.level.ghost.patrol.size(), static_cast<std::size_t>(2));
    if (run.level.ghost.patrol.size() == 2) {
        CHECK(::test::nearly(static_cast<float>(run.level.ghost.patrol[0].atPx.x), 314.0f));
        CHECK(::test::nearly(static_cast<float>(run.level.ghost.patrol[0].holdMs), 1000.0f));
        CHECK(::test::nearly(static_cast<float>(run.level.ghost.patrol[1].atPx.x), 412.0f));
        CHECK(::test::nearly(static_cast<float>(run.level.ghost.patrol[1].holdMs), 4000.0f));
    }
}

// It fades in before it fights: 3750 ms in which nothing it does can be reached.
void itFadesInBeforeItFights() {
    Run run;
    if (!Begin(kLevel, run)) return;

    Hold(run, kBehindTheLine, 200); // 3333 ms
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Appearing, "still appearing at 3.3 s");
    CHECK_EQ(run.level.ghost.shotsFired, 0);

    Hold(run, kBehindTheLine, 60); // past 3750 ms
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Shooting, "and fighting by 4.3 s");
}

// The red line, which is the whole of whether it shoots at all.
void theRedLineGatesItsShooting() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;

    ReachShooting(run);
    // Two whole intervals with the player behind the line.
    Hold(run, kBehindTheLine, 400);
    CHECK_MSG(run.level.ghost.shotsFired == 0, "a player left of the red line is not shot at");

    // And past it, where the first shot comes on the first beat after.
    Hold(run, kPastTheLine, 200);
    CHECK_MSG(run.level.ghost.shotsFired > 0, "past it, it shoots");
    std::printf("  level31b: %d shot(s) once the player crossed the red line\n", run.level.ghost.shotsFired);
}

// Fire, and nothing else. The flag is what a fireball sets, and Tick reads and
// clears it exactly as healBurn does.
void onlyFireHurtsIt() {
    Run run;
    if (!Begin(kLevel, run)) return;
    ReachShooting(run);

    CHECK_EQ(run.level.ghost.hp, 3);
    CHECK_EQ(run.level.ghost.hits, 0);

    // A long while with no fire at all, and it is untouched.
    Hold(run, kBehindTheLine, 300);
    CHECK_MSG(run.level.ghost.hp == 3, "nothing but fire touches it");
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Shooting, "and it is still shooting");

    run.level.ghost.burned = true;
    Hold(run, kBehindTheLine, 2);
    CHECK_MSG(run.level.ghost.hp == 2, "burned, it takes one");
    CHECK_EQ(run.level.ghost.hits, 1);
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Damage, "and goes to damage");
    CHECK_MSG(!run.level.ghost.burned, "healBurn: the flag is cleared rather than left set");
}

// Hurt, it calls an escort and waits for the player to kill it.
void hurtItCallsAnEscortAndWaitsForIt() {
    Run run;
    if (!Begin(kLevel, run)) return;
    ReachShooting(run);

    const std::size_t before = run.level.minions.minions.size();
    run.level.ghost.burned = true;
    Hold(run, kBehindTheLine, 2);
    Hold(run, kBehindTheLine, 250); // through the 4000 ms of damage, into rage

    CHECK_MSG(run.level.ghost.summons == 1, "rage calls one escort");
    CHECK_MSG(run.level.minions.minions.size() == before + 1, "and Minions owns the body it built");
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Rage, "and it waits there");

    // It keeps waiting while that escort stands - this is the part that would
    // deadlock if the level's own patroller were counted as one of its own.
    Hold(run, kBehindTheLine, 120);
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Rage, "still waiting two seconds later");
    CHECK_MSG(run.level.ghost.summons == 1, "and it does not call a second");

    entt::entity escort = entt::null;
    for (const Minions::Minion& minion : run.level.minions.minions) {
        if (minion.gone || minion.body == entt::null) continue;
        if (minion.name.rfind(run.level.ghost.name + "#", 0) != 0) continue;
        escort = minion.body;
        break;
    }
    CHECK_MSG(escort != entt::null, "the escort is findable by the boss's own name");
    if (escort == entt::null) return;

    run.level.minions.Take(run.registry, {escort});
    Hold(run, kBehindTheLine, 30);
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Shooting, "killed, and the boss comes back out");
}

// Three burns, and the key that is the level's only one.
void threeBurnsAndItDropsAYellowKey() {
    Run run;
    if (!Begin(kLevel, run)) return;
    ReachShooting(run);

    const std::size_t keysBefore = run.level.keys.keys.size();
    CHECK_MSG(keysBefore == 0, "level31b ships no key at all: the fight is the lock");
    CHECK_MSG(!run.level.keys.keyholes.empty(), "but it does ship the keyhole");

    BurnAndLetItRecover(run);
    CHECK_EQ(run.level.ghost.hp, 2);
    BurnAndLetItRecover(run);
    CHECK_EQ(run.level.ghost.hp, 1);

    // The third takes it to nothing, and DAMAGE goes straight to DEATH.
    run.level.ghost.burned = true;
    Hold(run, kBehindTheLine, 4);
    CHECK_EQ(run.level.ghost.hp, 0);
    CHECK_MSG(run.level.ghost.phase == Ghost::Phase::Death, "hp gone, it dies rather than raging again");
    CHECK_MSG(run.level.keys.keys.size() == keysBefore, "and it has not dropped the key yet");

    // 3550 ms of bursting, and then the key.
    Hold(run, kBehindTheLine, 230);
    CHECK_MSG(run.level.ghost.gone, "the ghost is gone");
    CHECK_MSG(run.level.keys.keys.size() == keysBefore + 1, "and a key is where it was");
    if (run.level.keys.keys.size() == keysBefore + 1) {
        const Keys::Key& dropped = run.level.keys.keys.back();
        CHECK_MSG(dropped.colour == "yellow", "yellow, from key.ent's own data; got '" + dropped.colour + "'");
        CHECK_MSG(std::fabs(dropped.atPx.x - run.level.ghost.atPx.x) < 1.0, "dropped where it died");
    }
    CHECK_EQ(run.level.ghost.hits, 3);
    std::printf("  level31b: the ghost took %d burn(s) and dropped a %s key\n", run.level.ghost.hits,
                run.level.keys.keys.empty() ? "no" : run.level.keys.keys.back().colour.c_str());
}

// One boss, one level. The others carry a boss_spawn the port does not play, and
// Ghost::Plays answering per NODE is what keeps that honest.
void noOtherLevelHasOne() {
    for (const char* name : {"level31", "level31a", "level30b", "level29b"}) {
        Run run;
        if (!Begin(name, run)) continue;
        CHECK_MSG(!run.level.ghost.present, std::string(name) + " has no ghost");
        CHECK_MSG(run.level.ghost.patrol.empty(), std::string(name) + " resolves no escort patrol");
    }
}

void runTests() {
    theRulesAndWhatLevelThirtyOneBHolds();
    itFadesInBeforeItFights();
    theRedLineGatesItsShooting();
    onlyFireHurtsIt();
    hurtItCallsAnEscortAndWaitsForIt();
    threeBurnsAndItDropsAYellowKey();
    noOtherLevelHasOne();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_ghost SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_ghost", 40);
}
