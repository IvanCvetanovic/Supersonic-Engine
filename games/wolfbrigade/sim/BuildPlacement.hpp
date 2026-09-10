#pragma once

#include <string>

#include "sim/Building.hpp"

namespace WolfBrigade {

class Match;
class Unit;

// Placing a building, from `scripts/systems/build_placement.gd`.
//
// The original drives a translucent ghost that follows the pointer anywhere in
// the walkable band, green where the footprint is clear and red where it is
// not. Everything with a pixel in it is gone here; what survives is the part
// with decisions in it - where the ghost would actually go, whether that spot
// is allowed, what confirming costs, and who gets sent to build it.
//
// So there is no ghost. There is a CANDIDATE POINT and a validity flag, which
// is exactly what the ghost was drawing.
//
// THE ORDER OF OPERATIONS IN `Confirm` IS THE WHOLE SLICE, and it is easy to
// get subtly wrong in three separate ways. It re-runs the clamp before spending
// (so the price is paid for the spot the building actually lands on, not the
// one the pointer was over); it returns WITHOUT cancelling when the spot is
// invalid (so a player who taps a wall can slide over and try again rather than
// having to re-open the menu); and it cancels WITHOUT building when the cost
// has become unaffordable since placement began. Each of those is one line and
// each is a different bug if it moves.
class BuildPlacement {
public:
    // Holds the Match rather than a container, because placement asks it three
    // separate questions - what buildings are standing, who can be sent to
    // build, and can the player afford this - and a narrower seam would be
    // three seams.
    explicit BuildPlacement(Match& match);

    bool IsActive() const { return m_active; }

    // Enters placement mode for a building id, with the ghost at the middle of
    // the world on the band's top row.
    //
    // Beginning while already placing CANCELS first, which is what makes
    // tapping a second building in the menu a swap rather than a no-op - and
    // means the active-changed signal goes false then true rather than
    // staying true.
    //
    // An id nobody authored is a NO-OP: it does not enter placement at all.
    // The original checks the data file for it, and the reason is that a stale
    // button in a UI would otherwise put the player into a placement mode for
    // a building that can never be built, with no way out but the cancel they
    // do not know they need.
    void Begin(const std::string& buildingId);

    // Moves the candidate, on both axes.
    //
    // x is CLAMPED TO THE FOOTPRINT, not to the world: half a body in from each
    // edge, so the building stays wholly on the map. That is a different clamp
    // from the one a TRAINED unit gets, which is to the bare world width - a
    // unit is a point on the lane and a building is not.
    //
    // y is clamped onto the walkable band, the rule a move order follows, so a
    // pointer over the sky still places on the back row. Buildings stand on any
    // row of the band, not only on the ground line (the game's 4b67dd2).
    void Update(const glm::vec2& worldPos);

    // Places it, if it can. Spends the cost, creates the site under
    // construction, and sends the nearest worker to it.
    void Confirm(const glm::vec2& worldPos);

    // Leaves placement mode, building nothing and spending nothing. Idempotent:
    // cancelling when nothing is active does not announce a change.
    void Cancel();

    // --- What the ghost was drawing ---------------------------------------

    glm::vec2 Candidate() const { return m_candidate; }
    float CandidateX() const { return m_candidate.x; }
    bool IsValidHere() const { return m_valid; }

    // Whether a footprint with its base centred here clears every standing
    // player building.
    //
    // Blocked by a building that overlaps it in x AND stands on a nearby row,
    // with base rows closer than `lane.building_row_gap`. Buildings on rows
    // further apart may share a stretch of lane - a barracks in front of the
    // Town Hall reads fine - and closer than that they stack into mush.
    //
    // The overlap is an x-span test, not `Building::Rect::Overlaps`, as in the
    // original: the row gap is what decides depth, and a rectangle test would
    // let a short building sit under a tall one's overhang.
    //
    // A PURE PREDICATE: no clamp inside. The clamp belongs to Update, where the
    // original has it. Its harness probes y = 950, below the band, and expects
    // the answer for 950; a clamp here would ask about 870 instead and flip it.
    //
    // Strict inequalities, so edge-to-edge is buildable and exactly the gap
    // apart is clear - the same boundaries the original's `>` and `<` draw.
    //
    // DEAD BUILDINGS DO NOT BLOCK. In Godot a destroyed building leaves its
    // group when it is freed, so the question never reaches it; nothing is
    // freed here, so it has to be asked directly. Without it the rubble of a
    // fallen barracks would reserve that stretch of lane for the rest of the
    // run.
    bool IsValidAt(float x, float y) const;

    const BuildingStats& Stats() const { return m_stats; }

private:
    // The nearest worker, PREFERRING IDLE ONES, measured along the lane.
    //
    // Two passes rather than one sort: the nearest idle worker if there is one,
    // otherwise the nearest worker of any kind. The preference is the point -
    // pulling a loaded worker off a delivery to walk across the map is a worse
    // answer than waking one that is standing still, even when the idle one is
    // further away.
    //
    // Null on a board with no workers, which is a real state late in a bad run
    // and not an error: the site simply stands unbuilt until somebody is free.
    Unit* NearestWorker(float x) const;

    void Place(const glm::vec2& at);

    Match* m_match{nullptr};

    bool m_active{false};
    BuildingStats m_stats;

    // Where the ghost is. Meaningless while inactive, and deliberately not
    // reset by Cancel - the original leaves its own last position behind too,
    // and a test that asserted otherwise would be pinning something nobody
    // decided.
    glm::vec2 m_candidate{0.0f};
    bool m_valid{false};
};

} // namespace WolfBrigade
