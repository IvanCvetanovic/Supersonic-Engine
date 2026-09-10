#include "sim/Progression.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/Json.hpp"

namespace WolfBrigade {

namespace {

using Supersonic::Json::Value;

// The effects an upgrade has on one entity: `effects.<entity>` as field -> delta.
const Value& effectsFor(const Value& definition, const std::string& entityId) {
    return definition["effects"][entityId];
}

} // namespace

// --- Profile ---------------------------------------------------------------

int Profile::MetaLevel(const std::string& id) const {
    const auto it = m_metaLevels.find(id);
    return it == m_metaLevels.end() ? 0 : it->second;
}

void Profile::AddRenown(int amount) {
    m_renown = std::max(0, m_renown + amount);
    m_dirty = true;
}

void Profile::RecordWave(int wave) {
    // Dirtied ONLY when the record actually moves, because the original does
    // not write either: `save.gd:92` guards the `_put`. A replay of the easy
    // early waves would otherwise rewrite the file once a wave for nothing.
    if (wave > m_bestWave) {
        m_bestWave = wave;
        m_dirty = true;
    }
}

std::string Profile::Difficulty(const std::string& fallback) const {
    return m_difficulty.empty() ? fallback : m_difficulty;
}

std::string Profile::ControlScheme(const std::string& fallback) const {
    return m_controlScheme.empty() ? fallback : m_controlScheme;
}

void Profile::SetMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    m_dirty = true;
}

void Profile::ResetProgress() {
    m_renown = 0;
    m_bestWave = 0;
    m_metaLevels.clear();
    m_dirty = true;

    // The preferences are NOT touched, and this is the whole point of the
    // function - see the header. Naming them here rather than leaving them to
    // an implicit "everything else" is what stops the next field from being
    // wiped by accident.
}

std::string Profile::ToJson() const {
    std::ostringstream out;
    out << "{\n  \"renown\": " << m_renown << ",\n  \"best_wave\": " << m_bestWave
        << ",\n  \"difficulty\": \"" << Supersonic::Json::Escape(m_difficulty) << "\""
        << ",\n  \"control_scheme\": \"" << Supersonic::Json::Escape(m_controlScheme) << "\""
        << ",\n  \"muted\": " << (m_muted ? "true" : "false")
        << ",\n  \"master_volume\": " << m_masterVolume
        << ",\n  \"meta_levels\": {";
    bool first = true;
    for (const auto& [id, level] : m_metaLevels) {
        if (!first) out << ",";
        first = false;
        out << "\n    \"" << Supersonic::Json::Escape(id) << "\": " << level;
    }
    out << (first ? "" : "\n  ") << "}\n}\n";
    return out.str();
}

bool Profile::FromJson(const std::string& text) {
    Value parsed;
    Supersonic::Json::Parser parser(text);
    if (!parser.Parse(parsed)) return false;

    m_renown = static_cast<int>(parsed["renown"].AsNumber(0.0));

    // Absent means zero, which is an older profile written before the key
    // existed rather than an error.
    m_bestWave = static_cast<int>(parsed["best_wave"].AsNumber(0.0));

    // Absent means never chosen, which is what an empty string says here and
    // what the menu turns into the data's own default.
    m_difficulty = parsed["difficulty"].AsString("");
    m_controlScheme = parsed["control_scheme"].AsString("");
    m_muted = parsed["muted"].AsBool(false);

    // Clamped on READ as well as on write, because the file is a text document
    // a player can edit and `save.gd:76` clamps in the same place.
    SetMasterVolume(static_cast<float>(parsed["master_volume"].AsNumber(1.0)));

    m_metaLevels.clear();
    for (const auto& [id, level] : parsed["meta_levels"].AsObject()) {
        m_metaLevels[id] = static_cast<int>(level.AsNumber(0.0));
    }

    // LAST, after every setter above has had its say. SetMasterVolume dirties
    // like any other setter, and a profile that has just arrived from disk has
    // nothing to write back.
    m_dirty = false;
    return true;
}

bool Profile::Save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << ToJson();
    if (!out.good()) return false;

    // Cleared only on a write that SUCCEEDED. A failed save that reported
    // itself clean would be a save silently skipped on every tick after.
    m_dirty = false;
    return true;
}

bool Profile::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);

    // A profile that is not there is a new player, not a failure. The original
    // says the same thing by starting from an empty dictionary.
    //
    // ASSIGNMENT, not a list of fields, and the reason is a bug that shipped:
    // this cleared m_renown and m_metaLevels and silently left m_bestWave
    // holding whatever the previous profile had reached. It was inert only
    // because nothing outside the tests called Load at all; the moment a save
    // path exists it is live, and a list of fields is a thing the NEXT field
    // gets left out of too.
    if (!in) {
        *this = Profile{};
        return false;
    }


    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    return FromJson(text);
}

// --- Upgrades --------------------------------------------------------------

namespace Upgrades {

bool IsAvailable(const GameData& data, const GameState& state, const std::string& id) {
    if (state.IsResearched(id)) return false;
    for (const Value& requirement : data.Upgrade(id)["requires"].AsArray()) {
        if (!state.IsResearched(requirement.AsString())) return false;
    }
    return true;
}

bool CanResearch(const GameData& data, const GameState& state, const std::string& id) {
    if (state.IsResearched(id)) return false;

    // An id nobody authored is not researchable, and is a different answer from
    // "you cannot afford it". Without this a typo in a UI button would look
    // like a free upgrade.
    const Value& definition = data.Upgrade(id);
    if (!definition.IsObject() || definition.AsObject().empty()) return false;

    for (const Value& requirement : definition["requires"].AsArray()) {
        if (!state.IsResearched(requirement.AsString())) return false;
    }

    Cost cost;
    for (const auto& [resource, amount] : definition["cost"].AsObject()) {
        cost[resource] = static_cast<int>(amount.AsNumber());
    }
    return state.CanAfford(cost);
}

bool Research(const GameData& data, GameState& state, const std::string& id,
              const std::vector<Building*>& existing) {
    if (!CanResearch(data, state, id)) return false;

    const Value& definition = data.Upgrade(id);
    Cost cost;
    for (const auto& [resource, amount] : definition["cost"].AsObject()) {
        cost[resource] = static_cast<int>(amount.AsNumber());
    }

    // Checked and then spent, and the spend can still fail if something changed
    // between the two. The original keeps both for the same reason.
    if (!state.TrySpend(cost)) return false;

    state.MarkResearched(id);

    // RETROACTIVE for buildings, and only for buildings. A structure the player
    // already paid for is the thing they are upgrading; a soldier already in
    // the field is not - they train new ones.
    for (Building* building : existing) {
        if (building == nullptr || !building->IsAlive()) continue;
        for (const auto& [field, delta] :
             effectsFor(definition, building->Stats().id).AsObject()) {
            building->ApplyUpgradeEffect(field, delta.AsNumber());
        }
    }
    return true;
}

namespace {

// Adds every researched upgrade's deltas for this entity. Shared by the unit
// and building paths, which differ only in the type they are writing into.
template <typename Stats>
int applyResearched(const GameData& data, const GameState& state, Stats& stats,
                    const std::string& entityId) {
    int unknownFields = 0;
    for (const auto& [id, definition] : data.Upgrades().AsObject()) {
        if (!state.IsResearched(id)) continue;
        for (const auto& [field, delta] : effectsFor(definition, entityId).AsObject()) {
            if (!stats.ApplyDelta(field, delta.AsNumber())) ++unknownFields;
        }
    }
    return unknownFields;
}

} // namespace

UnitStats ForUnit(const GameData& data, const GameState& state, const Profile& profile,
                  const std::string& unitId) {
    UnitStats stats = UnitStats::FromJson(unitId, data.Unit(unitId));
    applyResearched(data, state, stats, unitId);
    Meta::Apply(data, profile, stats, unitId);
    return stats;
}

BuildingStats ForBuilding(const GameData& data, const GameState& state, const Profile& profile,
                          const std::string& buildingId) {
    BuildingStats stats = BuildingStats::FromJson(buildingId, data.Building(buildingId));
    applyResearched(data, state, stats, buildingId);
    Meta::Apply(data, profile, stats, buildingId);
    return stats;
}

} // namespace Upgrades

// --- Meta ------------------------------------------------------------------

namespace Meta {

namespace {

template <typename Stats>
int applyOwned(const GameData& data, const Profile& profile, Stats& stats,
               const std::string& entityId) {
    int unknownFields = 0;
    for (const auto& [id, definition] : data.MetaUpgrades().AsObject()) {
        const int level = profile.MetaLevel(id);
        if (level <= 0) continue;

        for (const auto& [field, delta] : effectsFor(definition, entityId).AsObject()) {
            // Times the level, and flat. Three levels of Veteran Soldiers is
            // +45 hit points, not 15 compounded three times - which at any
            // meaningful level would be a different game.
            const double total = delta.AsNumber() * static_cast<double>(level);
            if (!stats.ApplyDelta(field, total)) ++unknownFields;
        }
    }
    return unknownFields;
}

} // namespace

int Apply(const GameData& data, const Profile& profile, UnitStats& stats,
          const std::string& entityId) {
    return applyOwned(data, profile, stats, entityId);
}

int Apply(const GameData& data, const Profile& profile, BuildingStats& stats,
          const std::string& entityId) {
    return applyOwned(data, profile, stats, entityId);
}

int StartingResourceBonus(const GameData& data, const Profile& profile,
                          const std::string& resource) {
    int total = 0;
    for (const auto& [id, definition] : data.MetaUpgrades().AsObject()) {
        const int level = profile.MetaLevel(id);
        if (level <= 0) continue;

        // "starting_resources" is a pseudo-entity: an upgrade that grants wood
        // at the start of a run targets it the way one that toughens soldiers
        // targets "soldier". Only this function reads it, and Meta::Apply
        // deliberately does not - a resource is not a stat block.
        const double perLevel =
            definition["effects"]["starting_resources"][resource].AsNumber(0.0);
        total += static_cast<int>(std::lround(perLevel * static_cast<double>(level)));
    }
    return total;
}

int MaxLevel(const GameData& data, const std::string& id) {
    return static_cast<int>(data.MetaUpgrade(id)["max_level"].AsNumber(0.0));
}

bool IsMaxed(const GameData& data, const Profile& profile, const std::string& id) {
    return profile.MetaLevel(id) >= MaxLevel(data, id);
}

int NextCost(const GameData& data, const Profile& profile, const std::string& id) {
    const Value& definition = data.MetaUpgrade(id);
    if (!definition.IsObject() || definition.AsObject().empty()) return -1;
    if (IsMaxed(data, profile, id)) return -1;

    const int base = static_cast<int>(definition["base_cost"].AsNumber(0.0));
    const int growth = static_cast<int>(definition["cost_growth"].AsNumber(0.0));
    return base + growth * profile.MetaLevel(id);
}

bool CanBuy(const GameData& data, const Profile& profile, const std::string& id) {
    const int cost = NextCost(data, profile, id);
    return cost >= 0 && profile.Renown() >= cost;
}

bool Buy(const GameData& data, Profile& profile, const std::string& id) {
    if (!CanBuy(data, profile, id)) return false;

    // Read before the level changes. Reading it after would charge the price of
    // the level the player just bought rather than the one they are buying.
    const int cost = NextCost(data, profile, id);
    profile.AddRenown(-cost);
    profile.SetMetaLevel(id, profile.MetaLevel(id) + 1);
    return true;
}

int RunEndRenown(const GameData& data, int wave, bool won) {
    const Value& currency = data.MetaCurrency();
    const int waves = std::max(0, wave);
    const int perWave = static_cast<int>(currency["per_wave"].AsNumber(0.0));
    const int growth = static_cast<int>(currency["per_wave_growth"].AsNumber(0.0));

    // Closed form of the sum over waves survived. Integer division is safe
    // because w*(w-1) is always even.
    int total = perWave * waves + growth * (waves * (waves - 1)) / 2;
    if (won) total += static_cast<int>(currency["victory_bonus"].AsNumber(0.0));
    return total;
}

int AwardRunEnd(const GameData& data, Profile& profile, int wave, bool won) {
    const int award = RunEndRenown(data, wave, won);
    if (award > 0) profile.AddRenown(award);
    return award;
}

} // namespace Meta

} // namespace WolfBrigade
