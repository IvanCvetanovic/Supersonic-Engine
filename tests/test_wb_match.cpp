// The ported match boot, against the whole-match numbers no earlier slice could
// reach.
//
// Every suite before this one built its board by hand, because the port had no
// `main.gd` and therefore nothing that owned an entity. `test_wb_snapshot` says
// so in its own header: "Its specific numbers - 270 wood, 963 hit points, six
// units - come from booting the whole match through `main.gd`. The port has no
// match boot, so this suite builds its own board and asserts the same
// PROPERTIES against numbers it sets itself."
//
// This is the match boot, and these are those numbers.
//
// To re-derive the original's side:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_snapshot.tscn
//
// It printed, on 27 August 2026:
//
//   ok  : captured some units (6)
//   ok  : capture->restore->capture is equivalent
//   ok  : wood restored to saved balance (270)
//   ok  : GATHERING units relinked their tree (3/3)
//   ok  : Town Hall damaged hp restored exactly (963)
//   ok  : WaveDirector elapsed restored (~16.0 vs 16.0)
//   ok  : enemy units restored (1 live == 1 saved)
//   ok  : WaveDirector recounted alive enemies (1)
//   (drove to: 6 units, 3 gathering)
//
// THE LEDGER BEHIND THOSE NUMBERS, because a number nobody can explain is a
// number nobody can defend when it changes:
//
//   270 wood  = economy.json's 300, times hard's starting_resources_mult of
//               0.8, is 240 - plus three workers each banking one full carry
//               of ten inside sixteen seconds. The workers spawn at 1680 /
//               1740 / 1800 and the nearest tree to all three is the one at
//               1900; ten seconds of gathering at 1.0/sec, then a walk back to
//               the Town Hall at 1500 at 140 px/sec, and all three are back on
//               the tree by t=16. Which is also the 3 gathering.
//   6 units   = three starting workers, one hand-placed raider, and TWO
//               SOLDIERS TRAINED OUT OF THE BARRACKS QUEUE. The soldiers are
//               the load-bearing pair: they exist only if a building finishing
//               a unit reaches a spawn, which is the edge this slice adds and
//               nothing before it had.
//   963 hp    = the Town Hall's authored 1000, less the 37 the harness deals.
//               Difficulty scales enemies only, so hard does not touch it.
//
// THOSE WERE THE NUMBERS BEFORE THE RESTRUCTURE. At the game's 50741d1 the
// same harness prints 8 units and 220 wood, and 3 or 4 gathering depending
// on the run. A probe of its board (the resync plan, slice 10) gives the
// ledger: the boot adds the hero, possessed at 1860; the Town Hall
// auto-trains a worker for 50 on step 3, born at 1600 on step 27; and three
// deliveries of ten bank 30. So 240 - 50 + 30 = 220, and 8 is four workers,
// the hero, the raider and the two soldiers. The gathering count moves
// because the original deals every spawn a random row and the port does
// not, so the port asserts the range the oracle can hold.
//
// ELAPSED IS NOT 16.0, and the tolerance is not decoration. Eighty additions of
// the double nearest 0.2 come to 15.999999999999975, and the harness itself
// compares elapsed within 0.5 for exactly this reason. Both sides do the same
// additions in the same order, so the residue is identical rather than lucky -
// which is why the exact value is ALSO asserted, as a determinism pin.
//
// The soldiers arrive on steps 40 and 80. Forty additions of 0.2 reach
// 8.000000000000004, four parts in a quadrillion over the 8.0 train time; the
// margin is real, it is on the right side, and it is the same margin in Godot.
//
// Two things this suite deliberately does NOT do.
//
// It does not migrate the nine existing suites onto a Match. That looks like a
// free cleanup and is not: `Town::NearestEnemyBuilding` answers null
// unconditionally even though it owns buildings, and `Battlefield`'s deposits
// are bare floats whose DepositExists is a bounds check with no liveness. A
// real Match answers both for real, so three suites would silently gain
// behaviour they have never had. That is a migration with known deltas, and it
// is its own slice.
//
// And it does not claim `verify_autosave`. Six of that harness's seven
// assertions are window and OS plumbing with no analogue in a static library.
// The seventh - a finished run must not be re-saved - is pure simulation, has
// never had a call site in this port, and is asserted here.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Match.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace WolfBrigade;
using Supersonic::Json::Value;

namespace {

// --- Scaffolding ----------------------------------------------------------

// A run-file path under the system temp directory, gone when the test is.
//
// Port suites get no WORKING_DIRECTORY from CTest, so a relative path lands
// wherever ctest happened to be invoked from - which is a file in somebody's
// build tree, and a test that passes because it wrote one.
class TempRunPath {
public:
    explicit TempRunPath(const std::string& tag) {
        m_path = std::filesystem::temp_directory_path() / ("wb_match_" + tag + ".json");
        std::error_code ignored;
        std::filesystem::remove(m_path, ignored);
    }
    ~TempRunPath() {
        std::error_code ignored;
        std::filesystem::remove(m_path, ignored);
    }
    TempRunPath(const TempRunPath&) = delete;
    TempRunPath& operator=(const TempRunPath&) = delete;

    std::string Path() const { return m_path.string(); }
    bool Exists() const { return std::filesystem::exists(m_path); }

private:
    std::filesystem::path m_path;
};

int countInState(const Match& match, Unit::State state) {
    int found = 0;
    for (const auto& unit : match.Units()) {
        if (unit->CurrentState() == state) ++found;
    }
    return found;
}

// The enriched board `verify_snapshot` drives, built the way that harness
// builds it: a completed Barracks at 2300 with two soldiers queued, and a
// raider at 4200 spawned through the match's own spawn path.
//
// The raider's stats come STRAIGHT off the data row, not through Upgrades and
// not difficulty-scaled, because that is what the harness does - it calls
// UnitStats.from_dict on the raw dictionary. A raider scaled by hard would have
// 1.3x the hit points and none of the asserted numbers would move, which is
// exactly why it is worth being faithful rather than approximately right.
Building* enrichLikeTheHarness(Match& match) {
    Building* barracks =
        match.PlaceBuilding(Upgrades::ForBuilding(wb::Shipped(), match.Run(),
                                                 match.PlayerProfile(), Ids::kBarracks),
                            true, glm::vec2(2300.0f, match.WorldLayout().groundY));
    barracks->EnqueueTraining(Ids::kSoldier);
    barracks->EnqueueTraining(Ids::kSoldier);

    match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                    glm::vec2(4200.0f, match.WorldLayout().groundY));
    return barracks;
}

// The harness's own driving order: units, then buildings, then the director,
// and projectiles never. Reproduced rather than improved on, because the
// numbers were recorded under it.
//
// `0.2` is a double literal. Written `0.2f` it would widen to
// 0.20000000298023224 and this would be a different simulation.
void driveLikeTheHarness(Match& match, int steps) {
    for (int i = 0; i < steps; ++i) {
        match.StepUnits(0.2);
        match.StepBuildings(0.2);
        match.StepDirector(0.2);
    }
}

// --- The full-fidelity digest ---------------------------------------------
//
// The same shape `test_wb_snapshot` uses, and for the same reason: only `sid`
// and the three reference fields renumber between a capture and a re-capture,
// so everything else is compared EXACTLY and the references are compared by
// translating each id to its referent's own record. That is what catches
// "relinked to the wrong entity of the same type".

std::string recordKey(const Value& record);

std::string translateRef(const Value& snapshot, int sid) {
    if (sid < 0) return "-1";
    for (const char* section : {"units", "buildings", "resource_nodes"}) {
        for (const Value& other : snapshot[section].AsArray()) {
            if (static_cast<int>(other["sid"].AsNumber(-1.0)) == sid) return recordKey(other);
        }
    }
    return "?";
}

std::string recordKey(const Value& record) {
    Value copy = record;
    copy.Set("sid", Value());
    for (const char* key : {"ref_tree", "ref_build", "ref_attack", "ref_heal"}) copy.Set(key, Value());
    return Snapshot::ToText(copy);
}

std::string digest(const Value& snapshot) {
    std::string out;
    for (const char* section : {"units", "buildings", "resource_nodes"}) {
        std::vector<std::string> rows;
        for (const Value& record : snapshot[section].AsArray()) {
            std::string row = recordKey(record);
            for (const char* key : {"ref_tree", "ref_build", "ref_attack", "ref_heal"}) {
                if (!record.Has(key)) continue;
                row += "|" + std::string(key) + "=" +
                       translateRef(snapshot, static_cast<int>(record[key].AsNumber(-1.0)));
            }
            rows.push_back(std::move(row));
        }
        std::sort(rows.begin(), rows.end());

        out += std::string(section) + ":\n";
        for (const std::string& row : rows) out += "  " + row + "\n";
    }
    out += "wave_director:" + Snapshot::ToText(snapshot["wave_director"]) + "\n";
    out += "game_state:" + Snapshot::ToText(snapshot["game_state"]) + "\n";
    return out;
}

// =========================================================================
// The oracle
// =========================================================================

static void testTheWholeMatchNumbersTheOraclePrints() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");

    // Set BEFORE the boot, which is what makes 240 rather than 300 the opening
    // balance - and is also the reason a boot is a call rather than something
    // the constructor did.
    match.Run().SetDifficulty("hard");
    match.Run().SetLevel("level_1");
    match.BootFresh();

    CHECK_MSG(match.Units().size() == 4, "a fresh board is three workers and the hero");
    CHECK_EQ(match.Run().Amount(Ids::kWood), 240);

    // Where each new worker is born. The hall auto-trains one, and where it
    // appears is the building's spawn point rather than anything the drive
    // does to it afterwards.
    std::vector<float> workerBirths;
    match.Bus().unitSpawned.Connect([&workerBirths](Unit* unit) {
        if (unit != nullptr && unit->Stats().id == Ids::kWorker) {
            workerBirths.push_back(unit->Position().x);
        }
    });

    enrichLikeTheHarness(match);
    driveLikeTheHarness(match, 80);

    // The four the harness prints.
    CHECK_MSG(match.Run().Amount(Ids::kWood) == 220,
              "240 on hard, less the 50 the hall's auto-trained worker cost, plus three "
              "deliveries of ten");
    CHECK_MSG(match.Units().size() == 8,
              "four workers, the hero, one raider, and two soldiers the barracks finished");
    CHECK_EQ(match.Director().AliveEnemies(), 1);

    // 3 or 4, and deliberately not one of them. The original deals every spawn
    // a random row, so its workers walk different distances run to run; seven
    // runs of HEAD's board gave 4 four times and 3 three times. The port does
    // not draw, so its answer is fixed - but it is pinned only to what the
    // oracle can hold steady.
    const int gathering = countInState(match, Unit::State::Gathering);
    CHECK_MSG(gathering >= 3 && gathering <= 4, "gathering is one of the oracle's two answers");

    // The ledger by id, which is what makes the 8 a count of the right things.
    int workers = 0;
    int heroes = 0;
    int soldiers = 0;
    int raiders = 0;
    for (const auto& unit : match.Units()) {
        const std::string& id = unit->Stats().id;
        workers += id == Ids::kWorker;
        heroes += id == Ids::kHero;
        soldiers += id == Ids::kSoldier;
        raiders += id == Ids::kRaider;
    }
    CHECK_EQ(workers, 4);
    CHECK_EQ(heroes, 1);
    CHECK_EQ(soldiers, 2);
    CHECK_EQ(raiders, 1);

    // The auto-trained worker, where the probe saw it born: 1500 plus half the
    // hall's 120 plus its spawn offset of 40. One, not two - the probe's hall
    // trained exactly one in the sixteen seconds.
    CHECK_EQ(static_cast<int>(workerBirths.size()), 1);
    if (!workerBirths.empty()) CHECK_NEAR(workerBirths[0], 1600.0f);

    // Elapsed, both ways: within the harness's own tolerance, and exactly, so a
    // change in how the director accumulates is a failure rather than a drift.
    CHECK_MSG(std::fabs(match.Director().Elapsed() - 16.0) < 0.5,
              "elapsed within the 0.5 the harness itself allows");
    CHECK_MSG(match.Director().Elapsed() == 15.999999999999975,
              "and exactly the residue eighty additions of 0.2 leave");

    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the Town Hall is on the board");
    if (hall == nullptr) return;
    CHECK_EQ(hall->Hp(), 1000);
    hall->TakeDamage(37);
    CHECK_EQ(hall->Hp(), 963);
}

// The two soldiers are the only units on that board produced by the code this
// slice ports, so they are asserted where they are BORN rather than where they
// end up. Soldier one has eight seconds left to run after it appears and a
// raider walking toward it; its final position is a fact about combat, not
// about the boot.
static void testATrainedUnitIsBornWhereTheBuildingPutsIt() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    std::vector<glm::vec2> born;
    match.Bus().unitSpawned.Connect([&born](Unit* unit) {
        if (unit != nullptr && unit->Stats().id == Ids::kSoldier) born.push_back(unit->Position());
    });

    Building* barracks =
        match.PlaceBuilding(Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile,
                                                 Ids::kBarracks),
                            true, glm::vec2(2300.0f, 800.0f));
    barracks->EnqueueTraining(Ids::kSoldier);

    // Thirty-nine steps of 0.2 is 7.8 seconds against a train time of 8.0, and
    // the wait is asserted rather than skipped past. Without it, a building
    // that never learned its training times would finish the soldier on the
    // FIRST step - Building::StepTraining floors an unknown time at a
    // hundredth of a second - and every assertion below would still hold.
    for (int i = 0; i < 39; ++i) match.StepBuildings(0.2);
    CHECK_MSG(born.empty(), "nothing is trained before its train time");

    match.StepBuildings(0.2);

    CHECK_MSG(born.size() == 1, "one soldier, once");
    if (born.empty()) return;

    // 2300, plus half the barracks' 110-wide body, plus its authored spawn
    // offset of 40.
    CHECK_NEAR(born[0].x, 2395.0f);
    CHECK_NEAR(born[0].y, 590.0f);   // forced to the ground line
}

static void testCaptureRestoreCaptureIsEquivalentThroughARealBoot() {
    Profile profile;
    Match first(wb::Shipped(), profile, "");
    first.Run().SetDifficulty("hard");
    first.Run().SetLevel("level_1");
    first.BootFresh();
    enrichLikeTheHarness(first);
    driveLikeTheHarness(first, 80);

    Building* hall = first.FindBuilding(Ids::kTownHall);
    if (hall != nullptr) hall->TakeDamage(37);

    const Value captured = first.Capture();
    const std::string text = Snapshot::ToText(captured);

    // Through TEXT, as the harness does, so a writer that rounds a double is a
    // failure here rather than a difference nobody sees.
    const Value reparsed = Snapshot::FromText(text);
    CHECK_MSG(Snapshot::IsValid(reparsed), "the document survives a trip through text");

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    CHECK_MSG(second.BootFromSave(reparsed), "and restores");

    const Value recaptured = second.Capture();

    const std::string a = digest(captured);
    const std::string b = digest(recaptured);
    CHECK_MSG(a == b, "capture -> restore -> capture is equivalent");
    if (a != b) std::printf("--- first ---\n%s--- second ---\n%s", a.c_str(), b.c_str());

    // And the ground truth, off the LIVE restored board rather than off the
    // documents - which is how the original's own harness says the building
    // cooldown gap first hid.
    CHECK_EQ(second.Run().Amount(Ids::kWood), 220);
    CHECK_MSG(second.Run().Level() == "level_1", "the level came back");
    CHECK(second.Run().CurrentDifficulty() == "hard");
    CHECK_EQ(static_cast<int>(second.Units().size()), 8);
    CHECK_EQ(second.Director().AliveEnemies(), 1);

    Building* restoredHall = second.FindBuilding(Ids::kTownHall);
    CHECK_MSG(restoredHall != nullptr, "the Town Hall came back");
    if (restoredHall != nullptr) CHECK_EQ(restoredHall->Hp(), 963);

    // Every gathering worker is pointed at a real tree again, which is the
    // harness's 3/3 or 4/4 - see the oracle test for why it is either.
    int gathering = 0;
    int relinked = 0;
    for (const auto& unit : second.Units()) {
        if (unit->CurrentState() != Unit::State::Gathering) continue;
        ++gathering;
        if (unit->TargetNode() != nullptr) ++relinked;
    }
    CHECK_MSG(gathering >= 3 && gathering <= 4, "gathering is one of the oracle's two answers");
    CHECK_EQ(relinked, gathering);

    // The restore hands the hero back: saved as an ordinary unit, possessed
    // again on the way in, and with no respawn pending.
    Unit* hero = second.Control().Hero();
    CHECK_MSG(hero != nullptr && hero->Stats().id == Ids::kHero, "the restored hero is possessed");
    CHECK_MSG(hero != nullptr && hero->IsControlled(), "and under the player's hand");
    CHECK_MSG(!second.RespawnPending() && !second.Run().HeroDown(), "and nobody is waiting to respawn");
}

// =========================================================================
// The board a fresh boot builds
// =========================================================================

static void testAFreshBootBuildsTheBoardTheDataDescribes() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    // One building: the Town Hall, already standing, already banking.
    CHECK_EQ(static_cast<int>(match.Buildings().size()), 1);
    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "and it is the Town Hall");
    if (hall == nullptr) return;
    CHECK_NEAR(hall->Position().x, 1500.0f);
    // `_building_y`'s row, 590 + 280 x 0.18, where the probe of HEAD's board
    // found it.
    CHECK_NEAR(hall->Position().y, 640.4f);
    CHECK_MSG(hall->IsComplete(), "pre-placed means finished, not a building site");
    CHECK_MSG(hall->IsDepositPoint(), "and a place to bank wood");

    // Twenty-two nodes: fifteen trees at 200 and seven berry bushes at 120.
    // On the ground line, where the original draws each a random row - the
    // row policy in the resync plan's slice 10.
    CHECK_EQ(static_cast<int>(match.Nodes().size()), 22);
    int wood = 0;
    int food = 0;
    for (const auto& node : match.Nodes()) {
        if (node->resource == Ids::kWood) {
            ++wood;
            CHECK_EQ(node->amount, 200);
            CHECK_NEAR(node->bodySize.x, 44.0f);
        } else if (node->resource == Ids::kFood) {
            ++food;
            CHECK_EQ(node->amount, 120);
            CHECK_NEAR(node->bodySize.y, 50.0f);
        }
        CHECK_NEAR(node->position.y, 590.0f);
    }
    CHECK_EQ(wood, 15);
    CHECK_EQ(food, 7);

    // The colours come through, which nothing else would notice: the original's
    // own equivalence digest throws them away, and a node rebuilt without one
    // looks like a default tree rather than like the node that was authored.
    CHECK(match.Nodes()[0]->color == "#3f6b34");

    // Three workers, spaced, on the ground.
    CHECK_EQ(static_cast<int>(match.Units().size()), 4);
    for (int i = 0; i < 3; ++i) {
        CHECK_NEAR(match.Units()[static_cast<size_t>(i)]->Position().x,
                   1680.0f + 60.0f * static_cast<float>(i));
        CHECK_NEAR(match.Units()[static_cast<size_t>(i)]->Position().y, 590.0f);
        CHECK(match.Units()[static_cast<size_t>(i)]->Stats().id == Ids::kWorker);
    }

    // And the hero, a hundred and eighty ahead of the worker line and already
    // in the player's hand - verify_respawn A, "boot possesses the hero
    // automatically".
    const Unit* hero = match.Units()[3].get();
    CHECK(hero->Stats().id == Ids::kHero);
    CHECK_NEAR(hero->Position().x, 1860.0f);
    CHECK_NEAR(hero->Position().y, 590.0f);
    CHECK_MSG(match.Control().Hero() == hero && hero->IsControlled(),
              "boot possesses the hero automatically");
    CHECK_EQ(match.LaneIndex().CountOf(Factions::kPlayer), 4);
}

static void testAFreshBootBanksTheArmoryBonusItWasBoughtWith() {
    // GameState::Reset adds the persistent starting bonus and reads it out of
    // its own copy of the meta levels - and until this slice the ONLY thing
    // that ever wrote those levels was the restore path. A boot that forgets to
    // push them in gives the Armory upgrade to a Continue and silently not to a
    // New Game, which is the one direction a player would never report as a
    // bug: they would simply never see the thing they paid for.
    const Supersonic::Json::Value& upgrades = wb::Shipped().MetaUpgrades();

    std::string resourceUpgrade;
    std::string resource;
    int perLevel = 0;
    for (const auto& [id, row] : upgrades.AsObject()) {
        // "starting_resources" is a pseudo-entity inside `effects`: an upgrade
        // that grants a resource at the start of a run targets it the same way
        // one that toughens a soldier targets "soldier".
        const Value& starting = row["effects"]["starting_resources"];
        if (!starting.IsObject()) continue;
        for (const auto& [key, amount] : starting.AsObject()) {
            resourceUpgrade = id;
            resource = key;
            perLevel = static_cast<int>(amount.AsNumber(0.0));
        }
        if (!resourceUpgrade.empty()) break;
    }
    CHECK_MSG(!resourceUpgrade.empty(),
              "meta.json still has an upgrade that raises a starting balance");
    if (resourceUpgrade.empty()) return;

    Profile bare;
    Match plain(wb::Shipped(), bare, "");
    plain.BootFresh();
    const int without = plain.Run().Amount(resource);

    Profile owned;
    owned.SetMetaLevel(resourceUpgrade, 1);
    Match stocked(wb::Shipped(), owned, "");
    stocked.BootFresh();

    CHECK_MSG(stocked.Run().Amount(resource) == without + perLevel,
              "a fresh boot starts with the Armory bonus, not without it");
}

static void testAFreshBootGivesTheBoardTheResearchItWasBootedWith() {
    // The Town Hall and the starting workers go through Upgrades, not straight
    // off the data row. With a blank profile the two are identical, which is
    // why this needs an owned level to be visible at all.
    const Supersonic::Json::Value& upgrades = wb::Shipped().MetaUpgrades();

    std::string hpUpgrade;
    double delta = 0.0;
    for (const auto& [id, row] : upgrades.AsObject()) {
        const Value& effect = row["effects"][Ids::kTownHall]["max_hp"];
        if (effect.IsNumber()) {
            hpUpgrade = id;
            delta = effect.AsNumber(0.0);
            break;
        }
    }
    if (hpUpgrade.empty()) {
        // Not a failure: the data may simply not have one. Say so rather than
        // silently asserting nothing.
        CHECK_MSG(true, "meta.json has no Town Hall max_hp effect to test with");
        return;
    }

    Profile owned;
    owned.SetMetaLevel(hpUpgrade, 1);
    Match match(wb::Shipped(), owned, "");
    match.BootFresh();

    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the hall exists");
    if (hall == nullptr) return;
    CHECK_EQ(hall->Hp(), 1000 + static_cast<int>(delta));
}

// =========================================================================
// The layout, and every fallback in it
// =========================================================================

static void testTheLayoutIsTheShippedWorldFile() {
    const Match::Layout layout = Match::Layout::FromData(wb::Shipped());
    CHECK_NEAR(layout.width, 6000.0f);
    CHECK_NEAR(layout.groundY, 590.0f);
    CHECK_NEAR(layout.laneDepth, 280.0f);
    CHECK_EQ(layout.buildingRow, 0.18);
    CHECK_NEAR(layout.enemyX, 5960.0f);
    CHECK_NEAR(layout.townHallX, 1500.0f);
    CHECK_NEAR(layout.playerSpawnX, 1680.0f);
    CHECK_NEAR(layout.playerSpawnSpacing, 60.0f);

    // `_building_y`, 590 + 280 x 0.18: the row the boot stands the hall on.
    CHECK_NEAR(layout.BuildingY(), 640.4f);
}

static void testTheEnemyEdgeFollowsTheWallRatherThanANumber() {
    // The shipped file sets both, so at shipped values a port that hardcoded
    // 5960 as the fallback is indistinguishable from a correct one. An authored
    // world with a different width and no spawn edge is the only thing that can
    // tell them apart - and getting it wrong puts every wave forty pixels from
    // a wall that is no longer there.
    {
        wb::ScratchData narrow("layout", "world.json", R"({ "width": 4000 })");
        GameData data;
        data.LoadAll(narrow.Path());
        const Match::Layout layout = Match::Layout::FromData(data);
        CHECK_NEAR(layout.width, 4000.0f);
        CHECK_NEAR(layout.enemyX, 3960.0f);
    }
    {
        // Everything absent: every fallback in one line.
        wb::ScratchData empty("layout", "world.json", R"({})");
        GameData data;
        data.LoadAll(empty.Path());
        const Match::Layout layout = Match::Layout::FromData(data);
        CHECK_NEAR(layout.width, 6000.0f);
        CHECK_NEAR(layout.groundY, 800.0f);
        CHECK_EQ(layout.buildingRow, 0.18);
        CHECK_NEAR(layout.BuildingY(), 800.0f);   // no band, so the row is the line
        CHECK_NEAR(layout.enemyX, 5960.0f);
        CHECK_NEAR(layout.townHallX, 1500.0f);
        CHECK_NEAR(layout.playerSpawnX, 1680.0f);
        CHECK_NEAR(layout.playerSpawnSpacing, 60.0f);
    }
}

// =========================================================================
// A trained unit: the clamp, the ground line, and the research
// =========================================================================

// The barracks positions here are unreachable in the shipped game - placement
// hands a building the ground line and the world's own bounds - so both of
// these need a fixture that puts one where a player could not.
static void testATrainedUnitIsClampedToTheWorldAtBothEnds() {
    Profile profile;

    for (const auto& [x, expected] : std::vector<std::pair<float, float>>{{6000.0f, 6000.0f},
                                                                          {-100.0f, 0.0f}}) {
        Match match(wb::Shipped(), profile, "");
        match.BootFresh();

        glm::vec2 born(-1.0f);
        match.Bus().unitSpawned.Connect([&born](Unit* unit) {
            if (unit != nullptr && unit->Stats().id == Ids::kSoldier) born = unit->Position();
        });

        Building* barracks = match.PlaceBuilding(
            Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks), true,
            glm::vec2(x, 800.0f));
        barracks->EnqueueTraining(Ids::kSoldier);
        for (int i = 0; i < 41; ++i) match.StepBuildings(0.2);

        // The ceiling is the width EXACTLY, not the width less half a body - so
        // a unit trained at the right-hand edge stands ON it, forty pixels past
        // the line the enemy spawns from.
        CHECK_NEAR(born.x, expected);
    }
}

static void testATrainedUnitCannotInheritAHeightFromWhatMadeIt() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    glm::vec2 born(-1.0f);
    match.Bus().unitSpawned.Connect([&born](Unit* unit) {
        if (unit != nullptr && unit->Stats().id == Ids::kSoldier) born = unit->Position();
    });

    Building* barracks = match.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks), true,
        glm::vec2(2300.0f, 500.0f));
    barracks->EnqueueTraining(Ids::kSoldier);
    for (int i = 0; i < 41; ++i) match.StepBuildings(0.2);

    CHECK_MSG(std::fabs(born.y - 590.0f) < 0.001f,
              "the ground line, not the 500 the building was standing at");
}

static void testATrainedUnitCarriesTheResearchTheRunHasNow() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    // Something affordable that raises a soldier's damage.
    std::string upgrade;
    for (const auto& [id, row] : wb::Shipped().Upgrades().AsObject()) {
        if (row["effects"][Ids::kSoldier]["damage"].IsNumber()) {
            upgrade = id;
            break;
        }
    }
    CHECK_MSG(!upgrade.empty(), "upgrades.json still raises a soldier's damage");
    if (upgrade.empty()) return;

    const int base = UnitStats::FromJson(Ids::kSoldier, wb::Shipped().Unit(Ids::kSoldier)).damage;

    match.Run().MarkResearched(upgrade);

    int trained = -1;
    match.Bus().unitSpawned.Connect([&trained](Unit* unit) {
        if (unit != nullptr && unit->Stats().id == Ids::kSoldier) trained = unit->Stats().damage;
    });

    Building* barracks = match.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks), true,
        glm::vec2(2300.0f, 800.0f));
    barracks->EnqueueTraining(Ids::kSoldier);
    for (int i = 0; i < 41; ++i) match.StepBuildings(0.2);

    CHECK_MSG(trained > base, "a soldier trained after the research is stronger than the row");
}

// =========================================================================
// What is announced, and what is not
// =========================================================================

static void testASpawnAnnouncesItselfAndARestoreNeverDoes() {
    // The assertion that keeps the sink minimal, and the ONLY one that can.
    //
    // If the restore path went through the spawn path, every restored enemy
    // would be counted twice - and Snapshot::Restore recounts what it rebuilt
    // and hands the number to the director, so AliveEnemies() would still read
    // correctly and the mistake would survive every assertion on it. The
    // emission count is the only observable the recount cannot repair.
    Profile profile;
    Match match(wb::Shipped(), profile, "");

    int spawned = 0;
    match.Bus().unitSpawned.Connect([&spawned](Unit*) { ++spawned; });

    match.BootFresh();
    CHECK_MSG(spawned == 4, "a fresh boot announces its three workers and the hero");

    match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                    glm::vec2(4200.0f, 800.0f));
    CHECK_EQ(spawned, 5);
    CHECK_EQ(match.Director().AliveEnemies(), 1);

    const Value document = match.Capture();

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    int restoredSpawns = 0;
    second.Bus().unitSpawned.Connect([&restoredSpawns](Unit*) { ++restoredSpawns; });

    CHECK_MSG(second.BootFromSave(document), "it restores");
    CHECK_MSG(restoredSpawns == 0, "and announces nothing, because nothing was spawned");
    CHECK_EQ(second.Director().AliveEnemies(), 1);
    CHECK_EQ(static_cast<int>(second.Units().size()), 5);

    // And they are in the lane, which nothing else here would notice: a restore
    // clears it, so a unit that never rejoins looks perfectly alive on the
    // board and is invisible to every scan in the game.
    CHECK_EQ(second.LaneIndex().CountOf(Factions::kPlayer), 4);
    CHECK_EQ(second.LaneIndex().CountOf(Factions::kEnemy), 1);
}

static void testADeathIsAnnouncedExactlyOnceHoweverOftenItIsAskedFor() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    int deaths = 0;
    match.Bus().unitDied.Connect([&deaths](Unit*) { ++deaths; });

    Unit* raider =
        match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                        glm::vec2(4200.0f, 800.0f));
    CHECK_EQ(match.Director().AliveEnemies(), 1);

    raider->Kill();
    CHECK_EQ(deaths, 1);
    CHECK_EQ(match.Director().AliveEnemies(), 0);

    // The second kill is what the guard is for. Without it the tally goes
    // negative, and a director waiting for it to reach zero never gets there -
    // so a run that has been won never says so.
    raider->Kill();
    CHECK_MSG(deaths == 1, "still one");
    CHECK_MSG(match.Director().AliveEnemies() == 0, "and the tally never goes below zero");
}

static void testOnlyAnEnemyMovesTheEnemyTally() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                    glm::vec2(4200.0f, 800.0f));
    CHECK_EQ(match.Director().AliveEnemies(), 1);

    // A worker dying is not a step toward victory.
    match.Units()[0]->Kill();
    CHECK_MSG(match.Director().AliveEnemies() == 1, "a player unit dying does not count");
}

static void testTheTownHallIsTheBuildingWhoseFallEndsTheRun() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* barracks = match.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks), true,
        glm::vec2(2300.0f, 800.0f));

    barracks->Destroy();
    CHECK_MSG(match.Run().IsPlaying(), "losing a barracks is a setback, not a defeat");

    Building* hall = match.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr, "the hall is there to lose");
    if (hall == nullptr) return;
    hall->Destroy();
    CHECK_MSG(match.Run().CurrentPhase() == GameState::Phase::Lost, "and the hall is the run");
}

// =========================================================================
// Booting twice
// =========================================================================

static void testAWarmMatchDoesNotInheritTheRunBeforeIt() {
    // Godot gets this for free - a restart destroys the whole scene tree - so
    // `_boot_fresh` may assume an empty world and there is no line in the
    // original that corresponds to this. A C++ Match is an object that gets
    // reused, and nothing before this slice ever cleared the fresh path.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                    glm::vec2(4200.0f, 800.0f));
    match.PlaceBuilding(Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks),
                        true, glm::vec2(2300.0f, 800.0f));
    match.Pool().Spawn(glm::vec2(0.0f), nullptr, 5, 700.0f);
    CHECK_MSG(match.Pool().PoolSize() > 0, "there is an arrow in the air");

    // And run the clock out past the first two scripted waves, which is the
    // half of this that actually bites: a director carries its own elapsed
    // time, and Setup deliberately does not touch it.
    match.StepDirector(200.0);
    CHECK_MSG(match.Director().Elapsed() > 0.0, "the first run got somewhere");
    CHECK_EQ(match.Run().CurrentWave(), 2);

    match.BootFresh();

    CHECK_EQ(static_cast<int>(match.Units().size()), 4);
    CHECK_EQ(static_cast<int>(match.Buildings().size()), 1);
    CHECK_EQ(static_cast<int>(match.Nodes().size()), 22);
    CHECK_MSG(match.LaneIndex().CountOf(Factions::kPlayer) == 4,
              "three workers and the hero in the lane, not eight");

    // And the hero in hand is this run's, not the one the first run spawned.
    CHECK_MSG(match.Control().Hero() == match.Units()[3].get(),
              "the hero possessed is the one this boot spawned");
    CHECK_EQ(match.LaneIndex().CountOf(Factions::kEnemy), 0);
    CHECK_EQ(match.Pool().PoolSize(), 0);

    // The director starts the second run at the start of it. Without this the
    // new match opens with the old one's clock already past wave two - so its
    // first step spawns a backlog - and with the old one's dead still counted
    // as alive, so it could never declare victory.
    CHECK_EQ(match.Director().AliveEnemies(), 0);
    CHECK_MSG(match.Director().Elapsed() == 0.0, "and at zero on the clock");
    CHECK_EQ(match.Run().CurrentWave(), 0);

    match.StepDirector(1.0);
    CHECK_MSG(match.Units().size() == 4, "one second in, no wave has arrived");
}

static void testTheForkTakesTheDocumentOrElseStartsOver() {
    Profile profile;

    {
        Match match(wb::Shipped(), profile, "");
        CHECK_MSG(!match.Boot(Value()), "nothing pending is a fresh run");
        CHECK_EQ(static_cast<int>(match.Units().size()), 4);
    }
    {
        // A stale-version document is refused rather than half-read, and the
        // fallback is a fresh match rather than an empty one.
        Value stale = Value(Supersonic::Json::Object{});
        stale.Set("version", Value(999.0));
        stale.Set("units", Value(Supersonic::Json::Array{}));

        Match match(wb::Shipped(), profile, "");
        CHECK_MSG(!match.Boot(stale), "a document from another schema is not a run");
        CHECK_EQ(static_cast<int>(match.Units().size()), 4);
    }
    {
        Match source(wb::Shipped(), profile, "");
        source.BootFresh();
        source.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                         glm::vec2(4200.0f, 800.0f));
        const Value document = source.Capture();

        Match match(wb::Shipped(), profile, "");
        CHECK_MSG(match.Boot(document), "a valid one is restored");
        CHECK_EQ(static_cast<int>(match.Units().size()), 5);
    }
}

// =========================================================================
// The director across a restore - both arming calls
// =========================================================================

static void testARestoredRunKeepsItsLevelAndItsSchedule() {
    // Once this was the endless case: the port's Snapshot::Restore sets the
    // run's choices at its own first step and arms nothing, and without the
    // second arming a Continue of an endless run came back with a director
    // that would never generate another wave. Endless is gone (73999ce); what
    // the restore still has to carry is the LEVEL, and a director armed to run
    // the schedule to its end.
    Profile profile;
    Match source(wb::Shipped(), profile, "");
    source.Run().SetLevel("level_1");
    source.BootFresh();
    CHECK_MSG(source.Run().Level() == "level_1", "the run it saved chose its level");

    const Value document = source.Capture();

    // A destination that has chosen nothing, which is the situation a Continue
    // from the main menu is always in.
    Profile secondProfile;
    Match restored(wb::Shipped(), secondProfile, "");
    CHECK_MSG(restored.Run().Level().empty(), "and the destination has not, before the restore");
    CHECK_MSG(restored.BootFromSave(document), "it restores");

    CHECK_MSG(restored.Run().Level() == "level_1", "the run's level came back");

    // Behaviourally: an armed director runs the schedule to its last wave and
    // then reports that none is left.
    for (int i = 0; i < 14; ++i) restored.StepDirector(60.0);
    CHECK_EQ(restored.Run().CurrentWave(), 5);
    CHECK_MSG(restored.Director().SecondsToNextWave() < 0.0,
              "and after the fifth there is no next wave");
}

static void testARestoredRunResumesItsScheduleRatherThanReplayingIt() {
    // The FIRST arming, before the restore. WaveDirector::FromSave clamps the
    // saved wave index against the schedule it has, and the schedule is only
    // ever filled in by Setup - so a restore into a never-armed director clamps
    // a wave-four run back to zero and sends the whole match round again.
    Profile profile;
    Match source(wb::Shipped(), profile, "");
    source.BootFresh();

    // Past the first two scripted waves, which are at 60 and 150.
    source.StepDirector(200.0);
    const int spawnedByNow = static_cast<int>(source.Units().size()) - 3;
    CHECK_MSG(spawnedByNow > 0, "some raiders are on the field");
    CHECK_EQ(source.Run().CurrentWave(), 2);

    const Value document = source.Capture();

    Profile secondProfile;
    Match restored(wb::Shipped(), secondProfile, "");
    CHECK_MSG(restored.BootFromSave(document), "it restores");

    // The next scripted wave is at 240 and elapsed is 200, so a director that
    // kept its place queues nothing on a short step. One that was rewound to
    // the start of the schedule would fire waves one and two again immediately.
    const int before = static_cast<int>(restored.Units().size());
    restored.StepDirector(1.0);
    CHECK_MSG(static_cast<int>(restored.Units().size()) == before,
              "a resumed run does not replay the waves it already fought");
    CHECK_EQ(restored.Run().CurrentWave(), 2);
}

static void testARestoredMatchStillKnowsHowWideTheWorldIs() {
    // The layout is read before the fork in the original, so it has to be on
    // BOTH paths here. A port that filled it in only on a fresh boot leaves a
    // restored match clamping every trained unit against a width of zero, which
    // puts it on the left edge of the world.
    //
    // AND IT NEEDS AN AUTHORED WORLD TO SEE THAT AT ALL. The Layout struct's
    // own defaults are the shipped file's numbers - deliberately, so that a
    // missing key is the game rather than a zero - which means at shipped
    // values a Match that never reads world.json is indistinguishable from one
    // that does. Removing the read went straight through the first version of
    // this case. A world four thousand wide with its ground line at 640 is the
    // only thing that can tell them apart.
    wb::ScratchData narrow("restore", "world.json",
                           R"({ "width": 4000, "height": 900, "ground_y": 640,
                                "town_hall_spawn_x": 1200, "player_spawn_x": 1300,
                                "player_spawn_spacing": 30, "enemy_spawn_x": 3900 })");
    GameData data;
    CHECK_MSG(data.LoadAll(narrow.Path()), "the authored world loads");

    Profile profile;
    Match source(data, profile, "");
    source.BootFresh();
    CHECK_NEAR(source.WorldLayout().groundY, 640.0f);
    CHECK_NEAR(source.Units()[0]->Position().y, 640.0f);

    const Value document = source.Capture();

    Profile secondProfile;
    Match restored(data, secondProfile, "");
    CHECK_MSG(restored.BootFromSave(document), "it restores");
    CHECK_NEAR(restored.WorldLayout().width, 4000.0f);
    CHECK_NEAR(restored.WorldLayout().groundY, 640.0f);

    glm::vec2 born(-1.0f);
    restored.Bus().unitSpawned.Connect([&born](Unit* unit) {
        if (unit != nullptr && unit->Stats().id == Ids::kSoldier) born = unit->Position();
    });

    Building* barracks = restored.PlaceBuilding(
        Upgrades::ForBuilding(data, restored.Run(), secondProfile, Ids::kBarracks), true,
        glm::vec2(2300.0f, 640.0f));
    barracks->EnqueueTraining(Ids::kSoldier);
    for (int i = 0; i < 41; ++i) restored.StepBuildings(0.2);

    CHECK_NEAR(born.x, 2395.0f);
    CHECK_MSG(std::fabs(born.y - 640.0f) < 0.001f,
              "on the restored world's ground line, not on the struct's default");
}

static void testAHalfBuiltBuildingComesBackHalfBuilt() {
    // The sink is told whether each building was finished, and passes it
    // through as the pre-placed flag. A sink that always said "finished" would
    // hand the player a free barracks on every Continue - and no board in this
    // suite has an unfinished building on it unless one is put there on
    // purpose, so that mutation walked through the round-trip check.
    Profile profile;
    Match source(wb::Shipped(), profile, "");
    source.BootFresh();

    Building* site = source.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), source.Run(), profile, Ids::kBarracks), false,
        glm::vec2(2600.0f, 800.0f));
    CHECK_MSG(!site->IsComplete(), "it starts as a building site");
    site->AddBuildProgress(4.0);   // of an authored ten

    const Value document = source.Capture();

    Profile secondProfile;
    Match restored(wb::Shipped(), secondProfile, "");
    CHECK_MSG(restored.BootFromSave(document), "it restores");

    Building* rebuilt = restored.FindBuilding(Ids::kBarracks);
    CHECK_MSG(rebuilt != nullptr, "the site came back");
    if (rebuilt == nullptr) return;
    CHECK_MSG(!rebuilt->IsComplete(), "and came back UNfinished");
    CHECK_MSG(rebuilt->BuildProgress() == 4.0, "with the progress it had, exactly");

    // And the Town Hall beside it, which was finished, still is.
    Building* hall = restored.FindBuilding(Ids::kTownHall);
    CHECK_MSG(hall != nullptr && hall->IsComplete(), "the finished one is still finished");
}

// =========================================================================
// Tombstones, and what they cost
// =========================================================================

static void testADeadEntityStaysOnTheBoardAndStopsBeingFound() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Unit* raider =
        match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                        glm::vec2(1700.0f, 800.0f));

    CHECK_MSG(match.NearestEnemyUnit(Factions::kPlayer, 1700.0f, 300.0f) == raider,
              "a living raider is found");

    raider->Kill();

    CHECK_MSG(match.Units().size() == 5, "the corpse is still owned");
    CHECK_EQ(match.DeadUnits(), 1);
    CHECK_MSG(match.NearestEnemyUnit(Factions::kPlayer, 1700.0f, 300.0f) == nullptr,
              "and stops being found the moment it dies");

    // A capture drops it, which is what makes a tombstone free where it counts.
    Snapshot::CaptureReport report;
    match.Capture(&report);
    CHECK_EQ(report.skippedDead, 1);
}

static void testACachedDepositIndexNeverChangesItsMind() {
    // The reason nothing is ever erased. A deposit index is a POSITIONAL alias
    // into the building list, not an identity - so if a fallen building were
    // removed from the middle, a worker's cached index would silently name a
    // different, live Town Hall. It would still pass DepositExists, the worker
    // would still deliver, and the wood would still balance, because nothing in
    // the economy cares who banked it.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const int first = match.NearestDeposit(1500.0f);
    CHECK_EQ(first, 0);

    // A barracks in the middle, then a second Town Hall past it.
    match.PlaceBuilding(Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks),
                        true, glm::vec2(2300.0f, 800.0f));
    match.PlaceBuilding(Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kTownHall),
                        true, glm::vec2(3000.0f, 800.0f));

    const int far = match.NearestDeposit(3000.0f);
    CHECK_EQ(far, 2);

    // Fell the building between them. The index of the far hall must not move.
    match.Buildings()[1]->Destroy();

    CHECK_MSG(match.NearestDeposit(3000.0f) == far, "the far hall is still where it was");
    CHECK_MSG(match.DepositExists(far), "and still a deposit");
    CHECK_NEAR(match.DepositPosition(far).x, 3000.0f);
    CHECK_MSG(!match.DepositExists(1), "while the fallen barracks is not one");
}

// =========================================================================
// The anti-farm property, both halves
// =========================================================================

static void testAFinishedRunCannotBeResumedToBeBankedTwice() {
    // Neither half of this has ever had a call site in the port. ClearRun
    // existed with nothing calling it, which left the original's anti-farm
    // property unenforced - and the autosave guard is what stops the very next
    // background write from putting the file straight back.
    TempRunPath run("antifarm");
    Profile profile;
    Match match(wb::Shipped(), profile, run.Path());
    match.BootFresh();
    match.Run().SetCurrentWave(3);

    CHECK_MSG(match.AutosaveRun(), "a live match saves");
    CHECK_MSG(run.Exists(), "and there is a run on disk");
    CHECK_MSG(Snapshot::IsValid(Snapshot::LoadRun(run.Path())), "a valid one");

    const int before = profile.Renown();
    match.Run().Lose();

    const int earned = profile.Renown() - before;
    CHECK_MSG(earned == Meta::RunEndRenown(wb::Shipped(), 3, false),
              "the run banked what reaching wave three is worth");

    // And the literal the oracle prints, so this is anchored to a number the
    // original computed rather than agreeing with the port's own formula:
    //   verify_meta.gd -> "run-end loss at wave 3 banks renown via the overlay
    //   wiring (30)"
    CHECK_EQ(earned, 30);
    CHECK_MSG(!run.Exists(), "and a finished run drops its Continue");

    // The half that makes the first half stick.
    CHECK_MSG(!match.AutosaveRun(), "a finished match does not save");
    CHECK_MSG(!run.Exists(), "so the file stays gone");

    // And losing twice banks nothing more, because the phase guard fires once.
    match.Run().Lose();
    CHECK_MSG(profile.Renown() - before == earned, "renown is awarded exactly once per run");
}

static void testAMatchWithNoRunPathTouchesNoFilesystem() {
    // "" is not a fallback for a default path - it is a Match that has been
    // told it has nowhere to write, and saying so by passing it is what makes a
    // suite filesystem-free rather than merely lucky.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    CHECK_MSG(!match.AutosaveRun(), "nowhere to write is not an error, it is a no-op");

    // Game over on a pathless match still banks, and still writes nothing.
    match.Run().SetCurrentWave(2);
    const int before = profile.Renown();
    match.Run().Win();
    CHECK_MSG(profile.Renown() > before, "the renown is still banked");
    CHECK_MSG(!match.AutosaveRun(), "and there is still nowhere to write it");
}

static void testTheHighWaterMarkOnlyEverRises() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    match.Bus().waveStarted.Emit(7);
    CHECK_EQ(profile.BestWave(), 7);

    // A restart replaying the early waves must not lower the record.
    match.Bus().waveStarted.Emit(2);
    CHECK_MSG(profile.BestWave() == 7, "still seven");

    // And it survives the trip through the profile document.
    Profile reloaded;
    CHECK_MSG(reloaded.FromJson(profile.ToJson()), "the profile round-trips");
    CHECK_EQ(reloaded.BestWave(), 7);

    // A profile written before the key existed reads zero, not garbage.
    Profile old;
    CHECK_MSG(old.FromJson(R"({"renown": 40, "meta_levels": {}})"), "an older profile still loads");
    CHECK_EQ(old.BestWave(), 0);
}

// =========================================================================
// The frame
// =========================================================================

static void testAUnitTrainedMidFrameWaitsForTheNextOne() {
    // Godot builds the list of nodes to process before any of them runs, so a
    // unit that appears during the frame does not tick on it. Under this port's
    // frame order the barracks runs BEFORE the units, so a count taken as the
    // units phase began would step the soldier on the frame it was born - a
    // one-frame divergence nothing else would ever notice.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Building* barracks = match.PlaceBuilding(
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kBarracks), true,
        glm::vec2(2300.0f, 800.0f));
    barracks->EnqueueTraining(Ids::kSoldier);

    // Soldiers by id, not the board's size: the hall auto-trains workers on
    // its own clock, so the size says nothing about the barracks.
    const auto soldierOnBoard = [&match]() -> const Unit* {
        for (const auto& unit : match.Units()) {
            if (unit->Stats().id == Ids::kSoldier) return unit.get();
        }
        return nullptr;
    };

    for (int i = 0; i < 39; ++i) match.Step(0.2);
    CHECK_MSG(soldierOnBoard() == nullptr, "no soldier yet, at 7.8 seconds of an 8.0 train time");

    // Something for a newborn to notice, placed so that "did it think this
    // frame" is the ONLY question the assertion below can be answering. The
    // soldier is born at 2395 with an aggro range of 300; a raider at 2450 is
    // well inside it. Without a target in range this test passes either way -
    // an Idle soldier that HAS thought and found nothing is indistinguishable
    // from one that never thought at all.
    match.SpawnUnit(UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider)),
                    glm::vec2(2450.0f, 800.0f));

    match.Step(0.2);

    const Unit* soldier = soldierOnBoard();
    CHECK_MSG(soldier != nullptr, "the soldier is on the board");
    if (soldier == nullptr) return;

    CHECK_MSG(soldier->CurrentState() == Unit::State::Idle,
              "which has not thought yet, because its frame had already been decided");
    CHECK_MSG(soldier->AttackTarget() == nullptr, "and so has acquired nothing");
}

static void testTheFrameAndTheHarnessOrderAgreeOnEveryOracleNumber() {
    // The harness drives units, then buildings, then the director, and never
    // steps projectiles. The shipped frame is the scene tree's order and does
    // step them. Asserting the two agree on this board is a FINDING rather than
    // a formality - and if they ever stop agreeing, that is a result with a
    // mechanism behind it rather than a reason to bend the frame to the test.
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().SetDifficulty("hard");
    match.Run().SetLevel("level_1");
    match.BootFresh();
    enrichLikeTheHarness(match);

    for (int i = 0; i < 80; ++i) match.Step(0.2);

    CHECK_EQ(match.Run().Amount(Ids::kWood), 220);
    CHECK_EQ(static_cast<int>(match.Units().size()), 8);
    const int gathering = countInState(match, Unit::State::Gathering);
    CHECK_MSG(gathering >= 3 && gathering <= 4, "gathering is one of the oracle's two answers");
    CHECK_EQ(match.Director().AliveEnemies(), 1);
}

} // namespace

// =========================================================================
// What the save gained with the restructure
// =========================================================================

// verify_snapshot 11 through a real boot: every node the economy file spawns
// has art, and a save written before art existed takes it back.
//
//   ok  : a pre-art save re-adopts today's sprites (22/22 textured)
static void testAPreArtSaveReAdoptsTodaysSpritesThroughARealBoot() {
    Profile profile;
    Match first(wb::Shipped(), profile, "");
    first.BootFresh();
    CHECK_MSG(!first.Nodes().empty() && first.Nodes()[0]->sprite == "res://assets/world/tree.png",
              "a fresh boot's nodes carry the economy file's art");

    Value captured = first.Capture();
    Supersonic::Json::Array nodes = captured["resource_nodes"].AsArray();
    for (Value& node : nodes) node.Set("sprite", Value());
    captured.Set("resource_nodes", Value(std::move(nodes)));

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    CHECK(second.BootFromSave(captured));
    int textured = 0;
    for (const auto& node : second.Nodes()) {
        if (!node->sprite.empty()) ++textured;
    }
    CHECK_EQ(static_cast<int>(second.Nodes().size()), 22);
    CHECK_MSG(textured == 22, "a pre-art save re-adopts today's sprites (22/22 textured)");
}

// Added by the port. The original's harness digests the capture points but
// never takes one, so nothing there shows a tug surviving a restore. Here the
// Lumber Camp is part-way to the enemy and the War Banner is held, and both
// come back - the banner with its bonus, on the restored run.
static void testCapturePointsComeBackWithTheirTug() {
    Profile profile;
    Match first(wb::Shipped(), profile, "");
    first.BootFresh();
    CHECK_EQ(static_cast<int>(first.CapturePoints().size()), 2);
    if (first.CapturePoints().size() < 2) return;
    first.CapturePoints()[0]->ForceProgress(-0.4);
    first.CapturePoints()[1]->ForceProgress(1.0);
    CHECK_MSG(first.Run().ArmyDamageMult() == 1.25, "precondition: the held banner counts");

    const Value captured = Snapshot::FromText(Snapshot::ToText(first.Capture()));
    CHECK_EQ(static_cast<int>(captured["capture_points"].AsArray().size()), 2);

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    CHECK(second.BootFromSave(captured));
    CHECK_EQ(static_cast<int>(second.CapturePoints().size()), 2);
    if (second.CapturePoints().size() < 2) return;
    CHECK_EQ(second.CapturePoints()[0]->Progress(), -0.4);
    CHECK(second.CapturePoints()[0]->Holder().empty());
    CHECK_EQ(second.CapturePoints()[1]->Progress(), 1.0);
    CHECK(second.CapturePoints()[1]->Holder() == Factions::kPlayer);
    CHECK_MSG(second.Run().ArmyDamageMult() == 1.25, "and the banner's bonus is the restored run's");
}

// A save from before capture points is still a save: it restores with neutral
// points rather than being refused. No version bump, as the original added
// the field.
static void testASaveFromBeforeCapturePointsRestoresThemNeutral() {
    Profile profile;
    Match first(wb::Shipped(), profile, "");
    first.BootFresh();
    if (first.CapturePoints().size() < 2) return;
    first.CapturePoints()[1]->ForceProgress(1.0);

    Value captured = first.Capture();
    captured.Set("capture_points", Value());

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    CHECK_MSG(second.BootFromSave(captured), "still a valid save");
    for (const auto& point : second.CapturePoints()) {
        CHECK_EQ(point->Progress(), 0.0);
        CHECK(point->Holder().empty());
    }
    CHECK_EQ(second.Run().ArmyDamageMult(), 1.0);
}

// =========================================================================
// The hero: at boot, at a death, and through a save
// =========================================================================
//
// verify_respawn at the game's 50741d1 printed, on 10 September 2026:
//
//   ok  : boot possesses the hero automatically
//   ok  : death releases possession
//   ok  : the hero respawns and is repossessed
//   ok  : respawn lands beside the Town Hall (nearest respawn building to 1860)
//   ok  : standing at the Town Hall latches its panel
//   ok  : walking away releases the panel
//   ok  : the Build menu slows time to 0.15
//   ok  : closing the menu restores full speed
//   ok  : a pending respawn is marked in GameState (rides saves)
//   ok  : a hero-less restore re-arms the respawn flow
//   ok  : the saved death position drives the re-armed respawn
//
// A is in the fresh-board test; B and E are here. C and D - the proximity
// panel and the build menu's slow-motion - are the bottom bar's, and the port
// has no bottom bar yet.

static void testAFallenHeroRespawnsBesideTheNearestRespawnBuilding() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    Unit* first = match.Control().Hero();
    CHECK_MSG(first != nullptr, "precondition: a hero in hand");
    if (first == nullptr) return;

    first->Kill();
    CHECK_MSG(match.Control().Hero() == nullptr, "death releases possession");
    CHECK_MSG(match.RespawnPending(), "and a respawn is pending");
    CHECK_MSG(match.Run().HeroDown() && match.Run().HeroDownX() == 1860.0,
              "marked in the run where he fell");

    // The harness shortens the delay to 0.1 and waits frames; the authored ten
    // seconds is asserted here instead, from both sides. 9.5 and then 0.5
    // lands on zero exactly, which is the edge Godot's timer fires on.
    CHECK_EQ(wb::Shipped().Economy()["hero_respawn_delay_s"].AsNumber(), 10.0);
    match.StepRespawn(9.5);
    CHECK_MSG(match.Control().Hero() == nullptr, "nobody at nine and a half seconds");
    match.StepRespawn(0.5);

    Unit* reborn = match.Control().Hero();
    CHECK_MSG(reborn != nullptr && reborn != first && reborn->IsControlled(),
              "the hero respawns and is repossessed");
    if (reborn == nullptr) return;

    // The harness's "within 400 of the Town Hall", and the number behind it:
    // the hall's 1500, plus half its 120-wide body, plus its spawn offset of 40.
    CHECK_MSG(std::fabs(reborn->Position().x - 1500.0f) < 400.0f,
              "respawn lands beside the Town Hall (nearest respawn building to 1860)");
    CHECK_NEAR(reborn->Position().x, 1600.0f);
    CHECK_NEAR(reborn->Position().y, 590.0f);
    CHECK_MSG(!match.RespawnPending() && !match.Run().HeroDown(), "and nothing is pending");
}

// Added by the port: the harness's hero dies beside the Town Hall, so it never
// has two sites to choose between.
static void testTheRespawnBuildingIsTheOneNearestWhereHeFell() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.BootFresh();

    const BuildingStats waystone =
        Upgrades::ForBuilding(wb::Shipped(), match.Run(), profile, Ids::kWaystone);
    CHECK_MSG(waystone.heroRespawn, "precondition: the data flags the Waystone");

    // The finished one is the furthest of three from where he falls. The two
    // nearer are unfinished and in ruins, and neither counts.
    match.PlaceBuilding(waystone, true, glm::vec2(4000.0f, 700.0f));
    match.PlaceBuilding(waystone, false, glm::vec2(3900.0f, 700.0f));
    Building* ruin = match.PlaceBuilding(waystone, true, glm::vec2(3800.0f, 700.0f));
    ruin->Destroy();

    Unit* hero = match.Control().Hero();
    if (hero == nullptr) return;
    hero->SetPosition(glm::vec2(3850.0f, 700.0f));
    hero->Kill();
    match.StepRespawn(10.0);

    Unit* reborn = match.Control().Hero();
    CHECK_MSG(reborn != nullptr, "he came back");
    if (reborn == nullptr) return;
    CHECK_NEAR(reborn->Position().x, 4000.0f + waystone.bodySize.x * 0.5f +
                                         static_cast<float>(waystone.spawnOffset));
}

// Added by the port, and unreachable at shipped values: the Town Hall is a
// respawn site, and its fall already loses the run. A board with a hero and
// no hall is the only way here, and it has to be built by hand.
static void testWithNowhereToRespawnTheRunIsLost() {
    Profile profile;
    Match match(wb::Shipped(), profile, "");
    match.Run().Reset();

    Unit* hero = match.SpawnUnit(Upgrades::ForUnit(wb::Shipped(), match.Run(), profile, Ids::kHero),
                                 glm::vec2(2000.0f, 590.0f));
    match.Control().Possess(hero);
    CHECK_MSG(match.Control().Hero() == hero, "precondition: possessed");

    int lost = 0;
    match.Bus().gameLost.Connect([&lost] { ++lost; });

    hero->Kill();
    CHECK_MSG(match.RespawnPending(), "the respawn is armed as ever");
    match.StepRespawn(10.0);
    CHECK_EQ(lost, 1);
    CHECK_MSG(!match.Run().IsPlaying(), "the run is over");
    CHECK_MSG(match.Control().Hero() == nullptr, "with nobody brought back");
    CHECK_MSG(match.Run().HeroDown(), "and the mark left where it was, as the original leaves it");
}

// Added by the port. Both halves of the original's is_playing guard: a run
// that ends while he waits brings nobody back, and a death after the end arms
// nothing.
static void testARunThatIsOverBringsNobodyBack() {
    {
        Profile profile;
        Match match(wb::Shipped(), profile, "");
        match.BootFresh();
        Unit* hero = match.Control().Hero();
        if (hero == nullptr) return;

        hero->Kill();
        match.Run().Win();
        match.StepRespawn(10.0);
        CHECK_MSG(match.Control().Hero() == nullptr, "a run won while he waits brings nobody back");
        CHECK_MSG(!match.RespawnPending(), "and the timer is spent");
        CHECK_EQ(static_cast<int>(match.Units().size()), 4);
    }
    {
        Profile profile;
        Match match(wb::Shipped(), profile, "");
        match.BootFresh();
        Unit* hero = match.Control().Hero();
        if (hero == nullptr) return;

        match.Run().Win();
        hero->Kill();
        CHECK_MSG(!match.RespawnPending(), "a death after the end arms nothing");
        CHECK_MSG(!match.Run().HeroDown(), "and marks nothing");
    }
}

// verify_respawn E through a real save, where the harness calls the restore's
// half by hand on a live board. The death is at the harness's own 4321, and a
// Waystone beside it makes the saved x the thing that decides where he comes
// back.
static void testAHeroLessSaveReArmsTheRespawnAtTheSavedDeathX() {
    Profile profile;
    Match first(wb::Shipped(), profile, "");
    first.BootFresh();

    const BuildingStats waystone =
        Upgrades::ForBuilding(wb::Shipped(), first.Run(), profile, Ids::kWaystone);
    first.PlaceBuilding(waystone, true, glm::vec2(4000.0f, 700.0f));

    Unit* hero = first.Control().Hero();
    if (hero == nullptr) return;
    hero->SetPosition(glm::vec2(4321.0f, 700.0f));
    hero->Kill();
    first.StepRespawn(4.0);   // part-way through the wait

    const Value captured = Snapshot::FromText(Snapshot::ToText(first.Capture()));
    const Supersonic::Json::Array& mark = captured["game_state"]["hero_down_x"].AsArray();
    CHECK_MSG(mark.size() == 1 && mark[0].AsNumber() == 4321.0,
              "a pending respawn is marked in GameState (rides saves)");

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    CHECK(second.BootFromSave(captured));
    CHECK_MSG(second.Control().Hero() == nullptr, "the save has no hero to hand back");
    CHECK_MSG(second.RespawnPending(), "a hero-less restore re-arms the respawn flow");
    CHECK_MSG(second.Run().HeroDown() && second.Run().HeroDownX() == 4321.0,
              "the saved death position drives the re-armed respawn");
    CHECK_MSG(second.RespawnLeft() == 10.0,
              "with the full delay, as the original's fresh timer gives it");

    second.StepRespawn(10.0);
    Unit* reborn = second.Control().Hero();
    CHECK_MSG(reborn != nullptr, "and he comes back");
    if (reborn != nullptr) {
        CHECK_NEAR(reborn->Position().x, 4000.0f + waystone.bodySize.x * 0.5f +
                                             static_cast<float>(waystone.spawnOffset));
    }
}

// Added by the port: the other half of the restore. A mark beside a living
// hero is stale by definition, and keeping it would respawn a second one.
static void testARestoredHeroClearsAStaleDeathMark() {
    Profile profile;
    Match first(wb::Shipped(), profile, "");
    first.BootFresh();
    first.Run().SetHeroDown(1234.0);

    Profile secondProfile;
    Match second(wb::Shipped(), secondProfile, "");
    CHECK(second.BootFromSave(first.Capture()));
    CHECK_MSG(second.Control().Hero() != nullptr, "he is up, and possessed");
    CHECK_MSG(!second.Run().HeroDown(), "so the stale mark is cleared");
    CHECK_MSG(!second.RespawnPending(), "and nothing is waiting to bring back a second");
}

// Added by the port. Both of the original's early returns: no hero named, and
// a name units.json does not have.
static void testAnUnauthoredHeroSpawnsNobody() {
    for (const char* economy : {R"({ "starting_hero": "" })", R"({ "starting_hero": "nobody" })"}) {
        wb::ScratchData scratch("hero", "economy.json", economy);
        GameData data;
        data.LoadAll(scratch.Path());

        Profile profile;
        Match match(data, profile, "");
        match.BootFresh();
        CHECK_MSG(match.Control().Hero() == nullptr, "no hero in hand");
        CHECK_EQ(static_cast<int>(match.Units().size()), 0);
    }
}

static void runTests() {
    testTheWholeMatchNumbersTheOraclePrints();
    testATrainedUnitIsBornWhereTheBuildingPutsIt();
    testCaptureRestoreCaptureIsEquivalentThroughARealBoot();

    testAFreshBootBuildsTheBoardTheDataDescribes();
    testAFreshBootBanksTheArmoryBonusItWasBoughtWith();
    testAFreshBootGivesTheBoardTheResearchItWasBootedWith();

    testTheLayoutIsTheShippedWorldFile();
    testTheEnemyEdgeFollowsTheWallRatherThanANumber();

    testATrainedUnitIsClampedToTheWorldAtBothEnds();
    testATrainedUnitCannotInheritAHeightFromWhatMadeIt();
    testATrainedUnitCarriesTheResearchTheRunHasNow();

    testASpawnAnnouncesItselfAndARestoreNeverDoes();
    testADeathIsAnnouncedExactlyOnceHoweverOftenItIsAskedFor();
    testOnlyAnEnemyMovesTheEnemyTally();
    testTheTownHallIsTheBuildingWhoseFallEndsTheRun();

    testAWarmMatchDoesNotInheritTheRunBeforeIt();
    testTheForkTakesTheDocumentOrElseStartsOver();

    testARestoredRunKeepsItsLevelAndItsSchedule();
    testARestoredRunResumesItsScheduleRatherThanReplayingIt();
    testARestoredMatchStillKnowsHowWideTheWorldIs();
    testAHalfBuiltBuildingComesBackHalfBuilt();

    testAPreArtSaveReAdoptsTodaysSpritesThroughARealBoot();
    testCapturePointsComeBackWithTheirTug();
    testASaveFromBeforeCapturePointsRestoresThemNeutral();

    testAFallenHeroRespawnsBesideTheNearestRespawnBuilding();
    testTheRespawnBuildingIsTheOneNearestWhereHeFell();
    testWithNowhereToRespawnTheRunIsLost();
    testARunThatIsOverBringsNobodyBack();
    testAHeroLessSaveReArmsTheRespawnAtTheSavedDeathX();
    testARestoredHeroClearsAStaleDeathMark();
    testAnUnauthoredHeroSpawnsNobody();

    testADeadEntityStaysOnTheBoardAndStopsBeingFound();
    testACachedDepositIndexNeverChangesItsMind();

    testAFinishedRunCannotBeResumedToBeBankedTwice();
    testAMatchWithNoRunPathTouchesNoFilesystem();
    testTheHighWaterMarkOnlyEverRises();

    testAUnitTrainedMidFrameWaitsForTheNextOne();
    testTheFrameAndTheHarnessOrderAgreeOnEveryOracleNumber();
}

TEST_MAIN("test_wb_match", 100)
