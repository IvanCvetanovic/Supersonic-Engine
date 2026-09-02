// A grid of integer movement costs, and the flow field that solves it.
//
// Every failure here is a unit walking somewhere sensible-looking and wrong.
// A field that cuts a corner sends it through the seam between two buildings.
// A tie broken differently on two machines sends it left of an obstacle here
// and right of it there, which is a desync that shows up minutes later as two
// armies in different places. A goal seeded on a wall floods a field out of a
// cell nothing can stand in, and every unit in the level walks confidently
// into it. None of those looks like a bug in the pathfinder.
//
// The roadmap that asked for this asked for "a deliberately-broken replay test
// first", and the first case below is that: a grid IS simulation state, so the
// oracle has to see a wall move before anything else here is worth having.
// The whole point of the exercise is that a hash which cannot notice reports
// SUCCESS while blind, which is the failure this engine has shipped twice.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "core/NavGrid.hpp"
#include "core/NavSystem.hpp"
#include "core/TransformSystem.hpp"

#include <string>
#include <vector>

using namespace Supersonic;

namespace {

using Cell = NavGrid::Cell;

NavGrid open(uint32_t width, uint32_t height) {
    NavGrid grid;
    grid.Resize(width, height);
    return grid;
}

std::string picture(const NavGrid& grid) {
    std::string out = "\n";
    for (uint32_t y = 0; y < grid.Height(); ++y) {
        for (uint32_t x = 0; x < grid.Width(); ++x) {
            if (grid.Blocked(x, y)) { out += '#'; continue; }
            const uint32_t d = grid.DistanceAt(x, y);
            if (d == NavGrid::kUnreachable) { out += '.'; continue; }
            if (d == 0) { out += 'G'; continue; }
            // The numeric keypad, so every one of the eight directions has a
            // character of its own: 8 is up, 2 down, 4 left, 6 right, and the
            // corners are 7, 9, 1, 3. Arrows were the first version and two
            // pairs of them collided - a backslash meant both up-left and
            // down-right - so a pinned picture could have missed a flow that
            // had reversed along a diagonal.
            const glm::ivec2 flow = grid.FlowAt(x, y);
            const char kKeypad[3][3] = {{'7', '8', '9'}, {'4', 'o', '6'}, {'1', '2', '3'}};
            out += kKeypad[flow.y + 1][flow.x + 1];
        }
        out += '\n';
    }
    return out;
}

// A straight-line walk down the flow from a cell, for asking whether the field
// actually leads anywhere. Stops on a goal, on a cell with no flow, or after a
// bound - a field with a cycle in it would otherwise hang the suite.
uint32_t walk(const NavGrid& grid, uint32_t x, uint32_t y, uint32_t limit = 4096) {
    uint32_t steps = 0;
    while (steps < limit) {
        if (grid.DistanceAt(x, y) == 0) return steps;
        const glm::ivec2 flow = grid.FlowAt(x, y);
        if (flow.x == 0 && flow.y == 0) return limit;   // stuck, not arrived
        x = static_cast<uint32_t>(static_cast<int32_t>(x) + flow.x);
        y = static_cast<uint32_t>(static_cast<int32_t>(y) + flow.y);
        ++steps;
    }
    return limit;
}

} // namespace

// --- the oracle, first -----------------------------------------------------

static void testTheHashSeesAWallMoveAndACostChange() {
    // THE TEST THE ROADMAP ASKED FOR FIRST, and the reason it is first. A grid
    // is state the moment a tick writes it: a wall knocked down on one tick is
    // read by every unit on the next. A hash that could not see that would let
    // a replay diverge and report success, which is not a weaker test than no
    // hash at all - it is a worse one, because it is believed.
    NavGrid a = open(8, 8);
    NavGrid b = open(8, 8);
    CHECK_MSG(a.Hash() == b.Hash(), "two identical grids agree");
    CHECK_MSG(a.Hash() != 0, "and a grid never hashes to the sentinel");

    b.SetCost(3, 3, NavGrid::kBlocked);
    CHECK_MSG(a.Hash() != b.Hash(), "a wall built where there was none is a different world");

    b.SetCost(3, 3, NavGrid::kNormal);
    CHECK_MSG(a.Hash() == b.Hash(), "and knocked down again is the old one");

    b.SetCost(5, 1, 9);
    CHECK_MSG(a.Hash() != b.Hash(), "so is mud where there was road");

    NavGrid wider = open(9, 8);
    CHECK_MSG(a.Hash() != wider.Hash(), "and so is a grid of another size");
}

static void testTheHashSeesTheGoalsAndWhetherTheFieldStillMatchesThem() {
    NavGrid a = open(6, 6);
    NavGrid b = open(6, 6);
    a.Build({ Cell{0, 0} });
    b.Build({ Cell{5, 5} });
    CHECK_MSG(a.Hash() != b.Hash(), "where the army is heading is state");

    // A WALL BUILT BEFORE THE SEARCH AND ONE BUILT AFTER IT. Identical costs,
    // identical goals, and different answers from every unit standing on the
    // grid - one field routes around the wall and the other has never heard of
    // it. The first version of this hash covered only the costs and the goals
    // and called those two worlds the same, which is the exact shape of the
    // failure this suite opens by naming: an oracle that reports success while
    // blind.
    NavGrid searchedThenWalled = open(6, 6);
    searchedThenWalled.Build({ Cell{0, 0} });
    searchedThenWalled.SetCost(2, 2, NavGrid::kBlocked);

    NavGrid walledThenSearched = open(6, 6);
    walledThenSearched.SetCost(2, 2, NavGrid::kBlocked);
    walledThenSearched.Build({ Cell{0, 0} });

    CHECK_MSG(searchedThenWalled.Hash() != walledThenSearched.Hash(),
              "a stale field and a current one over the same walls are different worlds");
    CHECK_MSG(!searchedThenWalled.FieldIsCurrent() && walledThenSearched.FieldIsCurrent(),
              "and that is the difference between them");

    // Rebuilt, they agree again - so the hash is reading the staleness rather
    // than merely counting how many times Build was called.
    searchedThenWalled.Build({ Cell{0, 0} });
    CHECK_MSG(searchedThenWalled.Hash() == walledThenSearched.Hash(),
              "the same walls searched from the same goal is one world, however it got there");
}

// --- the grid itself -------------------------------------------------------

static void testAFreshGridIsOpenGroundAndOutsideIsWall() {
    const NavGrid grid = open(4, 3);
    CHECK_EQ(grid.Width(), 4u);
    CHECK_EQ(grid.Height(), 3u);
    CHECK_EQ(grid.CostAt(0, 0), NavGrid::kNormal);
    CHECK_MSG(!grid.Blocked(3, 2), "the far corner is inside");
    CHECK_MSG(grid.Blocked(4, 0), "one past the width is outside, and outside is wall");
    CHECK_MSG(grid.Blocked(0, 3), "and so is one past the height");
    CHECK_MSG(grid.DistanceAt(0, 0) == NavGrid::kUnreachable,
              "with no field built, nothing has a distance");
    CHECK_MSG(!grid.FieldIsCurrent(), "and it says so");
}

static void testResizeRefusesWhatItCannotHoldAndKeepsWhatItHad() {
    // Sized FIRST, so "a refusal leaves the grid alone" is a claim with
    // something to lose. Asserted against a default-constructed grid it read
    // zero against zero and would have passed a Resize that cleared the grid
    // before checking its arguments.
    NavGrid grid;
    CHECK(grid.Resize(6, 5));
    grid.SetCost(3, 2, NavGrid::kBlocked);

    CHECK_MSG(!grid.Resize(0, 4), "a zero side is not a grid");
    CHECK_MSG(!grid.Resize(4, 0), "either way round");
    CHECK_MSG(!grid.Resize(2048, 2048), "four million cells is past the cap");

    CHECK_MSG(grid.Width() == 6 && grid.Height() == 5,
              "a refused resize leaves the grid the size it was");
    CHECK_MSG(grid.Blocked(3, 2), "with the level still in it");

    CHECK_MSG(grid.Resize(1024, 1024), "exactly the cap is allowed");
    CHECK_MSG(!grid.Blocked(3, 2), "and a resize that succeeds is a fresh grid");
}

static void testWritingACostStalesTheField() {
    NavGrid grid = open(4, 4);
    grid.Build({ Cell{0, 0} });
    CHECK(grid.FieldIsCurrent());

    grid.SetCost(2, 2, NavGrid::kBlocked);
    CHECK_MSG(!grid.FieldIsCurrent(),
              "a field that routes around a wall that has since moved is not a field");

    grid.Build({ Cell{0, 0} });
    CHECK(grid.FieldIsCurrent());
    grid.Fill(NavGrid::kNormal);
    CHECK_MSG(!grid.FieldIsCurrent(), "and a fill is a write like any other");
}

// --- the field -------------------------------------------------------------

static void testDistancesAreTenPerStepAndFourteenPerDiagonal() {
    NavGrid grid = open(5, 5);
    CHECK_EQ(grid.Build({ Cell{0, 0} }), 25u);

    CHECK_EQ(grid.DistanceAt(0, 0), 0u);
    CHECK_MSG(grid.DistanceAt(1, 0) == 10u, "one step across");
    CHECK_MSG(grid.DistanceAt(0, 1) == 10u, "one step down");
    CHECK_MSG(grid.DistanceAt(1, 1) == 14u, "one diagonal, not twenty");
    CHECK_MSG(grid.DistanceAt(2, 1) == 24u, "a diagonal and a straight");
    CHECK_MSG(grid.DistanceAt(4, 4) == 56u, "four diagonals");
}

static void testACostlyCellIsWalkedAroundRatherThanThrough() {
    // The whole point of a cost that is not one, and the grid has to leave a
    // way round for the claim to mean anything: a patch of mud that spans the
    // map cannot be avoided, so a test built on one asserts only the price of
    // entering it. This mud blocks the middle row and leaves the top and
    // bottom open.
    NavGrid grid = open(5, 3);
    grid.SetCost(2, 1, 20);
    grid.Build({ Cell{0, 1} });

    // Standing IN the mud costs what the mud costs - ten for the step from
    // the cell beside the goal, times twenty for the ground.
    CHECK_MSG(grid.DistanceAt(2, 1) == 10u + 20u * 10u,
              "entering mud costs its own multiple of the step");

    // THE PROPERTY THE NAME CLAIMS is about the far side, not about the mud:
    // (3,1) is reached for thirty-eight by stepping over the clear row above,
    // rather than for two hundred and twenty by ploughing straight through.
    // The first version of this test walled the mud across the whole map, so
    // there was no way round and nothing to prefer.
    CHECK_MSG(grid.DistanceAt(3, 1) == 14u + 10u + 14u,
              "the cheapest way to the far side goes around the mud, not through it:" +
                  picture(grid));
    CHECK_MSG(grid.DistanceAt(3, 1) < grid.DistanceAt(2, 1),
              "which is nearer than the mud itself, and that is what going around means");

    // A unit that finds itself in the mud still walks home the cheapest way,
    // which here is straight out along its own row - the cell to its left is
    // ordinary ground ten from the goal, and no diagonal beats that. Written
    // as "the flow leads somewhere strictly nearer" rather than as a
    // direction, because the direction was a guess and the guess was wrong.
    const glm::ivec2 outOfTheMud = grid.FlowAt(2, 1);
    CHECK_MSG(outOfTheMud != glm::ivec2(0, 0), "the mud is not a dead end");
    CHECK_MSG(grid.DistanceAt(2 + outOfTheMud.x, 1 + outOfTheMud.y) < grid.DistanceAt(2, 1),
              "and it leads out rather than deeper in:" + picture(grid));

    // Raise the price of going around and the mud becomes the way through -
    // which is the property a cost is for, and the direction that proves the
    // field is comparing rather than avoiding.
    NavGrid pricey = open(5, 3);
    pricey.SetCost(2, 1, 20);
    for (uint32_t x = 0; x < 5; ++x) {
        pricey.SetCost(x, 0, NavGrid::kBlocked);
        pricey.SetCost(x, 2, NavGrid::kBlocked);
    }
    pricey.Build({ Cell{0, 1} });
    CHECK_MSG(pricey.DistanceAt(2, 1) == 20u * 10u + 10u,
              "with the ways round walled off, the mud is entered and costs what it costs");
}

static void testAWallIsRoutedAroundAndTheFlowAgreesWithTheField() {
    // A wall across the middle with a gap at the bottom. Every reachable cell
    // must flow to the goal, and the only way there is through the gap.
    NavGrid grid = open(7, 5);
    for (uint32_t y = 0; y < 4; ++y) grid.SetCost(3, y, NavGrid::kBlocked);
    CHECK(grid.Build({ Cell{0, 0} }) > 0);

    CHECK_MSG(grid.DistanceAt(3, 0) == NavGrid::kUnreachable, "a wall has no distance");
    CHECK_MSG(grid.DistanceAt(6, 0) != NavGrid::kUnreachable,
              "the far side is reachable through the gap: " + picture(grid));

    for (uint32_t y = 0; y < 5; ++y) {
        for (uint32_t x = 0; x < 7; ++x) {
            if (grid.Blocked(x, y)) continue;
            CHECK_MSG(walk(grid, x, y) < 4096u,
                      "every open cell flows home from (" + std::to_string(x) + ", " +
                          std::to_string(y) + "):" + picture(grid));
        }
    }
}

static void testAWalledOffRegionIsUnreachableRatherThanFar() {
    NavGrid grid = open(5, 5);
    for (uint32_t y = 0; y < 5; ++y) grid.SetCost(2, y, NavGrid::kBlocked);
    const uint32_t reached = grid.Build({ Cell{0, 0} });

    CHECK_MSG(reached == 10u, "only the near half, and the wall is not part of it");
    CHECK_MSG(grid.DistanceAt(4, 4) == NavGrid::kUnreachable,
              "cut off is a different answer from a long way away - a unit can act on it");
    CHECK_MSG(grid.FlowAt(4, 4) == glm::ivec2(0, 0), "and there is nowhere to walk");
}

static void testNoDiagonalThroughTheSeamBetweenTwoWalls() {
    // THE CORNER RULE. Without it the field steps from (0,0) to (1,1) between
    // two blocked cells that touch at their corner - through solid geometry,
    // diagonally, which is exactly what it looks like in a game.
    NavGrid grid = open(3, 3);
    grid.SetCost(1, 0, NavGrid::kBlocked);
    grid.SetCost(0, 1, NavGrid::kBlocked);
    grid.Build({ Cell{0, 0} });

    CHECK_MSG(grid.DistanceAt(1, 1) != 14u,
              "the diagonal between two walls is not a step:" + picture(grid));
    CHECK_MSG(grid.DistanceAt(1, 1) == NavGrid::kUnreachable,
              "and with both its ways in blocked, that corner is sealed off");

    // THE RULE IS THE STRICT ONE: a diagonal is refused when EITHER of the two
    // cells it passes between is blocked, not only when both are. A unit has
    // width, and the one that clips a single wall's corner on the diagonal is
    // walking through the wall by half its own body.
    //
    // The cost of choosing this is a slightly longer path around every corner
    // in the level, which is the direction to be wrong in - the loose rule
    // buys fourteen units of distance and sells a unit walking through a
    // building.
    NavGrid oneWall = open(3, 3);
    oneWall.SetCost(1, 0, NavGrid::kBlocked);
    oneWall.Build({ Cell{0, 0} });
    CHECK_MSG(oneWall.DistanceAt(1, 1) == 20u,
              "one wall still refuses the diagonal past its corner - two straight steps, "
              "not one diagonal:" + picture(oneWall));

    // And with nothing in the way at all, the diagonal is a diagonal.
    NavGrid clear = open(3, 3);
    clear.SetCost(2, 2, NavGrid::kBlocked);   // a wall that touches neither orthogonal
    clear.Build({ Cell{0, 0} });
    CHECK_MSG(clear.DistanceAt(1, 1) == 14u,
              "an open corner costs fourteen:" + picture(clear));
}

static void testAGoalOnAWallIsRefusedRatherThanFloodedFrom() {
    NavGrid grid = open(4, 4);
    grid.SetCost(2, 2, NavGrid::kBlocked);
    const uint32_t reached = grid.Build({ Cell{2, 2} });

    CHECK_MSG(reached == 0u, "a wall is not a destination");
    CHECK_MSG(grid.DistanceAt(0, 0) == NavGrid::kUnreachable,
              "so nothing flows into it, which is what every unit would have done");
}

static void testAGoalOutsideTheGridIsIgnoredAndTheRestStillWork() {
    NavGrid grid = open(4, 4);
    const uint32_t reached = grid.Build({ Cell{99, 99}, Cell{1, 1} });
    CHECK_EQ(reached, 16u);
    CHECK_EQ(grid.DistanceAt(1, 1), 0u);
}

static void testSeveralGoalsAreOneSearchToTheNearest() {
    // The reason a flow field is worth having: "the nearest of these" costs
    // exactly one search, and every cell knows which one that is without ever
    // naming it.
    NavGrid grid = open(9, 1);
    CHECK_EQ(grid.Build({ Cell{0, 0}, Cell{8, 0} }), 9u);

    CHECK_EQ(grid.DistanceAt(0, 0), 0u);
    CHECK_EQ(grid.DistanceAt(8, 0), 0u);
    CHECK_MSG(grid.DistanceAt(4, 0) == 40u, "the middle is four steps from either end");
    CHECK_MSG(grid.FlowAt(1, 0) == glm::ivec2(-1, 0), "and near the left end you walk left");
    CHECK_MSG(grid.FlowAt(7, 0) == glm::ivec2(1, 0), "and near the right end, right");
}

static void testATieIsBrokenByTheNeighbourOrderAndTheOrderIsFixed() {
    // TWO NEIGHBOURS, EXACTLY THE SAME DISTANCE AWAY. On a uniform grid that
    // is the ordinary case rather than the exception, and something has to
    // decide - so the neighbour order decides, and it is written down as part
    // of the answer rather than left as a detail of the loop.
    //
    // The first version of this test asserted the property against a cell
    // whose best neighbour was strictly closer than the rest, so there was no
    // tie to break and reversing the whole order changed nothing it looked at.
    // A test of a tie-break has to contain a tie.

    // A straight step and a diagonal one, both onto a goal. Straight comes
    // first, so a unit crossing open ground walks the line rather than a
    // staircase to a point directly beside it.
    NavGrid straightVsDiagonal = open(3, 3);
    straightVsDiagonal.Build({ Cell{1, 0}, Cell{0, 0} });
    CHECK_MSG(straightVsDiagonal.DistanceAt(1, 0) == 0u && straightVsDiagonal.DistanceAt(0, 0) == 0u,
              "both are goals, so both neighbours of (1,1) are at distance zero");
    CHECK_MSG(straightVsDiagonal.FlowAt(1, 1) == glm::ivec2(0, -1),
              "the straight step wins the tie, not the diagonal:" + picture(straightVsDiagonal));

    // Two straight steps, opposite ways, both onto a goal. Up is listed before
    // down, so up wins - which is arbitrary and is the point: it is the SAME
    // arbitrary answer on every machine, which is what a replay needs.
    NavGrid upVsDown = open(3, 3);
    upVsDown.Build({ Cell{1, 0}, Cell{1, 2} });
    CHECK_MSG(upVsDown.FlowAt(1, 1) == glm::ivec2(0, -1),
              "an exact tie falls the same way every time:" + picture(upVsDown));

    // And left before right, for the same reason.
    NavGrid leftVsRight = open(3, 3);
    leftVsRight.Build({ Cell{0, 1}, Cell{2, 1} });
    CHECK_MSG(leftVsRight.FlowAt(1, 1) == glm::ivec2(-1, 0),
              "and so does the other axis:" + picture(leftVsRight));

    // UP AGAINST LEFT, which is the pair the three cases above cannot see.
    // Each of them ties two neighbours that face each other, so swapping the
    // first two entries of the table - up and left - reordered nothing any of
    // them looked at, and a mutation run said the order was untested. A tie
    // has to be between the two entries whose order is in question.
    NavGrid upVsLeft = open(3, 3);
    upVsLeft.Build({ Cell{1, 0}, Cell{0, 1} });
    CHECK_MSG(upVsLeft.FlowAt(1, 1) == glm::ivec2(0, -1),
              "up is listed before left, so up wins:" + picture(upVsLeft));

    // And the diagonals against each other, for the same reason.
    NavGrid diagonals = open(3, 3);
    diagonals.Build({ Cell{0, 0}, Cell{2, 0} });
    CHECK_MSG(diagonals.FlowAt(1, 1) == glm::ivec2(-1, -1),
              "up-left is listed before up-right:" + picture(diagonals));
}

static void testTheWholeFieldIsPinnedSoAnyChangeOfAnswerIsVisible() {
    // THE FIELD ITSELF, WRITTEN DOWN. The first version of this ran the same
    // build twice in one process and compared the two - which is true of any
    // deterministic function of its arguments, so it would have passed with
    // any queue at all and could not have failed. Determinism across runs is
    // not a thing a single process can observe; what it CAN observe is that
    // the answer is still the answer it was, and that is what a pinned picture
    // is for.
    //
    // A wall with a gap, a patch of mud, two goals. Any change to the queue,
    // the neighbour order, the step costs or the corner rule moves at least
    // one of these characters.
    NavGrid grid = open(9, 5);
    for (uint32_t y = 0; y < 4; ++y) grid.SetCost(4, y, NavGrid::kBlocked);
    grid.SetCost(6, 2, 5);
    grid.Build({ Cell{0, 0}, Cell{8, 4} });

    // Read before it was pinned, rather than guessed and then pasted:
    //
    //   the left half walks to the goal at (0,0) and the right half to the one
    //   at (8,4), and the two meet at (4,4) - the only cell the wall leaves
    //   open - which picks the right-hand goal because forty is less than the
    //   fifty-six that four diagonals to the left one would cost;
    //
    //   the mud at (6,2) is visibly gone round. (5,1) steps EAST rather than
    //   south-east, and (5,2) steps south-east rather than east, both of which
    //   are detours of exactly one cell around the expensive one. That is the
    //   cost doing its job, and it is the character in this picture most
    //   likely to move if the arithmetic ever changes.
    const std::string expected =
        "\n"
        "G444#3332\n"
        "8777#6332\n"
        "8777#3332\n"
        "8777#3332\n"
        "87776666G\n";

    CHECK_MSG(picture(grid) == expected,
              "the field is not the one that was pinned. If this is a deliberate change, "
              "read the new picture and paste it in - but a change here is a change to "
              "where every unit in a game walks, so read it rather than pasting it.\n"
              "got:" + picture(grid) + "expected:" + expected);
}

static void testEveryDistanceIsConsistentWithItsNeighbours() {
    // The invariant Dijkstra is supposed to leave behind, checked over an
    // awkward grid rather than argued: no open cell can be reached more
    // cheaply than the field says it can.
    NavGrid grid = open(13, 11);
    for (uint32_t y = 2; y < 9; ++y) grid.SetCost(6, y, NavGrid::kBlocked);
    for (uint32_t x = 2; x < 6; ++x) grid.SetCost(x, 5, NavGrid::kBlocked);
    grid.SetCost(9, 3, 7);
    grid.SetCost(9, 4, 7);
    grid.Build({ Cell{0, 0} });

    bool consistent = true;
    for (uint32_t y = 0; y < 11; ++y) {
        for (uint32_t x = 0; x < 13; ++x) {
            const uint32_t here = grid.DistanceAt(x, y);
            if (here == NavGrid::kUnreachable) continue;

            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int64_t nx = static_cast<int64_t>(x) + dx;
                    const int64_t ny = static_cast<int64_t>(y) + dy;
                    if (nx < 0 || ny < 0) continue;
                    const auto ux = static_cast<uint32_t>(nx);
                    const auto uy = static_cast<uint32_t>(ny);
                    if (grid.Blocked(ux, uy)) continue;
                    if (dx != 0 && dy != 0) {
                        if (grid.Blocked(static_cast<uint32_t>(static_cast<int64_t>(x) + dx), y)) continue;
                        if (grid.Blocked(x, static_cast<uint32_t>(static_cast<int64_t>(y) + dy))) continue;
                    }

                    const uint32_t step = (dx != 0 && dy != 0) ? NavGrid::kDiagonalStep
                                                               : NavGrid::kStraightStep;
                    const uint32_t viaHere = here + step * grid.CostAt(ux, uy);
                    if (grid.DistanceAt(ux, uy) > viaHere) consistent = false;
                }
            }
        }
    }
    CHECK_MSG(consistent, "no cell has a cheaper route than the one the field found");
}

// --- where the grid is in the world ----------------------------------------

static void testCellCentresAreHalfACellInFromTheCorner() {
    NavBounds bounds;
    bounds.origin = glm::vec3(10.0f, 0.0f, 20.0f);
    bounds.cellSize = 2.0f;

    const glm::vec3 first = bounds.CellCenter(0, 0);
    CHECK_NEAR(first.x, 11.0f);
    CHECK_NEAR(first.z, 21.0f);
    CHECK_MSG(first.y == 0.0f, "the ground plane is where the origin put it");

    const glm::vec3 next = bounds.CellCenter(1, 0);
    CHECK_MSG(next.x - first.x == 2.0f, "one cell across is one cell size");
}

static void testTheTwoPlanesDisagreeAboutWhichWayIsForward() {
    // The 3D lane walks on XZ; the 2D lane is authored in XY with rows running
    // DOWN, the way a tilemap is. A grid that assumed one of them would sit
    // edge-on to the world of the other, which is not a subtle failure but is
    // an easy one to write.
    NavBounds ground;
    ground.plane = NavBounds::Plane::XZ;
    NavBounds flat;
    flat.plane = NavBounds::Plane::XY;

    const glm::vec3 groundCell = ground.CellCenter(0, 3);
    const glm::vec3 flatCell = flat.CellCenter(0, 3);
    CHECK_MSG(groundCell.z == 3.5f && groundCell.y == 0.0f, "rows run along +Z on the ground");
    CHECK_MSG(flatCell.y == -3.5f && flatCell.z == 0.0f, "and DOWN -Y in a flat scene");
}

static void testCellAtIsTheInverseOfCellCentreAndRefusesTheOutside() {
    NavBounds bounds;
    bounds.origin = glm::vec3(-5.0f, 0.0f, -5.0f);
    bounds.cellSize = 0.5f;

    uint32_t x = 99, y = 99;
    CHECK(bounds.CellAt(bounds.CellCenter(7, 3), 10, 10, x, y));
    CHECK_EQ(x, 7u);
    CHECK_EQ(y, 3u);

    CHECK_MSG(bounds.CellAt(glm::vec3(-5.0f, 0.0f, -5.0f), 10, 10, x, y),
              "the outer corner is inside the first cell");
    CHECK_MSG(x == 0 && y == 0, "and it is cell zero");

    CHECK_MSG(!bounds.CellAt(glm::vec3(-5.01f, 0.0f, -5.0f), 10, 10, x, y),
              "a hair outside is outside, not cell four billion");
    CHECK_MSG(!bounds.CellAt(glm::vec3(0.0f, 0.0f, -5.0f), 10, 10, x, y),
              "and so is past the far edge");
}

// --- stamping the world into it ---------------------------------------------

static void testABoxColliderBlocksTheCellsItCovers() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(5.0f, 0.0f, 5.0f);
    registry.emplace<BoxColliderComponent>(entity).size = glm::vec3(2.0f, 2.0f, 2.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    NavBounds bounds;
    bounds.cellSize = 1.0f;
    NavGrid grid = open(10, 10);

    const uint32_t blocked = NavSystem::StampColliders(registry, bounds, grid);
    CHECK_MSG(blocked > 0, "a two-metre crate on a one-metre grid blocks something");
    CHECK_MSG(grid.Blocked(4, 4) && grid.Blocked(5, 5),
              "the cells whose centres are under it");
    CHECK_MSG(!grid.Blocked(0, 0), "and nothing else");
    CHECK_MSG(!grid.FieldIsCurrent(), "and the field is stale, because the world moved");

    // Additive and idempotent: stamping twice blocks nothing new, so a caller
    // can compose several passes without counting the same wall twice.
    CHECK_EQ(NavSystem::StampColliders(registry, bounds, grid), 0u);
}

static void testATriggerIsWalkedIntoRatherThanAround() {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(5.0f, 0.0f, 5.0f);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(4.0f);
    box.isTrigger = true;
    TransformSystem::UpdateWorldTransforms(registry);

    NavBounds bounds;
    NavGrid grid = open(10, 10);
    CHECK_MSG(NavSystem::StampColliders(registry, bounds, grid) == 0u,
              "a trigger volume exists to be walked into");
}

static void testACapsuleIsAUnitAndDoesNotBlockTheGround() {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(5.0f, 0.0f, 5.0f);
    registry.emplace<CapsuleColliderComponent>(entity);
    TransformSystem::UpdateWorldTransforms(registry);

    NavBounds bounds;
    NavGrid grid = open(10, 10);
    CHECK_MSG(NavSystem::StampColliders(registry, bounds, grid) == 0u,
              "a capsule is a character, and walling the ground under one traps its neighbours");
}

static void testAColliderOutsideTheGridStampsNothing() {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(-100.0f, 0.0f, -100.0f);
    registry.emplace<BoxColliderComponent>(entity).size = glm::vec3(2.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    NavBounds bounds;
    NavGrid grid = open(10, 10);
    CHECK_EQ(NavSystem::StampColliders(registry, bounds, grid), 0u);

    bool anyBlocked = false;
    for (uint32_t y = 0; y < 10; ++y) {
        for (uint32_t x = 0; x < 10; ++x) {
            if (grid.Blocked(x, y)) anyBlocked = true;
        }
    }
    CHECK_MSG(!anyBlocked, "a negative coordinate must not wrap into the grid");
}

static void testAColliderStraddlingTheOriginIsClampedRatherThanWrapped() {
    // THE ONLY INPUT THAT REACHES THE CLAMP. The test above places its crate
    // entirely outside the grid, so the range check refuses it before the
    // clamp is ever consulted - the guard it was named for was uncovered. A
    // collider that hangs over the origin is the case where a cell index is
    // computed from a negative coordinate, and where flooring it and casting
    // to unsigned would give four billion.
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(0.0f, 0.0f, 0.0f);
    registry.emplace<BoxColliderComponent>(entity).size = glm::vec3(4.0f, 4.0f, 4.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    NavBounds bounds;
    bounds.cellSize = 1.0f;
    NavGrid grid = open(10, 10);

    const uint32_t blocked = NavSystem::StampColliders(registry, bounds, grid);
    CHECK_MSG(blocked > 0, "the quarter of the crate that is over the grid blocks cells");
    CHECK_MSG(grid.Blocked(0, 0) && grid.Blocked(1, 1),
              "the cells near the corner it covers");
    CHECK_MSG(!grid.Blocked(3, 3), "and not the ones past its reach");

    uint32_t count = 0;
    for (uint32_t y = 0; y < 10; ++y) {
        for (uint32_t x = 0; x < 10; ++x) {
            if (grid.Blocked(x, y)) ++count;
        }
    }
    CHECK_MSG(count == blocked && count < 100,
              "a negative index clamped to zero, not wrapped past the far edge");
}

static void testStampingAnUnsizedGridIsRefusedRatherThanReadOffItsEnd() {
    // A Resize that was refused leaves a grid at zero by zero, and a caller
    // that ignored the return value would hand one to the stamp. Width() - 1
    // is unsigned, so the clamp that is meant to bound the walk to the grid
    // would bound it to the collider instead - which is a read off the end of
    // an empty vector.
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(1.0f, 0.0f, 1.0f);
    registry.emplace<BoxColliderComponent>(entity).size = glm::vec3(2.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    NavGrid unsized;
    CHECK_MSG(!unsized.Resize(4096, 4096), "refused, so the grid is still nothing");
    CHECK_EQ(NavSystem::StampColliders(registry, NavBounds{}, unsized), 0u);
}

static void testAScaledColliderBlocksMoreThanAnUnscaledOne() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(5.0f, 0.0f, 5.0f);
    registry.emplace<BoxColliderComponent>(entity).size = glm::vec3(1.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    NavBounds bounds;
    NavGrid small = open(10, 10);
    const uint32_t before = NavSystem::StampColliders(registry, bounds, small);

    transform.scale = glm::vec3(4.0f);
    TransformSystem::UpdateWorldTransforms(registry);
    NavGrid large = open(10, 10);
    const uint32_t after = NavSystem::StampColliders(registry, bounds, large);

    CHECK_MSG(after > before,
              "the world matrix decides the footprint, so scaling a crate up blocks more ground");
}

int main() {
    testTheHashSeesAWallMoveAndACostChange();
    testTheHashSeesTheGoalsAndWhetherTheFieldStillMatchesThem();

    testAFreshGridIsOpenGroundAndOutsideIsWall();
    testResizeRefusesWhatItCannotHoldAndKeepsWhatItHad();
    testWritingACostStalesTheField();

    testDistancesAreTenPerStepAndFourteenPerDiagonal();
    testACostlyCellIsWalkedAroundRatherThanThrough();
    testAWallIsRoutedAroundAndTheFlowAgreesWithTheField();
    testAWalledOffRegionIsUnreachableRatherThanFar();
    testNoDiagonalThroughTheSeamBetweenTwoWalls();
    testAGoalOnAWallIsRefusedRatherThanFloodedFrom();
    testAGoalOutsideTheGridIsIgnoredAndTheRestStillWork();
    testSeveralGoalsAreOneSearchToTheNearest();
    testATieIsBrokenByTheNeighbourOrderAndTheOrderIsFixed();
    testTheWholeFieldIsPinnedSoAnyChangeOfAnswerIsVisible();
    testEveryDistanceIsConsistentWithItsNeighbours();

    testCellCentresAreHalfACellInFromTheCorner();
    testTheTwoPlanesDisagreeAboutWhichWayIsForward();
    testCellAtIsTheInverseOfCellCentreAndRefusesTheOutside();

    testABoxColliderBlocksTheCellsItCovers();
    testATriggerIsWalkedIntoRatherThanAround();
    testACapsuleIsAUnitAndDoesNotBlockTheGround();
    testAColliderOutsideTheGridStampsNothing();
    testAColliderStraddlingTheOriginIsClampedRatherThanWrapped();
    testStampingAnUnsizedGridIsRefusedRatherThanReadOffItsEnd();
    testAScaledColliderBlocksMoreThanAnUnscaledOne();

    return test::summary("test_nav", 80);
}
