// The ported progression, against the original's own harnesses.
//
// Two of them: `verify_upgrades` for in-run research and `verify_meta` for the
// persistent Armory. Between them they print more computed numbers than any
// other pair in the suite - a soldier's damage going 8 to 12, a Town Hall going
// 1000 to 1500, three Armory levels costing exactly 40 + 80 + 120, and a wave-3
// loss being worth 30 renown.
//
// To re-derive:
//
//   cd /d/The-Wolf-Brigade
//   "D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe" \
//       --headless --path . res://tools/verify_upgrades.tscn
//   ...same with res://tools/verify_meta.tscn
//
// They printed, on 26 August 2026:
//
//   ok  : cost deducted (80 wood)
//   ok  : iron_swords: soldier damage 8 -> 12
//   ok  : unrelated unit (worker) unaffected
//   ok  : town hall starts at base hp (1000)
//   ok  : reinforced_walls retroactively toughens the EXISTING town hall (1500)
//   ok  : already-built unit stats NOT retroactively changed
//   ok  : next_cost at level 0 = base_cost (40)
//   ok  : renown deducted by 40 (1000 -> 960)
//   ok  : next_cost rises with level (80)
//   ok  : next_cost = -1 when maxed
//   ok  : spent exactly 40+80+120 = 240
//   ok  : Sharper Axes L2 -> worker gather_rate +0.6 (flat, per-level)
//   ok  : Veteran Soldiers L3 -> soldier max_hp +45
//   ok  : in-run research (+0.5) AND meta L1 (+0.3) both apply -> +0.8
//   ok  : fresh run: Deeper Coffers L2 -> +80 starting wood
//   ok  : Fortified Halls L2 -> town_hall max_hp +300
//   ok  : player path buffed by meta (90) but enemy path is not (60)
//   ok  : wave 1 -> 8 renown
//   ok  : wave 3 loss -> 30 renown (24 + 6 growth)
//   ok  : wave 5 WIN -> 120 (60 + 60 victory bonus)
//
// The Armory SCREEN itself (`verify_armory`) needs a window and has no headless
// oracle at all; so does `verify_settings`. Those are UI slices and will have to
// be verified some other way - said here rather than left to be assumed.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "sim/Building.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/Progression.hpp"
#include "sim/World.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

constexpr float kGroundY = 800.0f;

// A world with nothing in it. Progression does not ask the world anything; the
// buildings it upgrades are handed to it directly.
class EmptyWorld final : public World {
public:
    ResourceNode* NearestHarvestable(float) const override { return nullptr; }
    int NearestDeposit(float) const override { return -1; }
    bool DepositExists(int) const override { return false; }
    glm::vec2 DepositPosition(int) const override { return glm::vec2(0.0f); }
    Building* NearestUnfinishedBuilding(const std::string&, float) const override {
        return nullptr;
    }
    Unit* NearestEnemyUnit(const std::string&, float, float) const override { return nullptr; }
    Damageable* NearestEnemyBuilding(const std::string&, float) const override { return nullptr; }
    ProjectilePool* Projectiles() override { return nullptr; }
};

struct Run {
    EventBus bus;
    GameState state{wb::Shipped(), bus};
    Profile profile;
    EmptyWorld world;
    std::vector<std::unique_ptr<Building>> buildings;

    Run() { state.Reset(); }

    Building* Place(const std::string& id, float x) {
        auto building = std::make_unique<Building>(
            Upgrades::ForBuilding(wb::Shipped(), state, profile, id), true, state, bus, world);
        building->SetPosition(glm::vec2(x, kGroundY));
        Building* raw = building.get();
        buildings.push_back(std::move(building));
        return raw;
    }

    std::vector<Building*> Standing() {
        std::vector<Building*> all;
        for (const auto& building : buildings) all.push_back(building.get());
        return all;
    }
};

// --- 1. Research ---------------------------------------------------------

void testResearchingAnUpgradeCostsItsPriceAndHappensOnce() {
    Run run;
    const int wood = run.state.Amount(Ids::kWood);

    CHECK_MSG(Upgrades::CanResearch(wb::Shipped(), run.state, "sharper_axes"),
              "an affordable upgrade with no prerequisites is researchable");
    CHECK_MSG(Upgrades::Research(wb::Shipped(), run.state, "sharper_axes", run.Standing()),
              "and researching it succeeds");

    CHECK_MSG(run.state.IsResearched("sharper_axes"), "it is marked");
    CHECK_EQ(run.state.Amount(Ids::kWood), wood - 80);

    // Twice is refused, and free. A double-click on a research button must not
    // charge the player again for something they already own.
    CHECK_MSG(!Upgrades::CanResearch(wb::Shipped(), run.state, "sharper_axes"),
              "already researched is not researchable");
    CHECK_MSG(!Upgrades::Research(wb::Shipped(), run.state, "sharper_axes", run.Standing()),
              "and a second research is rejected");
    CHECK_EQ(run.state.Amount(Ids::kWood), wood - 80);
}

void testPrerequisitesGateBothAvailabilityAndResearch() {
    Run run;

    // masterwork_bows requires fletching.
    CHECK_MSG(!Upgrades::CanResearch(wb::Shipped(), run.state, "masterwork_bows"),
              "an unmet prerequisite makes it unresearchable");
    CHECK_MSG(!Upgrades::IsAvailable(wb::Shipped(), run.state, "masterwork_bows"),
              "and hides it from the menu");

    CHECK_MSG(Upgrades::Research(wb::Shipped(), run.state, "fletching", run.Standing()),
              "research the prerequisite");
    CHECK_MSG(Upgrades::IsAvailable(wb::Shipped(), run.state, "masterwork_bows"),
              "now it shows");
    CHECK_MSG(Upgrades::CanResearch(wb::Shipped(), run.state, "masterwork_bows"),
              "and can be bought");
}

void testAvailableAndAffordableAreDifferentQuestions() {
    // The UI shows what is AVAILABLE and greys out what cannot be paid for. A
    // port that conflated them would hide upgrades the player is saving up for,
    // which is most of the reason to look at the menu.
    Run run;
    run.state.TrySpend({{Ids::kWood, 290}});   // 10 left

    CHECK_MSG(Upgrades::IsAvailable(wb::Shipped(), run.state, "sharper_axes"),
              "it is available: nothing gates it");
    CHECK_MSG(!Upgrades::CanResearch(wb::Shipped(), run.state, "sharper_axes"),
              "and unaffordable: 10 wood is not 80");
}

void testAnUpgradeNobodyAuthoredIsNotFree() {
    Run run;
    CHECK_MSG(!Upgrades::CanResearch(wb::Shipped(), run.state, "laser_swords"),
              "a typo in a menu button must not be a free upgrade");
    CHECK_MSG(!Upgrades::Research(wb::Shipped(), run.state, "laser_swords", run.Standing()),
              "and cannot be researched");
}

// --- 2. Effects on future spawns -----------------------------------------

void testAUnitUpgradeChangesTheNextSpawnAndNotTheOneAlreadyOut() {
    Run run;

    const UnitStats before =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kSoldier);
    CHECK_EQ(before.damage, 8);

    Upgrades::Research(wb::Shipped(), run.state, "iron_swords", run.Standing());

    const UnitStats after =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kSoldier);
    CHECK_EQ(after.damage, 12);

    // The block handed out before the research is untouched. Units are never
    // retroactively upgraded - the player trains new ones - and a stat block
    // shared between spawns instead of copied would make that impossible.
    CHECK_EQ(before.damage, 8);

    // And nothing else moved. iron_swords names the soldier, so a worker must
    // come out exactly as the file says.
    const UnitStats worker =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kWorker);
    CHECK_EQ(worker.damage, 2);
    CHECK_NEAR(worker.gatherRate, 1.0f);
}

void testABuildingUpgradeAppliesToWhatIsAlreadyStanding() {
    // The asymmetry, and it is deliberate. A structure the player already paid
    // for is the thing they are upgrading; a soldier already in the field is
    // not.
    Run run;
    Building* hall = run.Place(Ids::kTownHall, 1500.0f);
    CHECK_EQ(hall->Hp(), 1000);
    CHECK_EQ(hall->Stats().maxHp, 1000);

    Upgrades::Research(wb::Shipped(), run.state, "reinforced_walls", run.Standing());

    CHECK_EQ(hall->Stats().maxHp, 1500);
    CHECK_EQ(hall->Hp(), 1500);

    // And a building placed afterwards gets it from the spawn path instead.
    const BuildingStats future =
        Upgrades::ForBuilding(wb::Shipped(), run.state, run.profile, Ids::kTownHall);
    CHECK_EQ(future.maxHp, 1500);

    // reinforced_walls names the barracks too, at a different number.
    const BuildingStats barracks =
        Upgrades::ForBuilding(wb::Shipped(), run.state, run.profile, Ids::kBarracks);
    CHECK_EQ(barracks.maxHp, 700);   // 500 + 200
}

void testARetroactiveUpgradeHealsWhatItToughens() {
    Run run;
    Building* hall = run.Place(Ids::kTownHall, 1500.0f);
    hall->TakeDamage(400);
    CHECK_EQ(hall->Hp(), 600);

    Upgrades::Research(wb::Shipped(), run.state, "reinforced_walls", run.Standing());
    CHECK_EQ(hall->Stats().maxHp, 1500);
    CHECK_EQ(hall->Hp(), 1100);
}

void testARuinIsNotUpgraded() {
    Run run;
    Building* hall = run.Place(Ids::kTownHall, 1500.0f);
    hall->Destroy();

    Upgrades::Research(wb::Shipped(), run.state, "reinforced_walls", run.Standing());
    CHECK_EQ(hall->Stats().maxHp, 1000);
}

// --- 3. The field mapping GDScript gets from reflection ------------------

void testEveryEffectFieldTheDataNamesIsAFieldSomethingHas() {
    // GDScript addresses these by name and warns at RUN TIME when one does not
    // exist. C++ has no reflection, so the mapping is written out - and that
    // turns the same question into one this test can ask about the whole file
    // at once, which is strictly better than a push_warning nobody reads.
    const GameData& data = wb::Shipped();

    const auto checkEffects = [&data](const Supersonic::Json::Value& definitions) {
        int missing = 0;
        for (const auto& [id, definition] : definitions.AsObject()) {
            for (const auto& [entity, mods] : definition["effects"].AsObject()) {
                // The pseudo-entity is not a stat block and has no fields.
                if (entity == "starting_resources") continue;

                const bool isUnit = data.Unit(entity).IsObject() &&
                                    !data.Unit(entity).AsObject().empty();
                for (const auto& [field, delta] : mods.AsObject()) {
                    (void)delta;
                    const bool ok = isUnit ? UnitStats::HasField(field)
                                           : BuildingStats::HasField(field);
                    if (!ok) {
                        ++missing;
                        CHECK_MSG(false, id + " effect on " + entity +
                                             " names a field nobody has: " + field);
                    }
                }
            }
        }
        return missing;
    };

    CHECK_EQ(checkEffects(data.Upgrades()), 0);
    CHECK_EQ(checkEffects(data.MetaUpgrades()), 0);
}

void testAFieldNobodyHasIsReportedRatherThanSwallowed() {
    UnitStats worker = UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker));
    CHECK_MSG(worker.ApplyDelta("damage", 5.0), "damage is a field a unit has");
    CHECK_EQ(worker.damage, 7);

    CHECK_MSG(!worker.ApplyDelta("build_time", 5.0), "a building's field is not");
    CHECK_MSG(!UnitStats::HasField("morale"), "and neither is one nobody invented");

    BuildingStats hall = BuildingStats::FromJson(Ids::kTownHall, wb::Shipped().Building(Ids::kTownHall));
    CHECK_MSG(hall.ApplyDelta("max_hp", 100.0), "max_hp is a field a building has");
    CHECK_MSG(!hall.ApplyDelta("gather_rate", 1.0), "a worker's field is not");
}

// --- 4. The Armory -------------------------------------------------------

void testTheArmoryChargesRisingPricesAndStopsAtTheMax() {
    Run run;
    run.profile.AddRenown(1000);

    CHECK_EQ(Meta::NextCost(wb::Shipped(), run.profile, "sharper_axes"), 40);
    CHECK_MSG(Meta::CanBuy(wb::Shipped(), run.profile, "sharper_axes"), "1000 renown buys it");
    CHECK_MSG(Meta::Buy(wb::Shipped(), run.profile, "sharper_axes"), "and the buy succeeds");

    CHECK_EQ(run.profile.MetaLevel("sharper_axes"), 1);
    CHECK_EQ(run.profile.Renown(), 960);
    CHECK_EQ(Meta::NextCost(wb::Shipped(), run.profile, "sharper_axes"), 80);

    CHECK_MSG(Meta::Buy(wb::Shipped(), run.profile, "sharper_axes"), "level 2");
    CHECK_MSG(Meta::Buy(wb::Shipped(), run.profile, "sharper_axes"), "level 3");

    CHECK_MSG(Meta::IsMaxed(wb::Shipped(), run.profile, "sharper_axes"), "maxed at three");
    CHECK_EQ(Meta::NextCost(wb::Shipped(), run.profile, "sharper_axes"), -1);
    CHECK_MSG(!Meta::CanBuy(wb::Shipped(), run.profile, "sharper_axes"), "nothing left to buy");
    CHECK_MSG(!Meta::Buy(wb::Shipped(), run.profile, "sharper_axes"), "and buying is a no-op");

    // 40 + 80 + 120.
    CHECK_EQ(run.profile.Renown(), 1000 - 240);
}

void testAnUnaffordableBuyChangesNothing() {
    Run run;
    run.profile.AddRenown(10);

    CHECK_MSG(!Meta::CanBuy(wb::Shipped(), run.profile, "veteran_soldiers"),
              "10 renown is not 60");
    CHECK_MSG(!Meta::Buy(wb::Shipped(), run.profile, "veteran_soldiers"), "so it is a no-op");
    CHECK_EQ(run.profile.Renown(), 10);
    CHECK_EQ(run.profile.MetaLevel("veteran_soldiers"), 0);
}

void testAnUnknownArmoryUpgradeIsNotFree() {
    Run run;
    run.profile.AddRenown(1000);
    CHECK_EQ(Meta::NextCost(wb::Shipped(), run.profile, "immortality"), -1);
    CHECK_MSG(!Meta::Buy(wb::Shipped(), run.profile, "immortality"), "nothing to buy");
    CHECK_EQ(run.profile.Renown(), 1000);
}

// --- 5. Owned levels, applied flat and per level -------------------------

void testOwnedLevelsAddFlatlyAndPerLevel() {
    Run run;
    run.profile.SetMetaLevel("sharper_axes", 2);

    const UnitStats worker =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kWorker);

    // +0.3 a level, twice. Not compounded: three levels of a 15hp bonus is 45,
    // and at any real level a multiplicative reading would be a different game.
    CHECK_NEAR(worker.gatherRate, 1.6f);

    run.profile.SetMetaLevel("veteran_soldiers", 3);
    const UnitStats soldier =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kSoldier);
    CHECK_EQ(soldier.maxHp, 60 + 45);

    // And level zero is the baseline, exactly.
    run.profile.SetMetaLevel("sharper_axes", 0);
    run.profile.SetMetaLevel("veteran_soldiers", 0);
    const UnitStats base =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kWorker);
    CHECK_NEAR(base.gatherRate, 1.0f);
}

void testInRunResearchAndOwnedLevelsBothApply() {
    // Both are flat and additive, which is why the order they land in does not
    // matter - and is worth checking, because a port that overwrote rather than
    // added would silently drop whichever went first.
    Run run;
    run.profile.SetMetaLevel("sharper_axes", 1);            // +0.3
    Upgrades::Research(wb::Shipped(), run.state, "sharper_axes", run.Standing());   // +0.5

    const UnitStats worker =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kWorker);
    CHECK_NEAR(worker.gatherRate, 1.8f);
}

void testMetaNeverReachesAnEnemy() {
    // There is no flag to forget here. Enemies are built by UnitStats::FromJson
    // and player units by Upgrades::ForUnit, and only one of those two calls
    // Meta - so the separation is a function nobody calls rather than a
    // condition somebody could get wrong.
    Run run;
    run.profile.SetMetaLevel("veteran_soldiers", 2);

    const UnitStats player =
        Upgrades::ForUnit(wb::Shipped(), run.state, run.profile, Ids::kSoldier);
    CHECK_EQ(player.maxHp, 90);

    const UnitStats enemy = UnitStats::FromJson(Ids::kRaider, wb::Shipped().Unit(Ids::kRaider));
    CHECK_EQ(enemy.maxHp, 40);
}

void testOwnedLevelsToughenABuildingToo() {
    Run run;
    run.profile.SetMetaLevel("fortified_halls", 2);

    const BuildingStats hall =
        Upgrades::ForBuilding(wb::Shipped(), run.state, run.profile, Ids::kTownHall);
    CHECK_EQ(hall.maxHp, 1000 + 300);
}

void testTheStartingResourceBonusIsReadOnlyByAFreshRun() {
    Run run;
    run.profile.SetMetaLevel("deeper_coffers", 2);

    CHECK_EQ(Meta::StartingResourceBonus(wb::Shipped(), run.profile, Ids::kWood), 80);
    CHECK_EQ(Meta::StartingResourceBonus(wb::Shipped(), run.profile, Ids::kFood), 0);

    // And it is NOT a stat: applying the pseudo-entity to a unit does nothing,
    // because "starting_resources" is not a unit id and no unit is called it.
    UnitStats worker = UnitStats::FromJson(Ids::kWorker, wb::Shipped().Unit(Ids::kWorker));
    Meta::Apply(wb::Shipped(), run.profile, worker, Ids::kWorker);
    CHECK_NEAR(worker.gatherRate, 1.0f);
}

// --- 6. What a run is worth ----------------------------------------------

void testARunIsWorthWhatTheOriginalSaidItWas() {
    const GameData& data = wb::Shipped();

    // per_wave 8, growth 2, victory bonus 60.
    CHECK_EQ(Meta::RunEndRenown(data, 0, false), 0);
    CHECK_EQ(Meta::RunEndRenown(data, 1, false), 8);
    CHECK_EQ(Meta::RunEndRenown(data, 3, false), 30);    // 24 + 6
    CHECK_EQ(Meta::RunEndRenown(data, 5, true), 120);    // 60 + 60

    // A negative wave is zero rather than a debt. Nothing produces one, which
    // is exactly why it is worth clamping - the day something does, it must not
    // take renown away.
    CHECK_EQ(Meta::RunEndRenown(data, -3, false), 0);
}

void testAwardingBanksItAndReportsIt() {
    Run run;
    CHECK_EQ(Meta::AwardRunEnd(wb::Shipped(), run.profile, 3, false), 30);
    CHECK_EQ(run.profile.Renown(), 30);

    // A worthless run banks nothing rather than zero, which is the same
    // outcome and one fewer write to disk.
    CHECK_EQ(Meta::AwardRunEnd(wb::Shipped(), run.profile, 0, false), 0);
    CHECK_EQ(run.profile.Renown(), 30);
}

// --- 7. Persistence ------------------------------------------------------

void testAProfileSurvivesBeingWrittenAndReadBack() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wb_profile_test.json";
    std::filesystem::remove(path);

    {
        Profile profile;
        profile.AddRenown(100);
        profile.SetMetaLevel("sharper_axes", 2);
        profile.SetMetaLevel("deeper_coffers", 1);
        CHECK_MSG(profile.Save(path.string()), "a profile must save");
    }

    Profile loaded;
    CHECK_MSG(loaded.Load(path.string()), "and load");
    CHECK_EQ(loaded.Renown(), 100);
    CHECK_EQ(loaded.MetaLevel("sharper_axes"), 2);
    CHECK_EQ(loaded.MetaLevel("deeper_coffers"), 1);

    // An upgrade nobody has bought reads as zero, not as absent. Every Armory
    // entry starts unowned and the UI has to be able to ask about all of them.
    CHECK_EQ(loaded.MetaLevel("veteran_soldiers"), 0);

    std::filesystem::remove(path);
}

void testAMissingProfileIsANewPlayerRatherThanAFailure() {
    Profile profile;
    profile.AddRenown(500);

    CHECK_MSG(!profile.Load("no/such/profile.json"), "it says it found nothing");
    CHECK_EQ(profile.Renown(), 0);
    CHECK_EQ(profile.MetaLevel("sharper_axes"), 0);
}

void testACorruptProfileDoesNotTakeTheRenownWithIt() {
    // A half-written file - a crash mid-save, a full disk. It must not be read
    // as a profile with nonsense in it.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wb_profile_broken.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << "{ \"renown\": 100, \"meta_lev";
    }

    Profile profile;
    CHECK_MSG(!profile.FromJson("{ \"renown\": 100, \"meta_lev"), "a truncated profile is refused");
    CHECK_MSG(!profile.Load(path.string()), "and so is the file holding one");

    std::filesystem::remove(path);
}

} // namespace

static void runTests() {
    testResearchingAnUpgradeCostsItsPriceAndHappensOnce();
    testPrerequisitesGateBothAvailabilityAndResearch();
    testAvailableAndAffordableAreDifferentQuestions();
    testAnUpgradeNobodyAuthoredIsNotFree();

    testAUnitUpgradeChangesTheNextSpawnAndNotTheOneAlreadyOut();
    testABuildingUpgradeAppliesToWhatIsAlreadyStanding();
    testARetroactiveUpgradeHealsWhatItToughens();
    testARuinIsNotUpgraded();

    testEveryEffectFieldTheDataNamesIsAFieldSomethingHas();
    testAFieldNobodyHasIsReportedRatherThanSwallowed();

    testTheArmoryChargesRisingPricesAndStopsAtTheMax();
    testAnUnaffordableBuyChangesNothing();
    testAnUnknownArmoryUpgradeIsNotFree();

    testOwnedLevelsAddFlatlyAndPerLevel();
    testInRunResearchAndOwnedLevelsBothApply();
    testMetaNeverReachesAnEnemy();
    testOwnedLevelsToughenABuildingToo();
    testTheStartingResourceBonusIsReadOnlyByAFreshRun();

    testARunIsWorthWhatTheOriginalSaidItWas();
    testAwardingBanksItAndReportsIt();

    testAProfileSurvivesBeingWrittenAndReadBack();
    testAMissingProfileIsANewPlayerRatherThanAFailure();
    testACorruptProfileDoesNotTakeTheRenownWithIt();
}

TEST_MAIN("test_wb_progression", 70)
