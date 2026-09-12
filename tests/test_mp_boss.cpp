// Chapter 1's boss: the beholder of level 1-32 (level31), as the original's
// script plays it (Boss.hpp; boss.json cites the bytes of android_game.bin).
//
// Pinned exactly, because the script says it: boss.json's decoded numbers, and
// level31's beholder, the button it raises and where to. Judged on the tick, on a
// bare rig of the beholder and a stand-in player:
//   - seeking, it glides toward the player, a step at a time, and wobbles;
//   - the player under it long enough makes it shut its eye and drop a rock over
//     the player every stride, three in a first round. The stride's clock carries
//     into the next round, which drops its first rock early and holds four, as
//     the original's does;
//   - a rock rising through it takes an hp, and three hits kill it;
//   - hurt, it fires rings of ten spikes, each ring turned from the last;
//   - the player dies within its radius, in a spike, or in the final blast;
//   - its rocks break on anything static they run into, and crush a player they
//     hit hard enough;
//   - dead, it raises the button.
// Then level31 is played. A rock dodged breaks on the floor; one not dodged
// crushes. And the level is won as the owner won it: a portal in the air under
// a falling rock and one under the beholder, three times, then the button and the
// exit.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Boss.hpp"
#include "sim/Game.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;
using Supersonic::BoxColliderComponent;
using Supersonic::RigidBodyComponent;
using Supersonic::SphereColliderComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;
constexpr double kPi = 3.14159265358979323846;

std::string Px(const glm::dvec2& at) {
    char text[48];
    std::snprintf(text, sizeof text, "(%.1f, %.1f)", at.x, at.y);
    return text;
}

glm::dvec2 PxOf(const entt::registry& registry, entt::entity e) {
    return Units::ToPixels(registry.get<TransformComponent>(e).position);
}

// ---- The rig: the beholder, a stand-in player, and nothing else -------------

struct Rig {
    entt::registry registry;
    Boss::State boss;
    entt::entity player = entt::null;
};

bool MakeRig(Rig& rig, const glm::dvec2& beholderPx, const glm::dvec2& playerPx) {
    std::string error;
    Boss::Rules rules;
    const bool read = Boss::LoadRules(kPortData + "/boss.json", rules, error);
    CHECK_MSG(read, error);
    if (!read) return false;
    rig.boss.rules = rules;
    Boss::Beholder beholder;
    beholder.name = "adder_test";
    beholder.atPx = beholderPx;
    beholder.startHeightPx = beholderPx.y;
    beholder.hp = rules.maxHp;
    rig.boss.beholder = beholder;
    rig.boss.rock.radiusPx = 30.0;
    rig.boss.rock.demolisher = true;
    rig.boss.rock.teleportable = true;
    rig.boss.contactMarginPx = rules.contactMarginPx;
    rig.boss.boundsPx = glm::dvec2(800.0, 300.0);
    rig.boss.buttonNode = "button_test";
    rig.boss.buttonDestPx = glm::dvec2(100.0, 200.0);
    // A dynamic body, as the player is: nothing the rock runs into as it would
    // into the static world.
    rig.player = rig.registry.create();
    rig.registry.emplace<TransformComponent>(rig.player).position = Units::ToWorld(playerPx.x, playerPx.y);
    rig.registry.emplace<SphereColliderComponent>(rig.player).radius = Units::ToMetres(10.0);
    rig.registry.emplace<RigidBodyComponent>(rig.player);
    return true;
}

void MovePlayer(Rig& rig, const glm::dvec2& atPx) {
    rig.registry.get<TransformComponent>(rig.player).position = Units::ToWorld(atPx.x, atPx.y);
}

Boss::State::Turn TickRig(Rig& rig, const std::vector<entt::entity>& stones = {}) {
    return rig.boss.Tick(rig.registry, rig.player, stones, kStep);
}

// A rolling stone at a point, moving at a velocity in the remake's pixels (+y down).
entt::entity Stone(Rig& rig, const glm::dvec2& atPx, const glm::dvec2& velocityPx) {
    const entt::entity stone =
        LevelBuilder::BuildRigidCircle(rig.registry, "test_stone", atPx, 30.0, LevelBuilder::Options{});
    rig.registry.get<RigidBodyComponent>(stone).velocity =
        glm::vec3(Units::ToMetres(velocityPx.x), Units::ToMetres(-velocityPx.y), 0.0f);
    return stone;
}

// ---- What the files say ------------------------------------------------------

void BossJsonIsTheScripts() {
    Boss::Rules r;
    std::string error;
    const bool read = Boss::LoadRules(kPortData + "/boss.json", r, error);
    CHECK_MSG(read, error);
    if (!read) return;
    CHECK_MSG(r.entity == "beholder.ent", r.entity);
    CHECK_EQ(r.maxHp, 2);
    CHECK(r.radiusPx == 48.0);
    CHECK(r.stepPx == 64.0);
    CHECK(r.wobblePx == 8.0);
    CHECK(r.glideMs == 2000.0);
    CHECK(r.retargetMs == 1000.0);
    CHECK(r.nearPx == 32.0);
    CHECK(r.holdMs == 400.0);
    CHECK(r.throwMs == 7900.0);
    CHECK(r.rockStrideMs == 2000.0);
    CHECK(r.rockHeightPx == -32.0);
    CHECK_MSG(r.rockEntity == "rolling_stone.ent", r.rockEntity);
    CHECK(r.bobRadS == 2.0);
    CHECK(r.bobPx == 4.0);
    CHECK(r.hurtMs == 7200.0);
    CHECK(r.volleyMs == 1200.0);
    CHECK(r.spikeStepDeg == 40.0);
    CHECK(r.spikeTurnDeg == 10.0);
    CHECK(r.spikeSpeedPx == 140.0);
    CHECK(r.deadMs == 3550.0);
    CHECK(r.blastPx == 80.0);
    CHECK(r.spikeHitShare == 0.7);
    CHECK(r.characterPx == glm::dvec2(40.0, 56.0));
    CHECK(r.cullMarginPx == glm::dvec2(16.0, 128.0));
    CHECK(r.crushIntensity == 2.5);
    // px_per_unit is carried, and marked _guess for its unit: not pinned.
}

void BossJsonRefusesGaps() {
    std::ifstream in(kPortData + "/boss.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "supersonic-test-mp-boss";
    std::filesystem::create_directories(directory);
    const auto write = [&](const char* name, const std::string& body) {
        const std::filesystem::path path = directory / name;
        std::ofstream(path, std::ios::binary) << body;
        return path.string();
    };
    const auto replaced = [&](const std::string& from, const std::string& to) {
        std::string changed = text;
        const std::size_t at = changed.find(from);
        if (at != std::string::npos) changed.replace(at, from.size(), to);
        return changed;
    };
    Boss::Rules rules;
    std::string error;
    CHECK(!Boss::LoadRules(write("gap.json", replaced("\"blast_px\"", "\"blast\"")), rules, error) &&
          error.find("beholder.dead.blast_px") != std::string::npos);
    error.clear();
    CHECK(!Boss::LoadRules(write("zero.json", replaced("\"step_deg\": 40.0", "\"step_deg\": 0.0")), rules, error) &&
          error.find("step_deg") != std::string::npos);
    error.clear();
    CHECK(!Boss::LoadRules(write("half.json", replaced("\"max_hp\": 2", "\"max_hp\": 2.5")), rules, error) &&
          error.find("max_hp") != std::string::npos);
}

// ---- The rig -------------------------------------------------------------------

void SeekingItGlidesTowardThePlayer() {
    // The player 200 px to its right. It aims 64 px on at a time, gliding there
    // over 2000 ms by smoothEnd, and aims again from wherever it is once 1000 ms
    // have passed. It never passes the player, and never leaves its height by
    // more than the wobble. Within 32 px of the player's x long enough, it
    // shuts its eye.
    Rig rig;
    if (!MakeRig(rig, {400.0, 80.0}, {600.0, 196.0})) return;
    const Boss::Beholder& b = *rig.boss.beholder;
    double lastX = b.atPx.x;
    double atOneSecond = 0.0;
    bool onward = true;
    bool level = true;
    for (int tick = 1; tick <= 600 && b.phase == Boss::Phase::Seeking; ++tick) {
        TickRig(rig);
        onward = onward && b.atPx.x >= lastX - 1e-9 && b.atPx.x <= 600.0 + 1e-9;
        level = level && std::fabs(b.atPx.y - 80.0) <= 8.0 + 1e-9;
        lastX = b.atPx.x;
        if (tick == 60) atOneSecond = b.atPx.x;
    }
    // A second in, half way through the first glide: 64 sin(pi/4) along.
    const double want = 400.0 + 64.0 * std::sin(kPi / 4.0);
    CHECK_MSG(std::fabs(atOneSecond - want) < 0.05,
              "at one second x is " + std::to_string(atOneSecond) + ", want " + std::to_string(want));
    CHECK_MSG(onward, "it only ever closed on the player");
    CHECK_MSG(level, "and stayed within 8 px of its height");
    CHECK_MSG(b.phase == Boss::Phase::ThrowRock && std::fabs(b.atPx.x - 600.0) < 32.0,
              "within 32 px of the player it shut its eye, at x " + std::to_string(b.atPx.x));
}

void UnderItItThrowsRocks() {
    // The player under it. Once it has been there more than 400 ms it shuts its
    // eye: on the 24th tick, since a tick of 1/60 s in single precision is a
    // hair over 16.667 ms and 24 of them are 400.00002. A rock comes over the player every 2000 ms: the
    // 120th, 240th and 360th ticks. Past 7900 ms, the 474th, it seeks again. The
    // clock has run 114 ticks since the last rock, and it carries: the next
    // round drops its first rock on its 6th tick, and holds four.
    Rig rig;
    if (!MakeRig(rig, {400.0, 80.0}, {400.0, 196.0})) return;
    const Boss::Beholder& b = *rig.boss.beholder;
    for (int round = 1; round <= 2; ++round) {
        int tick = 0;
        while (tick < 100 && b.phase == Boss::Phase::Seeking) {
            TickRig(rig);
            ++tick;
        }
        CHECK_EQ(tick, 24);
        CHECK(b.phase == Boss::Phase::ThrowRock);
        std::vector<int> drops;
        bool shut = true;
        bool bobbed = true;
        bool overThePlayer = true;
        for (tick = 1; tick <= 600 && b.phase == Boss::Phase::ThrowRock; ++tick) {
            const Boss::State::Turn turn = TickRig(rig);
            for (const Boss::Rock& rock : turn.dropped) {
                drops.push_back(tick);
                overThePlayer =
                    overThePlayer && glm::distance(PxOf(rig.registry, rock.body), glm::dvec2(400.0, -32.0)) < 1e-3;
            }
            shut = shut && (b.frame == 1 || b.phase != Boss::Phase::ThrowRock);
            bobbed = bobbed && std::fabs(b.atPx.y - b.bobFromPx.y) <= 4.0 + 1e-9;
        }
        CHECK_EQ(tick - 1, 474);
        const std::vector<int> want = round == 1 ? std::vector<int>{120, 240, 360} : std::vector<int>{6, 126, 246, 366};
        std::string got;
        for (const int d : drops) got += " " + std::to_string(d);
        CHECK_MSG(drops == want, "round " + std::to_string(round) + " dropped on ticks" + got);
        CHECK_MSG(overThePlayer, "every rock at (400, -32): over the player, high up");
        CHECK_MSG(shut, "its eye shut throughout");
        CHECK_MSG(bobbed, "bobbing within 4 px");
    }
    CHECK_EQ(rig.boss.beholder->rocksThrown, 7);
    CHECK_MSG(rig.boss.rocks.size() == 7u && rig.boss.rocks.front().name == "adder_test#1",
              std::to_string(rig.boss.rocks.size()) + " rocks");
}

void ARisingStoneHurtsIt() {
    Rig rig;
    if (!MakeRig(rig, {400.0, 80.0}, {100.0, 196.0})) return;
    const Boss::Beholder& b = *rig.boss.beholder;
    // Falling through it: nothing.
    const entt::entity falling = Stone(rig, {400.0, 110.0}, {0.0, 300.0});
    TickRig(rig, {falling});
    CHECK_EQ(b.hp, 2);
    CHECK(rig.registry.valid(falling));
    // Rising, 49 px from its centre: nothing.
    const entt::entity wide = Stone(rig, {449.0, 80.0}, {0.0, -300.0});
    TickRig(rig, {wide});
    CHECK_EQ(b.hp, 2);
    // Rising, within 48 px: it breaks, and the beholder is hurt.
    const entt::entity rising = Stone(rig, {400.0, 120.0}, {0.0, -300.0});
    const Boss::State::Turn turn = TickRig(rig, {rising, wide});
    CHECK_EQ(b.hp, 1);
    CHECK_EQ(b.hits, 1);
    CHECK(!rig.registry.valid(rising));
    CHECK(rig.registry.valid(wide));
    CHECK(turn.broken.size() == 1u && turn.broken.front() == rising);
    CHECK(b.phase == Boss::Phase::GotDamage);
    // The next turn goes red by half: its colour is (1, hp/2, hp/2).
    CHECK_EQ(b.hp * 2, rig.boss.rules.maxHp);
}

void ThreeHitsKillItAndTheButtonRises() {
    // Three hits take it from 2 to -1. The next turn it is dead, and once it has
    // been dead more than 3550 ms - 213 turns - the button rises, once, and the
    // beholder goes.
    Rig rig;
    if (!MakeRig(rig, {400.0, 80.0}, {100.0, 196.0})) return;
    const Boss::Beholder& b = *rig.boss.beholder;
    for (int hit = 1; hit <= 3; ++hit) {
        const entt::entity stone = Stone(rig, b.atPx + glm::dvec2(0.0, 20.0), {0.0, -300.0});
        TickRig(rig, {stone});
    }
    CHECK_EQ(b.hp, -1);
    CHECK_EQ(b.hits, 3);
    int dead = 0;
    int raisedOn = -1;
    int raised = 0;
    for (int tick = 1; tick <= 300; ++tick) {
        const Boss::State::Turn turn = TickRig(rig);
        if (b.phase == Boss::Phase::Dead && dead == 0) dead = tick;
        if (turn.buttonRaised) {
            ++raised;
            if (raisedOn < 0) raisedOn = tick;
        }
    }
    CHECK_EQ(dead, 1);
    CHECK_EQ(raisedOn, 213);
    CHECK_EQ(raised, 1);
    CHECK(b.gone && rig.boss.buttonRaised);
    CHECK(!rig.boss.playerKilled);
}

void HurtItFiresRingsOfTen() {
    // Hurt, it fires ten spikes every 1200 ms - the 72nd turn after - at every
    // 40 degrees from up, clockwise, to up again. The next ring starts 10 degrees
    // on. Six rings, and at 7200 ms, the 432nd turn, it seeks again.
    Rig rig;
    if (!MakeRig(rig, {400.0, 80.0}, {100.0, 196.0})) return;
    const Boss::Beholder& b = *rig.boss.beholder;
    TickRig(rig, {Stone(rig, {400.0, 100.0}, {0.0, -300.0})});
    CHECK(b.phase == Boss::Phase::GotDamage);
    int firstRing = -1;
    int seeking = -1;
    std::vector<glm::dvec2> ringOne;
    std::vector<glm::dvec2> ringTwo;
    for (int tick = 1; tick <= 500 && seeking < 0; ++tick) {
        const int before = b.volleys;
        const std::size_t count = rig.boss.spikes.size();
        TickRig(rig);
        if (b.volleys > before) {
            if (firstRing < 0) firstRing = tick;
            std::vector<glm::dvec2>& ring = ringOne.empty() ? ringOne : ringTwo;
            if (ring.empty()) {
                for (std::size_t i = count; i < rig.boss.spikes.size(); ++i) {
                    ring.push_back(rig.boss.spikes[i].directionPx);
                }
            }
        }
        if (b.phase == Boss::Phase::Seeking) seeking = tick;
    }
    CHECK_EQ(firstRing, 72);
    CHECK_EQ(seeking, 432);
    CHECK_EQ(b.volleys, 6);
    CHECK_EQ(ringOne.size(), std::size_t{10});
    CHECK_EQ(ringTwo.size(), std::size_t{10});
    if (ringOne.size() != 10 || ringTwo.size() != 10) return;
    const auto near = [](const glm::dvec2& got, double degrees) {
        const double r = degrees * kPi / 180.0;
        return std::fabs(got.x - std::sin(r)) < 1e-5 && std::fabs(got.y + std::cos(r)) < 1e-5;
    };
    bool everyForty = true;
    for (int i = 0; i < 10; ++i) everyForty = everyForty && near(ringOne[static_cast<std::size_t>(i)], 40.0 * i);
    CHECK_MSG(everyForty, "ring one at 0, 40, ... 360 degrees from up");
    bool turned = true;
    for (int i = 0; i < 10; ++i) turned = turned && near(ringTwo[static_cast<std::size_t>(i)], 10.0 + 40.0 * i);
    CHECK_MSG(turned, "ring two at 10, 50, ... 370");
    CHECK(near(ringOne.front(), 0.0) && near(ringOne.back(), 0.0)); // up, twice
}

void TheSpikesKillAndLeave() {
    // On the first ring's 160-degree line, 100 px out, a player is killed as the
    // spike passes. Directly under it no spike of either ring passes, and the
    // player lives through the whole of it. Then every spike leaves the level.
    {
        Rig rig;
        const double r = 160.0 * kPi / 180.0;
        const glm::dvec2 on(400.0 + 100.0 * std::sin(r), 80.0 - 100.0 * std::cos(r));
        if (!MakeRig(rig, {400.0, 80.0}, on)) return;
        TickRig(rig, {Stone(rig, {400.0, 100.0}, {0.0, -300.0})});
        for (int tick = 0; tick < 200 && !rig.boss.playerKilled; ++tick) TickRig(rig);
        CHECK_MSG(rig.boss.playerKilled && rig.boss.killedBy == "adder_test's spike",
                  "on the line at " + Px(on) + ": killed by '" + rig.boss.killedBy + "'");
    }
    {
        Rig rig;
        if (!MakeRig(rig, {400.0, 80.0}, {400.0, 202.0})) return;
        TickRig(rig, {Stone(rig, {400.0, 100.0}, {0.0, -300.0})});
        for (int tick = 0; tick < 1200; ++tick) TickRig(rig);
        CHECK_MSG(!rig.boss.playerKilled, "under it: killed by '" + rig.boss.killedBy + "'");
        CHECK_MSG(rig.boss.beholder->volleys == 6 && rig.boss.spikes.empty(),
                  std::to_string(rig.boss.spikes.size()) + " spikes left");
    }
}

void ThePlayerDiesWithinItsRadius() {
    {
        Rig rig;
        if (!MakeRig(rig, {400.0, 80.0}, {449.0, 80.0})) return;
        TickRig(rig);
        CHECK_MSG(!rig.boss.playerKilled, "49 px away it lives");
        MovePlayer(rig, {400.0, 127.0});
        TickRig(rig);
        CHECK_MSG(rig.boss.playerKilled && rig.boss.killedBy == "adder_test", "47 px away it dies");
    }
}

void ItsRocksBreakOnTheWorldAndCrush() {
    Rig rig;
    if (!MakeRig(rig, {400.0, 80.0}, {200.0, 196.0})) return;
    // A floor: a static box whose top is at y 224.
    const entt::entity floor = rig.registry.create();
    rig.registry.emplace<TransformComponent>(floor).position = Units::ToWorld(400.0, 240.0);
    rig.registry.emplace<BoxColliderComponent>(floor).size =
        glm::vec3(Units::ToMetres(800.0), Units::ToMetres(32.0), static_cast<float>(LevelBuilder::kStaticDepthMetres));
    const auto rock = [&](const char* name, const glm::dvec2& atPx, const glm::dvec2& velocityPx) {
        Boss::Rock made;
        made.name = name;
        made.body = Stone(rig, atPx, velocityPx);
        rig.boss.rocks.push_back(made);
        return made.body;
    };
    const entt::entity landing = rock("landing", {300.0, 194.0}, {0.0, 200.0});
    const entt::entity leaving = rock("leaving", {500.0, 194.0}, {0.0, -200.0});
    const entt::entity clear = rock("clear", {600.0, 150.0}, {0.0, 200.0});
    const entt::entity gentle = rock("gentle", {200.0, 157.0}, {0.0, 100.0});
    Portals::State portals;
    rig.boss.BeforeStep(rig.registry);
    std::vector<entt::entity> broken = rig.boss.Contacts(rig.registry, rig.player, portals, kStep);
    CHECK_MSG(broken.size() == 1u && broken.front() == landing, std::to_string(broken.size()) + " broken");
    CHECK(!rig.registry.valid(landing));
    const bool stayed = rig.registry.valid(leaving) && rig.registry.valid(clear) && rig.registry.valid(gentle);
    CHECK_MSG(stayed, "the rock leaving the floor, the one clear of it and the one on the player stay");
    CHECK_MSG(!rig.boss.playerKilled, "100 px/s on it is 2 in the original's units, under 2.5");
    if (!stayed) return;
    // Faster onto the player: 200 px/s is 4, and it is crushed.
    rig.registry.get<RigidBodyComponent>(gentle).velocity.y = -Units::ToMetres(200.0);
    rig.boss.BeforeStep(rig.registry);
    rig.boss.Contacts(rig.registry, rig.player, portals, kStep);
    CHECK_MSG(rig.boss.playerKilled && rig.boss.killedBy == "gentle", "killed by '" + rig.boss.killedBy + "'");
    // One held by a portal goes through it instead of landing.
    portals.budget = 2;
    // The PORTAL's own radius: this rig is about a rock held by a placement,
    // not about an antiportal. Its own number, not the decoded 14.
    portals.rules.entryRadiusPx = 16.0;
    CHECK(portals.TryPlace({700.0, 194.0}));
    const entt::entity held = rock("held", {700.0, 194.0}, {0.0, 200.0});
    rig.boss.BeforeStep(rig.registry);
    broken = rig.boss.Contacts(rig.registry, rig.player, portals, kStep);
    CHECK(broken.empty() && rig.registry.valid(held));
    CHECK_EQ(rig.boss.rocksBroken, 1);
}

// ---- level31 -------------------------------------------------------------------

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-boss", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run, float direction = 0.0f) {
    Game::Tick(run.data, run.registry, run.level, direction, kStep);
}

glm::dvec2 PlayerPx(const Run& run) {
    return PxOf(run.registry, run.level.player);
}

// Right or left toward x, and nothing within 3 px of it.
float Toward(const Run& run, double x) {
    const double dx = x - PlayerPx(run).x;
    return std::fabs(dx) < 3.0 ? 0.0f : (dx > 0.0 ? 1.0f : -1.0f);
}

void Level31sBeholder() {
    Run run;
    if (!Begin("level31", run)) return;
    const Boss::State& boss = run.level.boss;
    CHECK(boss.beholder.has_value());
    if (!boss.beholder) return;
    CHECK_MSG(boss.beholder->name == "adder_715", boss.beholder->name);
    CHECK(boss.beholder->atPx == glm::dvec2(729.0, 80.0));
    CHECK_EQ(boss.beholder->hp, 2);
    CHECK(boss.beholder->phase == Boss::Phase::Seeking);
    CHECK_MSG(boss.buttonNode == "button_794", boss.buttonNode);
    CHECK(boss.buttonDestPx == glm::dvec2(528.0, 220.0));
    CHECK(boss.boundsPx == glm::dvec2(768.0, 256.0));
    CHECK(boss.rock.radiusPx == 30.0 && boss.rock.teleportable && boss.rock.demolisher);
    // The button starts buried: its box at (565, 244) is inside the floor.
    const Puzzle::Button* button = run.level.channels.FindButton("button_794");
    CHECK(button != nullptr);
    if (button != nullptr) {
        const glm::dvec2 at = Units::ToPixels(glm::vec3(button->box.centre, 0.0f));
        CHECK_MSG(glm::distance(at, glm::dvec2(565.0, 244.0)) < 1e-3, Px(at));
    }
    // No other level of chapter 1 has one, and level31a's boss is not the port's.
    Run other;
    if (Begin("level30", other)) CHECK(!other.level.boss.beholder.has_value());
    Run dragon;
    if (Begin("level31a", dragon)) CHECK(!dragon.level.boss.beholder.has_value());
}

// Stands the player at `lureX` until the beholder, coming after it, shuts its
// eye over it; then walks it to `offset` px from the beholder's x and waits for
// the first rock. Returns the rock, or null.
entt::entity ProvokeARock(Run& run, double lureX, double offset, std::string& trace) {
    Boss::State& boss = run.level.boss;
    for (int tick = 0; tick < 1800 && !run.level.hazards.playerDied && boss.beholder->phase != Boss::Phase::ThrowRock;
         ++tick) {
        Tick(run, Toward(run, lureX));
    }
    const double standX = boss.beholder->atPx.x + offset;
    trace += " shut at " + Px(boss.beholder->atPx) + ", standing at x " + std::to_string(static_cast<int>(standX));
    const int thrown = boss.beholder->rocksThrown;
    for (int tick = 0; tick < 600 && !run.level.hazards.playerDied && boss.beholder->rocksThrown == thrown; ++tick) {
        Tick(run, Toward(run, standX));
    }
    return boss.rocks.empty() ? entt::null : boss.rocks.back().body;
}

void Level31ARockBreaksOnTheFloorOrCrushes() {
    // Stepped out from under it, a rock breaks on the floor. Stood under, it
    // crushes.
    for (const bool dodge : {true, false}) {
        Run run;
        if (!Begin("level31", run)) return;
        for (int tick = 0; tick < 30; ++tick) Tick(run);
        std::string trace;
        const entt::entity rock = ProvokeARock(run, 420.0, -120.0, trace);
        CHECK_MSG(rock != entt::null, "no rock:" + trace);
        if (rock == entt::null) return;
        const double standX = PlayerPx(run).x;
        for (int tick = 0; tick < 120 && run.registry.valid(rock) && !run.level.hazards.playerDied; ++tick) {
            Tick(run, dodge ? Toward(run, standX - 60.0) : 0.0f);
            if (!dodge && run.registry.valid(rock)) {
                const glm::vec3 v = run.registry.get<RigidBodyComponent>(rock).velocity;
                trace += " " + std::to_string(tick) + ":" + Px(PxOf(run.registry, rock)) + "v" +
                         std::to_string(static_cast<int>(-v.y * Units::kPixelsPerMetre)) + "p" + Px(PlayerPx(run));
            }
        }
        const bool gone = !run.registry.valid(rock);
        if (dodge) {
            CHECK_MSG(gone && run.level.boss.rocksBroken == 1 && !run.level.hazards.playerDied,
                      "dodged: the rock " + std::string(gone ? "broke" : "stayed") + ", the player " +
                          (run.level.hazards.playerDied ? "died to " + run.level.hazards.killedBy : "lived") + trace);
        } else {
            CHECK_MSG(run.level.hazards.playerDied && run.level.hazards.killedBy == "adder_715#1",
                      "stood under it: killed by '" + run.level.hazards.killedBy + "'" + trace);
        }
    }
}

void Level31IsWonAsTheOwnerWonIt() {
    // "I would put a portal under him and then one under the falling rock and if
    // the rock enter my portal, it would exit under the beholder and hit him from
    // under." Three times: let it come over the player and shut its eye, step
    // 120 px to its left, where no spike of either ring passes, and when the rock
    // appears over the player open one portal in the air under it and one under
    // the beholder. Then it dies, the button rises, and the player presses it
    // and walks out.
    //
    // Near the door it floats in front of wall04, which reaches down to y 106
    // over x 544 to 608: a rock rising under it there breaks on the wall. So
    // the player first draws it left, to x 420, and after that it follows the
    // player wherever the spikes left it standing.
    Run run;
    if (!Begin("level31", run)) return;
    Boss::State& boss = run.level.boss;
    if (!boss.beholder) return;
    int tick = 0;
    for (; tick < 30; ++tick) Tick(run);
    std::string trace;
    const auto died = [&]() { return run.level.hazards.playerDied; };
    for (int round = 1; round <= 3 && !died(); ++round) {
        trace += " | round " + std::to_string(round) + ":";
        const double lureX = round == 1 ? 420.0 : PlayerPx(run).x;
        const entt::entity rock = ProvokeARock(run, lureX, -120.0, trace);
        if (rock == entt::null || died()) break;
        const double standX = PlayerPx(run).x;
        const glm::dvec2 rockPx = PxOf(run.registry, rock);
        const glm::dvec2 under = boss.beholder->atPx + glm::dvec2(0.0, 60.0);
        const bool placed =
            run.level.portals.TryPlace(glm::dvec2(rockPx.x, 100.0)) && run.level.portals.TryPlace(under);
        trace += " rock at " + Px(rockPx) + ", portals " + Px(glm::dvec2(rockPx.x, 100.0)) + " and " + Px(under) +
                 (placed ? "" : " REFUSED");
        const int hits = boss.beholder->hits;
        for (int wait = 0; wait < 300 && !died() && boss.beholder->hits == hits; ++wait) {
            Tick(run, Toward(run, standX));
        }
        trace += " hit " + std::string(boss.beholder->hits > hits ? "yes" : "NO");
        // Where it stands, until the spikes are done.
        for (int wait = 0; wait < 600 && !died() && boss.beholder->phase == Boss::Phase::GotDamage; ++wait) {
            Tick(run, Toward(run, standX));
        }
    }
    const int hits = boss.beholder->hits;
    // Dead, it goes, and the button rises to (528, 220).
    for (int wait = 0; wait < 600 && !died() && !boss.buttonRaised; ++wait) Tick(run);
    trace += " | button " + std::string(boss.buttonRaised ? "raised" : "NOT raised");
    bool pressed = false;
    for (int wait = 0; wait < 600 && !died() && !pressed; ++wait) {
        Tick(run, Toward(run, 528.0));
        pressed = run.level.channels.Pressed(0);
    }
    // Its door takes the lift's stride to rise; then out to the right.
    for (int wait = 0; wait < 600 && !died(); ++wait) Tick(run, Toward(run, 528.0));
    int walk = 0;
    for (; walk < 900 && !died() && !run.level.goals.completed; ++walk) Tick(run, 1.0f);
    std::printf("  level31 as the owner won it: %d hit(s), %d traversal(s), %d portal(s), %s, %s\n", hits,
                run.level.portals.traversals, run.level.portals.portalsUsed, pressed ? "the button pressed" : "NOT pressed",
                run.level.goals.completed ? "completed" : (died() ? ("died to " + run.level.hazards.killedBy).c_str()
                                                                  : "NOT completed"));
    CHECK_MSG(hits == 3 && boss.beholder->gone, "three hits, and it is gone:" + trace);
    CHECK_MSG(!died(), "the player lived: killed by '" + run.level.hazards.killedBy + "'" + trace);
    CHECK_MSG(pressed, "the risen button pressed" + trace);
    CHECK_MSG(run.level.goals.completed, "and out through the exit" + trace);
    CHECK_EQ(run.level.portals.traversals, 3);
}

void runTests() {
    BossJsonIsTheScripts();
    BossJsonRefusesGaps();
    SeekingItGlidesTowardThePlayer();
    UnderItItThrowsRocks();
    ARisingStoneHurtsIt();
    ThreeHitsKillItAndTheButtonRises();
    HurtItFiresRingsOfTen();
    TheSpikesKillAndLeave();
    ThePlayerDiesWithinItsRadius();
    ItsRocksBreakOnTheWorldAndCrush();
    Level31sBeholder();
    Level31ARockBreaksOnTheFloorOrCrushes();
    Level31IsWonAsTheOwnerWonIt();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_boss: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_boss", 120);
}
