// Breaking a concave shape into convex pieces.
//
// A hull is convex by definition, so a doughnut collided as a disc and a chair
// as the block it sits in. The fix is several hulls - and the reason this suite
// exists at all is that "is a decomposition correct" is normally a matter of
// opinion, which this project does not accept as evidence.
//
// It is not a matter of opinion here, and that is a consequence of the method
// rather than of the tests. Splitting a CLOSED mesh by a plane and clipping the
// triangles leaves both halves closed, so each piece encloses a volume, the
// pieces are interior-disjoint, and their volumes therefore ADD. Two exact
// inequalities follow:
//
//     sum of the pieces >= the mesh          nothing was dropped
//     sum of the pieces <= the convex hull   nothing was invented that the
//                                            single hull did not already have
//
// And on the L below both bounds are met EXACTLY, with numbers derived by hand
// from the shape rather than read off the implementation: the L has volume 3,
// its convex hull has volume 3.5, and a correct split gives 2 and 1.

#include "core/ConvexDecomposition.hpp"
#include "TestHarness.hpp"

#include <cmath>
#include <vector>

using namespace Supersonic;

namespace {

// The L, as a closed prism of unit thickness.
//
// Plan, counter-clockwise: (0,0) (2,0) (2,1) (1,1) (1,2) (0,2), plus (0,1) on
// the last edge so the top and bottom faces tile as two rectangles. Area is
// 2x1 plus 1x1 = 3, and the convex hull drops the reflex corner at (1,1) for a
// pentagon of area 3.5 by the shoelace formula - half a unit of solid that is
// not there, which is exactly what a single hull collider would catch on.
struct Mesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
};

Mesh makeLPrism(float thickness = 1.0f) {
    const glm::vec2 plan[7] = {
        {0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 1.0f}, {1.0f, 1.0f},
        {1.0f, 2.0f}, {0.0f, 2.0f}, {0.0f, 1.0f},
    };
    constexpr uint32_t n = 7;

    Mesh mesh;
    for (uint32_t i = 0; i < n; ++i) mesh.positions.push_back(glm::vec3(plan[i].x, 0.0f, plan[i].y));
    for (uint32_t i = 0; i < n; ++i) {
        mesh.positions.push_back(glm::vec3(plan[i].x, thickness, plan[i].y));
    }

    const auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
    };

    // Bottom (y = 0), then top (y = thickness), wound opposite ways.
    //
    // Ear-clipped rather than cut into two rectangles. The obvious rectangle
    // pair shares only PART of an edge - (2,6) against (6,3) - which is a
    // T-junction: geometrically closed, topologically not, so every edge no
    // longer has exactly one partner and IsClosed rejects it. It cost a test
    // failure to notice, which is the test doing its job.
    const uint32_t fan[5][3] = {
        {0, 1, 2}, {0, 2, 3}, {0, 3, 6}, {6, 3, 4}, {6, 4, 5},
    };
    for (const auto& f : fan) {
        tri(f[0], f[2], f[1]);                       // bottom, facing down
        tri(n + f[0], n + f[1], n + f[2]);           // top, facing up
    }

    // The walls, one quad per boundary edge.
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = (i + 1) % n;
        tri(i, j, n + j);
        tri(i, n + j, n + i);
    }
    return mesh;
}

Mesh makeBox(const glm::vec3& half) {
    Mesh mesh;
    for (int i = 0; i < 8; ++i) {
        mesh.positions.push_back(glm::vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y,
                                           (i & 4) ? half.z : -half.z));
    }
    const uint32_t faces[6][4] = {
        {0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1},
        {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5},
    };
    for (const auto& f : faces) {
        mesh.indices.push_back(f[0]); mesh.indices.push_back(f[1]); mesh.indices.push_back(f[2]);
        mesh.indices.push_back(f[0]); mesh.indices.push_back(f[2]); mesh.indices.push_back(f[3]);
    }
    return mesh;
}

bool insideAnyPiece(const ConvexDecomposition& decomposition, const glm::vec3& point) {
    for (const ConvexHull& piece : decomposition.pieces()) {
        bool inside = true;
        for (const ConvexHull::Face& face : piece.faces()) {
            if (glm::dot(face.normal, point) > face.offset + 1.0e-3f) {
                inside = false;
                break;
            }
        }
        if (inside) return true;
    }
    return false;
}

// --- the preconditions every number below rests on -------------------------

void testTheFixturesAreClosedAndTheRightSize() {
    // An open mesh has no volume, so a concavity measured on one is a number
    // with no meaning - and it will still look like a number.
    const Mesh l = makeLPrism();
    CHECK_MSG(IsClosed(l.indices), "the L must be watertight, or every number below means nothing");
    CHECK_NEAR(std::fabs(SignedVolume(l.positions, l.indices)), 3.0f);

    const Mesh box = makeBox(glm::vec3(0.5f));
    CHECK_MSG(IsClosed(box.indices), "and so must the box");
    CHECK_NEAR(std::fabs(SignedVolume(box.positions, box.indices)), 1.0f);

    // A mesh missing a face is NOT closed, or the check above is decoration.
    Mesh open = makeBox(glm::vec3(0.5f));
    open.indices.resize(open.indices.size() - 6);
    CHECK_MSG(!IsClosed(open.indices), "a mesh with a hole in it must not pass as closed");
}

void testTheConvexHullOfTheLInventsExactlyHalfAUnit() {
    // The number the whole suite turns on, and it comes from the shoelace
    // formula over the pentagon (0,0) (2,0) (2,1) (1,2) (0,2), not from the
    // code: area 7/2, times unit thickness.
    const Mesh l = makeLPrism();
    ConvexHull whole;
    CHECK_MSG(whole.Build(l.positions), "the L must have a hull");
    CHECK_NEAR(HullVolume(whole), 3.5f);
}

// --- what a decomposition has to do ----------------------------------------

void testAnAlreadyConvexShapeIsOnePieceAndIsItsHull() {
    // The differential check, against code already trusted. A box is convex, so
    // a decomposition that returns anything other than its own hull has
    // invented structure that is not there.
    const Mesh box = makeBox(glm::vec3(0.5f, 0.25f, 0.75f));

    ConvexDecomposition decomposition;
    CHECK_MSG(decomposition.Build(box.positions, box.indices), "the box must decompose");
    CHECK_EQ(decomposition.pieces().size(), size_t{1});

    ConvexHull whole;
    CHECK(whole.Build(box.positions));
    CHECK_NEAR(HullVolume(decomposition.pieces().front()), HullVolume(whole));
    CHECK_MSG(decomposition.invented() < 1.0e-3f,
              "a convex shape invents nothing, so the number an author reads must be zero");
}

void testTheLSplitsIntoTwoBoxesWithNothingInvented() {
    // The oracle. Every figure is derived from the shape by hand: the L has
    // volume 3, its hull 3.5, and the axis-aligned split at x = 1 gives a 1x2
    // slab and a 1x1 cube - both convex, so the pieces are their own hulls and
    // the sum is exactly 3 with nothing invented at all.
    const Mesh l = makeLPrism();

    ConvexDecomposition decomposition;
    CHECK_MSG(decomposition.Build(l.positions, l.indices), "the L must decompose");
    CHECK_MSG(decomposition.pieces().size() >= 2,
              "a shape with a reflex corner cannot be one convex piece");

    float sum = 0.0f;
    bool withinCap = true;
    for (const ConvexHull& piece : decomposition.pieces()) {
        sum += HullVolume(piece);
        withinCap = withinCap && piece.residual() == 0.0f;
    }
    CHECK_MSG(withinCap, "no piece hit the vertex cap, which is what makes the bounds below hold");

    // Nothing dropped: the pieces cover the mesh.
    CHECK_MSG(sum >= 3.0f - 1.0e-3f, "the pieces must cover the mesh - no hole to fall through");
    // Nothing invented beyond what one hull already had.
    CHECK_MSG(sum <= 3.5f + 1.0e-3f, "and invent no solid the single hull did not already have");
    // And on this shape the lower bound is not merely met, it is exact.
    CHECK_MSG(std::fabs(sum - 3.0f) < 5.0e-3f,
              "an axis-aligned L splits into an axis-aligned pair with nothing invented");

    CHECK_MSG(decomposition.invented() < 5.0e-3f,
              "so the invented volume an author reads must be about zero");
}

void testTheNotchIsEmptyForThePiecesAndSolidForOneHull() {
    // The entire feature, as one point in space.
    //
    // (1.3, 1.3) in plan is in the corner the L does not occupy. The convex
    // hull of the L fills it - that is the bug: a doughnut collides as a disc
    // and a chair as the block it sits in. The pieces must leave it empty.
    const Mesh l = makeLPrism();
    const glm::vec3 inTheNotch(1.3f, 0.5f, 1.3f);

    ConvexHull whole;
    CHECK(whole.Build(l.positions));

    bool insideHull = true;
    for (const ConvexHull::Face& face : whole.faces()) {
        if (glm::dot(face.normal, inTheNotch) > face.offset + 1.0e-4f) {
            insideHull = false;
            break;
        }
    }
    CHECK_MSG(insideHull, "one hull fills the notch, which is the behaviour being fixed");

    ConvexDecomposition decomposition;
    CHECK(decomposition.Build(l.positions, l.indices));
    CHECK_MSG(!insideAnyPiece(decomposition, inTheNotch),
              "and the pieces must leave it empty, or nothing was gained");

    // The other half of the statement: a point that IS in the L must still be
    // solid, or a decomposition that returns nothing passes the check above.
    CHECK_MSG(insideAnyPiece(decomposition, glm::vec3(1.5f, 0.5f, 0.5f)),
              "a point inside the L must still be inside a piece");
}

void testNoInputPointFallsBetweenTwoPieces() {
    // The guard against the specific bug a clipper has: a vertex lying exactly
    // on the split plane assigned to neither side. Contentless on its own - one
    // hull satisfies it perfectly - which is why it sits beside the volume
    // bounds rather than instead of them.
    const Mesh l = makeLPrism();

    ConvexDecomposition decomposition;
    CHECK(decomposition.Build(l.positions, l.indices));

    int uncovered = 0;
    for (const glm::vec3& p : l.positions) {
        if (!insideAnyPiece(decomposition, p)) ++uncovered;
    }
    CHECK_MSG(uncovered == 0, "every vertex of the mesh must be inside some piece");
}

void testThePieceCountIsCapped() {
    // The recursion is driven by a measure a pathological mesh can keep
    // failing, so the cap is the thing that stops it, not the measure.
    const Mesh l = makeLPrism();

    ConvexDecomposition::Options options;
    options.concavityFraction = 0.0f;   // never satisfied
    options.maxPieces = 4;

    ConvexDecomposition decomposition;
    CHECK(decomposition.Build(l.positions, l.indices, options));
    CHECK_MSG(decomposition.pieces().size() <= size_t{4},
              "an impossible concavity target must still terminate at the cap");
}

void testGarbageInIsRefusedRatherThanGuessed() {
    ConvexDecomposition decomposition;
    CHECK_MSG(!decomposition.Build({}, {}), "nothing decomposes to nothing");
    CHECK_MSG(!decomposition.Build({glm::vec3(0.0f)}, {0, 0, 0}),
              "a degenerate triangle is not a solid");
}

void runTests() {
    testTheFixturesAreClosedAndTheRightSize();
    testTheConvexHullOfTheLInventsExactlyHalfAUnit();
    testAnAlreadyConvexShapeIsOnePieceAndIsItsHull();
    testTheLSplitsIntoTwoBoxesWithNothingInvented();
    testTheNotchIsEmptyForThePiecesAndSolidForOneHull();
    testNoInputPointFallsBetweenTwoPieces();
    testThePieceCountIsCapped();
    testGarbageInIsRefusedRatherThanGuessed();
}

} // namespace

TEST_MAIN("test_decomposition", 26)
