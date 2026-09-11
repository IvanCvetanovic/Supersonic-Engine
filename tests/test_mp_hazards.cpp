// Hazards: what kills the player, and where.
//
// Pinned exactly, because the files and the remake's code say it: chapter 1's
// four hazards and their boxes. None carries a trigger_size, so each kills in
// LevelRuntime's 16 px fallback box at its node, not in the shape the converter
// gives it (Hazards.hpp). What happens is judged on the tick: the player dies
// entering the box, and not beside it where level5's death_area octagon reaches
// but its box does not. Only the player can die - Hazards::State::Tick is given
// the player and nothing else - so that is not tested here.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Hazards.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-hazards", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void PutPlayer(Run& run, const glm::dvec2& atPx) {
    auto& transform = run.registry.get<TransformComponent>(run.level.player);
    const glm::vec3 at = Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);
    run.registry.get<RigidBodyComponent>(run.level.player).velocity = glm::vec3(0.0f);
}

void ChapterOnesFourHazards() {
    // Read off the level files with grep: a death_area in level5, destroyiers in
    // levels 23 and 24, none with a trigger_size.
    struct Expected {
        const char* level;
        const char* name;
        glm::dvec2 at;
    };
    const Expected expected[] = {
        {"level5", "death_area_ent_800", {513.0, 104.0}},
        {"level23", "destroyier_ent_1051", {448.0, 344.0}},
        {"level23", "destroyier_ent_1074", {64.0, 390.0}},
        {"level24", "destroyier_ent_1195", {448.0, 504.0}},
    };
    for (const Expected& hazard : expected) {
        Run run;
        if (!Begin(hazard.level, run)) continue;
        const Hazards::Hazard* found = run.level.hazards.FindHazard(hazard.name);
        CHECK_MSG(found != nullptr, std::string(hazard.level) + " has " + hazard.name);
        if (found == nullptr) continue;
        const glm::dvec2 centre = Units::ToPixels(glm::vec3(found->box.centre, 0.0f));
        const glm::dvec2 size = glm::dvec2(found->box.half) * 2.0 * Units::kPixelsPerMetre;
        CHECK_MSG(glm::distance(centre, hazard.at) < 1e-3 && std::fabs(size.x - 16.0) < 1e-3 &&
                      std::fabs(size.y - 16.0) < 1e-3,
                  std::string(hazard.name) + ": the 16 px fallback at its node");
    }
    Run level5;
    if (Begin("level5", level5)) CHECK_EQ(level5.level.hazards.hazards.size(), std::size_t{1});
}

void TheBoxKillsAndTheOctagonDoesNot() {
    // Beside the box, where the converter's 60 x 16 octagon still reaches (x from
    // 483 to 543): the player lives.
    {
        Run run;
        if (!Begin("level5", run)) return;
        const double half = run.data.tuning.widthPx * 0.5;
        PutPlayer(run, glm::dvec2(513.0 - 8.0 - 2.0 - half, 104.0));
        Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
        CHECK_MSG(!run.level.hazards.playerDied, "beside the box, inside the octagon, the player lives");
    }
    // In the box: it dies, on that tick, and says where.
    {
        Run run;
        if (!Begin("level5", run)) return;
        PutPlayer(run, glm::dvec2(513.0, 104.0));
        Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
        CHECK(run.level.hazards.playerDied);
        CHECK(run.level.hazards.killedBy == "death_area_ent_800");
    }
}

void runTests() {
    ChapterOnesFourHazards();
    TheBoxKillsAndTheOctagonDoesNot();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_hazards: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_hazards", 10);
}
