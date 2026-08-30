// Asset identity.
//
// The first item on the README's list, and the one whose failure is silent:
// renaming a file in Explorer breaks every scene, prefab and material pointing
// at it, and all you see is a fallback checkerboard with nothing to say which
// of forty references used to work.
//
// Two of the checks below are worth more than the rest. testARenamedAssetIsFound
// AgainByItsContents is the headline case. And every resolution test asserts on
// the ROUTE the answer came by, not just the answer: a test that renames a file
// and checks the path is right also passes when the fallback happened to be
// right, which proves nothing at all.

#include "core/AssetDatabase.hpp"
#include "core/AssetRepointer.hpp"
#include "core/Components.hpp"
#include "core/MaterialLibrary.hpp"
#include "TestHarness.hpp"

#include <entt/entt.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace Supersonic;

namespace {

fs::path scratchRoot() {
    return fs::temp_directory_path() / "supersonic_assetdb_test";
}

// A fresh, empty tree per case, so nothing carries over from a case that failed
// half way through.
fs::path freshRoot() {
    const fs::path root = scratchRoot();
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "textures", ec);
    return root;
}

std::string write(const fs::path& path, const std::string& contents) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
    file.close();
    return AssetDatabase::NormalisePath(path.generic_string());
}

bool exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::string readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// --- the pieces -----------------------------------------------------------

void testWhatCountsAsAnAsset() {
    CHECK(AssetDatabase::IsAssetPath("assets/textures/floor.png"));
    CHECK(AssetDatabase::IsAssetPath("assets/models/thing.glb"));
    CHECK(AssetDatabase::IsAssetPath("assets/audio/hum.wav"));
    CHECK(AssetDatabase::IsAssetPath("assets/materials/Tiles.material"));

    // Case is not part of the answer: the same file is the same asset whether
    // it was saved as .PNG or .png.
    CHECK(AssetDatabase::IsAssetPath("assets/textures/FLOOR.PNG"));

    // Shaders are compiled by the build and named in code; a glTF's .bin is
    // named by the .gltf beside it. Neither is ever written into a scene, so
    // neither needs an identity.
    CHECK(!AssetDatabase::IsAssetPath("assets/shaders/shader.frag"));
    CHECK(!AssetDatabase::IsAssetPath("assets/models/thing.bin"));
    CHECK(!AssetDatabase::IsAssetPath("assets/scenes/MainScene.scene"));
    CHECK(!AssetDatabase::IsAssetPath("readme"));
    CHECK(!AssetDatabase::IsAssetPath("assets/textures/floor.png.meta"));
}

void testPathsHaveOneSpelling() {
    // A reference written on Windows has to resolve on a machine that is not,
    // and two strings for one place resolve to nothing.
    CHECK(AssetDatabase::NormalisePath("assets\\textures\\floor.png") ==
          "assets/textures/floor.png");
    CHECK(AssetDatabase::NormalisePath("./assets/x.png") == "assets/x.png");
    CHECK(AssetDatabase::NormalisePath("assets/x.png") == "assets/x.png");
}

void testMintingIsDeterministicAndDependsOnBoth() {
    const std::string a = AssetDatabase::MintGuid("assets/textures/floor.png", 0xabcdef01u);
    const std::string b = AssetDatabase::MintGuid("assets/textures/floor.png", 0xabcdef01u);
    CHECK_MSG(a == b, "the same file must mint the same identity every time");
    CHECK_EQ(a.size(), size_t{32});

    // Content alone would give two copies of one texture the same identity, and
    // they are two assets.
    CHECK_MSG(a != AssetDatabase::MintGuid("assets/textures/wall.png", 0xabcdef01u),
              "two files with identical contents are still two assets");

    // Path alone would hand a new file whatever used to be at that name.
    CHECK_MSG(a != AssetDatabase::MintGuid("assets/textures/floor.png", 0x99u),
              "and a different file at the same name is a different asset");
}

void testHashingAFileThatIsNotThere() {
    const fs::path root = freshRoot();
    CHECK_EQ(AssetDatabase::HashFile((root / "nope.png").generic_string()), uint64_t{0});

    const std::string path = write(root / "textures" / "a.png", "some bytes");
    const uint64_t hash = AssetDatabase::HashFile(path);
    CHECK_MSG(hash != 0, "a file that exists never hashes to the failure value");
    CHECK_MSG(hash == AssetDatabase::HashFile(path), "and hashes the same twice");

    const std::string other = write(root / "textures" / "b.png", "some other bytes");
    CHECK(AssetDatabase::HashFile(other) != hash);
}

// --- scanning creates nothing ---------------------------------------------

void testScanningNeverWritesAnything() {
    // Not a detail. A scan runs from load paths and from tests, and one that
    // minted as a side effect would have the suite writing sidecars into the
    // project's own assets folder the first time anybody ran it.
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures" / "floor.png", "pixels");

    AssetDatabase database;
    const auto scanned = database.Scan(root.generic_string());

    CHECK(scanned.ok);
    CHECK_EQ(scanned.identified, size_t{0});
    CHECK_EQ(scanned.unidentified, size_t{1});
    CHECK_MSG(!exists(asset + ".meta"), "a scan must not mint");
    CHECK_EQ(database.size(), size_t{0});

    // And a reference to it still works, by path, which is every scene written
    // before any of this existed.
    database.ResetStats();
    CHECK_MSG(database.Resolve("", asset) == asset, "a reference with no identity uses its path");
    CHECK_EQ(database.stats().byPath, size_t{1});
    CHECK_EQ(database.stats().byGuid, size_t{0});
}

void testScanningAMissingFolderIsNotACrash() {
    AssetDatabase database;
    const auto scanned = database.Scan((scratchRoot() / "no_such_folder").generic_string());
    CHECK(!scanned.ok);
    CHECK_EQ(database.size(), size_t{0});
}

// --- importing ------------------------------------------------------------

void testImportMintsOnceAndThenLeavesThingsAlone() {
    const fs::path root = freshRoot();
    const std::string first = write(root / "textures" / "floor.png", "pixels");
    const std::string second = write(root / "textures" / "wall.png", "other pixels");

    AssetDatabase database;
    const auto imported = database.Import(root.generic_string());
    CHECK(imported.ok);
    CHECK_EQ(imported.minted, size_t{2});
    CHECK_EQ(imported.adopted, size_t{0});
    CHECK(exists(first + ".meta"));
    CHECK(exists(second + ".meta"));

    const std::string guid = database.GuidForPath(first);
    CHECK_EQ(guid.size(), size_t{32});

    // Running it again mints nothing: the identities are already on disk, and
    // an import that re-minted would break every reference each time it ran.
    const auto again = database.Import(root.generic_string());
    CHECK_EQ(again.minted, size_t{0});
    CHECK_MSG(database.GuidForPath(first) == guid, "and the identity is the one it already had");

    // A fresh database picks the same identities up off disk.
    AssetDatabase reopened;
    const auto scanned = reopened.Scan(root.generic_string());
    CHECK_EQ(scanned.identified, size_t{2});
    CHECK_EQ(scanned.unidentified, size_t{0});
    CHECK_MSG(reopened.GuidForPath(first) == guid, "an identity survives being read back");
}

void testEditingAFileKeepsItsIdentityAndRefreshesItsHash() {
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures" / "floor.png", "pixels");

    AssetDatabase database;
    database.Import(root.generic_string());
    const std::string guid = database.GuidForPath(asset);

    write(root / "textures" / "floor.png", "repainted pixels");
    const auto again = database.Import(root.generic_string());

    CHECK_EQ(again.minted, size_t{0});
    CHECK_EQ(again.refreshed, size_t{1});
    CHECK_MSG(database.GuidForPath(asset) == guid,
              "an edited asset is the same asset; every reference to it must survive");

    // The stored hash has to keep up, because it is what a later adoption
    // matches on.
    const std::string meta = readAll(asset + ".meta");
    CHECK_MSG(meta.find(guid) != std::string::npos, "the sidecar still names it");
}

// --- THE case ---------------------------------------------------------------

void testARenamedAssetIsFoundAgainByItsContents() {
    // Exactly what the README describes: somebody renamed a file in Explorer.
    // The sidecar stays behind under the old name, because Explorer has never
    // heard of it, and the asset arrives with no identity at all.
    const fs::path root = freshRoot();
    const std::string before = write(root / "textures" / "floor_tiles.png", "the same pixels");

    AssetDatabase database;
    database.Import(root.generic_string());
    const std::string guid = database.GuidForPath(before);
    CHECK_EQ(guid.size(), size_t{32});

    // The rename: the asset moves, the sidecar does not.
    std::error_code ec;
    fs::rename(root / "textures" / "floor_tiles.png", root / "textures" / "stone_tiles.png", ec);
    CHECK_MSG(!ec, "the fixture has to actually rename the file");
    const std::string after =
        AssetDatabase::NormalisePath((root / "textures" / "stone_tiles.png").generic_string());

    // A scan alone can see what happened and refuses to guess.
    AssetDatabase looked;
    const auto scanned = looked.Scan(root.generic_string());
    CHECK_EQ(scanned.unidentified, size_t{1});
    CHECK_EQ(scanned.orphaned, size_t{1});

    // The import is what carries the identity across, by CONTENT.
    AssetDatabase reimported;
    const auto imported = reimported.Import(root.generic_string());
    CHECK_EQ(imported.adopted, size_t{1});
    CHECK_MSG(imported.minted == 0,
              "a renamed file must be adopted, not given a brand new identity");

    CHECK_MSG(reimported.GuidForPath(after) == guid,
              "the renamed file carries the identity the old one had");
    CHECK(exists(after + ".meta"));
    CHECK_MSG(!exists(before + ".meta"),
              "and the sidecar left behind is gone, or the next import adopts from it twice");

    // And now the thing the whole feature is for: a reference saved before the
    // rename still finds the file, BY IDENTITY.
    reimported.ResetStats();
    CHECK_MSG(reimported.Resolve(guid, before) == after,
              "a reference saved before the rename finds the file at its new name");
    CHECK_MSG(reimported.stats().byGuid == 1,
              "and it got there by identity - a fallback that happened to be right "
              "would prove nothing");
    CHECK_EQ(reimported.stats().byPath, size_t{0});
    CHECK_EQ(reimported.stats().unresolved, size_t{0});
}

void testAnOpenSceneFollowsARename() {
    // The half that was missing. Identity has survived a rename since asset
    // identity landed - but only through the FILE. A loaded component holds a
    // path and nothing else; the guid that would resolve it was spent when the
    // scene was read. So the editor that was open while somebody renamed a
    // texture in Explorer went on naming a file that is not there, showing a
    // checkerboard, until the scene was saved and read back.
    const fs::path root = freshRoot();
    const std::string before = write(root / "textures" / "floor_tiles.png", "the same pixels");

    // The shared asset, created before the import so it gets an identity too.
    // Its copy of the texture path is the one MaterialSystem::Sync writes onto
    // every component using it, every frame.
    MaterialLibrary materials;
    MaterialAsset shared;
    shared.albedoTexturePath = before;
    const uint32_t id = materials.Create((root / "shared.material").generic_string(), shared);
    CHECK_MSG(id != MaterialLibrary::kInvalidMaterial, "the shared material must be created");

    AssetDatabase database;
    database.Import(root.generic_string());
    const std::string guid = database.GuidForPath(before);
    CHECK_EQ(guid.size(), size_t{32});

    // The scene, open, holding the path the way a component does.
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<MaterialComponent>(entity).albedoTexturePath = before;

    std::error_code ec;
    fs::rename(root / "textures" / "floor_tiles.png", root / "textures" / "stone_tiles.png", ec);
    CHECK_MSG(!ec, "the fixture has to actually rename the file");
    const std::string after =
        AssetDatabase::NormalisePath((root / "textures" / "stone_tiles.png").generic_string());

    AssetDatabase reimported;
    const auto imported = reimported.Import(root.generic_string());
    CHECK_EQ(imported.adopted, size_t{1});

    // The pair is the thing. Import clears its path index BEFORE adopting, so
    // the old path is gone by the time the new one is known - recording the
    // move during adoption is the only moment both halves exist at once, and
    // without them there is no way to know which references to rewrite.
    CHECK_EQ(imported.moved.size(), size_t{1});
    CHECK_MSG(!imported.moved.empty() && imported.moved[0].from == before,
              "the import must say where the identity came from");
    CHECK_MSG(!imported.moved.empty() && imported.moved[0].to == after,
              "and where it went");

    const RepointResult repointed = RepointAssets(registry, &materials, imported.moved);

    CHECK_MSG(registry.get<MaterialComponent>(entity).albedoTexturePath == after,
              "the open scene must name the file where it is now");
    CHECK_MSG(materials.Get(id) && materials.Get(id)->albedoTexturePath == after,
              "and so must the cached asset, or Sync copies the stale path back next frame");
    CHECK_EQ(repointed.componentFields, size_t{1});
    CHECK_EQ(repointed.materialFields, size_t{1});

    // And it is the SAME identity throughout, which is what makes this a
    // re-point rather than a fresh reference that happens to work.
    CHECK_MSG(reimported.GuidForPath(after) == guid,
              "adoption must carry the identity across, not mint a new one");
}

void testRepointingMatchesTheWholePathNotJustTheName() {
    // A rewrite that matches too eagerly is worse than one that does nothing:
    // it silently points an object at somebody else's texture, and the scene
    // still renders, so nothing ever says which object went wrong.
    //
    // The tempting shortcut is to compare file NAMES, because that is what a
    // rename changes. Projects are full of a floor.png per folder.
    entt::registry registry;

    const auto renamed = registry.create();
    registry.emplace<MaterialComponent>(renamed).albedoTexturePath = "assets/textures/floor.png";

    const auto sameName = registry.create();
    registry.emplace<MaterialComponent>(sameName).albedoTexturePath = "assets/props/floor.png";

    std::vector<AssetDatabase::Move> moves;
    moves.push_back(AssetDatabase::Move{"abc", "assets/textures/floor.png",
                                        "assets/textures/stone.png"});

    const RepointResult repointed = RepointAssets(registry, nullptr, moves);

    CHECK_EQ(repointed.componentFields, size_t{1});
    CHECK_MSG(registry.get<MaterialComponent>(renamed).albedoTexturePath ==
                  "assets/textures/stone.png",
              "the file that moved must follow");
    CHECK_MSG(registry.get<MaterialComponent>(sameName).albedoTexturePath ==
                  "assets/props/floor.png",
              "a different file with the same name must be left alone");
}

void testAdoptionHappensBeforeMinting() {
    // The ordering IS the feature. Mint first and the renamed file has a brand
    // new identity by the time adoption looks, so there is nothing left to
    // adopt - and the headline case silently does nothing while every counter
    // still reads like success.
    //
    // Two files change at once here, so a mint-first implementation has plenty
    // of chances to get in the way.
    const fs::path root = freshRoot();
    const std::string oldA = write(root / "textures" / "a.png", "contents of A");
    const std::string oldB = write(root / "textures" / "b.png", "contents of B");

    AssetDatabase database;
    database.Import(root.generic_string());
    const std::string guidA = database.GuidForPath(oldA);
    const std::string guidB = database.GuidForPath(oldB);

    std::error_code ec;
    fs::rename(root / "textures" / "a.png", root / "textures" / "alpha.png", ec);
    fs::rename(root / "textures" / "b.png", root / "textures" / "beta.png", ec);

    AssetDatabase reimported;
    const auto imported = reimported.Import(root.generic_string());
    CHECK_EQ(imported.adopted, size_t{2});
    CHECK_EQ(imported.minted, size_t{0});

    const std::string newA =
        AssetDatabase::NormalisePath((root / "textures" / "alpha.png").generic_string());
    const std::string newB =
        AssetDatabase::NormalisePath((root / "textures" / "beta.png").generic_string());

    CHECK_MSG(reimported.GuidForPath(newA) == guidA, "each identity follows its own contents");
    CHECK_MSG(reimported.GuidForPath(newB) == guidB, "and does not cross over to the other");
}

void testARenamedAndEditedAssetIsALimitationNotABug() {
    // Stated rather than solved. The stored hash is from the last import, so a
    // file that was renamed AND edited before the next one matches nothing.
    // Recovering it needs a similarity measure rather than an equality, which
    // is a different feature with a different failure mode.
    const fs::path root = freshRoot();
    write(root / "textures" / "a.png", "original");

    AssetDatabase database;
    database.Import(root.generic_string());

    std::error_code ec;
    fs::rename(root / "textures" / "a.png", root / "textures" / "b.png", ec);
    write(root / "textures" / "b.png", "edited as well");

    AssetDatabase reimported;
    const auto imported = reimported.Import(root.generic_string());
    CHECK_MSG(imported.adopted == 0 && imported.minted == 1,
              "renamed AND edited is a new asset, and this records that it is");
}

// --- resolution -------------------------------------------------------------

void testResolutionSaysWhichRouteItTook() {
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures" / "floor.png", "pixels");

    AssetDatabase database;
    database.Import(root.generic_string());
    const std::string guid = database.GuidForPath(asset);

    database.ResetStats();
    CHECK_MSG(database.Resolve(guid, asset) == asset, "an identity that resolves wins");
    CHECK_EQ(database.stats().byGuid, size_t{1});

    // No identity at all: every scene written before this feature existed.
    CHECK(database.Resolve("", "assets/textures/whatever.png") ==
          "assets/textures/whatever.png");
    CHECK_EQ(database.stats().byPath, size_t{1});

    // An identity nothing answers to. The saved path is used, and this is
    // COUNTED - a fallback that looks like success is worse than no feature,
    // because it is the old broken behaviour wearing this one's clothes.
    const std::string missing(32, 'f');
    CHECK(database.Resolve(missing, "assets/textures/gone.png") ==
          "assets/textures/gone.png");
    CHECK_EQ(database.stats().unresolved, size_t{1});
    CHECK_MSG(database.stats().byGuid == 1, "an unresolved identity is not a hit");
}

void testAnEmptyReferenceStaysEmpty() {
    // A material with no normal map is not a broken reference.
    AssetDatabase database;
    database.ResetStats();
    CHECK(database.Resolve("", "").empty());
    CHECK_EQ(database.stats().unresolved, size_t{0});
}

void testACorruptSidecarIsIgnoredRatherThanTrusted() {
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures" / "floor.png", "pixels");

    // Not JSON at all, and JSON with an identity of the wrong length: both are
    // an asset with no usable identity, not an asset with a broken one.
    write(root / "textures" / "floor.png.meta", "this is not json {{{");

    AssetDatabase database;
    const auto scanned = database.Scan(root.generic_string());
    CHECK_EQ(scanned.identified, size_t{0});
    CHECK_EQ(scanned.unidentified, size_t{1});

    write(root / "textures" / "floor.png.meta", "{ \"Guid\": \"tooshort\", \"Hash\": \"1\" }");
    const auto again = database.Scan(root.generic_string());
    CHECK_EQ(again.identified, size_t{0});

    // An import replaces it with one that works rather than leaving the asset
    // unreachable forever.
    AssetDatabase repairing;
    const auto imported = repairing.Import(root.generic_string());
    CHECK_EQ(imported.minted, size_t{1});
    CHECK_EQ(repairing.GuidForPath(asset).size(), size_t{32});
}

void testTheHashSurvivesJsonsDoubles() {
    // A 64-bit hash written as a JSON NUMBER comes back rounded, because every
    // JSON number is a double. It looked fine, and every adoption missed.
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures" / "floor.png",
                                    std::string(4096, '\xa7'));

    AssetDatabase database;
    database.Import(root.generic_string());

    const uint64_t hash = AssetDatabase::HashFile(asset);
    CHECK_MSG(hash > (1ull << 53), "the fixture needs a hash a double cannot hold exactly");

    // Renaming and re-importing is what actually exercises the stored value.
    std::error_code ec;
    fs::rename(root / "textures" / "floor.png", root / "textures" / "moved.png", ec);

    AssetDatabase reimported;
    const auto imported = reimported.Import(root.generic_string());
    CHECK_MSG(imported.adopted == 1, "the stored hash has to come back bit for bit");
}


// --- Import settings: how a texture asks to be sampled -------------------
//
// The engine filtered every texture linearly with no way to say otherwise,
// which makes pixel art blurred and unfixable. It is the one thing the sprite
// work found that a quad, a UV transform and an unlit material genuinely
// cannot express - region, flip, pivot and pixels-per-unit all already can.
//
// It belongs to the ASSET rather than to a material because TextureRegistry
// caches by path: two materials naming one file cannot disagree about it, since
// the first to ask would silently decide for both.
//
// Driven through the public surface only - FilterForAsset and Import - because
// that is what TextureRegistry and the importer actually call.

// A .meta as a hand-edited file, which is how somebody turns this on today.
std::string writeMeta(const fs::path& path, const std::string& guid,
                      const std::string& hash, const std::string& filter) {
    std::string text = "{\n  \"Guid\": \"" + guid + "\",\n  \"Hash\": \"" + hash + "\"";
    if (!filter.empty()) text += ",\n  \"Filter\": \"" + filter + "\"";
    text += "\n}\n";
    return write(path, text);
}

static void testATextureWithNoMetaIsFilteredLinearly() {
    // Every texture in the tree today, and every generated one - which is what
    // UploadRGBA hands over, under a key that was never a path.
    const fs::path root = freshRoot();

    CHECK_MSG(AssetDatabase::FilterForAsset((root / "textures/absent.png").generic_string()) ==
                  AssetDatabase::TextureFilter::Linear,
              "a texture with no .meta is linear");
    CHECK_MSG(AssetDatabase::FilterForAsset("generated:tone") ==
                  AssetDatabase::TextureFilter::Linear,
              "and so is a name that was never a path");
}

static void testAMetaThatSaysNothingStillMeansLinear() {
    // Every .meta already committed says nothing about filtering, and must go
    // on meaning what it has always meant. Nothing migrates.
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures/plain.png", "not really a png");
    writeMeta(root / "textures/plain.png.meta",
              "320e51673e6f8a91c3d06bca0efebea9", "c3d06bca0efebea9", "");

    CHECK_MSG(AssetDatabase::FilterForAsset(asset) == AssetDatabase::TextureFilter::Linear,
              "silence is linear");
}

static void testAMetaCanAskForNearest() {
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures/pixel_art.png", "pixels");
    writeMeta(root / "textures/pixel_art.png.meta",
              "7e56bc09147250985d6a0e1624caacee", "5d6a0e1624caacee", "nearest");

    CHECK_MSG(AssetDatabase::FilterForAsset(asset) == AssetDatabase::TextureFilter::Nearest,
              "and asking for nearest is heard");
}

static void testAReimportKeepsTheFilterSomebodyChose() {
    // THE ONE THAT WOULD HAVE BITTEN. Import rewrites a .meta whenever the
    // asset's content hash moves, which is every time the artist saves the
    // file - so an import setting the importer did not carry forward would
    // survive exactly until the next edit, and come back as a blurred sprite
    // with nothing to blame.
    //
    // It works because ReadMeta fills the whole entry and Import writes that
    // entry back, rather than minting a fresh one. Asserted rather than left to
    // that happening to stay true.
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures/hero.png", "version one");
    writeMeta(root / "textures/hero.png.meta",
              "239e87cb7045b349edfbc46b8f882006", "0", "nearest");

    AssetDatabase database;
    database.Import(root.generic_string());
    const std::string guid = database.GuidForPath(asset);
    CHECK_EQ(guid.size(), size_t{32});

    // The artist saves the file again, which moves the hash and makes Import
    // rewrite the .meta.
    write(root / "textures/hero.png", "version two, a different length entirely");
    const auto again = database.Import(root.generic_string());
    CHECK_MSG(again.refreshed >= 1, "the changed asset was re-imported");

    CHECK_MSG(AssetDatabase::FilterForAsset(asset) == AssetDatabase::TextureFilter::Nearest,
              "and the filter survived the rewrite");
    CHECK_MSG(database.GuidForPath(asset) == guid,
              "along with the identity, which is the thing the rewrite is for");
}

static void testImportingAPlainTextureWritesNoFilterKey() {
    // A .meta that said "linear" would be a diff across every identity file in
    // the project the first time anyone imported it, saying the thing their
    // absence already said. Those files are committed, and a diff nobody reads
    // is a diff that hides the one line that mattered.
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures/ordinary.png", "pixels");

    AssetDatabase database;
    database.Import(root.generic_string());

    std::ifstream in((root / "textures/ordinary.png.meta"), std::ios::binary);
    CHECK_MSG(in.is_open(), "the importer minted a .meta");
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK_MSG(text.find("Filter") == std::string::npos,
              "and said nothing about filtering: " + text);
    CHECK_MSG(AssetDatabase::FilterForAsset(asset) == AssetDatabase::TextureFilter::Linear,
              "which reads back as linear");
}

static void testAFilterFromTheFutureIsNotAnError() {
    // A typo, or a setting written by a later build. A texture that refuses to
    // load because its import settings are from the future is worse than one
    // that loads looking slightly wrong - and the IDENTITY is in the same file,
    // so refusing the entry over an import setting would lose the rename
    // recovery that is the whole reason a .meta exists.
    const fs::path root = freshRoot();
    const std::string asset = write(root / "textures/future.png", "pixels");
    writeMeta(root / "textures/future.png.meta",
              "083faf927389156960b458882481ad94", "60b458882481ad94", "anisotropic16x");

    CHECK_MSG(AssetDatabase::FilterForAsset(asset) == AssetDatabase::TextureFilter::Linear,
              "an unrecognised filter falls back");

    AssetDatabase database;
    database.Scan(root.generic_string());
    CHECK_MSG(database.GuidForPath(asset) == "083faf927389156960b458882481ad94",
              "and the identity in the same file is still read");
}

static void testTheTwoSpellingsAgreeWithEachOther() {
    // The writer and the reader use one pair of functions, so they cannot come
    // to disagree about how a filter is spelled on disk.
    for (const auto filter : { AssetDatabase::TextureFilter::Linear,
                               AssetDatabase::TextureFilter::Nearest }) {
        CHECK_MSG(AssetDatabase::FilterFromName(AssetDatabase::NameOfFilter(filter)) == filter,
                  std::string("round trip through \"") +
                      AssetDatabase::NameOfFilter(filter) + "\"");
    }
}

void runTests() {
    testWhatCountsAsAnAsset();
    testPathsHaveOneSpelling();
    testMintingIsDeterministicAndDependsOnBoth();
    testHashingAFileThatIsNotThere();

    testScanningNeverWritesAnything();
    testScanningAMissingFolderIsNotACrash();

    testImportMintsOnceAndThenLeavesThingsAlone();
    testEditingAFileKeepsItsIdentityAndRefreshesItsHash();

    testARenamedAssetIsFoundAgainByItsContents();
    testAnOpenSceneFollowsARename();
    testRepointingMatchesTheWholePathNotJustTheName();
    testAdoptionHappensBeforeMinting();
    testARenamedAndEditedAssetIsALimitationNotABug();

    testResolutionSaysWhichRouteItTook();
    testAnEmptyReferenceStaysEmpty();
    testACorruptSidecarIsIgnoredRatherThanTrusted();
    testTheHashSurvivesJsonsDoubles();

    std::error_code ec;
    fs::remove_all(scratchRoot(), ec);

    testATextureWithNoMetaIsFilteredLinearly();
    testAMetaThatSaysNothingStillMeansLinear();
    testAMetaCanAskForNearest();
    testAReimportKeepsTheFilterSomebodyChose();
    testImportingAPlainTextureWritesNoFilterKey();
    testAFilterFromTheFutureIsNotAnError();
    testTheTwoSpellingsAgreeWithEachOther();
}

} // namespace

TEST_MAIN("test_assetdatabase", 110)
