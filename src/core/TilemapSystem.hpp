#pragma once

#include <cstdint>
#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/MeshData.hpp"
#include "core/Raycast.hpp"

namespace Supersonic {

// Turns a TilemapComponent into the one mesh that draws it, and keeps that
// mesh current.
//
// Three halves, and the split is the point. `Bake` is the geometry - given a
// map, what are the vertices - and it is pure, because a tile's corner off by
// one cell or its texture coordinates off by one row is where a tilemap goes
// wrong, and it goes wrong legibly: the map draws, with the wrong tiles in it.
// `CellFromRay` is the inverse, for a brush. `Sync` is the bookkeeping - which
// maps need uploading or replacing this frame, and which draw nothing - and it
// is a template over the thing that owns the buffers so a suite can hand it a
// fake that records what it was asked to do, since a MeshRegistry needs a
// device and no suite can build one.
class TilemapSystem {
public:
    // What one bake did, for the log and the inspector.
    struct BakeReport {
        uint32_t drawn{0};

        // Cells naming an atlas index the grid does not have. Not drawn and
        // not wrapped: a flipbook wraps because a frame past the end of a run
        // is a loop, but a map cell past the end of an atlas is an authoring
        // error, and drawing the wrong tile in its place would hide it behind a
        // picture that looks finished.
        uint32_t outOfRange{0};
    };

    // The local-space rectangle of one cell.
    //
    // The map hangs from its origin: cell (0, 0) has its TOP-LEFT corner at
    // the origin and the map extends along +x and DOWN along -y, one unit per
    // cell. That is what every 2D tool means by a map - row zero at the top,
    // the same way the atlas is counted - laid into a world where y is up. The
    // entity's transform places and scales the whole map.
    static void CellRect(uint32_t column, uint32_t row, glm::vec2& outMin, glm::vec2& outMax);

    // Which cell a local-space point is in. False outside the map. The top
    // and left edges belong to the cell they border; the bottom and right
    // edges do not, so no point is in two cells.
    static bool CellFromLocal(const TilemapComponent& map, const glm::vec2& local,
                              uint32_t& outColumn, uint32_t& outRow);

    // Which cell a world-space ray hits. False when the ray is parallel to
    // the map's plane, hits it behind the ray's origin, or lands outside the
    // map. `world` is the entity's world matrix; the ray is inverted into the
    // map's space rather than the map transformed into the ray's, so a
    // rotated or non-uniformly scaled map answers exactly.
    static bool CellFromRay(const TilemapComponent& map, const glm::mat4& world, const Ray& ray,
                            uint32_t& outColumn, uint32_t& outRow);

    // One quad per occupied cell, atlas cell in the vertices, white vertex
    // colour, +Z normal, wound counter-clockwise seen from +Z - the same quad
    // ModelLoader::GenerateQuad makes, so a map faces the way a sprite does.
    //
    // Returns false when there is nothing to draw: every cell empty, an atlas
    // with no cells, or a map over the cap. `out` is cleared either way, and
    // a false here must never reach an upload - MeshRegistry::Upload answers
    // an empty mesh with the fallback cube, which is not what an empty map
    // looks like.
    static bool Bake(const TilemapComponent& map, MeshData& out, BakeReport* report = nullptr);

    // What the mesh depends on: the atlas grid, the map size and the cells.
    // Every frame compares this against what was last baked, and that is the
    // whole of change detection - there is no flag to forget to set, at the
    // cost of a walk over the cells each frame. Linear in the map, and a map
    // at the cap is a quarter of a megabyte, which is well under what the
    // transform pass already touches. A version counter is the response if
    // this ever shows in the profiler; it has not.
    //
    // Never zero, because zero is what "never baked" means on the component.
    static uint64_t ContentHash(const TilemapComponent& map);

    // The MeshRegistry key for an entity's map. Keyed by ENTITY INDEX rather
    // than by content, so an edit replaces the mesh behind one id rather than
    // uploading a new one per brush stroke - and so a scene reloaded into the
    // same slots finds its meshes already there and replaces rather than
    // grows. The cost of that choice is a mesh left behind when a map's
    // entity is destroyed; it sits in its slot until the next map to land on
    // that index reuses it, and the registry recycles indices, so the leak is
    // bounded by the most maps a scene ever held at once.
    static std::string MeshKey(entt::entity entity);

    // Brings every map's mesh up to date, before the renderer resolves what
    // each entity draws.
    //
    // `Meshes` needs three things, and MeshRegistry has all of them: Find(key)
    // -> id or kInvalidMesh, Upload(key, data) -> id, and Replace(id, data).
    // The decisions are: a map whose hash matches its last bake is handed its
    // mesh and costs nothing else; one that has changed is baked and either
    // uploaded (first sight) or replaced (already there); and one that bakes
    // to nothing is handed kInvalidMesh, which the draw loop skips - the
    // fallback cube is what an entity with no opinion gets, and an empty map
    // has one. Its mesh, if it had one, stays where it is: the registry never
    // recycles an id, so dropping the mesh and uploading again on the next
    // stroke would grow it by one dead slot per empty-and-refill, and a game
    // that clears a map on a tick would do that for ever.
    template <class Meshes>
    static void Sync(entt::registry& registry, Meshes& meshes) {
        auto view = registry.view<TilemapComponent, RenderableComponent>();
        for (auto entity : view) {
            auto& map = view.get<TilemapComponent>(entity);
            auto& renderable = view.get<RenderableComponent>(entity);

            const uint64_t hash = ContentHash(map);
            if (hash == map.bakedHash) {
                // Written every frame, not only when it changes. The
                // renderable is the thing the draw loop reads, and it can be
                // removed and re-added in the inspector, at which point it
                // holds the cube again and the map still hashes as baked.
                renderable.meshID = map.meshID;
                continue;
            }

            const std::string key = MeshKey(entity);
            const uint32_t existing = meshes.Find(key);

            MeshData data;
            if (!Bake(map, data)) {
                map.meshID = TilemapComponent::kNoMesh;

                // Nothing refreshes these while the id is invalid - the
                // resource sync copies bounds off the mesh it resolves, and
                // there is none - so an emptied map would keep the box of its
                // last bake and stay pickable across ground it no longer
                // draws. Back to what a fresh renderable holds.
                const RenderableComponent fresh;
                renderable.localBoundsMin = fresh.localBoundsMin;
                renderable.localBoundsMax = fresh.localBoundsMax;
            } else if (existing == TilemapComponent::kNoMesh) {
                map.meshID = meshes.Upload(key, data);
            } else {
                // Replace bumps the registry's generation, so every entity
                // re-resolves its ids next frame and the shadow cache rebuilds.
                // Right - the map's silhouette changed - and paid only on the
                // frames a map actually changes, which outside the editor's
                // brush is close to never.
                meshes.Replace(existing, data);
                map.meshID = existing;
            }

            renderable.meshID = map.meshID;
            map.bakedHash = hash;
        }
    }
};

} // namespace Supersonic
