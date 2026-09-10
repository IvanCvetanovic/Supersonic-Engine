// The ported worker, against numbers the original COMPUTED.
//
// `verify_economy` sections 2 and 2b run one worker for 140 steps of 0.2s -
// about 28 seconds - between a tree at x=1300 and a deposit at x=1000, and
// print exactly where the wood ended up. Those numbers are the output of a
// gather rate, a carry capacity, two ranges and a move speed interacting over
// 28 simulated seconds. There is no reading them off the data file.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_economy.tscn
//
// It printed, on 26 August 2026:
//
//   ok  : worker extracted from the tree (tree 79/100)
//   ok  : worker banked wood at the deposit (+20)
//   ok  : wood conserved: extracted 21 == banked 20 + carried 1
//   ok  : banked in full 10-loads (+20)
//   ok  : worker harvested the food node (79/100)
//   ok  : food banked at the deposit (+20)
//   ok  : food conserved (extracted == banked + carried)
//
// And from `verify_units`, the FSM half. Since the game's 2d38d9e a move
// order's y is clamped onto the walkable band rather than dropped, so an order
// aimed at the sky lands on world.json's ground_y:
//
//   ok  : spawns at full hp (30)      ok  : arrives at target x (got 1500.0)
//   ok  : spawns IDLE                 ok  : y clamped onto the band (got 590.0)
//   ok  : MOVING after move order     ok  : returns to IDLE on arrival
//
// And from `verify_combat`, the flee reflex:
//
//   ok  : attacked worker enters FLEEING
//   ok  : worker flees toward the Town Hall (leftward)
//   ok  : ordered worker fights instead of fleeing
//
// Re-run on 10 September 2026 at the game's 50741d1, the band and the parking
// cases, from verify_units and verify_economy:
//
//   ok  : a sky-clicked move clamps y onto the band
//   ok  : arrives on the band's top row
//   ok  : ...and x reached the ordered point
//   ok  : an in-band move CHANGES rows (2-D)
//   ok  : worker reached the tree in 2D (closest 32 <= range 46)
//   ok  : worker reached the deposit in 2D (closest 106 <= range 110)
//   ok  : cross-row gather still banks wood (+20)
//   ok  : parked worker settles to IDLE (state 0)
//   ok  : parked worker does NOT auto-gather (tree still 100)
//   ok  : parked worker holds where it was sent
//   ok  : a gather order resumes work (tree 90/100)

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/Projectiles.hpp"
#include "sim/Unit.hpp"
#include "sim/World.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace WolfBrigade;

namespace {

// The harness's numbers.
constexpr float kGroundY = 800.0f;
constexpr float kDepositX = 1000.0f;
constexpr float kNodeX = 1300.0f;
constexpr float kWorkerX = 1100.0f;
// The walkable band's top row: world.json's ground_y. The band runs lane.depth
// (280) below it. The worker fixtures above all stand on 800, inside it, and
// on ONE row - so their 2D distances are their x distances, which is why the
// original's 79/20/1 survive 2D pathing unchanged.
constexpr float kBandTop = 590.0f;
// The harness's dt, as a DOUBLE. `0.2f` is 0.20000000298023224 once widened,
// which is not the number the GDScript steps by - and over 140 steps that is
// the difference between reproducing the original and nearly reproducing it.
constexpr double kStep = 0.2;
constexpr int kSteps = 140;

// A world made of a list of nodes and a list of deposits, which is what the
// scene-tree groups amount to once the tree is gone.
class TestWorld final : public World {
public:
    std::vector<std::unique_ptr<ResourceNode>> nodes;
    std::vector<glm::vec2> deposits;

    // Which deposits still stand. Separate from the list so a Town Hall can
    // fall without the indices of everything after it shifting - which is the
    // whole reason a worker holds an index rather than a position.
    std::vector<bool> alive;

    ResourceNode* Add(const std::string& resource, float x, int amount, float y = kGroundY) {
        auto node = std::make_unique<ResourceNode>();
        node->resource = resource;
        node->maxAmount = amount;
        node->amount = amount;
        node->position = glm::vec2(x, y);
        nodes.push_back(std::move(node));
        return nodes.back().get();
    }

    void AddDeposit(float x, float y = kGroundY) {
        deposits.emplace_back(x, y);
        alive.push_back(true);
    }

    ResourceNode* NearestHarvestable(float x) const override {
        ResourceNode* best = nullptr;
        float bestDistance = 0.0f;
        for (const auto& node : nodes) {
            if (node->IsEmpty()) continue;
            const float distance = std::fabs(x - node->position.x);
            if (best == nullptr || distance < bestDistance) {
                best = node.get();
                bestDistance = distance;
            }
        }
        return best;
    }

    int NearestDeposit(float x) const override {
        int best = -1;
        float bestDistance = 0.0f;
        for (size_t i = 0; i < deposits.size(); ++i) {
            if (!alive[i]) continue;
            const float distance = std::fabs(x - deposits[i].x);
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    }

    bool DepositExists(int index) const override {
        return index >= 0 && index < static_cast<int>(deposits.size()) &&
               alive[static_cast<size_t>(index)];
    }

    glm::vec2 DepositPosition(int index) const override {
        return DepositExists(index) ? deposits[static_cast<size_t>(index)] : glm::vec2(0.0f);
    }

    // Nothing to fight here. A world with no enemies and no arrows is a
    // legitimate one - it is what a peaceful minute of gathering looks like -
    // and the combat suite next door builds the other kind.
    Building* NearestUnfinishedBuilding(const std::string&, float) const override {
        return nullptr;
    }
    Unit* NearestEnemyUnit(const std::string&, float, float) const override { return nullptr; }
    Damageable* NearestEnemyBuilding(const std::string&, float) const override { return nullptr; }
    ProjectilePool* Projectiles() override { return nullptr; }

    // Nothing here counts toward supply: the units are owned by the cases.
    std::vector<Building*> PlayerBuildings() const override { return {}; }
    std::vector<Unit*> PlayerUnits() const override { return {}; }
    Unit* NearestWoundedAlly(const Unit*, float) const override { return nullptr; }
    Unit* Hero() const override { return nullptr; }
    std::vector<Unit*> EnemiesWithin(const std::string&, float, float) const override {
        return {};
    }
};

// A run with one worker in it.
struct Site {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    TestWorld world;

    Site() { state.Reset(); }

    UnitStats WorkerStats() const {
        return UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker));
    }

    std::unique_ptr<Unit> Worker(float x = kWorkerX, float y = kGroundY) {
        auto unit = std::make_unique<Unit>(WorkerStats(), state, bus, world);
        unit->SetPosition(glm::vec2(x, y));
        return unit;
    }
};

// --- 1. The gather/deliver loop, against the original's own numbers -------

void testTheWorkerLoopBanksExactlyWhatTheOriginalBanked() {
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* tree = site.world.Add(Ids::kWood, kNodeX, 100);

    auto worker = site.Worker();
    const int before = site.state.Amount(Ids::kWood);

    for (int i = 0; i < kSteps; ++i) worker->Step(kStep);

    const int banked = site.state.Amount(Ids::kWood) - before;
    const int extracted = 100 - tree->amount;

    // The original's exact figures. 28 simulated seconds is two full trips and
    // one unit into a third.
    CHECK_EQ(tree->amount, 79);
    CHECK_EQ(banked, 20);
    CHECK_EQ(worker->Carrying(), 1);

    // Conservation: everything that came out of the tree is either in the bank
    // or still in hand. The property the whole economy rests on, and the one a
    // clamp bug in Extract breaks silently.
    CHECK_EQ(extracted, banked + worker->Carrying());

    // And it banked in FULL loads. A worker that delivered partial loads would
    // still conserve wood while trebling the walking.
    CHECK_EQ(banked % worker->Stats().carryCapacity, 0);
}

void testTheSameLoopWorksForTheSecondResource() {
    // A different resource id through the same code, which is what "resources
    // are a generic map" has to survive. The original gives this its own
    // section for the same reason.
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* bush = site.world.Add(Ids::kFood, kNodeX, 100);

    auto worker = site.Worker();
    const int before = site.state.Amount(Ids::kFood);

    for (int i = 0; i < kSteps; ++i) worker->Step(kStep);

    const int banked = site.state.Amount(Ids::kFood) - before;
    CHECK_EQ(bush->amount, 79);
    CHECK_EQ(banked, 20);
    CHECK_EQ(100 - bush->amount, banked + worker->Carrying());

    // And nothing leaked into the other resource.
    CHECK_EQ(site.state.Amount(Ids::kWood), 300);
}

void testAWorkerFinishesItsLoadWhenTheTreeRunsOut() {
    // A tree with less in it than a carry-load. The worker has to bank what it
    // got rather than standing over a stump waiting to fill up - which is what
    // a version that only delivered on a full load would do, forever.
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* stump = site.world.Add(Ids::kWood, kNodeX, 4);

    auto worker = site.Worker();
    const int before = site.state.Amount(Ids::kWood);
    for (int i = 0; i < kSteps; ++i) worker->Step(kStep);

    CHECK_EQ(stump->amount, 0);
    CHECK_EQ(site.state.Amount(Ids::kWood) - before, 4);
    CHECK_EQ(worker->Carrying(), 0);
    CHECK(worker->CurrentState() == Unit::State::Idle);
}

void testTheGatherRemainderIsKeptRatherThanTruncated() {
    // gather_rate is a rate per second and wood is a whole number, so the
    // fraction has to accumulate. Truncating each step gathers nothing at all
    // when the step is shorter than one unit's worth - at 1.0/sec and a
    // sixtieth of a second, that is always.
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* tree = site.world.Add(Ids::kWood, kNodeX, 100);

    auto worker = site.Worker(kNodeX - 10.0f);   // already in range
    for (int i = 0; i < 120; ++i) worker->Step(1.0 / 60.0);   // 2 seconds

    CHECK_MSG(tree->amount < 100, "a worker stepping at 60Hz must still gather");
    CHECK_EQ(100 - tree->amount, worker->Carrying());
}

// --- 2. The FSM ----------------------------------------------------------

void testAUnitSpawnsIdleAtFullHealth() {
    Site site;
    auto worker = site.Worker();
    CHECK_EQ(worker->Hp(), 30);
    CHECK(worker->CurrentState() == Unit::State::Idle);
    CHECK_MSG(worker->IsAlive(), "a fresh unit is alive");
    CHECK_MSG(worker->IsPlayer(), "a worker belongs to the player");
}

void testAMoveOrderTakesItThereAndThenReleasesIt() {
    // verify_units' FSM case: from (1000, 800), an order aimed at the sky at
    // y=200, then 80 steps of 0.1s.
    Site site;   // no nodes, no deposits: nothing to distract it
    auto worker = site.Worker(1000.0f);

    worker->CommandMoveTo(glm::vec2(1500.0f, 200.0f));
    CHECK(worker->CurrentState() == Unit::State::Moving);

    for (int i = 0; i < 80; ++i) worker->Step(0.1);

    CHECK_NEAR(worker->Position().x, 1500.0f);

    // The order's y is TAKEN, and clamped onto the walkable band, so the sky
    // click lands on the band's top row. Dropping it instead - the port's rule
    // until the game's 2d38d9e - would leave the worker on 800, and a move that
    // refuses to change rows reads as broken once workers cross rows to reach
    // their trees.
    CHECK_NEAR(worker->Position().y, kBandTop);

    CHECK(worker->CurrentState() == Unit::State::Idle);
}

void testAMoveOrderChangesRowsButNeverLeavesTheBand() {
    // verify_units' _check_lane_stays_1d, the movement half: from (1000, 700),
    // a sky click at (1400, 120), then an order to a row inside the band.
    Site site;
    auto worker = site.Worker(1000.0f, 700.0f);

    worker->CommandMoveTo(glm::vec2(1400.0f, 120.0f));
    CHECK_MSG(std::fabs(worker->MoveTarget().y - kBandTop) < 0.001f,
              "a sky-clicked move clamps y onto the band");
    for (int i = 0; i < 40; ++i) worker->Step(0.2);
    CHECK_MSG(std::fabs(worker->Position().y - kBandTop) < 0.001f, "arrives on the band's top row");
    CHECK_MSG(std::fabs(worker->Position().x - 1400.0f) < 4.0f,
              "...and x reached the ordered point");

    worker->CommandMoveTo(glm::vec2(1000.0f, kBandTop + 150.0f));
    for (int i = 0; i < 40; ++i) worker->Step(0.2);
    CHECK_MSG(std::fabs(worker->Position().y - (kBandTop + 150.0f)) < 4.0f,
              "an in-band move CHANGES rows (2-D)");

    // And the band has a floor: lane.depth, 280 below the top. A click on the
    // dirt cross-section lands on the front row.
    worker->CommandMoveTo(glm::vec2(1000.0f, 5000.0f));
    CHECK_NEAR(worker->MoveTarget().y, kBandTop + 280.0f);
}

void testAWorkerWalksUpToTheRealTreeAndDepositAcrossRows() {
    // verify_economy 2d: the deposit on the back row, the tree on the front
    // row 250 px below it, the worker on a third row; 200 steps of 0.2s.
    //
    // Worker pathing is 2D, so the worker stands AT the tree and at the Town
    // Hall's door. With x-only pathing it would stop gather_range away in x but
    // a whole row off in y, and never come within range of either.
    Site site;
    const glm::vec2 deposit(1000.0f, 610.0f);
    site.world.AddDeposit(deposit.x, deposit.y);
    ResourceNode* tree = site.world.Add(Ids::kWood, 1300.0f, 100, 860.0f);
    auto worker = site.Worker(1100.0f, 700.0f);

    const int before = site.state.Amount(Ids::kWood);
    float closestTree = 1.0e9f;
    float closestDeposit = 1.0e9f;
    for (int i = 0; i < 200; ++i) {
        worker->Step(kStep);
        closestTree = std::min(closestTree, glm::distance(worker->Position(), tree->position));
        closestDeposit = std::min(closestDeposit, glm::distance(worker->Position(), deposit));
    }

    // The original prints each closest approach to the pixel.
    CHECK_EQ(static_cast<int>(std::lround(closestTree)), 32);
    CHECK_EQ(static_cast<int>(std::lround(closestDeposit)), 106);
    CHECK_MSG(closestTree <= worker->Stats().gatherRange + 1.0f, "it reached the tree in 2D");
    CHECK_MSG(closestDeposit <= worker->Stats().depositRange + 1.0f,
              "and the deposit in 2D");
    CHECK_EQ(site.state.Amount(Ids::kWood) - before, 20);
}

void testAMovedWorkerParksUntilItIsToldToGather() {
    // verify_economy 2e. A worker the player sends to open ground holds there,
    // with a full tree 250 px away. A gather order is what puts it back to
    // work, and this board has no deposit, so it fills one load and keeps it.
    Site site;
    ResourceNode* tree = site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker();

    worker->CommandMoveTo(glm::vec2(1550.0f, kGroundY));
    for (int i = 0; i < 100; ++i) worker->Step(kStep);

    CHECK_MSG(worker->CurrentState() == Unit::State::Idle, "parked worker settles to IDLE");
    CHECK_MSG(worker->Parked(), "and is parked");
    CHECK_EQ(tree->amount, 100);
    CHECK_MSG(std::fabs(worker->Position().x - 1550.0f) < 5.0f,
              "parked worker holds where it was sent");

    worker->CommandGather(tree);
    CHECK_MSG(!worker->Parked(), "a gather order takes it off park");
    CHECK(worker->CurrentState() == Unit::State::Gathering);
    for (int i = 0; i < 120; ++i) worker->Step(kStep);
    CHECK_EQ(tree->amount, 90);
}

void testEveryWorkOrderUnparksAndAMoveParks() {
    // The three ways off park, one each. A worker left parked by an order that
    // should have released it would stand beside the job it was just given.
    Site site;
    ResourceNode* tree = site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker();
    CHECK_MSG(!worker->Parked(), "a fresh worker is not parked");

    auto target = site.Worker(1600.0f);   // something to be told to hit

    worker->CommandMoveTo(glm::vec2(1550.0f, kGroundY));
    CHECK_MSG(worker->Parked(), "a move order parks it");
    worker->CommandAttack(target.get());
    CHECK_MSG(!worker->Parked(), "an attack order unparks it");

    worker->CommandMoveTo(glm::vec2(1550.0f, kGroundY));
    worker->CommandGather(nullptr);
    CHECK_MSG(worker->Parked(), "a gather order with no tree changes nothing");
    tree->amount = 0;
    worker->CommandGather(tree);
    CHECK_MSG(worker->Parked(), "and neither does one at an empty tree");
}

void testADeadUnitStopsAndIgnoresOrders() {
    Site site;
    site.world.AddDeposit(kDepositX);
    site.world.Add(Ids::kWood, kNodeX, 100);

    auto worker = site.Worker();
    worker->Kill();
    CHECK(worker->CurrentState() == Unit::State::Dead);
    CHECK_MSG(!worker->IsAlive(), "a killed unit is not alive");

    const glm::vec2 where = worker->Position();
    worker->CommandMoveTo(glm::vec2(5000.0f, kGroundY));
    for (int i = 0; i < kSteps; ++i) worker->Step(kStep);

    CHECK(worker->CurrentState() == Unit::State::Dead);
    CHECK_NEAR(worker->Position().x, where.x);
}

void testTheBoardFreezesOnceTheRunIsDecided() {
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* tree = site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker();

    site.state.Lose();
    for (int i = 0; i < kSteps; ++i) worker->Step(kStep);

    CHECK_EQ(tree->amount, 100);
    CHECK(worker->CurrentState() == Unit::State::Idle);
}

// --- 3. The flee reflex --------------------------------------------------

void testAnAttackedWorkerRunsForTheTownHall() {
    Site site;
    site.world.AddDeposit(kDepositX);            // to the LEFT of the worker
    site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker(1400.0f);

    worker->TakeDamage(5);
    CHECK(worker->CurrentState() == Unit::State::Fleeing);
    CHECK_EQ(worker->Hp(), 25);

    const float before = worker->Position().x;
    for (int i = 0; i < 5; ++i) worker->Step(kStep);
    CHECK_MSG(worker->Position().x < before, "it must run toward the deposit, which is leftward");
}

void testAFleeingWorkerGoesBackToWorkOnceItIsSafe() {
    Site site;
    site.world.AddDeposit(kDepositX);
    site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker(1400.0f);

    worker->TakeDamage(5);
    for (int i = 0; i < kSteps; ++i) worker->Step(kStep);

    // Reached the Town Hall, calmed down, and picked the tree back up. A
    // worker that cowered forever is a player watching their economy stop with
    // nothing on screen to explain it.
    CHECK_MSG(worker->CurrentState() != Unit::State::Fleeing, "it must stop fleeing eventually");
}

void testAWorkerWithNowhereToRunGoesBackToWorkInstead() {
    // No deposit at all: the Town Hall has fallen. The flee has no
    // destination, and standing still is not an answer.
    Site site;
    site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker(1400.0f);

    worker->TakeDamage(5);
    for (int i = 0; i < 10; ++i) worker->Step(kStep);
    CHECK_MSG(worker->CurrentState() != Unit::State::Fleeing,
              "with nowhere to flee to, it must resume rather than freeze");
}

void testAWoundedWorkerRegeneratesOnlyOnceOutOfCombat() {
    // verify_buildings 3d at the game's 50741d1:
    //
    //   ok  : no regen inside the out-of-combat delay
    //   ok  : hp regenerates once out of combat (20 -> 23)
    //   ok  : taking a hit pauses regen again
    //
    // The worker's 0.5 a second, after economy.json's four seconds. The
    // original drives `_process_regen(0.1)` alone; a whole step runs the same
    // regen, and nothing else here touches the hit points.
    Site site;
    auto worker = site.Worker();
    worker->TakeDamage(10);
    const int hurt = worker->Hp();
    CHECK_EQ(hurt, 20);

    for (int i = 0; i < 20; ++i) worker->Step(0.1);   // two seconds, inside the delay
    CHECK_MSG(worker->Hp() == hurt, "no regen inside the out-of-combat delay");

    for (int i = 0; i < 80; ++i) worker->Step(0.1);   // eight more
    CHECK_EQ(worker->Hp(), 23);

    worker->TakeDamage(1);
    const int again = worker->Hp();
    for (int i = 0; i < 10; ++i) worker->Step(0.1);
    CHECK_MSG(worker->Hp() == again, "taking a hit pauses regen again");
}

void testAKillingBlowStillAnnouncesItsDamage() {
    // Emitted before the death check, so the number that mattered still floats.
    Site site;
    auto worker = site.Worker();

    int announced = 0;
    int total = 0;
    site.bus.damageDealt.Connect([&](const glm::vec2&, int amount, const std::string&) {
        ++announced;
        total += amount;
    });

    worker->TakeDamage(999);
    CHECK_EQ(announced, 1);
    CHECK_EQ(total, 999);
    CHECK(worker->CurrentState() == Unit::State::Dead);
    CHECK_EQ(worker->Hp(), 0);

    // And a corpse takes no more damage.
    worker->TakeDamage(10);
    CHECK_EQ(announced, 1);
}

// --- 4. The two clocks ---------------------------------------------------

void testDecisionsRunOnTheTickAndMovementRunsEveryStep() {
    // The original's seventh rule. Movement interpolates every frame; scans and
    // decisions run at ~8 Hz. A port that thought every frame would work
    // perfectly and cost eight times as much - which is the kind of difference
    // that only shows up on the device it was written for.
    Site site;
    auto worker = site.Worker(1000.0f);
    worker->CommandMoveTo(glm::vec2(1500.0f, kGroundY));

    // One step far shorter than the tick interval. It has to MOVE.
    worker->Step(0.01);
    CHECK_MSG(worker->Position().x > 1000.0f, "movement does not wait for the thinking tick");

    // But it has not arrived, so it is still moving - and nothing decided
    // anything, because 0.01s is a twelfth of a tick.
    CHECK(worker->CurrentState() == Unit::State::Moving);
}

void testAStepLongerThanTheTickThinksOnceRatherThanCatchingUp() {
    // Deliberately NOT a while loop, in the original or here. A unit that ran
    // eight decisions after a hitch would act on seven views of a world that
    // had already moved on.
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* tree = site.world.Add(Ids::kWood, kNodeX, 100);
    auto worker = site.Worker(kNodeX);   // standing on the tree

    // One second: eight ticks' worth. It picks up work on the first and only
    // one of them, so it is gathering rather than having cycled through
    // gather-deliver-gather.
    worker->Step(1.0);
    CHECK(worker->CurrentState() == Unit::State::Gathering);
    CHECK_MSG(tree->amount == 100, "the decision happens after the step's work, not before it");
}

// --- 5. Four things the 0.2s harness cannot see --------------------------
//
// The original's economy harness steps at 0.2s, and at a gather rate of 1.0 a
// step never asks for more than one unit of wood. That hides three separate
// bugs: charging the accumulator for what was ASKED rather than what came out,
// gathering past the carry capacity, and thinking more than once after a long
// step. All three survived their mutations until these cases existed. They need
// a step long enough for the asked-for amount and the available amount to
// disagree.

void testTheRemainderSurvivesATreeRunningOutMidBite() {
    // A five-second step asks for five wood from a tree holding two. The
    // accumulator must be charged TWO - the three it did not get are still
    // owed, and carry into the next tree. Charging five loses them silently,
    // and a worker quietly gathers slower than the data says forever after.
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* nearlyEmpty = site.world.Add(Ids::kWood, kNodeX, 2);
    ResourceNode* plenty = site.world.Add(Ids::kWood, kNodeX, 100);

    auto worker = site.Worker(kNodeX);
    for (int i = 0; i < 6; ++i) worker->Step(5.0);

    // Two from the first tree, banked; then eight from the second in one bite -
    // five seconds of rate plus the three that were owed.
    CHECK_EQ(nearlyEmpty->amount, 0);
    CHECK_EQ(site.state.Amount(Ids::kWood), 300 + 2);
    CHECK_EQ(plenty->amount, 92);
    CHECK_EQ(worker->Carrying(), 8);
}

void testAWorkerNeverCarriesMoreThanItCanCarry() {
    // With eight in hand and five seconds of gathering owed, the ask has to be
    // clamped to the two that fit. Unclamped it takes five, carries thirteen,
    // and banks a load that is not a load - the "full 10-loads" property the
    // original asserts quietly stops being true.
    Site site;
    site.world.AddDeposit(kDepositX);
    site.world.Add(Ids::kWood, kNodeX, 2);
    site.world.Add(Ids::kWood, kNodeX, 100);

    auto worker = site.Worker(kNodeX);
    const int capacity = worker->Stats().carryCapacity;
    const int before = site.state.Amount(Ids::kWood);

    for (int i = 0; i < 14; ++i) {
        worker->Step(5.0);
        CHECK_MSG(worker->Carrying() <= capacity, "a worker cannot hold more than its capacity");
    }

    // And what it banked past the first partial tree is whole loads.
    const int banked = site.state.Amount(Ids::kWood) - before;
    CHECK_EQ((banked - 2) % capacity, 0);
}

void testAnEmptiedTreeSendsTheWorkerHomeOnTheSameStep() {
    // StepGather checks the tree as well as the load, so a worker leaves the
    // instant it runs dry rather than waiting up to a tick for the thinking
    // step to notice. At the harness's 0.2s dt the tick fires every step and
    // the two are indistinguishable; at sixty frames a second it is an eighth
    // of a second of standing over a stump, every tree, every worker.
    Site site;
    site.world.AddDeposit(kDepositX);
    ResourceNode* stump = site.world.Add(Ids::kWood, kNodeX, 3);

    auto worker = site.Worker(kNodeX);

    // One 60Hz step is a twelfth of a thinking interval, so it takes eight of
    // them before the worker decides anything at all. That IS the two-clock
    // design working, and the first version of this case assumed one step was
    // enough.
    for (int i = 0; i < 8 && worker->CurrentState() != Unit::State::Gathering; ++i) {
        worker->Step(1.0 / 60.0);
    }
    CHECK(worker->CurrentState() == Unit::State::Gathering);

    // Step at 60Hz until the tree is empty, and check the state on that step.
    bool leftImmediately = false;
    for (int i = 0; i < 600 && stump->amount > 0; ++i) {
        worker->Step(1.0 / 60.0);
        if (stump->amount == 0) leftImmediately = worker->CurrentState() == Unit::State::Delivering;
    }
    CHECK_EQ(stump->amount, 0);
    CHECK_MSG(leftImmediately, "an emptied tree must release the worker on the same step");
}

void testALongStepDoesNotChainDecisions() {
    // One tick per step, not one per interval elapsed. A worker that has just
    // reached safety needs two ticks to be back at work: the first calms it
    // (FLEEING to IDLE), the second finds it a tree. With catch-up, one long
    // step does both - acting twice on one view of the world.
    //
    // This case used to arrive by a move order. Since parking, an arrived
    // worker holds and never seeks work at all, so that path could no longer
    // tell one tick from forty. A flee ends in an UNPARKED idle, so it can.
    Site site;
    site.world.AddDeposit(kDepositX);
    site.world.Add(Ids::kWood, kNodeX, 100);

    auto worker = site.Worker(kDepositX);   // already at the Town Hall
    worker->TakeDamage(1);
    CHECK(worker->CurrentState() == Unit::State::Fleeing);

    // Forty thinking intervals' worth.
    worker->Step(5.0);
    CHECK_MSG(worker->CurrentState() == Unit::State::Idle,
              "reaching safety releases the flee and nothing else happens this step");

    // And the next step does find it work, so the idle above was one tick
    // short of it rather than a worker that had given up.
    worker->Step(0.2);
    CHECK(worker->CurrentState() == Unit::State::Gathering);
}

} // namespace

static void runTests() {
    testTheWorkerLoopBanksExactlyWhatTheOriginalBanked();
    testTheSameLoopWorksForTheSecondResource();
    testAWorkerFinishesItsLoadWhenTheTreeRunsOut();
    testTheGatherRemainderIsKeptRatherThanTruncated();

    testAUnitSpawnsIdleAtFullHealth();
    testAMoveOrderTakesItThereAndThenReleasesIt();
    testAMoveOrderChangesRowsButNeverLeavesTheBand();
    testAWorkerWalksUpToTheRealTreeAndDepositAcrossRows();
    testAMovedWorkerParksUntilItIsToldToGather();
    testEveryWorkOrderUnparksAndAMoveParks();
    testADeadUnitStopsAndIgnoresOrders();
    testTheBoardFreezesOnceTheRunIsDecided();

    testAnAttackedWorkerRunsForTheTownHall();
    testAFleeingWorkerGoesBackToWorkOnceItIsSafe();
    testAWorkerWithNowhereToRunGoesBackToWorkInstead();
    testAKillingBlowStillAnnouncesItsDamage();
    testAWoundedWorkerRegeneratesOnlyOnceOutOfCombat();

    testDecisionsRunOnTheTickAndMovementRunsEveryStep();
    testAStepLongerThanTheTickThinksOnceRatherThanCatchingUp();

    testTheRemainderSurvivesATreeRunningOutMidBite();
    testAWorkerNeverCarriesMoreThanItCanCarry();
    testAnEmptiedTreeSendsTheWorkerHomeOnTheSameStep();
    testALongStepDoesNotChainDecisions();
}

TEST_MAIN("test_wb_worker", 40)
