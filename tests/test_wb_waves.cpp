// The ported wave director, against numbers the original COMPUTED.
//
// Every slice before this one asserted something structural - a state
// transition read out of the GDScript, a key count, a balance recomputed from
// the same file both sides read. A shared misreading would have passed both.
//
// These are different. 30 raiders, 1 brute, 31 across five waves, reaching wave
// 5: those are the output of three hundred simulation steps through the
// schedule, the difficulty scaling and the spawn interval, and there is no way
// to arrive at them by reading. This is the first slice where the oracle
// actually constrains the port.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_waves.tscn
//
// It printed, on 26 August 2026:
//
//   ok  : spawned 30 raiders total (got 30)
//   ok  : spawned 1 brute in wave 5 (got 1)
//   ok  : 31 enemies across all 5 waves (got 31)
//   ok  : reached wave 5 (got 5)
//   ok  : still playing before the field is cleared
//   ok  : game_won fired once when all waves cleared
//   ok  : GameState is WON
//   ok  : playing before the Town Hall falls
//   ok  : game_lost fired when Town Hall destroyed
//   ok  : GameState is LOST
//   ok  : no premature victory before the final wave starts
//   ok  : empty final wave resolves to victory (no soft-lock)
//
// The STEPPING is part of that specification, not an implementation detail.
// The harness drives `wd._process(2.0)` exactly 300 times with everything else
// frozen. A port that accumulated instead of stepping, or stepped at a
// different dt, drifts - and the debugging goes into the port when the
// disagreement is in the harness.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/WaveDirector.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

// The harness's numbers: the right edge of the world and the ground line.
constexpr float kEnemyX = 5960.0f;
constexpr float kGroundY = 800.0f;

// And its stepping. 300 steps of two seconds is ~600s of simulation, which
// clears the last scripted wave at t=420 with room to spare.
// A DOUBLE, like the GDScript's own dt. `2.0f` happens to be exact, but the
// habit is what matters: 0.25f is not 0.25, and a schedule stepped by the wrong
// number drifts a wave at a time.
constexpr double kStep = 2.0;
constexpr int kSteps = 300;

// A director with somewhere to put its spawns, and a record of what came out.
struct Field {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    WaveDirector director{wb::Shipped(), state, bus};

    std::vector<UnitStats> spawned;
    std::vector<int> wavesStarted;
    int wonCount{0};
    int lostCount{0};
    int allClearedCount{0};

    explicit Field(const GameData& data = wb::Shipped()) : state(data, bus), director(data, state, bus) {
        state.Reset();
        bus.gameWon.Connect([this] { ++wonCount; });
        bus.gameLost.Connect([this] { ++lostCount; });
        bus.allWavesCleared.Connect([this] { ++allClearedCount; });
        bus.waveStarted.Connect([this](int index) { wavesStarted.push_back(index); });
    }

    void Begin() {
        director.Setup(
            [this](const UnitStats& stats, const glm::vec2&) {
                spawned.push_back(stats);
                // Mirrors main.spawn_unit, which emits unit_spawned so the
                // director counts what it just handed out. Without this it
                // never knows anything is alive and declares victory on the
                // frame the last one spawns.
                director.OnEnemySpawned();
            },
            kEnemyX, kGroundY);
    }

    void Run(int steps = kSteps, double step = kStep) {
        for (int i = 0; i < steps; ++i) director.Step(step);
    }

    int CountOf(const std::string& id) const {
        int count = 0;
        for (const UnitStats& stats : spawned) {
            if (stats.id == id) ++count;
        }
        return count;
    }
};

// --- 1. The schedule, and victory when it is cleared ---------------------

void testTheScheduleSpawnsWhatTheOriginalCounted() {
    Field field;
    field.Begin();
    field.Run();

    // 2 + 4 + 6 + 8 + 10 raiders, and the brute that arrives with wave 5.
    CHECK_EQ(field.CountOf(Ids::kRaider), 30);
    CHECK_EQ(field.CountOf(Ids::kBrute), 1);
    CHECK_EQ(static_cast<int>(field.spawned.size()), 31);
    CHECK_EQ(field.state.CurrentWave(), 5);

    // Every wave announced itself, in order.
    CHECK_EQ(static_cast<int>(field.wavesStarted.size()), 5);
    for (size_t i = 0; i < field.wavesStarted.size(); ++i) {
        CHECK_EQ(field.wavesStarted[i], static_cast<int>(i) + 1);
    }
}

void testTheFieldMustBeClearedBeforeTheRunIsWon() {
    Field field;
    field.Begin();
    field.Run();

    // Everything has spawned and nothing has died.
    CHECK_MSG(field.state.IsPlaying(), "still playing while enemies are alive");
    CHECK_EQ(field.wonCount, 0);

    // Kill all but one and it is still not over: the LAST one decides.
    for (int i = 0; i < 30; ++i) field.director.OnEnemyDied();
    CHECK_MSG(field.state.IsPlaying(), "thirty of thirty-one is not a cleared field");
    CHECK_EQ(field.wonCount, 0);

    field.director.OnEnemyDied();
    CHECK_EQ(field.wonCount, 1);
    CHECK_EQ(field.allClearedCount, 1);
    CHECK(field.state.CurrentPhase() == GameState::Phase::Won);
}

void testTheTownHallFallingLosesTheRun() {
    Field field;
    field.Begin();
    field.Run(60);   // 120s: waves 1 and 2 are out, the rest are not

    CHECK_MSG(field.state.IsPlaying(), "playing before the Town Hall falls");
    field.director.OnTownHallDestroyed();
    CHECK_EQ(field.lostCount, 1);
    CHECK(field.state.CurrentPhase() == GameState::Phase::Lost);

    // And the director stops. A defeated player must not keep being spawned on.
    const int spawnedAtDefeat = static_cast<int>(field.spawned.size());
    field.Run();
    CHECK_EQ(static_cast<int>(field.spawned.size()), spawnedAtDefeat);
}

// --- 2. The soft-lock the original wrote a case against -------------------

void testAnEmptyFinalWaveStillResolvesToVictory() {
    // Victory is checked every step rather than only when something dies. A
    // final wave contributing zero spawns would otherwise never resolve:
    // nothing dies, so nothing asks, and the run sits at "playing" forever on
    // an empty field with the player having nothing to do.
    const wb::ScratchData empty("waves", "waves.json", R"({
      "spawn_edge": "right", "spawn_interval": 0.8,
      "waves": [
        { "index": 1, "time": 10, "spawns": [ { "unit": "raider", "count": 2 } ] },
        { "index": 2, "time": 60, "spawns": [] }
      ],
      "endless": { "start_delay": 90, "interval": 75, "unit": "raider" }
    })");
    GameData data;
    data.LoadAll(empty.Path());

    Field field(data);
    field.Begin();

    // Up to just before the empty final wave: two raiders out, still playing.
    field.Run(20, 2.0);
    CHECK_EQ(static_cast<int>(field.spawned.size()), 2);
    CHECK_MSG(field.state.IsPlaying(), "no premature victory before the final wave starts");
    CHECK_EQ(field.wonCount, 0);

    // Clear the field, then let the empty wave arrive.
    field.director.OnEnemyDied();
    field.director.OnEnemyDied();
    CHECK_MSG(field.state.IsPlaying(),
              "clearing the field before the last wave is not a win either");

    field.Run(20, 2.0);
    CHECK_MSG(!field.state.IsPlaying(), "the empty final wave must resolve");
    CHECK_EQ(field.wonCount, 1);
}

// --- 3. The spawn interval, which is what makes a wave a column ----------

void testAWaveArrivesAsAColumnRatherThanAllAtOnce() {
    // spawn_interval is 0.8 and the harness steps at 2.0, so a step drains
    // three: the accumulator is primed to a full interval when the wave
    // starts, then takes the step's two seconds on top.
    //
    // Worth pinning, because it is where a port most easily drifts. Priming
    // with zero instead loses one spawn per wave; not priming the accumulator
    // at all delays the whole wave by an interval; draining with an `if`
    // instead of a `while` spreads a ten-raider wave over ten steps.
    Field field;
    field.Begin();

    // t=60 is wave 1. Thirty steps of 2.0 gets there with the wave starting on
    // the step that crosses it.
    for (int i = 0; i < 30; ++i) {
        field.director.Step(2.0);
        if (i < 29) CHECK_MSG(field.spawned.empty(), "nothing spawns before the first wave");
    }
    CHECK_EQ(static_cast<int>(field.spawned.size()), 2);
    CHECK_EQ(field.director.QueuedSpawns(), 0);
}

void testTheFirstOfAWaveArrivesOnTheStepTheWaveStarts() {
    // The accumulator is PRIMED to a full interval when a wave starts, so the
    // first enemy comes out on the same step rather than 0.8s later. At the
    // harness's two-second dt that is invisible - both answers spawn the same
    // two raiders - which is exactly why this needs its own case at a small
    // one. Removing the priming otherwise changes nothing any test can see,
    // and a wave that begins with a beat of silence is a real difference at
    // sixty frames a second.
    const wb::ScratchData prompt("waves", "waves.json", R"({
      "spawn_interval": 0.8,
      "waves": [ { "index": 1, "time": 1, "spawns": [ { "unit": "raider", "count": 4 } ] } ],
      "endless": { "unit": "raider" }
    })");
    GameData data;
    data.LoadAll(prompt.Path());

    Field field(data);
    field.Begin();

    // TEN steps of 0.1 do not reach t=1. They come to 0.9999999999999999, and
    // this case failed the moment the simulation's scalars were widened to
    // double - in single precision the same ten steps overshoot to 1.0000001
    // and the wave had already started. The fixture was sitting on the
    // boundary; the eleventh step is unambiguously past it.
    for (int i = 0; i < 10; ++i) field.director.Step(0.1);
    CHECK_MSG(field.spawned.empty(), "nothing before the wave's time");

    // The step that crosses t=1 starts the wave AND lets the first one out.
    field.director.Step(0.1);
    CHECK_EQ(static_cast<int>(field.spawned.size()), 1);

    // And the rest follow at one every 0.8s, not all at once. Sampled well
    // clear of the boundary in both directions: the accumulator is 0.1 after
    // the first spawn, and asking exactly when it reaches 0.8 is asking what
    // seven additions of 0.1f come to.
    for (int i = 0; i < 5; ++i) field.director.Step(0.1);   // ~0.6
    CHECK_EQ(static_cast<int>(field.spawned.size()), 1);
    for (int i = 0; i < 3; ++i) field.director.Step(0.1);   // ~0.9
    CHECK_EQ(static_cast<int>(field.spawned.size()), 2);
}

void testALongStepStartsEveryWaveItPassedRatherThanOne() {
    // A while, not an if. One enormous step - a load hitch, or a harness
    // fast-forwarding - has to start every wave whose time it crossed, or the
    // schedule quietly loses the ones in between.
    Field field;
    field.Begin();
    field.director.Step(500.0);

    CHECK_EQ(static_cast<int>(field.wavesStarted.size()), 5);
    CHECK_EQ(field.state.CurrentWave(), 5);
}

// --- 4. Difficulty, which decides how many ------------------------------

void testDifficultyScalesTheScheduleAndTheEnemiesInIt() {
    {
        Field field;
        field.state.SetDifficulty("hard");
        field.state.Reset();
        field.Begin();
        field.Run();

        // 1.4x each group, rounded: 3 + 6 + 8 + 11 + 14 raiders, 1 brute.
        CHECK_EQ(field.CountOf(Ids::kRaider), 42);
        CHECK_EQ(field.CountOf(Ids::kBrute), 1);

        // And they are tougher. A raider is 40 HP at 1.3x.
        CHECK_MSG(!field.spawned.empty(), "hard must still spawn something");
        if (!field.spawned.empty()) CHECK_EQ(field.spawned.front().maxHp, 52);
    }
    {
        Field field;
        field.state.SetDifficulty("easy");
        field.state.Reset();
        field.Begin();
        field.Run();

        // 0.7x: 1 + 3 + 4 + 6 + 7, and the single brute survives the rounding.
        CHECK_EQ(field.CountOf(Ids::kRaider), 21);
        CHECK_EQ(field.CountOf(Ids::kBrute), 1);
        if (!field.spawned.empty()) CHECK_EQ(field.spawned.front().maxHp, 32);
    }
}

// --- 5. Endless is gone ---------------------------------------------------

void testALeftoverEndlessBlockIsIgnored() {
    // The game removed Endless mode (73999ce), and the port with it. A
    // waves.json still carrying the old block - a file from before the
    // removal, or a mod - must not bring it back: the schedule ends, the
    // countdown ends, and clearing the field wins. Every other endless case
    // this section held went with the mode; this one is what is left to say.
    const wb::ScratchData old("waves", "waves.json", R"({
      "spawn_edge": "right", "spawn_interval": 0.8,
      "waves": [ { "index": 1, "time": 1, "spawns": [ { "unit": "raider", "count": 2 } ] } ],
      "endless": { "start_delay": 1, "interval": 1, "unit": "raider", "base_count": 5 }
    })");
    GameData data;
    data.LoadAll(old.Path());

    Field field(data);
    field.Begin();
    field.Run(100, 0.5);   // t=50, far past where an endless wave would have started

    CHECK_EQ(field.CountOf(Ids::kRaider), 2);
    CHECK_EQ(field.state.CurrentWave(), 1);
    CHECK(std::fabs(field.director.SecondsToNextWave() - -1.0) < 1e-4);

    const int alive = field.director.AliveEnemies();
    for (int i = 0; i < alive; ++i) field.director.OnEnemyDied();
    CHECK_EQ(field.wonCount, 1);
}

// --- 6. Setup reads the data rather than assuming it --------------------

void testSecondsToNextWaveCountsDownAndEndsAtMinusOne() {
    Field field;
    field.Begin();

    // Wave 1 is at t=60. Compared as the double it is - CHECK_NEAR takes
    // floats, and the clock this port keeps is deliberately not one.
    CHECK(std::fabs(field.director.SecondsToNextWave() - 60.0) < 1e-4);
    field.director.Step(10.0);
    CHECK(std::fabs(field.director.SecondsToNextWave() - 50.0) < 1e-4);

    field.Run();
    CHECK(std::fabs(field.director.SecondsToNextWave() - -1.0) < 1e-4);
}

} // namespace

static void runTests() {
    testTheScheduleSpawnsWhatTheOriginalCounted();
    testTheFieldMustBeClearedBeforeTheRunIsWon();
    testTheTownHallFallingLosesTheRun();

    testAnEmptyFinalWaveStillResolvesToVictory();

    testAWaveArrivesAsAColumnRatherThanAllAtOnce();
    testTheFirstOfAWaveArrivesOnTheStepTheWaveStarts();
    testALongStepStartsEveryWaveItPassedRatherThanOne();

    testDifficultyScalesTheScheduleAndTheEnemiesInIt();

    testALeftoverEndlessBlockIsIgnored();

    testSecondsToNextWaveCountsDownAndEndsAtMinusOne();
}

TEST_MAIN("test_wb_waves", 60)
