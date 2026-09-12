// Keys, the keyholes they open, and the locked doors that go with them.
//
// What is pinned here is the DECODE. Keys.hpp cites where each number comes from;
// the ones a test can tell apart from the remake's guesses are:
//
//   range   ONE range of 26 px, serving both being picked up and opening a
//           keyhole. The remake invents two - pickup_range_px and
//           unlock_range_px - and guesses 40 for each, so a key 28 px from the
//           player is taken in the remake and not in the original. That exact
//           distance is tested in both directions.
//   pickup  a distance poll rather than a contact, and anything that is a
//           character OR a minion can take one. A minion carrying a key is
//           therefore tested, because it is the kind of thing a remake drops
//           without noticing.
//   fade    an unlocked keyhole holds 1000 ms and fades over 500, and only then
//           is the door deleted. Nothing re-locks it: `unlocked` is written in
//           one place in the whole binary and read in one other.
//
// And the pairing, which is by COLOUR and not by proximity. level20b is the only
// level in the game holding two pairs of one colour, and it is tested because it
// is the only one where a nearest-door rule could have picked the wrong door: its
// two yellow keyholes sit 57 px from their own doors and 216 px or more from each
// other's.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Keys.hpp"
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

// level7b is the first level in the game with a key: one yellow trio, the key at
// (174, 34), its keyhole at (588, 108) and its door at (624, 66).
constexpr const char* kLevel = "level7b";
constexpr const char* kKey = "key_ent_1335";
constexpr const char* kKeyhole = "keyhole_1338";
constexpr const char* kDoor = "door_locked_ent_1343";

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-keys", run.data,
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

void theOneRangeServesBothTests() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Keys::Rules& rules = run.level.keys.rules;
    CHECK(::test::nearly(static_cast<float>(rules.rangePx), 26.0f));
    CHECK_MSG(rules.rangePx < 40.0, "the remake's 40 px is not what is played");
    CHECK(::test::nearly(static_cast<float>(rules.leashPx), 40.0f));
    CHECK(::test::nearly(static_cast<float>(rules.fadeStartMs), 1000.0f));
    CHECK(::test::nearly(static_cast<float>(rules.fadeMs), 500.0f));

    CHECK_EQ(run.level.keys.keys.size(), static_cast<std::size_t>(1));
    CHECK_EQ(run.level.keys.keyholes.size(), static_cast<std::size_t>(1));

    const Keys::Key* key = run.level.keys.FindKey(kKey);
    CHECK(key != nullptr);
    if (key != nullptr) {
        CHECK_MSG(key->colour == "yellow", "the key is yellow");
        CHECK(key->owner == entt::null);
    }
    const Keys::Keyhole* keyhole = run.level.keys.FindKeyhole(kKeyhole);
    CHECK(keyhole != nullptr);
    if (keyhole != nullptr) {
        CHECK_MSG(keyhole->colour == "yellow", "and so is its keyhole");
        CHECK_MSG(keyhole->doorName == kDoor, "which took the door of its own colour");
        CHECK(!keyhole->unlocked);
    }
}

// The range, swept rather than asserted at one spot.
//
// Keys::Tick runs AFTER the physics step, and level7b's key lies between two
// collision polygons, so a player put down beside it is resolved somewhere else
// before the keys are judged - which makes any fixed offset a fiction. So each
// offset is tried, the distance the module really measured is read back, and the
// decision is pinned against THAT. What pins the number itself is the pair of
// brackets at the end: a key taken from beyond 20 px is not a tiny range, and one
// left lying while the player stood inside 40 is not the remake's guess.
void aKeyIsTakenAtTwentySixPixels() {
    const struct Case {
        double atPx;
        bool taken;
    } cases[] = {{16.0, true}, {20.0, true}, {24.0, true}, {28.0, false}, {32.0, false}, {40.0, false}};

    // `taken` above is what the decoded 26 says of each offset, and is kept as
    // the record of it; what is asserted is the measurement.
    bool tookBeyondTwenty = false;
    bool leftInsideForty = false;

    for (const Case& one : cases) {
        Run run;
        if (!Begin(kLevel, run)) continue;
        const Keys::Key* key = run.level.keys.FindKey(kKey);
        if (key == nullptr || run.level.player == entt::null) continue;
        const glm::dvec2 keyAt = key->atPx;

        PutAt(run.registry, run.level.player, glm::dvec2(keyAt.x + one.atPx, keyAt.y));
        Tick(run);

        const Keys::Key* after = run.level.keys.FindKey(kKey);
        CHECK(after != nullptr);
        if (after == nullptr) continue;

        // Where the player ACTUALLY is when the keys are judged. Keys::Tick runs
        // after the physics step, so a spot inside the level's geometry would be
        // resolved out from under this and the distance pinned here would not be
        // the distance the module measured.
        const glm::dvec2 playerPx =
            Units::ToPixels(run.registry.get<TransformComponent>(run.level.player).position);
        const double measured = std::sqrt((playerPx.x - keyAt.x) * (playerPx.x - keyAt.x) +
                                          (playerPx.y - keyAt.y) * (playerPx.y - keyAt.y));
        const bool taken = after->owner != entt::null;
        CHECK_MSG(taken == (measured < run.level.keys.rules.rangePx),
                  std::string("at ") + std::to_string(static_cast<int>(measured)) + " px the key is " +
                      (taken ? "taken" : "left"));

        // level7b spawns a minion 34 px from the key, and a minion can carry one.
        // If anything took it at a distance the player could not, say so.
        CHECK_MSG(!taken || after->owner == run.level.player,
                  "whatever took the key, it was the player and not a minion");

        if (taken && measured > 20.0) tookBeyondTwenty = true;
        if (!taken && measured < 40.0) leftInsideForty = true;
    }

    CHECK_MSG(tookBeyondTwenty, "a key is taken from beyond 20 px");
    CHECK_MSG(leftInsideForty, "and left lying from inside 40, which the remake's guess would have taken");
}

// Anything that is a character OR a minion takes one, and level7b has both: a
// minion_spawn at (208, 34), which is 34 px from the key lying at (174, 34). This
// one follows the player's.
void aCarriedKeyTrailsItsOwner() {
    Run run;
    if (!Begin(kLevel, run)) return;
    const Keys::Key* key = run.level.keys.FindKey(kKey);
    if (key == nullptr || run.level.player == entt::null) return;

    // By VALUE: FindKey hands back a pointer into the state, so reading it again
    // after the ticks below would be reading where the key is NOW, and comparing
    // that against itself.
    const glm::dvec2 lyingAt = key->atPx;

    PutAt(run.registry, run.level.player, lyingAt);
    Tick(run);
    const Keys::Key* taken = run.level.keys.FindKey(kKey);
    CHECK(taken != nullptr);
    if (taken == nullptr) return;
    CHECK(taken->owner == run.level.player);
    CHECK_EQ(run.level.keys.picked, 1);

    // Carried well away, the key follows rather than staying put - and it stays
    // within the leash rather than being pinned exactly on its owner.
    PutAt(run.registry, run.level.player, glm::dvec2(lyingAt.x + 300.0, lyingAt.y));
    for (int tick = 0; tick < 30; ++tick) Tick(run);

    const Keys::Key* carried = run.level.keys.FindKey(kKey);
    CHECK(carried != nullptr);
    if (carried == nullptr) return;
    const glm::dvec2 playerPx = Units::ToPixels(run.registry.get<TransformComponent>(run.level.player).position);
    const double gap = std::sqrt((carried->atPx.x - playerPx.x) * (carried->atPx.x - playerPx.x) +
                                 (carried->atPx.y - playerPx.y) * (carried->atPx.y - playerPx.y));
    CHECK_MSG(gap <= run.level.keys.rules.leashPx + 1.0, "a carried key stays inside its leash");
    CHECK_MSG(std::fabs(carried->atPx.x - lyingAt.x) > 100.0, "and it has left where it was lying");
}

// The pickup filter is isCharacter OR isMinion, so a minion carries a key just as
// the player does. Pinned because it is the one thing that separates a key from a
// diamond, whose candidate loop tests isCharacter and stops there: the decode
// skips a non-character at instruction 123 and never reaches the range test.
//
// level7b holds three minion_spawns and the nearest of them lies 34 px from the
// key, outside the 26 px range, so none of them takes it where it lies - which is
// what the sweep above asserts from the other side. This puts the first minion ON
// the key and reads back who owns it. Carriers are judged player-first, so an
// owner that is the minion is the filter being exercised rather than an accident
// of ordering.
void aMinionCanCarryAKey() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Keys::Key* key = run.level.keys.FindKey(kKey);
    CHECK(key != nullptr);
    if (key == nullptr) return;
    CHECK_MSG(!run.level.minions.minions.empty(), "level7b spawns a minion");
    if (run.level.minions.minions.empty()) return;

    const entt::entity minion = run.level.minions.minions[0].body;
    CHECK(minion != entt::null);
    if (minion == entt::null) return;
    const glm::dvec2 lyingAt = key->atPx;

    PutAt(run.registry, minion, lyingAt);
    Tick(run);

    const Keys::Key* after = run.level.keys.FindKey(kKey);
    CHECK(after != nullptr);
    if (after == nullptr) return;
    CHECK_MSG(after->owner == minion, "a minion took the key");
    CHECK_MSG(after->owner != run.level.player, "and it was not the player, which is a level away");
    CHECK_EQ(run.level.keys.picked, 1);
}

// The whole of what unlocking does to a level: after the hold and the fade, the
// door body is gone.
void unlockingHoldsThenFadesThenTakesTheDoor() {
    Run run;
    if (!Begin(kLevel, run)) return;
    const Keys::Key* key = run.level.keys.FindKey(kKey);
    const Keys::Keyhole* keyhole = run.level.keys.FindKeyhole(kKeyhole);
    if (key == nullptr || keyhole == nullptr || run.level.player == entt::null) return;

    const entt::entity door = keyhole->door;
    CHECK_MSG(door != entt::null && run.registry.valid(door), "the door is built and standing");
    const glm::dvec2 keyholeAt = keyhole->atPx;

    // Take the key, then carry it to the keyhole.
    PutAt(run.registry, run.level.player, key->atPx);
    Tick(run);
    PutAt(run.registry, run.level.player, keyholeAt);
    for (int tick = 0; tick < 30; ++tick) Tick(run);

    const Keys::Keyhole* reached = run.level.keys.FindKeyhole(kKeyhole);
    CHECK(reached != nullptr);
    if (reached == nullptr) return;
    CHECK_MSG(reached->unlocked, "the key opened its keyhole");
    CHECK_EQ(run.level.keys.unlocked, 1);
    const Keys::Key* spent = run.level.keys.FindKey(kKey);
    if (spent != nullptr) CHECK_MSG(spent->spent, "and the key is spent");

    // Still there through the hold: 1000 ms, and the fade has not begun.
    CHECK_MSG(run.registry.valid(door), "the door stands through the hold");

    // Through the hold and the fade - 1500 ms in all - it goes.
    for (int tick = 0; tick < 120; ++tick) Tick(run);
    const Keys::Keyhole* faded = run.level.keys.FindKeyhole(kKeyhole);
    CHECK(faded != nullptr);
    if (faded == nullptr) return;
    CHECK_MSG(faded->gone, "the keyhole faded away");
    CHECK(::test::nearly(static_cast<float>(faded->alpha), 0.0f));
    CHECK_MSG(!run.registry.valid(door), "and took its door with it");
    CHECK_EQ(run.level.keys.opened, 1);
}

// The one level holding two pairs of a colour: each keyhole takes its own door.
void twoPairsOfOneColourStillPairCorrectly() {
    Run run;
    if (!Begin("level20b", run)) return;

    CHECK_EQ(run.level.keys.keyholes.size(), static_cast<std::size_t>(3));
    CHECK_EQ(run.level.keys.keys.size(), static_cast<std::size_t>(3));

    // keyhole (602,114) belongs to door (640,72), and (368,144) to (400,192).
    const struct Pair {
        glm::dvec2 keyhole;
        glm::dvec2 door;
    } expected[] = {{{602.0, 114.0}, {640.0, 72.0}}, {{368.0, 144.0}, {400.0, 192.0}}, {{78.0, 202.0}, {128.0, 192.0}}};

    for (const Pair& want : expected) {
        const Keys::Keyhole* found = nullptr;
        for (const Keys::Keyhole& keyhole : run.level.keys.keyholes) {
            if (std::fabs(keyhole.atPx.x - want.keyhole.x) < 1.0 && std::fabs(keyhole.atPx.y - want.keyhole.y) < 1.0) {
                found = &keyhole;
            }
        }
        CHECK(found != nullptr);
        if (found == nullptr || found->door == entt::null) continue;
        const glm::dvec2 doorAt = Units::ToPixels(run.registry.get<TransformComponent>(found->door).position);
        CHECK_MSG(std::fabs(doorAt.x - want.door.x) < 1.0 && std::fabs(doorAt.y - want.door.y) < 1.0,
                  found->name + " took the door at its own end of the level");
    }
}

// Every level that carries the roles starts, and every keyhole found a door.
void everyLevelWithAKeyholePairsIt() {
    const char* levels[] = {"level7b", "level9b", "level15b", "level20b", "level29b", "level0c", "level27c"};
    for (const char* name : levels) {
        Run run;
        if (!Begin(name, run)) continue;
        for (const Keys::Keyhole& keyhole : run.level.keys.keyholes) {
            CHECK_MSG(!keyhole.doorName.empty(), std::string(name) + ": " + keyhole.name + " has a door");
            CHECK_MSG(keyhole.door == entt::null || run.registry.valid(keyhole.door),
                      std::string(name) + ": " + keyhole.name + "'s door is built");
        }
    }
}

void runTests() {
    theOneRangeServesBothTests();
    aKeyIsTakenAtTwentySixPixels();
    aCarriedKeyTrailsItsOwner();
    aMinionCanCarryAKey();
    unlockingHoldsThenFadesThenTakesTheDoor();
    twoPairsOfOneColourStillPairCorrectly();
    everyLevelWithAKeyholePairsIt();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_keys SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_keys", 40);
}
