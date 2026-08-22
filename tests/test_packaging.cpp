// Tests for "Package Standalone Game".
//
// This is the feature with the worst failure history in the repository and it
// had no test at all. Its own header records the two bugs: it once printed
// SUCCESS over an empty folder, and it wrote a hardcoded scene literal into
// every manifest, so "package the current scene" shipped whatever was last
// saved to MainScene.scene no matter which scene was actually open.
//
// Both are the same shape of bug - a packaging step that reports success it has
// not earned - and neither is visible until someone else runs the folder. CI's
// output check only proves the binaries exist.
//
// It touches no Vulkan entry point: it is a file copy, a directory walk and a
// manifest write, which is why it can be tested here at all.

#include "TestHarness.hpp"

#include "editor/GamePackager.hpp"
#include "core/GameRuntime.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace Supersonic;

namespace {

fs::path scratchRoot() {
    return fs::temp_directory_path() / "supersonic_packaging_test";
}

std::string readFile(const fs::path& p) {
    std::ifstream file(p, std::ios::binary);
    if (!file.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void cleanup() {
    std::error_code ec;
    fs::remove_all(scratchRoot(), ec);
}

} // namespace

// The bug the header names: the startup scene was a literal in the .cpp, so
// every package booted MainScene regardless of what was open.
static void testTheManifestNamesTheSceneItWasGiven() {
    cleanup();
    const fs::path out = scratchRoot() / "GameRelease";

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/Level7.scene");

    CHECK_MSG(result.ok, result.message);

    const fs::path manifestPath = out / GameRuntime::kManifestFilename;
    CHECK_MSG(fs::exists(manifestPath),
              "without a manifest the copied binary starts the editor");

    const GameManifest manifest = GameRuntime::Parse(readFile(manifestPath));
    CHECK_MSG(manifest.isGame,
              "the marker is the only thing telling the binary it is a game");
    CHECK_MSG(manifest.startupScene == "assets/scenes/Level7.scene",
              "the packaged scene must be the one asked for, not a literal");
    CHECK_MSG(manifest.title == "GameRelease",
              "the title comes from the output folder");

    cleanup();
}

// The other bug: SUCCESS printed over a folder with no executable in it.
static void testTheBinaryIsActuallyThere() {
    cleanup();
    const fs::path out = scratchRoot() / "WithBinary";

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");
    CHECK_MSG(result.ok, result.message);

    // The running executable is this test binary, which is the point: the
    // packager locates whatever is running rather than guessing a build layout.
    bool foundExecutable = false;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(out, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() == GameRuntime::kManifestFilename) continue;
        const std::string ext = entry.path().extension().string();
        if (ext == ".dll" || ext == ".so" || ext == ".dylib") continue;
        foundExecutable = true;
    }
    CHECK_MSG(foundExecutable, "a package with no binary in it is not a package");

    cleanup();
}

// A packaged folder must be readable by the loader that will open it. These two
// have disagreed before, which is why kManifestFilename is shared rather than
// spelled out in each.
static void testTheManifestRoundTripsThroughTheLoader() {
    cleanup();
    const fs::path out = scratchRoot() / "RoundTrip";

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/Boss.scene");
    CHECK_MSG(result.ok, result.message);

    const GameManifest parsed =
        GameRuntime::Parse(readFile(out / GameRuntime::kManifestFilename));
    const GameManifest reparsed = GameRuntime::Parse(GameRuntime::Serialize(parsed));

    CHECK(reparsed.isGame == parsed.isGame);
    CHECK(reparsed.startupScene == parsed.startupScene);
    CHECK(reparsed.title == parsed.title);

    cleanup();
}

// Packaging twice over the same folder must overwrite rather than fail, because
// that is what pressing the button twice does.
static void testPackagingIsRepeatable() {
    cleanup();
    const fs::path out = scratchRoot() / "Twice";

    const auto first = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/One.scene");
    CHECK_MSG(first.ok, first.message);

    const auto second = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/Two.scene");
    CHECK_MSG(second.ok, second.message);

    const GameManifest manifest =
        GameRuntime::Parse(readFile(out / GameRuntime::kManifestFilename));
    CHECK_MSG(manifest.startupScene == "assets/scenes/Two.scene",
              "the second package must replace the first, not be ignored by it");

    cleanup();
}

// A path that cannot be created has to be reported as a failure. Reporting
// success here is the exact class of bug this whole suite exists for.
static void testAnImpossibleDestinationFails() {
    cleanup();

    // An existing FILE where the output directory should go: create_directories
    // cannot produce a directory at that path.
    const fs::path root = scratchRoot();
    std::error_code ec;
    fs::create_directories(root, ec);

    const fs::path blocker = root / "blocked";
    { std::ofstream file(blocker); file << "not a directory"; }

    const auto result = GamePackager::PackageStandaloneGame(
        (blocker / "inside").string(), "assets/scenes/MainScene.scene");

    CHECK_MSG(!result.ok, "packaging into a path that cannot exist must not report success");
    CHECK_MSG(!result.message.empty(), "and must say what went wrong");

    cleanup();
}

static void runTests() {
    testTheManifestNamesTheSceneItWasGiven();
    testTheBinaryIsActuallyThere();
    testTheManifestRoundTripsThroughTheLoader();
    testPackagingIsRepeatable();
    testAnImpossibleDestinationFails();
}

TEST_MAIN("test_packaging", 16)
