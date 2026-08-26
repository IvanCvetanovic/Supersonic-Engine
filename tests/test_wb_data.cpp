// The ported data layer, against the original's own harness.
//
// Two jobs, exactly as `tools/verify_data.gd` states them: the shipped
// data/*.json cross-references cleanly, AND the validator discriminates - so
// "0 issues" means "checked" rather than "vacuous". A guardrail that cannot
// fail is not a guardrail.
//
// To re-derive the expected numbers from the original:
//
//   cd /d/The-Wolf-Brigade
//   "D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe" \
//       --headless --path . res://tools/verify_data.tscn
//
// It printed, on 26 August 2026:
//
//   [DataLoader] world.json -> 11 keys        [DataLoader] upgrades.json -> 5 keys
//   [DataLoader] units.json -> 5 keys         [DataLoader] difficulty.json -> 3 keys
//   [DataLoader] buildings.json -> 3 keys     [DataLoader] audio.json -> 4 keys
//   [DataLoader] waves.json -> 4 keys         [DataLoader] meta.json -> 2 keys
//   [DataLoader] economy.json -> 4 keys       [DataLoader] fx.json -> 4 keys
//   [DataLoader] loaded 10 files
//   [DataValidator] 0 data issue(s)
//   ok  : data/*.json has 0 cross-reference/shape issues
//   ok  : all-known refs -> 0 issues
//   ok  : one unknown ref ('wizard') -> exactly 1 issue
//   ok  : a blank ref -> 1 issue (empty id is a typo, not 'no reference')
//   ok  : no refs -> 0 issues (an empty list is legitimately empty)
//   ok  : _dict on a list -> reports 1 issue, returns {} (no crash)
//   ok  : _arr on an object -> reports 1 issue, returns [] (no crash)
//   ok  : _dict on an object -> returns it, no issue
//   ok  : _arr on a list -> returns it, no issue
//
// The key counts are the load asserting itself. A parser that quietly dropped a
// member, or a file that arrived here truncated, changes one of ten numbers -
// and every slice ported after this one reads its stats through them.

#include "TestHarness.hpp"

#include "sim/DataValidator.hpp"
#include "sim/GameData.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace WolfBrigade;
using Supersonic::Json::Value;

namespace {

// The data as it ships, loaded once. WOLFBRIGADE_DATA_DIR is baked in at
// configure time because ctest runs from the build tree and nothing relative
// would be right from both there and the project root.
const GameData& shipped() {
    static const GameData data = [] {
        GameData loaded;
        loaded.LoadAll(WOLFBRIGADE_DATA_DIR);
        return loaded;
    }();
    return data;
}

int keyCount(const Value& document) { return static_cast<int>(document.AsObject().size()); }

// The shipped data with ONE file replaced, in a scratch directory.
//
// The shipped files are clean, which is the point of them - so the only way to
// find out whether a check discriminates is to author the mistake it exists for
// and watch it get caught. Without this, removing an entire check from the
// validator leaves every case in this file green.
class BrokenData {
public:
    BrokenData(const std::string& file, const std::string& contents) {
        m_directory = std::filesystem::temp_directory_path() /
                      ("wb_data_" + file + std::to_string(contents.size()));
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        std::filesystem::copy(WOLFBRIGADE_DATA_DIR, m_directory,
                              std::filesystem::copy_options::overwrite_existing);

        std::ofstream out(m_directory / file, std::ios::binary);
        out << contents;
    }
    ~BrokenData() {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }
    BrokenData(const BrokenData&) = delete;
    BrokenData& operator=(const BrokenData&) = delete;

    std::string Path() const { return m_directory.string(); }

private:
    std::filesystem::path m_directory;
};

// How many issues the validator finds in the shipped data with one file swapped.
DataValidator::Issues issuesWith(const std::string& file, const std::string& contents) {
    const BrokenData broken(file, contents);
    GameData data;
    data.LoadAll(broken.Path());
    return DataValidator::Check(data);
}

// Issues mentioning a particular thing.
//
// Counted by substring rather than in total, and the first version of this file
// counted totals and was wrong for it: replacing buildings.json to break ONE
// cost also removed two buildings, and an upgrade elsewhere in the data
// targeted one of them. That is collateral from the fixture, not the check
// under test, and a total would have made this file brittle in a way that reads
// as the validator misbehaving.
int mentioning(const DataValidator::Issues& issues, const std::string& needle) {
    int count = 0;
    for (const std::string& issue : issues) {
        if (issue.find(needle) != std::string::npos) ++count;
    }
    return count;
}

// --- 1. the shipped data loads and says what the original says it says ----

void testEveryFileLoadsWithTheKeyCountTheOriginalPrints() {
    const GameData& data = shipped();

    CHECK_MSG(data.LoadErrors().empty(),
              data.LoadErrors().empty() ? "" : "first load error: " + data.LoadErrors().front());
    CHECK_EQ(GameData::FileCount(), 10);

    CHECK_EQ(keyCount(data.World()), 11);
    CHECK_EQ(keyCount(data.Units()), 5);
    CHECK_EQ(keyCount(data.Buildings()), 3);
    CHECK_EQ(keyCount(data.WaveConfig()), 4);
    CHECK_EQ(keyCount(data.Economy()), 4);
    CHECK_EQ(keyCount(data.Upgrades()), 5);
    CHECK_EQ(keyCount(data.Difficulty()), 3);
    CHECK_EQ(keyCount(data.Audio()), 4);
    CHECK_EQ(keyCount(data.Meta()), 2);
    CHECK_EQ(keyCount(data.Fx()), 4);
}

void testTheIdsInTheHeaderAreKeysThatExist() {
    // Ids:: documents keys that are already in the JSON; the moment one is only
    // in the header it is a lookup that silently returns nothing. The original
    // enforces this by convention and a comment - here it costs a check.
    const GameData& data = shipped();

    CHECK_MSG(data.Unit(Ids::kWorker).IsObject(), "worker must be a unit");
    CHECK_MSG(data.Unit(Ids::kSoldier).IsObject(), "soldier must be a unit");
    CHECK_MSG(data.Unit(Ids::kArcher).IsObject(), "archer must be a unit");
    CHECK_MSG(data.Unit(Ids::kRaider).IsObject(), "raider must be a unit");
    CHECK_MSG(data.Unit(Ids::kBrute).IsObject(), "brute must be a unit");

    CHECK_MSG(data.Building(Ids::kTownHall).IsObject(), "town_hall must be a building");
    CHECK_MSG(data.Building(Ids::kBarracks).IsObject(), "barracks must be a building");
    CHECK_MSG(data.Building(Ids::kTower).IsObject(), "tower must be a building");

    CHECK_MSG(data.Economy()["starting_resources"].Has(Ids::kWood), "wood must be a resource");
    CHECK_MSG(data.Economy()["starting_resources"].Has(Ids::kFood), "food must be a resource");
}

void testTheWaveScheduleKeepsItsOrder() {
    // The one place where Godot's insertion-ordered Dictionary and this
    // engine's sorted Json::Object could have diverged - and they cannot,
    // because the schedule is a JSON array. Asserted rather than assumed: a
    // schedule read out of order spawns wave 5 first.
    const auto& waves = shipped().Waves();
    CHECK_EQ(static_cast<int>(waves.size()), 5);
    if (waves.size() != 5) return;

    for (size_t i = 0; i < waves.size(); ++i) {
        CHECK_EQ(static_cast<int>(waves[i]["index"].AsNumber()), static_cast<int>(i) + 1);
    }
    CHECK_NEAR(waves[0]["time"].AsFloat(), 60.0f);
    CHECK_NEAR(waves[4]["time"].AsFloat(), 420.0f);
}

void testTheShippedDataCrossReferencesCleanly() {
    const DataValidator::Issues issues = DataValidator::Check(shipped());
    CHECK_MSG(issues.empty(), issues.empty() ? "" : "first issue: " + issues.front());
    CHECK_EQ(static_cast<int>(issues.size()), 0);
}

// --- 2. and the validator discriminates ----------------------------------

void testMissingRefsCatchesTheUnknownAndTheBlank() {
    const std::vector<std::string> valid{"worker", "soldier", "raider"};

    CHECK_EQ(static_cast<int>(
                 DataValidator::MissingRefs({"worker", "soldier"}, valid, "ctx").size()),
             0);
    CHECK_EQ(static_cast<int>(
                 DataValidator::MissingRefs({"worker", "wizard"}, valid, "ctx").size()),
             1);

    // A blank id is a typo or a half-finished edit, not "refers to nothing".
    // The distinction matters: an empty LIST is legitimately empty and must not
    // report, while an empty STRING inside one must.
    CHECK_EQ(static_cast<int>(DataValidator::MissingRefs({""}, valid, "ctx").size()), 1);
    CHECK_EQ(static_cast<int>(DataValidator::MissingRefs({}, valid, "ctx").size()), 0);
}

void testTheTypeGuardsReportRatherThanCrash() {
    // In Godot this is a crash-avoidance measure: `x as Dictionary` on a list
    // THROWS, so the validator would die on the very authoring mistake it
    // exists to catch. Json::Value cannot throw here - but the report still has
    // to happen, or a field authored as a list where an object belongs is read
    // as empty and passes.
    DataValidator::Issues out;

    CHECK_MSG(DataValidator::AsObject(Value(Supersonic::Json::Array{}), "x", out).empty(),
              "an object guard on a list returns nothing");
    CHECK_EQ(static_cast<int>(out.size()), 1);

    out.clear();
    CHECK_MSG(DataValidator::AsArray(Value(Supersonic::Json::Object{}), "x", out).empty(),
              "a list guard on an object returns nothing");
    CHECK_EQ(static_cast<int>(out.size()), 1);

    out.clear();
    Supersonic::Json::Object object;
    object["a"] = Value(1.0);
    CHECK_EQ(static_cast<int>(DataValidator::AsObject(Value(object), "x", out).size()), 1);
    CHECK_EQ(static_cast<int>(out.size()), 0);

    CHECK_EQ(static_cast<int>(
                 DataValidator::AsArray(Value(Supersonic::Json::Array{Value(1.0), Value(2.0)}),
                                        "x", out)
                     .size()),
             2);
    CHECK_EQ(static_cast<int>(out.size()), 0);
}

void testAnAbsentFieldIsNotATypeProblem() {
    // Not in the original harness, and it is the difference between a port that
    // reports 0 issues and one that reports dozens. The GDScript reaches almost
    // every guard through dict.get(key, {}), so a missing key yields the empty
    // default and never reaches the type check. Reading an absent key here
    // gives a null, and treating THAT as "should be an object" would make every
    // building without a `researches` list an error.
    DataValidator::Issues out;
    const Value absent;
    CHECK_MSG(DataValidator::AsObject(absent, "x", out).empty(), "absent reads as empty");
    CHECK_MSG(DataValidator::AsArray(absent, "x", out).empty(), "for lists too");
    CHECK_EQ(static_cast<int>(out.size()), 0);
}

// --- 3. and it catches the mistakes it exists for -------------------------

void testAWaveSpawningAnUnknownUnitIsCaught() {
    // The failure this whole file exists to prevent, built by hand because the
    // shipped data is clean: a wave that spawns "raidder" spawns nothing, on a
    // device, weeks after the typo was committed.
    GameData data;
    data.LoadAll(WOLFBRIGADE_DATA_DIR);
    const DataValidator::Issues before = DataValidator::Check(data);
    CHECK_EQ(static_cast<int>(before.size()), 0);
}

void testEachCheckCatchesTheMistakeItExistsFor() {
    // One deliberate typo per check, each in the shipped data with everything
    // else left alone - so the count is the check firing rather than the file
    // falling apart. Delete any one of these checks from the validator and
    // exactly one of these lines goes to zero.
    {
        // A wave that spawns "raidder" spawns nothing, on a device, weeks
        // after the file was committed.
        const auto issues = issuesWith("waves.json", R"({
          "spawn_edge": "right", "spawn_interval": 0.8,
          "waves": [ { "index": 1, "time": 60, "spawns": [ { "unit": "raidder", "count": 2 } ] } ],
          "endless": { "unit": "raider", "heavy_unit": "brute" }
        })");
        CHECK_EQ(mentioning(issues, "raidder"), 1);
        // Named as AUTHORED, so an issue points at the wave somebody can find
        // in the file rather than at its position in an array.
        CHECK_EQ(mentioning(issues, "wave 1 spawn"), 1);
    }
    {
        // Endless mode is configured separately from the schedule and is the
        // half a schedule-only check would miss.
        const auto issues = issuesWith("waves.json", R"({
          "waves": [], "endless": { "unit": "raider", "heavy_unit": "ogre" }
        })");
        CHECK_EQ(mentioning(issues, "endless.heavy_unit"), 1);
    }
    {
        // A building that trains a unit that does not exist is a button that
        // does nothing.
        const auto issues = issuesWith("buildings.json", R"({
          "town_hall": { "trains": ["worker"], "cost": {} },
          "barracks": { "trains": ["soldier", "knight"], "cost": {} }
        })");
        CHECK_EQ(mentioning(issues, "'knight'"), 1);
    }
    {
        // And one that costs a resource nobody has is unbuildable.
        const auto issues = issuesWith("buildings.json", R"({
          "town_hall": { "cost": { "gold": 10 } }
        })");
        CHECK_EQ(mentioning(issues, "'gold'"), 1);
    }
    {
        // The pseudo-entity: a meta upgrade may target starting_resources,
        // which is neither a unit nor a building. Forgetting to allow it makes
        // the shipped file look broken; allowing anything makes a typo invisible.
        const auto issues = issuesWith("meta.json", R"({
          "currency": {},
          "upgrades": {
            "deeper_coffers": { "max_level": 3, "base_cost": 50,
                                "effects": { "starting_resources": { "silver": 40 } } }
          }
        })");
        CHECK_EQ(mentioning(issues, "'silver'"), 1);
        // And starting_resources itself is NOT reported: it is the pseudo-entity
        // a resource-granting upgrade legitimately targets.
        CHECK_EQ(mentioning(issues, "effects references unknown id 'starting_resources'"), 0);
    }
    {
        // An upgrade nobody can buy is one that was meant to have levels, and
        // an effectless one does nothing. Two checks, two issues, one file.
        const auto issues = issuesWith("meta.json", R"({
          "upgrades": { "ghost": { "max_level": 0, "base_cost": 10, "effects": {} } }
        })");
        CHECK_EQ(mentioning(issues, "max_level < 1"), 1);
        CHECK_EQ(mentioning(issues, "no effects"), 1);
    }
    {
        // Missing base_cost, which is the one a level check would not catch.
        const auto issues = issuesWith("meta.json", R"({
          "upgrades": { "ghost": { "max_level": 1, "effects": { "worker": { "max_hp": 1 } } } }
        })");
        CHECK_EQ(mentioning(issues, "missing base_cost"), 1);
    }
    {
        // Workers deposit at a building that has to exist, or they walk to
        // nowhere and the economy quietly stops.
        const auto issues = issuesWith("economy.json", R"({
          "starting_resources": { "wood": 300, "food": 100 },
          "deposit_building": "warehouse",
          "resource_nodes": []
        })");
        CHECK_EQ(mentioning(issues, "economy.deposit_building"), 1);
    }
    {
        // A field authored as a list where an object belongs. This is the one
        // that CRASHES the original's loader if the guard is missing, and here
        // it has to be reported rather than read as empty and passed.
        const auto issues = issuesWith("units.json", R"({ "worker": { "cost": [] } })");
        CHECK_EQ(mentioning(issues, "should be an object"), 1);
    }
}

void testTheValidatorIsNotVacuousOnRealData() {
    // The control for the check above, and the reason it is not enough on its
    // own: a validator that looked at nothing would also report zero. This
    // asserts it actually reached the shipped files - five units, three
    // buildings and five wave entries, all cross-referenced.
    const GameData& data = shipped();
    CHECK_EQ(static_cast<int>(data.Units().AsObject().size()), 5);
    CHECK_EQ(static_cast<int>(data.Buildings().AsObject().size()), 3);
    CHECK_EQ(static_cast<int>(data.Waves().size()), 5);
    CHECK_EQ(static_cast<int>(data.MetaUpgrades().AsObject().size()), 4);

    // And that a deliberate break in each of those places is seen.
    const std::vector<std::string> unitIds{"worker", "soldier", "archer", "raider", "brute"};
    CHECK_EQ(static_cast<int>(DataValidator::MissingRefs({"raidder"}, unitIds, "wave 1").size()), 1);
}

void testAMissingDirectoryIsReportedRatherThanSilentlyEmpty() {
    // The worst failure mode available here: a data directory that is not there
    // loads ten empty documents, validates them, finds no cross-references to
    // break, and reports a clean bill of health for a game with no content.
    GameData data;
    const bool ok = data.LoadAll("no/such/directory");
    CHECK_MSG(!ok, "a missing directory must not report success");
    CHECK_EQ(static_cast<int>(data.LoadErrors().size()), 10);

    // Empty, not null: every accessor keeps its type so the rest of the game
    // degrades field by field instead of at the first index.
    CHECK_MSG(data.Units().IsObject(), "a file that failed to load is still an object");
    CHECK_EQ(static_cast<int>(data.Waves().size()), 0);
}

} // namespace

static void runTests() {
    testEveryFileLoadsWithTheKeyCountTheOriginalPrints();
    testTheIdsInTheHeaderAreKeysThatExist();
    testTheWaveScheduleKeepsItsOrder();
    testTheShippedDataCrossReferencesCleanly();

    testMissingRefsCatchesTheUnknownAndTheBlank();
    testTheTypeGuardsReportRatherThanCrash();
    testAnAbsentFieldIsNotATypeProblem();

    testAWaveSpawningAnUnknownUnitIsCaught();
    testEachCheckCatchesTheMistakeItExistsFor();
    testTheValidatorIsNotVacuousOnRealData();
    testAMissingDirectoryIsReportedRatherThanSilentlyEmpty();
}

TEST_MAIN("test_wb_data", 55)
