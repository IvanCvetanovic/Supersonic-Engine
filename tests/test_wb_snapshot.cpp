// The ported save/restore, against the original's harness and against itself.
//
// `verify_snapshot.gd` proves a mid-match save round-trips: drive a rich board,
// capture, JSON, restore, re-capture, and assert the two captures are
// equivalent. It also documents, in its own comments, why equivalence is not
// enough:
//
//   "Independent ground truth - the exact values WE set, checked against the
//    live restored nodes below. Equivalence alone can't catch a field dropped
//    from BOTH captures (that's how the building-cooldown gap first hid);
//    these can."
//
// So this suite does both, and pushes the first one further than the original
// can. Godot renumbers its save-ids on every rebuild, so its digest throws away
// the whole stats block, every reference, move_target, carry, attack_cd and
// more. Only `sid` and the `ref_*` fields actually renumber here - everything
// else is compared EXACTLY, and the references are compared by translating each
// id to its referent's own record rather than by dropping it. That catches
// "relinked to the wrong entity of the same type", which the original's digest
// cannot see.
//
// To re-derive the original's side:
//
//   cd /d/The-Wolf-Brigade
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . res://tools/verify_snapshot.tscn
//
// It printed, on 26 August 2026:
//
//   ok  : snapshot serializes to valid JSON
//   ok  : round-tripped snapshot is valid (version 1)
//   ok  : capture->restore->capture is equivalent
//   ok  : wood restored to saved balance (270)
//   ok  : difficulty + mode restored
//   ok  : GATHERING units relinked their tree (3/3)
//   ok  : Town Hall damaged hp restored exactly (963)
//   ok  : building attack_cd restored (0.55)
//   ok  : WaveDirector elapsed restored (~16.0 vs 16.0)
//   ok  : enemy units restored (1 live == 1 saved)
//   ok  : WaveDirector recounted alive enemies (1)
//
// Its specific numbers - 270 wood, 963 hit points, six units - come from booting
// the whole match through `main.gd`: the town hall spawn, the resource-node
// layout, three starting workers. The port has no `main.gd` yet, so this suite
// builds its own board and asserts the same PROPERTIES against numbers it sets
// itself. Said plainly rather than left to look like a match.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Building.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/Lane.hpp"
#include "sim/Progression.hpp"
#include "sim/Projectiles.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/Snapshot.hpp"
#include "sim/Unit.hpp"
#include "sim/WaveDirector.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace WolfBrigade;
using Supersonic::Json::Value;

namespace {

constexpr float kGroundY = 800.0f;

// --- Enum ordinals are part of the file format ---------------------------
//
// `state: 2` means Gathering, in a file somebody saved last month. Insert a
// state into the enum next quarter and every round-trip test still passes -
// capture and restore agree on the new ordinal - while every saved file
// silently means something else. These pin the wire format to the source it was
// ported from, and are the one thing that forces a conscious version bump.

static_assert(static_cast<int>(Unit::State::Idle) == 0, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Moving) == 1, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Gathering) == 2, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Delivering) == 3, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Building) == 4, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Attacking) == 5, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Fleeing) == 6, "unit.gd:11 enum State");
static_assert(static_cast<int>(Unit::State::Dead) == 7, "unit.gd:11 enum State");

static_assert(static_cast<int>(Building::State::Constructing) == 0, "building.gd:8 enum State");
static_assert(static_cast<int>(Building::State::Complete) == 1, "building.gd:8 enum State");
static_assert(static_cast<int>(Building::State::Dead) == 2, "building.gd:8 enum State");

static_assert(static_cast<int>(GameState::Phase::Playing) == 0, "game_state.gd:9 enum Phase");
static_assert(static_cast<int>(GameState::Phase::Won) == 1, "game_state.gd:9 enum Phase");
static_assert(static_cast<int>(GameState::Phase::Lost) == 2, "game_state.gd:9 enum Phase");

// --- A board that owns its entities --------------------------------------
//
// Shaped like the Match that will eventually own them for real, so that when
// one lands it implements RestoreSink by inheriting rather than by being
// reshaped around this. It lives here rather than in sim/ because deciding
// where entities live is the next slice's job, not this one's.
struct Board final : public World, public Snapshot::RestoreSink {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    Profile profile;
    WaveDirector director{wb::Shipped(), state, bus};
    Lane lane;
    ProjectilePool projectiles;

    // unique_ptr, and it matters: the id table holds raw pointers for the life
    // of a capture, and a vector of units by value would invalidate every one
    // of them the next time it grew.
    std::vector<std::unique_ptr<Unit>> units;
    std::vector<std::unique_ptr<Building>> buildings;
    std::vector<std::unique_ptr<ResourceNode>> nodes;

    Board() {
        state.Reset();
        director.Setup([](const UnitStats&, const glm::vec2&) {}, 5960.0f, kGroundY);
    }

    // --- World ---
    ResourceNode* NearestHarvestable(float x) const override {
        ResourceNode* best = nullptr;
        float bestDistance = 0.0f;
        for (const auto& node : nodes) {
            if (!node->Harvestable()) continue;
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
    Unit* NearestEnemyUnit(const std::string& faction, float x, float range) const override {
        return lane.NearestEnemy(faction, x, range);
    }
    Damageable* NearestEnemyBuilding(const std::string& faction, float x) const override {
        const std::string enemy =
            (faction == Factions::kPlayer) ? Factions::kEnemy : Factions::kPlayer;
        Building* best = nullptr;
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
    ProjectilePool* Projectiles() override { return &projectiles; }

    // --- RestoreSink ---
    Building* CreateBuilding(const BuildingStats& stats, bool complete,
                             const glm::vec2& position) override {
        return PlaceStats(stats, complete, position.x);
    }
    ResourceNode* CreateResourceNode(const ResourceNode& fromSave) override {
        nodes.push_back(std::make_unique<ResourceNode>(fromSave));
        return nodes.back().get();
    }
    Unit* CreateUnit(const UnitStats& stats, const glm::vec2& position) override {
        return SpawnStats(stats, position.x);
    }

    // --- Building the board by hand ---
    Building* Place(const std::string& id, float x, bool complete = true) {
        return PlaceStats(Upgrades::ForBuilding(wb::Shipped(), state, profile, id), complete, x);
    }
    Building* PlaceStats(const BuildingStats& stats, bool complete, float x) {
        auto building =
            std::make_unique<Building>(stats, complete, state, bus, *this);
        building->SetPosition(glm::vec2(x, kGroundY));
        building->SetTrainTimes(wb::Shipped().Units());
        buildings.push_back(std::move(building));
        return buildings.back().get();
    }

    Unit* Spawn(const std::string& id, float x) {
        return SpawnStats(UnitStats::FromJson(id, wb::Shipped().Unit(id)), x);
    }
    Unit* SpawnStats(const UnitStats& stats, float x) {
        auto unit = std::make_unique<Unit>(stats, state, bus, *this);
        unit->SetPosition(glm::vec2(x, kGroundY));
        Unit* raw = unit.get();
        units.push_back(std::move(unit));
        lane.Register(raw);
        return raw;
    }

    ResourceNode* AddNode(const std::string& resource, float x, int amount) {
        auto node = std::make_unique<ResourceNode>();
        node->resource = resource;
        node->maxAmount = amount;
        node->amount = amount;
        node->position = glm::vec2(x, kGroundY);
        nodes.push_back(std::move(node));
        return nodes.back().get();
    }

    Snapshot::Scene View() {
        Snapshot::Scene scene;
        for (const auto& unit : units) scene.units.push_back(unit.get());
        for (const auto& building : buildings) scene.buildings.push_back(building.get());
        for (const auto& node : nodes) scene.resourceNodes.push_back(node.get());
        return scene;
    }

    void Clear() {
        units.clear();
        buildings.clear();
        nodes.clear();
        lane.Clear();
    }

    Building* FindBuilding(const std::string& id) {
        for (const auto& building : buildings) {
            if (building->Stats().id == id) return building.get();
        }
        return nullptr;
    }
};

// --- The full-fidelity digest --------------------------------------------
//
// Only `sid` and the three `ref_*` renumber between a capture and a re-capture,
// because ids are minted in the caller's enumeration order. Everything else is
// compared exactly - including the whole stats block, move_target, carry,
// attack_cd and the node colours, all of which the original's digest throws
// away because Godot's renumbering forced it to.
//
// The references are TRANSLATED rather than dropped: each id becomes its
// referent's own record. That is what catches "relinked to the wrong entity of
// the same type", which no amount of dropping could.

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
    // The whole record minus sid and the refs, serialised. Sorting the FULL
    // string rather than a tuple prefix matters: a wave of identical raiders at
    // the same x would make a short key flap between runs, and ties on a full
    // record are exact duplicates whose order cannot matter.
    Value copy = record;
    copy.Set("sid", Value());
    for (const char* key : {"ref_tree", "ref_build", "ref_attack"}) copy.Set(key, Value());
    return Snapshot::ToText(copy);
}

std::vector<std::string> digestSection(const Value& snapshot, const char* section) {
    std::vector<std::string> rows;
    for (const Value& record : snapshot[section].AsArray()) {
        std::string row = recordKey(record);
        for (const char* key : {"ref_tree", "ref_build", "ref_attack"}) {
            if (!record.Has(key)) continue;
            row += "|" + std::string(key) + "=" +
                   translateRef(snapshot, static_cast<int>(record[key].AsNumber(-1.0)));
        }
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

std::string digest(const Value& snapshot) {
    std::string out;
    for (const char* section : {"units", "buildings", "resource_nodes"}) {
        out += std::string(section) + ":\n";
        for (const std::string& row : digestSection(snapshot, section)) out += "  " + row + "\n";
    }
    // Field for field, including elapsed and the spawn accumulator, bitwise.
    out += "wave_director:" + Snapshot::ToText(snapshot["wave_director"]) + "\n";
    out += "game_state:" + Snapshot::ToText(snapshot["game_state"]) + "\n";
    return out;
}

// A rich board: two buildings, three nodes, workers gathering, an enemy, a
// damaged Town Hall, a queued soldier. The shape verify_snapshot drives to,
// built by hand because the port has no match boot yet.
void enrich(Board& board) {
    board.state.SetDifficulty("hard");
    board.state.SetLevel("level_1");
    board.state.Reset();

    // The restructure's two run-wide switches (b6c73fb), set AFTER Reset,
    // which clears both. They are here so the digest covers them: the game's
    // own review of that commit found verify_snapshot had digested neither.
    board.state.SetWorkersSheltered(true);
    board.state.SetHeroDown(2210.5);

    board.Place(Ids::kTownHall, 1500.0f);
    board.Place(Ids::kBarracks, 2600.0f);

    board.AddNode(Ids::kWood, 1900.0f, 200);
    board.AddNode(Ids::kWood, 2450.0f, 200);
    board.AddNode(Ids::kFood, 1350.0f, 120);

    for (int i = 0; i < 3; ++i) board.Spawn(Ids::kWorker, 1680.0f + 60.0f * static_cast<float>(i));
    board.Spawn(Ids::kRaider, 3000.0f);

    // 16 seconds, at the harness's dt.
    for (int step = 0; step < 80; ++step) {
        for (const auto& unit : board.units) unit->Step(0.2);
        for (const auto& building : board.buildings) building->Step(0.2);
        board.director.Step(0.2);
    }
}

} // namespace

// --- 1. The document itself ----------------------------------------------

static void testADoubleSurvivesTheTripExactly() {
    // Six significant digits is the default and would truncate every one of
    // these. The original's harness would not notice - it compares cooldowns
    // within 0.001 and elapsed within 0.5 - which is exactly how a rounding
    // save would ship.
    Supersonic::Json::Object object;
    object["cooldown"] = Value(0.90000000000000002);
    object["elapsed"] = Value(15.999999999999998);
    object["tiny"] = Value(1e-300);
    object["huge"] = Value(1.7976931348623157e308);
    object["negative"] = Value(-0.1);
    const Value original(std::move(object));

    const std::string text = Snapshot::ToText(original);
    const Value parsed = Snapshot::FromText(text);

    // Exact equality, not a tolerance. A tolerance is what hides this.
    CHECK_MSG(parsed["cooldown"].AsNumber() == 0.90000000000000002, "a cooldown survives");
    CHECK_MSG(parsed["elapsed"].AsNumber() == 15.999999999999998, "an elapsed time survives");
    CHECK_MSG(parsed["tiny"].AsNumber() == 1e-300, "a denormal-ish value survives");
    CHECK_MSG(parsed["negative"].AsNumber() == -0.1, "a negative survives");

    // And writing it again is byte-identical, which is the property a
    // round-trip actually rests on.
    CHECK_MSG(Snapshot::ToText(parsed) == text, "the text round-trips byte for byte");
}

static void testANonFiniteNumberIsWrittenAsSomethingThatParses() {
    // inf and nan are not JSON. Writing one produces a file that cannot be
    // loaded at all, which turns a numeric bug into a lost save.
    Supersonic::Json::Object object;
    object["broken"] = Value(std::nan(""));
    object["overflow"] = Value(std::numeric_limits<double>::infinity());

    const std::string text = Snapshot::ToText(Value(std::move(object)));
    const Value parsed = Snapshot::FromText(text);
    CHECK_MSG(parsed.IsObject(), "the document must still parse: " + text);
    CHECK_NEAR(parsed["broken"].AsFloat(), 0.0f);
}

static void testGarbageIsRefusedRatherThanHalfRead() {
    CHECK_MSG(!Snapshot::FromText("{ \"version\": 1, \"unit").IsObject(),
              "a truncated document parses to nothing");
    CHECK_MSG(!Snapshot::IsValid(Snapshot::FromText("not json at all")), "and is not valid");
    CHECK_MSG(!Snapshot::IsValid(Value()), "nor is nothing");

    Supersonic::Json::Object future;
    future["version"] = Value(999.0);
    CHECK_MSG(!Snapshot::IsValid(Value(std::move(future))),
              "a newer schema is refused rather than half-read");
}

// --- 2. The stats block --------------------------------------------------

static void testTheStatsBlockCarriesEveryUpgradableFieldAndNoOther() {
    // The block exists because these numbers are BAKED at spawn - difficulty,
    // the spawn's own multipliers, research, owned meta - and cannot be recovered from the
    // unit's id. If the two lists drift, a stat is silently unsaved.
    const std::vector<std::string> block = UnitStats::BlockFields();

    for (const std::string& field : block) {
        CHECK_MSG(UnitStats::HasField(field), field + " is in the block but not upgradable");
    }

    // And the other way: every upgradable field except train_time, which is a
    // building's business and is never baked onto a unit.
    for (const char* field :
         {"max_hp", "damage", "carry_capacity", "heal_amount", "attacks_per_sec",
          "attack_range", "aggro_range", "move_speed", "projectile_speed", "gather_rate",
          "gather_range", "deposit_range"}) {
        CHECK_MSG(std::find(block.begin(), block.end(), field) != block.end(),
                  std::string(field) + " is upgradable but not in the saved block");
    }
    // Twelve since the priest (the game's da0d66f) added heal_amount.
    CHECK_EQ(static_cast<int>(block.size()), 12);
}

static void testEverySavedStatIsAlsoRead() {
    // ToBlock and ApplyBlock are separate lists, and a reader that handles
    // eleven of twelve passes a round trip: re-reading the restored unit
    // produces the same stale default on both sides. Twelve distinct sentinels
    // catch it.
    UnitStats source = UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker));
    source.maxHp = 101;
    source.damage = 102;
    source.carryCapacity = 103;
    source.attacksPerSec = 104.5f;
    source.attackRange = 105.5f;
    source.aggroRange = 106.5f;
    source.moveSpeed = 107.5f;
    source.projectileSpeed = 108.5f;
    source.gatherRate = 109.5f;
    source.gatherRange = 110.5f;
    source.depositRange = 111.5f;
    source.healAmount = 112;

    UnitStats target = UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker));
    target.ApplyBlock(source.ToBlock());

    CHECK_EQ(target.maxHp, 101);
    CHECK_EQ(target.damage, 102);
    CHECK_EQ(target.carryCapacity, 103);
    CHECK_NEAR(target.attacksPerSec, 104.5f);
    CHECK_NEAR(target.attackRange, 105.5f);
    CHECK_NEAR(target.aggroRange, 106.5f);
    CHECK_NEAR(target.moveSpeed, 107.5f);
    CHECK_NEAR(target.projectileSpeed, 108.5f);
    CHECK_NEAR(target.gatherRate, 109.5f);
    CHECK_NEAR(target.gatherRange, 110.5f);
    CHECK_NEAR(target.depositRange, 111.5f);
    CHECK_EQ(target.healAmount, 112);
}

static void testAScaledEnemyComesBackScaledRatherThanRebuiltFromData() {
    // The reason the block exists at all. A hard-difficulty raider is 52 hit
    // points and 8 damage; rebuilding one from units.json gives 40 and 6.
    Board board;
    board.state.SetDifficulty("hard");

    UnitStats raider = UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider));
    board.state.ScaleEnemyStats(raider);
    CHECK_EQ(raider.maxHp, 52);
    CHECK_EQ(raider.damage, 8);

    UnitStats rebuilt = UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider));
    rebuilt.ApplyBlock(raider.ToBlock());
    CHECK_EQ(rebuilt.maxHp, 52);
    CHECK_EQ(rebuilt.damage, 8);
}

// --- 3. The run's own state ----------------------------------------------

static void testRestoringABalanceDoesNotReBankTheStartingBonus() {
    // Reset re-banks the persistent starting bonus. A restore that called it
    // would double a player's Deeper Coffers on every single reload.
    Board board;
    board.profile.SetMetaLevel("deeper_coffers", 2);

    // The caller has to hand the owned levels to the run before resetting it.
    // GameState keeps its OWN copy so that Reset can compute the starting
    // bonus without reaching for a Profile - which is a second source of truth
    // for something the profile owns, and is the reason FromSave takes the
    // levels as an argument rather than reading them out of the file. A caller
    // that forgets this gets a run with no bonus and no error, which is
    // exactly what happened the first time this case was written.
    board.state.SetMetaLevels(board.profile.AllMetaLevels());

    board.state.Reset();
    CHECK_EQ(board.state.Amount(Ids::kWood), 380);   // 300 + 80

    board.state.TrySpend({{Ids::kWood, 110}});
    const int saved = board.state.Amount(Ids::kWood);
    CHECK_EQ(saved, 270);

    const Value document = board.state.ToSave();
    GameState restored(wb::Shipped(), board.bus);
    restored.FromSave(document, board.profile.AllMetaLevels());

    CHECK_EQ(restored.Amount(Ids::kWood), 270);
}

static void testMetaLevelsComeFromTheProfileAndNeverFromTheFile() {
    // A snapshot that could resurrect owned levels would let a stale save undo
    // a Reset Progress the player has since performed.
    Board board;
    board.profile.SetMetaLevel("fortified_halls", 2);
    board.state.SetMetaLevels(board.profile.AllMetaLevels());

    const Value document = board.state.ToSave();

    // The file does not carry them AT ALL, which is the structural half of the
    // guarantee: a restore cannot resurrect an owned level because there is
    // nothing in the document to resurrect it from. Asserted directly, because
    // the day somebody adds meta_levels to ToSave "for completeness" is the day
    // a stale save can undo a Reset Progress.
    CHECK_MSG(!document.Has("meta_levels"),
              "a run snapshot must not carry the persistent profile");

    Profile cleared;
    GameState restored(wb::Shipped(), board.bus);
    restored.FromSave(document, cleared.AllMetaLevels());
    CHECK_MSG(restored.GetMetaLevels().empty(), "a cleared profile stays cleared");

    // And the positive: a restore takes them from whatever profile it is
    // handed, so a player who bought a level between saving and loading has it.
    Profile richer;
    richer.SetMetaLevel("fortified_halls", 3);
    GameState upgraded(wb::Shipped(), board.bus);
    upgraded.FromSave(document, richer.AllMetaLevels());
    // Guarded, because .at() on a missing key THROWS - and a test that dies
    // reports nothing at all, which is strictly worse than one that fails.
    const auto found = upgraded.GetMetaLevels().find("fortified_halls");
    CHECK_MSG(found != upgraded.GetMetaLevels().end(),
              "the profile's levels must reach the restored run");
    if (found != upgraded.GetMetaLevels().end()) CHECK_EQ(found->second, 3);

    // And a building rebuilt afterwards reflects the CLEARED profile.
    const BuildingStats hall =
        Upgrades::ForBuilding(wb::Shipped(), restored, cleared, Ids::kTownHall);
    CHECK_EQ(hall.maxHp, 1000);
}

static void testADecidedRunComesBackDecided() {
    // Nothing steps after a loss, so a run restored as Playing looks perfectly
    // correct at zero steps and is a frozen board thereafter. The oracle's
    // harness never exercises this either.
    Board board;
    board.state.Lose();

    const Value document = board.state.ToSave();
    GameState restored(wb::Shipped(), board.bus);
    restored.FromSave(document, {});

    CHECK(restored.CurrentPhase() == GameState::Phase::Lost);
    CHECK_MSG(!restored.IsPlaying(), "and it is not playing");
}

static void testRestoringAnnouncesEveryBalanceItPutBack() {
    // Nothing polls. A HUD built before a restore shows boot defaults over
    // restored balances until something tells it otherwise - and there is no
    // HUD in this port yet, which is exactly why dropping the re-emit would
    // have no visible effect for a year.
    Board board;
    const Value document = board.state.ToSave();

    EventBus bus;
    int announcements = 0;
    bus.resourcesChanged.Connect([&announcements](const std::string&, int) { ++announcements; });

    GameState restored(wb::Shipped(), bus);
    restored.FromSave(document, {});
    CHECK_EQ(announcements, static_cast<int>(restored.Resources().size()));
    CHECK_EQ(announcements, 2);
}

// --- 4. The director's counters ------------------------------------------

static void testTheDirectorsProgressSurvivesAndItsCountIsRecounted() {
    Board board;
    for (int i = 0; i < 80; ++i) board.director.Step(0.2);

    const double elapsed = board.director.Elapsed();
    const Value document = board.director.ToSave();

    Board other;
    other.director.FromSave(document, 3);

    // Bitwise, not within a tolerance. The oracle allows 0.5 of drift on
    // elapsed, which is enough slack to hide a truncating writer.
    CHECK_MSG(other.director.Elapsed() == elapsed, "elapsed survives exactly");
    CHECK_EQ(other.director.AliveEnemies(), 3);
}

static void testANonsenseWaveIndexIsClampedAtBothEnds() {
    Board board;

    Supersonic::Json::Object broken;
    broken["next_wave"] = Value(-1.0);
    board.director.FromSave(Value(broken), 0);
    CHECK_NEAR(static_cast<float>(board.director.SecondsToNextWave()), 60.0f);

    // Unsigned, so a -1 would become eighteen quintillion and skip the whole
    // schedule silently. And past the end is what a data re-tune that removes
    // waves produces.
    Supersonic::Json::Object far;
    far["next_wave"] = Value(9999.0);
    board.director.FromSave(Value(far), 0);
    CHECK_MSG(board.director.SecondsToNextWave() < 0.0,
              "past the end means no wave is scheduled, not a crash");
}

// --- 5. References, on a board that can tell relink from re-acquisition ----

static void testAGatheringUnitComesBackOnTheTreeItChoseNotTheNearestOne() {
    // The discriminating board, and the whole point of it. A test that steps
    // even once before checking passes with relink DELETED - the thinking tick
    // re-acquires within 0.125s and usually picks the same thing. Two nodes,
    // the unit working the FARTHER one, asserted at zero steps: dropped relink
    // gives null, and relink-replaced-by-re-acquisition gives the nearer node.
    Board board;
    ResourceNode* nearer = board.AddNode(Ids::kWood, 2000.0f, 200);
    ResourceNode* farther = board.AddNode(Ids::kWood, 2600.0f, 200);
    board.Place(Ids::kTownHall, 1500.0f);

    Unit* worker = board.Spawn(Ids::kWorker, 1950.0f);
    worker->CommandMoveTo(glm::vec2(2600.0f, kGroundY));
    for (int i = 0; i < 60; ++i) worker->Step(0.2);

    // Precondition, asserted so a later edit cannot quietly make the board
    // indiscriminate.
    CHECK_MSG(board.NearestHarvestable(worker->Position().x) == farther,
              "the worker must be nearer the FARTHER node for this to discriminate");
    CHECK(worker->CurrentState() == Unit::State::Gathering);
    CHECK(worker->TargetNode() == farther);
    (void)nearer;

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::RestoreReport report;
    CHECK_MSG(Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile,
                                rebuilt.director, rebuilt, rebuilt.lane, rebuilt.projectiles,
                                &report),
              "the restore must succeed");

    CHECK_EQ(report.units, 1);
    CHECK_EQ(report.resourceNodes, 2);

    Unit* restored = rebuilt.units.front().get();
    CHECK(restored->CurrentState() == Unit::State::Gathering);
    CHECK_MSG(restored->TargetNode() != nullptr, "the tree must be relinked, not dropped");
    if (restored->TargetNode() == nullptr) return;

    // At ZERO steps, and it must be the one it chose.
    CHECK_NEAR(restored->TargetNode()->position.x, 2600.0f);
}

static void testAnAttackTargetRelinksAcrossTypesWithNoTypeTag() {
    // The reason the id space is single. A unit's target may be a Unit or a
    // Building, and the file carries no tag - so a raider pointed at a Town
    // Hall and a soldier pointed at a raider both have to come back right.
    Board board;
    Building* hall = board.Place(Ids::kTownHall, 1500.0f);
    Unit* raider = board.Spawn(Ids::kRaider, 1640.0f);
    Unit* soldier = board.Spawn(Ids::kSoldier, 1700.0f);

    raider->CommandAttack(hall);
    soldier->CommandAttack(raider);

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    Unit* restoredRaider = nullptr;
    Unit* restoredSoldier = nullptr;
    for (const auto& unit : rebuilt.units) {
        if (unit->Stats().id == Ids::kRaider) restoredRaider = unit.get();
        if (unit->Stats().id == Ids::kSoldier) restoredSoldier = unit.get();
    }
    CHECK_MSG(restoredRaider != nullptr && restoredSoldier != nullptr, "both units restored");
    if (restoredRaider == nullptr || restoredSoldier == nullptr) return;

    CHECK_MSG(restoredRaider->AttackTarget() == rebuilt.FindBuilding(Ids::kTownHall),
              "a raider's building target relinks");
    CHECK_MSG(restoredSoldier->AttackTarget() == restoredRaider,
              "and a soldier's unit target relinks, through the same id space");
    CHECK_MSG(restoredRaider->OrderedToAttack(), "the order flag survives too");
}

static void testAReferenceToSomethingOutsideTheSceneBecomesNothing() {
    // A Damageable that is neither a captured Unit nor a captured Building -
    // the case Godot's duck typing cannot even produce. A wild id or a bad cast
    // here is the one crash this design could produce.
    struct Outsider final : public Damageable {
        void TakeDamage(int) override {}
        bool IsAlive() const override { return true; }
        float HitHalfWidth() const override { return 10.0f; }
        glm::vec2 Position() const override { return glm::vec2(2000.0f, kGroundY); }
    } outsider;

    Board board;
    Unit* soldier = board.Spawn(Ids::kSoldier, 1900.0f);
    soldier->CommandAttack(&outsider);

    Snapshot::CaptureReport captured;
    const Value document =
        Snapshot::Capture(board.View(), board.state, board.director, &captured);

    // -1 in the file, and COUNTED - which is the only way an under-enumerated
    // scene is visible at all.
    CHECK_EQ(static_cast<int>(document["units"].AsArray()[0]["ref_attack"].AsNumber()), -1);
    CHECK_MSG(captured.unresolvedReferences > 0, "an unresolvable reference must be reported");

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);
    CHECK_MSG(rebuilt.units.front()->AttackTarget() == nullptr,
              "and comes back as nothing rather than as a wild pointer");
}

// --- 6. The whole round trip ---------------------------------------------

static void testARichBoardRoundTripsThroughTextExactly() {
    Board board;
    enrich(board);

    // Independent ground truth, set here and checked on the LIVE restored
    // entities below. Equivalence alone cannot catch a field dropped from BOTH
    // captures - the original's own comment says that is how its building
    // cooldown gap first hid.
    Building* hall = board.FindBuilding(Ids::kTownHall);
    hall->TakeDamage(37);
    const int truthHallHp = hall->Hp();
    const double truthElapsed = board.director.Elapsed();

    Building* barracks = board.FindBuilding(Ids::kBarracks);
    barracks->EnqueueTraining(Ids::kSoldier);
    barracks->Step(0.3);
    const double truthTrainProgress = barracks->TrainProgress();

    const Value first = Snapshot::Capture(board.View(), board.state, board.director);
    const std::string text = Snapshot::ToText(first);

    const Value parsed = Snapshot::FromText(text);
    CHECK_MSG(Snapshot::IsValid(parsed), "the document round-trips through text and is valid");
    CHECK_MSG(parsed["units"].AsArray().size() > 0, "and carries units");

    Board rebuilt;
    Snapshot::RestoreReport report;
    CHECK(Snapshot::Restore(parsed, wb::Shipped(), rebuilt.state, rebuilt.profile,
                            rebuilt.director, rebuilt, rebuilt.lane, rebuilt.projectiles,
                            &report));
    CHECK_EQ(report.unknownIds, 0);

    const Value second = Snapshot::Capture(rebuilt.View(), rebuilt.state, rebuilt.director);

    const std::string a = digest(first);
    const std::string b = digest(second);
    CHECK_MSG(a == b, "capture->restore->capture must be equivalent");
    if (a != b) {
        std::printf("--- first ---\n%s--- second ---\n%s", a.c_str(), b.c_str());
    }

    // And the ground truth, off the live entities rather than off the JSON.
    Building* restoredHall = rebuilt.FindBuilding(Ids::kTownHall);
    CHECK_MSG(restoredHall != nullptr, "the Town Hall came back");
    if (restoredHall != nullptr) CHECK_EQ(restoredHall->Hp(), truthHallHp);

    CHECK_MSG(rebuilt.director.Elapsed() == truthElapsed, "elapsed exactly, not within 0.5");

    Building* restoredBarracks = rebuilt.FindBuilding(Ids::kBarracks);
    CHECK_MSG(restoredBarracks != nullptr, "the barracks came back");
    if (restoredBarracks != nullptr) {
        CHECK_EQ(restoredBarracks->QueueLength(), 1);
        CHECK_MSG(restoredBarracks->TrainProgress() == truthTrainProgress,
                  "and its training progress exactly");
    }

    CHECK_MSG(rebuilt.state.Level() == "level_1", "the level came back");
    CHECK(rebuilt.state.CurrentDifficulty() == "hard");
    CHECK_MSG(rebuilt.state.WorkersSheltered(), "the shelter bell came back rung");
    CHECK_MSG(rebuilt.state.HeroDown() && rebuilt.state.HeroDownX() == 2210.5,
              "and where the hero fell, exactly");
}

static void testEverySidIsUniqueAcrossAllThreeKinds() {
    // One counter, three sections. An implementer who resets it between the
    // capture loops makes every reference resolve to whichever entity happened
    // to be bound last - and the digest strips sids, so it cannot see it.
    Board board;
    enrich(board);
    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    std::vector<int> sids;
    for (const char* section : {"units", "buildings", "resource_nodes"}) {
        for (const Value& record : document[section].AsArray()) {
            sids.push_back(static_cast<int>(record["sid"].AsNumber(-1.0)));
        }
    }
    const size_t total = sids.size();
    CHECK_MSG(total > 5, "the board must be rich enough for this to mean anything");

    std::sort(sids.begin(), sids.end());
    sids.erase(std::unique(sids.begin(), sids.end()), sids.end());
    CHECK_EQ(static_cast<int>(sids.size()), static_cast<int>(total));
}

static void testTheLaneIsRebuiltSoUnitsCanStillFindEachOther() {
    // Every relink and equivalence check runs at zero steps and never scans the
    // lane, so a sink that forgot to register would pass all of them - and then
    // no unit would ever acquire an enemy again.
    Board board;
    board.Place(Ids::kTownHall, 1500.0f);
    board.Spawn(Ids::kSoldier, 2000.0f);
    board.Spawn(Ids::kRaider, 2100.0f);

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    CHECK_EQ(rebuilt.lane.CountOf(Factions::kEnemy), 1);
    CHECK_EQ(rebuilt.lane.CountOf(Factions::kPlayer), 1);
    CHECK_EQ(rebuilt.director.AliveEnemies(), 1);

    // And an engagement actually resolves, which is the thing the lane is for.
    for (int i = 0; i < 200; ++i) {
        for (const auto& unit : rebuilt.units) unit->Step(0.1);
    }
    int alive = 0;
    for (const auto& unit : rebuilt.units) {
        if (unit->IsAlive()) ++alive;
    }
    CHECK_MSG(alive == 1, "the soldier and the raider must have fought");
}

static void testARestoreIntoAWarmBoardDoesNotInheritTheLastRun() {
    // A unit never unregisters itself, so a lane carried across a restore keeps
    // the previous run's corpses - exactly the bug Lane.clear_all() exists to
    // prevent in the original.
    Board board;
    board.Spawn(Ids::kRaider, 2000.0f);
    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board warm;
    warm.Spawn(Ids::kRaider, 4000.0f);
    warm.Spawn(Ids::kRaider, 4100.0f);
    CHECK_EQ(warm.lane.CountOf(Factions::kEnemy), 2);
    warm.units.clear();   // the caller drops them; the lane still holds them

    Snapshot::Restore(document, wb::Shipped(), warm.state, warm.profile, warm.director, warm,
                      warm.lane, warm.projectiles);
    CHECK_EQ(warm.lane.CountOf(Factions::kEnemy), 1);
}

// --- 7. What must not be fabricated --------------------------------------

static void testAUnitWhoseDataRowIsGoneIsSkippedRatherThanInvented() {
    // The worst failure available here, and the schema version does not cover
    // it: nobody bumps it when only units.json is re-tuned. FromJson on a
    // missing row returns every default - a player-faction worker - so a saved
    // raider would come back on the player's side, counted by no enemy tally,
    // and hand the player a victory.
    Board board;
    board.Spawn(Ids::kRaider, 2000.0f);
    board.Spawn(Ids::kWorker, 1600.0f);

    Value document = Snapshot::Capture(board.View(), board.state, board.director);

    // Rewrite one id to something nobody authored.
    Supersonic::Json::Array units = document["units"].AsArray();
    CHECK_EQ(static_cast<int>(units.size()), 2);
    for (Value& record : units) {
        if (record["id"].AsString() == Ids::kRaider) record.Set("id", Value(std::string("ogre")));
    }
    document.Set("units", Value(std::move(units)));

    Board rebuilt;
    Snapshot::RestoreReport report;
    CHECK(Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile,
                            rebuilt.director, rebuilt, rebuilt.lane, rebuilt.projectiles,
                            &report));

    CHECK_EQ(report.unknownIds, 1);
    CHECK_EQ(report.units, 1);
    CHECK_EQ(static_cast<int>(rebuilt.units.size()), 1);
    CHECK_MSG(rebuilt.units.front()->Stats().id == Ids::kWorker,
              "the survivor is the worker, and no ogre-shaped worker appeared");
    CHECK_EQ(rebuilt.lane.CountOf(Factions::kEnemy), 0);
}

static void testABuildingWhoseDataRowIsGoneIsSkippedRatherThanLeftAtOneHitPoint() {
    Board board;
    board.Place(Ids::kTownHall, 1500.0f);

    Value document = Snapshot::Capture(board.View(), board.state, board.director);
    Supersonic::Json::Array buildings = document["buildings"].AsArray();
    for (Value& record : buildings) record.Set("id", Value(std::string("citadel")));
    document.Set("buildings", Value(std::move(buildings)));

    Board rebuilt;
    Snapshot::RestoreReport report;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles, &report);

    CHECK_EQ(report.unknownIds, 1);
    CHECK_EQ(report.buildings, 0);
    CHECK_MSG(rebuilt.buildings.empty(),
              "a missing row is maxHp 1, and the clamp would put a Town Hall back at one");
}

// --- 8. Buildings ---------------------------------------------------------

static void testAHalfBuiltBarracksComesBackHalfBuilt() {
    // The easy path is to create everything complete. Every equivalence check
    // still passes if the re-capture re-reads the same wrong state, and the
    // player's half-built barracks is a free finished one on every reload.
    Board board;
    Building* barracks = board.Place(Ids::kBarracks, 2600.0f, /*complete=*/false);
    barracks->AddBuildProgress(4.0);
    CHECK(barracks->CurrentState() == Building::State::Constructing);

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    Building* restored = rebuilt.FindBuilding(Ids::kBarracks);
    CHECK_MSG(restored != nullptr, "it came back");
    if (restored == nullptr) return;
    CHECK(restored->CurrentState() == Building::State::Constructing);
    CHECK_NEAR(static_cast<float>(restored->BuildProgress()), 4.0f);
    CHECK_MSG(!restored->CanTrain(Ids::kSoldier), "and it still trains nothing");
}

static void testARestoredQueueTrainsAgainstItsRealTrainTime() {
    // The train times are derived from units.json and nothing serialises them.
    // A barracks restored with a queue and an empty table trains against 0.0
    // and pops a free soldier on the next step - and the round trip passes,
    // because the queue is identical on both sides.
    Board board;
    Building* barracks = board.Place(Ids::kBarracks, 2600.0f);
    barracks->EnqueueTraining(Ids::kSoldier);
    for (int i = 0; i < 8; ++i) barracks->Step(0.25);   // 2.0s of 8.0

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    int trained = 0;
    rebuilt.bus.unitTrained.Connect(
        [&trained](const std::string&, const glm::vec2&) { ++trained; });

    Building* restored = rebuilt.FindBuilding(Ids::kBarracks);
    CHECK_MSG(restored != nullptr, "it came back");
    if (restored == nullptr) return;

    restored->Step(0.25);
    CHECK_MSG(trained == 0, "a restored queue must not pop a free unit on the first step");

    // SIX seconds still owed, not eight: the progress came back with the
    // queue. Twenty-four steps of 0.25 from a saved 2.0 is exactly 8.0, so the
    // twenty-third leaves it short and the twenty-fourth completes it - which
    // is the assertion in both directions rather than a step past the boundary.
    for (int i = 0; i < 22; ++i) restored->Step(0.25);
    CHECK_MSG(trained == 0, "and it still owes a quarter second");
    restored->Step(0.25);
    CHECK_EQ(trained, 1);
}

static void testAShrunkProfileClampsARestoredBuildingRatherThanOverfillingIt() {
    // Owned meta is re-derived on restore, so a player who reset their Armory
    // has a smaller Town Hall now. A saved 1300 onto a 1000-point cap would
    // leave it above its own bar forever.
    Board board;
    board.profile.SetMetaLevel("fortified_halls", 2);
    Building* hall = board.Place(Ids::kTownHall, 1500.0f);
    CHECK_EQ(hall->Stats().maxHp, 1300);
    CHECK_EQ(hall->Hp(), 1300);

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;   // a fresh, empty profile
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    Building* restored = rebuilt.FindBuilding(Ids::kTownHall);
    CHECK_MSG(restored != nullptr, "it came back");
    if (restored == nullptr) return;
    CHECK_EQ(restored->Stats().maxHp, 1000);
    CHECK_EQ(restored->Hp(), 1000);
}

static void testARestoreAnnouncesNothingItDidNotActuallyDo() {
    // A Continue that re-fired every building's completion would play the
    // sound and the effect for a village the player built an hour ago. It is
    // true today by accident of the constructor - only CompleteConstruction
    // emits - and nothing else would catch a future emit added to it.
    Board board;
    board.Place(Ids::kTownHall, 1500.0f);
    board.Place(Ids::kBarracks, 2600.0f);
    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    int completed = 0;
    int trained = 0;
    rebuilt.bus.buildingCompleted.Connect([&completed](Building*) { ++completed; });
    rebuilt.bus.unitTrained.Connect(
        [&trained](const std::string&, const glm::vec2&) { ++trained; });

    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    CHECK_EQ(completed, 0);
    CHECK_EQ(trained, 0);
}

// --- 9. The upgrade inversion --------------------------------------------

static void testUnitsAndBuildingsRestoreUnderOppositeRules() {
    // They are opposite, and their code sits in adjacent loops in one function.
    // A change that "fixes" one silently inverts the other, and a test in only
    // one direction blesses it.
    //
    // A unit spawned BEFORE a research keeps its old numbers - units are never
    // retroactively upgraded, so the baked block is the truth. A building comes
    // back through ForBuilding and picks the research UP, because a structure
    // the player paid for is the thing being upgraded.
    Board board;
    Unit* soldier = board.Spawn(Ids::kSoldier, 2000.0f);
    Building* hall = board.Place(Ids::kTownHall, 1500.0f);
    CHECK_EQ(soldier->Stats().damage, 8);
    CHECK_EQ(hall->Stats().maxHp, 1000);

    Upgrades::Research(wb::Shipped(), board.state, "iron_swords", {});
    Upgrades::Research(wb::Shipped(), board.state, "reinforced_walls", {});

    // Researched, but the standing entities were not handed to Research, so
    // neither has changed yet.
    CHECK_EQ(soldier->Stats().damage, 8);
    CHECK_EQ(hall->Stats().maxHp, 1000);

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    CHECK_MSG(rebuilt.units.front()->Stats().damage == 8,
              "a unit keeps the numbers it was spawned with");
    Building* restoredHall = rebuilt.FindBuilding(Ids::kTownHall);
    CHECK_MSG(restoredHall != nullptr, "the hall came back");
    if (restoredHall != nullptr) {
        CHECK_MSG(restoredHall->Stats().maxHp == 1500,
                  "and a building picks the research up, through ForBuilding");
    }
}

// --- 10. The divergences, pinned as assertions ---------------------------

static void testARestoredWorkerLosesOneTickReAcquiringItsDeposit() {
    // The documented cost of not saving the deposit index. Stated as a passing
    // assertion rather than discovered later as a position mismatch: the worker
    // stands still for the step in which its thinking tick re-acquires, and
    // moves on the next one.
    Board board;
    board.Place(Ids::kTownHall, 1500.0f);
    board.AddNode(Ids::kWood, 2000.0f, 200);
    Unit* worker = board.Spawn(Ids::kWorker, 1950.0f);

    // Drive it into Delivering with a load.
    for (int i = 0; i < 200 && worker->CurrentState() != Unit::State::Delivering; ++i) {
        worker->Step(0.2);
    }
    CHECK(worker->CurrentState() == Unit::State::Delivering);
    CHECK_MSG(worker->Carrying() > 0, "and carrying something");

    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      rebuilt, rebuilt.lane, rebuilt.projectiles);

    Unit* restored = rebuilt.units.front().get();
    const float where = restored->Position().x;

    restored->Step(0.2);
    CHECK_MSG(restored->Position().x == where,
              "step one is spent re-acquiring the deposit it did not save");

    restored->Step(0.2);
    CHECK_MSG(restored->Position().x < where, "and step two moves toward it");
}

// --- 11. The run file ----------------------------------------------------

static void testTheRunFileIsWrittenReadAndCleared() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wb_run_test.json";
    Snapshot::ClearRun(path.string());
    CHECK_MSG(!Snapshot::HasRun(path.string()), "no run to begin with");

    Board board;
    board.Place(Ids::kTownHall, 1500.0f);
    board.Spawn(Ids::kWorker, 1600.0f);
    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    CHECK_MSG(Snapshot::SaveRun(path.string(), document), "it saves");
    CHECK_MSG(Snapshot::HasRun(path.string()), "and then there is a run");

    const Value loaded = Snapshot::LoadRun(path.string());
    CHECK_MSG(Snapshot::IsValid(loaded), "which is valid");
    CHECK_EQ(static_cast<int>(loaded["units"].AsArray().size()), 1);

    Snapshot::ClearRun(path.string());
    CHECK_MSG(!Snapshot::HasRun(path.string()), "and clearing removes it");
}

static void testACorruptRunStillCountsAsARunAndStillFailsToLoad() {
    // Presence and validity are separate questions, and the original keeps them
    // separate on purpose: the menu offers Continue and the load then refuses
    // it, which is a better story than a Continue button that silently vanishes.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wb_run_corrupt.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << "{ \"version\": 1, \"units\": [ { \"id\"";
    }

    CHECK_MSG(Snapshot::HasRun(path.string()), "there is a file, so there is a run");
    CHECK_MSG(!Snapshot::IsValid(Snapshot::LoadRun(path.string())), "and it will not load");
    Snapshot::ClearRun(path.string());
}

static void testAStaleVersionIsRefusedHavingCreatedNothing() {
    Board board;
    board.Place(Ids::kTownHall, 1500.0f);
    board.Spawn(Ids::kWorker, 1600.0f);
    Value document = Snapshot::Capture(board.View(), board.state, board.director);
    document.Set("version", Value(999.0));

    Board rebuilt;
    Snapshot::RestoreReport report;
    CHECK_MSG(!Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile,
                                 rebuilt.director, rebuilt, rebuilt.lane, rebuilt.projectiles,
                                 &report),
              "a stale schema is refused");
    CHECK_MSG(rebuilt.units.empty() && rebuilt.buildings.empty(),
              "and nothing was half-built before the refusal");
}

// A sink that does the MINIMUM the contract asks for.
//
// Two mutations survived the first pass because the Board does more than a sink
// has to: it calls SetTrainTimes itself, so removing that call from Restore
// changed nothing, and it ignores the `complete` flag because FromSave sets the
// state a moment later anyway. Both are things Restore promises, and a promise
// only one caller happens not to need is a promise nothing tests.
//
// This one creates entities and nothing else, and records what it was told.
struct BareSink final : public Snapshot::RestoreSink {
    Board& board;
    std::vector<bool> completeFlags;

    explicit BareSink(Board& target) : board(target) {}

    Building* CreateBuilding(const BuildingStats& stats, bool complete,
                             const glm::vec2& position) override {
        completeFlags.push_back(complete);

        // Deliberately NOT SetTrainTimes. Restore is responsible for it, and if
        // it is not, a restored barracks trains against a time of 0.01 and pops
        // a free soldier on the next step.
        auto building = std::make_unique<Building>(stats, complete, board.state, board.bus, board);
        building->SetPosition(position);
        board.buildings.push_back(std::move(building));
        return board.buildings.back().get();
    }

    ResourceNode* CreateResourceNode(const ResourceNode& fromSave) override {
        board.nodes.push_back(std::make_unique<ResourceNode>(fromSave));
        return board.nodes.back().get();
    }

    Unit* CreateUnit(const UnitStats& stats, const glm::vec2& position) override {
        auto unit = std::make_unique<Unit>(stats, board.state, board.bus, board);
        unit->SetPosition(position);
        Unit* raw = unit.get();
        board.units.push_back(std::move(unit));
        board.lane.Register(raw);
        return raw;
    }
};

static void testRestoreSetsTheTrainTimesSoASinkNeedNot() {
    Board board;
    Building* barracks = board.Place(Ids::kBarracks, 2600.0f);
    barracks->EnqueueTraining(Ids::kSoldier);
    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    BareSink sink(rebuilt);
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      sink, rebuilt.lane, rebuilt.projectiles);

    int trained = 0;
    rebuilt.bus.unitTrained.Connect(
        [&trained](const std::string&, const glm::vec2&) { ++trained; });

    Building* restored = rebuilt.FindBuilding(Ids::kBarracks);
    CHECK_MSG(restored != nullptr, "it came back");
    if (restored == nullptr) return;

    // Without the train times a queued soldier costs 0.01 seconds and pops on
    // the first step. With them it costs eight seconds.
    for (int i = 0; i < 30; ++i) restored->Step(0.25);
    CHECK_MSG(trained == 0, "seven and a half seconds is not eight");
    for (int i = 0; i < 2; ++i) restored->Step(0.25);
    CHECK_EQ(trained, 1);
}

static void testTheSinkIsToldWhetherEachBuildingWasFinished() {
    // The flag is not decoration. FromSave sets the state a moment later, so
    // the Board cannot tell the difference - but a sink that registers a
    // deposit point at creation time can, and a real Match will. Asserted on
    // the argument rather than on its effect, because the argument IS the
    // contract.
    Board board;
    board.Place(Ids::kTownHall, 1500.0f);                      // complete
    board.Place(Ids::kBarracks, 2600.0f, /*complete=*/false);   // not
    const Value document = Snapshot::Capture(board.View(), board.state, board.director);

    Board rebuilt;
    BareSink sink(rebuilt);
    Snapshot::Restore(document, wb::Shipped(), rebuilt.state, rebuilt.profile, rebuilt.director,
                      sink, rebuilt.lane, rebuilt.projectiles);

    CHECK_EQ(static_cast<int>(sink.completeFlags.size()), 2);
    if (sink.completeFlags.size() != 2) return;

    // Buildings are captured in the caller's order, so the hall is first.
    CHECK_MSG(sink.completeFlags[0], "the Town Hall was finished");
    CHECK_MSG(!sink.completeFlags[1], "and the barracks was not");
}

static void runTests() {
    testADoubleSurvivesTheTripExactly();
    testANonFiniteNumberIsWrittenAsSomethingThatParses();
    testGarbageIsRefusedRatherThanHalfRead();

    testTheStatsBlockCarriesEveryUpgradableFieldAndNoOther();
    testEverySavedStatIsAlsoRead();
    testAScaledEnemyComesBackScaledRatherThanRebuiltFromData();

    testRestoringABalanceDoesNotReBankTheStartingBonus();
    testMetaLevelsComeFromTheProfileAndNeverFromTheFile();
    testADecidedRunComesBackDecided();
    testRestoringAnnouncesEveryBalanceItPutBack();

    testTheDirectorsProgressSurvivesAndItsCountIsRecounted();
    testANonsenseWaveIndexIsClampedAtBothEnds();

    testAGatheringUnitComesBackOnTheTreeItChoseNotTheNearestOne();
    testAnAttackTargetRelinksAcrossTypesWithNoTypeTag();
    testAReferenceToSomethingOutsideTheSceneBecomesNothing();

    testARichBoardRoundTripsThroughTextExactly();
    testEverySidIsUniqueAcrossAllThreeKinds();
    testTheLaneIsRebuiltSoUnitsCanStillFindEachOther();
    testARestoreIntoAWarmBoardDoesNotInheritTheLastRun();

    testAUnitWhoseDataRowIsGoneIsSkippedRatherThanInvented();
    testABuildingWhoseDataRowIsGoneIsSkippedRatherThanLeftAtOneHitPoint();

    testAHalfBuiltBarracksComesBackHalfBuilt();
    testARestoredQueueTrainsAgainstItsRealTrainTime();
    testAShrunkProfileClampsARestoredBuildingRatherThanOverfillingIt();
    testARestoreAnnouncesNothingItDidNotActuallyDo();

    testUnitsAndBuildingsRestoreUnderOppositeRules();
    testARestoredWorkerLosesOneTickReAcquiringItsDeposit();

    testRestoreSetsTheTrainTimesSoASinkNeedNot();
    testTheSinkIsToldWhetherEachBuildingWasFinished();

    testTheRunFileIsWrittenReadAndCleared();
    testACorruptRunStillCountsAsARunAndStillFailsToLoad();
    testAStaleVersionIsRefusedHavingCreatedNothing();
}

TEST_MAIN("test_wb_snapshot", 90)
