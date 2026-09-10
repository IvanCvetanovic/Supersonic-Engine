#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace WolfBrigade {

class Building;
class Damageable;
class ProjectilePool;
class Unit;
struct ResourceNode;

// Everything a unit can see that it does not own.
//
// The Godot version asks the scene tree for a group - `get_nodes_in_group`
// against the resource nodes, the deposit points, the enemy buildings - and
// there is no scene tree here. So the thing that owns the world answers
// instead. It is the same question, asked of something a test can build.
//
// It is also where the original's rule 7 lives: these are SCANS, and a unit is
// only allowed to run one on its ~8 Hz thinking tick, never per frame. Nothing
// here enforces that; the unit does, and the reason it matters is that a
// hundred units scanning a hundred nodes every frame is ten thousand distance
// checks sixty times a second.
class World {
public:
    virtual ~World() = default;

    // The nearest node with anything left in it, measured along the lane.
    // Null when everything is exhausted, which is a real state late in a run.
    virtual ResourceNode* NearestHarvestable(float x) const = 0;

    // Deposit points are addressed by INDEX rather than by pointer.
    //
    // A worker caches the one it is walking to and keeps it across cycles,
    // because it does not move - but the Town Hall CAN fall while a worker is
    // on its way to it, and a cached pointer to a building that no longer
    // exists is the one failure this port cannot afford. An index the world
    // can invalidate is the same question Godot's is_instance_valid answers,
    // asked in a way that cannot dangle.
    virtual int NearestDeposit(float x) const = 0;   // -1 when there are none
    virtual bool DepositExists(int index) const = 0;
    virtual glm::vec2 DepositPosition(int index) const = 0;

    // The nearest building of this unit's OWN faction that is still being
    // built, at any distance. Null when everything is finished.
    //
    // No range, deliberately, and this is the anti-deadlock rule: ANY idle
    // worker walks to ANY unfinished site. The builder that was assigned to one
    // may have fled, died or been re-tasked, and without this the half-built
    // barracks it left behind stands there for the rest of the run with the
    // player unable to see why.
    //
    // Asked only while `economy.auto_assist_build` is on, which is how it
    // ships. Off, a stalled site waits for an explicit build order.
    virtual Building* NearestUnfinishedBuilding(const std::string& faction,
                                                float x) const = 0;

    // --- Combat -----------------------------------------------------------

    // The nearest living unit of the OTHER faction within range, from the lane
    // index. Null when nothing is in aggro, which is most of the time.
    virtual Unit* NearestEnemyUnit(const std::string& faction, float x,
                                   float maxRange) const = 0;

    // The nearest standing building of the other faction, at any distance.
    //
    // No range, deliberately: a raider with nothing in aggro walks toward the
    // Town Hall from the far end of a six-thousand-pixel world, and that walk
    // IS the game. Null once every enemy building has fallen, at which point a
    // raider heads for the left edge instead.
    virtual Damageable* NearestEnemyBuilding(const std::string& faction, float x) const = 0;

    // The nearest OTHER living unit of `me`'s faction that is missing hit
    // points, within range along the lane - a priest's target scan, off the
    // lane index. Null when nobody nearby is hurt.
    virtual Unit* NearestWoundedAlly(const Unit* me, float maxRange) const = 0;

    // Every LIVING unit of the other faction within range along the lane, in
    // registration order - what the hero's Cleave hits. Inclusive at the edge,
    // as NearestEnemyUnit is.
    virtual std::vector<Unit*> EnemiesWithin(const std::string& faction, float x,
                                             float maxRange) const = 0;

    // Where arrows come from. Null in a world with no ranged units in it,
    // which is a legitimate configuration and not an error - an archer that
    // cannot find a pool simply does not shoot, exactly as the original
    // returns null from _acquire when nothing has set a parent.
    virtual ProjectilePool* Projectiles() = 0;

    // --- Counting -----------------------------------------------------------

    // The player's own buildings and units, standing or not - the original's
    // two faction groups. Supply counts off them, and a finished research
    // raises every standing building through them. Asked on clicks and on the
    // slow auto-train tick, never per frame.
    virtual std::vector<Building*> PlayerBuildings() const = 0;
    virtual std::vector<Unit*> PlayerUnits() const = 0;

    // --- The hero -----------------------------------------------------------

    // The hero the warband follows, or null when there is none - the
    // original's `HeroControl.hero`. That is a static on an autoload, so one
    // harness's hero is still the next one's unless it remembers to null it;
    // here it belongs to the world, for the reason the army bonus belongs to
    // the run. A fallen hero may still be returned: a follower asks IsAlive
    // itself, as the original does.
    virtual Unit* Hero() const = 0;
};

} // namespace WolfBrigade
