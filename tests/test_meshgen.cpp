// Regression tests for the mesh generators and the OBJ loader.
//
// These were all dead code with zero call sites, so their defects had never
// been able to fire. They are wired into MeshRegistry now, which makes the
// unsigned underflows and the uint16 index overflow reachable.

#include "TestHarness.hpp"
#include "core/ModelLoader.hpp"
#include "core/TerrainGenerator.hpp"
#include "core/JobSystem.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace Supersonic;

static bool indicesInRange(const MeshData& mesh) {
    for (const uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) return false;
    }
    return true;
}

static void testCube() {
    MeshData mesh;
    CHECK(ModelLoader::GenerateCube(2.0f, mesh));
    CHECK_EQ(mesh.vertices.size(), size_t{24});
    CHECK_EQ(mesh.indices.size(), size_t{36});
    CHECK(indicesInRange(mesh));
    CHECK_NEAR(mesh.boundsMin.x, -1.0f);
    CHECK_NEAR(mesh.boundsMax.y, 1.0f);

    MeshData bad;
    CHECK_MSG(!ModelLoader::GenerateCube(0.0f, bad), "zero size must be rejected");
}

static void testSphereRejectsDegenerateParameters() {
    MeshData mesh;

    // rings - 1 and sectors - 1 are computed on unsigned types. Zero wrapped to
    // ~4.29 billion loop iterations; rings == 1 divided by zero and produced
    // NaN positions.
    CHECK_MSG(!ModelLoader::GenerateSphere(1.0f, 0, 0, mesh), "rings/sectors of 0 must be rejected");
    CHECK_MSG(!ModelLoader::GenerateSphere(1.0f, 1, 8, mesh), "rings of 1 must be rejected");
    CHECK_MSG(!ModelLoader::GenerateSphere(1.0f, 8, 2, mesh), "sectors below 3 must be rejected");
    CHECK_MSG(!ModelLoader::GenerateSphere(0.0f, 8, 8, mesh), "zero radius must be rejected");

    CHECK(ModelLoader::GenerateSphere(0.5f, 16, 16, mesh));
    CHECK(indicesInRange(mesh));
    CHECK_MSG(mesh.indices.size() % 3 == 0, "index count must be a whole number of triangles");
}

// Geometric normal of a triangle, which must agree with the stored vertex
// normal or back-face culling discards the surface from the side you can see.
static glm::vec3 faceNormal(const MeshData& mesh, size_t tri) {
    const glm::vec3 a = mesh.vertices[mesh.indices[tri * 3 + 0]].pos;
    const glm::vec3 b = mesh.vertices[mesh.indices[tri * 3 + 1]].pos;
    const glm::vec3 c = mesh.vertices[mesh.indices[tri * 3 + 2]].pos;
    return glm::normalize(glm::cross(b - a, c - a));
}

static void testPlane() {
    MeshData mesh;
    CHECK(ModelLoader::GeneratePlane(4.0f, 6.0f, mesh));
    CHECK_EQ(mesh.vertices.size(), size_t{4});
    CHECK_EQ(mesh.indices.size(), size_t{6});
    CHECK_NEAR(mesh.boundsMin.x, -2.0f);
    CHECK_NEAR(mesh.boundsMax.z, 3.0f);

    MeshData bad;
    CHECK(!ModelLoader::GeneratePlane(0.0f, 1.0f, bad));
}

static void testPlaneFacesUpward() {
    // The plane shipped wound the wrong way: its geometric normal pointed -Y
    // while every vertex normal said +Y, so a ground plane was culled when
    // viewed from above. It was invisible for as long as RenderSystem drew a
    // cube for every entity regardless of its mesh.
    MeshData mesh;
    CHECK(ModelLoader::GeneratePlane(2.0f, 2.0f, mesh));

    for (size_t tri = 0; tri < mesh.indices.size() / 3; ++tri) {
        const glm::vec3 n = faceNormal(mesh, tri);
        CHECK_MSG(n.y > 0.9f, "plane triangles must wind so the face normal points +Y");
    }
    for (const auto& v : mesh.vertices) {
        CHECK_NEAR(v.normal.y, 1.0f);
    }
}

static void testTerrainFacesUpward() {
    MeshData mesh;
    CHECK(TerrainGenerator::GenerateTerrainMesh(4, 4, 0.0f, mesh)); // flat, so normals are exactly +Y

    for (size_t tri = 0; tri < mesh.indices.size() / 3; ++tri) {
        CHECK_MSG(faceNormal(mesh, tri).y > 0.9f, "terrain triangles must face upward");
    }
}

static void testTerrainRejectsDegenerateSize() {
    MeshData mesh;
    CHECK_MSG(!TerrainGenerator::GenerateTerrainMesh(0, 0, 1.0f, mesh), "zero extents must be rejected");
    CHECK_MSG(!TerrainGenerator::GenerateTerrainMesh(1, 8, 1.0f, mesh), "a single column cannot form a quad");

    CHECK(TerrainGenerator::GenerateTerrainMesh(8, 8, 1.0f, mesh));
    CHECK_EQ(mesh.vertices.size(), size_t{64});
    CHECK_EQ(mesh.indices.size(), size_t{7 * 7 * 6});
    CHECK(indicesInRange(mesh));
}

static void testTerrainSurvivesPast65kVertices() {
    // 300x300 = 90,000 vertices. With uint16 indices, index 65536 wrapped to 0
    // and stitched the tail of the mesh onto its own head, silently, while
    // reporting success.
    MeshData mesh;
    CHECK(TerrainGenerator::GenerateTerrainMesh(300, 300, 0.5f, mesh));
    CHECK_EQ(mesh.vertices.size(), size_t{90000});
    CHECK_MSG(indicesInRange(mesh), "indices must address all 90k vertices without wrapping");

    uint32_t maxIndex = 0;
    for (const uint32_t index : mesh.indices) maxIndex = std::max(maxIndex, index);
    CHECK_MSG(maxIndex > 65535u, "the mesh must actually reference vertices past the uint16 limit");
}

static void testObjParsesFaces() {
    const std::string path = "test_cube_tmp.obj";
    {
        std::ofstream f(path);
        f << "# a quad made of two triangles\n";
        f << "v 0.0 0.0 0.0\n";
        f << "v 1.0 0.0 0.0\n";
        f << "v 1.0 1.0 0.0\n";
        f << "v 0.0 1.0 0.0\n";
        f << "vt 0.0 0.0\n";
        f << "vn 0.0 0.0 1.0\n";
        f << "f 1//1 2//1 3//1\n";
        f << "f 1//1 3//1 4//1\n";
    }

    MeshData mesh;
    const bool ok = ModelLoader::LoadOBJ(path, mesh);
    std::remove(path.c_str());

    CHECK(ok);
    // Two triangles. The old loader ignored 'f' lines entirely and fabricated a
    // sequential index list over raw positions.
    CHECK_EQ(mesh.indices.size(), size_t{6});
    CHECK(indicesInRange(mesh));

    // "1//1" must bind the NORMAL index, not the texture index.
    for (const auto& v : mesh.vertices) {
        CHECK_NEAR(v.normal.z, 1.0f);
    }
}

static void testObjQuadIsTriangulated() {
    const std::string path = "test_quad_tmp.obj";
    {
        std::ofstream f(path);
        f << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
        f << "f 1 2 3 4\n";
    }

    MeshData mesh;
    const bool ok = ModelLoader::LoadOBJ(path, mesh);
    std::remove(path.c_str());

    CHECK(ok);
    CHECK_MSG(mesh.indices.size() == 6, "an n-gon face must be triangulated as a fan");
}

static void testObjWithoutFacesIsRejected() {
    const std::string path = "test_nofaces_tmp.obj";
    {
        std::ofstream f(path);
        f << "v 0 0 0\nv 1 0 0\nv 1 1 0\n";
    }

    MeshData mesh;
    const bool ok = ModelLoader::LoadOBJ(path, mesh);
    std::remove(path.c_str());

    CHECK_MSG(!ok, "a file with no topology must fail rather than invent triangles");
}

static void testObjTruncatedVertexIsSkipped() {
    // "v 1.0" leaves y and z indeterminate: a failed extraction does not zero
    // them, and the garbage flowed into a device-local vertex buffer.
    const std::string path = "test_trunc_tmp.obj";
    {
        std::ofstream f(path);
        f << "v 1.0\n";
        f << "v 0 0 0\nv 1 0 0\nv 1 1 0\n";
        f << "f 1 2 3\n";
    }

    MeshData mesh;
    const bool ok = ModelLoader::LoadOBJ(path, mesh);
    std::remove(path.c_str());

    CHECK(ok);
    for (const auto& v : mesh.vertices) {
        CHECK_MSG(std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z),
                  "no vertex may carry indeterminate coordinates");
    }
}

static void testObjMissingFileFails() {
    MeshData mesh;
    CHECK(!ModelLoader::LoadOBJ("definitely_not_here_12345.obj", mesh));
}

static void testTangentsAreValid() {
    // Normal mapping needs a tangent basis. Without one there is nothing to
    // rotate a tangent-space normal into world space with.
    MeshData mesh;
    CHECK(ModelLoader::GenerateCube(1.0f, mesh));

    for (const auto& v : mesh.vertices) {
        const glm::vec3 t(v.tangent);
        CHECK_MSG(std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.z),
                  "tangents must never be NaN");
        CHECK_NEAR(glm::length(t), 1.0f);
        // Orthogonal to the normal, or the mapped normal comes out skewed.
        CHECK_MSG(std::abs(glm::dot(t, v.normal)) < 1e-3f,
                  "tangent must be orthogonal to the vertex normal");
        CHECK_MSG(std::abs(v.tangent.w) == 1.0f, "handedness must be exactly +/-1");
    }
}

static void testTangentsSurviveDegenerateUVs() {
    // A face whose UVs have no area gives no usable direction. The generator
    // must fall back to an arbitrary perpendicular rather than divide by zero
    // and write NaN into a device-local vertex buffer.
    MeshData mesh;
    mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.5f, 0.5f}, {}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.5f, 0.5f}, {}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.5f, 0.5f}, {}},
    };
    mesh.indices = { 0, 1, 2 };
    mesh.computeTangents();

    for (const auto& v : mesh.vertices) {
        const glm::vec3 t(v.tangent);
        CHECK_MSG(std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.z),
                  "degenerate UVs must not produce NaN tangents");
        CHECK_NEAR(glm::length(t), 1.0f);
        CHECK_MSG(std::abs(glm::dot(t, v.normal)) < 1e-3f, "fallback tangent must still be perpendicular");
    }
}

static void testTerrainHasTangents() {
    MeshData mesh;
    CHECK(TerrainGenerator::GenerateTerrainMesh(8, 8, 0.5f, mesh));
    for (const auto& v : mesh.vertices) {
        CHECK_NEAR(glm::length(glm::vec3(v.tangent)), 1.0f);
    }
}

// Everything above runs with no worker pool, so every JobSystem::Dispatch takes
// the inline fallback. That is exactly the blind spot that let a missing fence
// ship: computeTangents' jobs capture the local accumulator vectors and `this`
// by reference, and without a Wait they outlived all three.
//
// Reaching the parallel path needs BOTH a running pool AND a mesh larger than
// the dispatch group size. 128x128 is 16,384 vertices - 8 groups of 2048 for the
// tangent pass and 4 of 4096 for terrain generation - so a dangling capture has
// real concurrency to go wrong in.
static void testLargeMeshIsIdenticalWithAndWithoutWorkers() {
    constexpr uint32_t kDim = 128;

    JobSystem::Shutdown();
    MeshData serial;
    CHECK(TerrainGenerator::GenerateTerrainMesh(kDim, kDim, 1.5f, serial));
    CHECK_MSG(serial.vertices.size() > 4096, "the grid must exceed one dispatch group");

    JobSystem::Initialize(4);
    CHECK(JobSystem::IsInitialized());

    MeshData parallel;
    CHECK(TerrainGenerator::GenerateTerrainMesh(kDim, kDim, 1.5f, parallel));

    CHECK_EQ(parallel.vertices.size(), serial.vertices.size());
    CHECK_EQ(parallel.indices.size(), serial.indices.size());

    size_t vertexMismatch = 0;
    for (size_t i = 0; i < serial.vertices.size(); ++i) {
        const Vertex& a = serial.vertices[i];
        const Vertex& b = parallel.vertices[i];
        if (a.pos != b.pos || a.normal != b.normal || a.texCoord != b.texCoord ||
            a.tangent != b.tangent) {
            ++vertexMismatch;
        }
    }
    CHECK_MSG(vertexMismatch == 0,
              "the parallel build must be bit-identical to the serial one (" +
                  std::to_string(vertexMismatch) + " differed)");

    size_t indexMismatch = 0;
    for (size_t i = 0; i < serial.indices.size(); ++i) {
        if (serial.indices[i] != parallel.indices[i]) ++indexMismatch;
    }
    CHECK_MSG(indexMismatch == 0, "and the winding order must not depend on completion order");

    // Independently of the comparison: every tangent must be finite and unit
    // length. A tangent written after `tan` was freed is neither.
    size_t badTangent = 0;
    for (const auto& v : parallel.vertices) {
        const float length = std::sqrt(v.tangent.x * v.tangent.x +
                                       v.tangent.y * v.tangent.y +
                                       v.tangent.z * v.tangent.z);
        if (!std::isfinite(length) || std::fabs(length - 1.0f) > 1e-3f) ++badTangent;
        if (v.tangent.w != 1.0f && v.tangent.w != -1.0f) ++badTangent;
    }
    CHECK_MSG(badTangent == 0, "every tangent must be finite, unit length and correctly signed");

    JobSystem::Shutdown();
}

static void runTests() {
    testCube();
    testSphereRejectsDegenerateParameters();
    testPlane();
    testPlaneFacesUpward();
    testTangentsAreValid();
    testTangentsSurviveDegenerateUVs();
    testTerrainHasTangents();
    testTerrainFacesUpward();
    testTerrainRejectsDegenerateSize();
    testTerrainSurvivesPast65kVertices();
    testLargeMeshIsIdenticalWithAndWithoutWorkers();
    testObjParsesFaces();
    testObjQuadIsTriangulated();
    testObjWithoutFacesIsRejected();
    testObjTruncatedVertexIsSkipped();
    testObjMissingFileFails();
}

TEST_MAIN("test_meshgen")
