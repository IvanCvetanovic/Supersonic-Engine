// The small motions the original's scripts give an entity: linearMotion, as
// sim/Motion reads it from motions.json (the visuals plan's 3.2).
//
// What is pinned, and from where (motions.json's _source, the bytecode listing):
//
//   the rules    frame cap 200 ms, the wrap at PI x 2f, and the crystal's and the
//                key's rows: speed 2, stride 1.5, vertical, axis 0, start angle on
//                [0, PI]; and what a file that would draw another motion is refused.
//   the call     the first call starts at its start angle and STEPS it before the
//                offset is read, so the first frame is already a step on; the frame
//                capped at 200 ms; one turn taken off only when strictly above it,
//                once, never a modulo; a frame of 0 (paused) holds the angle.
//   the offset   cos(angle) x stride x sign(speed), along y or x, turned by the axis
//                counter-clockwise on a +y-down screen; zero before the first call.
//   the bob      a period of PI seconds and an amplitude of 1.5 on the port's 60 Hz
//                tick, over thousands of ticks and their wraps.
//   the phases   drawn in order from a seeded stream, the same for the same seed,
//                inside [0, PI]; each level's stream seeded with its own name.
//   the census   over the 128 converted levels: 470 crystals (35 `crystal`, 435
//                `crystal.ent`) and 39 keys (8 `key`, 31 `key.ent`), none carrying
//                `speed` or `stride`, so every one takes its callback's defaults.
//
// The arithmetic is pure and runs anywhere. The census reads the converted levels
// from outside this repository, and is skipped without them, never with 77.

#include "TestHarness.hpp"

#include "sim/Motion.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;

// The port's tick as the layer hands it over: a float's 1/60 s, in milliseconds.
const double kTickMs = static_cast<double>(1.0f / 60.0f) * 1000.0;
// AngelScript's PI, the float 3.141592654f, and PI x 2f.
const double kFloatPi = static_cast<double>(3.141592654f);
const double kFloatTwoPi = static_cast<double>(3.141592654f * 2.0f);

bool Near(double a, double b, double eps = 1e-12) {
    return std::fabs(a - b) <= eps;
}

bool NearV(const glm::dvec2& a, const glm::dvec2& b, double eps = 1e-12) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps);
}

std::string Show(const glm::dvec2& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

Motion::Rules TheRules() {
    Motion::Rules rules;
    std::string error;
    CHECK_MSG(Motion::LoadRules(kPortData + "/motions.json", rules, error), error);
    return rules;
}

bool Refused(const std::string& text, const std::string& named) {
    const std::filesystem::path broken = std::filesystem::temp_directory_path() / "supersonic-test-mp-motion.json";
    {
        std::ofstream out(broken, std::ios::binary);
        out << text;
    }
    Motion::Rules refused;
    std::string error;
    const bool ok = !Motion::LoadRules(broken.string(), refused, error) && error.find(named) != std::string::npos;
    if (!ok) std::printf("    refusal of '%s' said: %s\n", named.c_str(), error.c_str());
    std::error_code ec;
    std::filesystem::remove(broken, ec);
    return ok;
}

std::string File(const std::string& linear, const std::string& crystal, const std::string& key) {
    return "{\"linear_motion\": {" + linear + "}, \"crystal\": {" + crystal + "}, \"key\": {" + key + "}}";
}

const std::string kLinear = "\"frame_cap_ms\": 200.0, \"wrap_rad\": 6.2831854820251465";
const std::string kRow = "\"entities\": [\"crystal\"], \"speed\": 2.0, \"stride\": 1.5, \"vertical\": true, "
                         "\"axis_deg\": 0.0, \"start_angle_from\": 0.0, \"start_angle_to\": 3.1415927410125732";

void TheRulesAreMotionsJsons() {
    const Motion::Rules rules = TheRules();
    CHECK_MSG(rules.frameCapMs == 200.0, "unitsPerSecond's min(200, frame)");
    CHECK_MSG(rules.wrapRad == kFloatTwoPi, "PI x 2f, in float: " + std::to_string(rules.wrapRad));
    for (const Motion::Row* row : {&rules.crystal, &rules.key}) {
        CHECK_EQ(row->speed, 2.0);
        CHECK_EQ(row->stride, 1.5);
        CHECK(row->vertical);
        CHECK_EQ(row->axisDeg, 0.0);
        CHECK_EQ(row->startFrom, 0.0);
        CHECK_MSG(row->startTo == kFloatPi, "randF(PI), PI the float: " + std::to_string(row->startTo));
    }
    CHECK_MSG(rules.crystal.entities.size() == 1 && rules.crystal.Names("crystal") && !rules.crystal.Names("key"),
              "ETHCallback_crystal's entity, without its .ent");
    CHECK_MSG(rules.key.entities.size() == 1 && rules.key.Names("key") && !rules.key.Names("key_pos"),
              "ETHCallback_key's, and key_pos is another entity");

    // What would draw another motion than the script's is refused, by name.
    {
        const std::filesystem::path good = std::filesystem::temp_directory_path() / "supersonic-test-mp-motion-good.json";
        {
            std::ofstream out(good, std::ios::binary);
            out << File(kLinear, kRow, kRow);
        }
        Motion::Rules read;
        std::string error;
        CHECK_MSG(Motion::LoadRules(good.string(), read, error) && read.key.Names("crystal"), "the fixture itself reads: " + error);
        std::error_code ec;
        std::filesystem::remove(good, ec);
    }
    CHECK(Refused(File("\"frame_cap_ms\": 0.0, \"wrap_rad\": 6.28", kRow, kRow), "frame_cap_ms"));
    CHECK(Refused(File("\"frame_cap_ms\": 200.0", kRow, kRow), "wrap_rad"));
    CHECK(Refused("{\"linear_motion\": {" + kLinear + "}, \"crystal\": {" + kRow + "}}", "key is missing"));
    std::string still = kRow;
    still.replace(still.find("\"stride\": 1.5"), 13, "\"stride\": 0.0");
    CHECK(Refused(File(kLinear, still, kRow), "stride is above 0"));
    std::string halted = kRow;
    halted.replace(halted.find("\"speed\": 2.0"), 12, "\"speed\": 0.0");
    CHECK(Refused(File(kLinear, kRow, halted), "speed is not 0"));
    std::string spelled = kRow;
    spelled.replace(spelled.find("[\"crystal\"]"), 11, "[\"crystal.ent\"]");
    CHECK(Refused(File(kLinear, spelled, kRow), "bare entity name"));
    std::string upsideDown = kRow;
    upsideDown.replace(upsideDown.find("\"start_angle_from\": 0.0"), 23, "\"start_angle_from\": 4.0");
    CHECK(Refused(File(kLinear, upsideDown, kRow), "start_angle_to"));
    std::string noAxis = kRow;
    noAxis.replace(noAxis.find("\"vertical\": true"), 16, "\"vertical\": 1");
    CHECK(Refused(File(kLinear, noAxis, kRow), "vertical"));
}

void TheFirstCallStepsBeforeTheOffsetIsRead() {
    const Motion::Rules rules = TheRules();
    Motion::Linear bob = Motion::Start(rules.crystal, 1.0);
    // Until the first call the entity is where the level put it.
    CHECK(!bob.started);
    CHECK(NearV(Motion::OffsetPx(bob), glm::dvec2(0.0)));
    Motion::Advance(rules, bob, kTickMs);
    CHECK(bob.started);
    const double angle = 1.0 + 2.0 * kTickMs / 1000.0;
    CHECK_MSG(Near(bob.angle, angle), "the start angle and one step: " + std::to_string(bob.angle));
    // So the first frame drawn is cos(start + step), not cos(start).
    CHECK_MSG(NearV(Motion::OffsetPx(bob), glm::dvec2(0.0, 1.5 * std::cos(angle))),
              "the first offset: " + Show(Motion::OffsetPx(bob)));
    CHECK(!NearV(Motion::OffsetPx(bob), glm::dvec2(0.0, 1.5 * std::cos(1.0)), 1e-3));
    // A later call does not start again.
    Motion::Advance(rules, bob, kTickMs);
    CHECK(Near(bob.angle, angle + 2.0 * kTickMs / 1000.0));
}

void TheFrameIsCappedAndAPausedFrameHolds() {
    const Motion::Rules rules = TheRules();
    Motion::Linear bob = Motion::Start(rules.crystal, 0.5);
    Motion::Advance(rules, bob, 500.0);
    CHECK_MSG(Near(bob.angle, 0.5 + 2.0 * 0.2), "a 500 ms frame steps 200 ms: " + std::to_string(bob.angle));
    Motion::Advance(rules, bob, 200.0);
    CHECK(Near(bob.angle, 0.5 + 2.0 * 0.4));
    // m_factor 0: a paused frame, however long, steps nothing; the first call still starts.
    const double held = bob.angle;
    for (int frame = 0; frame < 100; ++frame) Motion::Advance(rules, bob, 0.0);
    CHECK_EQ(bob.angle, held);
    Motion::Linear paused = Motion::Start(rules.key, 2.0);
    Motion::Advance(rules, paused, 0.0);
    CHECK(paused.started && paused.angle == 2.0);
}

void OneTurnIsTakenOffOnlyAboveATurnAndOnce() {
    const Motion::Rules rules = TheRules();
    // Exactly at the turn: CMPf; JNP keeps it.
    Motion::Linear at = Motion::Start(rules.crystal, kFloatTwoPi);
    Motion::Advance(rules, at, 0.0);
    CHECK_MSG(at.angle == kFloatTwoPi, "not above the turn, not wrapped: " + std::to_string(at.angle));
    // Just past it: one turn off.
    Motion::Linear past = Motion::Start(rules.crystal, kFloatTwoPi - 0.01);
    Motion::Advance(rules, past, kTickMs);
    CHECK_MSG(Near(past.angle, kFloatTwoPi - 0.01 + 2.0 * kTickMs / 1000.0 - kFloatTwoPi),
              "wrapped once: " + std::to_string(past.angle));
    // Three turns: one off per call, so it is still above a turn after it. Not a modulo.
    Motion::Linear far = Motion::Start(rules.crystal, 3.0 * kFloatTwoPi);
    Motion::Advance(rules, far, 0.0);
    CHECK_MSG(Near(far.angle, 2.0 * kFloatTwoPi), "one turn a call: " + std::to_string(far.angle));
    Motion::Advance(rules, far, 0.0);
    CHECK(Near(far.angle, kFloatTwoPi));
}

void ABobIsPiSecondsAndItsStride() {
    const Motion::Rules rules = TheRules();
    Motion::Linear bob = Motion::Start(rules.crystal, 0.25);
    double expected = 0.25;
    double highest = -1e9;
    double lowest = 1e9;
    int off = 0;
    int above = 0;
    for (int tick = 1; tick <= 20000; ++tick) {
        Motion::Advance(rules, bob, kTickMs);
        expected += 2.0 * kTickMs / 1000.0;
        if (expected > kFloatTwoPi) expected -= kFloatTwoPi;
        const glm::dvec2 offset = Motion::OffsetPx(bob);
        if (!Near(bob.angle, expected, 1e-9) || !NearV(offset, glm::dvec2(0.0, 1.5 * std::cos(expected)), 1e-9)) ++off;
        if (bob.angle > kFloatTwoPi || bob.angle < 0.0) ++above;
        highest = std::max(highest, offset.y);
        lowest = std::min(lowest, offset.y);
    }
    std::printf("  a crystal's bob over 20000 ticks: y %.4f..%.4f\n", lowest, highest);
    CHECK_MSG(off == 0, std::to_string(off) + " ticks off 1.5 cos(start + 2 t), wrapped at a turn");
    CHECK_EQ(above, 0);
    CHECK_MSG(highest > 1.4999 && highest <= 1.5 && lowest < -1.4999 && lowest >= -1.5,
              "the amplitude is the stride, 1.5");
    // The period: 2 rad/s is PI seconds. Where it is one period on, on the continuous angle.
    Motion::Linear a = Motion::Start(rules.crystal, 1.0);
    Motion::Advance(rules, a, 1000.0 * 0.1);
    Motion::Linear b = a;
    for (int step = 0; step < 31; ++step) Motion::Advance(rules, b, 100.0);
    Motion::Advance(rules, b, (3.14159265358979323846 - 3.1) * 1000.0);
    CHECK_MSG(NearV(Motion::OffsetPx(a), Motion::OffsetPx(b), 1e-6),
              "PI seconds later it is where it was: " + Show(Motion::OffsetPx(a)) + " and " + Show(Motion::OffsetPx(b)));
}

void TheSignAndTheAxis() {
    const Motion::Rules rules = TheRules();
    // A negative speed runs the angle down, never wraps it, and turns the swing over.
    Motion::Row backwards = rules.crystal;
    backwards.speed = -2.0;
    Motion::Linear down = Motion::Start(backwards, 0.1);
    for (int tick = 0; tick < 600; ++tick) Motion::Advance(rules, down, kTickMs);
    CHECK_MSG(Near(down.angle, 0.1 - 600.0 * 2.0 * kTickMs / 1000.0, 1e-9), "down, unwrapped: " + std::to_string(down.angle));
    CHECK(NearV(Motion::OffsetPx(down), glm::dvec2(0.0, -1.5 * std::cos(down.angle)), 1e-12));

    // At angle 0 the offset is the whole stride, so the axis alone shows.
    const auto at = [&rules](bool vertical, double axisDeg) {
        Motion::Row row = rules.crystal;
        row.vertical = vertical;
        row.axisDeg = axisDeg;
        Motion::Linear motion = Motion::Start(row, 0.0);
        Motion::Advance(rules, motion, 0.0);
        return Motion::OffsetPx(motion);
    };
    const double h = 1.5 * std::sqrt(0.5);
    CHECK_MSG(NearV(at(true, 0.0), glm::dvec2(0.0, 1.5)), "vertical: down the screen, +y");
    CHECK_MSG(NearV(at(false, 0.0), glm::dvec2(1.5, 0.0)), "horizontal: right");
    // rotateZ as multiply takes it: (x cos a + y sin a, -x sin a + y cos a).
    CHECK_MSG(NearV(at(false, 90.0), glm::dvec2(0.0, -1.5), 1e-12),
              "horizontal turned 90: up the screen, counter-clockwise: " + Show(at(false, 90.0)));
    CHECK_MSG(NearV(at(true, 90.0), glm::dvec2(1.5, 0.0), 1e-12), "vertical turned 90: right: " + Show(at(true, 90.0)));
    CHECK_MSG(NearV(at(false, 45.0), glm::dvec2(h, -h), 1e-12), "horizontal at 45: right and up: " + Show(at(false, 45.0)));
    CHECK_MSG(NearV(at(false, -90.0), glm::dvec2(0.0, 1.5), 1e-12), "and -90 down: " + Show(at(false, -90.0)));
}

void ThePhasesAreTheSeedsInOrder() {
    const Motion::Rules rules = TheRules();
    Motion::Phases one(20260917u);
    Motion::Phases two(20260917u);
    Motion::Phases other(20260918u);
    int differ = 0;
    int outside = 0;
    int sameAsOther = 0;
    double sum = 0.0;
    std::set<double> distinct;
    constexpr int kDraws = 5000;
    for (int i = 0; i < kDraws; ++i) {
        const Motion::Row& row = (i % 3 == 0) ? rules.key : rules.crystal;
        const double a = one.Next(row);
        const double b = two.Next(row);
        const double c = other.Next(row);
        if (a != b) ++differ;
        if (a == c) ++sameAsOther;
        if (a < 0.0 || a > kFloatPi) ++outside;
        sum += a;
        distinct.insert(a);
    }
    const double mean = sum / kDraws;
    std::printf("  %d start angles: mean %.4f, %zu distinct\n", kDraws, mean, distinct.size());
    CHECK_MSG(differ == 0, "the same seed draws the same angles in the same order");
    CHECK_MSG(outside == 0, "every one on [0, PI]");
    CHECK_MSG(sameAsOther < 10, "another seed draws others");
    CHECK_MSG(std::fabs(mean - kFloatPi / 2.0) < 0.05, "uniform: its mean is PI / 2");
    CHECK(distinct.size() > 4990);
    // A row with no range gives its one angle and draws nothing.
    Motion::Row fixed = rules.crystal;
    fixed.startFrom = fixed.startTo = 1.25;
    Motion::Phases three(20260917u);
    CHECK_EQ(three.Next(fixed), 1.25);
    Motion::Phases four(20260917u);
    CHECK_EQ(three.Next(rules.crystal), four.Next(rules.crystal));
}

void EveryLevelDrawsItsOwnPhases() {
    const Motion::Rules rules = TheRules();
    // One seed for every level would give each level's first crystal the phase of
    // every other level's first; the original draws each afresh.
    std::set<std::uint32_t> seeds;
    std::set<double> firsts;
    const char* worlds[] = {"", "a", "b", "c"};
    for (const char* world : worlds) {
        for (int i = 0; i < 32; ++i) {
            const std::string name = "level" + std::to_string(i) + world;
            seeds.insert(Motion::LevelSeed(20260917u, name));
            Motion::Phases phases(Motion::LevelSeed(20260917u, name));
            firsts.insert(phases.Next(rules.crystal));
        }
    }
    CHECK_MSG(seeds.size() == 128 && firsts.size() == 128,
              "128 levels, " + std::to_string(seeds.size()) + " seeds and " + std::to_string(firsts.size()) + " first angles");
    CHECK_EQ(Motion::LevelSeed(20260917u, "level5"), Motion::LevelSeed(20260917u, "level5"));
    // FNV-1a over the name, xor the base: the empty name leaves the offset basis.
    CHECK_EQ(Motion::LevelSeed(0u, ""), 2166136261u);
    CHECK_EQ(Motion::LevelSeed(20260917u, ""), 20260917u ^ 2166136261u);
}

void EveryLevelsCrystalsAndKeys() {
    const Motion::Rules rules = TheRules();
    int levels = 0, crystal = 0, crystalEnt = 0, key = 0, keyEnt = 0, carrying = 0;
    std::string firstCarrying;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kLevels, ec)) {
        if (entry.path().extension() != ".tscn") continue;
        ++levels;
        const std::string level = entry.path().stem().string();
        Tscn::Scene scene;
        std::string error;
        if (!Tscn::Load(entry.path().string(), scene, error)) {
            CHECK_MSG(false, level + ": " + error);
            continue;
        }
        for (const Tscn::Node& node : scene.nodes) {
            if (node.parent != ".") continue;
            const std::string name = Roles::EntityName(node);
            std::string bare = name;
            if (bare.size() > 4 && bare.compare(bare.size() - 4, 4, ".ent") == 0) bare.resize(bare.size() - 4);
            const bool bobs = rules.crystal.Names(bare) || rules.key.Names(bare);
            if (!bobs) continue;
            if (name == "crystal") ++crystal;
            if (name == "crystal.ent") ++crystalEnt;
            if (name == "key") ++key;
            if (name == "key.ent") ++keyEnt;
            // A `speed` would skip the callback's own speed and stride (ins 1-12).
            if (node.Meta("speed") != nullptr || node.Meta("stride") != nullptr) {
                if (carrying++ == 0) firstCarrying = level + " " + node.name;
            }
        }
    }
    std::printf("  %d levels: %d crystal + %d crystal.ent, %d key + %d key.ent, %d carrying speed or stride\n", levels,
                crystal, crystalEnt, key, keyEnt, carrying);
    CHECK_EQ(levels, 128);
    CHECK_EQ(crystal, 35);
    CHECK_EQ(crystalEnt, 435);
    CHECK_EQ(key, 8);
    CHECK_EQ(keyEnt, 31);
    CHECK_MSG(carrying == 0, "none takes other than the callback's speed and stride; the first " + firstCarrying);
}

} // namespace

int main() {
    TheRulesAreMotionsJsons();
    TheFirstCallStepsBeforeTheOffsetIsRead();
    TheFrameIsCappedAndAPausedFrameHolds();
    OneTurnIsTakenOffOnlyAboveATurnAndOnce();
    ABobIsPiSecondsAndItsStride();
    TheSignAndTheAxis();
    ThePhasesAreTheSeedsInOrder();
    EveryLevelDrawsItsOwnPhases();

    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec)) {
        std::printf("test_mp_motion: the levels' census SKIPPED - needs the converted levels at %s.\n"
                    "  They live outside this repository; configure with -DSUPERSONIC_MAGICPORTALS_LEVELS=...\n",
                    kLevels.c_str());
        return ::test::summary("test_mp_motion", 45);
    }
    EveryLevelsCrystalsAndKeys();
    return ::test::summary("test_mp_motion", 50);
}
