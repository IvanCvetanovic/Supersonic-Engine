// The hero under the player's hand, against the original's harness.
//
// `verify_hero` covers seven things, and four are this suite's: B, the unit's
// CONTROLLED state; F, HeroControl's possession lifecycle; G, the abilities,
// less its Q/E routing and on-screen buttons; and the two lines of A that are
// data. The rest of A (the control scheme), C (the camera follow), D (input
// routing) and E (the joystick) are presentation and input.
//
// To re-derive, from the game's directory:
//
//   & .\tools\godot.bat --headless --path . res://tools/verify_hero.tscn
//
// At the game's 50741d1, on 10 September 2026, it printed 72 ok lines and no
// failures. The ones reproduced here:
//
//   ok  : hero is data-flagged controllable
//   ok  : worker is NOT controllable
//   ok  : set_controlled(true) -> CONTROLLED
//   ok  : steering right moves at move_speed
//   ok  : steering down stops at the band's bottom edge
//   ok  : AI tick makes no decisions while CONTROLLED (no auto-acquire)
//   ok  : manual strike damages the enemy in reach
//   ok  : strike faces its target
//   ok  : cooldown gates manual strikes
//   ok  : a strike out of lane reach whiffs (no damage)
//   ok  : a whiff still swings (faces + spends cooldown)
//   ok  : set_controlled(false) -> IDLE
//   ok  : after release, the AI re-acquires (defender)
//   ok  : from_save maps CONTROLLED -> IDLE
//   ok  : a DEAD unit cannot be controlled
//   ok  : possess(hero) begins control
//   ok  : the possessed hero is published (active_unit + the static leader ref)
//   ok  : active_changed(true) fired once
//   ok  : stick steering reaches the unit
//   ok  : keyboard steers when the stick is idle
//   ok  : a non-controllable unit is refused (hero keeps control)
//   ok  : the hero dying ends control
//   ok  : hero_lost fires once with the death position
//   ok  : active_changed(false) fired on death
//   ok  : a DEAD hero cannot be re-possessed
//   ok  : a respawn delay is configured (economy.json)
//   ok  : the Town Hall is a respawn point
//   ok  : the Waystone is a buildable respawn point
//   ok  : hero has two ability slots
//   ok  : cleave + dash defs exist in abilities.json
//   ok  : the _comment key is not an ability
//   ok  : no cast while under AI (hero-mode only)
//   ok  : cleave hits every enemy in radius, both sides (lane |dx|)
//   ok  : cleave spares enemies beyond its radius
//   ok  : cleave starts its cooldown
//   ok  : cooldown blocks a second cleave
//   ok  : cooldowns tick down
//   ok  : ability cooldowns ride the save
//   ok  : dash grants i-frames
//   ok  : a hit during dash i-frames is dodged entirely
//   ok  : dash travels its configured distance (260px)
//   ok  : after the i-frames, hits land again
//   ok  : dash clamps to the band
//
// The harness zeroes `_attack_cd` by hand between strikes. The port has no
// setter for it and steps the unit a second instead. Under control, with no
// steering, a step does nothing else: the AI does not think and the hero is
// at full health.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Building.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/HeroControl.hpp"
#include "sim/Lane.hpp"
#include "sim/Match.hpp"
#include "sim/Projectiles.hpp"
#include "sim/Unit.hpp"
#include "sim/World.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace WolfBrigade;
using Supersonic::Json::Value;

namespace {

// Somewhere to stand and something to hit. Nothing to gather, build or heal.
class Arena final : public World {
public:
    Lane lane;
    ProjectilePool arrows;

    ResourceNode* NearestHarvestable(float) const override { return nullptr; }
    int NearestDeposit(float) const override { return -1; }
    bool DepositExists(int) const override { return false; }
    glm::vec2 DepositPosition(int) const override { return glm::vec2(0.0f); }
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
    std::vector<Building*> PlayerBuildings() const override { return {}; }
    std::vector<Unit*> PlayerUnits() const override { return {}; }
    Unit* Hero() const override { return nullptr; }
    std::vector<Unit*> EnemiesWithin(const std::string& faction, float x,
                                     float maxRange) const override {
        return lane.EnemiesWithin(faction, x, maxRange);
    }
};

struct Field {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    Arena arena;
    std::vector<std::unique_ptr<Unit>> units;

    Field() { state.Reset(); }

    Unit* Spawn(const std::string& id, float x, float y) {
        auto unit = std::make_unique<Unit>(UnitStats::FromJson(id, wb::Shipped().Unit(id)), state,
                                           bus, arena);
        unit->SetPosition(glm::vec2(x, y));
        Unit* raw = unit.get();
        units.push_back(std::move(unit));
        arena.lane.Register(raw);
        return raw;
    }

    // A unit from a hand-edited block, for the case a data file cannot reach.
    Unit* Spawn(const UnitStats& stats, float x, float y) {
        auto unit = std::make_unique<Unit>(stats, state, bus, arena);
        unit->SetPosition(glm::vec2(x, y));
        Unit* raw = unit.get();
        units.push_back(std::move(unit));
        arena.lane.Register(raw);
        return raw;
    }
};

float groundY() { return wb::Shipped().World()["ground_y"].AsFloat(0.0f); }
float laneDepth() { return wb::Shipped().World()["lane"]["depth"].AsFloat(0.0f); }

// --- A. Who can be steered ---------------------------------------------------

void testOnlyTheHeroIsDataFlaggedControllable() {
    CHECK_MSG(UnitStats::FromJson(Ids::kHero, wb::Shipped().Unit(Ids::kHero)).controllable,
              "hero is data-flagged controllable");
    CHECK_MSG(!UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker)).controllable,
              "worker is NOT controllable");
}

// --- B. The CONTROLLED state -------------------------------------------------

void testAControlledHeroSteersInTwoDimensionsInsideTheBand() {
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, groundY() + 40.0f);
    f.Spawn(Ids::kRaider, 1560.0f, groundY() + 40.0f);

    hero->SetControlled(true);
    CHECK_MSG(hero->IsControlled() && hero->CurrentState() == Unit::State::Controlled,
              "set_controlled(true) -> CONTROLLED");

    const float x0 = hero->Position().x;
    hero->SetControlDir(glm::vec2(1.0f, 0.0f));
    hero->Step(0.5);
    CHECK_MSG(std::fabs(hero->Position().x - (x0 + hero->Stats().moveSpeed * 0.5f)) < 1.0f,
              "steering right moves at move_speed");

    hero->SetControlDir(glm::vec2(0.0f, 1.0f));
    for (int i = 0; i < 60; ++i) hero->Step(0.25);
    CHECK_MSG(std::fabs(hero->Position().y - (groundY() + laneDepth())) < 0.5f,
              "steering down stops at the band's bottom edge");
    hero->SetControlDir(glm::vec2(0.0f));

    // A raider stands inside the hero's aggro, and must not be taken.
    hero->TickAi();
    CHECK_MSG(hero->CurrentState() == Unit::State::Controlled && hero->AttackTarget() == nullptr,
              "AI tick makes no decisions while CONTROLLED (no auto-acquire)");
}

void testSteeringIsClampedToUnitLengthAndToTheWorld() {
    // Added by the port. A diagonal is normalised so it never outruns
    // move_speed; a half deflection is kept as it is, for half speed; and
    // the world's left edge holds a hero steering into it.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 10.0f, 700.0f);
    hero->SetControlled(true);

    hero->SetControlDir(glm::vec2(1.0f, 1.0f));
    CHECK(std::fabs(glm::length(hero->ControlDir()) - 1.0f) < 1e-6f);
    hero->SetControlDir(glm::vec2(0.5f, 0.0f));
    CHECK(hero->ControlDir() == glm::vec2(0.5f, 0.0f));

    hero->SetControlDir(glm::vec2(-1.0f, 0.0f));
    hero->Step(1.0);
    CHECK_EQ(hero->Position().x, 0.0f);
    CHECK_MSG(hero->Facing() < 0.0, "steering left turns it left");
}

void testTakingControlDropsTheFightInHand() {
    // Added by the port: set_controlled clears the attack, so a hero picked
    // up mid-swing does not keep swinging on its own.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, 640.0f);
    f.Spawn(Ids::kRaider, 1560.0f, 640.0f);
    hero->TickAi();
    CHECK_MSG(hero->CurrentState() == Unit::State::Attacking, "precondition: fighting");
    hero->SetControlled(true);
    CHECK(hero->AttackTarget() == nullptr);
    CHECK(hero->CurrentState() == Unit::State::Controlled);
}

void testAManualStrikeHitsOnlyWhatIsInLaneReach() {
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, groundY() + laneDepth());
    Unit* raider = f.Spawn(Ids::kRaider, 1560.0f, groundY());
    int swings = 0;
    f.bus.unitAttacked.Connect([&swings](const glm::vec2&) { ++swings; });
    hero->SetControlled(true);

    // Sixty px along the lane, inside 46 plus half a raider - and the whole
    // band's depth away in y, which is the lane rule: combat is |dx|.
    const int hp0 = raider->Hp();
    hero->ControlledAttackAuto();
    CHECK_MSG(raider->Hp() == hp0 - hero->EffectiveDamage(),
              "manual strike damages the enemy in reach");
    CHECK_MSG(hero->Facing() > 0.0, "strike faces its target");
    const int hp1 = raider->Hp();
    hero->ControlledAttackAuto();
    CHECK_MSG(raider->Hp() == hp1, "cooldown gates manual strikes");

    hero->Step(1.0);   // the harness zeroes _attack_cd here
    CHECK_MSG(hero->AttackCooldown() <= 0.0, "precondition: the cooldown has run out");
    raider->SetPosition(glm::vec2(hero->Position().x + 400.0f, groundY()));
    const int before = swings;
    hero->ControlledAttackAt(raider->Position());
    CHECK_MSG(raider->Hp() == hp1, "a strike out of lane reach whiffs (no damage)");
    CHECK_MSG(hero->Facing() > 0.0 && hero->AttackCooldown() > 0.0,
              "a whiff still swings (faces + spends cooldown)");

    // Added by the port: the whiff is heard, so the input always reads back.
    CHECK_EQ(swings, before + 1);
}

void testAManualStrikeTurnsToAThreatBehind() {
    // Added by the port. The harness's hero already faces right, so its
    // "strike faces its target" holds before the strike runs. A raider on
    // the left is the case where the turn is the strike's doing.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, 640.0f);
    Unit* raider = f.Spawn(Ids::kRaider, 1440.0f, 640.0f);
    hero->SetControlled(true);
    CHECK_MSG(hero->Facing() > 0.0, "precondition: facing right");
    const int hp0 = raider->Hp();
    hero->ControlledAttackAuto();
    CHECK(hero->Facing() < 0.0);
    CHECK(raider->Hp() < hp0);
}

void testReleasedTheAiTakesOverAndASaveNeverResumesControl() {
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, groundY() + laneDepth());
    Unit* raider = f.Spawn(Ids::kRaider, 3000.0f, groundY());
    hero->SetControlled(true);

    hero->SetControlled(false);
    CHECK_MSG(hero->CurrentState() == Unit::State::Idle, "set_controlled(false) -> IDLE");
    raider->SetPosition(glm::vec2(hero->Position().x + 100.0f, groundY()));
    hero->TickAi();
    CHECK_MSG(hero->CurrentState() == Unit::State::Attacking,
              "after release, the AI re-acquires (defender)");

    Supersonic::Json::Object record;
    record["state"] = Value(static_cast<double>(static_cast<int>(Unit::State::Controlled)));
    hero->FromSave(Value(std::move(record)));
    CHECK_MSG(hero->CurrentState() == Unit::State::Idle, "from_save maps CONTROLLED -> IDLE");

    hero->Kill();
    hero->SetControlled(true);
    CHECK_MSG(hero->CurrentState() == Unit::State::Dead, "a DEAD unit cannot be controlled");
}

void testAFrozenBoardTakesNoStrikes() {
    // Added by the port, from the original's own guard: keys leak through
    // the game-over overlay, and the board is frozen.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, 640.0f);
    Unit* raider = f.Spawn(Ids::kRaider, 1560.0f, 640.0f);
    hero->SetControlled(true);
    f.state.Lose();
    const int hp0 = raider->Hp();
    hero->ControlledAttackAuto();
    hero->ControlledAttackAt(raider->Position());
    CHECK_EQ(raider->Hp(), hp0);
    CHECK_EQ(hero->AttackCooldown(), 0.0);
}

// --- F. HeroControl ----------------------------------------------------------

void testHeroControlPossessesTheHeroAndLetsGoWhenItFalls() {
    Field f;
    HeroControl control(f.bus);
    std::vector<bool> actives;
    std::vector<glm::vec2> losses;
    control.activeChanged.Connect([&actives](bool on) { actives.push_back(on); });
    control.heroLost.Connect([&losses](const glm::vec2& at) { losses.push_back(at); });

    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, 640.0f);
    Unit* worker = f.Spawn(Ids::kWorker, 1600.0f, 640.0f);

    control.Possess(hero);
    CHECK_MSG(control.IsActive() && hero->IsControlled(), "possess(hero) begins control");
    CHECK_MSG(control.ActiveUnit() == hero && control.Hero() == hero,
              "the possessed hero is published (active_unit + the leader ref)");
    CHECK_MSG(actives == std::vector<bool>{true}, "active_changed(true) fired once");

    control.SetStickDir(glm::vec2(1.0f, 0.0f));
    CHECK_MSG(hero->ControlDir() == glm::vec2(1.0f, 0.0f), "stick steering reaches the unit");
    control.SetStickDir(glm::vec2(0.0f));
    control.SetKeyboardDir(glm::vec2(-1.0f, 0.0f));
    CHECK_MSG(hero->ControlDir() == glm::vec2(-1.0f, 0.0f), "keyboard steers when the stick is idle");

    // Added by the port: while the stick is held it outranks the keys.
    control.SetStickDir(glm::vec2(0.0f, 1.0f));
    CHECK(hero->ControlDir() == glm::vec2(0.0f, 1.0f));
    control.SetStickDir(glm::vec2(0.0f));
    CHECK(hero->ControlDir() == glm::vec2(-1.0f, 0.0f));

    control.Possess(worker);
    CHECK_MSG(control.ActiveUnit() == hero && !worker->IsControlled(),
              "a non-controllable unit is refused (hero keeps control)");

    const float deathX = hero->Position().x;
    hero->Kill();
    CHECK_MSG(!control.IsActive() && control.Hero() == nullptr, "the hero dying ends control");
    CHECK_MSG(losses.size() == 1 && std::fabs(losses[0].x - deathX) < 1.0f,
              "hero_lost fires once with the death position");
    CHECK_MSG(actives == (std::vector<bool>{true, false}), "active_changed(false) fired on death");
    control.Possess(hero);
    CHECK_MSG(!control.IsActive(), "a DEAD hero cannot be re-possessed");

    // Added by the port: with nobody possessed, every intent is a no-op.
    control.AttackPressed();
    control.AttackAt(glm::vec2(1500.0f, 640.0f));
    control.SetStickDir(glm::vec2(1.0f, 0.0f));
    CHECK(actives.size() == 2);
}

void testTheAttackButtonStrikesThroughHeroControl() {
    // Added by the port: the two strike intents reach the unit.
    Field f;
    HeroControl control(f.bus);
    Unit* hero = f.Spawn(Ids::kHero, 1500.0f, 640.0f);
    Unit* raider = f.Spawn(Ids::kRaider, 1560.0f, 640.0f);
    control.Possess(hero);
    const int hp0 = raider->Hp();
    control.AttackPressed();
    CHECK_EQ(raider->Hp(), hp0 - hero->EffectiveDamage());
    // Two blows of 22 are more than a raider's 40: the click finishes it.
    hero->Step(1.0);
    control.AttackAt(raider->Position());
    CHECK_EQ(raider->Hp(), 0);
    CHECK(!raider->IsAlive());
}

void testTheRespawnIsConfigured() {
    CHECK_MSG(wb::Shipped().Economy()["hero_respawn_delay_s"].AsNumber(0.0) > 0.0,
              "a respawn delay is configured (economy.json)");
    CHECK_MSG(BuildingStats::FromJson(Ids::kTownHall, wb::Shipped().Building(Ids::kTownHall))
                  .heroRespawn,
              "the Town Hall is a respawn point");
    const BuildingStats waystone =
        BuildingStats::FromJson(Ids::kWaystone, wb::Shipped().Building(Ids::kWaystone));
    CHECK_MSG(waystone.heroRespawn && waystone.buildable,
              "the Waystone is a buildable respawn point");
}

void testTheMatchsWarbandFollowsWhoeverIsPossessed() {
    // Added by the port: World::Hero() in a real Match is its HeroControl's
    // hero, and it goes with the board.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();
    Unit* hero = match.SpawnUnit(
        UnitStats::FromJson(Ids::kHero, wb::Shipped().Unit(Ids::kHero)), glm::vec2(3000.0f, 640.0f));
    match.Control().Possess(hero);
    CHECK(match.Hero() == hero);

    Unit* follower = match.SpawnUnit(
        UnitStats::FromJson(Ids::kSoldier, wb::Shipped().Unit(Ids::kSoldier)),
        glm::vec2(2700.0f, 640.0f));
    follower->SetSquad(Squads::kWarband);
    follower->TickAi();
    CHECK(follower->CurrentState() == Unit::State::Moving);
    CHECK(follower->MoveTarget().x > 2700.0f);

    hero->Kill();
    CHECK_MSG(match.Hero() == nullptr, "a fallen hero leads nobody");
}

// --- G. Abilities --------------------------------------------------------------

const Value& ability(const char* id) { return wb::Shipped().Ability(id); }

void testTheHeroCarriesCleaveAndDash() {
    CHECK_MSG(UnitStats::FromJson(Ids::kHero, wb::Shipped().Unit(Ids::kHero)).abilities.size() == 2,
              "hero has two ability slots");
    CHECK_MSG(ability("cleave").IsObject() && ability("dash").IsObject(),
              "cleave + dash defs exist in abilities.json");
    CHECK_MSG(!ability("_comment").IsObject(), "the _comment key is not an ability");
}

void testCleaveHitsEveryEnemyInItsRadiusAlongTheLane() {
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    Unit* near1 = f.Spawn(Ids::kRaider, 3080.0f, 700.0f);   // |dx| 80, another row
    Unit* near2 = f.Spawn(Ids::kRaider, 2920.0f, 600.0f);   // |dx| 80, the other side
    Unit* far = f.Spawn(Ids::kRaider, 3400.0f, 640.0f);     // |dx| 400, past the radius
    std::vector<std::string> casts;
    f.bus.abilityUsed.Connect([&casts](const std::string& id, Unit*) { casts.push_back(id); });

    hero->UseAbility(0);
    CHECK_MSG(hero->AbilityCooldownLeft("cleave") == 0.0, "no cast while under AI (hero-mode only)");
    CHECK(near1->Hp() == near1->Stats().maxHp && casts.empty());

    hero->SetControlled(true);
    const int damage = static_cast<int>(
        std::round(hero->EffectiveDamage() * ability("cleave")["damage_mult"].AsNumber(0.0)));
    CHECK_EQ(damage, 35);   // 22 x 1.6, rounded
    const int farHp = far->Hp();
    hero->UseAbility(0);
    CHECK_MSG(near1->Hp() == near1->Stats().maxHp - damage &&
                  near2->Hp() == near2->Stats().maxHp - damage,
              "cleave hits every enemy in radius, both sides (lane |dx|)");
    CHECK_MSG(far->Hp() == farHp, "cleave spares enemies beyond its radius");
    CHECK_MSG(hero->AbilityCooldownLeft("cleave") > 0.0, "cleave starts its cooldown");
    CHECK(casts == std::vector<std::string>{"cleave"});
    const int after = near1->Hp();
    hero->UseAbility(0);
    CHECK_MSG(near1->Hp() == after, "cooldown blocks a second cleave");

    const double cd0 = hero->AbilityCooldownLeft("cleave");
    hero->Step(0.5);
    CHECK_MSG(hero->AbilityCooldownLeft("cleave") < cd0, "cooldowns tick down");
    CHECK_EQ(hero->AbilityCooldownLeft("cleave"), 7.5);   // added by the port: 8 - 0.5

    // The harness clears the hero's table and loads it back; a fresh hero is
    // the same question with nothing left over.
    const Value saved = hero->ToSave(SidTable());
    Unit* restored = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    restored->FromSave(saved);
    CHECK_MSG(restored->AbilityCooldownLeft("cleave") == hero->AbilityCooldownLeft("cleave"),
              "ability cooldowns ride the save");
}

void testAHeldBannerStrengthensTheCleave() {
    // Added by the port, from the harness's own header: banners boost the
    // cleave as they boost any blow. 22 x 1.5 is 33, and 33 x 1.6 is 52.8,
    // which kills a raider a plain cleave leaves on 5.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    Unit* raider = f.Spawn(Ids::kRaider, 3080.0f, 640.0f);
    f.state.SetArmyBonus(&f, 1.5);
    hero->SetControlled(true);
    hero->UseAbility(0);
    CHECK(!raider->IsAlive());
}

void testADashTravelsItsDistanceBehindIFrames() {
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    hero->SetControlled(true);
    const double distance = ability("dash")["distance"].AsNumber(0.0);
    CHECK_EQ(distance, 260.0);

    hero->SetControlDir(glm::vec2(1.0f, 0.0f));
    const float x0 = hero->Position().x;
    hero->UseAbility(1);
    hero->SetControlDir(glm::vec2(0.0f));   // the stick let go mid-dash; the dash still flies
    CHECK_MSG(hero->IsInvulnerable(), "dash grants i-frames");
    const int hp0 = hero->Hp();
    hero->TakeDamage(50);
    CHECK_MSG(hero->Hp() == hp0, "a hit during dash i-frames is dodged entirely");

    for (int i = 0; i < 30; ++i) hero->Step(0.033);   // about a second
    CHECK_MSG(std::fabs(hero->Position().x - (x0 + static_cast<float>(distance))) < 2.0f,
              "dash travels its configured distance (260px)");
    CHECK(std::fabs(hero->Position().x - (x0 + static_cast<float>(distance))) < 0.01f);
    hero->TakeDamage(10);
    CHECK_MSG(hero->Hp() == hp0 - 10, "after the i-frames, hits land again");
}

void testADashNeverLeavesTheBand() {
    Field f;
    const float bottom = groundY() + laneDepth();
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, bottom - 5.0f);
    hero->SetControlled(true);
    hero->SetControlDir(glm::vec2(0.0f, 1.0f));
    hero->UseAbility(1);
    hero->SetControlDir(glm::vec2(0.0f));
    for (int i = 0; i < 20; ++i) hero->Step(0.05);
    CHECK_MSG(std::fabs(hero->Position().y - bottom) < 0.5f, "dash clamps to the band");
}

void testAStandingDashGoesTheWayTheHeroFaces() {
    // Added by the port: with no steering the dash flies along the facing.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    hero->SetControlled(true);
    hero->SetControlDir(glm::vec2(-1.0f, 0.0f));
    hero->Step(0.1);
    hero->SetControlDir(glm::vec2(0.0f));
    CHECK_MSG(hero->Facing() < 0.0, "precondition: facing left");
    const float x0 = hero->Position().x;
    hero->UseAbility(1);
    for (int i = 0; i < 20; ++i) hero->Step(0.05);
    CHECK(std::fabs(hero->Position().x - (x0 - 260.0f)) < 0.01f);
}

void testLettingGoOfTheHeroEndsADashInFlight() {
    // Added by the port: set_controlled drops the dash; only the i-frames run on.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    hero->SetControlled(true);
    hero->SetControlDir(glm::vec2(1.0f, 0.0f));
    hero->UseAbility(1);
    hero->Step(0.05);   // 47.5 px of the 260
    const float midway = hero->Position().x;
    hero->SetControlled(false);
    hero->SetControlled(true);
    for (int i = 0; i < 20; ++i) hero->Step(0.05);
    CHECK_EQ(hero->Position().x, midway);
}

void testADodgedHitDoesNotPauseRegen() {
    // Added by the port, from take_damage's own comment. A wounded hero
    // regenerates 1.5 hp a second; a hit landing would pause that for four
    // seconds, and a dodged one must not.
    Field f;
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    hero->SetControlled(true);
    hero->SetHp(100);
    hero->Step(1.0);
    const int before = hero->Hp();
    CHECK_MSG(before > 100, "precondition: regenerating");
    hero->UseAbility(1);
    hero->TakeDamage(50);
    hero->Step(1.0);
    CHECK(hero->Hp() > before);
}

void testAnAbilityTheDataDoesNotDefineCastsNothing() {
    // Added by the port: a slot naming an id abilities.json does not have
    // spends no cooldown and announces nothing. Only a hand-edited block can
    // reach it.
    Field f;
    UnitStats stats = UnitStats::FromJson(Ids::kHero, wb::Shipped().Unit(Ids::kHero));
    stats.abilities = {"meteor"};
    Unit* hero = f.Spawn(stats, 3000.0f, 640.0f);
    int casts = 0;
    f.bus.abilityUsed.Connect([&casts](const std::string&, Unit*) { ++casts; });
    hero->SetControlled(true);
    hero->UseAbility(0);
    hero->UseAbility(1);   // no second slot at all
    CHECK_EQ(hero->AbilityCooldownLeft("meteor"), 0.0);
    CHECK_EQ(casts, 0);

    // And a frozen board casts nothing either.
    Unit* real = f.Spawn(Ids::kHero, 3500.0f, 640.0f);
    real->SetControlled(true);
    f.state.Lose();
    real->UseAbility(0);
    CHECK_EQ(real->AbilityCooldownLeft("cleave"), 0.0);
}

void testHeroControlCastsAndReportsBySlot() {
    // Added by the port: the Q/E and button intents, and the cooldown the
    // buttons paint.
    Field f;
    HeroControl control(f.bus);
    Unit* hero = f.Spawn(Ids::kHero, 3000.0f, 640.0f);
    CHECK_EQ(control.AbilityCooldownLeft(0), 0.0);
    control.UseAbility(0);   // nobody possessed: nothing
    CHECK_EQ(hero->AbilityCooldownLeft("cleave"), 0.0);

    control.Possess(hero);
    control.UseAbility(0);
    CHECK(hero->AbilityCooldownLeft("cleave") > 0.0);
    CHECK_EQ(control.AbilityCooldownLeft(0), hero->AbilityCooldownLeft("cleave"));
    CHECK_EQ(control.AbilityCooldownLeft(1), 0.0);
    CHECK_EQ(control.AbilityCooldownLeft(2), 0.0);
    CHECK_EQ(control.AbilityCooldownLeft(-1), 0.0);
}

} // namespace

static void runTests() {
    testOnlyTheHeroIsDataFlaggedControllable();

    testAControlledHeroSteersInTwoDimensionsInsideTheBand();
    testSteeringIsClampedToUnitLengthAndToTheWorld();
    testTakingControlDropsTheFightInHand();
    testAManualStrikeHitsOnlyWhatIsInLaneReach();
    testAManualStrikeTurnsToAThreatBehind();
    testReleasedTheAiTakesOverAndASaveNeverResumesControl();
    testAFrozenBoardTakesNoStrikes();

    testHeroControlPossessesTheHeroAndLetsGoWhenItFalls();
    testTheAttackButtonStrikesThroughHeroControl();
    testTheRespawnIsConfigured();
    testTheMatchsWarbandFollowsWhoeverIsPossessed();

    testTheHeroCarriesCleaveAndDash();
    testCleaveHitsEveryEnemyInItsRadiusAlongTheLane();
    testAHeldBannerStrengthensTheCleave();
    testADashTravelsItsDistanceBehindIFrames();
    testADashNeverLeavesTheBand();
    testAStandingDashGoesTheWayTheHeroFaces();
    testLettingGoOfTheHeroEndsADashInFlight();
    testADodgedHitDoesNotPauseRegen();
    testAnAbilityTheDataDoesNotDefineCastsNothing();
    testHeroControlCastsAndReportsBySlot();
}

TEST_MAIN("test_wb_hero", 80)
