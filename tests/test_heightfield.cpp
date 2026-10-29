// Heightfield collision.
//
// The gap this closes was named in the README for a long time: the procedurally
// generated terrain was scenery you fell through. The thing that makes it worth
// a suite of its own is that nearly every way of getting it wrong looks
// plausible on screen - a character standing a little above the ground, a ball
// that hesitates at every cell boundary, a slope whose normal is one triangle
// out - so the only useful evidence is arithmetic with an expected answer.
//
// Vulkan-free by construction: Heightfield knows about glm and nothing else.

#include "core/CollisionHull.hpp"
#include "core/Heightfield.hpp"
#include "core/MeshData.hpp"
#include "core/TerrainGenerator.hpp"
#include "TestHarness.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

using namespace Supersonic;

namespace {

const float kRoot2 = std::sqrt(2.0f);

bool nearlyVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return test::nearly(a.x, b.x, eps) && test::nearly(a.y, b.y, eps) &&
           test::nearly(a.z, b.z, eps);
}

Heightfield flatField(uint32_t width, uint32_t depth, float height, float thickness = 4.0f) {
    Heightfield field;
    std::vector<float> heights(static_cast<size_t>(width) * depth, height);
    field.Build(width, depth, thickness, std::move(heights));
    return field;
}

// A field whose height depends only on the grid X index, which is every ramp,
// wall, ridge and groove this suite needs.
Heightfield fieldFromColumns(uint32_t depth, const std::vector<float>& columns,
                             float thickness = 4.0f) {
    const auto width = static_cast<uint32_t>(columns.size());
    std::vector<float> heights(static_cast<size_t>(width) * depth);
    for (uint32_t z = 0; z < depth; ++z) {
        for (uint32_t x = 0; x < width; ++x) {
            heights[static_cast<size_t>(z) * width + x] = columns[x];
        }
    }
    Heightfield field;
    field.Build(width, depth, thickness, std::move(heights));
    return field;
}

// --- what a grid is -------------------------------------------------------

void testBuildRejectsWhatCannotBeACell() {
    Heightfield field;
    CHECK_MSG(!field.Build(1, 8, 4.0f, std::vector<float>(8, 0.0f)),
              "a single column cannot form a cell");
    CHECK_MSG(!field.Build(8, 1, 4.0f, std::vector<float>(8, 0.0f)),
              "and neither can a single row");
    CHECK_MSG(!field.Build(0, 0, 4.0f, {}), "zero wraps on an unsigned type");
    CHECK_MSG(!field.valid(), "a rejected build must leave nothing behind");

    // The size has to agree with the dimensions or every index into it is a
    // different vertex than the caller meant.
    CHECK_MSG(!field.Build(4, 4, 4.0f, std::vector<float>(15, 0.0f)),
              "a short height array must be refused, not read past");
    CHECK(field.Build(2, 2, 4.0f, std::vector<float>(4, 0.0f)));
    CHECK(field.valid());
}

void testTheGridIsCentredTheWayTheMeshIs() {
    // NOT symmetric about the origin, and this is the detail that puts a
    // collider half a cell away from the surface you can see. A 64-wide grid
    // has its vertices at -32 through +31, because the mesh subtracts
    // width/2 from an index that stops at width - 1.
    const Heightfield field = flatField(64, 64, 0.0f, 4.0f);

    CHECK_EQ(field.LocalMin().x, -32.0f);
    CHECK_EQ(field.LocalMax().x, 31.0f);
    CHECK_EQ(field.LocalMin().z, -32.0f);
    CHECK_EQ(field.LocalMax().z, 31.0f);

    CHECK(nearlyVec(field.VertexAt(0, 0), glm::vec3(-32.0f, 0.0f, -32.0f)));
    CHECK(nearlyVec(field.VertexAt(63, 63), glm::vec3(31.0f, 0.0f, 31.0f)));

    // The solid extends below the surface, and the bounds have to say so or the
    // broadphase never offers the pair that would push a buried body out.
    CHECK_EQ(field.LocalMin().y, -4.0f);
    CHECK_EQ(field.LocalMax().y, 0.0f);
}

// --- the collider IS the surface you can see ------------------------------

void testTheColliderIsTheMesh() {
    // The one test that matters most, and the only one that can catch a
    // heightfield that is internally consistent and describes a different hill.
    // Both go through TerrainGenerator::SampleHeight, so this is an equality,
    // not an approximation - any tolerance at all here would hide exactly the
    // drift the shared function exists to prevent.
    const uint32_t kDim = 64;
    const float kScale = 0.6f;

    MeshData mesh;
    CHECK(TerrainGenerator::GenerateTerrainMesh(kDim, kDim, kScale, mesh));

    Heightfield field;
    CHECK(TerrainGenerator::GenerateHeightfield(kDim, kDim, kScale, 4.0f, field));

    CHECK_EQ(mesh.vertices.size(), static_cast<size_t>(kDim) * kDim);
    CHECK_EQ(field.width(), kDim);
    CHECK_EQ(field.depth(), kDim);

    int mismatches = 0;
    for (uint32_t z = 0; z < kDim; ++z) {
        for (uint32_t x = 0; x < kDim; ++x) {
            const glm::vec3 fromMesh = mesh.vertices[static_cast<size_t>(z) * kDim + x].pos;
            const glm::vec3 fromField = field.VertexAt(x, z);
            if (fromMesh != fromField) ++mismatches;
        }
    }
    CHECK_MSG(mismatches == 0,
              "every collider vertex must be the mesh vertex, bit for bit");

    // Deliberately NOT compared: the mesh's per-vertex normals. Those are the
    // analytic normals of the smooth surface; the collider's are the flat
    // normals of the triangles the mesh is actually made of. They differ, and
    // they are both right.
}

// --- looking the surface up -----------------------------------------------

void testHeightAtReturnsTheVerticesItWasBuiltFrom() {
    Heightfield field;
    CHECK(TerrainGenerator::GenerateHeightfield(16, 16, 1.25f, 4.0f, field));

    int mismatches = 0;
    for (uint32_t z = 0; z < 16; ++z) {
        for (uint32_t x = 0; x < 16; ++x) {
            const glm::vec3 vertex = field.VertexAt(x, z);
            float sampled = 0.0f;
            if (!field.HeightAt(vertex.x, vertex.z, sampled)) { ++mismatches; continue; }
            if (!test::nearly(sampled, vertex.y, 1e-5f)) ++mismatches;
        }
    }
    CHECK_MSG(mismatches == 0, "sampling at a vertex must return that vertex's height");
}

void testHeightAtInterpolatesTheTriangleAndNotTheQuad() {
    // A saddle cell, where the two answers are furthest apart. Bilinear is the
    // obvious thing to write and it is a CURVED patch through the four corners;
    // the mesh is two flat triangles, and a body resting on the difference sits
    // in the air or inside the ground by the whole of it.
    Heightfield field;
    std::vector<float> heights = {
        0.0f, 1.0f,   // z = 0: TL, TR
        1.0f, 0.0f,   // z = 1: BL, BR
    };
    CHECK(field.Build(2, 2, 4.0f, std::move(heights)));

    // Local coordinates: a 2 x 2 grid spans -1 to 0 on both axes.
    float centre = 0.0f;
    CHECK(field.HeightAt(-0.5f, -0.5f, centre));
    // On the diagonal itself, which belongs to the first triangle: 0 + 0.5 +
    // 0.5 = 1. Bilinear would say 0.5.
    CHECK_NEAR(centre, 1.0f);

    float inSecond = 0.0f;
    CHECK(field.HeightAt(-0.4f, -0.4f, inSecond));
    // u = v = 0.6, so the second triangle: 0 + 0.4 * 1 + 0.4 * 1 = 0.8.
    // Bilinear would say 0.48.
    CHECK_NEAR(inSecond, 0.8f);

    float outside = 0.0f;
    CHECK_MSG(!field.HeightAt(2.0f, 0.0f, outside), "there is no surface off the grid");
    CHECK_MSG(!field.HeightAt(-0.5f, -3.0f, outside), "on either axis");
}

void testTriangleNormalsPointOutOfTheSurface() {
    const Heightfield flat = flatField(4, 4, 0.0f);
    glm::vec3 corners[3];
    glm::vec3 normal(0.0f);
    CHECK(flat.TriangleAt(0.0f, 0.0f, corners, normal));
    CHECK(nearlyVec(normal, glm::vec3(0.0f, 1.0f, 0.0f)));

    // A ramp climbing in +x by half a unit per cell. The normal leans the other
    // way, by exactly the slope: (-s, 1, 0) normalised.
    const Heightfield ramp = fieldFromColumns(4, {0.0f, 0.5f, 1.0f, 1.5f});
    const glm::vec3 expected = glm::normalize(glm::vec3(-0.5f, 1.0f, 0.0f));

    int wrong = 0;
    // Both triangles of every cell, since a ramp in x is one plane and any
    // disagreement between the two is a triangulation the mesh does not share.
    // A 4 x 4 grid spans -2 to +1 on both axes; past that there is no surface
    // and TriangleAt is right to say so.
    for (float x = -1.9f; x < 0.9f; x += 0.13f) {
        for (float z = -1.9f; z < 0.9f; z += 0.17f) {
            glm::vec3 unused[3];
            glm::vec3 sampled(0.0f);
            if (!ramp.TriangleAt(x, z, unused, sampled)) { ++wrong; continue; }
            if (!nearlyVec(sampled, expected)) ++wrong;
        }
    }
    CHECK_MSG(wrong == 0, "every triangle of a ramp lies in the same plane");

    CHECK_MSG(!flat.TriangleAt(9.0f, 0.0f, corners, normal), "and none of them off the grid");
}

// --- spheres --------------------------------------------------------------

void testASphereRestsOnFlatGround() {
    const Heightfield field = flatField(8, 8, 0.0f);

    const Heightfield::Manifold resting = field.CollideSphere(glm::vec3(0.0f, 0.4f, 0.0f), 0.5f);
    CHECK_EQ(resting.count, 1);
    CHECK(!resting.speculative);
    CHECK(nearlyVec(resting.points[0].normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK_NEAR(resting.points[0].penetration, 0.1f);
    CHECK_NEAR(resting.points[0].position.y, 0.0f);

    const Heightfield::Manifold clear = field.CollideSphere(glm::vec3(0.0f, 1.0f, 0.0f), 0.5f);
    CHECK_EQ(clear.count, 0);

    // Apart, but near enough to close the gap this step. Reported with a
    // NEGATIVE penetration so the solver brakes instead of pushing.
    const Heightfield::Manifold closing =
        field.CollideSphere(glm::vec3(0.0f, 1.0f, 0.0f), 0.5f, 0.6f);
    CHECK_EQ(closing.count, 1);
    CHECK(closing.speculative);
    CHECK_NEAR(closing.points[0].penetration, -0.5f);
}

void testASphereDoesNotCatchOnTheSEAMS() {
    // THE failure this contact model exists to avoid, and the one the obvious
    // implementation has. A sphere over one triangle is also within reach of
    // its neighbour, whose nearest point is the shared edge - so a naive loop
    // reports a second contact with a sideways normal on ground that is
    // perfectly flat, and a ball rolling across a field is shoved at every cell
    // boundary it crosses.
    //
    // Swept rather than spot-checked, because the artefact only appears within
    // one radius of a boundary and a single sample lands wherever it lands.
    const Heightfield field = flatField(8, 8, 0.0f);

    int wrongCount = 0;
    int wrongNormal = 0;
    int wrongDepth = 0;
    int samples = 0;
    for (float x = -2.0f; x <= 2.0f; x += 0.05f) {
        for (float z = -1.0f; z <= 1.0f; z += 0.25f) {
            const Heightfield::Manifold hit =
                field.CollideSphere(glm::vec3(x, 0.4f, z), 0.5f);
            ++samples;
            if (hit.count != 1) { ++wrongCount; continue; }
            if (!nearlyVec(hit.points[0].normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-5f)) ++wrongNormal;
            if (!test::nearly(hit.points[0].penetration, 0.1f, 1e-5f)) ++wrongDepth;
        }
    }
    CHECK_MSG(samples > 400, "the sweep has to actually cover the boundaries");
    CHECK_MSG(wrongCount == 0, "flat ground is one contact everywhere, seams included");
    CHECK_MSG(wrongNormal == 0, "and it points straight up everywhere");
    CHECK_MSG(wrongDepth == 0, "at the same depth everywhere");
}

void testASphereOnASlopeIsPushedAlongTheSlope() {
    // The failure the SAT narrowphase was written to fix, in the shape that
    // replaced it: a ball on a hill must be held along the hill's normal, not
    // along +Y, or it rests in the air and never rolls.
    const Heightfield ramp = fieldFromColumns(4, {0.0f, 0.5f, 1.0f, 1.5f});
    const glm::vec3 normal = glm::normalize(glm::vec3(-0.5f, 1.0f, 0.0f));

    // A point strictly inside one triangle: cell (1, 1) at u = v = 0.25.
    const glm::vec3 surface(-0.75f, 0.625f, -0.75f);
    float sampled = 0.0f;
    CHECK(ramp.HeightAt(surface.x, surface.z, sampled));
    CHECK_NEAR(sampled, surface.y);

    // Backed off along the normal so the overlap is exactly 0.1.
    const glm::vec3 centre = surface + normal * 0.4f;
    const Heightfield::Manifold hit = ramp.CollideSphere(centre, 0.5f);

    CHECK_EQ(hit.count, 1);
    CHECK(nearlyVec(hit.points[0].normal, normal));
    CHECK_NEAR(hit.points[0].penetration, 0.1f);
    CHECK(nearlyVec(hit.points[0].position, surface));
}

void testASphereIsHeldUpByTheCRESTOfARidge() {
    // The case the seam filter must NOT eat. On the top of a ridge neither face
    // has the sphere over it, so neither can offer a face contact - the only
    // thing holding the ball up is the edge they share. Drop that and a ball
    // rolled at a hill goes through the top of it.
    const Heightfield tent = fieldFromColumns(5, {0.0f, 1.0f, 2.0f, 1.0f, 0.0f});

    // The crest runs along grid x = 2, which is local x = -0.5.
    const glm::vec3 centre(-0.5f, 2.4f, 0.0f);
    const Heightfield::Manifold hit = tent.CollideSphere(centre, 0.5f);

    CHECK_MSG(hit.count > 0, "the crest of a ridge is solid");
    CHECK(!hit.speculative);

    float deepest = 0.0f;
    int sideways = 0;
    for (int i = 0; i < hit.count; ++i) {
        deepest = std::max(deepest, hit.points[i].penetration);
        // Straight down onto the crest from directly above is straight up back
        // out of it, by symmetry, whichever face reported it.
        if (!nearlyVec(hit.points[i].normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-4f)) ++sideways;
    }
    CHECK_MSG(sideways == 0, "and it pushes straight up, not off one side");
    CHECK_NEAR(deepest, 0.1f);
}

void testASphereInAGrooveIsHeldByBothFaces() {
    // The concave case, and the reason a manifold carries more than one point:
    // held by the deepest face alone, a ball in a valley is pushed up the
    // opposite wall, then back, one step after another.
    const Heightfield groove = fieldFromColumns(5, {2.0f, 1.0f, 0.0f, 1.0f, 2.0f});

    // Both faces are at 45 degrees, so a centre y above the crease is a
    // perpendicular distance of y / sqrt(2) from each of them.
    const float distance = 0.4f;
    const glm::vec3 centre(-0.5f, distance * kRoot2, 0.0f);
    const Heightfield::Manifold hit = groove.CollideSphere(centre, 0.5f);

    CHECK_EQ(hit.count, 2);

    glm::vec3 sum(0.0f);
    int wrongDepth = 0;
    for (int i = 0; i < hit.count; ++i) {
        sum += hit.points[i].normal;
        if (!test::nearly(hit.points[i].penetration, 0.1f)) ++wrongDepth;
    }
    CHECK_MSG(wrongDepth == 0, "both faces are the same distance away");

    // The two lean opposite ways by the same amount, so together they push
    // straight up and the ball settles in the bottom instead of climbing.
    CHECK(nearlyVec(glm::normalize(sum), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(nearlyVec(hit.points[0].normal + hit.points[1].normal,
                    glm::vec3(0.0f, kRoot2, 0.0f)));
}

void testABuriedSphereIsPushedOutOfTheTop() {
    // The recovery path. Without it a body that has been placed inside a hill -
    // by a spawn point, an editor gizmo, a script - has no way out and the
    // surface is one-sided, which is the failure mode of every thin collider.
    const Heightfield field = flatField(8, 8, 0.0f, 4.0f);

    const Heightfield::Manifold buried = field.CollideSphere(glm::vec3(0.0f, -0.5f, 0.0f), 0.25f);
    CHECK_EQ(buried.count, 1);
    CHECK(nearlyVec(buried.points[0].normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK_NEAR(buried.points[0].penetration, 0.75f);
    CHECK_NEAR(buried.points[0].position.y, 0.0f);

    // Past the bottom of the slab it has left the terrain, and shoving it back
    // up through the whole hill would be a worse answer than letting it fall.
    const Heightfield::Manifold gone = field.CollideSphere(glm::vec3(0.0f, -5.0f, 0.0f), 0.25f);
    CHECK_EQ(gone.count, 0);

    // Which is what `thickness` means, so a deeper slab still catches it.
    const Heightfield deep = flatField(8, 8, 0.0f, 20.0f);
    const Heightfield::Manifold caught = deep.CollideSphere(glm::vec3(0.0f, -5.0f, 0.0f), 0.25f);
    CHECK_EQ(caught.count, 1);
    CHECK_NEAR(caught.points[0].penetration, 5.25f);
}

// --- capsules -------------------------------------------------------------

void testACapsuleStandsOnItsLowerCap() {
    const Heightfield field = flatField(8, 8, 0.0f);

    const Heightfield::Manifold upright = field.CollideCapsule(
        glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, 2.4f, 0.0f), 0.5f);
    CHECK_MSG(upright.count == 1, "the top of a standing capsule touches nothing");
    CHECK_NEAR(upright.points[0].penetration, 0.1f);

    // Lying down, it is held at BOTH ends - which is what stops it rocking end
    // over end about a single point in the middle.
    const Heightfield::Manifold lying = field.CollideCapsule(
        glm::vec3(-1.0f, 0.4f, 0.0f), glm::vec3(1.0f, 0.4f, 0.0f), 0.5f);
    CHECK_EQ(lying.count, 2);
    CHECK_NEAR(lying.points[0].penetration, 0.1f);
    CHECK_NEAR(lying.points[1].penetration, 0.1f);
    CHECK(nearlyVec(lying.points[0].normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(nearlyVec(lying.points[1].normal, glm::vec3(0.0f, 1.0f, 0.0f)));

    // A capsule of no length is a sphere. Collecting the same end twice would
    // hand the solver two identical constraints and twice the impulse.
    const Heightfield::Manifold degenerate = field.CollideCapsule(
        glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, 0.4f, 0.0f), 0.5f);
    CHECK_EQ(degenerate.count, 1);
}

// --- boxes ----------------------------------------------------------------

CollisionSAT::Obb boxAt(const glm::vec3& centre, const glm::vec3& halfExtent) {
    CollisionSAT::Obb box;
    box.centre = centre;
    box.halfExtent = halfExtent;
    box.axes = glm::mat3(1.0f);
    return box;
}

void testACrateRestsOnItsFourCorners() {
    const Heightfield field = flatField(8, 8, 0.0f);

    const Heightfield::Manifold hit =
        field.CollideObb(boxAt(glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.5f)));

    // Four points, not one. A crate held by a single spot in its own footprint
    // is free to rotate about it, and it does.
    CHECK_EQ(hit.count, 4);
    int wrong = 0;
    for (int i = 0; i < hit.count; ++i) {
        if (!nearlyVec(hit.points[i].normal, glm::vec3(0.0f, 1.0f, 0.0f))) ++wrong;
        if (!test::nearly(hit.points[i].penetration, 0.1f)) ++wrong;
    }
    CHECK_MSG(wrong == 0, "every corner of a level crate sinks the same amount");

    // Clear of the ground is no contact at all.
    CHECK_EQ(field.CollideObb(boxAt(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.5f))).count, 0);
}

void testACrateStraddlingABumpDoesNotSwallowIt() {
    // The case that corner sampling alone gets wrong, and the reason the box
    // path also tests the SURFACE's vertices against the box. Here the crate is
    // wide enough that all four of its corners are over flat ground and clear
    // of it - so a corners-only collider reports nothing at all and the bump
    // passes through the crate's floor.
    Heightfield field;
    std::vector<float> heights = {
        0.0f, 0.0f, 0.0f,
        0.0f, 0.3f, 0.0f,   // one spike, at grid (1, 1) = local (-0.5, -0.5)
        0.0f, 0.0f, 0.0f,
    };
    CHECK(field.Build(3, 3, 4.0f, std::move(heights)));

    const glm::vec3 spike = field.VertexAt(1, 1);
    CHECK(nearlyVec(spike, glm::vec3(-0.5f, 0.3f, -0.5f)));

    // Bottom face at y = 0.05: above every flat vertex, below the spike.
    const CollisionSAT::Obb crate = boxAt(glm::vec3(-0.5f, 1.05f, -0.5f), glm::vec3(1.0f));
    const Heightfield::Manifold hit = field.CollideObb(crate);

    CHECK_MSG(hit.count == 1, "the bump is the only thing touching, and it is touching");
    CHECK(nearlyVec(hit.points[0].position, spike));
    CHECK(nearlyVec(hit.points[0].normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    // How far the crate has to rise for the spike to clear its underside.
    CHECK_NEAR(hit.points[0].penetration, 0.25f);

    // And the corners really are clear, so this test is testing what it says.
    int cornersUnderGround = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 position(
            crate.centre.x + ((corner & 1) ? 1.0f : -1.0f),
            crate.centre.y + ((corner & 2) ? 1.0f : -1.0f),
            crate.centre.z + ((corner & 4) ? 1.0f : -1.0f));
        float surface = 0.0f;
        if (field.HeightAt(position.x, position.z, surface) && position.y < surface) {
            ++cornersUnderGround;
        }
    }
    CHECK_EQ(cornersUnderGround, 0);
}

// --- rays -----------------------------------------------------------------

void testARayFindsTheSurfaceBelowIt() {
    const Heightfield field = flatField(8, 8, 1.5f);

    float distance = 0.0f;
    glm::vec3 normal(0.0f);
    CHECK(field.Raycast(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 10.0f,
                        distance, normal));
    CHECK_NEAR(distance, 3.5f);
    CHECK(nearlyVec(normal, glm::vec3(0.0f, 1.0f, 0.0f)));

    // Short of the surface is a miss, or IsGrounded would report solid ground
    // from any height at all.
    CHECK(!field.Raycast(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 2.0f,
                         distance, normal));

    // Off the grid there is nothing to hit.
    CHECK(!field.Raycast(glm::vec3(100.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f,
                         distance, normal));

    // From UNDERNEATH, which is how a buried body finds out which way out is.
    // A one-sided triangle test would report nothing here.
    CHECK(field.Raycast(glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 10.0f,
                        distance, normal));
    CHECK_NEAR(distance, 1.0f);
}

void testAShallowRayCannotStepOverARidge() {
    // A ray sampled at a fixed interval walks straight through anything thinner
    // than its step, and the symptom - a shot that passes through a hill about
    // one time in ten - is close to impossible to reproduce deliberately. The
    // march is over CELLS for that reason, and this is the case that tells the
    // two apart.
    const Heightfield wall = fieldFromColumns(8, {0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f});

    float distance = 0.0f;
    glm::vec3 normal(0.0f);
    CHECK(wall.Raycast(glm::vec3(-3.5f, 0.5f, 0.3f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f,
                       distance, normal));

    // The face climbs from 0 at local x = -1 to 3 at local x = 0, so it crosses
    // y = 0.5 one sixth of the way along.
    CHECK_NEAR(distance, 3.5f - 1.0f + (1.0f / 6.0f));

    // Nearly vertical, since the face rises three units across one.
    CHECK_MSG(normal.y > 0.0f && normal.y < 0.4f, "a steep face has a shallow normal");
    CHECK_MSG(normal.x < -0.9f, "and it faces back the way the ray came");
}

// --- the triangle query the contact model is built on ---------------------

void testClosestPointOnTriangle() {
    const glm::vec3 a(0.0f, 0.0f, 0.0f);
    const glm::vec3 b(1.0f, 0.0f, 0.0f);
    const glm::vec3 c(0.0f, 0.0f, 1.0f);

    // Face: straight down onto the interior.
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(0.25f, 5.0f, 0.25f)),
                    glm::vec3(0.25f, 0.0f, 0.25f)));
    // Vertex regions, one each.
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(-2.0f, 1.0f, -2.0f)), a));
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(4.0f, 1.0f, -2.0f)), b));
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(-2.0f, 1.0f, 4.0f)), c));
    // Edge regions.
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(0.5f, 1.0f, -3.0f)),
                    glm::vec3(0.5f, 0.0f, 0.0f)));
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(-3.0f, 1.0f, 0.5f)),
                    glm::vec3(0.0f, 0.0f, 0.5f)));
    CHECK(nearlyVec(CollisionSAT::ClosestPointOnTriangle(a, b, c, glm::vec3(2.0f, 1.0f, 2.0f)),
                    glm::vec3(0.5f, 0.0f, 0.5f)));

    // A degenerate triangle is a flat spot, not an error: two coincident
    // corners must return a point on what is left rather than divide by a zero
    // area and hand back a NaN that quietly poisons a contact normal.
    const glm::vec3 collapsed =
        CollisionSAT::ClosestPointOnTriangle(a, b, b, glm::vec3(0.5f, 1.0f, 0.0f));
    CHECK(std::isfinite(collapsed.x) && std::isfinite(collapsed.y) && std::isfinite(collapsed.z));
    CHECK_NEAR(collapsed.y, 0.0f);
}

// --- a convex hull on terrain --------------------------------------------
//
// A hull used to collide with terrain as its world bounding box, so a wedge on
// a hill floated on the corner of a box nobody could see.
//
// The check that matters is differential rather than invented. CollideObb IS
// this algorithm specialised to a cube - its eight corners are the cube hull's
// eight vertices, and its least-exit-axis is what ClosestPointOnHull computes
// for a point inside any convex shape - so a cube hull placed exactly where a
// box is must produce the SAME manifold, contact for contact.

std::vector<Heightfield::Contact> sortedContacts(const Heightfield::Manifold& manifold) {
    std::vector<Heightfield::Contact> out(manifold.points,
                                          manifold.points + manifold.count);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.position.x != b.position.x) return a.position.x < b.position.x;
        if (a.position.z != b.position.z) return a.position.z < b.position.z;
        return a.position.y < b.position.y;
    });
    return out;
}

CollisionSAT::Obb makeBox(const glm::vec3& centre, const glm::vec3& halfExtent,
                          float yaw = 0.0f) {
    CollisionSAT::Obb box;
    box.centre = centre;
    box.halfExtent = halfExtent;
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    box.axes = glm::mat3(glm::vec3(c, 0.0f, s), glm::vec3(0.0f, 1.0f, 0.0f),
                         glm::vec3(-s, 0.0f, c));
    return box;
}

void testACubeHullOnTerrainIsTheBoxPath() {
    // Five arrangements, chosen so each exercises a different half: resting
    // flat, sunk in, straddling a ridge (which only the DUAL can report),
    // rotated, and clear of the ground inside the speculative margin.
    const Heightfield flat = flatField(8, 8, 0.0f);
    const Heightfield ridge = fieldFromColumns(8, {0.0f, 0.0f, 0.0f, 1.2f, 1.2f, 0.0f, 0.0f, 0.0f});

    struct Case {
        const Heightfield* field;
        CollisionSAT::Obb box;
        float margin;
        const char* what;
    };
    const Case cases[] = {
        { &flat,  makeBox(glm::vec3(0.0f, 0.45f, 0.0f), glm::vec3(0.5f)),        0.0f, "resting" },
        { &flat,  makeBox(glm::vec3(0.0f, 0.20f, 0.0f), glm::vec3(0.5f)),        0.0f, "sunk in" },
        { &ridge, makeBox(glm::vec3(0.0f, 0.90f, 0.0f), glm::vec3(2.0f, 0.4f, 1.0f)), 0.0f, "straddling" },
        { &flat,  makeBox(glm::vec3(0.3f, 0.35f, -0.2f), glm::vec3(0.6f, 0.5f, 0.4f), 0.7f), 0.0f, "rotated" },
        { &flat,  makeBox(glm::vec3(0.0f, 0.90f, 0.0f), glm::vec3(0.5f)),        0.6f, "speculative" },
    };

    int mismatched = 0;
    int compared = 0;
    for (const Case& c : cases) {
        const Heightfield::Manifold boxManifold = c.field->CollideObb(c.box, c.margin);

        CollisionHull::Instance hull = CollisionHull::InstanceFromObb(c.box);
        CHECK_MSG(hull.hull != nullptr, "the cube instance must build");
        const Heightfield::Manifold hullManifold = c.field->CollideHull(hull, c.margin);

        if (boxManifold.count != hullManifold.count ||
            boxManifold.speculative != hullManifold.speculative) {
            ++mismatched;
            continue;
        }

        const auto a = sortedContacts(boxManifold);
        const auto b = sortedContacts(hullManifold);
        for (size_t i = 0; i < a.size(); ++i) {
            ++compared;
            if (!nearlyVec(a[i].position, b[i].position, 1e-4f) ||
                !nearlyVec(a[i].normal, b[i].normal, 1e-4f) ||
                !test::nearly(a[i].penetration, b[i].penetration, 1e-4f)) {
                ++mismatched;
            }
        }
    }

    CHECK_MSG(mismatched == 0, "a cube hull must collide with terrain exactly as a box does");
    CHECK_MSG(compared >= 8, "the arrangements must actually produce contacts to compare");
}

void testTheDualIsWhatCatchesARidge() {
    // The half that is easy to leave out. A hull wider than a cell straddling a
    // bump has no VERTEX under the ground - the bump comes up through its
    // underside between them - so vertices alone report nothing at all.
    const Heightfield ridge = fieldFromColumns(8, {0.0f, 0.0f, 0.0f, 1.2f, 1.2f, 0.0f, 0.0f, 0.0f});

    // A slab spanning the ridge, its underside above the flat ground and just
    // below the crest.
    //
    // JUST below, and that is not fussiness. The rule is least-exit, inherited
    // from the box path: a terrain vertex is pushed out through whichever face
    // of the hull it is nearest to leaving through. Sink the slab far enough
    // and the crest is nearer its TOP, at which point the correct answer by
    // that rule is to push the slab DOWN - which is the documented shortcut a
    // box has always had here, not a new bug. The slab has to be positioned so
    // that its underside really is the nearest face, or the test asserts
    // something the algorithm never claimed.
    const CollisionSAT::Obb box = makeBox(glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(2.0f, 0.4f, 1.0f));
    CollisionHull::Instance hull = CollisionHull::InstanceFromObb(box);

    const Heightfield::Manifold manifold = ridge.CollideHull(hull, 0.0f);
    CHECK_MSG(manifold.count > 0, "the ridge must be reported through the slab's underside");

    int pushedUp = 0;
    for (int i = 0; i < manifold.count; ++i) {
        if (manifold.points[i].normal.y > 0.5f) ++pushedUp;
    }
    CHECK_MSG(pushedUp > 0, "and the slab must be pushed UP off it, not sideways");
}

void testAHullClearOfTheGroundReportsNothing() {
    const Heightfield flat = flatField(8, 8, 0.0f);
    const CollisionSAT::Obb box = makeBox(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.5f));
    CollisionHull::Instance hull = CollisionHull::InstanceFromObb(box);

    const Heightfield::Manifold manifold = flat.CollideHull(hull, 0.0f);
    CHECK_EQ(manifold.count, uint32_t{0});
}

void testAPointedHullFitsWhereItsBoundingBoxCannot() {
    // The gap this closes, stated as a number rather than as a picture.
    //
    // A hull used to collide as the oriented box containing it. A shape that
    // comes to a point therefore rested on that box's flat underside, so it
    // floated above ground its apex would have gone into - and the README called
    // it "a wedge on a hill floats by the gap between the two".
    //
    // A notch one cell wide, with the floor a full unit below the shoulders.
    const Heightfield groove =
        fieldFromColumns(8, {1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f});

    // Grid column 3 is world x = 3 - 8/2 = -1, so the notch floor is there.
    // A tetrahedron with its apex pointing DOWN into it, and nothing touching:
    // the apex is a quarter unit above the floor, and every other vertex is
    // well above the shoulders.
    ConvexHull tetra;
    CHECK_MSG(tetra.Build({glm::vec3(-1.0f, 0.25f, 0.0f), glm::vec3(-1.7f, 1.15f, -0.7f),
                           glm::vec3(-0.3f, 1.15f, -0.7f), glm::vec3(-1.0f, 1.15f, 0.7f)}),
              "the tetrahedron must build");

    CollisionHull::Instance instance;
    CHECK_MSG(CollisionHull::MakeInstance(tetra, glm::vec3(0.0f), glm::mat3(1.0f), instance),
              "and place");

    const Heightfield::Manifold hullManifold = groove.CollideHull(instance, 0.0f);
    CHECK_MSG(hullManifold.count == 0,
              "nothing is touching, so the hull must report nothing");

    // The same shape through the path it used to take. Its bounding box has a
    // flat underside spanning the whole notch, and the shoulders come up
    // through it.
    CollisionSAT::Obb box;
    box.centre = (tetra.boundsMin() + tetra.boundsMax()) * 0.5f;
    box.halfExtent = (tetra.boundsMax() - tetra.boundsMin()) * 0.5f;
    box.axes = glm::mat3(1.0f);

    const Heightfield::Manifold boxManifold = groove.CollideObb(box, 0.0f);
    CHECK_MSG(boxManifold.count > 0,
              "the bounding box must collide here, or this fixture proves nothing");
}

// How big a collision grid is allowed to be. The size is a scene file's claim, and
// the grid was sized from it before anything checked: 65536 x 65536 is 17 GB of
// heights, asked for by a few bytes of JSON on the first physics step.
void testAGridTooBigToBuildIsRefusedBeforeItIsAllocated() {
    Heightfield out;

    // Over the side limit, but small in cells: the old code built this happily,
    // which is what makes the failure visible without allocating anything absurd.
    CHECK_MSG(!TerrainGenerator::GenerateHeightfield(TerrainGenerator::kMaxHeightfieldSide + 1, 2,
                                                     1.0f, 1.0f, out),
              "a side past the limit is refused");

    // Under the side limit on both, over the cell limit.
    CHECK_MSG(!TerrainGenerator::GenerateHeightfield(8193, 8193, 1.0f, 1.0f, out),
              "more cells than the limit is refused");

    // Four billion by four billion: must come back false, not an allocation.
    bool threw = false;
    try {
        CHECK(!TerrainGenerator::GenerateHeightfield(4000000000u, 4000000000u, 1.0f, 1.0f, out));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK_MSG(!threw, "an absurd size is refused, not thrown");

    // And the limits are not an accident of the check: a real size still builds.
    CHECK_MSG(TerrainGenerator::GenerateHeightfield(64, 64, 1.0f, 1.0f, out),
              "an ordinary grid still builds");
}

void runTests() {
    testBuildRejectsWhatCannotBeACell();
    testAGridTooBigToBuildIsRefusedBeforeItIsAllocated();
    testTheGridIsCentredTheWayTheMeshIs();
    testTheColliderIsTheMesh();

    testHeightAtReturnsTheVerticesItWasBuiltFrom();
    testHeightAtInterpolatesTheTriangleAndNotTheQuad();
    testTriangleNormalsPointOutOfTheSurface();

    testASphereRestsOnFlatGround();
    testASphereDoesNotCatchOnTheSEAMS();
    testASphereOnASlopeIsPushedAlongTheSlope();
    testASphereIsHeldUpByTheCRESTOfARidge();
    testASphereInAGrooveIsHeldByBothFaces();
    testABuriedSphereIsPushedOutOfTheTop();

    testACapsuleStandsOnItsLowerCap();

    testACrateRestsOnItsFourCorners();
    testACrateStraddlingABumpDoesNotSwallowIt();

    testARayFindsTheSurfaceBelowIt();
    testAShallowRayCannotStepOverARidge();

    testClosestPointOnTriangle();
    testACubeHullOnTerrainIsTheBoxPath();
    testTheDualIsWhatCatchesARidge();
    testAHullClearOfTheGroundReportsNothing();
    testAPointedHullFitsWhereItsBoundingBoxCannot();
}

} // namespace

TEST_MAIN("test_heightfield", 70)
