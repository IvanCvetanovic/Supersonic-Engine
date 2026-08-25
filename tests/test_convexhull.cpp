// Convex hulls.
//
// The last shape on the README's list. There is no closed form to check a hull
// against - unlike the heightfield, whose surface is an expression both sides
// can be asked for - so this suite leans on two other things.
//
// STRUCTURAL invariants that hold for every convex polyhedron and for nothing
// else: Euler's V - E + F = 2, and every vertex behind every face plane. A face
// dropped, a face added twice, a merge that left a hole and an inverted winding
// all break one of those, and none of them shows up in a picture.
//
// And the CUBE, which is the one shape whose hull can be written down: eight
// corners in, six faces out, with the six axis-aligned normals. That single
// assertion catches a failed merge (twelve triangles), a missing face, a
// duplicate, and a winding that turned a normal inwards.

#include "core/CollisionHull.hpp"
#include "core/ConvexHull.hpp"
#include "TestHarness.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <string>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Supersonic;

namespace {

std::vector<glm::vec3> cubeCorners(float half = 0.5f) {
    std::vector<glm::vec3> points;
    for (int i = 0; i < 8; ++i) {
        points.push_back(glm::vec3((i & 1) ? half : -half,
                                   (i & 2) ? half : -half,
                                   (i & 4) ? half : -half));
    }
    return points;
}

bool nearlyVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return test::nearly(a.x, b.x, eps) && test::nearly(a.y, b.y, eps) &&
           test::nearly(a.z, b.z, eps);
}

bool hasNormal(const ConvexHull& hull, const glm::vec3& wanted) {
    for (const auto& face : hull.faces()) {
        if (glm::dot(face.normal, wanted) > 0.999f) return true;
    }
    return false;
}

// --- what is not a solid ----------------------------------------------------

void testTooFewPointsIsNotAHull() {
    ConvexHull hull;
    CHECK(!hull.Build({}));
    CHECK(!hull.Build({glm::vec3(0.0f)}));
    CHECK(!hull.Build({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}));
    CHECK(!hull.Build({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}));
    CHECK_MSG(!hull.valid(), "and a rejected build leaves nothing behind to collide with");
}

void testCollinearPointsAreNotASolid() {
    // A straight line, however many points are on it. The seed search looks for
    // a point off the LINE and finds none.
    std::vector<glm::vec3> line;
    for (int i = 0; i < 12; ++i) line.push_back(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));

    ConvexHull hull;
    CHECK(!hull.Build(line));
}

void testCoplanarPointsAreNotASolid() {
    // A flat sheet is a real thing to ask for and it is not a volume. Building
    // one anyway gives a tetrahedron of zero thickness whose normals are a
    // division by nothing.
    std::vector<glm::vec3> sheet;
    for (int x = 0; x < 4; ++x) {
        for (int z = 0; z < 4; ++z) {
            sheet.push_back(glm::vec3(static_cast<float>(x), 2.0f, static_cast<float>(z)));
        }
    }

    ConvexHull hull;
    CHECK(!hull.Build(sheet));
}

void testCoincidentPointsAreOnePoint() {
    // Four points in two places is two points, and two points are not a solid.
    ConvexHull hull;
    CHECK(!hull.Build({glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                       glm::vec3(1.0f, 0.0f, 0.0f)}));
}

void testAHullOfNonsenseIsNotAHull() {
    // A NaN in a vertex reaches this from a corrupted file or a script, and a
    // NaN plane makes every "is this point outside" comparison false - so the
    // hull silently stops growing and comes out as whatever the seed was.
    const float nan = std::nanf("");
    std::vector<glm::vec3> points = cubeCorners();
    points.push_back(glm::vec3(nan, nan, nan));

    ConvexHull hull;
    CHECK_MSG(hull.Build(points), "the nonsense is dropped and the rest still builds");
    CHECK_EQ(hull.vertices().size(), size_t{8});
    for (const glm::vec3& vertex : hull.vertices()) {
        CHECK(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z));
    }
}

// --- THE shape --------------------------------------------------------------

void testACubeComesBackAsSixFaces() {
    ConvexHull hull;
    CHECK(hull.Build(cubeCorners()));

    CHECK_EQ(hull.vertices().size(), size_t{8});
    CHECK_MSG(hull.faces().size() == 6,
              "six, not twelve: coplanar triangles have to merge or every reference "
              "face is a sliver and a crate rocks on it");
    CHECK_EQ(hull.edges().size(), size_t{12});

    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                               {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int found = 0;
    for (const glm::vec3& axis : axes) {
        if (hasNormal(hull, axis)) ++found;
    }
    CHECK_MSG(found == 6, "and each of the six faces points along its own axis, outwards");

    // Every face four-sided, which a merge that stopped half way would not give.
    int quads = 0;
    for (const auto& face : hull.faces()) {
        if (face.count == 4) ++quads;
    }
    CHECK_EQ(quads, 6);
}

void testTheStructuralInvariantsHold() {
    // What a convex polyhedron IS, checked on several shapes rather than argued
    // about. A dropped face, a duplicated one and a merge that left a hole all
    // break one of these and none of them is visible.
    struct Case {
        const char* name;
        std::vector<glm::vec3> points;
    };

    std::vector<Case> cases;
    cases.push_back({"cube", cubeCorners()});
    cases.push_back({"tetrahedron",
                     {glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, 1.0f)}});

    // A wedge: a cube with one edge collapsed, which is the shape a ramp is.
    cases.push_back({"wedge",
                     {glm::vec3(-1, -1, -1), glm::vec3(1, -1, -1), glm::vec3(1, -1, 1),
                      glm::vec3(-1, -1, 1), glm::vec3(-1, 1, -1), glm::vec3(-1, 1, 1)}});

    // A rough ball, which is where the merge has the least to do and the face
    // count is highest.
    {
        std::vector<glm::vec3> ball;
        for (int i = 0; i < 60; ++i) {
            const float u = static_cast<float>(i) * 0.61803399f;
            const float theta = u * 6.2831853f;
            const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 60.0f;
            const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
            ball.push_back(glm::vec3(std::cos(theta) * radius, y, std::sin(theta) * radius));
        }
        cases.push_back({"ball", ball});
    }

    for (const Case& one : cases) {
        ConvexHull hull;
        CHECK_MSG(hull.Build(one.points), one.name);
        CHECK_MSG(hull.IsConvex(), std::string(one.name) + ": every vertex behind every face");
        CHECK_MSG(hull.SatisfiesEulerFormula(),
                  std::string(one.name) + ": V - E + F must be 2");
    }
}

void testAHullContainsThePointsItWasBuiltFrom() {
    // The other half of convexity, and the one that says the hull is of THESE
    // points rather than of some of them.
    std::vector<glm::vec3> points;
    for (int i = 0; i < 40; ++i) {
        const float t = static_cast<float>(i);
        points.push_back(glm::vec3(std::sin(t) * 2.0f, std::cos(t * 1.7f), std::sin(t * 0.3f) * 3.0f));
    }

    ConvexHull hull;
    CHECK(hull.Build(points));
    CHECK_MSG(hull.residual() < 1e-3f,
              "under the vertex cap the hull contains every point it was given");

    int outside = 0;
    for (const glm::vec3& point : points) {
        for (const auto& face : hull.faces()) {
            if (glm::dot(face.normal, point) > face.offset + 1e-3f) {
                ++outside;
                break;
            }
        }
    }
    CHECK_EQ(outside, 0);
}

void testInteriorPointsChangeNothing() {
    // A hull is of the OUTSIDE. Points inside it are already described and must
    // not become vertices, or a detailed mesh spends its whole vertex budget on
    // geometry nobody can touch.
    std::vector<glm::vec3> points = cubeCorners();
    for (int i = 0; i < 50; ++i) {
        const float t = static_cast<float>(i) * 0.1f;
        points.push_back(glm::vec3(std::sin(t) * 0.2f, std::cos(t) * 0.2f, std::sin(t * 2.0f) * 0.2f));
    }

    ConvexHull hull;
    CHECK(hull.Build(points));
    CHECK_MSG(hull.vertices().size() == 8, "fifty points inside a cube are still a cube");
    CHECK_EQ(hull.faces().size(), size_t{6});
}

// --- the support function SAT is built on -----------------------------------

void testSupportFindsTheFarthestCorner() {
    ConvexHull hull;
    CHECK(hull.Build(cubeCorners(1.0f)));

    CHECK(hull.Support(glm::vec3(1, 0, 0)).x > 0.99f);
    CHECK(hull.Support(glm::vec3(-1, 0, 0)).x < -0.99f);

    const glm::vec3 corner = hull.Support(glm::vec3(1, 1, 1));
    CHECK_MSG(corner.x > 0.99f && corner.y > 0.99f && corner.z > 0.99f,
              "a diagonal direction finds the corner, not a face centre");

    // The projection of the hull onto any axis is bounded by its two supports,
    // which is the only property SAT actually uses.
    const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, -0.8f, 0.5f));
    const float high = glm::dot(hull.Support(axis), axis);
    const float low = glm::dot(hull.Support(-axis), axis);
    int outside = 0;
    for (const glm::vec3& vertex : hull.vertices()) {
        const float along = glm::dot(vertex, axis);
        if (along > high + 1e-4f || along < low - 1e-4f) ++outside;
    }
    CHECK_MSG(outside == 0, "no vertex projects outside the two supports");
}

// --- the cap ----------------------------------------------------------------

void testTheVertexCapIsHonestAboutWhatItCost() {
    // Hull-against-hull SAT tests every pair of edges, so the cost is quadratic
    // in the edge count and an exact hull of a detailed mesh is one nobody can
    // afford to collide. The cap bounds it - and says by how much, rather than
    // leaving "the hull is slightly smaller than the mesh" to be discovered.
    std::vector<glm::vec3> ball;
    for (int i = 0; i < 400; ++i) {
        const float u = static_cast<float>(i) * 0.61803399f;
        const float theta = u * 6.2831853f;
        const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 400.0f;
        const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
        ball.push_back(glm::vec3(std::cos(theta) * radius, y, std::sin(theta) * radius));
    }

    ConvexHull hull;
    CHECK(hull.Build(ball));
    CHECK_MSG(hull.vertices().size() <= ConvexHull::kMaxVertices,
              "the cap is a cap");
    CHECK_MSG(hull.residual() > 0.0f,
              "four hundred points on a sphere do not fit in sixty-four, and it says so");
    CHECK_MSG(hull.residual() < 0.1f,
              "but farthest-first ordering keeps what is left small");
    CHECK(hull.IsConvex());
    CHECK(hull.SatisfiesEulerFormula());
}

void testAHullIsTheSameHullTwice() {
    // Determinism. A collider that came out differently on two runs would put
    // the recorded-input tests and the fixed-step replay out of step with each
    // other for reasons nobody could see.
    const std::vector<glm::vec3> points = cubeCorners(1.5f);

    ConvexHull first;
    ConvexHull second;
    CHECK(first.Build(points));
    CHECK(second.Build(points));

    CHECK_EQ(first.vertices().size(), second.vertices().size());
    CHECK_EQ(first.faces().size(), second.faces().size());

    int different = 0;
    for (size_t i = 0; i < first.vertices().size(); ++i) {
        if (first.vertices()[i] != second.vertices()[i]) ++different;
    }
    CHECK_MSG(different == 0, "the same points give the same hull, vertex for vertex");
}

// --- colliding one --------------------------------------------------------
//
// The DIFFERENTIAL check the rest of this file cannot give. A hull built from a
// cube's eight corners is an oriented box, and there is already a box path that
// is tested and trusted - so the hull path has to agree with it. That is a
// stronger statement than any expected value worked out by hand, because it
// compares against code whose own answers were checked against hand-worked
// numbers years of commits ago.

static CollisionSAT::Obb boxAt(const glm::vec3& centre, const glm::vec3& halfExtent,
                               const glm::mat3& axes = glm::mat3(1.0f)) {
    CollisionSAT::Obb box;
    box.centre = centre;
    box.halfExtent = halfExtent;
    box.axes = axes;
    return box;
}

static void compareWithTheBoxPath(const CollisionSAT::Obb& a, const CollisionSAT::Obb& b,
                                  const char* what) {
    const CollisionSAT::Manifold fromBoxes = CollisionSAT::CollideObbObb(a, b);
    const CollisionSAT::Manifold fromHulls = CollisionHull::CollideHullHull(
        CollisionHull::InstanceFromObb(a), CollisionHull::InstanceFromObb(b));

    CHECK_MSG(fromBoxes.colliding == fromHulls.colliding,
              std::string(what) + ": the two paths must agree that they touch at all");
    if (!fromBoxes.colliding || !fromHulls.colliding) return;

    CHECK_MSG(glm::dot(fromBoxes.normal, fromHulls.normal) > 0.99f,
              std::string(what) + ": and on which way to push");
    CHECK_MSG(test::nearly(fromBoxes.MaxPenetration(), fromHulls.MaxPenetration(), 1e-3f),
              std::string(what) + ": and on how far");
}

void testAHullOfACubeCollidesLikeABox() {
    // Face to face, which is the case a manifold has to produce four points for.
    compareWithTheBoxPath(boxAt(glm::vec3(0.0f), glm::vec3(0.5f)),
                          boxAt(glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.5f)),
                          "flat on flat");

    // Offset, so the clipped patch is a rectangle rather than the whole face.
    compareWithTheBoxPath(boxAt(glm::vec3(0.0f), glm::vec3(0.5f)),
                          boxAt(glm::vec3(0.35f, 0.9f, -0.2f), glm::vec3(0.5f)),
                          "flat on flat, offset");

    // A corner into a face.
    const glm::mat3 tilted = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.6f,
                                                   glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))));
    compareWithTheBoxPath(boxAt(glm::vec3(0.0f), glm::vec3(0.5f)),
                          boxAt(glm::vec3(0.1f, 1.0f, 0.05f), glm::vec3(0.5f), tilted),
                          "corner into a face");

    // Apart, which both must agree about.
    compareWithTheBoxPath(boxAt(glm::vec3(0.0f), glm::vec3(0.5f)),
                          boxAt(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.5f)),
                          "nowhere near");

    // Different sizes, so neither path can be right by symmetry alone.
    compareWithTheBoxPath(boxAt(glm::vec3(0.0f), glm::vec3(2.0f, 0.25f, 2.0f)),
                          boxAt(glm::vec3(0.3f, 0.6f, -0.4f), glm::vec3(0.4f)),
                          "a crate on a slab");
}

void testAFaceContactIsAPatchAndNotAPoint() {
    // Four points, not one. A crate held by a single spot in its own footprint
    // is free to rotate about it, and does - which is the whole reason a
    // manifold carries more than one point.
    const CollisionSAT::Manifold hit = CollisionHull::CollideHullHull(
        CollisionHull::InstanceFromObb(boxAt(glm::vec3(0.0f), glm::vec3(0.5f))),
        CollisionHull::InstanceFromObb(boxAt(glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.5f))));

    CHECK(hit.colliding);
    CHECK_MSG(hit.pointCount == 4,
              "a flat face resting on a flat face is a patch, and a merged hull face "
              "is what makes it one - a triangulated hull gives a sliver");
    CHECK(nearlyVec(hit.normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK_NEAR(hit.MaxPenetration(), 0.1f);
}

void testTheReferenceFaceIsTheOneFacingTheOtherShape() {
    // A hull has TWO faces for every axis - the top and the bottom of a slab
    // both answer to Y - and the axis search flips a normal to point from one
    // shape toward the other, so the face that won the axis may be the one
    // facing away.
    //
    // Trusting it put the reference plane at the BOTTOM of a slab: every
    // clipped point measured a metre behind it, all of them were dropped, and a
    // crate fell through a floor the SAT had correctly reported it was standing
    // on. The pair of hulls has to be UNEVEN for it to show - a centred unit
    // cube against another has its faces in an order that happens to be right.
    ConvexHull cube;
    CHECK(cube.Build(cubeCorners(0.5f)));

    glm::mat3 slabBasis(1.0f);
    slabBasis[0] = glm::vec3(6.0f, 0.0f, 0.0f);
    slabBasis[2] = glm::vec3(0.0f, 0.0f, 6.0f);

    CollisionHull::Instance slab;
    CHECK(CollisionHull::MakeInstance(cube, glm::vec3(0.0f), slabBasis, slab));

    CollisionHull::Instance crate;
    CHECK(CollisionHull::MakeInstance(cube, glm::vec3(0.0f, 0.9f, 0.0f), glm::mat3(1.0f), crate));

    // BOTH orders. The broadphase sorts its proxies, so which of the two is A
    // is not something the narrowphase gets to choose - and the bug only
    // appeared in one of them.
    const CollisionSAT::Manifold upward = CollisionHull::CollideHullHull(slab, crate);
    CHECK(upward.colliding);
    CHECK_MSG(upward.pointCount == 4,
              "a crate on a slab is a four-point face contact, not an empty manifold");
    CHECK(nearlyVec(upward.normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK_NEAR(upward.MaxPenetration(), 0.1f);

    const CollisionSAT::Manifold downward = CollisionHull::CollideHullHull(crate, slab);
    CHECK(downward.colliding);
    CHECK_MSG(downward.pointCount == 4, "and the same pair the other way round");
    CHECK(nearlyVec(downward.normal, glm::vec3(0.0f, -1.0f, 0.0f)));
    CHECK_NEAR(downward.MaxPenetration(), 0.1f);
}

void testAWedgeRestsOnItsSlope() {
    // The shape hulls exist for. A ramp is one convex solid and was three or
    // four boxes that never quite fit.
    std::vector<glm::vec3> wedge = {
        glm::vec3(-1, 0, -1), glm::vec3(1, 0, -1), glm::vec3(1, 0, 1), glm::vec3(-1, 0, 1),
        glm::vec3(-1, 1, -1), glm::vec3(-1, 1, 1),
    };
    ConvexHull hull;
    CHECK(hull.Build(wedge));

    CollisionHull::Instance placed;
    CHECK(CollisionHull::MakeInstance(hull, glm::vec3(0.0f), glm::mat3(1.0f), placed));

    // A ball resting on the sloped face.
    //
    // The face runs from the top edge at x = -1, y = 1 down to the bottom edge
    // at x = +1, y = 0 - a fall of one over a run of two - so its normal is
    // (1, 2, 0) normalised and NOT the forty-five degrees the corner points
    // suggest at a glance.
    const glm::vec3 slope = glm::normalize(glm::vec3(1.0f, 2.0f, 0.0f));
    const glm::vec3 surface(0.0f, 0.5f, 0.0f);
    const glm::vec3 centre = surface + slope * 0.4f;

    const CollisionSAT::Manifold hit =
        CollisionHull::CollideCapsuleHull(centre, centre, 0.5f, placed);

    CHECK_MSG(hit.colliding, "a ball on the slope is touching it");
    CHECK_MSG(glm::dot(hit.normal, slope) > 0.9f,
              "and is held along the SLOPE rather than straight up, which is the whole "
              "difference between a wedge and the box that contains it");
    CHECK_NEAR(hit.MaxPenetration(), 0.1f);
}

void testAHullSquashedOnOneAxisCollidesAsWhatItLooksLike() {
    // The one collider for which a non-uniform scale is exact. A sphere has to
    // collapse to one radius and a capsule to one; a linear transform of a
    // convex set is still convex, so a hull simply is the shape it is drawn as.
    ConvexHull cube;
    CHECK(cube.Build(cubeCorners(1.0f)));

    glm::mat3 squashed(1.0f);
    squashed[1] = glm::vec3(0.0f, 0.25f, 0.0f);   // a quarter as tall

    CollisionHull::Instance placed;
    CHECK(CollisionHull::MakeInstance(cube, glm::vec3(0.0f), squashed, placed));

    // Its top is now at 0.25, not 1. A ball whose bottom is at 0.3 misses it...
    const glm::vec3 high(0.0f, 0.8f, 0.0f);
    CHECK_MSG(!CollisionHull::CollideCapsuleHull(high, high, 0.5f, placed).colliding,
              "a squashed hull does not collide as the box it used to be");

    // ...and one whose bottom is at 0.2 does not.
    const glm::vec3 low(0.0f, 0.7f, 0.0f);
    const CollisionSAT::Manifold hit =
        CollisionHull::CollideCapsuleHull(low, low, 0.5f, placed);
    CHECK(hit.colliding);
    CHECK_MSG(nearlyVec(hit.normal, glm::vec3(0.0f, 1.0f, 0.0f)),
              "and its face normal still points out of the face, which is what the "
              "inverse transpose is for");
}

void testACapsuleLyingOnAFaceIsHeldAtBothEnds() {
    ConvexHull cube;
    CHECK(cube.Build(cubeCorners(1.0f)));
    CollisionHull::Instance placed;
    CHECK(CollisionHull::MakeInstance(cube, glm::vec3(0.0f), glm::mat3(1.0f), placed));

    // Lying across the top face, well inside it.
    const CollisionSAT::Manifold hit = CollisionHull::CollideCapsuleHull(
        glm::vec3(-0.6f, 1.4f, 0.0f), glm::vec3(0.6f, 1.4f, 0.0f), 0.5f, placed);

    CHECK(hit.colliding);
    CHECK_MSG(hit.pointCount == 2,
              "held at both ends, or it rocks end over end about a single point in "
              "the middle with nothing anywhere else to resist");
    CHECK(nearlyVec(hit.normal, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void testABodyInsideAHullIsPushedOut() {
    ConvexHull cube;
    CHECK(cube.Build(cubeCorners(1.0f)));
    CollisionHull::Instance placed;
    CHECK(CollisionHull::MakeInstance(cube, glm::vec3(0.0f), glm::mat3(1.0f), placed));

    // Nearest the +y face, so that is the way out.
    const glm::vec3 buried(0.0f, 0.7f, 0.0f);
    bool inside = false;
    const glm::vec3 surface = CollisionHull::ClosestPointOnHull(placed, buried, inside);
    CHECK_MSG(inside, "a point within the hull knows it is within the hull");
    CHECK_MSG(nearlyVec(surface, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f),
              "and the way out is the face it is nearest to leaving through");

    const CollisionSAT::Manifold hit =
        CollisionHull::CollideCapsuleHull(buried, buried, 0.25f, placed);
    CHECK(hit.colliding);
    CHECK_MSG(glm::dot(hit.normal, glm::vec3(0.0f, 1.0f, 0.0f)) > 0.9f, "pushed upwards");
    CHECK_MSG(hit.MaxPenetration() > 0.25f, "by more than its own radius, since it is inside");
}

void testCollidingAgainstNothingIsNotACrash() {
    // An invalid hull is what a collider built on a flat sheet or a handful of
    // points comes out as, and it has to collide with nothing rather than read
    // past the end of an empty face list.
    ConvexHull empty;
    CHECK(!empty.Build({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}));

    CollisionHull::Instance broken;
    CHECK_MSG(!CollisionHull::MakeInstance(empty, glm::vec3(0.0f), glm::mat3(1.0f), broken),
              "an invalid hull cannot be placed");

    CHECK(!CollisionHull::CollideHullHull(broken, broken).colliding);
    CHECK(!CollisionHull::CollideCapsuleHull(glm::vec3(0.0f), glm::vec3(0.0f), 1.0f,
                                             broken).colliding);

    // And a basis with no volume, which is a collider scaled to nothing.
    ConvexHull cube;
    CHECK(cube.Build(cubeCorners()));
    glm::mat3 flattened(1.0f);
    flattened[1] = glm::vec3(0.0f);
    CollisionHull::Instance squashed;
    CHECK_MSG(!CollisionHull::MakeInstance(cube, glm::vec3(0.0f), flattened, squashed),
              "a hull with no thickness has no outside to be on");
}

void runTests() {
    testTooFewPointsIsNotAHull();
    testCollinearPointsAreNotASolid();
    testCoplanarPointsAreNotASolid();
    testCoincidentPointsAreOnePoint();
    testAHullOfNonsenseIsNotAHull();

    testACubeComesBackAsSixFaces();
    testTheStructuralInvariantsHold();
    testAHullContainsThePointsItWasBuiltFrom();
    testInteriorPointsChangeNothing();

    testSupportFindsTheFarthestCorner();

    testAHullOfACubeCollidesLikeABox();
    testAFaceContactIsAPatchAndNotAPoint();
    testTheReferenceFaceIsTheOneFacingTheOtherShape();
    testAWedgeRestsOnItsSlope();
    testAHullSquashedOnOneAxisCollidesAsWhatItLooksLike();
    testACapsuleLyingOnAFaceIsHeldAtBothEnds();
    testABodyInsideAHullIsPushedOut();
    testCollidingAgainstNothingIsNotACrash();

    testTheVertexCapIsHonestAboutWhatItCost();
    testAHullIsTheSameHullTwice();
}

} // namespace

TEST_MAIN("test_convexhull", 85)
