#pragma once

#include <glm/glm.hpp>

namespace WolfBrigade {

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
};

} // namespace WolfBrigade
