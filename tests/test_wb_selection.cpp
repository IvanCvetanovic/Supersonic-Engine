// The ported selection and orders, against the original's own picks.
//
// These two files are what the port has been missing to be PLAYABLE rather than
// merely correct. Every slice before this one built a simulation that could be
// driven by a test; `selection.gd` and `commands.gd` are how a person drives it.
// Until they landed, a player could not pick a unit or tell it to go anywhere.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_units.tscn
//   ... res://tools/verify_buildings.tscn
//
// verify_units printed, on 27 August 2026:
//
//   ok  : unit selected
//   ok  : selection pruned on unit_died
//   ok  : command after death does not error
//   ok  : rect selects 2 of 3 (got 2)
//   ok  : selects the correct two
//   ok  : unit_at hits a unit under the point
//   ok  : unit_at misses far point
//
// and verify_buildings:
//
//   ok  : building_at finds the building under the point
//   ok  : select_at selects the building
//   ok  : selection_changed emitted
//
// THE BOX-SELECT GEOMETRY IS THE ONE REAL NUMBER HERE. Three units at x=1000,
// 1100 and 2000, all on the ground line, against a rectangle at (950, 700)
// sized 200x200 - so x in [950, 1150) and y in [700, 900). Two in, one out. It
// is a weak oracle compared with a wave count, and this suite says so; what it
// does pin is the half-open rectangle, because Godot's `Rect2.has_point`
// excludes the far edges and a port that included them would select a unit
// standing exactly on the boundary the player dragged to.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Commands.hpp"
#include "sim/Match.hpp"
#include "sim/Selection.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

Unit* spawnWorker(Match& match, float x) {
    return match.SpawnUnit(UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker)),
                           glm::vec2(x, 800.0f));
}

Unit* spawnRaider(Match& match, float x) {
    return match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                           glm::vec2(x, 800.0f));
}

bool holds(const Selection& selection, const Unit* unit) {
    for (const Unit* selected : selection.Units()) {
        if (selected == unit) return true;
    }
    return false;
}

// --- 1. The oracle's box-select geometry ---------------------------------

void testTheMarqueeSelectsExactlyTheUnitsInsideIt() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* a = spawnWorker(match, 1000.0f);
    Unit* b = spawnWorker(match, 1100.0f);
    Unit* c = spawnWorker(match, 2000.0f);

    // The harness's own rectangle: (950, 700) sized 200 x 200.
    match.Picked().BoxSelect(glm::vec2(950.0f, 700.0f), glm::vec2(1150.0f, 900.0f));

    CHECK_EQ(static_cast<int>(match.Picked().Units().size()), 2);
    CHECK_MSG(holds(match.Picked(), a), "the one at 1000 is in");
    CHECK_MSG(holds(match.Picked(), b), "the one at 1100 is in");
    CHECK_MSG(!holds(match.Picked(), c), "the one at 2000 is not");
}

void testTheMarqueesFarEdgesAreOutsideIt() {
    // Godot's Rect2.has_point takes the near edges and excludes the far ones,
    // and a marquee is dragged to a boundary constantly. A port that included
    // both edges would pick up a unit the player dragged AROUND rather than
    // OVER - which is invisible until somebody notices an extra soldier
    // marching off with the group.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* onNearEdge = spawnWorker(match, 950.0f);
    Unit* onFarEdge = spawnWorker(match, 1150.0f);

    match.Picked().BoxSelect(glm::vec2(950.0f, 700.0f), glm::vec2(1150.0f, 900.0f));

    CHECK_MSG(holds(match.Picked(), onNearEdge), "the near edge is inside");
    CHECK_MSG(!holds(match.Picked(), onFarEdge), "the far edge is not");
}

void testTheMarqueeMeasuresTheFeetAndNotTheBody() {
    // A point pick aims at the BODY, half a height up; a marquee is drawn
    // around ground POSITIONS. The two are seventeen pixels apart for a worker
    // and every fixture above straddles both, so a port that measured the body
    // here would pass all of them. This band sits between the two: the feet at
    // 800 are inside, the body centre at 783 is not.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* worker = spawnWorker(match, 1000.0f);
    const float centreY = 800.0f - 34.0f * 0.5f;
    CHECK_MSG(centreY < 790.0f, "the body centre really is above the band");

    match.Picked().BoxSelect(glm::vec2(950.0f, 790.0f), glm::vec2(1150.0f, 900.0f));
    CHECK_MSG(holds(match.Picked(), worker), "a marquee catches the unit by its feet");

    // And the mirror: a band that covers the body but not the feet takes
    // nothing, which is what says the choice is deliberate rather than lucky.
    match.Picked().BoxSelect(glm::vec2(950.0f, 700.0f), glm::vec2(1150.0f, 790.0f));
    CHECK_MSG(!holds(match.Picked(), worker), "and not by its chest");
}

void testAMarqueeTakesOnlyThePlayersOwnUnits() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* mine = spawnWorker(match, 1000.0f);
    spawnRaider(match, 1050.0f);

    match.Picked().BoxSelect(glm::vec2(950.0f, 700.0f), glm::vec2(1150.0f, 900.0f));

    CHECK_EQ(static_cast<int>(match.Picked().Units().size()), 1);
    CHECK_MSG(holds(match.Picked(), mine), "a marquee cannot select the enemy's army");
}

// --- 2. Picking a point ---------------------------------------------------

void testAPointPickHitsTheBodyAndMissesTheSky() {
    // The harness's two: a hit at (1000, 783) on a unit standing at (1000, 800),
    // and a miss at (1000, 400). A worker's body is 26 x 34, so its centre is
    // seventeen pixels up and the authored pick radius of 52 is what actually
    // decides.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* worker = spawnWorker(match, 1000.0f);

    CHECK_MSG(match.Picked().UnitAt(glm::vec2(1000.0f, 783.0f)) == worker,
              "unit_at hits a unit under the point");
    CHECK_MSG(match.Picked().UnitAt(glm::vec2(1000.0f, 400.0f)) == nullptr,
              "unit_at misses far point");

    // THE ORACLE'S PROBE CANNOT TELL THE TWO APART. (1000, 783) is seventeen
    // pixels from the body centre AND seventeen from the feet - it is exactly
    // between them - so a port measuring either passes. A point further up
    // separates them: 43 from the centre is a hit, 60 from the feet is not.
    CHECK_MSG(match.Picked().UnitAt(glm::vec2(1000.0f, 740.0f)) == worker,
              "a point over the body is a hit, measured from the body");
}

void testThePickRadiusComesFromTheDataAndNotFromTheBody() {
    // A worker is 26 x 34, so 0.6 of its longest side is 20.4 - far under the
    // authored 52. The radius that decides is the DATA's, and a port that used
    // only the body would make every worker a third as easy to tap.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();
    CHECK_NEAR(match.WorldLayout().pickRadius, 52.0f);

    Unit* worker = spawnWorker(match, 1000.0f);
    const float centreY = 800.0f - 34.0f * 0.5f;

    // Forty pixels out: inside the authored radius, outside the body's.
    CHECK_MSG(match.Picked().UnitAt(glm::vec2(1040.0f, centreY)) == worker,
              "forty pixels away is still a hit");
    CHECK_MSG(match.Picked().UnitAt(glm::vec2(1060.0f, centreY)) == nullptr,
              "sixty is not");
}

void testTheNearestUnitWinsAPointPick() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    // The FAR one first, deliberately. With both spawned near enough to be
    // under the point, a port that stopped at the first candidate would return
    // whichever was created first - so a fixture that spawns the nearest first
    // agrees with the bug.
    Unit* far = spawnWorker(match, 1030.0f);
    Unit* near = spawnWorker(match, 1000.0f);

    Unit* picked = match.Picked().UnitAt(glm::vec2(1005.0f, 783.0f));
    CHECK_MSG(picked == near, "the closer of two overlapping picks wins");
    CHECK_MSG(picked != far, "and not merely the first one found");
}

// --- 3. A building beats a unit -------------------------------------------

void testABuildingUnderThePointWinsOverAUnitStandingOnIt() {
    // The interactive landmarks win. A worker standing in the Town Hall's
    // doorway must not steal the tap that was meant to open its panel.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the hall is there");
    if (hall == nullptr) return;

    // The harness's own probe: a point inside the Town Hall's body.
    CHECK_MSG(match.Picked().BuildingAt(glm::vec2(1500.0f, 720.0f)) == hall,
              "building_at finds the building under the point");

    // For the contest, a point inside BOTH. The hall's footprint runs y from
    // 640 to 800; a worker's centre is seventeen pixels above the ground line
    // with a pick radius of 52, so 770 is comfortably inside each. The oracle's
    // own 720 is inside the hall and just outside the worker, which is why it
    // cannot be the probe that settles this.
    const glm::vec2 probe(1500.0f, 770.0f);

    Unit* inTheDoorway = spawnWorker(match, 1500.0f);
    CHECK_MSG(match.Picked().BuildingAt(probe) == hall, "the hall is under the probe");
    CHECK_MSG(match.Picked().UnitAt(probe) == inTheDoorway, "and a unit is genuinely there too");

    match.Picked().SelectAt(probe);
    CHECK_MSG(match.Picked().SelectedBuilding() == hall, "select_at selects the building");
    CHECK_MSG(!match.Picked().HasSelection(), "and the unit was not taken as well");
}

void testPickingOneKindClearsTheOther() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* hall = match.FindBuilding(Ids::kTownHall);
    Unit* worker = spawnWorker(match, 3000.0f);
    if (hall == nullptr) return;

    match.Picked().SelectOnly(worker);
    CHECK_MSG(match.Picked().HasSelection(), "a unit is picked");
    CHECK_MSG(!match.Picked().HasBuildingSelected(), "and no building");

    match.Picked().SelectBuilding(hall);
    CHECK_MSG(match.Picked().HasBuildingSelected(), "now a building");
    CHECK_MSG(!match.Picked().HasSelection(), "and the units let go");

    match.Picked().SelectOnly(worker);
    CHECK_MSG(!match.Picked().HasBuildingSelected(), "and back again");
}

void testTappingEmptyGroundClearsEverything() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.Picked().SelectOnly(spawnWorker(match, 3000.0f));
    CHECK_MSG(match.Picked().HasSelection(), "something is picked");

    match.Picked().SelectAt(glm::vec2(4500.0f, 700.0f));
    CHECK_MSG(!match.Picked().HasSelection(), "and a tap on nothing lets go");
}

// --- 4. Every change announces itself exactly once ------------------------

void testEverySelectionOperationAnnouncesExactlyOneChange() {
    // The contract the contextual bottom bar depends on. An operation that
    // announced twice would rebuild a panel the player is mid-tap on.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    int changes = 0;
    match.Bus().selectionChanged.Connect([&changes] { ++changes; });

    Unit* worker = spawnWorker(match, 3000.0f);

    match.Picked().SelectOnly(worker);
    CHECK_EQ(changes, 1);

    match.Picked().BoxSelect(glm::vec2(2900.0f, 700.0f), glm::vec2(3100.0f, 900.0f));
    CHECK_EQ(changes, 2);

    Building* hall = match.FindBuilding(Ids::kTownHall);
    if (hall != nullptr) {
        match.Picked().SelectBuilding(hall);
        CHECK_EQ(changes, 3);
    }

    match.Picked().Clear();
    CHECK_EQ(changes, 4);
}

// --- 5. A selection cannot outlive what it points at ----------------------

void testADeadUnitLeavesTheSelectionAndSaysSo() {
    // The oracle's own sequence: select, kill, and the selection is pruned.
    // Nothing dangles in this port - a corpse is a tombstone - which is exactly
    // why this matters: a dead unit left in the set would silently swallow
    // every order the player gave it, and the port would look like it was
    // ignoring them.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* worker = spawnWorker(match, 1000.0f);
    match.Picked().SelectOnly(worker);
    CHECK_MSG(match.Picked().HasSelection() && match.Picked().Units().size() == 1,
              "unit selected");

    int changes = 0;
    match.Bus().selectionChanged.Connect([&changes] { ++changes; });

    worker->Kill();

    CHECK_MSG(!match.Picked().HasSelection(), "selection pruned on unit_died");
    CHECK_MSG(changes == 1, "and the prune announced itself");

    // And the order path survives an empty selection, which is the harness's
    // "command after death does not error".
    match.Orders().MoveSelectedTo(glm::vec2(1400.0f, 800.0f));
    CHECK_MSG(true, "command after death does not error");
}

void testAFallenBuildingLeavesTheSelection() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the hall is there");
    if (hall == nullptr) return;

    match.Picked().SelectBuilding(hall);
    CHECK_MSG(match.Picked().HasBuildingSelected(), "the hall is picked");

    hall->Destroy();
    CHECK_MSG(!match.Picked().HasBuildingSelected(), "and its rubble is not");
}

// --- 6. Orders ------------------------------------------------------------

void testAMoveOrderSpreadsTheGroupAroundThePointRatherThanFromIt() {
    // `(i - (n-1)/2) * spacing`. Three units at a spacing of 46 land at -46, 0
    // and +46 around the point - so the middle one stands exactly where the
    // player tapped. A formation that started AT the point and grew rightward
    // would put the tap on the left flank, which reads as the order having
    // missed by half the group's width.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();
    CHECK_NEAR(match.WorldLayout().formationSpacing, 46.0f);

    Unit* a = spawnWorker(match, 1000.0f);
    Unit* b = spawnWorker(match, 1010.0f);
    Unit* c = spawnWorker(match, 1020.0f);

    match.Picked().BoxSelect(glm::vec2(900.0f, 700.0f), glm::vec2(1100.0f, 900.0f));
    CHECK_EQ(static_cast<int>(match.Picked().Units().size()), 3);

    glm::vec2 pinged(0.0f);
    int pings = 0;
    match.Bus().moveOrdered.Connect([&](const glm::vec2& at) {
        pinged = at;
        ++pings;
    });

    match.Orders().MoveSelectedTo(glm::vec2(3000.0f, 800.0f));

    CHECK_EQ(pings, 1);
    CHECK_NEAR(pinged.x, 3000.0f);

    // One step is enough to see which way each was sent.
    for (int i = 0; i < 3; ++i) match.Step(0.1);

    CHECK_MSG(a->CurrentState() == Unit::State::Moving, "and they are on their way");
    CHECK_MSG(a->Position().x < b->Position().x, "spread in order");
    CHECK_MSG(b->Position().x < c->Position().x, "along the lane");

    // CENTRED, which is the part the ordering above cannot see: a formation
    // growing rightward from the point is also in order, and also spread. Let
    // them arrive and the middle one must be standing where the player tapped,
    // with one either side of it.
    for (int i = 0; i < 400; ++i) match.Step(0.1);

    // Within the arrive threshold rather than exactly on it: a unit stops when
    // it is close enough, and "close enough" is a number the port already
    // carries. Asserting equality here would be asserting that a unit lands on
    // a float exactly, which it does not and should not.
    CHECK_MSG(std::fabs(b->Position().x - 3000.0f) <= Unit::kArriveThreshold,
              "the middle one stands where the player tapped");
    CHECK_MSG(a->Position().x < 3000.0f, "one to the left of the tap");
    CHECK_MSG(c->Position().x > 3000.0f, "and one to the right");
}

void testAnOrderToAnEmptySelectionPingsNothing() {
    // The ping is a confirmation, not a decoration: it fires only when a live
    // unit actually took the order, which is what tells a player their
    // selection is gone rather than their tap having missed.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    int pings = 0;
    match.Bus().moveOrdered.Connect([&pings](const glm::vec2&) { ++pings; });

    match.Orders().MoveSelectedTo(glm::vec2(3000.0f, 800.0f));
    CHECK_EQ(pings, 0);

    // AND WITH A SELECTION THAT IS NOT EMPTY BUT IS NOT ALIVE. An empty one
    // returns before the ping is even reached, so it cannot tell whether the
    // ping is guarded - only that the early return exists. A unit killed
    // BEFORE it is picked gets past the prune (which only watches units already
    // held) and leaves a selection of one corpse, which is what a player taps
    // on the frame their soldier falls.
    Unit* dying = spawnWorker(match, 1000.0f);
    dying->Kill();
    match.Picked().SelectOnly(dying);
    CHECK_MSG(match.Picked().Units().size() == 1, "a corpse can still be picked");

    match.Orders().MoveSelectedTo(glm::vec2(3000.0f, 800.0f));
    CHECK_MSG(pings == 0, "a ping confirms an order somebody took, not one that was given");
}

void testARightClickOnAnEnemyAttacksAndOnGroundMoves() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* soldier = match.SpawnUnit(
        UnitStats::FromJson(Ids::kSoldier, wb::Shipped().Unit(Ids::kSoldier)),
        glm::vec2(1000.0f, 800.0f));
    Unit* raider = spawnRaider(match, 2000.0f);

    match.Picked().SelectOnly(soldier);

    int attacks = 0;
    int moves = 0;
    match.Bus().attackOrdered.Connect([&attacks](const glm::vec2&) { ++attacks; });
    match.Bus().moveOrdered.Connect([&moves](const glm::vec2&) { ++moves; });

    // Straight at the raider's body.
    match.Orders().OnCommandAt(glm::vec2(2000.0f, 800.0f - 20.0f));
    CHECK_EQ(attacks, 1);
    CHECK_EQ(moves, 0);
    CHECK_MSG(soldier->AttackTarget() == raider, "the soldier was given the raider");
    CHECK_MSG(soldier->OrderedToAttack(), "and told to, rather than choosing to");

    // Empty ground is a move.
    match.Orders().OnCommandAt(glm::vec2(3500.0f, 800.0f));
    CHECK_EQ(moves, 1);
    CHECK_EQ(attacks, 1);
}

void testATapIsContextSensitiveInTheOriginalsOrder() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* hall = match.FindBuilding(Ids::kTownHall);
    Unit* worker = spawnWorker(match, 3000.0f);
    if (hall == nullptr) return;

    // A building selects it, whatever was in hand.
    match.Picked().SelectOnly(worker);
    match.Orders().ContextTap(glm::vec2(1500.0f, 720.0f));
    CHECK_MSG(match.Picked().SelectedBuilding() == hall, "a tap on a building selects it");

    // A friendly unit selects it.
    match.Orders().ContextTap(glm::vec2(3000.0f, 783.0f));
    CHECK_MSG(match.Picked().Units().size() == 1, "a tap on a friendly selects it");

    // With something in hand, empty ground is a MOVE rather than a clear.
    int moves = 0;
    match.Bus().moveOrdered.Connect([&moves](const glm::vec2&) { ++moves; });
    match.Orders().ContextTap(glm::vec2(4000.0f, 800.0f));
    CHECK_EQ(moves, 1);
    CHECK_MSG(match.Picked().HasSelection(), "and the selection is kept");

    // With nothing in hand, the same tap clears instead.
    match.Picked().Clear();
    match.Orders().ContextTap(glm::vec2(4200.0f, 800.0f));
    CHECK_EQ(moves, 1);
    CHECK_MSG(!match.Picked().HasSelection(), "an empty tap on empty ground is a let-go");
}

void testAnEnemyUnitIsPickedBeforeAnEnemyBuilding() {
    // A raider standing in front of its own wall is what the player meant to
    // hit. Units are scanned first, and only then buildings.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    BuildingStats enemyHall =
        BuildingStats::FromJson(Ids::kTownHall, wb::Shipped().Building(Ids::kTownHall));
    enemyHall.faction = Factions::kEnemy;
    match.PlaceBuilding(enemyHall, true, glm::vec2(2000.0f, 800.0f));

    Unit* raider = spawnRaider(match, 2000.0f);

    // A point inside both the wall's footprint and the raider's pick radius.
    Damageable* picked = match.Picked().EnemyAt(glm::vec2(2000.0f, 780.0f));
    CHECK_MSG(picked == static_cast<Damageable*>(raider), "the raider in front wins");

    // Away from the raider, the wall is what is left.
    Damageable* wall = match.Picked().EnemyAt(glm::vec2(2050.0f, 700.0f));
    CHECK_MSG(wall != nullptr && wall != static_cast<Damageable*>(raider),
              "and off to the side it is the wall");
}

void testTheEnemyPickIgnoresThePlayersOwnArmy() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    spawnWorker(match, 3000.0f);
    CHECK_MSG(match.Picked().EnemyAt(glm::vec2(3000.0f, 783.0f)) == nullptr,
              "a friendly is never a target for an attack order");
}

// --- 7. A boot lets go ----------------------------------------------------

void testBootingClearsWhateverWasSelected() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.Picked().SelectOnly(spawnWorker(match, 3000.0f));
    CHECK_MSG(match.Picked().HasSelection(), "something is picked");

    match.BootFresh();
    CHECK_MSG(!match.Picked().HasSelection(),
              "a selection cannot survive the board it pointed at");
}

} // namespace

static void runTests() {
    testTheMarqueeSelectsExactlyTheUnitsInsideIt();
    testTheMarqueesFarEdgesAreOutsideIt();
    testTheMarqueeMeasuresTheFeetAndNotTheBody();
    testAMarqueeTakesOnlyThePlayersOwnUnits();

    testAPointPickHitsTheBodyAndMissesTheSky();
    testThePickRadiusComesFromTheDataAndNotFromTheBody();
    testTheNearestUnitWinsAPointPick();

    testABuildingUnderThePointWinsOverAUnitStandingOnIt();
    testPickingOneKindClearsTheOther();
    testTappingEmptyGroundClearsEverything();

    testEverySelectionOperationAnnouncesExactlyOneChange();

    testADeadUnitLeavesTheSelectionAndSaysSo();
    testAFallenBuildingLeavesTheSelection();

    testAMoveOrderSpreadsTheGroupAroundThePointRatherThanFromIt();
    testAnOrderToAnEmptySelectionPingsNothing();
    testARightClickOnAnEnemyAttacksAndOnGroundMoves();
    testATapIsContextSensitiveInTheOriginalsOrder();
    testAnEnemyUnitIsPickedBeforeAnEnemyBuilding();
    testTheEnemyPickIgnoresThePlayersOwnArmy();

    testBootingClearsWhateverWasSelected();
}

TEST_MAIN("test_wb_selection", 60)
