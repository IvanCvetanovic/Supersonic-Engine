#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Json.hpp"
#include "sim/Building.hpp"
#include "sim/SaveIds.hpp"
#include "sim/UnitStats.hpp"

namespace WolfBrigade {

class CapturePoint;
class GameData;
class Lane;
class Profile;
class ProjectilePool;
class Unit;
class WaveDirector;
struct EventBus;
struct ResourceNode;

// Mid-match save and restore, from `scripts/systems/snapshot.gd`.
//
// An OBSERVER, deliberately. It does not own a single entity and does not
// decide who does: capture walks whatever the caller enumerates, and restore
// builds into whatever the caller provides. The port has no scene tree and no
// entity registry, and the slice that will decide where entities live - the one
// that ports `main.gd` into a real match - has information this one does not.
// A save that made itself the owner would be deciding that question by fiat.
//
// The cost of that choice is stated rather than hidden: a caller that leaves an
// entity out of the Scene produces a save in which every reference to it is -1,
// and a capture-restore-capture comparison cannot see it, because the entity is
// missing from both sides. Capture therefore COUNTS unresolved references and
// reports them, which is the only way an under-enumerated scene is visible at
// all.
namespace Snapshot {

// This port's schema. Exact equality: a newer or an older file is refused
// rather than half-read.
//
// NOT interchangeable with Godot's `user://wolf_brigade_run.json`, and no test
// should imply it is. Three independent reasons: the original writes a colour
// as eight hex digits with no leading '#' where this stores "#3f6b34"; a unit's
// position is `global_position` there and a plain position here; and ids are
// minted in the caller's enumeration order rather than in scene-tree child
// order.
inline constexpr int kVersion = 1;

// What the caller says the world currently contains.
//
// Raw pointers because the caller owns them. The one requirement this cannot
// check and therefore states: the containers behind these must keep their
// elements at stable addresses for the life of the capture. A
// `vector<unique_ptr<Unit>>` is fine; a `vector<Unit>` by value invalidates
// every pointer already in the id table the next time it grows.
struct Scene {
    std::vector<Unit*> units;
    std::vector<Building*> buildings;
    std::vector<ResourceNode*> resourceNodes;

    // In the level's order. Only their tug state is saved: the points
    // themselves are level furniture, rebuilt from the level's data on every
    // boot, and a save is index-aligned to that list.
    std::vector<CapturePoint*> capturePoints;
};

// What a capture noticed on the way past.
struct CaptureReport {
    // Entities skipped because they were dead or exhausted, matching the
    // original: a corpse mid-fade and an emptied tree are not worth restoring.
    int skippedDead{0};

    // References that pointed at something not in the Scene. Nonzero means
    // either a genuinely dead referent - fine - or an under-enumerated Scene,
    // which is a silently wrong save. The caller has to be able to tell.
    int unresolvedReferences{0};
};

// Walks the live match into a versioned document.
Supersonic::Json::Value Capture(const Scene& scene, const GameState& state,
                                const WaveDirector& director, CaptureReport* report = nullptr);

// Present, an object, and this build's schema.
bool IsValid(const Supersonic::Json::Value& snapshot);

// Where a restore puts what it builds.
//
// The caller creates the entities, because the caller owns them - and the sink
// shape is deliberately the shape a real Match will have, so that when one
// lands it can implement this by inheriting rather than by being reshaped
// around it.
class RestoreSink {
public:
    virtual ~RestoreSink() = default;

    // Called in dependency order: buildings, then resource nodes, then units.
    // Each returns the created entity, or null to decline - which the report
    // counts rather than treating as an error.
    virtual Building* CreateBuilding(const BuildingStats& stats, bool complete,
                                     const glm::vec2& position) = 0;
    virtual ResourceNode* CreateResourceNode(const ResourceNode& fromSave) = 0;
    virtual Unit* CreateUnit(const UnitStats& stats, const glm::vec2& position) = 0;

    // The capture points already on the board, in the level's order. NOT
    // created by the restore: the boot spawns them from the level's data on
    // both paths, as the original's main does, and a restore only puts their
    // tug state back. Empty by default, so a sink with no level furniture
    // restores the way it always has.
    virtual std::vector<CapturePoint*> CapturePointsToRestore() { return {}; }
};

struct RestoreReport {
    // Entities whose id is not in the current data files. SKIPPED, never
    // fabricated: `UnitStats::FromJson` on a missing row returns every default -
    // a player-faction worker - so a saved raider would come back on the wrong
    // side, counted by nobody, and hand the player a victory. A building is
    // worse: a missing row is maxHp 1, and the hp clamp then puts a saved Town
    // Hall back at one hit point.
    //
    // The schema version does not cover this. It guards the SHAPE of the file,
    // and nobody bumps it when only units.json is re-tuned.
    int unknownIds{0};

    // References in the file that resolved to nothing.
    int unresolvedReferences{0};

    int units{0};
    int buildings{0};
    int resourceNodes{0};
};

// Rebuilds a match from a document.
//
// Synchronous and single-pass-per-stage: everything is created, and only then
// are references relinked. The original needs a comment warning that no frame
// may tick in between, because a thinking tick would clear the targets it is
// about to restore. Here there is no frame to interpose - the guarantee is
// structural rather than defended, which is stronger and is also why it cannot
// be tested directly. What is tested is the outcome: targets correct at zero
// steps.
//
// Clears the lane index and the projectile pool before building anything.
// Neither is saved and both are derived, and a restore into a warm process
// would otherwise leave the previous run's corpses in the lane - the exact bug
// the original's `Lane.clear_all()` exists to prevent.
bool Restore(const Supersonic::Json::Value& snapshot, const GameData& data, GameState& state,
             const Profile& profile, WaveDirector& director, RestoreSink& sink, Lane& lane,
             ProjectilePool& projectiles, RestoreReport* report = nullptr);

// --- The document, as text -------------------------------------------------

// Serialises with %.17g, which is what makes a double survive the trip.
//
// The default six significant digits would truncate an attack cooldown - and
// the original's own harness would not notice, because it compares cooldowns
// with a tolerance of 0.001 and elapsed time with 0.5. Copying those tolerances
// is what hides it. This port's entire numeric argument is that a cooldown of
// 1.0 decremented by 0.1 lands on a different side of zero in float than in
// double; a save that rounds it reintroduces exactly that class of error.
//
// A non-finite number is written as 0 rather than as `inf`, which is not JSON.
std::string ToText(const Supersonic::Json::Value& document);

// Parses. Returns a null value on anything unparseable, which IsValid then
// refuses - a truncated file is not a match with some parts missing.
Supersonic::Json::Value FromText(const std::string& text);

// --- The run file ----------------------------------------------------------
//
// The mid-match "Continue" save, which has its own lifecycle: written when the
// player leaves a live match, cleared when one ends. The persistent profile is
// a different file with a different lifetime and lives in Progression.hpp.

bool SaveRun(const std::string& path, const Supersonic::Json::Value& snapshot);

// The document on disk, or null. Deliberately does NOT validate: HasRun and
// IsValid are separate questions, and the original keeps them separate so a
// corrupt file still counts as "there is a run here" for the menu.
Supersonic::Json::Value LoadRun(const std::string& path);

bool HasRun(const std::string& path);
void ClearRun(const std::string& path);

} // namespace Snapshot

} // namespace WolfBrigade
