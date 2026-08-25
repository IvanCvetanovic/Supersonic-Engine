#include "core/AssetDatabase.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/Json.hpp"
#include "core/Log.hpp"

namespace fs = std::filesystem;

namespace Supersonic {

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t fnv1a(const void* data, size_t bytes, uint64_t seed = kFnvOffset) {
    const auto* cursor = static_cast<const unsigned char*>(data);
    uint64_t hash = seed;
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= cursor[i];
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t fnv1a(const std::string& text, uint64_t seed = kFnvOffset) {
    return fnv1a(text.data(), text.size(), seed);
}

std::string toHex(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer, 16);
}

} // namespace

std::string AssetDatabase::NormalisePath(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');

    // A leading "./" is the same place and a different string, and a reference
    // that disagrees with the scan by two characters resolves to nothing.
    while (out.rfind("./", 0) == 0) out.erase(0, 2);
    return out;
}

bool AssetDatabase::IsAssetPath(const std::string& path) {
    const std::string normalised = NormalisePath(path);

    // Shaders are compiled by the build and named in code, not by a scene. A
    // glTF's `.bin` is named by the `.gltf` beside it, and giving it an
    // identity nothing references would be a sidecar per buddy file for no
    // benefit. Scenes and prefabs are named by the MANIFEST, which the
    // standalone runtime reads before any database exists, so they have to stay
    // self-contained paths - see the note in GameRuntime.
    static const char* kExtensions[] = {
        ".png", ".jpg", ".jpeg", ".tga", ".bmp",     // textures
        ".obj", ".gltf", ".glb",                     // models
        ".wav",                                      // audio
        ".material",                                 // material assets
    };

    const size_t dot = normalised.find_last_of('.');
    if (dot == std::string::npos) return false;

    std::string extension = normalised.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const char* candidate : kExtensions) {
        if (extension == candidate) return true;
    }
    return false;
}

uint64_t AssetDatabase::HashFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return 0;

    uint64_t hash = kFnvOffset;
    char buffer[64 * 1024];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        hash = fnv1a(buffer, static_cast<size_t>(file.gcount()), hash);
    }

    // Zero is what a file that could not be read returns, so a real file that
    // happened to hash to it would be indistinguishable from a failure. One
    // file in eighteen quintillion, and the fix is a single compare.
    return hash == 0 ? 1 : hash;
}

std::string AssetDatabase::MintGuid(const std::string& path, uint64_t contentHash) {
    return toHex(fnv1a(NormalisePath(path))) + toHex(contentHash);
}

std::vector<std::string> AssetDatabase::CollectAssets(const std::string& root) {
    std::vector<std::string> found;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return found;

    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const std::string path = NormalisePath(it->path().generic_string());
        if (!IsAssetPath(path)) continue;
        found.push_back(path);
    }

    // Sorted, so an ambiguous adoption - two identical files, or two orphans
    // with the same contents - lands on the same one every time rather than on
    // whatever the filesystem happened to enumerate first.
    std::sort(found.begin(), found.end());
    return found;
}

bool AssetDatabase::ReadMeta(const std::string& metaPath, Entry& out) {
    std::ifstream file(metaPath, std::ios::binary);
    if (!file.is_open()) return false;

    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    Json::Value root;
    std::string error;
    if (!Json::Parse(text, root, error) || !root.IsObject()) return false;

    out.guid = root["Guid"].AsString("");
    if (out.guid.size() != 32) return false;

    // Stored as a string: JSON numbers are doubles, and a 64-bit hash does not
    // survive one. It came back as a rounded value and every adoption missed.
    const std::string hash = root["Hash"].AsString("");
    out.contentHash = hash.empty() ? 0 : std::strtoull(hash.c_str(), nullptr, 16);
    return true;
}

bool AssetDatabase::WriteMeta(const std::string& metaPath, const Entry& entry) {
    std::ofstream file(metaPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "{\n"
         << "  \"Guid\": \"" << entry.guid << "\",\n"
         << "  \"Hash\": \"" << toHex(entry.contentHash) << "\"\n"
         << "}\n";
    return file.good();
}

void AssetDatabase::Remember(const Entry& entry) {
    m_byGuid[entry.guid] = entry;
    m_guidByPath[entry.path] = entry.guid;
}

void AssetDatabase::Clear() {
    m_byGuid.clear();
    m_guidByPath.clear();
    m_stats = Stats{};
    m_firstUnresolved.clear();
}

AssetDatabase::ScanResult AssetDatabase::Scan(const std::string& root) {
    Clear();

    ScanResult result;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return result;
    result.ok = true;

    for (const std::string& path : CollectAssets(root)) {
        Entry entry;
        if (!ReadMeta(MetaPathFor(path), entry)) {
            ++result.unidentified;
            continue;
        }
        entry.path = path;
        Remember(entry);
        ++result.identified;
    }

    // Sidecars whose asset is gone. Counted rather than deleted: this is the
    // trace a rename leaves behind, and Import is what turns it back into a
    // working reference.
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const std::string path = NormalisePath(it->path().generic_string());
        if (path.size() < 5 || path.compare(path.size() - 5, 5, ".meta") != 0) continue;
        const std::string asset = path.substr(0, path.size() - 5);
        if (!fs::exists(asset, ec)) ++result.orphaned;
    }

    return result;
}

AssetDatabase::ImportResult AssetDatabase::Import(const std::string& root) {
    ImportResult result;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return result;
    result.ok = true;

    const std::vector<std::string> assets = CollectAssets(root);

    // ---- What already has an identity --------------------------------------
    Clear();
    std::vector<std::string> unidentified;
    for (const std::string& path : assets) {
        Entry entry;
        if (!ReadMeta(MetaPathFor(path), entry)) {
            unidentified.push_back(path);
            continue;
        }
        entry.path = path;

        // The stored hash is what adoption matches on, so it has to keep up
        // with the file. An identity is NOT re-minted when contents change -
        // it is the same asset, edited.
        const uint64_t current = HashFile(path);
        if (current != 0 && current != entry.contentHash) {
            entry.contentHash = current;
            if (WriteMeta(MetaPathFor(path), entry)) ++result.refreshed;
        }
        Remember(entry);
    }

    // ---- Orphans, BEFORE anything is minted --------------------------------
    //
    // A sidecar whose asset is gone is what a rename leaves behind. Matching it
    // to an un-identified file by CONTENT is what carries the identity across,
    // and it has to happen first: mint one new identity and the file it would
    // have been adopted by is no longer un-identified.
    struct Orphan {
        std::string metaPath;

        // Where the asset USED to be, taken from the sidecar's own name rather
        // than from inside it: a .meta stores a guid and a hash, never a path,
        // because the path is the one thing about an asset that is allowed to
        // change. It is the name of the file that is not there any more, which
        // is exactly the reference a scene still open is holding.
        std::string assetPath;

        Entry entry;
    };
    std::vector<Orphan> orphans;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const std::string path = NormalisePath(it->path().generic_string());
        if (path.size() < 5 || path.compare(path.size() - 5, 5, ".meta") != 0) continue;
        if (fs::exists(path.substr(0, path.size() - 5), ec)) continue;

        Orphan orphan;
        orphan.metaPath = path;
        orphan.assetPath = NormalisePath(path.substr(0, path.size() - 5));
        if (!ReadMeta(path, orphan.entry)) continue;
        if (orphan.entry.contentHash == 0) continue;
        orphans.push_back(std::move(orphan));
    }
    std::sort(orphans.begin(), orphans.end(),
              [](const Orphan& a, const Orphan& b) { return a.metaPath < b.metaPath; });

    std::vector<bool> claimed(orphans.size(), false);

    for (const std::string& path : unidentified) {
        const uint64_t hash = HashFile(path);
        if (hash == 0) continue;

        // The limitation, stated rather than solved: the stored hash is from
        // the last import, so a file that was renamed AND edited before the
        // next one matches nothing and gets a fresh identity. Recovering that
        // would need a similarity measure rather than an equality, which is a
        // different feature with a different failure mode.
        size_t match = orphans.size();
        for (size_t i = 0; i < orphans.size(); ++i) {
            if (claimed[i] || orphans[i].entry.contentHash != hash) continue;
            match = i;
            break;
        }
        if (match == orphans.size()) continue;

        claimed[match] = true;
        Entry entry = orphans[match].entry;

        // This is the only moment both halves of the move exist at once.
        const std::string from = orphans[match].assetPath;

        entry.path = path;
        entry.contentHash = hash;
        if (!WriteMeta(MetaPathFor(path), entry)) continue;

        // The old sidecar goes, or the next import would try to adopt from it
        // again and hand a second file the same identity.
        fs::remove(orphans[match].metaPath, ec);

        Remember(entry);
        ++result.adopted;
        // Recorded even when `from` is empty - a sidecar written by an older
        // version may not carry a path - because the count and the list must
        // agree, and a move with nothing to rewrite is skipped by the caller
        // rather than dropped silently here.
        result.moved.push_back(Move{entry.guid, from, path});
        SUPERSONIC_LOG_INFO("AssetDatabase")
            << "Adopted " << entry.guid.substr(0, 8) << " from " << orphans[match].metaPath
            << " for " << path << " (same contents)." << std::endl;
    }

    // ---- Whatever is left is genuinely new ---------------------------------
    for (const std::string& path : unidentified) {
        if (m_guidByPath.find(path) != m_guidByPath.end()) continue;   // adopted above

        Entry entry;
        entry.path = path;
        entry.contentHash = HashFile(path);
        entry.guid = MintGuid(path, entry.contentHash);

        // Two files that mint the same identity would be one asset with two
        // paths, and the second would silently take over the first's
        // references. It needs identical contents AND an identical path, which
        // cannot happen - but the check costs nothing and the failure would be
        // invisible.
        if (m_byGuid.find(entry.guid) != m_byGuid.end()) {
            SUPERSONIC_LOG_ERROR("AssetDatabase")
                << "Refusing to mint a duplicate identity for " << path << "." << std::endl;
            continue;
        }

        if (!WriteMeta(MetaPathFor(path), entry)) continue;
        Remember(entry);
        ++result.minted;
    }

    return result;
}

std::string AssetDatabase::Resolve(const std::string& guid, const std::string& path) {
    if (!guid.empty()) {
        if (const auto it = m_byGuid.find(guid); it != m_byGuid.end()) {
            ++m_stats.byGuid;
            return it->second.path;
        }

        // A reference that carries an identity nothing answers to.
        //
        // Counted always, logged ONCE. MainScene alone has eighteen texture
        // references, so a project whose sidecars went missing would print
        // twenty-odd identical lines per load and every one again on each
        // Reload Scene - and a signal that arrives twenty times is one nobody
        // reads. The first is named in full and the rest are in the count, which
        // ReportUnresolved prints when the load is over.
        ++m_stats.unresolved;
        if (m_firstUnresolved.empty()) m_firstUnresolved = guid;
        return path;
    }

    ++m_stats.byPath;
    return path;
}

std::string AssetDatabase::GuidForPath(const std::string& path) const {
    const auto it = m_guidByPath.find(NormalisePath(path));
    return it == m_guidByPath.end() ? std::string() : it->second;
}

const std::string* AssetDatabase::PathForGuid(const std::string& guid) const {
    const auto it = m_byGuid.find(guid);
    return it == m_byGuid.end() ? nullptr : &it->second.path;
}

void AssetDatabase::ReportUnresolved() {
    if (m_stats.unresolved == 0) return;

    SUPERSONIC_LOG_WARN("AssetDatabase")
        << m_stats.unresolved << " reference(s) carry an identity no asset answers to - "
        << "the first was " << m_firstUnresolved << ". Each fell back to the path it was "
        << "saved with, which may work today and will not survive the next rename. A .meta "
        << "was probably lost; --import-assets can recover it if the file is still there."
        << std::endl;
}

AssetDatabase& AssetDatabase::Instance() {
    static AssetDatabase database;
    return database;
}

} // namespace Supersonic
