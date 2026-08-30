#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Supersonic {

// Stable identities for the files a scene refers to.
//
// The gap this closes was the first item on the README's list: every reference
// is a raw relative path, so renaming a file in Explorer silently breaks every
// scene, prefab and material pointing at it. Silently is the whole problem -
// the texture is simply gone, the fallback checkerboard appears, and nothing
// says which of the forty references was the one that used to work.
//
// The shape of the fix is deliberately narrow. Components still hold PATHS and
// every registry still takes a path, so nothing downstream of this file changes.
// What changes is that a saved reference carries an identity ALONGSIDE its path,
// and on load the identity decides where to look. Plumbing an id through the
// texture, mesh, audio and material registries would be a much larger change
// and would buy nothing that this does not.
//
// The identity lives in a `.meta` file next to the asset - `floor_tiles.png`
// gets `floor_tiles.png.meta` - which is the one place it can live and still be
// copied by a packager that copies directory trees, moved by a person who moves
// the asset, and diffed by git.
//
// Vulkan-free and registry-free: this is file paths, hashes and a map.
class AssetDatabase {
public:
    // 32 hex characters. A string rather than a pair of integers because it is
    // written to JSON, compared, and read by people.
    // How a texture should be sampled, decided when it is IMPORTED rather than
    // where it is used.
    //
    // It lives beside the asset because it is a property of the image, not of a
    // material: two materials naming one file must not disagree about it. They
    // could not even try - TextureRegistry caches by path, so the first
    // material to ask would win and the second would silently get the other
    // one's answer.
    //
    // Every texture in this engine was linearly filtered with no way to say
    // otherwise, which makes pixel art blurred and unfixable. That is the one
    // thing the sprite work found that a quad, a UV transform and an unlit
    // material genuinely cannot express.
    enum class TextureFilter { Linear, Nearest };

    struct Entry {
        std::string guid;
        std::string path;          // relative, forward slashes
        uint64_t contentHash{0};

        // Absent from the file means Linear, which is what every .meta already
        // written says by saying nothing. Nothing has to migrate.
        TextureFilter filter{TextureFilter::Linear};
    };

    // The filter a texture asks for, read from the .meta beside it.
    //
    // Static and file-backed rather than a lookup into the scanned database:
    // a texture is acquired once and cached, so one small read is cheap, and it
    // works in a packaged game whose database was never scanned. Linear for a
    // file with no .meta, which is every texture in the tree today.
    static TextureFilter FilterForAsset(const std::string& assetPath);

    // The two spellings that appear in a .meta, and their round trip. Exposed
    // so the writer and the reader cannot disagree about them.
    static const char* NameOfFilter(TextureFilter filter);
    static TextureFilter FilterFromName(const std::string& name);

    // ---- Reading what is on disk -------------------------------------------

    struct ScanResult {
        size_t identified{0};      // assets with a readable .meta
        size_t unidentified{0};    // assets with none
        size_t orphaned{0};        // a .meta whose asset is gone
        bool ok{false};
    };

    // Reads every `.meta` under `root` and builds the maps.
    //
    // Creates NOTHING. That is not a detail: a scan runs from load paths and
    // from tests, and a scan that minted identities as a side effect would have
    // the test suite writing sidecars into the project's own assets folder the
    // first time anybody ran it.
    ScanResult Scan(const std::string& root);

    // ---- Writing identities ------------------------------------------------

    // Where one identity moved to. Recorded DURING adoption because it cannot
    // be recovered afterwards: Import clears the path index before adopting, so
    // by the time the new path is known the old one is gone, and no diff of the
    // result can reconstruct it. Without the pair, a live scene cannot be told
    // which of its references to rewrite.
    struct Move {
        std::string guid;
        std::string from;
        std::string to;
    };

    struct ImportResult {
        size_t minted{0};          // new identities written
        size_t adopted{0};         // identities recovered from an orphaned .meta
        size_t refreshed{0};       // content hash brought up to date
        bool ok{false};

        // One per adoption, in the order they were adopted.
        std::vector<Move> moved;
    };

    // Mints identities for anything that has none and WRITES the sidecars.
    //
    // Only an explicit editor action or a first-run step calls this. The order
    // inside matters and is the whole feature: orphaned metas are matched
    // against un-identified files by CONTENT before anything is minted. Mint
    // first and every renamed file gets a brand new identity, adoption finds
    // nothing left to adopt, and the headline case - somebody renamed a texture
    // in Explorer and left the sidecar behind - silently does nothing.
    ImportResult Import(const std::string& root);

    // ---- Resolution --------------------------------------------------------

    // Where a saved reference actually points now.
    //
    // The identity wins when it resolves. When it does not, the written path is
    // used - which is what makes every scene written before this existed keep
    // working - and that case is COUNTED and logged, because a fallback that
    // looks like success is worse than no feature at all: it is the old broken
    // behaviour wearing the new one's clothes.
    std::string Resolve(const std::string& guid, const std::string& path);

    // What a reference should be saved with. Empty when the path has no
    // identity, which is not an error - it is an asset nobody has imported.
    std::string GuidForPath(const std::string& path) const;

    // Null when nothing carries that identity.
    const std::string* PathForGuid(const std::string& guid) const;

    // How the last run of resolutions went. Exposed so a test can assert which
    // ROUTE was taken rather than only that the answer came out right: a test
    // that renames a file and checks the path is correct also passes when the
    // fallback happened to be correct, which proves nothing at all.
    struct Stats {
        size_t byGuid{0};
        size_t byPath{0};
        size_t unresolved{0};
    };
    const Stats& stats() const { return m_stats; }
    void ResetStats() { m_stats = Stats{}; m_firstUnresolved.clear(); }

    // Says once what Resolve deliberately did not say twenty times.
    //
    // Called at the END of a scene load, with ResetStats at the start, so the
    // count is per load and is still there to be read afterwards. Reporting and
    // clearing in one step would make the counter unreadable by the caller -
    // and by a test, which is the only thing that can prove the count is right.
    void ReportUnresolved();

    size_t size() const { return m_byGuid.size(); }
    void Clear();

    // The process-wide database.
    //
    // A singleton, unlike anything else here, and the difference is real: a
    // heightfield cache belongs to a scene and would hand the next scene
    // somebody else's hills, but this maps FILES ON DISK, which outlive every
    // scene and belong to none of them. ComponentCodec::Read is a free function
    // with a fixed signature and no way to be handed one.
    static AssetDatabase& Instance();

    // ---- Pieces, exposed because they are worth testing on their own -------

    // FNV-1a over the file's bytes. Zero when the file cannot be read, which is
    // never a valid hash for a file that exists, so it doubles as the failure.
    static uint64_t HashFile(const std::string& path);

    // Deterministic: the same path and contents always mint the same identity.
    //
    // Both halves matter. Content alone would give two copies of one texture the
    // same identity, and they are two assets. Path alone would give a file the
    // identity of whatever used to be at that name.
    static std::string MintGuid(const std::string& path, uint64_t contentHash);

    // Whether this is a file a scene or a material can name. Shaders are build
    // inputs and models' `.bin` buddies are named by the `.gltf` beside them,
    // so neither needs an identity of its own.
    static bool IsAssetPath(const std::string& path);

    // `dir\\file.png` becomes `dir/file.png`. One spelling, so a reference
    // written on Windows resolves on a machine that is not.
    static std::string NormalisePath(const std::string& path);

    static std::string MetaPathFor(const std::string& assetPath) {
        return assetPath + ".meta";
    }

private:
    // Everything under `root` that IsAssetPath accepts, sorted, so an ambiguous
    // adoption resolves the same way twice.
    static std::vector<std::string> CollectAssets(const std::string& root);

    static bool ReadMeta(const std::string& metaPath, Entry& out);
    static bool WriteMeta(const std::string& metaPath, const Entry& entry);

    void Remember(const Entry& entry);

    std::map<std::string, Entry> m_byGuid;
    std::map<std::string, std::string> m_guidByPath;
    Stats m_stats;

    // The first identity that resolved to nothing since the last report, so the
    // one line printed can name something concrete.
    std::string m_firstUnresolved;
};

} // namespace Supersonic
