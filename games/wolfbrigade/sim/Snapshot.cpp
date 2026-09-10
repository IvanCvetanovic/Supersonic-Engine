#include "sim/Snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "sim/Building.hpp"
#include "sim/CapturePoint.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameData.hpp"
#include "sim/GameState.hpp"
#include "sim/Lane.hpp"
#include "sim/Progression.hpp"
#include "sim/Projectiles.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/Unit.hpp"
#include "sim/WaveDirector.hpp"

namespace WolfBrigade {
namespace Snapshot {

namespace {

using Supersonic::Json::Array;
using Supersonic::Json::Object;
using Supersonic::Json::Value;

// A double, written so it reads back as the same double.
//
// %.17g is the shortest width that round-trips every IEEE-754 double. The
// default six significant digits would truncate an attack cooldown of
// 0.90000000000000002 to 0.9, and the original's own harness would not notice:
// it compares cooldowns within 0.001 and elapsed time within 0.5. This port's
// whole numeric argument is that a cooldown decremented by 0.1 lands on a
// different side of zero in float than in double - a save that rounds it
// reintroduces exactly that.
//
// Not std::to_chars: nothing in this tree uses <charconv>'s floating-point
// overloads, and shortest-round-trip buys nothing over 17 digits here.
void writeNumber(std::ostream& out, double value) {
    // JSON has no inf and no nan. Writing one produces a document that will not
    // parse, which turns a numeric bug into a file that cannot be loaded at
    // all; zero is wrong in a way somebody can see and recover from.
    if (!std::isfinite(value)) {
        out << '0';
        return;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out << buffer;
}

void writeValue(std::ostream& out, const Value& value);

void writeObject(std::ostream& out, const Object& object) {
    out << '{';
    bool first = true;
    for (const auto& [key, member] : object) {
        if (!first) out << ',';
        first = false;
        out << '"' << Supersonic::Json::Escape(key) << "\":";
        writeValue(out, member);
    }
    out << '}';
}

void writeValue(std::ostream& out, const Value& value) {
    switch (value.GetType()) {
    case Supersonic::Json::Type::Null:   out << "null"; break;
    case Supersonic::Json::Type::Bool:   out << (value.AsBool() ? "true" : "false"); break;
    case Supersonic::Json::Type::Number: writeNumber(out, value.AsNumber()); break;
    case Supersonic::Json::Type::String:
        out << '"' << Supersonic::Json::Escape(value.AsString()) << '"';
        break;
    case Supersonic::Json::Type::Array: {
        out << '[';
        bool first = true;
        for (const Value& entry : value.AsArray()) {
            if (!first) out << ',';
            first = false;
            writeValue(out, entry);
        }
        out << ']';
        break;
    }
    case Supersonic::Json::Type::Object: writeObject(out, value.AsObject()); break;
    }
}

} // namespace

std::string ToText(const Value& document) {
    std::ostringstream out;
    writeValue(out, document);
    return out.str();
}

Value FromText(const std::string& text) {
    Value parsed;

    // `text` is a named reference the caller owns, and it has to be: Json::Parser
    // holds a REFERENCE to what it is given, which is why its rvalue constructor
    // is deleted. Handing it a temporary parses a string that no longer exists.
    Supersonic::Json::Parser parser(text);
    if (!parser.Parse(parsed)) return Value();
    return parsed;
}

// --- Capture ---------------------------------------------------------------

Value Capture(const Scene& scene, const GameState& state, const WaveDirector& director,
              CaptureReport* report) {
    CaptureReport local;
    CaptureReport& counts = report != nullptr ? *report : local;

    // Two passes, as in the original: mint every surviving entity an id, then
    // serialise. A reference can only become a number once its referent has
    // one, and a referent may come later in the walk.
    SidTable ids;

    std::vector<std::pair<int, const Unit*>> units;
    std::vector<std::pair<int, const Building*>> buildings;
    std::vector<std::pair<int, const ResourceNode*>> nodes;

    // Buildings first, then nodes, then units - the order the restore needs
    // them in. Nothing depends on it here, but keeping the two in step means a
    // reader can follow one against the other.
    for (Building* building : scene.buildings) {
        if (building == nullptr || !building->IsAlive()) {
            ++counts.skippedDead;
            continue;
        }
        buildings.emplace_back(ids.Add(static_cast<const Damageable*>(building)), building);
    }
    for (ResourceNode* node : scene.resourceNodes) {
        // An exhausted node is not restored, matching the original: it has
        // nothing left to give and its only remaining job is to fade out.
        if (node == nullptr || node->IsEmpty()) {
            ++counts.skippedDead;
            continue;
        }
        nodes.emplace_back(ids.Add(node), node);
    }
    for (Unit* unit : scene.units) {
        if (unit == nullptr || !unit->IsAlive()) {
            ++counts.skippedDead;
            continue;
        }
        units.emplace_back(ids.Add(static_cast<const Damageable*>(unit)), unit);
    }

    Array unitsOut;
    for (const auto& [sid, unit] : units) {
        Value record = unit->ToSave(ids);
        record.Set("sid", Value(static_cast<double>(sid)));

        // Counted here rather than inside the unit, because only the capture
        // knows whether a -1 means "the referent is dead" or "the caller did
        // not tell me about it". Both are -1 in the file; only one is a bug,
        // and a nonzero count is what makes the second one visible at all -
        // a capture-restore-capture comparison cannot see an entity that is
        // missing from both sides.
        for (const char* key : {"ref_tree", "ref_build", "ref_attack", "ref_heal"}) {
            if (static_cast<int>(record[key].AsNumber(-1.0)) < 0) ++counts.unresolvedReferences;
        }
        unitsOut.push_back(std::move(record));
    }

    Array buildingsOut;
    for (const auto& [sid, building] : buildings) {
        Value record = building->ToSave();
        record.Set("sid", Value(static_cast<double>(sid)));
        buildingsOut.push_back(std::move(record));
    }

    Array nodesOut;
    for (const auto& [sid, node] : nodes) {
        Value record = node->ToSave();
        record.Set("sid", Value(static_cast<double>(sid)));
        nodesOut.push_back(std::move(record));
    }

    // Capture-point tug state, index-aligned to the level's list. Their
    // geometry comes back from the level's data on every boot; only progress
    // and holder are the run's.
    Array pointsOut;
    for (const CapturePoint* point : scene.capturePoints) {
        if (point != nullptr) pointsOut.push_back(point->ToSave());
    }

    Object out;
    out["version"] = Value(static_cast<double>(kVersion));
    out["game_state"] = state.ToSave();
    out["wave_director"] = director.ToSave();
    out["units"] = Value(std::move(unitsOut));
    out["buildings"] = Value(std::move(buildingsOut));
    out["resource_nodes"] = Value(std::move(nodesOut));

    // Read back with an empty default, so a save from before capture points
    // restores with neutral ones rather than being refused - no version bump,
    // exactly as the original added it.
    out["capture_points"] = Value(std::move(pointsOut));
    return Value(std::move(out));
}

bool IsValid(const Value& snapshot) {
    if (!snapshot.IsObject() || snapshot.AsObject().empty()) return false;
    return static_cast<int>(snapshot["version"].AsNumber(-1.0)) == kVersion;
}

// --- Restore ---------------------------------------------------------------

namespace {

// Whether the current data files still describe this id. A snapshot older than
// a re-tune can name a unit that no longer exists.
bool knownUnit(const GameData& data, const std::string& id) {
    return data.Unit(id).IsObject() && !data.Unit(id).AsObject().empty();
}
bool knownBuilding(const GameData& data, const std::string& id) {
    return data.Building(id).IsObject() && !data.Building(id).AsObject().empty();
}

} // namespace

bool Restore(const Value& snapshot, const GameData& data, GameState& state,
             const Profile& profile, WaveDirector& director, RestoreSink& sink, Lane& lane,
             ProjectilePool& projectiles, RestoreReport* report) {
    if (!IsValid(snapshot)) return false;

    RestoreReport local;
    RestoreReport& counts = report != nullptr ? *report : local;

    // Both are DERIVED and neither is saved. Cleared before anything is built,
    // because a restore into a warm process would otherwise leave the previous
    // run's corpses in the lane - which is exactly what the original's
    // Lane.clear_all() exists to prevent, and a unit never unregisters itself.
    lane.Clear();
    projectiles.Clear();

    // 1. The run's own state first. The mode and the difficulty are read by
    //    everything downstream, and the researched upgrades have to be in
    //    place before a building's stats are re-derived from them.
    state.FromSave(snapshot["game_state"], profile.AllMetaLevels());

    SidResolver resolver;

    // 2. Buildings before units: a worker's build target and a raider's walk
    //    goal both have to exist before anything can point at them.
    for (const Value& record : snapshot["buildings"].AsArray()) {
        const std::string id = record["id"].AsString();
        if (!knownBuilding(data, id)) {
            // SKIPPED, never fabricated. A missing row gives maxHp 1, and the
            // hp clamp would then put a saved Town Hall back at one hit point.
            ++counts.unknownIds;
            continue;
        }

        // Re-derived, not saved: the current research and the CURRENT profile
        // decide what a building is worth now.
        const BuildingStats stats = Upgrades::ForBuilding(data, state, profile, id);

        const auto& pos = record["pos"].AsArray();
        const glm::vec2 position = pos.size() >= 2
                                       ? glm::vec2(pos[0].AsFloat(), pos[1].AsFloat())
                                       : glm::vec2(0.0f);
        const bool complete =
            static_cast<int>(record["state"].AsNumber(1.0)) ==
            static_cast<int>(Building::State::Complete);

        Building* building = sink.CreateBuilding(stats, complete, position);
        if (building == nullptr) continue;

        building->SetTrainTimes(data.Units());
        building->FromSave(record);
        resolver.Bind(static_cast<int>(record["sid"].AsNumber(-1.0)), building);
        ++counts.buildings;
    }

    // 2b. Capture points already exist - the boot spawned them from the
    //     level's data on both paths - so only their tug state comes back,
    //     index-aligned. A save from before points has none, and they stay
    //     neutral; a level that has since lost a point ignores the extra.
    const std::vector<CapturePoint*> points = sink.CapturePointsToRestore();
    const auto& savedPoints = snapshot["capture_points"].AsArray();
    for (size_t i = 0; i < std::min(savedPoints.size(), points.size()); ++i) {
        if (points[i] != nullptr) points[i]->FromSave(savedPoints[i]);
    }

    // 3. Resource nodes before units, for the same reason: a gather target has
    //    to exist before a worker can be pointed back at it.
    for (const Value& record : snapshot["resource_nodes"].AsArray()) {
        ResourceNode restored = ResourceNode::FromSave(record);

        // A save from before art has no sprite, and takes today's from the
        // economy file rather than restoring a flat rectangle into a world of
        // sprites (verify_snapshot 11).
        if (restored.sprite.empty()) {
            restored.sprite = ResourceNode::SpriteFor(data, restored.resource);
        }
        ResourceNode* node = sink.CreateResourceNode(restored);
        if (node == nullptr) continue;
        resolver.Bind(static_cast<int>(record["sid"].AsNumber(-1.0)), node);
        ++counts.resourceNodes;
    }

    // 4. Units. Their stats are the base row plus the BAKED block, never
    //    Upgrades::ForUnit: units are not retroactively upgraded, and an enemy
    //    carries difficulty and per-spawn scaling that re-deriving would either
    //    lose or apply twice.
    std::vector<std::pair<Unit*, const Value*>> relinkable;
    for (const Value& record : snapshot["units"].AsArray()) {
        const std::string id = record["id"].AsString();
        if (!knownUnit(data, id)) {
            // A raider whose row is gone would come back as every default -
            // a player-faction worker - registered in the wrong lane list,
            // counted by no enemy tally, and handing the player a victory.
            ++counts.unknownIds;
            continue;
        }

        UnitStats stats = UnitStats::FromJson(id, data.Unit(id));
        stats.ApplyBlock(record["stats"]);

        const auto& pos = record["pos"].AsArray();
        const glm::vec2 position = pos.size() >= 2
                                       ? glm::vec2(pos[0].AsFloat(), pos[1].AsFloat())
                                       : glm::vec2(0.0f);

        Unit* unit = sink.CreateUnit(stats, position);
        if (unit == nullptr) continue;

        unit->FromSave(record);
        resolver.Bind(static_cast<int>(record["sid"].AsNumber(-1.0)), unit);
        relinkable.emplace_back(unit, &record);
        ++counts.units;
    }

    // 5. Only now, with everything built, do references become pointers.
    for (const auto& [unit, record] : relinkable) {
        unit->Relink(*record, resolver, &counts.unresolvedReferences);
    }

    // 6. The director's counters, and the alive count RECOUNTED from what was
    //    actually rebuilt rather than read from the file. The restore path
    //    bypasses the spawn callback that would have reported each one, so a
    //    stored number would be the only source of a truth nothing corrects.
    int aliveEnemies = 0;
    for (const auto& [unit, record] : relinkable) {
        (void)record;
        if (unit->IsAlive() && unit->Faction() == Factions::kEnemy) ++aliveEnemies;
    }
    director.FromSave(snapshot["wave_director"], aliveEnemies);

    return true;
}

// --- The run file ----------------------------------------------------------

bool SaveRun(const std::string& path, const Value& snapshot) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << ToText(snapshot);
    return out.good();
}

Value LoadRun(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Value();

    std::ostringstream buffer;
    buffer << in.rdbuf();

    // Named, because the parser holds a reference to it.
    const std::string text = buffer.str();
    return FromText(text);
}

bool HasRun(const std::string& path) {
    // Presence, not validity. The original keeps these separate so a corrupt
    // file still counts as "there is a run here" - the menu offers Continue and
    // the load then refuses it, which is a better story than a Continue button
    // that silently vanishes.
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

void ClearRun(const std::string& path) { std::remove(path.c_str()); }

} // namespace Snapshot
} // namespace WolfBrigade
