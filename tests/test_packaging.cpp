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
#include "platform/ExecutablePath.hpp"

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

// A runtime library that fails to copy used to call ec.clear() and carry on, so
// a game missing the one plugin it cannot run without was announced as packaged
// successfully. It is a partial package now, and it says which file.
//
// Deterministic rather than dependent on what happens to sit beside this
// binary: it plants a probe library next to the executable so there is
// certainly one to copy, then puts a DIRECTORY at the destination name, which
// copy_file cannot overwrite. An earlier draft searched for an existing .dll
// and returned early when it found none - which is exactly the soft pass the
// TestHarness check floor exists to catch, and it passed vacuously here.
static void testAFailedRuntimeLibraryCopyIsNotASuccess() {
    cleanup();
    const fs::path out = scratchRoot() / "Partial";
    std::error_code ec;
    fs::create_directories(out, ec);

    const fs::path exeDir = fs::path(ExecutablePath()).parent_path();
    const fs::path probe = exeDir / "supersonic_packaging_probe.dll";
    { std::ofstream file(probe, std::ios::binary); file << "probe"; }

    // The destination name is taken by something copy_file cannot write over.
    fs::create_directories(out / probe.filename(), ec);

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");

    CHECK_MSG(!result.ok, "a library that did not copy must not report a clean package");
    CHECK_MSG(result.message.find(probe.filename().string()) != std::string::npos,
              "and must name the file that failed: " + result.message);

    fs::remove(probe, ec);
    cleanup();
}

// The same probe, unobstructed, must land in the package - otherwise the test
// above would pass just as well against a packager that copied nothing at all.
static void testRuntimeLibrariesAreCopied() {
    cleanup();
    const fs::path out = scratchRoot() / "WithLibs";
    std::error_code ec;

    const fs::path exeDir = fs::path(ExecutablePath()).parent_path();
    const fs::path probe = exeDir / "supersonic_packaging_probe.dll";
    { std::ofstream file(probe, std::ios::binary); file << "probe"; }

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");

    CHECK_MSG(result.ok, result.message);
    CHECK_MSG(fs::exists(out / probe.filename()),
              "a runtime library beside the binary belongs in the package");

    fs::remove(probe, ec);
    cleanup();
}

// Hot reload loads numbered copies of the script plugin, so a build tree fills
// up with GameScripts.loaded1.dll and friends. Shipping those puts a stale copy
// of the game's scripts in the release folder.
static void testScratchHotReloadCopiesAreNotShipped() {
    cleanup();
    const fs::path out = scratchRoot() / "NoScratch";
    std::error_code ec;

    const fs::path exeDir = fs::path(ExecutablePath()).parent_path();
    const fs::path scratch = exeDir / "supersonic_probe.loaded7.dll";
    { std::ofstream file(scratch, std::ios::binary); file << "stale"; }

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");

    CHECK_MSG(result.ok, result.message);
    CHECK_MSG(!fs::exists(out / scratch.filename()),
              "a hot-reload scratch copy must not be shipped");

    fs::remove(scratch, ec);
    cleanup();
}

// Textures extracted out of a .glb live in cache/, not assets/, because they
// are generated. But a scene that uses one serialises a cache/gltf/... path
// into its material, and the packaged game looks for exactly that path beside
// its own executable.
//
// It could regenerate them - the .glb ships and the mesh resolves first - but
// only where the install directory is writable, which under Program Files it is
// not. The failure there is permanent: TextureRegistry caches a failed load as
// a checkerboard.
static void testExtractedGlbTexturesAreShipped() {
    cleanup();
    const fs::path out = scratchRoot() / "WithCache";
    std::error_code ec;

    // The packager copies whatever is in cache/gltf relative to the working
    // directory, which for a test run is the project root.
    const fs::path cacheDir = fs::path("cache") / "gltf";
    fs::create_directories(cacheDir, ec);
    const fs::path probe = cacheDir / "supersonic_packaging_probe-image0.png";
    { std::ofstream file(probe, std::ios::binary); file << "png-ish"; }

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");
    CHECK_MSG(result.ok, result.message);

    CHECK_MSG(fs::exists(out / "cache" / "gltf" / probe.filename()),
              "a texture extracted from a .glb has to travel with the game that "
              "serialised its path");

    fs::remove(probe, ec);
    cleanup();
}

// A packaged game that ships assets WITHOUT their sidecars resolves every
// reference by path, which is the behaviour asset identity exists to replace -
// and it would do it silently, in somebody else's build, which is this feature's
// signature failure. Worse, running --import-assets in that tree would mint
// fresh identities that disagree with the project's.
//
// Nothing filters them out today: the assets tree is copied wholesale, and the
// extension list further up this file gates the RUNTIME LIBRARIES next to the
// executable, not this. That is exactly the sort of thing that stops being true
// without anyone noticing, so it is asserted rather than reasoned about.
static void testAPackagedGameShipsAssetIdentities() {
    cleanup();
    const fs::path out = scratchRoot() / "GameRelease";

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");
    CHECK_MSG(result.ok, result.message);

    size_t sidecars = 0;
    std::error_code ec;
    const fs::path textures = out / "assets" / "textures";
    if (fs::is_directory(textures, ec)) {
        for (const auto& entry : fs::directory_iterator(textures, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".meta") ++sidecars;
        }
    }

    // The project has three textures, each with an identity. If the source tree
    // has not been imported there is nothing to ship and nothing to assert, so
    // the check is on the RATIO rather than a bare count.
    size_t sourceSidecars = 0;
    if (fs::is_directory("assets/textures", ec)) {
        for (const auto& entry : fs::directory_iterator("assets/textures", ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".meta") ++sourceSidecars;
        }
    }

    CHECK_MSG(sidecars == sourceSidecars,
              "every .meta in the source tree has to reach the packaged one, or a "
              "shipped build resolves nothing by identity");
    CHECK_MSG(sourceSidecars > 0,
              "and the project itself must have identities, or this proves nothing");

    cleanup();
}

// The packaged folder has to be one the RUNTIME will recognise.
//
// A game anchors its working directory to its own folder so it survives being
// launched from a shortcut, and it does that only when the assets are actually
// beside it. That test and this packager are two descriptions of one layout,
// written in different files, and nothing held them together: drop "shaders"
// from the packager's copy list and every packaged game keeps packaging
// successfully, keeps passing every other test in this suite, and dies on
// startup on a missing .spv.
//
// So rather than restating the list here - which is a copy, and copies drift -
// this packages for real and asks the runtime's own predicate about the result.
static void testAPackagedFolderIsOneTheRuntimeWillAnchorTo() {
    cleanup();
    const fs::path out = scratchRoot() / "Anchorable";

    const auto result = GamePackager::PackageStandaloneGame(
        out.string(), "assets/scenes/MainScene.scene");
    CHECK_MSG(result.ok, result.message);

    CHECK_MSG(LooksLikeAPackagedFolder(out),
              "the packager wrote a folder the runtime does not recognise as a "
              "packaged game; it will refuse to anchor and die on a missing shader");

    // And the negative that gives the positive its meaning. An empty directory
    // called `assets` is the case that actually shipped broken: it appears
    // beside a build output for ordinary reasons, so a check for the NAME
    // alone anchored the editor into its own build folder.
    const fs::path decoy = scratchRoot() / "Decoy";
    std::error_code ec;
    fs::create_directories(decoy / "assets", ec);
    CHECK_MSG(!LooksLikeAPackagedFolder(decoy),
              "a bare `assets` directory is not a packaged game, and treating it "
              "as one is what broke running a game out of its build tree");

    CHECK_MSG(!LooksLikeAPackagedFolder(scratchRoot() / "NoSuchFolder"),
              "a folder that is not there cannot be anchored to");
    CHECK_MSG(!LooksLikeAPackagedFolder({}),
              "an unknown executable directory must not anchor to the filesystem root");

    cleanup();
}

static void runTests() {
    testAPackagedFolderIsOneTheRuntimeWillAnchorTo();
    testTheManifestNamesTheSceneItWasGiven();
    testAPackagedGameShipsAssetIdentities();
    testTheBinaryIsActuallyThere();
    testTheManifestRoundTripsThroughTheLoader();
    testPackagingIsRepeatable();
    testAnImpossibleDestinationFails();
    testRuntimeLibrariesAreCopied();
    testAFailedRuntimeLibraryCopyIsNotASuccess();
    testScratchHotReloadCopiesAreNotShipped();
    testExtractedGlbTexturesAreShipped();
}

TEST_MAIN("test_packaging", 32)
