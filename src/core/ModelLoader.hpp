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
    static constexpr float kPlaneWidth = 1.0f;
    static constexpr float kPlaneHeight = 1.0f;

    static bool GenerateSphere(float radius, uint32_t rings, uint32_t sectors, MeshData& out);
    static bool GenerateCube(float size, MeshData& out);
    static bool GeneratePlane(float width, float height, MeshData& out);

    // Parses positions, normals, UVs and faces. Triangulates n-gons as a fan.
    static bool LoadOBJ(const std::string& filepath, MeshData& out);
};

} // namespace Supersonic
