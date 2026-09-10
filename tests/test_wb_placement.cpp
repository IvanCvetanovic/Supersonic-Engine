// The ported build placement, against two harnesses at once.
//
// `build_placement.gd` is the last piece of the game with an oracle behind it.
// Its numbers are weaker than the wave director's or the tone synthesiser's -
// they are decisions rather than computed output, and this suite says so rather
// than dressing them up - but the decisions are the kind that cost a player a
// building, and two of them are asserted nowhere else in the original.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_buildings.tscn
//   tools\godot.bat --path . res://tools/verify_input.tscn    # NEEDS A DISPLAY
//
// verify_buildings prints, at the game's 50741d1 (re-run 10 September 2026):
//
//   ok  : placement active after begin
//   ok  : overlapping a SAME-ROW building is INVALID
//   ok  : clear ground is VALID
//   ok  : same x on a far row is VALID (>= building_row_gap)
//   ok  : same x on a NEARBY row is INVALID (< gap)
//   ok  : cancel exits placement
//
// with a Town Hall at (2000, 800) and a Barracks ghost - a footprint of
// [1940, 2060] against a 110-wide body, so 2000 overlaps and 4000 does not -
// and a building_row_gap of 115, so a base row 150 below the hall's is clear
// and one 60 below it is not.
//
// verify_input adds the four that only it makes:
//
//   ok  : cancel placed no building (no cost spent)
//   ok  : LIVE match + placement -> Back cancels the ghost
//   ok  : ghost still up after the match ends
//   ok  : match OVER + placement -> Back EXITS on first press
//
// THAT HARNESS DOES NOT RUN HEADLESS - its header says so, and a headless run
// fails six of its assertions because the offscreen viewport does not route GUI
// mouse picking. Every one of those six is a REAL CLICK on a Control. What this
// suite reproduces is what survives without a mouse: the placement state the
// click was driving, and the pure predicate behind Android's Back button. The
// clicks themselves are not portable and are not claimed.
//
// WHAT THIS SLICE UNBLOCKS, which is the reason it was worth taking last:
// `GestureMachine::SetPlacementMode` has existed since the input slice with
// nothing in the port ever calling it from real state - a flag only a test ever
// set. Placement now emits the edge that drives it.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/BuildPlacement.hpp"
#include "sim/Match.hpp"

#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

// The shipped bodies these numbers come out of: a Town Hall is 120 wide and a
// Barracks 110, both anchored at the base centre.
constexpr float kHallHalf = 60.0f;
constexpr float kBarracksHalf = 55.0f;

int liveBuildings(const Match& match) {
    int count = 0;
    for (const auto& building : match.Buildings()) {
        if (building->IsAlive()) ++count;
    }
    return count;
}

int barracksCost() {
    const BuildingStats stats =
        BuildingStats::FromJson(Ids::kBarracks, wb::Shipped().Building(Ids::kBarracks));
    const auto it = stats.cost.find(Ids::kWood);
    return it == stats.cost.end() ? 0 : it->second;
}

// --- 1. The oracle's own four -------------------------------------------

void testPlacementOpensAndClosesAndKnowsWhatGroundIsClear() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    // The harness builds its own board rather than booting: a single Town Hall
    // at 2000, which is where its [1940, 2060] comes from.
    Match board(wb::Shipped(), profile, "");
    board.Run().Reset();
    Building* hall = board.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), board.Run(), profile, Ids::kTownHall), true,
        glm::vec2(2000.0f, 800.0f));
    CHECK_MSG(hall != nullptr, "the hall stands");
    if (hall == nullptr) return;

    // The footprint the harness names in its own comment.
    CHECK_NEAR(hall->Footprint().min.x, 2000.0f - kHallHalf);
    CHECK_NEAR(hall->Footprint().max.x, 2000.0f + kHallHalf);

    BuildPlacement& placement = board.Placement();
    placement.Begin(Ids::kBarracks);
    CHECK_MSG(placement.IsActive(), "placement active after begin");

    CHECK_MSG(!placement.IsValidAt(2000.0f, 800.0f), "overlapping a SAME-ROW building is INVALID");
    CHECK_MSG(placement.IsValidAt(4000.0f, 800.0f), "clear ground is VALID");
    CHECK_MSG(placement.IsValidAt(2000.0f, 950.0f),
              "same x on a far row is VALID (>= building_row_gap)");
    CHECK_MSG(!placement.IsValidAt(2000.0f, 860.0f), "same x on a NEARBY row is INVALID (< gap)");

    // The boundary, which the harness does not probe: exactly the gap apart is
    // clear, because the original's test is a strict `<`.
    CHECK_MSG(placement.IsValidAt(2000.0f, 915.0f), "exactly building_row_gap apart is clear");
    CHECK_MSG(!placement.IsValidAt(2000.0f, 914.0f), "and a pixel closer is not");

    placement.Cancel();
    CHECK_MSG(!placement.IsActive(), "cancel exits placement");
}

void testCancellingPlacesNothingAndSpendsNothing() {
    // verify_input's own assertion, and the only one in the twenty-two that
    // makes it. `cancel()` and `confirm()` are adjacent in the GDScript and
    // both end by clearing the active flag, which is exactly the shape a
    // copy-paste turns into a free building.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const int buildingsBefore = liveBuildings(match);
    const int woodBefore = match.Run().Amount(Ids::kWood);
    CHECK_EQ(buildingsBefore, 1);   // just the Town Hall

    match.Placement().Begin(Ids::kBarracks);
    CHECK_MSG(match.Placement().IsActive(), "placement active after begin");

    match.Placement().Cancel();

    CHECK_MSG(!match.Placement().IsActive(), "cancel exits placement");
    CHECK_MSG(liveBuildings(match) == buildingsBefore, "cancel placed no building");
    CHECK_MSG(match.Run().Amount(Ids::kWood) == woodBefore, "and spent nothing");
}

// --- 2. Confirming, in its three outcomes --------------------------------

void testConfirmingOnClearGroundBuildsASiteAndPaysForIt() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const int woodBefore = match.Run().Amount(Ids::kWood);

    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });

    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Confirm(glm::vec2(3000.0f, 800.0f));

    CHECK_MSG(!match.Placement().IsActive(), "a confirmed placement closes");
    CHECK_EQ(liveBuildings(match), 2);
    CHECK_MSG(match.Run().Amount(Ids::kWood) == woodBefore - barracksCost(),
              "the barracks was paid for exactly once");

    CHECK_MSG(placed != nullptr, "and announced");
    if (placed == nullptr) return;
    CHECK_NEAR(placed->Position().x, 3000.0f);
    CHECK_NEAR(placed->Position().y, 800.0f);

    // A SITE, not a building. Pre-placed would hand the player a finished
    // barracks for the price of an unfinished one.
    CHECK_MSG(!placed->IsComplete(), "it starts as a construction site");
    CHECK_MSG(placed->BuildProgress() == 0.0, "with nothing poured into it yet");
}

void testConfirmingOnAnOccupiedSpotChangesNothingAndStaysOpen() {
    // The branch that is one `return` away from being a cancel. A player who
    // taps a wall should be able to slide over and try again; closing placement
    // there costs them the trip back to the menu for a mistap.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const int woodBefore = match.Run().Amount(Ids::kWood);

    match.Placement().Begin(Ids::kBarracks);
    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the hall is there");
    if (hall == nullptr) return;
    match.Placement().Confirm(hall->Position());   // straight onto the hall

    CHECK_MSG(match.Placement().IsActive(), "an invalid spot leaves placement RUNNING");
    CHECK_EQ(liveBuildings(match), 1);
    CHECK_MSG(match.Run().Amount(Ids::kWood) == woodBefore, "and costs nothing");

    // And the player slides over and it works.
    match.Placement().Confirm(glm::vec2(3000.0f, 800.0f));
    CHECK_MSG(!match.Placement().IsActive(), "the second attempt lands");
    CHECK_EQ(liveBuildings(match), 2);
}

void testAPlacementThatBecameUnaffordableCancelsRatherThanBuilding() {
    // Between opening the menu and tapping the ground, a wave arrives and the
    // player spends elsewhere. Unreachable from shipped data in one step, so
    // the balance is emptied by hand.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.Placement().Begin(Ids::kBarracks);
    CHECK_MSG(match.Placement().IsActive(), "placing");

    // Drain the treasury under it.
    Cost everything;
    everything[Ids::kWood] = match.Run().Amount(Ids::kWood);
    CHECK_MSG(match.Run().TrySpend(everything), "the wood is gone");

    match.Placement().Confirm(glm::vec2(3000.0f, 800.0f));

    CHECK_MSG(!match.Placement().IsActive(), "an unaffordable confirm CANCELS");
    CHECK_EQ(liveBuildings(match), 1);
    CHECK_EQ(match.Run().Amount(Ids::kWood), 0);
}

// --- 3. Where the ghost may stand ----------------------------------------

void testTheGhostIsClampedByItsFootprintNotByTheWorldEdge() {
    // A different clamp from the one a TRAINED unit gets. A unit is a point on
    // the lane and is clamped to [0, width]; a building has a body, and half of
    // it hanging off the map is a building the player cannot see the edge of.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const float width = match.WorldLayout().width;
    match.Placement().Begin(Ids::kBarracks);

    match.Placement().Update(glm::vec2(-1000.0f, 800.0f));
    CHECK_NEAR(match.Placement().CandidateX(), kBarracksHalf);

    match.Placement().Update(glm::vec2(99999.0f, 800.0f));
    CHECK_NEAR(match.Placement().CandidateX(), width - kBarracksHalf);

    // And an ordinary point is left alone, on both axes.
    match.Placement().Update(glm::vec2(3000.0f, 800.0f));
    CHECK_NEAR(match.Placement().CandidateX(), 3000.0f);
    CHECK_NEAR(match.Placement().Candidate().y, 800.0f);
}

void testTheGhostsRowIsClampedOntoTheBand() {
    // The pointer's y is taken and clamped onto the walkable band, the rule a
    // move order follows: over the sky the ghost lands on the back row, over
    // the dirt on the front one. The band is world.json's ground_y and
    // lane.depth.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const float top = match.WorldLayout().groundY;
    const float depth = match.WorldLayout().laneDepth;
    CHECK_NEAR(top, 590.0f);
    CHECK_NEAR(depth, 280.0f);
    CHECK_NEAR(match.WorldLayout().buildingRowGap, 115.0f);

    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Update(glm::vec2(3000.0f, 120.0f));
    CHECK_NEAR(match.Placement().Candidate().y, top);
    match.Placement().Update(glm::vec2(3000.0f, 5000.0f));
    CHECK_NEAR(match.Placement().Candidate().y, top + depth);

    // And a confirm puts the site on the clamped row.
    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });
    match.Placement().Confirm(glm::vec2(3000.0f, 5000.0f));
    CHECK_MSG(placed != nullptr, "the site went up");
    if (placed == nullptr) return;
    CHECK_NEAR(placed->Position().y, top + depth);
}

void testBeginningPlacementPutsTheGhostInTheMiddleOfTheWorld() {
    // The original creates its ghost and immediately updates it at
    // `_world_width * 0.5`, so a confirm with no pointer movement at all lands
    // at the centre rather than at the origin.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.Placement().Begin(Ids::kBarracks);
    CHECK_NEAR(match.Placement().CandidateX(), match.WorldLayout().width * 0.5f);
    CHECK_NEAR(match.Placement().Candidate().y, match.WorldLayout().groundY);
    CHECK_MSG(match.Placement().IsValidHere(), "the middle of an empty map is clear");
}

void testEdgeToEdgeIsBuildableAndOverlapIsNot() {
    // Strict inequalities, the same boundary the footprint overlap test already
    // uses. Two buildings that touch are two buildings; one pixel of overlap is
    // a refusal.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the hall is there");
    if (hall == nullptr) return;

    const float right = hall->Footprint().max.x;

    match.Placement().Begin(Ids::kBarracks);
    const float row = hall->Position().y;
    CHECK_MSG(match.Placement().IsValidAt(right + kBarracksHalf, row), "edge to edge is buildable");
    CHECK_MSG(!match.Placement().IsValidAt(right + kBarracksHalf - 1.0f, row),
              "and one pixel into it is not");
}

void testRubbleDoesNotReserveTheGroundItFellOn() {
    // Godot frees a destroyed building, so it leaves the group and stops being
    // asked about. Nothing is freed here - the port keeps dead entities as
    // tombstones on purpose - so the question has to be asked directly. Without
    // it, a fallen barracks would hold that stretch of lane for the rest of the
    // run and the player would have no idea why.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* barracks = match.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks), true,
        glm::vec2(3000.0f, 800.0f));

    match.Placement().Begin(Ids::kBarracks);
    CHECK_MSG(!match.Placement().IsValidAt(3000.0f, 800.0f), "a standing barracks blocks the spot");

    barracks->Destroy();
    CHECK_MSG(match.Placement().IsValidAt(3000.0f, 800.0f), "its rubble does not");
}

void testAnEnemyBuildingIsNotInTheWay() {
    // The original scans the PLAYER buildings group only. The shipped game has
    // no enemy buildings at all, so this needs a fixture - and it is the kind
    // of thing that only shows up the day somebody adds one.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    BuildingStats enemyHall =
        BuildingStats::FromJson(Ids::kTownHall, wb::Shipped().Building(Ids::kTownHall));
    enemyHall.faction = Factions::kEnemy;
    match.PlaceBuilding(enemyHall, true, glm::vec2(3000.0f, 800.0f));

    match.Placement().Begin(Ids::kBarracks);
    CHECK_MSG(match.Placement().IsValidAt(3000.0f, 800.0f),
              "placement only asks about the player's own buildings");
}

// --- 4. Who gets sent ----------------------------------------------------

void testAnIdleWorkerIsPreferredOverACloserBusyOne() {
    // The preference is the point. Pulling a loaded worker off a delivery to
    // walk across the map is a worse answer than waking one that is standing
    // still, even when the idle one is much further away.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    // Let the three starting workers find work and stop being idle.
    for (int i = 0; i < 20; ++i) match.Step(0.1);

    // Workers only: the boot's hero is never Idle either - he is Controlled -
    // and he is not someone placement could send.
    int busyNearby = 0;
    for (const auto& unit : match.Units()) {
        if (unit->Stats().id != Ids::kWorker) continue;
        if (unit->CurrentState() != Unit::State::Idle) ++busyNearby;
    }
    CHECK_MSG(busyNearby == 3, "the starting workers are all busy");

    // One idle worker, a very long way off.
    Unit* idle = match.SpawnUnit(
        UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker)),
        glm::vec2(5000.0f, 800.0f));
    CHECK_MSG(idle->CurrentState() == Unit::State::Idle, "and it is idle");

    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });

    // Right beside the busy ones.
    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Confirm(glm::vec2(1900.0f, 800.0f));

    CHECK_MSG(placed != nullptr, "the site went up");
    if (placed == nullptr) return;
    CHECK_MSG(idle->BuildTarget() == placed,
              "the distant IDLE worker was sent, not the nearby busy ones");
}

void testWithNoIdleWorkerTheNearestOneOfAnyKindGoes() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    for (int i = 0; i < 20; ++i) match.Step(0.1);

    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });

    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Confirm(glm::vec2(1900.0f, 800.0f));
    CHECK_MSG(placed != nullptr, "the site went up");
    if (placed == nullptr) return;

    int sent = 0;
    for (const auto& unit : match.Units()) {
        if (unit->BuildTarget() == placed) ++sent;
    }
    CHECK_MSG(sent == 1, "exactly one worker was sent");
}

void testACorpseIsNotSentToBuild() {
    // In Godot a dead worker is freed and leaves the units group, so the
    // question never reaches it. Nothing is freed here, so it has to be asked -
    // and a site "assigned" to a corpse is a site that never gets built, with
    // nothing on screen to explain why.
    // THE IDLE PREFERENCE HIDES THIS, which is why the board has to be driven
    // first. A corpse is in state Dead, never Idle, so while any living worker
    // is standing around the preference picks it and a port with no liveness
    // check looks correct. The bug is only reachable once every worker is busy
    // and the nearest-of-any-kind pass is the one that decides - which is
    // exactly the moment a player is most likely to be placing a building.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    for (int i = 0; i < 20; ++i) match.Step(0.1);
    for (const auto& unit : match.Units()) {
        CHECK_MSG(unit->CurrentState() != Unit::State::Idle, "every worker has found work");
    }

    // The three starting workers stand at 1680 / 1740 / 1800. Kill the one
    // nearest the site so a port that ignores liveness would pick it.
    Unit* nearest = nullptr;
    for (const auto& unit : match.Units()) {
        if (nearest == nullptr || unit->Position().x > nearest->Position().x) {
            nearest = unit.get();
        }
    }
    CHECK_MSG(nearest != nullptr, "there are workers");
    if (nearest == nullptr) return;
    nearest->Kill();
    const glm::vec2 corpseAt = nearest->Position();

    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });

    // Right on top of the corpse, so distance cannot be the reason it loses.
    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Confirm(corpseAt);
    CHECK_MSG(placed != nullptr, "the site went up");
    if (placed == nullptr) return;

    CHECK_MSG(nearest->BuildTarget() == nullptr, "the corpse was not sent");

    int sentAndAlive = 0;
    for (const auto& unit : match.Units()) {
        if (unit->BuildTarget() == placed) {
            ++sentAndAlive;
            CHECK_MSG(unit->IsAlive(), "whoever was sent is alive");
        }
    }
    CHECK_MSG(sentAndAlive == 1, "and a living worker went instead");
}

void testAnEnemyWorkerWillNotBuildForThePlayer() {
    // The original scans the PLAYER units group. Every enemy the shipped game
    // has is an aggressor, so the behaviour filter already excludes them and
    // the faction check looks redundant - it is reachable only by authoring an
    // enemy that works for a living, which is exactly the kind of branch this
    // port keeps finding is untested.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    UnitStats thrall = UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker));
    thrall.faction = Factions::kEnemy;
    CHECK_MSG(thrall.behavior == Unit::kWorker, "it works like a worker");

    // Standing right on the site, so distance cannot be the reason it loses.
    Unit* enemyWorker = match.SpawnUnit(thrall, glm::vec2(3000.0f, 800.0f));

    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });

    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Confirm(glm::vec2(3000.0f, 800.0f));
    CHECK_MSG(placed != nullptr, "the site went up");
    if (placed == nullptr) return;

    CHECK_MSG(enemyWorker->BuildTarget() == nullptr,
              "an enemy worker is not the player's to command");

    int sent = 0;
    for (const auto& unit : match.Units()) {
        if (unit->BuildTarget() == placed) ++sent;
    }
    CHECK_MSG(sent == 1, "one of the player's own went instead");
}

void testABoardWithNoWorkersLeavesTheSiteStanding() {
    // A real state late in a bad run, and not an error: the site waits until
    // somebody is free, which the worker slice's anti-deadlock rule then
    // handles.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();
    match.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kTownHall), true,
        glm::vec2(1500.0f, 800.0f));

    Building* placed = nullptr;
    match.Bus().buildingPlaced.Connect([&placed](Building* b) { placed = b; });

    match.Placement().Begin(Ids::kBarracks);
    match.Placement().Confirm(glm::vec2(3000.0f, 800.0f));

    CHECK_MSG(placed != nullptr, "the site still goes up with nobody to build it");
    CHECK_EQ(liveBuildings(match), 2);
}

// --- 5. The edge the gesture machine has been waiting for ----------------

void testPlacementAnnouncesEveryChangeAndNoChangeTwice() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    std::vector<bool> changes;
    match.Bus().placementActiveChanged.Connect([&changes](bool active) {
        changes.push_back(active);
    });

    match.Placement().Cancel();
    CHECK_MSG(changes.empty(), "cancelling nothing announces nothing");

    match.Placement().Begin(Ids::kBarracks);
    CHECK_EQ(static_cast<int>(changes.size()), 1);
    if (changes.size() != 1) return;
    CHECK_MSG(changes[0], "opening announces true");

    // Beginning again is a SWAP, so it closes and reopens rather than staying
    // silent - which is what lets an input controller that only listens for
    // edges keep up with a change of building.
    match.Placement().Begin(Ids::kTower);
    CHECK_EQ(static_cast<int>(changes.size()), 3);

    // The guard is not decoration. CHECK_EQ reports and CONTINUES, so a
    // mutation that drops the swap's first edge leaves two entries here and the
    // reads below run off the end - which under MSVC's debug std::vector is a
    // blocking assertion dialog rather than a failure. The mutation that
    // deletes the swap hung this suite for two minutes before this line
    // existed.
    if (changes.size() != 3) return;
    CHECK_MSG(!changes[1], "the swap closes the first");
    CHECK_MSG(changes[2], "and opens the second");

    match.Placement().Cancel();
    CHECK_EQ(static_cast<int>(changes.size()), 4);
    if (changes.size() != 4) return;
    CHECK_MSG(!changes[3], "closing announces false");
}

void testAnIdNobodyAuthoredNeverOpensPlacementAtAll() {
    // A stale button in a UI would otherwise put the player into a placement
    // mode for a building that can never be built, with no way out but a cancel
    // they do not know they need.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    int changes = 0;
    match.Bus().placementActiveChanged.Connect([&changes](bool) { ++changes; });

    match.Placement().Begin("catapult");
    CHECK_MSG(!match.Placement().IsActive(), "an unknown id is a no-op");
    CHECK_MSG(changes == 0, "and announces nothing");
}

// --- 6. The Back gate ----------------------------------------------------

void testBackCancelsAGhostOnlyWhileTheMatchIsStillLive() {
    // The truth table verify_input asserts, and the regression it exists for.
    // Victory or defeat can land while a ghost is still up; that ghost then
    // sits behind the game-over overlay where nobody can see it, so Back has to
    // EXIT on the first press rather than spend one cancelling something
    // invisible.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    CHECK_MSG(!match.BackCancelsPlacement(), "live match, no ghost -> Back exits");

    match.Placement().Begin(Ids::kTower);
    CHECK_MSG(match.BackCancelsPlacement(), "LIVE match + placement -> Back cancels the ghost");

    match.Run().Lose();

    CHECK_MSG(match.Placement().IsActive(), "ghost still up after the match ends");
    CHECK_MSG(!match.BackCancelsPlacement(),
              "match OVER + placement -> Back EXITS on the first press");
}

// --- 7. A ghost does not survive a boot ----------------------------------

void testBootingClosesAnOpenPlacement() {
    // Godot gets this from the scene going away. A Match is reused, and a
    // placement left open across a boot would be priced against a treasury that
    // no longer exists.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.Placement().Begin(Ids::kBarracks);
    CHECK_MSG(match.Placement().IsActive(), "placing");

    match.BootFresh();
    CHECK_MSG(!match.Placement().IsActive(), "a boot closes it");
}

} // namespace

static void runTests() {
    testPlacementOpensAndClosesAndKnowsWhatGroundIsClear();
    testCancellingPlacesNothingAndSpendsNothing();

    testConfirmingOnClearGroundBuildsASiteAndPaysForIt();
    testConfirmingOnAnOccupiedSpotChangesNothingAndStaysOpen();
    testAPlacementThatBecameUnaffordableCancelsRatherThanBuilding();

    testTheGhostIsClampedByItsFootprintNotByTheWorldEdge();
    testTheGhostsRowIsClampedOntoTheBand();
    testBeginningPlacementPutsTheGhostInTheMiddleOfTheWorld();
    testEdgeToEdgeIsBuildableAndOverlapIsNot();
    testRubbleDoesNotReserveTheGroundItFellOn();
    testAnEnemyBuildingIsNotInTheWay();

    testAnIdleWorkerIsPreferredOverACloserBusyOne();
    testWithNoIdleWorkerTheNearestOneOfAnyKindGoes();
    testACorpseIsNotSentToBuild();
    testAnEnemyWorkerWillNotBuildForThePlayer();
    testABoardWithNoWorkersLeavesTheSiteStanding();

    testPlacementAnnouncesEveryChangeAndNoChangeTwice();
    testAnIdNobodyAuthoredNeverOpensPlacementAtAll();

    testBackCancelsAGhostOnlyWhileTheMatchIsStillLive();
    testBootingClosesAnOpenPlacement();
}

TEST_MAIN("test_wb_placement", 65)
