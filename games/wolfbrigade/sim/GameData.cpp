#include "sim/GameData.hpp"

#include <fstream>
#include <sstream>

namespace WolfBrigade {

namespace {

using Supersonic::Json::Object;
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
        {"levels", "levels.json"},
        {"abilities", "abilities.json"},
    };
    return table;
}

// The sections a level may override, in the order apply_level walks them.
constexpr const char* kLevelSections[] = {"world", "economy", "waves"};

const Value& null() {
    static const Value kNull;
    return kNull;
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
    m_merged.clear();
    m_levelId.clear();
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
            m_documents[key] = Value(Object{});
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
            m_documents[key] = Value(Object{});
            continue;
        }

        m_documents[key] = std::move(parsed);
    }

    // The default level, straight away, as load_all does: validation and any
    // match booted without a menu see a fully merged configuration.
    ApplyLevel(DefaultLevel());

    return m_loadErrors.empty();
}

const Value& GameData::Raw(const std::string& key) const {
    const auto it = m_documents.find(key);
    return it == m_documents.end() ? null() : it->second;
}

const Value& GameData::Section(const std::string& key) const {
    const auto it = m_merged.find(key);
    return it == m_merged.end() ? Raw(key) : it->second;
}

std::string GameData::DifficultyDefault() const {
    // "normal" when the file says nothing, which is what a harness that skips
    // the menu gets - and the original spells the same fallback in the same
    // place rather than leaving it to the caller.
    return Difficulty()["default"].AsString("normal");
}

std::string GameData::DefaultLevel() const {
    const auto& order = LevelsOrder();
    return order.empty() ? std::string("level_1") : order.front().AsString();
}

void GameData::ApplyLevel(const std::string& id) {
    m_levelId = Levels().Has(id) ? id : DefaultLevel();
    const Value& level = Level(m_levelId);

    m_merged.clear();
    for (const char* section : kLevelSections) {
        // From the PRISTINE base every time - the original duplicates the base
        // dictionary on each call - so applying level B after level A is level
        // B, not A with B on top.
        const Value& base = Raw(section);
        Object merged = base.IsObject() ? base.AsObject() : Object{};

        const Value& overrides = level[section];
        if (overrides.IsObject()) {
            for (const auto& [key, value] : overrides.AsObject()) merged[key] = value;
        }
        m_merged[section] = Value(std::move(merged));
    }
}

const Value& GameData::Ability(const std::string& id) const {
    if (!id.empty() && id.front() == '_') return null();
    const Value& ability = Abilities()[id];
    return ability.IsObject() ? ability : null();
}

} // namespace WolfBrigade
