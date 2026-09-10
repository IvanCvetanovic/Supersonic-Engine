// Squads, priests and the shelter bell, against the original's harnesses.
//
// `verify_squads` pins the army layer of the hero-first restructure: which
// units follow, the two horns, the warband keeping station on the hero and
// breaking off at the leash, the garrison walking home, priests in either
// squad, the squad a recruit joins, and the worker shelter bell.
// `verify_casters` pins the priest's heal: the cast on the attack cooldown,
// the clamp, a healer refusing an attack order, and a temple training one.
//
// To re-derive, from the game's directory:
//
//   & .\tools\godot.bat --headless --path . res://tools/verify_squads.tscn
//   & .\tools\godot.bat --headless --path . res://tools/verify_casters.tscn
//
// At the game's 50741d1, on 10 September 2026, verify_squads printed thirty
// ok lines and no failures:
//
//   ok  : soldier can follow
//   ok  : archer can follow
//   ok  : priest can follow
//   ok  : workers never follow
//   ok  : the hero leads, never follows
//   ok  : rally_all puts every can_follow unit in the warband
//   ok  : counts see the whole band
//   ok  : send_home returns everyone to the garrison
//   ok  : warband: far from the hero -> marches
//   ok  : the march goal is the hero's side, not a post
//   ok  : the follower closed most of the gap
//   ok  : warband engages enemies even mid-march
//   ok  : past the leash the follower abandons the fight and returns to the hero
//   ok  : the leash holds across ticks (no ATTACKING/MOVING flip-flop)
//   ok  : garrison walks back to its home post
//   ok  : a garrison unit marching home engages ambushers
//   ok  : a wounded ally outranks following the hero
//   ok  : nothing to heal -> the warband priest tails the hero
//   ok  : re-latched the wounded ally
//   ok  : a warband priest breaks off a chase past the leash
//   ok  : garrison priest heals near its post
//   ok  : a garrison priest will not desert its post mid-chase
//   ok  : recruits default to the garrison
//   ok  : unit_trained carries the building's recruit squad
//   ok  : recruit_squad rides the building save
//   ok  : squad + home post ride the unit save
//   ok  : the bell sends a far worker running for a drop-off
//   ok  : the worker holes up at the drop-off and stands
//   ok  : sheltered workers never seek work
//   ok  : the bell state rides the run save
//
// and verify_casters thirty, of which C, D and E are here (A is
// test_wb_data's and B is test_wb_combat's):
//
//   ok  : tick: wounded ally in range -> HEALING
//   ok  : cast restores heal_amount
//   ok  : EventBus.healed carries the amount
//   ok  : cast spends the cooldown
//   ok  : cooldown gates the next cast
//   ok  : still channeling while ally is hurt
//   ok  : ally at full hp -> back to IDLE
//   ok  : receive_heal clamps at max_hp
//   ok  : healed event reports the amount ACTUALLY restored
//   ok  : healing a full unit is a silent no-op
//   ok  : a healer ignores attack orders
//   ok  : to_save writes ref_heal via the id map
//   ok  : relink resolves ref_heal back to the ally
//   ok  : a complete temple can train a priest
//   ok  : priest accepted into the temple queue
//   ok  : training completes -> unit_trained('priest')
//
// THE HARNESSES CALL _tick_ai() BY HAND, between moves they make themselves,
// and these cases do the same - which is why Unit::TickAi is public.
//
// WHAT IS NOT PINNED: a follower's exact slot. The original fans the warband
// out by get_instance_id(), which nothing can predict, and its harness asserts
// only that a follower marches to the hero's side and closes most of the gap.
// So do these.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Building.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/Lane.hpp"
#include "sim/Match.hpp"
#include "sim/Projectiles.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/SaveIds.hpp"
#include "sim/Squads.hpp"
#include "sim/Unit.hpp"
#include "sim/World.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace WolfBrigade;
using Supersonic::Json::Value;

namespace {

// A row inside the shipped band, which runs from 590 down 280 px. The
// harnesses stand everything on it.
constexpr float kRow = 640.0f;

constexpr const char* kHero = "hero";

// A camp: a lane, some buildings, an optional tree, and a hero to follow.
class Camp final : public World {
public:
    Lane lane;
    ProjectilePool arrows;
    std::vector<std::unique_ptr<Unit>> units;
    std::vector<std::unique_ptr<Building>> buildings;

    // The harnesses' `HeroControl.hero = hero`.
    Unit* hero{nullptr};

    // One optional tree, so a sheltered worker has work to refuse.
    mutable ResourceNode node;
    bool hasNode{false};

    ResourceNode* NearestHarvestable(float) const override {
        return hasNode && node.Harvestable() ? &node : nullptr;
    }

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

    Building* NearestUnfinishedBuilding(const std::string&, float) const override {
        return nullptr;
    }
    Unit* NearestEnemyUnit(const std::string& faction, float x, float maxRange) const override {
        return lane.NearestEnemy(faction, x, maxRange);
    }
    Damageable* NearestEnemyBuilding(const std::string&, float) const override { return nullptr; }
    Unit* NearestWoundedAlly(const Unit* me, float maxRange) const override {
        return lane.NearestWoundedAlly(me, maxRange);
    }
    ProjectilePool* Projectiles() override { return &arrows; }

    std::vector<Building*> PlayerBuildings() const override {
        std::vector<Building*> out;
        for (const auto& building : buildings) {
            if (building->Faction() == Factions::kPlayer) out.push_back(building.get());
        }
        return out;
    }
    std::vector<Unit*> PlayerUnits() const override {
        std::vector<Unit*> out;
        for (const auto& unit : units) {
            if (unit->IsPlayer()) out.push_back(unit.get());
        }
        return out;
    }

    Unit* Hero() const override { return hero; }
};

struct Muster {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    Camp camp;

    Muster() { state.Reset(); }

    Unit* Spawn(const std::string& id, float x, float y = kRow) {
        auto unit = std::make_unique<Unit>(UnitStats::FromJson(id, wb::Shipped().Unit(id)), state,
                                           bus, camp);
        unit->SetPosition(glm::vec2(x, y));
        unit->SetFormationKey(static_cast<int>(camp.units.size()));
        Unit* raw = unit.get();
        camp.units.push_back(std::move(unit));
        camp.lane.Register(raw);
        return raw;
    }

    // Complete, and with auto-train off: every recruit below is one the case
    // asked for.
    Building* Place(const std::string& id, float x) {
        auto building = std::make_unique<Building>(
            BuildingStats::FromJson(id, wb::Shipped().Building(id)), true, state, bus, camp);
        building->SetPosition(glm::vec2(x, kRow));
        building->SetTrainTimes(wb::Shipped().Units());
        for (const std::string& unitId : building->Stats().trains) {
            building->SetAutoTrain(unitId, false);
        }
        Building* raw = building.get();
        camp.buildings.push_back(std::move(building));
        return raw;
    }

    static double Economy(const char* key) {
        return wb::Shipped().Economy()[key].AsNumber(0.0);
    }
};

void recordHeals(Muster& m, std::vector<int>& into) {
    m.bus.healed.Connect([&into](const glm::vec2&, int amount) { into.push_back(amount); });
}

// --- A. Who follows, and the two horns --------------------------------------

void testFightersFollowWhileWorkersAndTheHeroNeverDo() {
    for (const char* id : {Ids::kSoldier, Ids::kArcher, Ids::kPriest}) {
        CHECK_MSG(UnitStats::FromJson(id, wb::Shipped().Unit(id)).canFollow, id);
    }
    CHECK_MSG(!UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker)).canFollow,
              "workers never follow");
    CHECK_MSG(!UnitStats::FromJson(kHero, wb::Shipped().Unit(kHero)).canFollow,
              "the hero leads, never follows");
}

void testTheHornsRelabelEveryLivingFollowerAndNothingElse() {
    Muster m;
    Unit* hero = m.Spawn(kHero, 3000.0f);
    Unit* follower = m.Spawn(Ids::kSoldier, 1500.0f);
    Unit* holder = m.Spawn(Ids::kSoldier, 1600.0f, 700.0f);
    m.camp.hero = hero;

    // Added by the port: what the horns must NOT touch - a worker, the hero
    // himself, and the dead.
    Unit* worker = m.Spawn(Ids::kWorker, 1700.0f);
    Unit* fallen = m.Spawn(Ids::kSoldier, 1800.0f);
    fallen->Kill();

    Squads::RallyAll(m.camp);
    CHECK(follower->Squad() == Squads::kWarband && holder->Squad() == Squads::kWarband);
    const Squads::Tally band = Squads::Counts(m.camp);
    CHECK_EQ(band.warband, 2);
    CHECK_EQ(band.garrison, 0);
    CHECK_MSG(worker->Squad() == Squads::kGarrison, "a worker is never rallied");
    CHECK_MSG(hero->Squad() == Squads::kGarrison, "the hero is not in the band he leads");
    CHECK_MSG(fallen->Squad() == Squads::kGarrison, "the dead are not rallied");

    Squads::SendHome(m.camp);
    CHECK(follower->Squad() == Squads::kGarrison && holder->Squad() == Squads::kGarrison);
    const Squads::Tally home = Squads::Counts(m.camp);
    CHECK_EQ(home.garrison, 2);
    CHECK_EQ(home.warband, 0);
}

// --- B. The warband and the garrison ----------------------------------------

void testAWarbandSoldierMarchesToTheHerosSideAndStandsThere() {
    Muster m;
    Unit* hero = m.Spawn(kHero, 3000.0f);
    Unit* follower = m.Spawn(Ids::kSoldier, 1500.0f);
    m.camp.hero = hero;
    follower->SetSquad(Squads::kWarband);

    follower->TickAi();
    CHECK_MSG(follower->CurrentState() == Unit::State::Moving, "far from the hero, it marches");
    CHECK_MSG(follower->MoveTarget().x > 2000.0f, "to the hero's side, not to a post");

    // Twenty seconds of walking at the harness's step.
    for (int i = 0; i < 400; ++i) follower->Step(0.05);
    follower->TickAi();
    CHECK_MSG(std::fabs(follower->Position().x - hero->Position().x) < 300.0f,
              "the follower closed most of the gap");

    // Added by the port: inside the slack it stands rather than shuffling at
    // the tick rate, and it stands BEHIND the hero, who faces right.
    CHECK(follower->CurrentState() == Unit::State::Idle);
    CHECK(follower->Position().x < hero->Position().x);
}

void testAWarbandSoldierEngagesOnTheMarchAndBreaksOffPastTheLeash() {
    Muster m;
    Unit* hero = m.Spawn(kHero, 3000.0f);
    Unit* follower = m.Spawn(Ids::kSoldier, 2000.0f);
    m.camp.hero = hero;
    follower->SetSquad(Squads::kWarband);
    follower->TickAi();
    CHECK_MSG(follower->CurrentState() == Unit::State::Moving, "precondition: on the march");

    // Added by the port: a thousand px out it is past the leash, and the
    // leash-aware scan sees nothing - not even a raider inside its aggro. It
    // marches on.
    Unit* raider = m.Spawn(Ids::kRaider, follower->Position().x + 100.0f);
    follower->TickAi();
    CHECK(follower->CurrentState() == Unit::State::Moving && follower->AttackTarget() == nullptr);

    // Inside the leash - the harness walks it there first - it engages.
    follower->SetPosition(glm::vec2(2600.0f, kRow));
    raider->SetPosition(glm::vec2(2700.0f, kRow));
    follower->TickAi();
    CHECK_MSG(follower->CurrentState() == Unit::State::Attacking,
              "warband engages enemies even mid-march");
    CHECK(follower->AttackTarget() == raider);

    // The fight goes on while the hero moves off, to 200 px past the leash.
    raider->SetPosition(glm::vec2(follower->Position().x + 30.0f, kRow));
    hero->SetPosition(glm::vec2(
        follower->Position().x + static_cast<float>(Muster::Economy("warband_leash_px")) + 200.0f,
        kRow));
    follower->TickAi();
    CHECK_MSG(follower->CurrentState() == Unit::State::Moving && follower->AttackTarget() == nullptr,
              "past the leash the follower abandons the fight and returns to the hero");
    CHECK_MSG(follower->MoveTarget().x > raider->Position().x, "back toward the hero");

    // The regression the original's review found: the next ticks must not
    // pick the same raider straight back up.
    follower->TickAi();
    follower->TickAi();
    CHECK_MSG(follower->CurrentState() == Unit::State::Moving && follower->AttackTarget() == nullptr,
              "the leash holds across ticks (no ATTACKING/MOVING flip-flop)");
}

void testAGarrisonSoldierWalksHomeAndFightsWhatItMeetsOnTheWay() {
    Muster m;
    Unit* holder = m.Spawn(Ids::kSoldier, 2200.0f, 700.0f);
    holder->SetHomePost(glm::vec2(1600.0f, 700.0f));

    holder->TickAi();
    CHECK_MSG(holder->CurrentState() == Unit::State::Moving &&
                  std::fabs(holder->MoveTarget().x - 1600.0f) < 1.0f,
              "garrison walks back to its home post");

    Unit* ambusher = m.Spawn(Ids::kRaider, holder->Position().x - 100.0f, 700.0f);
    holder->TickAi();
    CHECK_MSG(holder->CurrentState() == Unit::State::Attacking,
              "a garrison unit marching home engages ambushers");
    CHECK(holder->AttackTarget() == ambusher);
}

void testAPlayersMoveOrderStillWalksPastAnEnemy() {
    // Added by the port. The march home and a player's order are one MOVING
    // state, told apart only by the flag CommandMoveTo clears. Without the
    // clear, an order given to a unit already marching home inherits its
    // combat-awareness, and a retreat stops at the first raider.
    Muster m;
    Unit* holder = m.Spawn(Ids::kSoldier, 2200.0f, 700.0f);
    holder->SetHomePost(glm::vec2(1600.0f, 700.0f));
    holder->TickAi();
    CHECK_MSG(holder->CurrentState() == Unit::State::Moving, "precondition: marching home");

    holder->CommandMoveTo(glm::vec2(1000.0f, 700.0f));
    m.Spawn(Ids::kRaider, 2100.0f, 700.0f);
    holder->TickAi();
    CHECK(holder->CurrentState() == Unit::State::Moving);
    CHECK(holder->AttackTarget() == nullptr);
}

void testASoldierWithNowhereToGoHolds() {
    // Added by the port: the early-outs. Every unit the boot or a wave spawns
    // has no post, and must not set off for one at infinity.
    Muster m;
    Unit* drafted = m.Spawn(Ids::kSoldier, 2200.0f);
    drafted->TickAi();
    CHECK_MSG(drafted->CurrentState() == Unit::State::Idle, "no post: it holds");

    Unit* posted = m.Spawn(Ids::kSoldier, 1650.0f);
    posted->SetHomePost(glm::vec2(1600.0f, kRow));
    posted->TickAi();
    CHECK_MSG(posted->CurrentState() == Unit::State::Idle, "within 60 px of its post it stays");

    // A warband soldier with no hero holds too, even with a post: it is not
    // in the garrison, so the post is not its to walk to.
    Unit* orphan = m.Spawn(Ids::kSoldier, 2400.0f);
    orphan->SetSquad(Squads::kWarband);
    orphan->SetHomePost(glm::vec2(1600.0f, kRow));
    orphan->TickAi();
    CHECK_MSG(orphan->CurrentState() == Unit::State::Idle, "a warband with no hero holds");

    // And a fallen hero is no hero.
    Unit* hero = m.Spawn(kHero, 3000.0f);
    hero->Kill();
    m.camp.hero = hero;
    orphan->TickAi();
    CHECK_MSG(orphan->CurrentState() == Unit::State::Idle, "a dead hero is not followed");
}

// --- C. Priests in either squad ---------------------------------------------

void testAPriestHealsFirstAndOtherwiseTailsTheHero() {
    Muster m;
    Unit* hero = m.Spawn(kHero, 4000.0f);
    Unit* priest = m.Spawn(Ids::kPriest, 3000.0f);
    Unit* hurt = m.Spawn(Ids::kSoldier, 3080.0f);
    m.camp.hero = hero;
    priest->SetSquad(Squads::kWarband);

    hurt->SetHp(10);
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing,
              "a wounded ally outranks following the hero");

    hurt->SetHp(hurt->Stats().maxHp);
    priest->TickAi();   // the patient is well: idle
    CHECK(priest->CurrentState() == Unit::State::Idle);
    priest->TickAi();   // nothing to heal: follow
    CHECK_MSG(priest->CurrentState() == Unit::State::Moving && priest->MoveTarget().x > 3500.0f,
              "nothing to heal -> the warband priest tails the hero");
}

void testAPriestBreaksOffAChasePastItsLeash() {
    Muster m;
    Unit* hero = m.Spawn(kHero, 3000.0f);
    Unit* priest = m.Spawn(Ids::kPriest, 3000.0f);
    Unit* hurt = m.Spawn(Ids::kSoldier, 3080.0f);
    m.camp.hero = hero;
    priest->SetSquad(Squads::kWarband);

    hurt->SetHp(10);
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing, "latched the wounded ally");

    // The chase has dragged it 600 px from the hero; the leash is 500.
    priest->SetPosition(glm::vec2(3600.0f, kRow));
    hurt->SetPosition(glm::vec2(3680.0f, kRow));
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() != Unit::State::Healing,
              "a warband priest breaks off a chase past the leash");
    CHECK(priest->HealTarget() == nullptr);

    // A garrison priest measures from its post, and no hero is involved.
    m.camp.hero = nullptr;
    priest->SetSquad(Squads::kGarrison);
    priest->SetHomePost(glm::vec2(3600.0f, kRow));
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing, "garrison priest heals near its post");
    priest->SetPosition(glm::vec2(4200.0f, kRow));
    hurt->SetPosition(glm::vec2(4280.0f, kRow));
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() != Unit::State::Healing,
              "a garrison priest will not desert its post mid-chase");

    // Added by the port: with no post and no hero there is nothing to
    // measure from, and so no leash.
    priest->SetHomePost(Unit::NoPost());
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing, "precondition: re-latched");
    priest->SetPosition(glm::vec2(5000.0f, kRow));
    hurt->SetPosition(glm::vec2(5080.0f, kRow));
    priest->TickAi();
    CHECK(priest->CurrentState() == Unit::State::Healing);
}

// --- verify_casters C and E: the heal itself --------------------------------

void testThePriestCastsHealAmountOnTheAttackCooldown() {
    Muster m;
    Unit* priest = m.Spawn(Ids::kPriest, 2000.0f);
    Unit* ally = m.Spawn(Ids::kSoldier, 2060.0f);
    m.Spawn(Ids::kRaider, 5000.0f);   // far away, and never a patient
    std::vector<int> heals;
    recordHeals(m, heals);

    ally->SetHp(20);
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing && priest->HealTarget() == ally,
              "tick: wounded ally in range -> HEALING");

    // One step of the harness's 0.016. The ally is 60 px off, inside the
    // cast reach of 120 plus half a soldier, so the first step casts.
    priest->Step(0.016);
    CHECK_EQ(priest->Stats().healAmount, 8);
    CHECK_EQ(ally->Hp(), 20 + 8);
    CHECK(heals == std::vector<int>{8});
    CHECK_MSG(priest->ToSave(SidTable())["attack_cd"].AsNumber(0.0) > 0.0,
              "cast spends the cooldown");
    priest->Step(0.016);
    CHECK_MSG(ally->Hp() == 28, "cooldown gates the next cast");
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing, "still channeling while ally is hurt");

    ally->SetHp(ally->Stats().maxHp);
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Idle && priest->HealTarget() == nullptr,
              "ally at full hp -> back to IDLE");
}

void testReceiveHealClampsAndSaysWhatItRestored() {
    Muster m;
    Unit* ally = m.Spawn(Ids::kSoldier, 2060.0f);
    std::vector<int> heals;
    recordHeals(m, heals);

    ally->SetHp(ally->Stats().maxHp - 3);
    ally->ReceiveHeal(999);
    CHECK_MSG(ally->Hp() == ally->Stats().maxHp, "receive_heal clamps at max_hp");
    CHECK_MSG(heals == std::vector<int>{3}, "the event reports what was ACTUALLY restored");
    ally->ReceiveHeal(5);
    CHECK_MSG(heals == std::vector<int>{3}, "healing a full unit is a silent no-op");

    // Added by the port: a zero heal and a corpse are no-ops too.
    ally->SetHp(10);
    ally->ReceiveHeal(0);
    CHECK_EQ(ally->Hp(), 10);
    ally->Kill();
    ally->ReceiveHeal(5);
    CHECK_EQ(ally->Hp(), 10);
    CHECK_EQ(static_cast<int>(heals.size()), 1);
}

void testBeingHealedDoesNotPausePassiveRegen() {
    // Added by the port, from receive_heal's own comment: being healed is not
    // being hit, so the regen clock keeps running. Two soldiers take the same
    // blow and one is healed a second later. At 0.8 hp a second after a
    // four-second pause, six seconds leaves the control one hp up; had the
    // heal restarted the clock, the healed one would have regenerated nothing
    // and be one short.
    Muster m;
    Unit* healed = m.Spawn(Ids::kSoldier, 1000.0f);
    Unit* control = m.Spawn(Ids::kSoldier, 1400.0f);
    healed->TakeDamage(30);
    control->TakeDamage(30);
    for (int i = 0; i < 10; ++i) {
        healed->Step(0.1);
        control->Step(0.1);
    }
    healed->ReceiveHeal(5);
    for (int i = 0; i < 50; ++i) {
        healed->Step(0.1);
        control->Step(0.1);
    }
    CHECK_MSG(control->Hp() > 30, "precondition: regen has started");
    CHECK_EQ(healed->Hp(), control->Hp() + 5);
}

void testAHealerRefusesAnAttackOrder() {
    Muster m;
    Unit* priest = m.Spawn(Ids::kPriest, 2000.0f);
    Unit* raider = m.Spawn(Ids::kRaider, 5000.0f);
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Idle, "precondition: nobody to heal");

    priest->CommandAttack(raider);
    CHECK_MSG(priest->CurrentState() != Unit::State::Attacking && priest->AttackTarget() == nullptr,
              "a healer ignores attack orders");
    CHECK(!priest->OrderedToAttack());

    // The same order is a real one: a soldier takes it.
    Unit* soldier = m.Spawn(Ids::kSoldier, 2100.0f);
    soldier->CommandAttack(raider);
    CHECK(soldier->CurrentState() == Unit::State::Attacking);
}

void testTheHealTargetRidesTheSave() {
    Muster m;
    Unit* priest = m.Spawn(Ids::kPriest, 2000.0f);
    Unit* ally = m.Spawn(Ids::kSoldier, 2060.0f);
    ally->SetHp(20);
    priest->TickAi();
    CHECK_MSG(priest->CurrentState() == Unit::State::Healing, "precondition: channelling");

    SidTable ids;
    ids.Add(static_cast<const Damageable*>(priest));
    const int allySid = ids.Add(static_cast<const Damageable*>(ally));
    const Value saved = priest->ToSave(ids);
    CHECK_MSG(static_cast<int>(saved["ref_heal"].AsNumber(-1.0)) == allySid,
              "to_save writes ref_heal via the id map");

    Unit* restored = m.Spawn(Ids::kPriest, 2000.0f);
    restored->FromSave(saved);
    SidResolver resolver;
    resolver.Bind(allySid, ally);
    int unresolved = 0;
    restored->Relink(saved, resolver, &unresolved);
    CHECK_MSG(restored->HealTarget() == ally, "relink resolves ref_heal back to the ally");
    CHECK(restored->CurrentState() == Unit::State::Healing);
    CHECK_EQ(unresolved, 0);

    // Added by the port: a patient the file names but nobody rebuilt is
    // counted, not quietly dropped.
    Unit* orphan = m.Spawn(Ids::kPriest, 2000.0f);
    orphan->FromSave(saved);
    orphan->Relink(saved, SidResolver(), &unresolved);
    CHECK(orphan->HealTarget() == nullptr);
    CHECK_EQ(unresolved, 1);
}

void testSteeringNeverSurvivesALoad() {
    // Added by the port: the two edits the widened enum needed. HEALING loads
    // as HEALING; CONTROLLED loads as IDLE, because steering is input and a
    // load never resumes it; past the end is IDLE as before.
    Muster m;
    Unit* soldier = m.Spawn(Ids::kSoldier, 2000.0f);
    Value saved = soldier->ToSave(SidTable());

    saved.Set("state", Value(9.0));
    soldier->FromSave(saved);
    CHECK(soldier->CurrentState() == Unit::State::Healing);
    saved.Set("state", Value(10.0));
    soldier->FromSave(saved);
    CHECK(soldier->CurrentState() == Unit::State::Idle);

    saved.Set("state", Value(9.0));
    soldier->FromSave(saved);
    saved.Set("state", Value(8.0));
    soldier->FromSave(saved);
    CHECK(soldier->CurrentState() == Unit::State::Idle);
}

// --- D. Where a recruit goes ------------------------------------------------

void testARecruitCarriesItsBuildingsSquad() {
    Muster m;
    m.Place(Ids::kTownHall, 1200.0f);
    Building* barracks = m.Place(Ids::kBarracks, 2600.0f);
    std::vector<std::string> squads;
    m.bus.unitTrained.Connect([&squads](const std::string&, const glm::vec2&, const glm::vec2&,
                                        const std::string& squad) { squads.push_back(squad); });

    CHECK_MSG(barracks->RecruitSquad() == Squads::kGarrison, "recruits default to the garrison");
    barracks->SetRecruitSquad(Squads::kWarband);
    barracks->EnqueueTraining(Ids::kSoldier);
    CHECK_MSG(barracks->QueueLength() == 1, "precondition: queued");
    barracks->Step(wb::Shipped().Unit(Ids::kSoldier)["train_time"].AsNumber(0.0) + 0.1);
    CHECK_MSG(squads == std::vector<std::string>{Squads::kWarband},
              "unit_trained carries the building's recruit squad");

    const Value saved = barracks->ToSave();
    barracks->SetRecruitSquad(Squads::kGarrison);
    barracks->FromSave(saved);
    CHECK_MSG(barracks->RecruitSquad() == Squads::kWarband, "recruit_squad rides the building save");
}

void testASquadAndAPostRideTheUnitSave() {
    Muster m;
    Unit* unit = m.Spawn(Ids::kSoldier, 1800.0f);
    unit->SetSquad(Squads::kWarband);
    unit->SetHomePost(glm::vec2(1750.0f, 650.0f));
    const Value saved = unit->ToSave(SidTable());
    unit->SetSquad(Squads::kGarrison);
    unit->SetHomePost(Unit::NoPost());
    unit->FromSave(saved);
    CHECK_MSG(unit->Squad() == Squads::kWarband && unit->HomePost() == glm::vec2(1750.0f, 650.0f),
              "squad + home post ride the unit save");

    // Added by the port: no post saves as [] and loads as no post, and a save
    // from before squads loads as a garrison with no post.
    Unit* plain = m.Spawn(Ids::kSoldier, 1800.0f);
    Value old = plain->ToSave(SidTable());
    CHECK(old["home"].AsArray().empty());
    old.Set("squad", Value());
    old.Set("home", Value());
    plain->SetSquad(Squads::kWarband);
    plain->SetHomePost(glm::vec2(1.0f, 2.0f));
    plain->FromSave(old);
    CHECK(plain->Squad() == Squads::kGarrison);
    CHECK(!Unit::HasPost(plain->HomePost()));
}

void testATrainedUnitJoinsItsSquadAndTakesItsPost() {
    // main.gd's _on_unit_trained through a real Match: the emission a
    // building makes, and what the match does with it.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();
    const int before = static_cast<int>(match.Units().size());

    match.Bus().unitTrained.Emit(Ids::kSoldier, glm::vec2(2600.0f, 0.0f),
                                 glm::vec2(2800.0f, 700.0f), Squads::kWarband);
    CHECK_EQ(static_cast<int>(match.Units().size()), before + 1);
    const Unit* rallied = match.Units().back().get();
    CHECK(rallied->Squad() == Squads::kWarband);
    CHECK_MSG(rallied->HomePost() == glm::vec2(2800.0f, 700.0f), "a rally IS the post");
    CHECK(rallied->CurrentState() == Unit::State::Moving);

    match.Bus().unitTrained.Emit(Ids::kSoldier, glm::vec2(2600.0f, 0.0f), Building::NoRally(),
                                 Squads::kGarrison);
    const Unit* stayed = match.Units().back().get();
    CHECK_MSG(stayed->HomePost() == stayed->Position(), "no rally: the post is where it appeared");
    CHECK(stayed->CurrentState() == Unit::State::Idle);

    match.Bus().unitTrained.Emit(Ids::kWorker, glm::vec2(1500.0f, 0.0f), Building::NoRally(),
                                 Squads::kWarband);
    CHECK_MSG(match.Units().back()->Squad() == Squads::kGarrison,
              "a worker cannot follow, whatever its building recruits for");
}

// --- E. The shelter bell ----------------------------------------------------

void testTheBellSendsWorkersToTheHallAndHoldsThemThere() {
    Muster m;
    Building* hall = m.Place(Ids::kTownHall, 1200.0f);
    Unit* worker = m.Spawn(Ids::kWorker, 2400.0f);

    // Added by the port: a tree beside the worker, so "never seeks work" is
    // refusing something. The harness has none, and there it is vacuous.
    m.camp.node.resource = Ids::kWood;
    m.camp.node.amount = 100;
    m.camp.node.maxAmount = 100;
    m.camp.node.position = glm::vec2(2450.0f, kRow);
    m.camp.hasNode = true;

    m.state.SetWorkersSheltered(true);
    worker->TickAi();
    CHECK_MSG(worker->CurrentState() == Unit::State::Fleeing,
              "the bell sends a far worker running for a drop-off");

    for (int i = 0; i < 300; ++i) worker->Step(0.05);
    worker->TickAi();
    CHECK_MSG(worker->CurrentState() == Unit::State::Idle &&
                  glm::distance(worker->Position(), hall->Position()) <=
                      worker->Stats().depositRange + 5.0f,
              "the worker holes up at the drop-off and stands");
    worker->TickAi();
    CHECK_MSG(worker->CurrentState() == Unit::State::Idle, "sheltered workers never seek work");

    // Added by the port: ringing it off puts the village back to work.
    m.state.SetWorkersSheltered(false);
    worker->TickAi();
    CHECK(worker->CurrentState() == Unit::State::Gathering);
}

void testTheBellRidesTheRunSave() {
    Muster m;
    m.state.SetWorkersSheltered(true);
    const Value saved = m.state.ToSave();
    m.state.SetWorkersSheltered(false);
    m.state.FromSave(saved, MetaLevels{});
    CHECK_MSG(m.state.WorkersSheltered(), "the bell state rides the run save");
}

// --- verify_casters D: a temple trains a priest ------------------------------

void testATempleTrainsAPriest() {
    Muster m;
    m.Place(Ids::kTownHall, 1500.0f);   // supply comes from buildings
    Building* temple = m.Place(Ids::kTemple, 1700.0f);
    std::vector<std::string> trained;
    m.bus.unitTrained.Connect([&trained](const std::string& id, const glm::vec2&,
                                         const glm::vec2&, const std::string&) {
        trained.push_back(id);
    });

    CHECK_MSG(temple->CanTrain(Ids::kPriest), "a complete temple can train a priest");
    temple->EnqueueTraining(Ids::kPriest);
    CHECK_MSG(temple->QueueLength() == 1 && temple->TrainQueue()[0] == Ids::kPriest,
              "priest accepted into the temple queue");
    temple->Step(wb::Shipped().Unit(Ids::kPriest)["train_time"].AsNumber(0.0) + 0.1);
    CHECK_MSG(trained == std::vector<std::string>{Ids::kPriest},
              "training completes -> unit_trained('priest')");
}

} // namespace

static void runTests() {
    testFightersFollowWhileWorkersAndTheHeroNeverDo();
    testTheHornsRelabelEveryLivingFollowerAndNothingElse();

    testAWarbandSoldierMarchesToTheHerosSideAndStandsThere();
    testAWarbandSoldierEngagesOnTheMarchAndBreaksOffPastTheLeash();
    testAGarrisonSoldierWalksHomeAndFightsWhatItMeetsOnTheWay();
    testAPlayersMoveOrderStillWalksPastAnEnemy();
    testASoldierWithNowhereToGoHolds();

    testAPriestHealsFirstAndOtherwiseTailsTheHero();
    testAPriestBreaksOffAChasePastItsLeash();

    testThePriestCastsHealAmountOnTheAttackCooldown();
    testReceiveHealClampsAndSaysWhatItRestored();
    testBeingHealedDoesNotPausePassiveRegen();
    testAHealerRefusesAnAttackOrder();
    testTheHealTargetRidesTheSave();
    testSteeringNeverSurvivesALoad();

    testARecruitCarriesItsBuildingsSquad();
    testASquadAndAPostRideTheUnitSave();
    testATrainedUnitJoinsItsSquadAndTakesItsPost();

    testTheBellSendsWorkersToTheHallAndHoldsThemThere();
    testTheBellRidesTheRunSave();

    testATempleTrainsAPriest();
}

TEST_MAIN("test_wb_squads", 90)
