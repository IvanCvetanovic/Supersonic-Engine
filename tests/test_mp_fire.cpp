// Fire agents, burning crates and barrel bombs.
//
// What is pinned here is the DECODE, and it disagrees with the remake in every
// number. Fire.hpp cites where each comes from; the three that a test can tell
// apart are:
//
//   reach   32, the fire agent's GetSize().x, centre to centre. The remake reads
//           the entity's 38 x 38 collision box and halves it to 24, so a crate
//           28 px away burns in the original and does not in the remake. That
//           exact distance is tested, because it is the only one that separates
//           the two readings.
//   burn    1000 ms before a crate is taken away; the remake invents 2000.
//   blast   80 px, against real colliders; the remake invents 96.
//
// And one thing that is not a number: fire kills the player, a blast does not.
// The blast's filter is isBreakableOrExplosiveOrBurnable, which excludes
// characters, and no main_char placement carries any of those flags - so although
// the bomb passes killPlayer true, the player is never in the grabbed set. Both
// directions are tested, because that asymmetry is exactly what a remake smooths
// away without noticing.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Fire.hpp"
#include "sim/Game.hpp"
#include "sim/Units.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// level9a carries both a fire agent and burnable crates; level12a three barrel
// bombs within a blast of each other.
constexpr const char* kFireLevel = "level9a";
constexpr const char* kBombLevel = "level12a";

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-fire", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

// Puts a body exactly this far from a point, along +x, and holds nothing: one
// tick of gravity moves a crate about 0.27 px, well inside the margins used here.
void PutAt(entt::registry& registry, entt::entity body, const glm::dvec2& atPx) {
    auto& transform = registry.get<TransformComponent>(body);
    const glm::vec3 world = Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(world.x, world.y, transform.position.z);
}

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-fire";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string WriteRules(const char* name, const std::string& body) {
    const std::filesystem::path path = Scratch() / name;
    std::ofstream file(path, std::ios::trunc);
    file << body;
    return path.string();
}

void TheDecodedNumbersAreNotTheRemakesGuesses() {
    Fire::Rules rules;
    std::string error;
    CHECK_MSG(Fire::LoadRules(kPortData + "/fire.json", rules, error), kPortData + "/fire.json: " + error);

    CHECK_MSG(::test::nearly(static_cast<float>(rules.reachPx), 32.0f),
              "a fire agent reaches its sprite's 32, not the remake's halved collision box of 24");
    CHECK_MSG(::test::nearly(static_cast<float>(rules.burnMs), 1000.0f),
              "BURN_TIME is 1000 ms, not the remake's invented 2000");
    CHECK_MSG(::test::nearly(static_cast<float>(rules.blastPx), 80.0f),
              "a blast grabs at 80, not the remake's invented 96");

    // Only a crate fades and goes: manageBurnable's one caller is crateCallback.
    // A shock_agent carries the burnable flag and must NOT be in this list, or
    // burning one would delete it and change its level's puzzle.
    bool sawCrate = false;
    bool sawShockAgent = false;
    for (const std::string& name : rules.fadeNames) {
        if (name == "crate_no_emissive") sawCrate = true;
        if (name == "shock_agent") sawShockAgent = true;
    }
    CHECK_MSG(sawCrate, "the crates fade and are taken away");
    CHECK_MSG(!sawShockAgent, "a shock agent carries the flag but is not one of them");
}

void TheReaderRefusesWhatWouldBeSilent() {
    Fire::Rules rules;
    std::string error;

    const std::string zeroReach = WriteRules("zero-reach.json", R"({
      "fire": {"reach_px": 0.0},
      "burn": {"time_ms": 1000.0, "fade_names": ["crate"]},
      "blast": {"radius_px": 80.0}
    })");
    CHECK_MSG(!Fire::LoadRules(zeroReach, rules, error), "a reach of nothing is refused");

    const std::string zeroBurn = WriteRules("zero-burn.json", R"({
      "fire": {"reach_px": 32.0},
      "burn": {"time_ms": 0.0, "fade_names": ["crate"]},
      "blast": {"radius_px": 80.0}
    })");
    CHECK_MSG(!Fire::LoadRules(zeroBurn, rules, error), "and a burn that takes no time");

    const std::string zeroBlast = WriteRules("zero-blast.json", R"({
      "fire": {"reach_px": 32.0},
      "burn": {"time_ms": 1000.0, "fade_names": ["crate"]},
      "blast": {"radius_px": 0.0}
    })");
    CHECK_MSG(!Fire::LoadRules(zeroBlast, rules, error), "and a blast that reaches nothing");

    // An empty list makes burning a flag and nothing more, in every level at once.
    const std::string noFades = WriteRules("no-fades.json", R"({
      "fire": {"reach_px": 32.0},
      "burn": {"time_ms": 1000.0, "fade_names": []},
      "blast": {"radius_px": 80.0}
    })");
    CHECK_MSG(!Fire::LoadRules(noFades, rules, error), "and a burn that never removes anything");
}

void WhatTheLevelsCarry() {
    Run fire;
    if (!Begin(kFireLevel, fire)) return;
    CHECK_MSG(!fire.level.fire.agents.empty(), "level9a places at least one fire agent");
    CHECK_MSG(!fire.level.fire.burnables.empty(), "and burnable crates");

    bool anyFades = false;
    for (const Fire::Burnable& burnable : fire.level.fire.burnables) {
        if (burnable.fades) anyFades = true;
        CHECK_MSG(!burnable.burned, "and nothing is alight before anything reaches it");
    }
    CHECK_MSG(anyFades, "and a crate among them is one that burns away");
    std::printf("  %s: %d fire agent(s), %d burnable(s)\n", kFireLevel,
                static_cast<int>(fire.level.fire.agents.size()),
                static_cast<int>(fire.level.fire.burnables.size()));

    Run bombs;
    if (!Begin(kBombLevel, bombs)) return;
    CHECK_MSG(bombs.level.fire.bombs.size() >= 2, "level12a places several barrel bombs");
    for (const Fire::Bomb& bomb : bombs.level.fire.bombs) {
        CHECK_MSG(!bomb.requested && !bomb.blown, "and none is asked to go off before anything asks it");
    }
    std::printf("  %s: %d bomb(s)\n", kBombLevel, static_cast<int>(bombs.level.fire.bombs.size()));
}

// THE test that separates the decode from the remake's reading: 28 px is inside
// the original's 32 and outside the remake's 24.
void AFireAgentReachesItsSpritesWidth() {
    Run run;
    if (!Begin(kFireLevel, run)) return;
    if (run.level.fire.agents.empty() || run.level.fire.burnables.empty()) {
        CHECK_MSG(false, "level9a carries a fire agent and a crate");
        return;
    }

    const glm::dvec2 agentPx = run.level.fire.agents.front().atPx;
    const std::string name = run.level.fire.burnables.front().name;
    const entt::entity crate = run.level.fire.burnables.front().body;

    PutAt(run.registry, crate, agentPx + glm::dvec2(28.0, 0.0));
    Tick(run);
    const Fire::Burnable* inReach = run.level.fire.FindBurnable(name);
    CHECK_MSG(inReach != nullptr && inReach->burned,
              "a crate 28 px away burns, which the remake's halved 24 would not reach");

    // And beyond it, nothing. A fresh level, because the first one is alight.
    Run beyond;
    if (!Begin(kFireLevel, beyond)) return;
    const entt::entity other = beyond.level.fire.burnables.front().body;
    PutAt(beyond.registry, other, beyond.level.fire.agents.front().atPx + glm::dvec2(40.0, 0.0));
    Tick(beyond);
    const Fire::Burnable* outOfReach = beyond.level.fire.FindBurnable(name);
    CHECK_MSG(outOfReach != nullptr && !outOfReach->burned, "and one 40 px away does not");
}

void ABurningCrateGoesAfterItsSecond() {
    Run run;
    if (!Begin(kFireLevel, run)) return;
    if (run.level.fire.agents.empty() || run.level.fire.burnables.empty()) return;

    const std::string name = run.level.fire.burnables.front().name;
    const entt::entity crate = run.level.fire.burnables.front().body;
    PutAt(run.registry, crate, run.level.fire.agents.front().atPx);
    Tick(run);

    const Fire::Burnable* lit = run.level.fire.FindBurnable(name);
    CHECK_MSG(lit != nullptr && lit->burned, "a crate in the flame catches");
    CHECK_MSG(run.registry.valid(crate), "and is still there while it burns");

    // Half of BURN_TIME: still there. That is what makes burning a race.
    for (int tick = 0; tick < 30; ++tick) Tick(run);
    const Fire::Burnable* half = run.level.fire.FindBurnable(name);
    CHECK_MSG(half != nullptr && !half->gone, "half a second in it is still there");

    for (int tick = 0; tick < 31; ++tick) Tick(run);
    const Fire::Burnable* done = run.level.fire.FindBurnable(name);
    CHECK_MSG(done != nullptr && done->gone, "and after its second it is gone");
    CHECK_MSG(!run.registry.valid(crate), "its body taken away, collision and all");
    std::printf("  %s: a crate burned away after %.0f ms\n", kFireLevel, run.data.fire.burnMs);
}

void FireKillsThePlayerAndABlastDoesNot() {
    // Fire does.
    Run burned;
    if (!Begin(kFireLevel, burned)) return;
    if (burned.level.fire.agents.empty()) return;
    const glm::dvec2 agentPx = burned.level.fire.agents.front().atPx;
    for (int tick = 0; tick < 3 && !burned.level.hazards.playerDied; ++tick) {
        PutAt(burned.registry, burned.level.player, agentPx);
        Tick(burned);
    }
    CHECK_MSG(burned.level.fire.playerKilled, "a player standing in the flame burns");
    CHECK_MSG(burned.level.hazards.playerDied, "and dies as a hazard kills it");
    CHECK_MSG(burned.level.hazards.killedBy == burned.level.fire.killedBy,
              "named by the fire agent, got '" + burned.level.hazards.killedBy + "'");

    // A blast does not, and this is the original's own answer.
    Run blown;
    if (!Begin(kBombLevel, blown)) return;
    if (blown.level.fire.bombs.empty()) return;
    const entt::entity bomb = blown.level.fire.bombs.front().body;
    const glm::dvec2 bombPx = Units::ToPixels(blown.registry.get<TransformComponent>(bomb).position);
    PutAt(blown.registry, blown.level.player, bombPx);
    blown.level.fire.bombs.front().requested = true;
    Tick(blown);
    Tick(blown);

    CHECK_MSG(blown.level.fire.blasts > 0, "the bomb the player stands on goes off");
    CHECK_MSG(!blown.level.fire.playerKilled,
              "and the player is untouched: the grabber takes breakable, explosive and burnable, and a character is "
              "none of those");
    CHECK_MSG(!blown.level.hazards.playerDied, "so nothing kills it");
    std::printf("  %s: %d blast(s) under the player, which survived\n", kBombLevel, blown.level.fire.blasts);
}

void AChainRipplesATickAtATime() {
    Run run;
    if (!Begin(kBombLevel, run)) return;
    if (run.level.fire.bombs.size() < 2) return;

    run.level.fire.bombs.front().requested = true;
    Tick(run);
    CHECK_EQ(run.level.fire.blasts, 1);

    int asked = 0;
    for (const Fire::Bomb& bomb : run.level.fire.bombs) {
        if (bomb.requested && !bomb.blown) ++asked;
    }
    CHECK_MSG(asked > 0, "the first blast asks its neighbours rather than setting them off itself");

    Tick(run);
    CHECK_MSG(run.level.fire.blasts > 1, "and they go off on the tick after, not the same one");
    std::printf("  %s: a chain of %d went off, one tick apart\n", kBombLevel, run.level.fire.blasts);
}

void runTests() {
    TheDecodedNumbersAreNotTheRemakesGuesses();
    TheReaderRefusesWhatWouldBeSilent();
    WhatTheLevelsCarry();
    AFireAgentReachesItsSpritesWidth();
    ABurningCrateGoesAfterItsSecond();
    FireKillsThePlayerAndABlastDoesNot();
    AChainRipplesATickAtATime();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_fire SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_fire", 25);
}
