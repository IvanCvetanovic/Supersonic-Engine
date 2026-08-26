#pragma once

#include <map>
#include <string>
#include <vector>

namespace WolfBrigade {

class Unit;

// Who is standing where, per faction, from `scripts/systems/lane.gd`.
//
// The game is a single lane, so "the nearest enemy" is a question about one
// coordinate - and answering it by scanning the scene tree every frame is the
// thing the original's seventh rule exists to forbid. Units register when they
// spawn and unregister when they die, so the lists change on those events and
// never per frame.
//
// A linear scan over the cached list, deliberately: at this game's unit counts
// it is faster than anything with a structure, and the original says what to do
// if that ever stops being true - keep the list sorted by x and binary-search
// it.
//
// An OBJECT rather than the original's static registry. Godot's version needs a
// clear_all() on restart precisely because it is global, and forgetting it
// leaves the next run querying the last one's corpses.
class Lane {
public:
    void Register(Unit* unit);
    void Unregister(Unit* unit);
    void Clear() { m_lists.clear(); }

    // The nearest LIVING unit of the opposing faction within range, or null.
    //
    // Range is inclusive, matching the original's `<=`: a unit exactly at the
    // edge of its aggro is in it, and a port that used `<` would make every
    // engagement start a fraction of a pixel later.
    Unit* NearestEnemy(const std::string& faction, float x, float maxRange) const;

    int CountOf(const std::string& faction) const;

private:
    const std::vector<Unit*>& ListOf(const std::string& faction) const;

    std::map<std::string, std::vector<Unit*>> m_lists;
};

} // namespace WolfBrigade
