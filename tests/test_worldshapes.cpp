// The world-space shape buffer.
//
// The engine's other shape layer draws into an ImGui list over the finished
// image, which makes its circles screen circles and its sizes fractions of the
// screen. That is right for a 2D game with a fixed camera and cannot express
// three things a 3D game needs: world units, perspective, and depth.
//
// This suite covers the part that is arithmetic - the tessellation and the
// accumulation. What it deliberately does not cover is the pixels, which need a
// device.

#include "TestHarness.hpp"
#include "core/WorldShapes.hpp"

#include <cmath>
#include <vector>

using namespace Supersonic;

namespace {

bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
           std::fabs(a.z - b.z) <= eps;
}

void testALineIsTwoVerticesAndNothingElse() {
    WorldShapes shapes;
    CHECK_MSG(shapes.Empty(), "a fresh buffer is empty");

    shapes.AddLine(glm::vec3(0.0f), glm::vec3(1.0f, 2.0f, 3.0f), glm::vec4(1.0f));

    CHECK_EQ(static_cast<int>(shapes.Vertices().size()), 2);
    CHECK_EQ(static_cast<int>(shapes.LineCount()), 1);
    CHECK_MSG(near3(shapes.Vertices()[0].position, glm::vec3(0.0f)), "from");
    CHECK_MSG(near3(shapes.Vertices()[1].position, glm::vec3(1.0f, 2.0f, 3.0f)), "to");
}

void testACircleClosesExactlyRatherThanNearly() {
    // The last point IS the first point, reused rather than recomputed. Working
    // it out twice from an angle leaves a hairline gap that only shows at large
    // radii - and reads as a rendering fault rather than a missing segment.
    WorldShapes shapes;
    shapes.AddCircle(glm::vec3(10.0f, 0.0f, -4.0f), 3.0f, glm::vec4(1.0f), 8);

    CHECK_EQ(static_cast<int>(shapes.LineCount()), 8);

    const auto& v = shapes.Vertices();
    CHECK_MSG(v.front().position == v.back().position,
              "the ring closes on the exact same float, not on a near one");
}

void testEveryPointOfACircleIsOnTheCircle() {
    WorldShapes shapes;
    const glm::vec3 centre(5.0f, 2.0f, -1.0f);
    const float radius = 7.0f;
    shapes.AddCircle(centre, radius, glm::vec4(1.0f), 24);

    for (const WorldShapes::Vertex& vertex : shapes.Vertices()) {
        const float distance = glm::length(vertex.position - centre);
        CHECK_MSG(std::fabs(distance - radius) < 1e-3f, "every vertex is exactly one radius out");
    }
}

void testAGroundCircleStaysOnTheGround() {
    // The default normal is up, which is where nearly every one of these goes -
    // a range ring, a selection marker, an area of effect. A basis that seeded
    // its tangent with a fixed up vector would collapse for exactly this case,
    // because the cross product of up with up is zero.
    WorldShapes shapes;
    shapes.AddCircle(glm::vec3(0.0f, 4.0f, 0.0f), 2.0f, glm::vec4(1.0f), 16);

    CHECK_MSG(!shapes.Empty(), "the ground circle produced geometry at all");
    for (const WorldShapes::Vertex& vertex : shapes.Vertices()) {
        CHECK_MSG(std::fabs(vertex.position.y - 4.0f) < 1e-4f,
                  "a circle on the ground plane keeps its height");
    }
}

void testACircleInAVerticalPlaneAlsoWorks() {
    // The other side of the same trap: a normal along x must not collapse
    // either, and it is what a wall decal or a portal ring needs.
    WorldShapes shapes;
    shapes.AddCircle(glm::vec3(0.0f), 1.0f, glm::vec4(1.0f), 16, glm::vec3(1.0f, 0.0f, 0.0f));

    CHECK_MSG(!shapes.Empty(), "a circle with a horizontal normal produced geometry");
    for (const WorldShapes::Vertex& vertex : shapes.Vertices()) {
        CHECK_MSG(std::fabs(vertex.position.x) < 1e-4f, "and stays in the plane it was given");
        CHECK_MSG(std::fabs(glm::length(vertex.position) - 1.0f) < 1e-3f, "at the right radius");
    }
}

void testTooFewSegmentsBecomeARingRatherThanNothing() {
    // Two segments would be a line drawn twice and zero would be a silent
    // nothing. Three is the fewest that is a ring.
    WorldShapes shapes;
    shapes.AddCircle(glm::vec3(0.0f), 1.0f, glm::vec4(1.0f), 0);
    CHECK_EQ(static_cast<int>(shapes.LineCount()), 3);

    WorldShapes negative;
    negative.AddCircle(glm::vec3(0.0f), 1.0f, glm::vec4(1.0f), -5);
    CHECK_EQ(static_cast<int>(negative.LineCount()), 3);
}

void testAGroundRectIsFourClosedEdgesAtOneHeight() {
    WorldShapes shapes;
    shapes.AddGroundRect(glm::vec3(-1.0f, 3.0f, -2.0f), glm::vec3(5.0f, 99.0f, 6.0f),
                         glm::vec4(1.0f));

    CHECK_EQ(static_cast<int>(shapes.LineCount()), 4);

    // The FIRST corner's height for all four, so a rectangle given corners at
    // different heights is still flat. A caller wanting a quadrilateral in
    // space wants four lines rather than this.
    for (const WorldShapes::Vertex& vertex : shapes.Vertices()) {
        CHECK_MSG(std::fabs(vertex.position.y - 3.0f) < 1e-4f, "flat, at the first corner's height");
    }
}

void testABoxIsTwelveEdges() {
    WorldShapes shapes;
    shapes.AddBox(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec4(1.0f));
    CHECK_EQ(static_cast<int>(shapes.LineCount()), 12);
}

void testClearEmptiesTheBufferAndTheDropCount() {
    WorldShapes shapes;
    shapes.AddBox(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec4(1.0f));
    CHECK_MSG(!shapes.Empty(), "something was added");

    shapes.Clear();
    CHECK_MSG(shapes.Empty(), "and cleared");
    CHECK_EQ(static_cast<int>(shapes.LineCount()), 0);
}

void testThePaintIsCarriedPerVertex() {
    WorldShapes shapes;
    const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 green(0.0f, 1.0f, 0.0f, 0.5f);

    shapes.AddLine(glm::vec3(0.0f), glm::vec3(1.0f), red);
    shapes.AddLine(glm::vec3(0.0f), glm::vec3(1.0f), green);

    CHECK_MSG(shapes.Vertices()[0].color == red, "the first line kept its colour");
    CHECK_MSG(shapes.Vertices()[2].color == green, "and the second its own");
    CHECK_MSG(std::fabs(shapes.Vertices()[3].color.a - 0.5f) < 1e-4f,
              "alpha travels with it, so a fading ring can fade");
}

void testPastTheCeilingShapesAreDroppedRatherThanGrowingForever() {
    // An immediate-mode API invites a loop that forgot its bound. The useful
    // failure is a visibly truncated overlay at a steady frame rate, not an
    // allocation spike nobody attributes to anything.
    WorldShapes shapes;

    const size_t linesToOverflow = WorldShapes::kMaxVertices;   // twice what fits
    for (size_t i = 0; i < linesToOverflow; ++i) {
        shapes.AddLine(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(1.0f));
    }

    CHECK_MSG(shapes.Vertices().size() <= WorldShapes::kMaxVertices,
              "the buffer never grows past its ceiling");
    CHECK_MSG(shapes.DroppedVertices() > 0, "and says that it dropped work");

    // A SHAPE is dropped whole rather than half-drawn. A circle that ran out of
    // room mid-ring would leave a stray arc that looks like a real effect.
    CHECK_MSG(shapes.Vertices().size() % 2 == 0, "no half a line survives the ceiling");
}

void testAShapeThatDoesNotFitIsRefusedWholeRatherThanTruncated() {
    WorldShapes shapes;

    // Fill to just under the ceiling, leaving room for two lines.
    while (shapes.Vertices().size() + 4 <= WorldShapes::kMaxVertices) {
        shapes.AddLine(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(1.0f));
    }
    const size_t before = shapes.Vertices().size();

    // A box needs twelve lines and cannot fit. None of it should appear.
    shapes.AddBox(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec4(1.0f));

    CHECK_MSG(shapes.Vertices().size() == before,
              "a shape too big for the remaining room adds nothing at all");
    CHECK_MSG(shapes.DroppedVertices() >= 24, "and reports the whole shape as dropped");
}

} // namespace

static void runTests() {
    testALineIsTwoVerticesAndNothingElse();
    testACircleClosesExactlyRatherThanNearly();
    testEveryPointOfACircleIsOnTheCircle();
    testAGroundCircleStaysOnTheGround();
    testACircleInAVerticalPlaneAlsoWorks();
    testTooFewSegmentsBecomeARingRatherThanNothing();
    testAGroundRectIsFourClosedEdgesAtOneHeight();
    testABoxIsTwelveEdges();
    testClearEmptiesTheBufferAndTheDropCount();
    testThePaintIsCarriedPerVertex();
    testPastTheCeilingShapesAreDroppedRatherThanGrowingForever();
    testAShapeThatDoesNotFitIsRefusedWholeRatherThanTruncated();
}

TEST_MAIN("test_worldshapes", 60)
