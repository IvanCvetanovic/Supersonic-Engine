#pragma once

#include <string>

#include "core/MeshData.hpp"

namespace Supersonic {

class ModelLoader {
public:
    // All generators validate their parameters and return false rather than
    // producing a degenerate mesh or looping on unsigned underflow.
    // What each named primitive IS, in one place.
    //
    // These were written out at the call site, and there is now more than one
    // call site: MeshRegistry builds the mesh you see and ConvexHullCache
    // builds the collider you hit. A collider that agrees with its mesh by
    // coincidence stops agreeing the first time either number is tuned, and the
    // symptom - a shape you can see through or stand on the air beside - gets
    // blamed on the narrowphase.
    static constexpr float kCubeSize = 1.0f;
    static constexpr float kSphereRadius = 0.5f;
    static constexpr uint32_t kSphereRings = 32;
    static constexpr uint32_t kSphereSectors = 32;
    // The quad is one unit square, so a transform scale reads directly as a
    // size in world units - which is what a 2D game wants: a 30x40 pixel body
    // becomes scale (0.30, 0.40, 1).
    static constexpr float kQuadWidth = 1.0f;
    static constexpr float kQuadHeight = 1.0f;

    static constexpr float kPlaneWidth = 1.0f;
    static constexpr float kPlaneHeight = 1.0f;

    static bool GenerateSphere(float radius, uint32_t rings, uint32_t sectors, MeshData& out);
    static bool GenerateCube(float size, MeshData& out);
    static bool GeneratePlane(float width, float height, MeshData& out);

    // A flat rectangle in the XY plane facing +Z, for 2D.
    //
    // Not GeneratePlane, which lies in XZ - that is a floor, and a floor seen
    // by a side-on camera is a line. This is a sprite: it faces the viewer.
    //
    // Its vertex colours are WHITE, and that is the entire reason it exists
    // rather than a flattened cube being good enough. GenerateCube gives every
    // corner a different colour on purpose - it is the engine's debug
    // primitive - and the shader multiplies albedo by that, so a "flat colour"
    // quad built from a cube comes out as a rainbow gradient. It took a
    // screenshot to notice, which is precisely the kind of thing a screenshot
    // is for.
    //
    // Centred on its origin, with no pivot parameter. A pivot would mean a
    // different mesh per pivot, and the mesh cache is keyed by primitive name -
    // so two pivots would silently share one mesh. Placing by the feet is the
    // caller offsetting by half the height, which is one line where it is
    // needed and no cache hazard anywhere.
    static bool GenerateQuad(float width, float height, MeshData& out);

    // Parses positions, normals, UVs and faces. Triangulates n-gons as a fan.
    static bool LoadOBJ(const std::string& filepath, MeshData& out);
};

} // namespace Supersonic
