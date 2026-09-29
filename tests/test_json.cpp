// Tests for the hand-written JSON reader.
//
// Json.hpp is 300 lines of recursive descent and it parses every .scene,
// .prefab, .material and game.manifest the engine loads. Nothing tested it
// directly: it was exercised only through test_serialize's round-trips, which
// feed it output the engine itself produced and therefore never feed it
// anything malformed.
//
// The two failures pinned hardest here both produced a file that could not be
// read back rather than a message saying why:
//
//   - unbounded recursion, where about two kilobytes of '[' exhausted the
//     stack inside Json::Parse, killing the process in the middle of the read
//     that SceneSerializer performs specifically so a bad file cannot damage
//     the open scene;
//   - non-finite floats, which the writer emitted and the reader rejects, so a
//     scene saved after typing 1e40 into the inspector reported success and
//     was then permanently unloadable.

#include "TestHarness.hpp"
#include "core/Json.hpp"

#include <string>
#include <utility>
#include <vector>

using namespace Supersonic;

namespace {

bool parses(const std::string& text) {
    Json::Value out;
    std::string error;
    return Json::Parse(text, out, error);
}

Json::Value parsed(const std::string& text) {
    Json::Value out;
    std::string error;
    Json::Parse(text, out, error);
    return out;
}

using Keys = std::vector<std::string>;

std::string joined(const Keys& keys) {
    std::string out;
    for (const std::string& key : keys) out += (out.empty() ? "" : ",") + key;
    return "[" + out + "]";
}

// Order as a message, so a failure says which order it got.
#define CHECK_KEYS(value, ...) \
    do { \
        const Keys _got = (value).OrderedKeys(); \
        const Keys _want{__VA_ARGS__}; \
        CHECK_MSG(_got == _want, "got " + joined(_got) + ", expected " + joined(_want)); \
    } while (false)

std::string errorFor(const std::string& text) {
    Json::Value out;
    std::string error;
    Json::Parse(text, out, error);
    return error;
}

} // namespace

static void testParsesTheShapesTheEngineWrites() {
    CHECK(parses("{}"));
    CHECK(parses("[]"));
    CHECK(parses(R"({"Scene": "MainScene", "Entities": []})"));
    CHECK(parses(R"({"Position": [1.0, -2.5, 3e2]})"));
    CHECK(parses(R"({"a": true, "b": false, "c": null})"));

    Json::Value out;
    std::string error;
    CHECK(Json::Parse(R"({"Entities": [{"Tag": "Cube"}]})", out, error));
    CHECK(error.empty());
    CHECK_EQ(out["Entities"].AsArray().size(), size_t{1});
    CHECK(out["Entities"].AsArray()[0]["Tag"].AsString("") == "Cube");
}

static void testDeepNestingIsRefusedRatherThanRecursedInto() {
    // The whole point: this must RETURN, not crash. A depth that overflows the
    // stack on the way in would also overflow being freed, because Value's
    // destructor recurses as well - which is why the limit is enforced during
    // the parse rather than checked after it.
    const std::string deep(4000, '[');
    CHECK_MSG(!parses(deep), "nesting past the limit must be refused, not recursed into");
    CHECK_MSG(errorFor(deep).find("nesting") != std::string::npos,
              "the failure should say what was wrong");

    // A structure the engine could plausibly write must still parse: scenes
    // nest object -> array -> object -> array, nowhere near the limit.
    std::string ok;
    for (int i = 0; i < 30; ++i) ok += "[";
    ok += "1";
    for (int i = 0; i < 30; ++i) ok += "]";
    CHECK_MSG(parses(ok), "ordinary nesting must not be caught by the depth limit");
}

static void testTrailingContentIsRejected() {
    // "{...} garbage" used to return the leading object and report success, so
    // a file that had been truncated and appended to loaded as though whole.
    CHECK_MSG(!parses("{} nonsense"), "trailing content must fail the parse");
    CHECK_MSG(!parses("[1, 2] [3]"), "a second top-level value must fail the parse");
    CHECK_MSG(parses("  {\"a\": 1}  \n "), "trailing whitespace is not trailing content");
}

static void testDuplicateKeysTakeTheLastValue() {
    // std::map::emplace keeps the FIRST value and silently drops the second,
    // which is the opposite of what every other JSON reader does.
    Json::Value out;
    std::string error;
    CHECK(Json::Parse(R"({"Range": 10, "Range": 25})", out, error));
    CHECK_NEAR(out["Range"].AsFloat(0.0f), 25.0f);
}

static void testNonFiniteLiteralsAreNotNumbers() {
    // The reader has never accepted these. That is the half of the problem the
    // writer used to violate.
    CHECK(!parses(R"({"x": inf})"));
    CHECK(!parses(R"({"x": nan})"));
    CHECK(!parses(R"({"x": -inf})"));
}

static void testMalformedInputFailsWithAMessage() {
    for (const char* bad : {
             "{",
             "}",
             "[1, 2",
             R"({"unterminated: 1})",
             R"({"key" 1})",
             R"({"key": })",
             "[1,, 2]",
             "",
         }) {
        CHECK_MSG(!parses(bad), std::string("must reject: ") + bad);
        CHECK_MSG(!errorFor(bad).empty(), std::string("must explain: ") + bad);
    }
}

static void testMissingKeysFallBack() {
    // How the codec reads every optional field, so a scene written before a
    // component gained one still loads.
    Json::Value out;
    std::string error;
    CHECK(Json::Parse(R"({"Present": 3})", out, error));
    CHECK_NEAR(out["Absent"].AsFloat(7.5f), 7.5f);
    CHECK(out["Absent"].AsString("fallback") == "fallback");
    CHECK(out["Absent"].AsBool(true));
    CHECK_EQ(out["Absent"].AsArray().size(), size_t{0});
}

// --- Document order ------------------------------------------------------
//
// Object is a std::map, so a game reading its build list or a cost out of a
// data file got the keys alphabetically: Wolf Brigade's builds came out armory
// first where the file says barracks, and every "75 wood, 20 food" read as
// "20 food, 75 wood". OrderedKeys is the file's order, beside a map that does
// not move.

static void testKeysComeBackInTheOrderTheFileWroteThem() {
    const Json::Value data = parsed(
        R"({"barracks": {"cost": {"wood": 150}},
            "armory":   {"cost": {"wood": 100}},
            "tower":    {"cost": {"wood": 120, "food": 30}, "range": 400}})");

    CHECK_KEYS(data, "barracks", "armory", "tower");
    CHECK_KEYS(data["tower"], "cost", "range");
    CHECK_KEYS(data["tower"]["cost"], "wood", "food");
    CHECK_KEYS(data["barracks"]["cost"], "wood");
    CHECK_KEYS(parsed("{}"));
}

static void testTheMapUnderneathIsStillAlphabetical() {
    // The half that must NOT change: everything that iterates AsObject() -
    // the codec, the serialisers, a game's own loops - sees what it always
    // saw, and a lookup finds what it always found.
    const Json::Value cost = parsed(R"({"wood": 75, "food": 20})");

    Keys iterated;
    for (const auto& entry : cost.AsObject()) iterated.push_back(entry.first);
    CHECK_MSG(iterated == (Keys{"food", "wood"}), "got " + joined(iterated));
    CHECK_KEYS(cost, "wood", "food");

    CHECK_NEAR(cost["wood"].AsFloat(), 75.0f);
    CHECK_NEAR(cost["food"].AsFloat(), 20.0f);
    CHECK(cost.Has("wood"));
    CHECK(!cost.Has("stone"));
}

static void testObjectsInsideArraysKeepTheirOwnOrder() {
    const Json::Value waves = parsed(
        R"({"waves": [{"raider": 4, "brute": 1}, {"zealot": 2, "archer": 3, "brute": 2}]})");

    const Json::Array& list = waves["waves"].AsArray();
    CHECK_EQ(list.size(), size_t{2});
    if (list.size() != 2) return;
    CHECK_KEYS(list[0], "raider", "brute");
    CHECK_KEYS(list[1], "zealot", "archer", "brute");

    // An array is not an object, whatever it holds.
    CHECK_KEYS(waves["waves"]);
}

static void testARepeatedKeyKeepsItsFirstPlaceAndItsLastValue() {
    // The value half is testDuplicateKeysTakeTheLastValue's, and unchanged.
    // The place half matches Godot's Dictionary, JavaScript and Python: an
    // assignment to a key that is already there does not move it.
    const Json::Value repeated = parsed(R"({"b": 1, "a": 2, "b": 3})");
    CHECK_KEYS(repeated, "b", "a");
    CHECK_NEAR(repeated["b"].AsFloat(), 3.0f);
    CHECK_EQ(repeated.AsObject().size(), size_t{2});
}

static void testKeysAddedAfterTheParseFollowInMapOrder() {
    Json::Value value = parsed(R"({"wood": 75, "food": 20})");

    // Set is the one mutator. What it adds was never in the file, so it has
    // no file order to keep, and it follows alphabetically.
    value.Set("stone", Json::Value(5.0));
    value.Set("gold", Json::Value(1.0));
    CHECK_KEYS(value, "wood", "food", "gold", "stone");

    // Replacing a key the file wrote is not moving it.
    value.Set("wood", Json::Value(90.0));
    CHECK_KEYS(value, "wood", "food", "gold", "stone");
    CHECK_NEAR(value["wood"].AsFloat(), 90.0f);

    // Nothing removes a key today, so "a recorded key that is gone is left
    // out" has no public path to exercise. The nearest is Set on something
    // that is not an object, which starts a fresh one with nothing recorded.
    Json::Value list = parsed("[1, 2]");
    list.Set("b", Json::Value(true));
    list.Set("a", Json::Value(false));
    CHECK_KEYS(list, "a", "b");
}

static void testAnObjectBuiltInCodeIsInMapOrder() {
    Json::Object built;
    built["wood"] = Json::Value(75.0);
    built["food"] = Json::Value(20.0);
    CHECK_KEYS(Json::Value(built), "food", "wood");
}

static void testCopiesAndMovesKeepTheOrder() {
    // Lookups hand out references into the tree and callers copy them freely
    // - a row pulled out of a table, a table merged into another - so the
    // order has to travel with the value rather than live in the parser.
    const Json::Value source = parsed(R"({"tower": {"cost": {"wood": 120, "food": 30}}})");

    const Json::Value copied = source["tower"]["cost"];
    CHECK_KEYS(copied, "wood", "food");

    Json::Value assigned;
    assigned = copied;
    CHECK_KEYS(assigned, "wood", "food");

    Json::Value moved(std::move(assigned));
    CHECK_KEYS(moved, "wood", "food");

    Json::Value moveAssigned;
    moveAssigned = std::move(moved);
    CHECK_KEYS(moveAssigned, "wood", "food");

    // Carried inside a container that is then copied, too.
    Json::Array holder;
    holder.push_back(copied);
    const Json::Value wrapped(holder);
    CHECK_KEYS(wrapped.AsArray()[0], "wood", "food");

    Json::Object table;
    table["tower"] = copied;
    CHECK_KEYS(Json::Value(table)["tower"], "wood", "food");

    CHECK_KEYS(source["tower"]["cost"], "wood", "food");
}

static void testAnythingButAnObjectHasNoKeys() {
    CHECK_KEYS(parsed("3"));
    CHECK_KEYS(parsed(R"("wood")"));
    CHECK_KEYS(parsed("true"));
    CHECK_KEYS(parsed("null"));
    CHECK_KEYS(parsed("[{\"a\": 1}]"));
    CHECK_KEYS(Json::Value());

    // A lookup that misses hands back the shared null, not an empty object.
    const Json::Value data = parsed(R"({"present": {"a": 1}})");
    CHECK_KEYS(data["absent"]);
    CHECK_KEYS(data["present"]["a"]);
}

static void runTests() {
    testParsesTheShapesTheEngineWrites();
    testDeepNestingIsRefusedRatherThanRecursedInto();
    testTrailingContentIsRejected();
    testDuplicateKeysTakeTheLastValue();
    testNonFiniteLiteralsAreNotNumbers();
    testMalformedInputFailsWithAMessage();
    testMissingKeysFallBack();
    testKeysComeBackInTheOrderTheFileWroteThem();
    testTheMapUnderneathIsStillAlphabetical();
    testObjectsInsideArraysKeepTheirOwnOrder();
    testARepeatedKeyKeepsItsFirstPlaceAndItsLastValue();
    testKeysAddedAfterTheParseFollowInMapOrder();
    testAnObjectBuiltInCodeIsInMapOrder();
    testCopiesAndMovesKeepTheOrder();
    testAnythingButAnObjectHasNoKeys();
}

TEST_MAIN("test_json", 70)
