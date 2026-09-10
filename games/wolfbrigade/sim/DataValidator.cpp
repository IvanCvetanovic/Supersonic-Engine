#include "sim/DataValidator.hpp"

namespace WolfBrigade {
namespace DataValidator {

namespace {

using Supersonic::Json::Array;
using Supersonic::Json::Object;
using Supersonic::Json::Value;

const char* typeName(const Value& value) {
    switch (value.GetType()) {
    case Supersonic::Json::Type::Null:   return "null";
    case Supersonic::Json::Type::Bool:   return "a boolean";
    case Supersonic::Json::Type::Number: return "a number";
    case Supersonic::Json::Type::String: return "text";
    case Supersonic::Json::Type::Array:  return "a list [...]";
    case Supersonic::Json::Type::Object: return "an object {...}";
    }
    return "something else";
}

// The keys of an object, as ids.
std::vector<std::string> keysOf(const Object& object) {
    std::vector<std::string> keys;
    keys.reserve(object.size());
    for (const auto& [key, value] : object) {
        (void)value;
        keys.push_back(key);
    }
    return keys;
}

bool contains(const std::vector<std::string>& haystack, const std::string& needle) {
    for (const std::string& candidate : haystack) {
        if (candidate == needle) return true;
    }
    return false;
}

void appendAll(Issues& out, const Issues& more) {
    out.insert(out.end(), more.begin(), more.end());
}

// One reference, for the many checks that validate a single id.
std::vector<std::string> one(const std::string& id) { return {id}; }

// Resource ids: the keys of starting_resources, plus anything a resource node
// produces that is not already among them.
//
// Deliberately derived rather than listed. The set of resources IS whatever the
// economy file says it is, and a hardcoded {wood, food} here would make adding
// a third one a two-file change with no error if you forgot the second.
std::vector<std::string> resourceIds(const Value& economy, Issues& out) {
    std::vector<std::string> ids =
        keysOf(AsObject(economy["starting_resources"], "economy.starting_resources", out));

    for (const Value& node : AsArray(economy["resource_nodes"], "economy.resource_nodes", out)) {
        const Object& fields = AsObject(node, "economy.resource_node", out);
        const auto it = fields.find("resource");
        if (it == fields.end()) continue;
        const std::string resource = it->second.AsString();
        if (!resource.empty() && !contains(ids, resource)) ids.push_back(resource);
    }
    return ids;
}

// A field's value, or null when it is absent - which is how Godot's
// dict.get(key, default) behaves and is why an absent field never reports a
// type problem.
const Value& field(const Object& object, const std::string& key) {
    static const Value kNull;
    const auto it = object.find(key);
    return it == object.end() ? kNull : it->second;
}

void checkMeta(const GameData& data, const std::vector<std::string>& entityIds,
               const std::vector<std::string>& resources, Issues& out) {
    // The pseudo-entity a meta upgrade targets when it grants resources at the
    // start of a run rather than changing a unit. It is not a unit or a
    // building, so it has to be allowed explicitly or "deeper_coffers" reads as
    // a typo.
    std::vector<std::string> effectTargets = entityIds;
    effectTargets.push_back("starting_resources");

    for (const auto& [id, definition] : AsObject(data.MetaUpgrades(), "meta.json upgrades", out)) {
        const std::string context = "meta '" + id + "'";
        const Object& upgrade = AsObject(definition, context, out);

        // Unbuyable, which is a typo rather than a design: an upgrade nobody
        // can ever purchase is one that was meant to have levels.
        if (static_cast<int>(field(upgrade, "max_level").AsNumber(0.0)) < 1) {
            out.push_back(context + " has max_level < 1 (unbuyable)");
        }
        if (upgrade.find("base_cost") == upgrade.end()) {
            out.push_back(context + " is missing base_cost");
        }

        const Object& effects = AsObject(field(upgrade, "effects"), context + " effects", out);
        if (effects.empty()) out.push_back(context + " has no effects (does nothing)");
        appendAll(out, MissingRefs(keysOf(effects), effectTargets, context + " effects"));

        const Object& starting = AsObject(field(effects, "starting_resources"),
                                          context + " starting_resources effect", out);
        appendAll(out, MissingRefs(keysOf(starting), resources,
                                   context + " starting_resources effect"));
    }
}

} // namespace

const Object& AsObject(const Value& value, const std::string& context, Issues& out) {
    if (value.IsObject()) return value.AsObject();

    // Absent is not a problem. The GDScript reaches almost every one of these
    // through dict.get(key, {}), where a missing key yields the empty default
    // and never reaches the type check at all - so a building with no `cost` is
    // a building that costs nothing, not a malformed one.
    static const Object kEmpty;
    if (value.GetType() == Supersonic::Json::Type::Null) return kEmpty;

    out.push_back(context + " should be an object {...} but is " + typeName(value));
    return kEmpty;
}

const Array& AsArray(const Value& value, const std::string& context, Issues& out) {
    if (value.IsArray()) return value.AsArray();

    static const Array kEmpty;
    if (value.GetType() == Supersonic::Json::Type::Null) return kEmpty;

    out.push_back(context + " should be a list [...] but is " + typeName(value));
    return kEmpty;
}

Issues MissingRefs(const std::vector<std::string>& refs,
                   const std::vector<std::string>& validIds,
                   const std::string& context) {
    Issues out;
    for (const std::string& ref : refs) {
        if (ref.empty() || !contains(validIds, ref)) {
            out.push_back(context + " references unknown id '" + ref + "'");
        }
    }
    return out;
}

Issues Check(const GameData& data) {
    Issues out;

    const Object& units = AsObject(data.Units(), "units.json (root)", out);
    const Object& buildings = AsObject(data.Buildings(), "buildings.json (root)", out);
    const Object& upgrades = AsObject(data.Upgrades(), "upgrades.json (root)", out);
    const Object& economy = AsObject(data.Economy(), "economy.json (root)", out);

    const std::vector<std::string> unitIds = keysOf(units);
    const std::vector<std::string> buildingIds = keysOf(buildings);
    const std::vector<std::string> upgradeIds = keysOf(upgrades);
    const std::vector<std::string> resources = resourceIds(data.Economy(), out);

    std::vector<std::string> entityIds = unitIds;
    entityIds.insert(entityIds.end(), buildingIds.begin(), buildingIds.end());

    // Buildings: what they train, what they research, what they cost.
    for (const auto& [id, definition] : buildings) {
        const std::string context = "building '" + id + "'";
        const Object& building = AsObject(definition, context, out);

        std::vector<std::string> trains;
        for (const Value& unit :
             AsArray(field(building, "trains"), context + " trains", out)) {
            trains.push_back(unit.AsString());
        }
        appendAll(out, MissingRefs(trains, unitIds, context + " trains"));

        std::vector<std::string> researches;
        for (const Value& upgrade :
             AsArray(field(building, "researches"), context + " researches", out)) {
            researches.push_back(upgrade.AsString());
        }
        appendAll(out, MissingRefs(researches, upgradeIds, context + " researches"));

        appendAll(out, MissingRefs(keysOf(AsObject(field(building, "cost"), context + " cost", out)),
                                   resources, context + " cost"));
    }

    // Units: what they cost.
    for (const auto& [id, definition] : units) {
        const std::string context = "unit '" + id + "'";
        const Object& unit = AsObject(definition, context, out);
        appendAll(out, MissingRefs(keysOf(AsObject(field(unit, "cost"), context + " cost", out)),
                                   resources, context + " cost"));
    }

    // In-run upgrades: prerequisites, cost, and what each effect targets.
    for (const auto& [id, definition] : upgrades) {
        const std::string context = "upgrade '" + id + "'";
        const Object& upgrade = AsObject(definition, context, out);

        std::vector<std::string> prerequisites;
        for (const Value& prerequisite :
             AsArray(field(upgrade, "requires"), context + " requires", out)) {
            prerequisites.push_back(prerequisite.AsString());
        }
        appendAll(out, MissingRefs(prerequisites, upgradeIds, context + " requires"));

        appendAll(out, MissingRefs(keysOf(AsObject(field(upgrade, "cost"), context + " cost", out)),
                                   resources, context + " cost"));
        appendAll(out, MissingRefs(
                           keysOf(AsObject(field(upgrade, "effects"), context + " effects", out)),
                           entityIds, context + " effects"));
    }

    // Waves: every spawn names a known unit. (Endless mode, and its own
    // configuration check, went with the game's 73999ce.)
    for (const Value& entry : AsArray(data.WaveConfig()["waves"], "waves.json waves", out)) {
        const Object& wave = AsObject(entry, "wave entry", out);

        // The index as authored, so an issue names the wave a person can find
        // in the file rather than its position in an array.
        const Value& index = field(wave, "index");
        const std::string label =
            index.IsNumber() ? std::to_string(static_cast<int>(index.AsNumber())) : "?";
        const std::string context = "wave " + label + " spawn";

        for (const Value& spawn : AsArray(field(wave, "spawns"), context, out)) {
            const Object& fields = AsObject(spawn, context, out);
            appendAll(out, MissingRefs(one(field(fields, "unit").AsString()), unitIds, context));
        }
    }

    // Economy: where workers deposit, and what the nodes produce.
    if (economy.find("deposit_building") != economy.end()) {
        appendAll(out, MissingRefs(one(field(economy, "deposit_building").AsString()), buildingIds,
                                   "economy.deposit_building"));
    }
    for (const Value& node :
         AsArray(field(economy, "resource_nodes"), "economy.resource_nodes", out)) {
        const Object& fields = AsObject(node, "economy.resource_node", out);
        appendAll(out, MissingRefs(one(field(fields, "resource").AsString()), resources,
                                   "economy.resource_node"));
    }

    // Levels: the order names real levels, and every capture point's bonus is
    // well formed - an income bonus needs a real resource, and an unknown kind
    // fails loud instead of silently doing nothing.
    const Object& levels = AsObject(data.Levels(), "levels.json levels", out);
    std::vector<std::string> order;
    for (const Value& id : AsArray(data.Raw("levels")["order"], "levels.order", out)) {
        order.push_back(id.AsString());
    }
    appendAll(out, MissingRefs(order, keysOf(levels), "levels.order"));
    for (const auto& [id, definition] : levels) {
        const std::string context = "level '" + id + "'";
        const Object& level = AsObject(definition, context, out);
        for (const Value& point :
             AsArray(field(level, "capture_points"), context + " capture_points", out)) {
            const Object& capture = AsObject(point, context + " capture_point", out);
            const Object& bonus =
                AsObject(field(capture, "bonus"), context + " capture_point bonus", out);
            // An absent bonus is an income bonus, as the original reads it.
            const std::string kind =
                bonus.empty() ? std::string("income") : field(bonus, "kind").AsString("income");
            if (kind == "income") {
                const Value& resource = bonus.find("resource") != bonus.end()
                                            ? field(bonus, "resource")
                                            : field(capture, "resource");
                appendAll(out, MissingRefs(one(resource.AsString()), resources,
                                           context + " capture_point income"));
            } else if (kind != "army_damage") {
                out.push_back(context + " capture_point has unknown bonus kind '" + kind + "'");
            }
        }
    }

    // Economy: the starting hero, if there is one, is a real unit.
    const std::string startingHero = field(economy, "starting_hero").AsString();
    if (!startingHero.empty()) {
        appendAll(out, MissingRefs(one(startingHero), unitIds, "economy.starting_hero"));
    }

    checkMeta(data, entityIds, resources, out);
    return out;
}

} // namespace DataValidator
} // namespace WolfBrigade
