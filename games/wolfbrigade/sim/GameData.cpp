#include "sim/GameData.hpp"

#include <fstream>
#include <sstream>

namespace WolfBrigade {

namespace {

using Supersonic::Json::Value;

// Logical key -> filename, and the ORDER data_loader.gd's FILES dictionary
// lists them in. Kept as a vector rather than a map so the order is the
// original's rather than alphabetical: the two logs are meant to be readable
// side by side while a slice is being ported.
const std::vector<std::pair<std::string, std::string>>& fileTable() {
    static const std::vector<std::pair<std::string, std::string>> table{
        {"world", "world.json"},
        {"units", "units.json"},
        {"buildings", "buildings.json"},
        {"waves", "waves.json"},
        {"economy", "economy.json"},
        {"upgrades", "upgrades.json"},
        {"difficulty", "difficulty.json"},
        {"audio", "audio.json"},
        {"meta", "meta.json"},
        {"fx", "fx.json"},
    };
    return table;
}

} // namespace

int GameData::FileCount() { return static_cast<int>(fileTable().size()); }

const std::vector<std::string>& GameData::FileKeys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> out;
        out.reserve(fileTable().size());
        for (const auto& [key, file] : fileTable()) {
            (void)file;
            out.push_back(key);
        }
        return out;
    }();
    return keys;
}

bool GameData::LoadAll(const std::string& directory) {
    m_documents.clear();
    m_loadErrors.clear();

    // A trailing separator either way, so callers can pass "data" or "data/".
    std::string base = directory;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';

    for (const auto& [key, file] : fileTable()) {
        const std::string path = base + file;

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            m_loadErrors.push_back("missing file: " + path);
            // An EMPTY OBJECT rather than nothing. Every accessor keeps its
            // type, so a missing file reads as "this file says nothing" and the
            // validator reports the cross-references it breaks - instead of the
            // first caller to index into it getting a null and going quiet.
            m_documents[key] = Value(Supersonic::Json::Object{});
            continue;
        }

        std::ostringstream buffer;
        buffer << in.rdbuf();

        // Named, because Json::Parser holds a REFERENCE to what it is given.
        // Handing it buffer.str() directly parses a string that has already
        // been destroyed - which does not crash, it just fails on the first
        // character and reports every file as malformed JSON.
        const std::string text = buffer.str();

        Value parsed;
        Supersonic::Json::Parser parser(text);
        if (!parser.Parse(parsed)) {
            m_loadErrors.push_back("JSON error in " + path);
            m_documents[key] = Value(Supersonic::Json::Object{});
            continue;
        }

        m_documents[key] = std::move(parsed);
    }

    return m_loadErrors.empty();
}

const Value& GameData::Raw(const std::string& key) const {
    static const Value kNull;
    const auto it = m_documents.find(key);
    return it == m_documents.end() ? kNull : it->second;
}

std::string GameData::DifficultyDefault() const {
    // "normal" when the file says nothing, which is what a harness that skips
    // the menu gets - and the original spells the same fallback in the same
    // place rather than leaving it to the caller.
    return Difficulty()["default"].AsString("normal");
}

} // namespace WolfBrigade
