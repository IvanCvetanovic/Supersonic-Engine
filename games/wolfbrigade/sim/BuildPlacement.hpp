#pragma once

#include <string>

#include "sim/Building.hpp"

namespace WolfBrigade {

class Match;
class Unit;

// Placing a building, from `scripts/systems/build_placement.gd`.
//
// The original drives a translucent ghost that follows the pointer along the
// ground line, green where the footprint is clear and red where it is not.
// Everything with a pixel in it is gone here; what survives is the part with
// decisions in it - where the ghost would actually go, whether that spot is
// allowed, what confirming costs, and who gets sent to build it.
//
// So there is no ghost. There is a CANDIDATE X and a validity flag, which is
// exactly what the ghost was drawing.
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

    // Enters placement mode for a building id.
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

    // Moves the candidate. Only x is taken - this is a single-lane game and the
    // ground line is not the pointer's to choose.
    //
    // CLAMPED TO THE FOOTPRINT, not to the world: half a body in from each
    // edge, so the building stays wholly on the map. That is a different clamp
    // from the one a TRAINED unit gets, which is to the bare world width - a
    // unit is a point on the lane and a building is not.
    void Update(float worldX);

    // Places it, if it can. Spends the cost, creates the site under
    // construction, and sends the nearest worker to it.
    void Confirm(float worldX);

    // Leaves placement mode, building nothing and spending nothing. Idempotent:
    // cancelling when nothing is active does not announce a change.
    void Cancel();

    // --- What the ghost was drawing ---------------------------------------

    float CandidateX() const { return m_candidateX; }
    bool IsValidHere() const { return m_valid; }

    // Whether a footprint centred here clears every standing player building.
    //
    // AN X-ONLY OVERLAP, deliberately, and not `Building::Rect::Overlaps`. The
    // original calls itself a "single-lane x check" and it is right to: two
    // buildings sharing a stretch of lane collide whatever their heights, and a
    // rectangle test would call a short building under a tall one's overhang
    // placeable. The two agree on every board the game can build, because
    // everything sits on the same ground line; they differ only on a fixture
    // that puts a building somewhere the game cannot.
    //
    // Strict inequalities, so edge-to-edge is buildable - the same boundary
    // `Building::Rect::Overlaps` already uses and `test_wb_buildings` already
    // pins.
    //
    // DEAD BUILDINGS DO NOT BLOCK. In Godot a destroyed building leaves its
    // group when it is freed, so the question never reaches it; nothing is
    // freed here, so it has to be asked directly. Without it the rubble of a
    // fallen barracks would reserve that stretch of lane for the rest of the
    // run.
    bool IsValidAt(float x) const;

    const BuildingStats& Stats() const { return m_stats; }

private:
    // The nearest worker, PREFERRING IDLE ONES.
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

    void Place(float x);

    Match* m_match{nullptr};

    bool m_active{false};
    BuildingStats m_stats;

    // Where the ghost is. Meaningless while inactive, and deliberately not
    // reset by Cancel - the original leaves its own last position behind too,
    // and a test that asserted otherwise would be pinning something nobody
    // decided.
    float m_candidateX{0.0f};
    bool m_valid{false};
};

} // namespace WolfBrigade
