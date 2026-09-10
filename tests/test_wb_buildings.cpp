// The ported buildings, against the original's own harness.
//
// `verify_buildings` covers construction, training, the defensive tower and the
// footprint maths that placement and picking both rest on. Its computed number
// is the tower's: **two bolts pooled** after thirty steps of 0.1s at 0.7
// attacks per second, quantised to an 8 Hz acquisition tick.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_buildings.tscn
//
// It printed, on 26 August 2026:
//
//   ok  : placed barracks starts CONSTRUCTING
//   ok  : cannot train while constructing
//   ok  : reaches COMPLETE at build_time
//   ok  : emits building_completed on completion
//   ok  : can train soldier once complete
//   ok  : pre-placed Town Hall starts complete
//   ok  : Town Hall is a deposit point
//   ok  : worker enters BUILDING on command_build
//   ok  : building completed by the worker
//   ok  : worker returns to IDLE when done
//   ok  : affords a soldier
//   ok  : training deducted the full soldier cost (wood + food)
//   ok  : soldier queued
//   ok  : emits unit_trained(soldier) after train_time
//   ok  : queue empties after training
//   ok  : overlapping an existing building is INVALID
//   ok  : clear ground is VALID
//   ok  : building_at finds the building under the point
//   ok  : tower is buildable
//   ok  : tower fires at an enemy in range (2 shots pooled)
//   ok  : non-combat building (town hall) never fires
//
// The worker-builds and placement-mode sections need the BUILDING state on the
// unit and the placement system, which are the next slice; the selection lines
// need Selection and Commands. What is here is everything a building does on
// its own.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Building.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/Lane.hpp"
#include "sim/Projectiles.hpp"
#include "sim/Unit.hpp"
#include "sim/World.hpp"

#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

constexpr float kGroundY = 800.0f;

// A town with buildings and a lane in it.
class Town final : public World {
public:
    Lane lane;
    ProjectilePool bolts;
    std::vector<std::unique_ptr<Unit>> units;

    // One optional node, for the case that asks which of a tree and a
    // building site an idle worker picks.
    mutable ResourceNode node;
    bool hasNode{false};

    ResourceNode* NearestHarvestable(float) const override {
        return hasNode && node.Harvestable() ? &node : nullptr;
    }

    // Deposit points are the COMPLETE buildings that say they are one, which is
    // the group membership the original maintains as construction finishes.
    int NearestDeposit(float x) const override {
        int best = -1;
        float bestDistance = 0.0f;
        for (size_t i = 0; i < buildings.size(); ++i) {
            if (!buildings[i]->IsDepositPoint()) continue;
            const float distance = std::fabs(x - buildings[i]->Position().x);
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    }
    bool DepositExists(int index) const override {
        return index >= 0 && index < static_cast<int>(buildings.size()) &&
               buildings[static_cast<size_t>(index)]->IsDepositPoint();
    }
    glm::vec2 DepositPosition(int index) const override {
        return DepositExists(index) ? buildings[static_cast<size_t>(index)]->Position()
                                    : glm::vec2(0.0f);
    }

    // The anti-deadlock scan: any unfinished building of this faction, at any
    // distance, nearest first.
    Building* NearestUnfinishedBuilding(const std::string& faction, float x) const override {
        Building* best = nullptr;
        float bestDistance = 0.0f;
        for (const auto& building : buildings) {
            if (building->Faction() != faction) continue;
            if (building->IsComplete() || !building->IsAlive()) continue;
            const float distance = std::fabs(x - building->Position().x);
            if (best == nullptr || distance < bestDistance) {
                best = building.get();
                bestDistance = distance;
            }
        }
        return best;
    }

    Unit* NearestEnemyUnit(const std::string& faction, float x, float maxRange) const override {
        return lane.NearestEnemy(faction, x, maxRange);
    }
    Damageable* NearestEnemyBuilding(const std::string&, float) const override { return nullptr; }
    ProjectilePool* Projectiles() override { return &bolts; }

    std::vector<std::unique_ptr<Building>> buildings;
};

struct Site {
    EventBus bus;
    GameState state;
    Town town;

    std::vector<std::string> trained;
    std::vector<const Building*> completed;
    std::vector<const Building*> destroyed;

    // The run reads `data`, which is the shipped files unless a case needs a
    // flag the shipped files do not set.
    explicit Site(const GameData& data = wb::Shipped()) : state(data, bus) {
        state.Reset();
        bus.unitTrained.Connect(
            [this](const std::string& id, const glm::vec2&) { trained.push_back(id); });
        bus.buildingCompleted.Connect([this](Building* b) { completed.push_back(b); });
        bus.buildingDestroyed.Connect([this](Building* b) { destroyed.push_back(b); });
    }

    Building* Place(const std::string& id, float x, bool prePlaced) {
        auto building = std::make_unique<Building>(
            BuildingStats::FromJson(id, wb::Shipped().Building(id)), prePlaced, state, bus,
            town);
        building->SetPosition(glm::vec2(x, kGroundY));
        building->SetTrainTimes(wb::Shipped().Units());
        Building* raw = building.get();
        town.buildings.push_back(std::move(building));
        return raw;
    }

    Unit* SpawnUnit(const std::string& id, float x) {
        auto unit = std::make_unique<Unit>(
            UnitStats::FromJson(id, wb::Shipped().Unit(id)), state, bus, town);
        unit->SetPosition(glm::vec2(x, kGroundY));
        Unit* raw = unit.get();
        town.units.push_back(std::move(unit));
        town.lane.Register(raw);
        return raw;
    }

    Unit* SpawnEnemy(const std::string& id, float x) {
        auto unit = std::make_unique<Unit>(
            UnitStats::FromJson(id, wb::Shipped().Unit(id)), state, bus, town);
        unit->SetPosition(glm::vec2(x, kGroundY));
        Unit* raw = unit.get();
        town.units.push_back(std::move(unit));
        town.lane.Register(raw);
        return raw;
    }
};

// --- 1. Construction -----------------------------------------------------

void testAPlacedBuildingIsUnderConstructionUntilAWorkerFinishesIt() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);

    CHECK(barracks->CurrentState() == Building::State::Constructing);
    CHECK_MSG(!barracks->IsComplete(), "a placed barracks is scaffolding");

    // And it does nothing while it stands there. A barracks that trained while
    // half-built would let a player skip the entire build time by queuing early.
    CHECK_MSG(!barracks->CanTrain(Ids::kSoldier), "it cannot train yet");
    barracks->EnqueueTraining(Ids::kSoldier);
    CHECK_EQ(barracks->QueueLength(), 0);

    // Ten seconds of a worker's attention, which is what buildings.json asks
    // for. Poured in a step at a time, the way a worker pours it.
    //
    // A hundred pours of 0.1 come to 9.999999999999998, so they do NOT finish
    // it - and that is the ORIGINAL's behaviour too, since GDScript accumulates
    // in the same double. Asserted in both directions rather than stepped past,
    // because it pins the arithmetic: in single precision the same hundred
    // pours overshoot and this case would pass for the wrong reason.
    for (int i = 0; i < 100; ++i) barracks->AddBuildProgress(0.1);
    CHECK_MSG(!barracks->IsComplete(),
              "a hundred tenths is a hair under ten, in double as in GDScript");
    CHECK_EQ(static_cast<int>(site.completed.size()), 0);

    barracks->AddBuildProgress(0.1);
    CHECK_MSG(barracks->IsComplete(), "it must reach COMPLETE at build_time");
    CHECK_EQ(static_cast<int>(site.completed.size()), 1);
    CHECK_MSG(barracks->CanTrain(Ids::kSoldier), "and can train once complete");
}

void testProgressIsClampedAndCompletionFiresOnce() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);

    // One enormous pour. It finishes rather than overshooting into a number the
    // progress bar cannot draw.
    barracks->AddBuildProgress(1000.0);
    CHECK_NEAR(static_cast<float>(barracks->BuildProgress()), 10.0f);
    CHECK_EQ(static_cast<int>(site.completed.size()), 1);

    // And more progress on a finished building changes nothing. Any idle worker
    // may walk up to a site, so the same building gets progress from several
    // and the last one must not re-complete it.
    barracks->AddBuildProgress(50.0);
    CHECK_EQ(static_cast<int>(site.completed.size()), 1);
    CHECK_NEAR(static_cast<float>(barracks->BuildProgress()), 10.0f);
}

void testAPrePlacedTownHallStartsFinishedAndBanking() {
    Site site;
    Building* hall = site.Place(Ids::kTownHall, 1500.0f, true);

    CHECK_MSG(hall->IsComplete(), "the Town Hall is standing when the run starts");
    CHECK_MSG(hall->IsDepositPoint(), "and workers can bank at it");

    // A barracks is not one, however finished it is.
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);
    CHECK_MSG(!barracks->IsDepositPoint(), "a barracks is not a warehouse");
}

void testAHalfBuiltDepositIsNotADeposit() {
    // Not in the original harness directly - it falls out of the group being
    // joined on completion - and it is worth its own case: a worker walking to
    // scaffolding to bank wood would stand there until it was finished.
    Site site;

    // A Town Hall that is NOT pre-placed and has a build time is the only way
    // to construct this situation, so the fixture gives it one.
    const wb::ScratchData slow("bld", "buildings.json", R"({
      "town_hall": { "faction": "player", "build_time": 5.0, "max_hp": 1000,
                     "is_deposit_point": true, "trains": ["worker"],
                     "body_size": [120, 160] }
    })");
    GameData data;
    data.LoadAll(slow.Path());

    EventBus bus;
    GameState state(data, bus);
    Town town;
    Building hall(BuildingStats::FromJson(Ids::kTownHall, data.Building(Ids::kTownHall)), false,
                  state, bus, town);

    CHECK_MSG(!hall.IsDepositPoint(), "scaffolding is not a warehouse");
    hall.AddBuildProgress(5.0);
    CHECK_MSG(hall.IsDepositPoint(), "and it becomes one the moment it is finished");
}

// --- 2. Training ---------------------------------------------------------

void testABarracksTrainsOneUnitPerTrainTime() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);
    barracks->EnqueueTraining(Ids::kSoldier);
    CHECK_EQ(barracks->QueueLength(), 1);

    // The harness's own stepping: train_time / 0.25, plus four for slack.
    const int steps = static_cast<int>(8.0 / 0.25) + 4;
    for (int i = 0; i < steps; ++i) barracks->Step(0.25);

    CHECK_EQ(static_cast<int>(site.trained.size()), 1);
    if (!site.trained.empty()) CHECK(site.trained.front() == Ids::kSoldier);
    CHECK_EQ(barracks->QueueLength(), 0);
}

void testAQueueOfTwoTakesTwoFullTrainTimes() {
    // The remainder is reset rather than carried, so five soldiers take five
    // full train times - which is what the training bar in the HUD draws.
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);
    barracks->EnqueueTraining(Ids::kSoldier);   // 8.0s
    barracks->EnqueueTraining(Ids::kArcher);    // 7.0s

    for (int i = 0; i < 33; ++i) barracks->Step(0.25);   // 8.25s
    CHECK_EQ(static_cast<int>(site.trained.size()), 1);

    for (int i = 0; i < 29; ++i) barracks->Step(0.25);   // another 7.25s
    CHECK_EQ(static_cast<int>(site.trained.size()), 2);

    // In the order they were queued. A queue that came out backwards would
    // hand the player an archer when they asked for a soldier first.
    if (site.trained.size() == 2) {
        CHECK(site.trained[0] == Ids::kSoldier);
        CHECK(site.trained[1] == Ids::kArcher);
    }
}

void testABuildingRefusesToTrainWhatItDoesNotTrain() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);

    // A barracks trains soldiers and archers. A worker comes from the Town
    // Hall, and queueing one here has to be refused rather than silently
    // producing a worker from a barracks.
    CHECK_MSG(!barracks->CanTrain(Ids::kWorker), "a barracks does not train workers");
    barracks->EnqueueTraining(Ids::kWorker);
    CHECK_EQ(barracks->QueueLength(), 0);
}

void testATrainedUnitAppearsBesideTheBuildingRatherThanInsideIt() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);

    // Half the body plus the authored offset: 55 + 40.
    const glm::vec2 spawn = barracks->SpawnPoint();
    CHECK_NEAR(spawn.x, 2695.0f);
    CHECK_NEAR(spawn.y, kGroundY);

    // Which is outside its own footprint, or the unit spawns in a wall.
    CHECK_MSG(!barracks->ContainsPoint(spawn), "the spawn point is clear of the building");
}

// --- 3. The footprint that placement and picking both rest on ------------

void testTheFootprintStandsOnTheGroundRatherThanStraddlingIt() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);
    const Building::Rect box = barracks->Footprint();

    // 110 wide and 130 tall, centred on x and sitting ON the ground line.
    // Centring it on the origin instead would bury half the building and make
    // every overlap test wrong by half a body.
    CHECK_NEAR(box.min.x, 2545.0f);
    CHECK_NEAR(box.max.x, 2655.0f);
    CHECK_NEAR(box.max.y, kGroundY);
    CHECK_NEAR(box.min.y, kGroundY - 130.0f);

    // A click at chest height hits it; the same x well above it does not.
    CHECK_MSG(barracks->ContainsPoint(glm::vec2(2600.0f, kGroundY - 60.0f)), "a click on it hits");
    CHECK_MSG(!barracks->ContainsPoint(glm::vec2(2600.0f, kGroundY - 400.0f)), "the sky does not");
    CHECK_MSG(!barracks->ContainsPoint(glm::vec2(2800.0f, kGroundY - 60.0f)), "nor does next door");
}

void testOverlappingFootprintsAreRefusedAndClearGroundIsNot() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);
    const Building::Rect standing = barracks->Footprint();

    // A second barracks 60px away overlaps: two 110-wide buildings need 110
    // between their centres.
    Building* tooClose = site.Place(Ids::kBarracks, 2660.0f, true);
    CHECK_MSG(standing.Overlaps(tooClose->Footprint()), "60px apart is an overlap");

    Building* clear = site.Place(Ids::kBarracks, 2800.0f, true);
    CHECK_MSG(!standing.Overlaps(clear->Footprint()), "200px apart is clear ground");

    // And exactly touching is NOT an overlap, or a player could never build a
    // row of anything.
    Building* touching = site.Place(Ids::kBarracks, 2710.0f, true);
    CHECK_MSG(!standing.Overlaps(touching->Footprint()), "edge to edge is buildable");
}

// --- 4. The tower, and the buildings that are not one --------------------

void testATowerFiresTheBoltsTheOriginalCounted() {
    Site site;
    Building* tower = site.Place(Ids::kTower, 2000.0f, true);
    site.SpawnEnemy(Ids::kRaider, 2200.0f);   // inside its 300 range

    CHECK_MSG(tower->Stats().buildable, "the tower is offered in the build menu");

    for (int i = 0; i < 30; ++i) tower->Step(0.1);

    // Two. Three seconds at 0.7 shots a second is 2.1, and the acquisition
    // tick quantises the second one to a boundary rather than to 1.43s exactly.
    CHECK_EQ(site.town.bolts.PoolSize(), 2);
}

void testABuildingWithNoWeaponNeverFires() {
    Site site;
    Building* hall = site.Place(Ids::kTownHall, 2100.0f, true);
    site.SpawnEnemy(Ids::kRaider, 2150.0f);   // standing right beside it

    for (int i = 0; i < 30; ++i) hall->Step(0.1);

    CHECK_EQ(site.town.bolts.PoolSize(), 0);
}

void testATowerWithNothingInRangeHoldsItsFire() {
    Site site;
    Building* tower = site.Place(Ids::kTower, 2000.0f, true);
    site.SpawnEnemy(Ids::kRaider, 2500.0f);   // 500px, outside its 300 range

    for (int i = 0; i < 30; ++i) tower->Step(0.1);
    CHECK_EQ(site.town.bolts.PoolSize(), 0);
}

void testAnUnfinishedTowerIsHarmless() {
    Site site;
    Building* tower = site.Place(Ids::kTower, 2000.0f, false);
    site.SpawnEnemy(Ids::kRaider, 2200.0f);

    for (int i = 0; i < 30; ++i) tower->Step(0.1);
    CHECK_EQ(site.town.bolts.PoolSize(), 0);

    // Finish it and it opens up.
    tower->AddBuildProgress(8.0);
    for (int i = 0; i < 30; ++i) tower->Step(0.1);
    CHECK_MSG(site.town.bolts.PoolSize() > 0, "a finished tower fires");
}

// --- 5. Damage and death -------------------------------------------------

void testABuildingTakesDamageAndAnnouncesItBeforeItFalls() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);

    int announced = 0;
    site.bus.damageDealt.Connect([&announced](const glm::vec2&, int, const std::string&) {
        ++announced;
    });

    barracks->TakeDamage(100);
    CHECK_EQ(barracks->Hp(), 400);
    CHECK_MSG(barracks->IsAlive(), "it is still standing");
    CHECK_EQ(announced, 1);

    // The finishing blow still shows its number, and fires the destroyed
    // signal exactly once.
    barracks->TakeDamage(9999);
    CHECK_EQ(announced, 2);
    CHECK_EQ(barracks->Hp(), 0);
    CHECK_MSG(!barracks->IsAlive(), "and now it is not");
    CHECK_EQ(static_cast<int>(site.destroyed.size()), 1);

    barracks->TakeDamage(10);
    CHECK_EQ(announced, 2);
    CHECK_EQ(static_cast<int>(site.destroyed.size()), 1);
}

void testADestroyedTownHallStopsBeingSomewhereToBank() {
    Site site;
    Building* hall = site.Place(Ids::kTownHall, 1500.0f, true);
    CHECK_MSG(hall->IsDepositPoint(), "it is a deposit while it stands");
    CHECK_MSG(site.town.NearestDeposit(1600.0f) >= 0, "and the world can find it");

    hall->Destroy();
    CHECK_MSG(!hall->IsDepositPoint(), "a ruin is not");
    CHECK_EQ(site.town.NearestDeposit(1600.0f), -1);
}

void testTheBoardFreezesForBuildingsToo() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, true);
    barracks->EnqueueTraining(Ids::kSoldier);

    site.state.Win();
    for (int i = 0; i < 100; ++i) barracks->Step(0.25);

    CHECK_EQ(static_cast<int>(site.trained.size()), 0);
    CHECK_EQ(barracks->QueueLength(), 1);
}

// --- 6. Upgrades applied to something already standing -------------------

void testReinforcingADamagedWallHealsItAsWellAsTougheningIt() {
    Site site;
    Building* hall = site.Place(Ids::kTownHall, 1500.0f, true);
    hall->TakeDamage(300);
    CHECK_EQ(hall->Hp(), 700);

    CHECK_MSG(hall->ApplyUpgradeEffect("max_hp", 150.0), "max_hp is a field a building has");
    CHECK_EQ(hall->Stats().maxHp, 1150);

    // The current total rises with the cap. Raising only the cap would leave
    // the player's Town Hall on the same hit points behind a longer bar, which
    // is not what "reinforced walls" sounds like it did.
    CHECK_EQ(hall->Hp(), 850);
}

void testAnUpgradeTargetingAFieldNobodyHasIsReportedRatherThanSwallowed() {
    Site site;
    Building* hall = site.Place(Ids::kTownHall, 1500.0f, true);
    CHECK_MSG(!hall->ApplyUpgradeEffect("gather_rate", 1.0),
              "a unit's field on a building is a data typo, and must say so");
}

// --- 7. Four things the shipped data cannot show -------------------------
//
// Every case above runs against buildings.json as it ships, and four separate
// mutations survived that: a Town Hall is pre-placed AND has no build time, so
// which of the two finishes it is invisible; the training remainder only
// matters in a window narrower than any of these; the tower's 8 Hz gate does
// not change its rate at 0.7 shots a second; and the no-weapon guard is never
// reached, because a Town Hall has no attack RANGE either and so finds nothing
// to shoot at anyway.

void testABuildingWithNoBuildTimeIsFinishedEvenIfNobodyPlacedIt() {
    // The original completes on `pre_placed OR build_time <= 0`. Both are true
    // of a Town Hall, which hides which one did it - and a building with no
    // build time that is not pre-placed would otherwise sit at zero progress
    // forever, because the total it needs is already met and a worker pouring
    // into it changes nothing.
    Site site;
    Building* hall = site.Place(Ids::kTownHall, 1500.0f, false);
    CHECK_MSG(hall->IsComplete(), "nothing to build means nothing to wait for");
    CHECK_MSG(hall->IsDepositPoint(), "and it banks immediately");
}

void testTrainingStartsTheNextUnitFromZeroRatherThanFromTheOvershoot() {
    // This one had to be searched for, and that is the finding.
    //
    // The overshoot at the end of a unit is under one step, so carrying it can
    // only ever buy back a whole step once enough of them have piled up - and
    // at most step sizes it never does, because the accumulation error eats it.
    // Three archers at 7.0s and a step of 0.6 finish on exactly the same steps
    // either way. Four units at 3.0s and a step of 0.05 is the first
    // combination where the two answers separate at all, and by then they are
    // three steps apart.
    //
    // Which is the honest shape of this choice: it is not a bug that shows up
    // in a fight, it is a slow drift that shows up in a long queue. Worth
    // pinning precisely because nothing else would ever notice.
    const wb::ScratchData quick("bldq", "units.json", R"({
      "archer": { "display_name": "Archer", "faction": "player",
                  "trained_at": "barracks", "cost": {}, "train_time": 3.0,
                  "max_hp": 40, "damage": 6, "behavior": "ranged",
                  "body_size": [28, 38] }
    })");
    GameData data;
    data.LoadAll(quick.Path());

    EventBus bus;
    GameState state(data, bus);
    Town town;
    int trained = 0;
    bus.unitTrained.Connect([&trained](const std::string&, const glm::vec2&) { ++trained; });

    Building barracks(BuildingStats::FromJson(Ids::kBarracks, data.Building(Ids::kBarracks)),
                      true, state, bus, town);
    barracks.SetTrainTimes(data.Units());
    for (int i = 0; i < 4; ++i) barracks.EnqueueTraining(Ids::kArcher);

    // Reset finishes them on steps 61, 122, 183 and 244. Carry finishes them on
    // 61, 121, 181 and 241.
    for (int i = 0; i < 241; ++i) barracks.Step(0.05);
    CHECK_MSG(trained == 3, "the fourth archer needs its own full three seconds");

    for (int i = 0; i < 3; ++i) barracks.Step(0.05);
    CHECK_EQ(trained, 4);
}

void testATowersRateIsCappedByItsThinkingTickNotOnlyByItsCooldown() {
    // At the shipped 0.7 shots a second the cooldown is the binding constraint
    // and the 8 Hz gate never shows. A tower that reloads faster than it thinks
    // is where it does: acquisition happens on the tick, so the rate stops at
    // eight a second no matter what the cooldown says.
    const wb::ScratchData rapid("bld", "buildings.json", R"({
      "tower": { "faction": "player", "build_time": 0, "max_hp": 400,
                 "buildable": true, "trains": [],
                 "damage": 1, "attacks_per_sec": 100, "attack_range": 300,
                 "projectile_speed": 760, "body_size": [64, 150] }
    })");
    GameData data;
    data.LoadAll(rapid.Path());

    EventBus bus;
    GameState state(data, bus);
    Town town;

    Building tower(BuildingStats::FromJson(Ids::kTower, data.Building(Ids::kTower)), true, state,
                   bus, town);
    tower.SetPosition(glm::vec2(2000.0f, kGroundY));

    auto raider = std::make_unique<Unit>(
        UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)), state, bus, town);
    raider->SetPosition(glm::vec2(2200.0f, kGroundY));
    town.lane.Register(raider.get());

    // Twenty steps of 0.1 is two seconds. A cooldown of 0.01s is spent the
    // moment it is set, so nothing but the tick limits this - and the tick
    // allows eight a second, which over two seconds is sixteen. Without the
    // gate it would be one per step: twenty.
    for (int i = 0; i < 20; ++i) tower.Step(0.1);

    CHECK_EQ(town.bolts.PoolSize(), 16);
}

void testABuildingWithRangeButNoDamageStillNeverFires() {
    // The guard is on BOTH numbers. A Town Hall has neither, so removing it
    // changes nothing - it finds no target inside a range of zero. A building
    // with a range and no damage is what actually reaches the check, and it
    // must stay silent rather than loosing bolts that do nothing.
    const wb::ScratchData harmless("bld", "buildings.json", R"({
      "town_hall": { "faction": "player", "build_time": 0, "max_hp": 1000,
                     "is_deposit_point": true, "trains": ["worker"],
                     "damage": 0, "attack_range": 300, "attacks_per_sec": 1,
                     "body_size": [120, 160] }
    })");
    GameData data;
    data.LoadAll(harmless.Path());

    EventBus bus;
    GameState state(data, bus);
    Town town;

    Building hall(BuildingStats::FromJson(Ids::kTownHall, data.Building(Ids::kTownHall)), true,
                  state, bus, town);
    hall.SetPosition(glm::vec2(2100.0f, kGroundY));

    auto raider = std::make_unique<Unit>(
        UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)), state, bus, town);
    raider->SetPosition(glm::vec2(2150.0f, kGroundY));
    town.lane.Register(raider.get());

    for (int i = 0; i < 30; ++i) hall.Step(0.1);
    CHECK_EQ(town.bolts.PoolSize(), 0);
}

// --- 8. The worker that finishes it --------------------------------------
//
// The five lines of `verify_buildings` that need both halves of the port at
// once. Building landed before the unit's BUILDING state did, and these are
// what closes that.

void testAWorkerSentToASiteWalksThereAndFinishesIt() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);
    Unit* worker = site.SpawnUnit(Ids::kWorker, 2000.0f);

    worker->CommandBuild(barracks);
    CHECK_MSG(worker->CurrentState() == Unit::State::Building,
              "an ordered worker enters BUILDING");
    CHECK_MSG(worker->BuildTarget() == barracks, "and remembers which site");

    // 600px at 140/sec is 4.3 seconds of walking, then ten of building.
    for (int i = 0; i < 300; ++i) worker->Step(0.1);

    CHECK_MSG(barracks->IsComplete(), "the worker must finish it");
    CHECK_EQ(static_cast<int>(site.completed.size()), 1);
    CHECK_MSG(worker->CurrentState() == Unit::State::Idle,
              "and go back to idle when there is nothing left to pour into");
    CHECK_MSG(worker->BuildTarget() == nullptr, "letting the site go");
}

void testAnIdleWorkerFindsAnAbandonedSiteWithoutBeingTold() {
    // The anti-deadlock rule, and the reason the scan has no range. A builder
    // that flees, dies or is re-tasked leaves a half-built barracks; without
    // this it stands there for the rest of the run with nothing on screen to
    // explain why the player's army never arrives.
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);
    barracks->AddBuildProgress(4.0);   // somebody got a third of the way

    Unit* other = site.SpawnUnit(Ids::kWorker, 2000.0f);
    CHECK_MSG(other->CurrentState() == Unit::State::Idle, "this one was never told anything");

    for (int i = 0; i < 300; ++i) other->Step(0.1);

    CHECK_MSG(barracks->IsComplete(), "any idle worker resumes an abandoned site");
}

void testAWorkerBuildsBeforeItGathersAndBanksBeforeEither() {
    // The priority order, which is the deadlock fix. A worker that preferred a
    // tree would leave the site standing until every node ran dry - and since
    // every idle worker uses the same rule, the whole village would walk past
    // it together.
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);
    Unit* worker = site.SpawnUnit(Ids::kWorker, 2600.0f);

    // A tree is right there too, and it must lose.
    site.town.node.resource = Ids::kWood;
    site.town.node.amount = 100;
    site.town.node.maxAmount = 100;
    site.town.node.position = glm::vec2(2600.0f, kGroundY);
    site.town.hasNode = true;

    worker->Step(0.2);   // one thinking tick
    CHECK_MSG(worker->CurrentState() == Unit::State::Building,
              "the site comes first");
    CHECK_EQ(site.town.node.amount, 100);

    // Finish it, and now the tree wins.
    barracks->AddBuildProgress(20.0);
    for (int i = 0; i < 5; ++i) worker->Step(0.2);
    CHECK_MSG(worker->CurrentState() == Unit::State::Gathering,
              "with nothing left to build, it gathers");
}

void testAWorkerBuildsFromItsGatherRangeNotItsAttackRange() {
    // reach = the building's half-width plus the worker's GATHER range. Using
    // the attack range instead would have it stop 6px short of a barracks it
    // could otherwise reach - close enough to look right and never finish.
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);
    Unit* worker = site.SpawnUnit(Ids::kWorker, 2000.0f);
    worker->CommandBuild(barracks);

    for (int i = 0; i < 100; ++i) worker->Step(0.1);

    // It comes from the LEFT, so it stops 101px short of the centre - 55 of
    // body plus 46 of gather range - at 2499 rather than walking to 2600.
    // Using the attack range of 40 instead would send it 6px further in, which
    // looks identical and is a different number in the data for a reason.
    CHECK_MSG(worker->Position().x >= 2499.0f, "it walks in to gather range");
    CHECK_MSG(worker->Position().x <= 2515.0f, "and no further than it needs to");
    CHECK_MSG(barracks->BuildProgress() > 0.0, "and it is pouring");
}

void testABuildingDestroyedUnderItsBuilderReleasesTheWorker() {
    Site site;
    Building* barracks = site.Place(Ids::kBarracks, 2100.0f, false);
    Unit* worker = site.SpawnUnit(Ids::kWorker, 2000.0f);
    worker->CommandBuild(barracks);

    for (int i = 0; i < 20; ++i) worker->Step(0.1);
    CHECK_MSG(barracks->BuildProgress() > 0.0, "it started");

    barracks->Destroy();
    for (int i = 0; i < 20; ++i) worker->Step(0.1);

    // A dead site is not complete either, so the tick has to notice it is gone
    // rather than only that it is finished - a worker pouring time into rubble
    // is a worker doing nothing forever.
    CHECK_MSG(worker->CurrentState() != Unit::State::Building,
              "a worker must not keep building a ruin");
}

void testBuildAssistFollowsTheEconomysFlag() {
    // verify_buildings 6 at the game's 50741d1:
    //
    //   ok  : auto_assist_build defaults ON (hero-placed sites build themselves)
    //   ok  : idle worker auto-joins the new site
    //   ok  : the volunteer completes it (no stuck construction)
    //   ok  : flag off -> idle worker ignores the site
    //   ok  : command_build still assigns explicitly
    CHECK_MSG(wb::Shipped().Economy()["auto_assist_build"].AsBool(false),
              "auto_assist_build defaults ON (hero-placed sites build themselves)");
    {
        Site site;
        Building* barracks = site.Place(Ids::kBarracks, 2000.0f, false);
        Unit* worker = site.SpawnUnit(Ids::kWorker, 2400.0f);

        worker->Step(0.2);   // one thinking tick: the idle decision
        CHECK_MSG(worker->CurrentState() == Unit::State::Building &&
                      worker->BuildTarget() == barracks,
                  "idle worker auto-joins the new site");
        for (int i = 0; i < 160; ++i) worker->Step(0.2);
        CHECK_MSG(barracks->IsComplete(), "the volunteer completes it (no stuck construction)");
    }

    // Flag OFF. The original flips its loaded dictionary in place. The port's
    // shipped data is shared and const, so this case reads a scratch copy.
    std::ifstream in(std::string(WOLFBRIGADE_DATA_DIR) + "/economy.json", std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string economy = buffer.str();
    const std::string on = "\"auto_assist_build\": true";
    const size_t at = economy.find(on);
    CHECK_MSG(at != std::string::npos, "the shipped economy states the flag");
    if (at == std::string::npos) return;
    economy.replace(at, on.size(), "\"auto_assist_build\": false");

    wb::ScratchData scratch("assist", "economy.json", economy);
    GameData manual;
    CHECK_MSG(manual.LoadAll(scratch.Path()), "the scratch data loads");
    CHECK_MSG(!manual.Economy()["auto_assist_build"].AsBool(true), "with the flag off");

    Site site(manual);
    Building* barracks = site.Place(Ids::kBarracks, 2600.0f, false);
    Unit* worker = site.SpawnUnit(Ids::kWorker, 2900.0f);

    worker->Step(0.2);
    CHECK_MSG(worker->CurrentState() != Unit::State::Building,
              "flag off -> idle worker ignores the site");
    worker->CommandBuild(barracks);
    CHECK_MSG(worker->CurrentState() == Unit::State::Building &&
                  worker->BuildTarget() == barracks,
              "command_build still assigns explicitly");
}

} // namespace

static void runTests() {
    testAPlacedBuildingIsUnderConstructionUntilAWorkerFinishesIt();
    testProgressIsClampedAndCompletionFiresOnce();
    testAPrePlacedTownHallStartsFinishedAndBanking();
    testAHalfBuiltDepositIsNotADeposit();

    testABarracksTrainsOneUnitPerTrainTime();
    testAQueueOfTwoTakesTwoFullTrainTimes();
    testABuildingRefusesToTrainWhatItDoesNotTrain();
    testATrainedUnitAppearsBesideTheBuildingRatherThanInsideIt();

    testTheFootprintStandsOnTheGroundRatherThanStraddlingIt();
    testOverlappingFootprintsAreRefusedAndClearGroundIsNot();

    testATowerFiresTheBoltsTheOriginalCounted();
    testABuildingWithNoWeaponNeverFires();
    testATowerWithNothingInRangeHoldsItsFire();
    testAnUnfinishedTowerIsHarmless();

    testABuildingTakesDamageAndAnnouncesItBeforeItFalls();
    testADestroyedTownHallStopsBeingSomewhereToBank();
    testTheBoardFreezesForBuildingsToo();

    testReinforcingADamagedWallHealsItAsWellAsTougheningIt();
    testAnUpgradeTargetingAFieldNobodyHasIsReportedRatherThanSwallowed();

    testABuildingWithNoBuildTimeIsFinishedEvenIfNobodyPlacedIt();
    testTrainingStartsTheNextUnitFromZeroRatherThanFromTheOvershoot();
    testATowersRateIsCappedByItsThinkingTickNotOnlyByItsCooldown();
    testABuildingWithRangeButNoDamageStillNeverFires();

    testAWorkerSentToASiteWalksThereAndFinishesIt();
    testAnIdleWorkerFindsAnAbandonedSiteWithoutBeingTold();
    testAWorkerBuildsBeforeItGathersAndBanksBeforeEither();
    testAWorkerBuildsFromItsGatherRangeNotItsAttackRange();
    testABuildingDestroyedUnderItsBuilderReleasesTheWorker();
    testBuildAssistFollowsTheEconomysFlag();
}

TEST_MAIN("test_wb_buildings", 55)
