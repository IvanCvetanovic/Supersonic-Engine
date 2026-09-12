#include "sim/Scores.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace MagicPortals::Scores {

namespace {

namespace Json = Supersonic::Json;

// The file, inside whichever directory the game was handed.
std::string FileIn(const std::string& directory) {
    return (std::filesystem::path(directory) / "scores.json").string();
}

// kUnplayed through kGold and nothing else.
//
// A value outside that range is not merely odd: getLargeSpriteMedalName maps
// everything it does not recognise to BRONZE, so a 7 in the file would draw a
// bronze medal on a level and look like a real result.
bool IsMedal(int medal) { return medal >= kUnplayed && medal <= kGold; }

} // namespace

bool Store::Open(const std::string& directory, std::string& error) {
    m_entries.clear();
    m_path.clear();
    error.clear();

    // Memory only, and deliberately silent about it: this is how every suite
    // builds the layer.
    if (directory.empty()) return true;

    const std::string path = FileIn(directory);

    // A directory with nothing saved in it is a player who has not finished a
    // level, not a failure. The path is kept so the first Save creates the file.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        m_path = path;
        return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    // NAMED, not a temporary: Json::Parser holds its text by reference and
    // deletes the rvalue constructor precisely to stop `Parser(read(path))`.
    const std::string text = buffer.str();

    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root["scores"].IsArray()) {
        error = path + ": scores is an array";
        return false;
    }

    for (const Json::Value& entry : root["scores"].AsArray()) {
        if (!entry.IsObject() || !entry.Has("world") || !entry.Has("level") || !entry.Has("medal")) {
            error = path + ": every score names a world, a level and a medal";
            m_entries.clear();
            return false;
        }
        const int world = static_cast<int>(entry["world"].AsNumber(-1.0));
        const int level = static_cast<int>(entry["level"].AsNumber(-1.0));
        const int medal = static_cast<int>(entry["medal"].AsNumber(-1.0));
        if (world < 0 || level < 0 || !IsMedal(medal)) {
            error = path + ": a score outside the medals, or a negative world or level";
            m_entries.clear();
            return false;
        }
        // Through Record, so a file naming the same level twice keeps the
        // better of the two rather than whichever line happened to be last.
        Record(world, level, medal);
    }

    // ONLY ON SUCCESS. A file that exists and will not parse leaves this store
    // memory-only, so the next Save cannot overwrite it - the header says why:
    // starting silently from zero would erase a player's progress, and the
    // unreadable file is the only copy of it there is.
    m_path = path;
    return true;
}

int Store::Get(int world, int level) const {
    for (const Entry& entry : m_entries) {
        if (entry.world == world && entry.level == level) return entry.medal;
    }
    return kUnplayed;
}

bool Store::Record(int world, int level, int medal) {
    // kUnplayed is not recorded: it is the absence of a medal, and writing it
    // would put rows in the file that mean "no row".
    if (world < 0 || level < 0 || !IsMedal(medal) || medal == kUnplayed) return false;

    for (Entry& entry : m_entries) {
        if (entry.world != world || entry.level != level) continue;
        if (medal <= entry.medal) return false; // a worse replay takes nothing away
        entry.medal = medal;
        return true;
    }

    Entry added;
    added.world = world;
    added.level = level;
    added.medal = medal;
    m_entries.push_back(added);
    return true;
}

bool Store::Save(std::string& error) const {
    error.clear();
    if (m_path.empty()) return true; // memory only: nowhere to write is not a failure

    const std::filesystem::path path(m_path);
    std::error_code ec;
    if (path.has_parent_path()) {
        // UserDataDirectory creates its own, but a directory handed in by a
        // test or a -D need not exist yet.
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    // Sorted, so the file does not reshuffle itself every time a level is
    // cleared: a save that rewrites its whole order on each play is one nobody
    // can diff, and this one is small enough that sorting costs nothing.
    std::vector<Entry> ordered = m_entries;
    std::sort(ordered.begin(), ordered.end(), [](const Entry& a, const Entry& b) {
        if (a.world != b.world) return a.world < b.world;
        return a.level < b.level;
    });

    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = temp.string() + ": cannot open for writing";
            return false;
        }
        file << "{\n";
        file << "  \"version\": 1,\n";
        file << "  \"scores\": [\n";
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const Entry& entry = ordered[i];
            file << "    {\"world\": " << entry.world << ", \"level\": " << entry.level
                 << ", \"medal\": " << entry.medal << "}";
            if (i + 1 < ordered.size()) file << ",";
            file << "\n";
        }
        file << "  ]\n";
        file << "}\n";
        if (!file.good()) {
            error = temp.string() + ": write failed";
            return false;
        }
    }

    // THE RENAME, which is PipelineCache's idiom and is here for its reason: a
    // file truncated by a process killed mid-write is worse than no file, and
    // this one is the player's progress.
    ec.clear();
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        // Some filesystems refuse a rename onto an existing file. Remove and
        // retry rather than give up: the temporary is already complete.
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
    }
    if (ec) {
        error = m_path + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace MagicPortals::Scores
