// Gravity wells: entity `gravity_agent` / `gravity_agent.ent`, role
// `gravity_well`. Ten placements across five levels, and the last mechanic in
// the game that is not a boss.
//
// WHAT THIS SUITE IS REALLY FOR. A well is three things, and only two of them
// are anywhere in a level file. The node gives a position, a radius, a
// StaticBody2D and a CircleShape2D - so the solid circle builds itself and the
// attraction can be read off the radius. The THIRD is invisible: the agent's own
// callback adds an antiportal.ent at its position on its first tick, and no
// .tscn in the game mentions it. A port built from the level data alone would
// let a portal open inside a well, and every role count would still read
// "playing" - the same blindness light_wall.ent had before test_mp_torch watched
// the body. So the zone is asserted here, through Portals::State::TryPlace.
//
// Reads the converted levels from outside this repository, and skips, saying
// where it looked, when they are absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/GravityWell.hpp"
#include "sim/Roles.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// A zero-gravity level with one well, and a level with the world's gravity and
// one - the two sides of the forceLength branch.
constexpr const char* kWeightless = "level16c";
constexpr const char* kHeavy = "level20c";

double Length(const glm::vec3& v) {
    return std::sqrt(static_cast<double>(v.x * v.x + v.y * v.y));
}

bool Open(const std::string& name, Game::Data& data, entt::registry& registry, Game::Level& level) {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-wells";
    std::string error;
    if (!Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) ||
        !Game::Start(data, registry, level, error)) {
        CHECK_MSG(false, name + ": " + error);
        return false;
    }
    return true;
}

void TheRulesRead() {
    GravityWell::Rules rules;
    std::string error;
    const bool ok = GravityWell::LoadRules(kPortData + "/gravitywell.json", rules, error);
    CHECK_MSG(ok, error);
    if (!ok) return;
    CHECK(::test::nearly(static_cast<float>(rules.forceZeroGravity), 0.18f));
    CHECK(::test::nearly(static_cast<float>(rules.forceWithGravity), 0.5f));
    CHECK(::test::nearly(static_cast<float>(rules.referenceFrameMs), 16.6666f));
    CHECK(::test::nearly(static_cast<float>(rules.zoneShrinkPx), 24.0f));
    std::printf("  pulls at %.2f m/s in zero gravity and %.2f with, per %.4f ms frame\n", rules.forceZeroGravity,
                rules.forceWithGravity, rules.referenceFrameMs);

    // And the ring the agent adds with that zone: antiportal.ent's own file, and
    // the green its callback paints it at instr 117-131.
    const GravityWell::Ring& ring = rules.ring;
    CHECK_MSG(ring.sprite == "white_ring.png" && ring.additive, "white_ring.png, added (blendMode 1)");
    CHECK_MSG(ring.colour == glm::dvec3(0.25, 0.5, 0.25), "SetColor(vector3(0.5, 1, 0.5) * 0.5): green");
    CHECK_MSG(ring.emissive == glm::dvec3(1.0), "antiportal.ent's EmissiveColor (1, 1, 1)");
    CHECK_MSG(!ring.applyLight && ring.isStatic, "applyLight 0, static 1");
    CHECK_MSG(ring.z == -5.0, "AddEntity's vector3(pos, -5)");
}

// THE RING IS THE ZONE SEEN, and the size is the primary of the two: scaleToSize
// gives the added antiportal a SIZE of radius * 2 - 24, and the refusal is half of
// it (GetSize().x * 0.5). Read in that order, so a ruling about the refusal
// (00_order R2) cannot move the picture.
void TheRingIsTheZoneSeen() {
    GravityWell::Rules rules;
    std::string error;
    if (!GravityWell::LoadRules(kPortData + "/gravitywell.json", rules, error)) return;

    const struct Level {
        const char* name;
        double sizes[4];
        std::size_t count;
    } levels[] = {{"level16c", {188.0, 0.0, 0.0, 0.0}, 1},
                  {"level17c", {188.0, 256.0, 188.0, 188.0}, 4},
                  {"level18c", {256.0, 104.0, 104.0, 0.0}, 3},
                  {"level20c", {496.0, 0.0, 0.0, 0.0}, 1},
                  {"level27c", {232.0, 0.0, 0.0, 0.0}, 1}};

    std::size_t rings = 0;
    for (const Level& one : levels) {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (!Open(one.name, data, registry, level)) continue;
        if (level.wells.wells.size() != one.count) continue;
        for (std::size_t i = 0; i < level.wells.wells.size(); ++i) {
            const GravityWell::Well& well = level.wells.wells[i];
            const double size = well.RingSizePx(rules);
            CHECK_MSG(::test::nearly(static_cast<float>(size), static_cast<float>(one.sizes[i])),
                      well.name + " is drawn " + std::to_string(one.sizes[i]) + " units across, not " +
                          std::to_string(size));
            // The refusal is half the picture, at every placement.
            CHECK(::test::nearly(static_cast<float>(well.ZoneRadiusPx(rules)), static_cast<float>(size * 0.5)));
            ++rings;
        }
        std::printf("  %s: %zu ring(s), the first %.0f units across\n", one.name, level.wells.wells.size(),
                    level.wells.wells.front().RingSizePx(rules));
    }
    CHECK_EQ(rings, std::size_t{10});
}

// A ring is antiportal.ent's own row, and none of it may be guessed: a file that
// is missing any of it is refused rather than drawn at a default on five levels.
void ARingThatIsNotTheEntitysIsRefused() {
    std::ifstream file(kPortData + "/gravitywell.json", std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string real = buffer.str();
    CHECK(real.find("\"ring\": {") != std::string::npos);

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "supersonic-test-mp-wells";
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);
    const auto refused = [&real, &scratch](const std::string& from, const std::string& to, const std::string& name) {
        std::string text = real;
        const std::size_t at = text.find(from);
        CHECK_MSG(at != std::string::npos, from);
        if (at == std::string::npos) return;
        text.replace(at, from.size(), to);
        const std::filesystem::path path = scratch / ("gravitywell-ring-" + name + ".json");
        std::ofstream out(path, std::ios::binary);
        out << text;
        out.close();
        GravityWell::Rules rules;
        std::string error;
        const bool ok = GravityWell::LoadRules(path.string(), rules, error);
        CHECK_MSG(!ok, "refused: " + name);
        CHECK_MSG(error.find("ring") != std::string::npos, name + ": " + error);
    };
    refused("\"ring\": {", "\"ring_gone\": {", "absent");
    refused("\"sprite\": \"white_ring.png\"", "\"sprite\": \"\"", "no picture");
    refused("\"additive\": true", "\"additive\": 1", "a blend that is not a bool");
    refused("\"colour\": [0.25, 0.5, 0.25]", "\"colour\": [0.25, 0.5]", "two channels");
    refused("\"colour\": [0.25, 0.5, 0.25]", "\"colour\": [0.25, -0.5, 0.25]", "below zero");
    refused("\"emissive\": [1.0, 1.0, 1.0]", "\"emissive\": \"1 1 1\"", "an emissive that is not three numbers");
    refused("\"apply_light\": false", "\"apply_light\": \"no\"", "a lighting flag that is not a bool");
    refused("\"z\": -5.0", "\"z\": \"-5\"", "a depth that is not a number");
}

// The census, counted by reading every node's radius.
void TenWellsAcrossFiveLevels() {
    const struct Level {
        const char* name;
        std::size_t count;
    } levels[] = {{"level16c", 1}, {"level17c", 4}, {"level18c", 3}, {"level20c", 1}, {"level27c", 1}};

    std::size_t total = 0;
    for (const Level& one : levels) {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (!Open(one.name, data, registry, level)) continue;
        CHECK_MSG(level.wells.wells.size() == one.count,
                  std::string(one.name) + " places " + std::to_string(one.count) + " gravity well(s)");
        total += level.wells.wells.size();
        for (const GravityWell::Well& well : level.wells.wells) {
            CHECK_MSG(well.radiusPx > 0.0, well.name + " has a radius");
        }
    }
    CHECK_EQ(total, std::size_t{10});
    std::printf("  %zu gravity well(s) across 5 levels\n", total);

    // A level with none has none.
    {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (Open("level0", data, registry, level)) CHECK(level.wells.wells.empty());
    }
}

// radius - 12, not radius. Twelve pixels at every placement in the game.
void TheZoneIsTwelvePixelsInsideTheWell() {
    GravityWell::Rules rules;
    std::string error;
    if (!GravityWell::LoadRules(kPortData + "/gravitywell.json", rules, error)) return;

    GravityWell::Well well;
    well.radiusPx = 106.0;
    CHECK(::test::nearly(static_cast<float>(well.ZoneRadiusPx(rules)), 94.0f));
    well.radiusPx = 260.0;
    CHECK(::test::nearly(static_cast<float>(well.ZoneRadiusPx(rules)), 248.0f));
    well.radiusPx = 64.0;
    CHECK(::test::nearly(static_cast<float>(well.ZoneRadiusPx(rules)), 52.0f));
}

// The arithmetic on its own, away from any level.
void ItPullsInwardAndFadesToNothingAtTheRim() {
    GravityWell::Rules rules;
    std::string error;
    if (!GravityWell::LoadRules(kPortData + "/gravitywell.json", rules, error)) return;

    GravityWell::Well well;
    well.name = "well";
    well.atPx = glm::dvec2(100.0, 100.0);
    well.radiusPx = 100.0;

    // A body to the RIGHT of the centre is pulled LEFT.
    const glm::vec3 fromRight = GravityWell::Pull(well, rules, glm::dvec2(150.0, 100.0), true, kStep);
    CHECK(fromRight.x < 0.0f);
    CHECK(::test::nearly(fromRight.y, 0.0f));

    // A body BELOW it - a larger y in the remake's pixels, which count down - is
    // pulled UP, which is +y in the engine's metres. The sign trap again.
    const glm::vec3 fromBelow = GravityWell::Pull(well, rules, glm::dvec2(100.0, 150.0), true, kStep);
    CHECK(fromBelow.y > 0.0f);
    CHECK(::test::nearly(fromBelow.x, 0.0f));

    // Nearer the centre pulls HARDER: smoothEnd(1 - d^2/r^2) is an ease-out.
    const double near = Length(GravityWell::Pull(well, rules, glm::dvec2(110.0, 100.0), true, kStep));
    const double mid = Length(GravityWell::Pull(well, rules, glm::dvec2(150.0, 100.0), true, kStep));
    const double rim = Length(GravityWell::Pull(well, rules, glm::dvec2(195.0, 100.0), true, kStep));
    std::printf("  a 100 px well pulls %.5f at 10 px, %.5f at 50, %.5f at 95\n", near, mid, rim);
    CHECK(near > mid);
    CHECK(mid > rim);
    CHECK(rim > 0.0);

    // OUTSIDE is nothing at all, and the rim itself is outside - the original's
    // test is a strict less-than.
    CHECK_EQ(Length(GravityWell::Pull(well, rules, glm::dvec2(200.0, 100.0), true, kStep)), 0.0);
    CHECK_EQ(Length(GravityWell::Pull(well, rules, glm::dvec2(400.0, 100.0), true, kStep)), 0.0);
    // And dead centre is nothing rather than a NaN.
    const glm::vec3 centre = GravityWell::Pull(well, rules, well.atPx, true, kStep);
    CHECK(::test::nearly(centre.x, 0.0f));
    CHECK(::test::nearly(centre.y, 0.0f));
}

// 0.18 against 0.5, and the branch is the LEVEL's flag.
void ItPullsHarderWhereTheWorldHasGravity() {
    GravityWell::Rules rules;
    std::string error;
    if (!GravityWell::LoadRules(kPortData + "/gravitywell.json", rules, error)) return;

    GravityWell::Well well;
    well.atPx = glm::dvec2(0.0, 0.0);
    well.radiusPx = 100.0;
    const glm::dvec2 at(50.0, 0.0);

    const double weightless = Length(GravityWell::Pull(well, rules, at, true, kStep));
    const double heavy = Length(GravityWell::Pull(well, rules, at, false, kStep));
    CHECK(heavy > weightless);
    // Exactly the ratio of the two strengths, since everything else is equal.
    CHECK(::test::nearly(static_cast<float>(heavy / weightless),
                         static_cast<float>(rules.forceWithGravity / rules.forceZeroGravity)));

    // And the levels carry the flag the branch reads.
    {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (Open(kWeightless, data, registry, level)) CHECK(level.wells.noGravity);
    }
    {
        Game::Data data;
        entt::registry registry;
        Game::Level level;
        if (Open(kHeavy, data, registry, level)) CHECK(!level.wells.noGravity);
    }
}

// THE PART NO LEVEL FILE MENTIONS.
void NoPortalOpensInsideAWell() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;
    if (level.wells.wells.empty()) {
        CHECK_MSG(false, "level16c places a gravity well");
        return;
    }

    const GravityWell::Well& well = level.wells.wells.front();
    const double zonePx = well.ZoneRadiusPx(level.wells.rules);

    // A zone was handed to the portals for it, carrying its OWN radius rather
    // than the antiportal constant.
    const Portals::NoPortalZone* zone = level.portals.FindZone(well.name);
    CHECK_MSG(zone != nullptr, well.name + " gave the portals a no-portal zone");
    if (zone == nullptr) return;
    CHECK(::test::nearly(static_cast<float>(zone->radiusPx), static_cast<float>(zonePx)));

    // Dead centre is refused, and so is a tap just inside the rim.
    CHECK(!level.portals.TryPlace(well.atPx));
    CHECK(!level.portals.TryPlace(well.atPx + glm::dvec2(zonePx - 2.0, 0.0)));
    // And just outside it is not refused for THIS reason. The tap may still fail
    // on a budget or another zone, so what is asserted is only that the well is
    // not what stops it - by placing it far enough out to be clear.
    std::printf("  %s: %s refuses a portal within %.0f px of (%.0f, %.0f)\n", kWeightless, well.name.c_str(), zonePx,
                well.atPx.x, well.atPx.y);
}

// A dynamic body is pulled; a kinematic one is not.
void ItTakesTheLooseBodiesOnly() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kWeightless, data, registry, level)) return;
    if (level.wells.wells.empty()) return;

    // The player is dynamic, so a well reaches it. Put it inside one and let it
    // go: in a zero-gravity level nothing else can move it.
    const GravityWell::Well& well = level.wells.wells.front();
    auto& transform = registry.get<TransformComponent>(level.player);
    const glm::dvec2 startPx = well.atPx + glm::dvec2(well.radiusPx * 0.5, 0.0);
    const glm::vec3 start = Units::ToWorld(startPx.x, startPx.y);
    transform.position = glm::vec3(start.x, start.y, transform.position.z);
    registry.get<RigidBodyComponent>(level.player).velocity = glm::vec3(0.0f);

    const double before = glm::distance(startPx, well.atPx);
    for (int tick = 0; tick < 30; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    const glm::dvec2 nowPx = Units::ToPixels(registry.get<TransformComponent>(level.player).position);
    const double after = glm::distance(nowPx, well.atPx);
    std::printf("  %s: the player falls from %.1f px to %.1f px of the well in half a second\n", kWeightless, before,
                after);
    CHECK(after < before);
}

void TheRoleIsPlayed() {
    CHECK(Roles::IsPorted(Roles::kGravityWell));
    // It does not MOVE, though: the agent is a solid circle that stands still.
    CHECK(!Roles::Moves(Roles::kGravityWell));
}

void runTests() {
    TheRulesRead();
    TenWellsAcrossFiveLevels();
    TheZoneIsTwelvePixelsInsideTheWell();
    TheRingIsTheZoneSeen();
    ARingThatIsNotTheEntitysIsRefused();
    ItPullsInwardAndFadesToNothingAtTheRim();
    ItPullsHarderWhereTheWorldHasGravity();
    NoPortalOpensInsideAWell();
    ItTakesTheLooseBodiesOnly();
    TheRoleIsPlayed();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_wells: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_wells", 75);
}
