// The ported combat, against numbers the original COMPUTED.
//
// `verify_combat` runs six fights and prints where they ended up. Two of the
// lines are the kind that cannot be read off a data file: a soldier that kills
// a raider ends on 36 of 60 hit points, and a raider left alone with the Town
// Hall for twelve seconds takes it to 934 of 1000. Both fall out of two attack
// rates, two damage values, two move speeds and two reaches interacting over
// twenty seconds of simulation.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   "D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe" \
//       --headless --path . res://tools/verify_combat.tscn
//
// It printed, on 26 August 2026:
//
//   ok  : nearest enemy within range is the closest
//   ok  : no enemy within a short range
//   ok  : dead enemy is skipped, next nearest returned
//   ok  : raider dies to the soldier
//   ok  : soldier survives
//   ok  : soldier took damage in the fight (hp 36/60)
//   ok  : attacked worker enters FLEEING
//   ok  : ordered worker fights instead of fleeing
//   ok  : raider damages the Town Hall (hp 934/1000)
//   ok  : raider is attacking the building
//   ok  : soldier auto-acquired the raider
//   ok  : move order pulls the soldier off a live target
//   ok  : re-tasked worker's flee reflex is restored
//   ok  : archer auto-acquired the raider (ATTACKING)
//   ok  : archer fired pooled projectiles (pool 3)
//
// The Town Hall here is a STUB rather than the ported Building, which does not
// exist yet. That is honest: `building.gd`'s take_damage is a plain subtraction
// with no armour, no resistance and no shield, so a stub that subtracts is the
// same thing - and the number the raider drives it to is the property under
// test either way. When Building lands, this fixture is what it has to match.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Damageable.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/Lane.hpp"
#include "sim/Projectiles.hpp"
#include "sim/Unit.hpp"
#include "sim/World.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

constexpr float kGroundY = 800.0f;

// Stands in for a Building until Building is ported. Plain hit points and a
// width, which is all `building.gd` brings to a fight.
class StubBuilding final : public Damageable {
public:
    StubBuilding(const std::string& owner, float x, int hitPoints, float width)
        : m_faction(owner), m_position(x, kGroundY), m_hp(hitPoints), m_width(width) {}

    void TakeDamage(int amount) override {
        if (m_hp <= 0) return;
        m_hp = std::max(0, m_hp - amount);
    }
    bool IsAlive() const override { return m_hp > 0; }
    float HitHalfWidth() const override { return m_width * 0.5f; }
    glm::vec2 Position() const override { return m_position; }

    const std::string& Faction() const { return m_faction; }
    int Hp() const { return m_hp; }

private:
    std::string m_faction;
    glm::vec2 m_position;
    int m_hp;
    float m_width;
};

// A target that records every blow, alive or not.
//
// Unit and StubBuilding both refuse damage once they are dead, which is right -
// and it means neither of them can tell you whether the ARROW checked. This one
// does not refuse anything, so "an arrow that arrives at a corpse deals no
// damage" becomes a claim about the arrow rather than about what it hit.
class CountingTarget final : public Damageable {
public:
    explicit CountingTarget(float x) : m_position(x, kGroundY) {}

    void TakeDamage(int amount) override {
        ++hits;
        total += amount;
    }
    bool IsAlive() const override { return alive; }
    float HitHalfWidth() const override { return 0.0f; }
    glm::vec2 Position() const override { return m_position; }

    bool alive{true};
    int hits{0};
    int total{0};

private:
    glm::vec2 m_position;
};

// A battlefield: a lane index, some buildings, and an arrow pool.
class Battlefield final : public World {
public:
    Lane lane;
    ProjectilePool arrows;
    std::vector<std::unique_ptr<StubBuilding>> buildings;

    // Nothing to gather here. The economy has its own suite; this one is about
    // what happens when two factions are in range of each other.
    ResourceNode* NearestHarvestable(float) const override { return nullptr; }
    int NearestDeposit(float x) const override {
        int best = -1;
        float bestDistance = 0.0f;
        for (size_t i = 0; i < deposits.size(); ++i) {
            const float distance = std::fabs(x - deposits[i]);
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    }
    bool DepositExists(int index) const override {
        return index >= 0 && index < static_cast<int>(deposits.size());
    }
    glm::vec2 DepositPosition(int index) const override {
        return DepositExists(index) ? glm::vec2(deposits[static_cast<size_t>(index)], kGroundY)
                                    : glm::vec2(0.0f);
    }

    Building* NearestUnfinishedBuilding(const std::string&, float) const override {
        return nullptr;
    }

    Unit* NearestEnemyUnit(const std::string& faction, float x, float maxRange) const override {
        return lane.NearestEnemy(faction, x, maxRange);
    }

    Damageable* NearestEnemyBuilding(const std::string& faction, float x) const override {
        const std::string enemy =
            (faction == Factions::kPlayer) ? Factions::kEnemy : Factions::kPlayer;
        StubBuilding* best = nullptr;
        float bestDistance = 0.0f;
        for (const auto& building : buildings) {
            if (building->Faction() != enemy || !building->IsAlive()) continue;
            const float distance = std::fabs(building->Position().x - x);
            if (best == nullptr || distance < bestDistance) {
                best = building.get();
                bestDistance = distance;
            }
        }
        return best;
    }

    ProjectilePool* Projectiles() override { return &arrows; }

    std::vector<float> deposits;

    StubBuilding* AddBuilding(const std::string& owner, float x, int hp, float width) {
        buildings.push_back(std::make_unique<StubBuilding>(owner, x, hp, width));
        return buildings.back().get();
    }
};

// A fight, with everything it needs to run.
struct Fight {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    Battlefield field;
    std::vector<std::unique_ptr<Unit>> units;

    Fight() { state.Reset(); }

    Unit* Spawn(const std::string& id, float x) {
        auto unit = std::make_unique<Unit>(
            UnitStats::FromJson(id, wb::Shipped().Unit(id)), state, bus, field);
        unit->SetPosition(glm::vec2(x, kGroundY));
        Unit* raw = unit.get();
        units.push_back(std::move(unit));

        // Registered here, the way the original registers in _ready. A unit
        // that is not in the index is invisible to everything that hunts.
        field.lane.Register(raw);
        return raw;
    }

    void Step(double delta) {
        for (auto& unit : units) unit->Step(delta);
        field.arrows.Step(delta, state.IsPlaying());
    }
};

// --- 1. The lane index ---------------------------------------------------

void testTheLaneFindsTheNearestLivingEnemyInRange() {
    Fight fight;
    Unit* soldier = fight.Spawn(Ids::kSoldier, 2000.0f);
    Unit* near = fight.Spawn(Ids::kRaider, 2100.0f);
    Unit* far = fight.Spawn(Ids::kRaider, 2400.0f);
    (void)soldier;

    CHECK(fight.field.lane.NearestEnemy(Factions::kPlayer, 2000.0f, 500.0f) == near);

    // Out of range is nothing, not "the closest anyway". A soldier that
    // acquired across the map would abandon its post the moment a wave spawned
    // at the far edge.
    CHECK(fight.field.lane.NearestEnemy(Factions::kPlayer, 2000.0f, 50.0f) == nullptr);

    // A corpse is skipped and the next one is returned. The original checks
    // is_alive here because a unit lingers for its death fade, and a soldier
    // that kept hitting a fading body would stand there while the wave walked
    // past it.
    near->Kill();
    CHECK(fight.field.lane.NearestEnemy(Factions::kPlayer, 2000.0f, 500.0f) == far);
}

void testTheLaneIsSymmetricBetweenFactions() {
    Fight fight;
    Unit* soldier = fight.Spawn(Ids::kSoldier, 2000.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 2100.0f);

    CHECK(fight.field.lane.NearestEnemy(Factions::kEnemy, 2100.0f, 500.0f) == soldier);
    CHECK(fight.field.lane.NearestEnemy(Factions::kPlayer, 2000.0f, 500.0f) == raider);

    // And a unit never finds itself, or anything on its own side.
    CHECK_EQ(fight.field.lane.CountOf(Factions::kPlayer), 1);
    CHECK_EQ(fight.field.lane.CountOf(Factions::kEnemy), 1);
}

// --- 2. Soldier versus raider, to the original's own hit points ----------

void testASoldierKillsARaiderAndKeepsTheHealthTheOriginalSaidItWould() {
    Fight fight;
    Unit* soldier = fight.Spawn(Ids::kSoldier, 2000.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 2160.0f);

    for (int i = 0; i < 200; ++i) fight.Step(0.1);

    CHECK_MSG(!raider->IsAlive(), "the raider must die to the soldier");
    CHECK_MSG(soldier->IsAlive(), "and the soldier must survive it");

    // 36 of 60. A soldier hits for 8 at 1/sec against 40 hit points, and takes
    // 6 a second back while it does - the number is what those two rates come
    // to once the closing walk and the attack cooldowns are accounted for.
    CHECK_EQ(soldier->Hp(), 36);
    CHECK_EQ(soldier->Stats().maxHp, 60);
}

void testAWinnerStopsSwingingOnceThereIsNothingToHit() {
    Fight fight;
    Unit* soldier = fight.Spawn(Ids::kSoldier, 2000.0f);
    fight.Spawn(Ids::kRaider, 2160.0f);

    for (int i = 0; i < 200; ++i) fight.Step(0.1);

    const int after = soldier->Hp();
    for (int i = 0; i < 100; ++i) fight.Step(0.1);

    CHECK_EQ(soldier->Hp(), after);
    CHECK_MSG(soldier->CurrentState() == Unit::State::Idle,
              "with nothing in aggro it goes back to idle rather than fighting a corpse");
    CHECK_MSG(soldier->AttackTarget() == nullptr, "and lets the dead target go");
}

// --- 3. Raider versus the Town Hall --------------------------------------

void testARaiderWalksIntoTheTownHallAndTakesItDownToTheOriginalsNumber() {
    Fight fight;
    StubBuilding* hall = fight.field.AddBuilding(Factions::kPlayer, 1500.0f, 1000, 120.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 1640.0f);

    for (int i = 0; i < 120; ++i) fight.Step(0.1);

    CHECK_EQ(hall->Hp(), 934);
    CHECK_MSG(raider->CurrentState() == Unit::State::Attacking,
              "and it is still attacking when the clock runs out");
}

void testARaiderWithNothingLeftToFightWalksLeft() {
    // Every enemy building has fallen. The original sends the raider at the
    // left edge rather than letting it stand still, so a won field still
    // drains rather than filling up with idle enemies.
    Fight fight;
    Unit* raider = fight.Spawn(Ids::kRaider, 3000.0f);

    for (int i = 0; i < 40; ++i) fight.Step(0.1);

    CHECK_MSG(raider->CurrentState() == Unit::State::Moving, "it must keep advancing");
    CHECK_MSG(raider->Position().x < 3000.0f, "leftward, toward where a Town Hall would be");
}

void testARaiderPrefersWhateverIsActuallyNearer() {
    // A unit inside aggro beats a building further away, and the building wins
    // when it is the closer of the two. Getting this backwards makes a raider
    // walk past a soldier to punch a wall.
    {
        Fight fight;
        fight.field.AddBuilding(Factions::kPlayer, 1000.0f, 1000, 120.0f);
        Unit* raider = fight.Spawn(Ids::kRaider, 2000.0f);
        Unit* soldier = fight.Spawn(Ids::kSoldier, 2100.0f);

        fight.Step(0.2);
        CHECK_MSG(raider->AttackTarget() == soldier,
                  "the soldier at 100px beats the hall at 1000px");
    }
    {
        Fight fight;
        StubBuilding* hall = fight.field.AddBuilding(Factions::kPlayer, 1950.0f, 1000, 120.0f);
        Unit* raider = fight.Spawn(Ids::kRaider, 2000.0f);
        fight.Spawn(Ids::kSoldier, 2250.0f);

        fight.Step(0.2);
        CHECK_MSG(raider->AttackTarget() == hall, "and the hall wins when it is the closer one");
    }
}

// --- 4. Orders beat instincts --------------------------------------------

void testAMoveOrderPullsASoldierOffALiveTarget() {
    Fight fight;
    Unit* soldier = fight.Spawn(Ids::kSoldier, 2000.0f);
    fight.Spawn(Ids::kRaider, 2100.0f);

    fight.Step(0.2);
    CHECK(soldier->CurrentState() == Unit::State::Attacking);

    soldier->CommandMoveTo(glm::vec2(1200.0f, kGroundY));
    fight.Step(0.2);
    CHECK_MSG(soldier->CurrentState() == Unit::State::Moving,
              "a retreat order must actually retreat, with a live target in range");
}

void testAnOrderedWorkerFightsInsteadOfFleeing() {
    Fight fight;
    fight.field.deposits.push_back(1000.0f);
    Unit* worker = fight.Spawn(Ids::kWorker, 3000.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 3050.0f);

    worker->CommandAttack(raider);
    CHECK(worker->CurrentState() == Unit::State::Attacking);
    CHECK_MSG(worker->OrderedToAttack(), "the order is remembered");

    worker->TakeDamage(3);
    CHECK_MSG(worker->CurrentState() == Unit::State::Attacking,
              "a worker under orders does not run when hit");
}

void testReTaskingAWorkerRestoresItsFleeReflex() {
    // The exact sequence the original checks: order an attack, then order a
    // move, then hit it. The move has to clear the attack ORDER as well as the
    // target, or the worker keeps standing its ground for the rest of the run.
    Fight fight;
    fight.field.deposits.push_back(1000.0f);
    Unit* worker = fight.Spawn(Ids::kWorker, 3000.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 3050.0f);

    worker->CommandAttack(raider);
    worker->CommandMoveTo(glm::vec2(2800.0f, kGroundY));
    CHECK_MSG(!worker->OrderedToAttack(), "a re-task drops the attack order");

    worker->TakeDamage(3);
    CHECK_MSG(worker->CurrentState() == Unit::State::Fleeing, "so the flee reflex is back");
}

// --- 5. The archer, and the pool it shoots from --------------------------

void testAnArcherShootsFromRangeWithoutClosing() {
    Fight fight;
    Unit* archer = fight.Spawn(Ids::kArcher, 2000.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 2120.0f);   // inside its 220 range

    fight.Step(0.2);
    CHECK_MSG(archer->CurrentState() == Unit::State::Attacking, "it acquires at range");

    const float where = archer->Position().x;
    for (int i = 0; i < 30; ++i) fight.Step(0.1);

    // It stayed put. An archer that closed to melee would be a soldier with
    // forty hit points, which is a very short-lived soldier.
    CHECK_NEAR(archer->Position().x, where);
    CHECK_MSG(fight.field.arrows.PoolSize() > 0, "and it fired");

    // The raider walked into it and took arrows on the way.
    CHECK_MSG(raider->Hp() < raider->Stats().maxHp, "the arrows landed");
}

void testThePoolIsReusedRatherThanGrownPerShot() {
    // The point of a pool. Thirty seconds of shooting at 0.8/sec is two dozen
    // arrows; the pool should settle at the most that were ever in the air at
    // once, which is a handful.
    Fight fight;
    fight.Spawn(Ids::kArcher, 2000.0f);
    fight.Spawn(Ids::kRaider, 2120.0f);

    for (int i = 0; i < 300; ++i) fight.Step(0.1);

    CHECK_MSG(fight.field.arrows.PoolSize() > 0, "something was fired");
    CHECK_MSG(fight.field.arrows.PoolSize() <= 6,
              "and the pool did not grow one entry per shot");
}

void testAnArrowWhoseTargetDiesMidFlightFizzles() {
    // It coasts to the last place it saw the target and deals nothing. An
    // arrow that snapped to a corpse - or that dealt its damage anyway - would
    // let a volley kill a raider twice, and the second kill would count.
    Fight fight;
    Unit* raider = fight.Spawn(Ids::kRaider, 2500.0f);

    fight.field.arrows.Spawn(glm::vec2(2000.0f, kGroundY), raider, 6, 700.0f);
    CHECK_EQ(fight.field.arrows.ActiveCount(), 1);

    raider->Kill();
    const int before = raider->Hp();

    for (int i = 0; i < 60; ++i) fight.Step(0.1);

    CHECK_EQ(fight.field.arrows.ActiveCount(), 0);
    CHECK_EQ(raider->Hp(), before);
}

void testArrowsFreezeWithTheRestOfTheBoard() {
    Fight fight;
    Unit* raider = fight.Spawn(Ids::kRaider, 2500.0f);
    fight.field.arrows.Spawn(glm::vec2(2000.0f, kGroundY), raider, 6, 700.0f);

    // Where it was when the run ended. Read BEFORE the loss, because the
    // assertion below is about movement rather than about damage.
    const Projectile* arrow = fight.field.arrows.At(0);
    CHECK_MSG(arrow != nullptr, "the arrow is in the pool");
    if (arrow == nullptr) return;
    const glm::vec2 launched = arrow->position;

    fight.state.Lose();
    for (int i = 0; i < 60; ++i) fight.Step(0.1);

    CHECK_MSG(fight.field.arrows.ActiveCount() == 1,
              "an arrow in flight when the run ends must not land");
    CHECK_EQ(raider->Hp(), raider->Stats().maxHp);

    // AND IT DID NOT MOVE. The two assertions above are both satisfied by an
    // implementation that keeps flying arrows and merely suppresses the damage
    // on arrival - which would leave a volley drifting across a frozen board
    // behind the game-over panel. The original asserts the position for exactly
    // this reason:
    //   verify_projectiles.gd -> "in-flight arrow frozen after game over"
    CHECK_NEAR(fight.field.arrows.At(0)->position.x, launched.x);
    CHECK_NEAR(fight.field.arrows.At(0)->position.y, launched.y);
}

void testThePoolSettlesAtTheMostArrowsEverInTheAirAtOnce() {
    // The bound next door - "did not grow one entry per shot" - is loose enough
    // that a pool which grew by one every OTHER shot would pass it. The
    // original pins the number instead: three concurrent shots make three
    // arrows, and three more once those have retired make no more.
    //
    //   ok  : pool grew to 3 for 3 concurrent shots
    //   ok  : 3 more shots REUSED the pool (size still 3, not 6)
    //   ok  : 3 active after reuse
    Fight fight;

    // Far enough that nothing arrives during the steps below, so what is being
    // measured is the pool rather than the flight.
    CountingTarget target(9000.0f);
    const glm::vec2 from(2000.0f, kGroundY);

    for (int i = 0; i < 3; ++i) fight.field.arrows.Spawn(from, &target, 1, 700.0f);
    CHECK_EQ(fight.field.arrows.PoolSize(), 3);
    CHECK_EQ(fight.field.arrows.ActiveCount(), 3);

    // Retire all three by killing what they were aimed at: a target that dies
    // in flight makes its arrow coast to where it last saw it and fizzle.
    target.alive = false;
    for (int i = 0; i < 200 && fight.field.arrows.ActiveCount() > 0; ++i) fight.Step(0.1);
    CHECK_EQ(fight.field.arrows.ActiveCount(), 0);
    CHECK_MSG(fight.field.arrows.PoolSize() == 3, "the pool never shrinks");

    // ALIVE AGAIN BEFORE THE SECOND VOLLEY, and the order matters: Launch aims
    // a shot at a DEAD target at the launch point itself, so a batch fired at a
    // corpse is already at its destination and retires on its first step -
    // which would make the reuse assertion below measure nothing at all.
    target.alive = true;
    for (int i = 0; i < 3; ++i) fight.field.arrows.Spawn(from, &target, 1, 700.0f);

    CHECK_MSG(fight.field.arrows.PoolSize() == 3, "three, not six");
    CHECK_EQ(fight.field.arrows.ActiveCount(), 3);
}

void testARaiderWalksToADistantTownHallRatherThanAttackingItFromAfar() {
    // The aggro seed. With nothing in range, the comparison starts at the
    // AGGRO RANGE, so a building only wins if it is inside aggro too - and a
    // Town Hall two thousand pixels away is something to walk to, not
    // something to attack. Seeding with infinity instead puts the raider in an
    // attacking stance from across the map, which is a state nothing else in
    // the game expects it to be in that far from anything.
    Fight fight;
    fight.field.AddBuilding(Factions::kPlayer, 1000.0f, 1000, 120.0f);
    Unit* raider = fight.Spawn(Ids::kRaider, 3000.0f);

    fight.Step(0.2);
    CHECK_MSG(raider->CurrentState() == Unit::State::Moving,
              "a hall well outside aggro is walked to, not attacked");
    CHECK_MSG(raider->AttackTarget() == nullptr, "and nothing is acquired at that distance");
}

void testAnArrowArrivingAtACorpseDealsNothing() {
    // Asserted on the ARROW rather than on what it hit. A unit refuses damage
    // once it is dead, so a version that swung anyway would look identical
    // through a unit - and would deal real damage to anything that did not
    // guard, which is every building the moment one forgets to.
    Fight fight;
    CountingTarget target(2500.0f);

    fight.field.arrows.Spawn(glm::vec2(2000.0f, kGroundY), &target, 6, 700.0f);
    target.alive = false;

    for (int i = 0; i < 60; ++i) fight.Step(0.1);

    CHECK_EQ(fight.field.arrows.ActiveCount(), 0);
    CHECK_EQ(target.hits, 0);
}

void testAnArrowThatLandsOnALiveTargetDoesDealItsDamage() {
    // The control for the case above. Without it, an arrow that never dealt
    // damage at all would pass just as well.
    Fight fight;
    CountingTarget target(2500.0f);

    fight.field.arrows.Spawn(glm::vec2(2000.0f, kGroundY), &target, 6, 700.0f);
    for (int i = 0; i < 60; ++i) fight.Step(0.1);

    CHECK_EQ(fight.field.arrows.ActiveCount(), 0);
    CHECK_EQ(target.hits, 1);
    CHECK_EQ(target.total, 6);
}

void testFiringAtNothingIsHarmless() {
    // A pool asked for an arrow at a null target. It fizzles where it started
    // rather than dereferencing it - which is the same check that stops it
    // hitting a corpse, doing the other half of its job.
    Fight fight;
    fight.field.arrows.Spawn(glm::vec2(2000.0f, kGroundY), nullptr, 6, 700.0f);
    for (int i = 0; i < 10; ++i) fight.Step(0.1);
    CHECK_EQ(fight.field.arrows.ActiveCount(), 0);
}

} // namespace

static void runTests() {
    testTheLaneFindsTheNearestLivingEnemyInRange();
    testTheLaneIsSymmetricBetweenFactions();

    testASoldierKillsARaiderAndKeepsTheHealthTheOriginalSaidItWould();
    testAWinnerStopsSwingingOnceThereIsNothingToHit();

    testARaiderWalksIntoTheTownHallAndTakesItDownToTheOriginalsNumber();
    testARaiderWithNothingLeftToFightWalksLeft();
    testARaiderPrefersWhateverIsActuallyNearer();
    testARaiderWalksToADistantTownHallRatherThanAttackingItFromAfar();

    testAMoveOrderPullsASoldierOffALiveTarget();
    testAnOrderedWorkerFightsInsteadOfFleeing();
    testReTaskingAWorkerRestoresItsFleeReflex();

    testAnArcherShootsFromRangeWithoutClosing();
    testThePoolIsReusedRatherThanGrownPerShot();
    testAnArrowWhoseTargetDiesMidFlightFizzles();
    testAnArrowArrivingAtACorpseDealsNothing();
    testAnArrowThatLandsOnALiveTargetDoesDealItsDamage();
    testFiringAtNothingIsHarmless();
    testArrowsFreezeWithTheRestOfTheBoard();
    testThePoolSettlesAtTheMostArrowsEverInTheAirAtOnce();
}

TEST_MAIN("test_wb_combat", 35)
