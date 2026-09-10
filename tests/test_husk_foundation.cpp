// HUSK's port, the pieces every tick is built from: the RON reader, Rust's
// cast and NaN rules, the glam primitives and PCG32, and every catalog and
// mission value - the last three against what the Rust original printed.
//
// The goldens come from games/husk/oracle, which links the game itself
// (Desktop/Test-Game at 721e19e). To regenerate, with a Rust toolchain:
//
//   cd games/husk/oracle && cargo build --release
//   target/release/husk-oracle catalogs   > ../golden/catalogs.txt
//   target/release/husk-oracle primitives > ../golden/primitives.txt
//
// Floats are compared as IEEE bits throughout, so "equal" here means
// bit-identical, not close.

#include "TestHarness.hpp"

#include "sim/Data.hpp"
#include "sim/Fnv.hpp"
#include "sim/Mission.hpp"
#include "sim/Ron.hpp"
#include "sim/Rng.hpp"
#include "sim/RustMath.hpp"
#include "sim/Scenario.hpp"
#include "sim/Vec2.hpp"
#include "sim/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace husk;

namespace {

std::vector<std::string> readLines(const std::filesystem::path& p) {
    std::vector<std::string> out;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) out.push_back(line);
    return out;
}

std::string hex(uint64_t v, int width) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*llx", width, static_cast<unsigned long long>(v));
    return buf;
}

// The first line two texts disagree on, or empty when they agree.
std::string firstDifference(const std::vector<std::string>& expected, const std::vector<std::string>& actual) {
    const size_t n = std::max(expected.size(), actual.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string e = i < expected.size() ? expected[i] : "<missing>";
        const std::string a = i < actual.size() ? actual[i] : "<missing>";
        if (e != a) return "line " + std::to_string(i + 1) + ":\n      rust " + e + "\n      port " + a;
    }
    return {};
}

void testRonReadsWhatSerdeWrites() {
    const ron::Value v = ron::parse(R"(
        // a comment, then the value
        (
            name: "Stalker",   /* block /* nested */ comment */
            list: [1, 2, 3,],
            tuple: (0.25, 0.9),
            some: Some((Flora, 1)),
            none: None,
            variant: FloraRegen(rate: 4.0, radius: 9.0),
            newtype: TimeAtLeast(1.5),
            pair: ObjectiveActiveForAtLeast("hold", 30.0),
            unit: PlayerWiped,
            map: { "soldier": [(item: "rebar knuckles", chance: 0.08)] },
            text: "line\nbreak \"quoted\"",
            flag: true,
        ))");
    CHECK(v.kind == ron::Value::Kind::Struct);
    CHECK(ron::asString(ron::require(v, "name", "t"), "t") == "Stalker");
    CHECK(ron::asList(ron::require(v, "list", "t"), "t").size() == 3);
    CHECK(ron::asTuple(ron::require(v, "tuple", "t"), 2, "t").size() == 2);

    const ron::Value* some = ron::asOption(ron::require(v, "some", "t"), "t");
    CHECK(some && ron::asTuple(*some, 2, "t")[0].text == "Flora");
    CHECK(ron::asOption(ron::require(v, "none", "t"), "t") == nullptr);

    const ron::Variant variant = ron::asVariant(ron::require(v, "variant", "t"), "t");
    CHECK(variant.name == "FloraRegen" && variant.payload && variant.payload->kind == ron::Value::Kind::Struct);
    const ron::Variant newtype = ron::asVariant(ron::require(v, "newtype", "t"), "t");
    CHECK(newtype.name == "TimeAtLeast" && newtype.payload && newtype.payload->items.size() == 1);
    const ron::Variant pair = ron::asVariant(ron::require(v, "pair", "t"), "t");
    CHECK(pair.payload && pair.payload->items.size() == 2);
    const ron::Variant unit = ron::asVariant(ron::require(v, "unit", "t"), "t");
    CHECK(unit.name == "PlayerWiped" && unit.payload == nullptr);

    const ron::Value& map = ron::require(v, "map", "t");
    CHECK(map.kind == ron::Value::Kind::Map && map.keys.size() == 1 && map.keys[0].text == "soldier");
    CHECK(ron::asString(ron::require(v, "text", "t"), "t") == "line\nbreak \"quoted\"");
    CHECK(ron::asBool(ron::require(v, "flag", "t"), "t"));

    bool threw = false;
    try {
        ron::parse("(a: 1,");
    } catch (const ron::Error&) {
        threw = true;
    }
    CHECK_MSG(threw, "an unterminated struct is an error, not a value");

    // A missing required field is reported by name, as serde does.
    std::string message;
    try {
        ron::require(v, "absent", "units/stalker.ron");
    } catch (const ron::Error& e) {
        message = e.what();
    }
    CHECK_MSG(message.find("absent") != std::string::npos && message.find("stalker.ron") != std::string::npos, message);

    // The type decides the conversion: an f32 that says 100 is 100.0, and a
    // u32 that says 2.0 is an error.
    CHECK(ron::asF32(ron::parse("100"), "t") == 100.0f);
    bool intThrew = false;
    try {
        ron::asU32(ron::parse("2.0"), "t");
    } catch (const ron::Error&) {
        intThrew = true;
    }
    CHECK(intThrew);

    // Correctly rounded, as Rust's f32::from_str: 0.1 is the nearest float.
    CHECK_EQ(floatBits(ron::asF32(ron::parse("0.1"), "t")), 0x3dcccccdu);
    CHECK_EQ(floatBits(ron::asF32(ron::parse("-60.0"), "t")), 0xc2700000u);
}

void testRustCastsSaturate() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(asI32(-1.5f) == -1);
    CHECK(asI32(1.9f) == 1);
    CHECK(asI32(1e10f) == INT32_MAX);
    CHECK(asI32(-1e10f) == INT32_MIN);
    CHECK(asI32(nan) == 0);
    CHECK(asU32(-3.0f) == 0);
    CHECK(asU32(-0.5f) == 0);
    CHECK(asU32(5e9f) == UINT32_MAX);
    CHECK(asU64(1e30f) == UINT64_MAX);
    CHECK(asU64(nan) == 0);

    // f32::max/min give back the other operand when one is NaN, on either side.
    CHECK(fmaxr(nan, 1.0f) == 1.0f);
    CHECK(fmaxr(1.0f, nan) == 1.0f);
    CHECK(fminr(nan, 2.0f) == 2.0f);
    CHECK(fminr(2.0f, nan) == 2.0f);
    // f32::clamp lets a NaN through.
    CHECK(std::isnan(clampr(nan, 0.0f, 1.0f)));
    CHECK(clampr(-2.0f, 0.0f, 1.0f) == 0.0f);
    CHECK(clampr(2.0f, 0.0f, 1.0f) == 1.0f);
}

// One line of the oracle's `primitives`, from the same generator and in the
// same order. Every draw is its own statement: Rust evaluates Vec2::new's
// arguments left to right and C++ does not promise to.
std::string primitiveLine(int i, Pcg32& r) {
    const float ax = r.rangeF32(-60.0f, 60.0f);
    const float ay = r.rangeF32(-60.0f, 60.0f);
    const Vec2 a(ax, ay);
    Vec2 b;
    if (i % 8 == 0) {
        const float dx = r.rangeF32(-0.001f, 0.001f);
        const float dy = r.rangeF32(-0.001f, 0.001f);
        b = a + Vec2(dx, dy);
    } else {
        const float bx = r.rangeF32(-60.0f, 60.0f);
        const float by = r.rangeF32(-60.0f, 60.0f);
        b = Vec2(bx, by);
    }
    const float k = r.rangeF32(0.0f, 10.0f);
    const float len = a.length();
    const float d = a.distance(b);
    const Vec2 n = len > 1e-4f ? a.normalize() : kZero;
    const Vec2 c = a.clampLengthMax(k);
    const Vec2 cl = a.clamp(Vec2::splat(-50.0f + k), Vec2::splat(50.0f - k));
    const Vec2 chase = d > 1e-4f ? (b - a) / d * k : kZero;
    const Vec2 stepped = a + chase * kSimDt;

    std::ostringstream o;
    o << "p " << i << " len=" << bitsHex(len) << " lsq=" << bitsHex(a.lengthSquared()) << " dist=" << bitsHex(d)
      << " dsq=" << bitsHex(a.distanceSquared(b)) << " dot=" << bitsHex(a.dot(b)) << " norm=" << bitsHex(n.x) << ','
      << bitsHex(n.y) << " clampmax=" << bitsHex(c.x) << ',' << bitsHex(c.y) << " clamp=" << bitsHex(cl.x) << ','
      << bitsHex(cl.y) << " chase=" << bitsHex(chase.x) << ',' << bitsHex(chase.y) << " step=" << bitsHex(stepped.x)
      << ',' << bitsHex(stepped.y) << " floor=" << asI32(std::floor(a.x / 2.0f)) << " trunc=" << asI32(a.y)
      << " round=" << asI32(std::round(a.x)) << " ceil=" << asI32(std::ceil(a.y * 0.5f)) << " u32=" << asU32(a.x)
      << " sqrt=" << bitsHex(std::sqrt(k * 7.0f));
    return o.str();
}

// The glam calls the sim makes, and PCG32 itself, on 4,000 inputs drawn from
// the sim's own generator. This is what catches a fused multiply-add: the
// compiler flag that forbids one is load-bearing, and this is its proof.
void testPrimitivesMatchTheOracle() {
    const auto golden = readLines(goldenDir() / "primitives.txt");
    CHECK_EQ(golden.size(), static_cast<size_t>(4005));

    std::vector<std::string> ours;
    for (uint64_t seed : {0ull, 7ull, 11ull, 0x4855534Bull, 0xFFFFFFFFFFFFFFFFull}) {
        Pcg32 r = makeSimRng(seed);
        std::string raw;
        for (int i = 0; i < 8; ++i) raw += (i ? "," : "") + hex(r.nextU32(), 8);
        const auto [state, inc] = r.saveState();
        ours.push_back("rng " + std::to_string(seed) + " " + raw + " state=" + hex(state, 16) + "," + hex(inc, 16));
    }
    Pcg32 r(0x5eed, 0xda3e39cb94b95bdbull);
    for (int i = 0; i < 4000; ++i) ours.push_back(primitiveLine(i, r));

    const std::string diff = firstDifference(golden, ours);
    CHECK_MSG(diff.empty(), "primitives differ from the Rust original at " + diff);
}

// Every catalog value, both missions and their sidecar files, parsed here and
// by serde's RON, compared as bits.
void testCatalogsMatchTheOracle() {
    const auto golden = readLines(goldenDir() / "catalogs.txt");
    CHECK(golden.size() > 250);

    const auto cats = sharedCatalogs();
    std::string text = dumpCatalogs(*cats);
    text += dumpMission(loadMissionDef(cats->root, "first_light"), "first_light");
    text += dumpMission(loadMissionDef(cats->root, "heartwood"), "heartwood");

    const std::string diff = firstDifference(golden, splitLines(text));
    CHECK_MSG(diff.empty(), "data differs from the Rust original at " + diff);
}

// data.rs's catalogs_load_with_stable_order: ids follow the sorted FILE names,
// not the display names inside them.
void testCatalogIdsFollowSortedFileNames() {
    const Catalogs& c = *sharedCatalogs();
    CHECK_EQ(c.units.defs.size(), static_cast<size_t>(15));
    const char* expected[] = {"artillery", "cruiser", "extractor",    "golem", "ifv",
                              "monolith",  "officer", "riot shield",  "siphon", "soldier",
                              "stalker",   "swat trooper", "tank",    "thrall", "wisp"};
    for (uint16_t i = 0; i < 15; ++i) CHECK_MSG(c.units.id(expected[i]) == i, expected[i]);
    CHECK(!c.units.def(c.units.id("siphon")).attack.has_value());
    CHECK_EQ(c.buildings.defs.size(), static_cast<size_t>(4));
    // the Tower sorts last, so existing ids and saves are unshifted
    CHECK(c.buildings.id("tower") == 3);
    CHECK(c.buildings.def(c.buildings.id("tower")).attack.has_value());
    CHECK(c.sources.defs.size() >= 6);
    CHECK(c.sources.def(c.sources.id("ancient tree")).anima > 0.0f);
    CHECK(c.units.def(c.units.id(c.hero.unit)).name == "Extractor");
    CHECK_EQ(c.abilities.defs.size(), static_cast<size_t>(4));
    CHECK(c.items.defs.size() >= 3);
}

// mission.rs's install_mission: an unknown name is a load-time error, and the
// world is left exactly as it was.
void testBadMissionDataLeavesTheWorldAlone() {
    auto cats = sharedCatalogs();
    World w(cats);
    MissionDef def = loadMissionDef(cats->root, "first_light");
    TriggerAction typo;
    typo.kind = TriggerAction::Kind::SpawnSquad;
    typo.name = "golum";
    typo.count = 2;
    def.triggers.front().actions.push_back(typo);

    const std::optional<std::string> err = installMission(w, def);
    CHECK_MSG(err && err->find("unknown unit \"golum\"") != std::string::npos, err.value_or("installed"));
    CHECK(w.entities.empty());
    CHECK(!w.mission.has_value());
    CHECK(w.idAlloc == 0);
}

} // namespace

void runTests() {
    testRonReadsWhatSerdeWrites();
    testRustCastsSaturate();
    testPrimitivesMatchTheOracle();
    testCatalogsMatchTheOracle();
    testCatalogIdsFollowSortedFileNames();
    testBadMissionDataLeavesTheWorldAlone();
}

TEST_MAIN("test_husk_foundation", 60)
