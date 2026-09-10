// The ported economy state, against the original's own harness.
//
// `tools/verify_economy.gd` covers five things. Three of them are here:
// resource-node extraction and depletion, multi-resource costs with an atomic
// spend, and the resources_changed signal the HUD reads. The other two - the
// worker gather/deliver loop and the food loop - drive `unit.gd`, which is the
// largest file in the project and is its own slice.
//
// To re-derive from the original:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_economy.tscn
//
// It printed, on 26 August 2026:
//
//   ok  : extract(30) removes 30 (amount 170)
//   ok  : extract clamps to remaining, empties tree
//   ok  : tree reports empty
//   ok  : empty tree leaves resource group immediately
//   ok  : affords a wood+food cost
//   ok  : short on one resource -> not affordable
//   ok  : spends a multi-resource cost
//   ok  : both resources deducted
//   ok  : unaffordable spend rejected
//   ok  : rejected spend leaves resources untouched (atomic)
//   ok  : add() emits resources_changed(wood, <before+50>)

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/EventBus.hpp"
#include "sim/GameData.hpp"
#include "sim/GameState.hpp"
#include "sim/ResourceNode.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace WolfBrigade;

namespace {

const GameData& shipped() { return wb::Shipped(); }

// A fresh run: a bus nobody is listening to yet and a state just reset.
struct Run {
    EventBus bus;
    GameState state{shipped(), bus};

    Run() { state.Reset(); }
};

// --- 1. ResourceNode extract + deplete -----------------------------------

void testExtractTakesWhatIsAskedAndLeavesTheRest() {
    ResourceNode tree;
    tree.resource = Ids::kWood;
    tree.maxAmount = 200;
    tree.amount = 200;

    CHECK_EQ(tree.Extract(30), 30);
    CHECK_EQ(tree.amount, 170);
}

void testExtractClampsToWhatIsLeftRatherThanMintingWood() {
    // The conservation property the whole economy rests on. A version that
    // returned what was ASKED for would let a worker bank a full load from a
    // nearly-empty tree, and the total wood in the world would grow.
    ResourceNode tree;
    tree.maxAmount = 200;
    tree.amount = 170;

    CHECK_EQ(tree.Extract(500), 170);
    CHECK_EQ(tree.amount, 0);
    CHECK_MSG(tree.IsEmpty(), "the tree must report empty");
}

void testAnEmptyNodeStopsBeingATargetImmediately() {
    // The original leaves the resource group the instant the node empties,
    // rather than when its fade-out finishes - so no worker walks to a tree
    // that is already gone.
    ResourceNode tree;
    tree.amount = 10;
    CHECK_MSG(tree.Harvestable(), "a tree with wood in it is a target");

    tree.Extract(10);
    CHECK_MSG(!tree.Harvestable(), "an emptied tree is not");
}

void testANegativeExtractTakesNothing() {
    // Not in the original harness, and it is free here: mini(n, amount) in
    // GDScript with a negative n returns the negative, ADDS to the node and
    // hands the worker a debt. Clamped at both ends.
    ResourceNode tree;
    tree.amount = 100;
    CHECK_EQ(tree.Extract(-5), 0);
    CHECK_EQ(tree.amount, 100);
}

void testFractionDrivesTheVisualAndSurvivesAZeroMaximum() {
    ResourceNode tree;
    tree.maxAmount = 200;
    tree.amount = 50;
    CHECK_NEAR(tree.Fraction(), 0.25f);

    // A node authored with no maximum would divide by zero. The original
    // guards with maxi(max_amount, 1) for the same reason.
    tree.maxAmount = 0;
    tree.amount = 0;
    CHECK_NEAR(tree.Fraction(), 0.0f);
}

// --- 2. Multi-resource costs ---------------------------------------------

void testAffordabilityAndAtomicSpending() {
    Run run;

    const int wood = run.state.Amount(Ids::kWood);
    const int food = run.state.Amount(Ids::kFood);

    // The shipped economy: 300 wood, 100 food, at Normal with no meta owned.
    CHECK_EQ(wood, 300);
    CHECK_EQ(food, 100);

    CHECK_MSG(run.state.CanAfford({{Ids::kWood, 75}, {Ids::kFood, 20}}),
              "a wood+food cost inside the balance is affordable");
    CHECK_MSG(!run.state.CanAfford({{Ids::kWood, 75}, {Ids::kFood, 999999}}),
              "short on ONE resource means not affordable");

    CHECK_MSG(run.state.TrySpend({{Ids::kWood, 75}, {Ids::kFood, 20}}),
              "and it spends");
    CHECK_EQ(run.state.Amount(Ids::kWood), wood - 75);
    CHECK_EQ(run.state.Amount(Ids::kFood), food - 20);
}

void testARejectedSpendChangesNothing() {
    // Atomicity, and the reason TrySpend exists rather than a Spend that
    // deducts as it goes: a player who can afford the wood but not the food
    // must not lose the wood.
    Run run;
    run.state.TrySpend({{Ids::kWood, 75}, {Ids::kFood, 20}});

    const int wood = run.state.Amount(Ids::kWood);
    const int food = run.state.Amount(Ids::kFood);

    CHECK_MSG(!run.state.TrySpend({{Ids::kWood, 10}, {Ids::kFood, 999999}}),
              "an unaffordable spend is rejected");
    CHECK_EQ(run.state.Amount(Ids::kWood), wood);
    CHECK_EQ(run.state.Amount(Ids::kFood), food);
}

// --- 3. The signal the HUD reads -----------------------------------------

void testAddAnnouncesTheNewTotalNotTheDelta() {
    // The HUD repaints from this and never polls, so the number carried has to
    // be what to display. A delta would make every listener keep its own
    // running total and disagree the first time one missed an event.
    Run run;

    std::vector<std::pair<std::string, int>> heard;
    const int id = run.bus.resourcesChanged.Connect(
        [&heard](const std::string& resource, int amount) { heard.emplace_back(resource, amount); });

    const int before = run.state.Amount(Ids::kWood);
    run.state.Add(Ids::kWood, 50);
    run.bus.resourcesChanged.Disconnect(id);

    CHECK_EQ(static_cast<int>(heard.size()), 1);
    if (heard.size() != 1) return;
    CHECK(heard[0].first == Ids::kWood);
    CHECK_EQ(heard[0].second, before + 50);
}

void testASpendAnnouncesEveryResourceItTouched() {
    Run run;
    int announcements = 0;
    run.bus.resourcesChanged.Connect([&announcements](const std::string&, int) { ++announcements; });

    run.state.TrySpend({{Ids::kWood, 10}, {Ids::kFood, 5}});
    CHECK_EQ(announcements, 2);

    // And a rejected one announces nothing at all: a HUD that flashed a
    // deduction that did not happen is worse than one that missed it.
    announcements = 0;
    run.state.TrySpend({{Ids::kWood, 999999}});
    CHECK_EQ(announcements, 0);
}

void testAListenerThatDisconnectsItselfDoesNotBreakTheEmission() {
    // Not in the original harness because Godot handles it. Here the slot list
    // is a vector, and a listener erasing itself from inside its own callback
    // would invalidate the loop calling it - a panel closing on the event that
    // told it to.
    EventBus bus;
    int calls = 0;
    int second = 0;

    int first = 0;
    first = bus.resourcesChanged.Connect([&](const std::string&, int) {
        ++calls;
        bus.resourcesChanged.Disconnect(first);
    });
    second = bus.resourcesChanged.Connect([&](const std::string&, int) { ++calls; });
    (void)second;

    bus.resourcesChanged.Emit("wood", 1);
    CHECK_EQ(calls, 2);

    // And the disconnect took effect for the next one.
    calls = 0;
    bus.resourcesChanged.Emit("wood", 2);
    CHECK_EQ(calls, 1);
}

// --- 4. Difficulty, which scales the starting balance --------------------

void testDifficultyScalesTheStartingBalance() {
    // Not a section of verify_economy - difficulty has its own harness - but
    // Reset reads it, so a wrong multiplier here shows up as an economy that
    // starts wrong on every difficulty but Normal.
    Run run;

    run.state.SetDifficulty("easy");
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 450);   // 300 * 1.5

    run.state.SetDifficulty("hard");
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 240);   // 300 * 0.8

    // An unknown id resolves to the data default rather than to nothing, so a
    // harness that skips the menu behaves as Normal.
    run.state.SetDifficulty("impossible");
    CHECK(run.state.CurrentDifficulty() == "normal");
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 300);
}

void testAnAbsentDifficultyKeyIsOneRatherThanZero() {
    // A new difficulty axis added to the JSON must be a no-op until code reads
    // it. Returning 0.0 for an unknown key would silently zero whatever the
    // first caller multiplied by it.
    Run run;
    CHECK_NEAR(run.state.DifficultyMultiplier("an_axis_nobody_has_added_yet"), 1.0f);
}

void testWaveCountsScaleAndNeverRoundAGroupAway() {
    Run run;
    run.state.SetDifficulty("easy");
    CHECK_EQ(run.state.ScaleWaveCount(10), 7);
    CHECK_EQ(run.state.ScaleWaveCount(1), 1);

    // An empty group stays empty. "At least one" applies to a group that has
    // something in it, not to one the wave did not list.
    CHECK_EQ(run.state.ScaleWaveCount(0), 0);

    // The floor itself is NOT reachable at shipped values, and the first
    // version of this case pretended otherwise: 0.7 of one brute rounds to
    // one anyway, so removing the max(1, ...) changed nothing and the mutation
    // walked straight through. It takes a gentler preset than any that ships.
    const wb::ScratchData gentle("econ", "difficulty.json", R"({
      "default": "normal",
      "order": ["gentle", "normal"],
      "presets": {
        "gentle": { "wave_size_mult": 0.2, "starting_resources_mult": 1.0 },
        "normal": { "wave_size_mult": 1.0, "starting_resources_mult": 1.0 }
      }
    })");
    GameData data;
    data.LoadAll(gentle.Path());
    EventBus bus;
    GameState state(data, bus);
    state.SetDifficulty("gentle");

    CHECK_MSG(state.ScaleWaveCount(1) == 1,
              "a wave that lists one brute must still send one");
    CHECK_EQ(state.ScaleWaveCount(10), 2);
    CHECK_EQ(state.ScaleWaveCount(0), 0);
}

// --- 5. Phase ------------------------------------------------------------

void testWinningAndLosingAreOneWayAndFireOnce() {
    // The last raider on the field and the Town Hall falling can land in either
    // order, and the simulation does not control which. Whichever gets there
    // first decides.
    Run run;
    int won = 0;
    int lost = 0;
    run.bus.gameWon.Connect([&won] { ++won; });
    run.bus.gameLost.Connect([&lost] { ++lost; });

    CHECK_MSG(run.state.IsPlaying(), "a fresh run is playing");
    run.state.Win();
    CHECK_EQ(won, 1);
    CHECK(run.state.CurrentPhase() == GameState::Phase::Won);

    run.state.Win();
    CHECK_MSG(won == 1, "winning twice fires once");
    run.state.Lose();
    CHECK_MSG(lost == 0, "and a loss after a win is refused");
    CHECK(run.state.CurrentPhase() == GameState::Phase::Won);
}

void testResetKeepsTheChosenDifficultyAndLevel() {
    // Restart rebuilds the run, not the player's choices. Clearing these would
    // silently put a player who picked Hard back on Normal, and back on the
    // first level, the first time they died.
    Run run;
    run.state.SetDifficulty("hard");
    run.state.SetLevel("level_1");

    run.state.Reset();
    CHECK(run.state.CurrentDifficulty() == "hard");
    CHECK_MSG(run.state.Level() == "level_1", "and still on its level");

    // What Reset DOES clear - including the restructure's two run-wide
    // switches, which belong to the run as much as the wave does.
    run.state.MarkResearched("sharper_arrows");
    run.state.SetCurrentWave(4);
    run.state.SetWorkersSheltered(true);
    run.state.SetHeroDown(1800.0);
    run.state.Reset();
    CHECK_MSG(!run.state.IsResearched("sharper_arrows"), "researched upgrades are a run's, not a player's");
    CHECK_EQ(run.state.CurrentWave(), 0);
    CHECK_MSG(!run.state.WorkersSheltered(), "a restart rings the shelter bell off");
    CHECK_MSG(!run.state.HeroDown(), "and forgets a pending respawn");
}

void testAnUnknownLevelResolvesToTheDefault() {
    // `game_state.gd` current_level(): an unset or unknown choice is the data
    // default, so a harness booting a match directly plays level 1, and a save
    // naming a level that no longer ships still opens.
    Run run;
    CHECK_MSG(run.state.CurrentLevel() == "level_1", "nothing chosen is level 1");
    run.state.SetLevel("level_42");
    CHECK_MSG(run.state.CurrentLevel() == "level_1", "an unknown level is level 1");
    CHECK_MSG(run.state.Level() == "level_42", "without rewriting what was chosen");
}

// --- 6. Persistent meta --------------------------------------------------

void testOwnedMetaAddsToTheStartingBalanceOnceAndPerLevel() {
    // deeper_coffers grants +40 wood a level, and the shipped file allows
    // three. The bonus is flat and additive, and it lands only on a fresh
    // Reset: a Continue restores balances that already banked it, and adding it
    // again on resume would compound every time the player reloaded.
    Run run;
    run.state.SetMetaLevels({{"deeper_coffers", 2}});
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 300 + 80);

    // Applied AFTER the difficulty multiplier, not before: it is a flat grant,
    // so Hard does not shrink it.
    run.state.SetDifficulty("hard");
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 240 + 80);

    // An upgrade the player does not own contributes nothing.
    run.state.SetMetaLevels({{"deeper_coffers", 0}});
    run.state.SetDifficulty("normal");
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 300);

    // And one whose effects target a unit does not touch the balance.
    run.state.SetMetaLevels({{"veteran_soldiers", 3}});
    run.state.Reset();
    CHECK_EQ(run.state.Amount(Ids::kWood), 300);
    CHECK_EQ(run.state.Amount(Ids::kFood), 100);
}

} // namespace

static void runTests() {
    testExtractTakesWhatIsAskedAndLeavesTheRest();
    testExtractClampsToWhatIsLeftRatherThanMintingWood();
    testAnEmptyNodeStopsBeingATargetImmediately();
    testANegativeExtractTakesNothing();
    testFractionDrivesTheVisualAndSurvivesAZeroMaximum();

    testAffordabilityAndAtomicSpending();
    testARejectedSpendChangesNothing();

    testAddAnnouncesTheNewTotalNotTheDelta();
    testASpendAnnouncesEveryResourceItTouched();
    testAListenerThatDisconnectsItselfDoesNotBreakTheEmission();

    testDifficultyScalesTheStartingBalance();
    testAnAbsentDifficultyKeyIsOneRatherThanZero();
    testWaveCountsScaleAndNeverRoundAGroupAway();

    testWinningAndLosingAreOneWayAndFireOnce();
    testResetKeepsTheChosenDifficultyAndLevel();
    testAnUnknownLevelResolvesToTheDefault();
    testOwnedMetaAddsToTheStartingBalanceOnceAndPerLevel();
}

TEST_MAIN("test_wb_economy", 45)
