#pragma once

#include "core/MeshData.hpp"

namespace Supersonic {

class Heightfield;

class TerrainGenerator {
public:
    // What the "Terrain" mesh primitive IS, in one place.
    //
    // MeshRegistry generated a 64 x 64 grid at 0.6 and
    // HeightfieldColliderComponent defaulted to the same three numbers written
    // out again. A collider that agrees with the mesh by coincidence stops
    // agreeing the first time either is tuned, and the symptom - a character
    // walking half a unit above the ground, or sunk into it - gets blamed on
    // the solver.
    static constexpr uint32_t kPrimitiveWidth = 64;
    static constexpr uint32_t kPrimitiveDepth = 64;
    static constexpr float kPrimitiveHeightScale = 0.6f;

    // width/height are vertex counts per axis, so both must be at least 2 to
    // produce a single quad. Returns false rather than looping on underflow.
    static bool GenerateTerrainMesh(uint32_t width, uint32_t height, float heightScale, MeshData& out);

    // THE surface, as one expression.
    //
    // x and z are LOCAL coordinates - already centred, the way the mesh stores
    // them - so a caller that has a world position must take it into the
    // terrain's own frame first.
    //
    // Extracted because the collider has to agree with what you can see, and
    // the only way to guarantee that is for both to ask the same function
    // rather than each carrying its own copy of a sine. A second copy would
    // start correct and drift the first time anyone tuned the frequency, and
    // the symptom - a character standing slightly above or inside the ground -
    // is exactly the kind that gets blamed on the solver.
    static float SampleHeight(float x, float z, float heightScale);

    // The same surface as a collision grid. Same width/height/heightScale
    // arguments as GenerateTerrainMesh, and therefore the same vertices, to the
    // last bit: both go through SampleHeight.
    //
    // `thickness` is how far the solid extends BELOW the surface. It is not
    // cosmetic: a body that has ended up under the terrain has to be pushed out
    // of the top, and without a bottom there is no way to say when it has left.
    // The most a collision grid is allowed to be: per side, and in cells.
    //
    // The size comes out of a scene file (TerrainColliderComponent's width and
    // depth) or a typed inspector field, so it is somebody's claim and not a fact.
    // Taken at its word, 65536 x 65536 asked for 17 GB of heights before a single
    // one was computed. 8192 x 8192 - the cell limit - is 134 million triangles
    // of ground, far past anything a game here draws or collides with.
    static constexpr uint32_t kMaxHeightfieldSide = 32768;
    static constexpr uint64_t kMaxHeightfieldCells = 1ull << 26;

    static bool GenerateHeightfield(uint32_t width, uint32_t height, float heightScale,
                                    float thickness, Heightfield& out);
};

} // namespace Supersonic
