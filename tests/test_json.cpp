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

using namespace Supersonic;

namespace {

bool parses(const std::string& text) {
    Json::Value out;
    std::string error;
    return Json::Parse(text, out, error);
}

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

static void runTests() {
    testParsesTheShapesTheEngineWrites();
    testDeepNestingIsRefusedRatherThanRecursedInto();
    testTrailingContentIsRejected();
    testDuplicateKeysTakeTheLastValue();
    testNonFiniteLiteralsAreNotNumbers();
    testMalformedInputFailsWithAMessage();
    testMissingKeysFallBack();
}

TEST_MAIN("test_json", 30)
