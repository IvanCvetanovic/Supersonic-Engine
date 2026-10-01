// Regression tests for tilemaps.
//
// A tilemap fails legibly, which is the worst way to fail: the map draws, in
// the right place, with the wrong tiles in it. A row counted from the bottom
// draws the level upside down and looks like the author's mistake. A flip that
// swaps the wrong axis mirrors a staircase into a ramp. A texture corner off by
// one atlas row shows the tile below the one asked for. None of those is an
// error anyone attributes to the engine, so each is pinned here as a number.
//
// And the half that is not geometry: which maps need uploading, which need
// replacing and which should stop drawing. A MeshRegistry needs a device and no
// suite can build one, so the sync runs here against a fake that records what
// it was asked to do - the same arrangement the resource signature and the
// shadow cache use.

#include "TestHarness.hpp"

#include "core/ComponentCodec.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"
#include "core/StateHash.hpp"
#include "core/TilemapSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

TilemapComponent grid(uint32_t width, uint32_t height, uint32_t atlasColumns = 4,
                      uint32_t atlasRows = 4) {
    TilemapComponent map;
    map.atlasColumns = atlasColumns;
    map.atlasRows = atlasRows;
    map.Resize(width, height);
    return map;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool near(const glm::vec2& a, const glm::vec2& b) { return near(a.x, b.x) && near(a.y, b.y); }
bool near(const glm::vec3& a, const glm::vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

// The one quad a single-cell bake produces, by corner. GenerateQuad's order:
// bottom-left, bottom-right, top-right, top-left.
struct Quad {
    Vertex bottomLeft, bottomRight, topRight, topLeft;
};

Quad quadOf(const MeshData& mesh, size_t index) {
    const size_t base = index * 4;
    return Quad{mesh.vertices[base], mesh.vertices[base + 1], mesh.vertices[base + 2],
                mesh.vertices[base + 3]};
}

// Stands in for MeshRegistry. Records every call so a test can say not only
// what state the registry ended in but what it cost to get there.
struct FakeMeshes {
    std::map<std::string, uint32_t> lookup;
    uint32_t nextId{7};   // not zero: zero is the cube in the real one
    std::vector<std::string> calls;

    uint32_t Find(const std::string& key) const {
        const auto it = lookup.find(key);
        return it == lookup.end() ? TilemapComponent::kNoMesh : it->second;
    }
    uint32_t Upload(const std::string& key, const MeshData& data) {
        calls.push_back("upload " + key + " " + std::to_string(data.indices.size() / 6));
        const uint32_t id = nextId++;
        lookup[key] = id;
        return id;
    }
    bool Replace(uint32_t id, const MeshData& data) {
        calls.push_back("replace " + std::to_string(id) + " " +
                        std::to_string(data.indices.size() / 6));
        return true;
    }

    // No Invalidate on purpose. The sync must never drop a map's mesh: the
    // registry never recycles an id, so dropping and re-uploading would grow
    // it by a slot per empty-and-refill. A Sync that reached for one would
    // fail to compile against this fake, which is the point.

    // Never indexes past the end. A mutation that drops a call leaves the
    // list one short, and a test that then read `calls[1]` would abort on
    // the debug runtime's bounds check rather than fail on its own message -
    // which is a catch, but the wrong kind: it reads as a crash in the code
    // under test and prints nothing about what was expected.
    std::string call(size_t index) const {
        return index < calls.size() ? calls[index] : "(no such call)";
    }
    std::string last() const { return calls.empty() ? "(no calls)" : calls.back(); }
};

} // namespace

// --- the component -----------------------------------------------------------

static void testAFreshMapIsEmptyEverywhereAndDrawsNothing() {
    TilemapComponent map;
    CHECK_EQ(map.cellCount(), 256u);
    CHECK_MSG(map.cells.empty(), "no allocation until something is written");
    CHECK_MSG(TilemapComponent::IsEmpty(map.At(0, 0)), "an unwritten cell reads as empty");
    CHECK_MSG(TilemapComponent::IsEmpty(map.At(99, 99)), "and so does one outside the map");

    MeshData mesh;
    CHECK_MSG(!TilemapSystem::Bake(map, mesh), "nothing to draw is a false, not a mesh");
    CHECK_MSG(mesh.empty(), "and the output is left empty, never the cube");
}

static void testSetGrowsTheVectorAndRefusesTheOutside() {
    TilemapComponent map;
    CHECK(map.Set(3, 2, 5));
    CHECK_EQ(map.cells.size(), size_t{256});
    CHECK_EQ(map.At(3, 2), 5);
    CHECK_MSG(!map.Set(16, 0, 1), "column 16 of a 16-wide map is outside it");
    CHECK_MSG(!map.Set(0, 16, 1), "and so is row 16");
}

static void testACellPacksItsIndexAndFlips() {
    const int32_t plain = TilemapComponent::MakeCell(12);
    CHECK_EQ(TilemapComponent::AtlasIndex(plain), 12u);
    CHECK(!TilemapComponent::FlipH(plain));
    CHECK(!TilemapComponent::FlipV(plain));
    CHECK(!TilemapComponent::IsEmpty(plain));

    const int32_t both = TilemapComponent::MakeCell(12, true, true);
    CHECK_EQ(TilemapComponent::AtlasIndex(both), 12u);
    CHECK(TilemapComponent::FlipH(both));
    CHECK(TilemapComponent::FlipV(both));
    CHECK_MSG(both > 0, "a flipped cell is still a non-negative number, so still one integer in a file");

    CHECK_MSG(!TilemapComponent::FlipH(TilemapComponent::kEmpty),
              "an empty cell has no flips, whatever its bits say");
}

static void testResizeKeepsWhatStillFits() {
    TilemapComponent map = grid(4, 3);
    map.Set(0, 0, 1);
    map.Set(3, 0, 2);   // last column, will be cut
    map.Set(1, 2, 3);   // last row, will be cut
    map.Set(2, 1, 4);   // survives

    map.Resize(3, 2);
    CHECK_EQ(map.width, 3u);
    CHECK_EQ(map.height, 2u);
    CHECK_EQ(map.cells.size(), size_t{6});
    CHECK_EQ(map.At(0, 0), 1);
    CHECK_MSG(map.At(2, 1) == 4, "a cell inside both sizes is where it was, not shifted by the width change");
    CHECK_MSG(TilemapComponent::IsEmpty(map.At(2, 0)),
              "the cell that was at column 3 is gone, not wrapped onto the next row");

    map.Resize(5, 4);
    CHECK_EQ(map.At(2, 1), 4);
    CHECK_MSG(TilemapComponent::IsEmpty(map.At(4, 3)), "new ground is empty");
}

// --- where a cell is -----------------------------------------------------------

static void testCellZeroHangsFromTheOriginAndRowsRunDown() {
    // The map extends along +x and DOWN along -y, so row zero is the top row
    // on a y-up screen, the way the atlas is counted and the way every tile
    // editor writes a map. Counting rows upward would draw the level upside
    // down, which is a legible picture of the wrong thing.
    glm::vec2 min, max;
    TilemapSystem::CellRect(0, 0, min, max);
    CHECK(near(min, glm::vec2(0.0f, -1.0f)));
    CHECK(near(max, glm::vec2(1.0f, 0.0f)));

    TilemapSystem::CellRect(3, 2, min, max);
    CHECK_MSG(near(min.x, 3.0f), "column three starts three units along");
    CHECK_MSG(near(max.y, -2.0f) && near(min.y, -3.0f), "row two is the third unit DOWN");
}

static void testCellFromLocalIsTheInverseOfCellRect() {
    const TilemapComponent map = grid(4, 3);
    uint32_t column = 99, row = 99;

    CHECK(TilemapSystem::CellFromLocal(map, glm::vec2(0.5f, -0.5f), column, row));
    CHECK_EQ(column, 0u);
    CHECK_EQ(row, 0u);

    CHECK(TilemapSystem::CellFromLocal(map, glm::vec2(3.9f, -2.1f), column, row));
    CHECK_EQ(column, 3u);
    CHECK_EQ(row, 2u);

    // Edges: the top and left belong to the cell, the bottom and right do
    // not, so no point is in two cells and the far edges are outside.
    CHECK(TilemapSystem::CellFromLocal(map, glm::vec2(0.0f, 0.0f), column, row));
    CHECK_EQ(column, 0u);
    CHECK_EQ(row, 0u);
    CHECK_MSG(!TilemapSystem::CellFromLocal(map, glm::vec2(4.0f, -0.5f), column, row),
              "x = width is past the last column");
    CHECK_MSG(!TilemapSystem::CellFromLocal(map, glm::vec2(0.5f, -3.0f), column, row),
              "y = -height is past the last row");
    CHECK_MSG(!TilemapSystem::CellFromLocal(map, glm::vec2(-0.1f, -0.5f), column, row),
              "left of the map");
    CHECK_MSG(!TilemapSystem::CellFromLocal(map, glm::vec2(0.5f, 0.1f), column, row),
              "above the map - a positive y is not row minus one");
}

static void testARayFindsTheCellItLandsOn() {
    const TilemapComponent map = grid(4, 3);
    uint32_t column = 99, row = 99;

    Ray straightDown;
    straightDown.origin = glm::vec3(2.5f, -1.5f, 5.0f);
    straightDown.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(TilemapSystem::CellFromRay(map, glm::mat4(1.0f), straightDown, column, row));
    CHECK_EQ(column, 2u);
    CHECK_EQ(row, 1u);

    // The map moved and doubled. The same world ray now lands on a different
    // cell, and the answer comes from the map's space, not the ray's.
    const glm::mat4 world = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
                                       glm::vec3(2.0f));
    CHECK(TilemapSystem::CellFromRay(map, world, straightDown, column, row));
    CHECK_MSG(column == 0 && row == 0, "world (2.5, -1.5) is local (0.25, -0.75), cell zero");

    Ray sideways;
    sideways.origin = glm::vec3(0.5f, -0.5f, 1.0f);
    sideways.direction = glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK_MSG(!TilemapSystem::CellFromRay(map, glm::mat4(1.0f), sideways, column, row),
              "a ray along the plane never crosses it");

    Ray away;
    away.origin = glm::vec3(0.5f, -0.5f, 1.0f);
    away.direction = glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK_MSG(!TilemapSystem::CellFromRay(map, glm::mat4(1.0f), away, column, row),
              "the plane is behind the eye");

    Ray offTheEdge;
    offTheEdge.origin = glm::vec3(-1.0f, -0.5f, 1.0f);
    offTheEdge.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK_MSG(!TilemapSystem::CellFromRay(map, glm::mat4(1.0f), offTheEdge, column, row),
              "hits the plane, misses the map");
}

// --- the bake --------------------------------------------------------------------

static void testOneCellIsOneQuadInTheRightPlace() {
    TilemapComponent map = grid(4, 3, 2, 2);
    map.Set(1, 2, TilemapComponent::MakeCell(0));

    MeshData mesh;
    TilemapSystem::BakeReport report;
    CHECK(TilemapSystem::Bake(map, mesh, &report));
    CHECK_EQ(report.drawn, 1u);
    CHECK_EQ(mesh.vertices.size(), size_t{4});
    CHECK_EQ(mesh.indices.size(), size_t{6});

    const Quad quad = quadOf(mesh, 0);
    CHECK(near(quad.bottomLeft.pos, glm::vec3(1.0f, -3.0f, 0.0f)));
    CHECK(near(quad.topRight.pos, glm::vec3(2.0f, -2.0f, 0.0f)));
    CHECK_MSG(near(mesh.boundsMin, glm::vec3(1.0f, -3.0f, 0.0f)) &&
              near(mesh.boundsMax, glm::vec3(2.0f, -2.0f, 0.0f)),
              "the bounds are the occupied cells, not the whole map");

    for (size_t i = 0; i < 4; ++i) {
        CHECK_MSG(near(mesh.vertices[i].normal, glm::vec3(0.0f, 0.0f, 1.0f)), "faces +Z, like a sprite");
        CHECK_MSG(near(mesh.vertices[i].color, glm::vec3(1.0f)),
                  "WHITE: the shader multiplies albedo by this and Vertex does not initialise it");
    }
}

static void testTheWindingFacesTheCameraASpriteFaces() {
    // The scene pipeline culls back faces. A quad wound the other way is not
    // drawn wrong, it is not drawn - and a map that is invisible from the
    // side a sprite is visible from reports nothing.
    TilemapComponent map = grid(1, 1, 1, 1);
    map.Set(0, 0, 0);
    MeshData mesh;
    CHECK(TilemapSystem::Bake(map, mesh));

    const glm::vec3 a = mesh.vertices[mesh.indices[0]].pos;
    const glm::vec3 b = mesh.vertices[mesh.indices[1]].pos;
    const glm::vec3 c = mesh.vertices[mesh.indices[2]].pos;
    const glm::vec3 faceNormal = glm::cross(b - a, c - a);
    CHECK_MSG(faceNormal.z > 0.0f, "counter-clockwise seen from +Z");

    const glm::vec3 d = mesh.vertices[mesh.indices[3]].pos;
    const glm::vec3 e = mesh.vertices[mesh.indices[4]].pos;
    const glm::vec3 f = mesh.vertices[mesh.indices[5]].pos;
    CHECK_MSG(glm::cross(e - d, f - d).z > 0.0f, "and so is the second triangle");
}

static void testTheTextureCornersAreTheAtlasCell() {
    // Atlas cell 6 of a 4x4 grid is column 2, row 1: u from 0.5 to 0.75, v
    // from 0.25 to 0.5. v is ZERO along the top edge, because an image's first
    // row is its top and so is the atlas's row zero - the same reason the
    // sprite suite pins its cells from the top.
    TilemapComponent map = grid(1, 1);
    map.Set(0, 0, TilemapComponent::MakeCell(6));
    MeshData mesh;
    CHECK(TilemapSystem::Bake(map, mesh));

    const Quad quad = quadOf(mesh, 0);
    CHECK(near(quad.topLeft.texCoord, glm::vec2(0.5f, 0.25f)));
    CHECK(near(quad.topRight.texCoord, glm::vec2(0.75f, 0.25f)));
    CHECK(near(quad.bottomLeft.texCoord, glm::vec2(0.5f, 0.5f)));
    CHECK(near(quad.bottomRight.texCoord, glm::vec2(0.75f, 0.5f)));
}

static void testFlipsSwapOneAxisAndLeaveTheOther() {
    TilemapComponent map = grid(2, 1);
    map.Set(0, 0, TilemapComponent::MakeCell(6, true, false));
    map.Set(1, 0, TilemapComponent::MakeCell(6, false, true));
    MeshData mesh;
    CHECK(TilemapSystem::Bake(map, mesh));
    CHECK_EQ(mesh.vertices.size(), size_t{8});

    const Quad h = quadOf(mesh, 0);
    CHECK_MSG(near(h.topLeft.texCoord, glm::vec2(0.75f, 0.25f)) &&
              near(h.topRight.texCoord, glm::vec2(0.5f, 0.25f)),
              "flip H: u runs right to left");
    CHECK_MSG(near(h.bottomLeft.texCoord.y, 0.5f), "and v is untouched");

    const Quad v = quadOf(mesh, 1);
    CHECK_MSG(near(v.topLeft.texCoord, glm::vec2(0.5f, 0.5f)) &&
              near(v.bottomLeft.texCoord, glm::vec2(0.5f, 0.25f)),
              "flip V: v runs bottom to top");
    CHECK_MSG(near(v.topRight.texCoord.x, 0.75f), "and u is untouched");

    // The geometry is the same either way: a flip is a texture decision.
    CHECK(near(h.bottomLeft.pos, glm::vec3(0.0f, -1.0f, 0.0f)));
    CHECK(near(v.bottomLeft.pos, glm::vec3(1.0f, -1.0f, 0.0f)));
}

static void testEmptyCellsAreSkippedAndIndicesStayInRange() {
    TilemapComponent map = grid(3, 3);
    map.Set(0, 0, 1);
    map.Set(2, 2, 2);
    MeshData mesh;
    TilemapSystem::BakeReport report;
    CHECK(TilemapSystem::Bake(map, mesh, &report));
    CHECK_EQ(report.drawn, 2u);
    CHECK_EQ(mesh.vertices.size(), size_t{8});
    CHECK_EQ(mesh.indices.size(), size_t{12});
    for (const uint32_t index : mesh.indices) {
        CHECK_MSG(index < mesh.vertices.size(), "every index names a vertex that exists");
    }
    // The second quad's indices are offset by the first quad's vertices, so
    // both triangles of the second quad reference vertices 4..7.
    CHECK_EQ(mesh.indices[6], 4u);
    CHECK_EQ(mesh.indices[11], 4u);
}

static void testAnAtlasPastThirtyTwoBitsStillDrawsItsCells() {
    // 65536 x 65536 atlas cells is 2^32, and the product in uint32_t was zero - so
    // every cell was "past the atlas" and the whole map was counted out of range
    // and not drawn.
    TilemapComponent map = grid(1, 1, 65536, 65536);
    map.Set(0, 0, TilemapComponent::MakeCell(5));
    MeshData mesh;
    TilemapSystem::BakeReport report;
    CHECK(TilemapSystem::Bake(map, mesh, &report));
    CHECK_EQ(report.drawn, 1u);
    CHECK_EQ(report.outOfRange, 0u);
}

static void testACellPastTheAtlasIsNotDrawnAndIsCounted() {
    // A flipbook WRAPS a frame past the end of its run, because a frame past
    // the end is a loop. A map cell past the end of its atlas is a typo, and
    // wrapping it would draw a plausible tile in place of the wrong one.
    TilemapComponent map = grid(2, 1, 2, 2);
    map.Set(0, 0, TilemapComponent::MakeCell(3));   // the last real cell
    map.Set(1, 0, TilemapComponent::MakeCell(4));   // one past it
    MeshData mesh;
    TilemapSystem::BakeReport report;
    CHECK(TilemapSystem::Bake(map, mesh, &report));
    CHECK_EQ(report.drawn, 1u);
    CHECK_EQ(report.outOfRange, 1u);
    CHECK_EQ(mesh.vertices.size(), size_t{4});

    TilemapComponent allBad = grid(1, 1, 2, 2);
    allBad.Set(0, 0, TilemapComponent::MakeCell(9));
    CHECK_MSG(!TilemapSystem::Bake(allBad, mesh), "a map with nothing drawable is nothing to draw");
    CHECK(mesh.empty());
}

static void testTheCapIsEnforcedWhereTheCellsAreAllocated() {
    // The first version checked the cap only at the bake. By then the cells
    // had been allocated - and the inspector offered 65,536 on each side,
    // which is sixteen gigabytes asked for in one go from a UI frame.
    TilemapComponent map = grid(256, 256);
    CHECK_MSG(!map.Resize(256, 257), "one row over the cap is refused");
    CHECK_MSG(map.width == 256 && map.height == 256 && map.cells.size() == 65536,
              "and the map is exactly as it was");
    CHECK(map.Set(255, 255, 0));
    MeshData mesh;
    CHECK_MSG(TilemapSystem::Bake(map, mesh), "exactly the cap is allowed");
    CHECK_EQ(mesh.vertices.size(), size_t{4});

    // Written straight into the fields, which Resize cannot stop. As a
    // 32-bit product 65,536 x 65,536 is ZERO, and zero is under every cap.
    TilemapComponent direct;
    direct.width = 65536;
    direct.height = 65536;
    CHECK_MSG(direct.cellCount() == 4294967296ull, "the count does not wrap");
    CHECK(!direct.withinCap());
    CHECK_MSG(!direct.Set(0, 1, 0), "Set refuses rather than allocating sixteen gigabytes");
    CHECK_MSG(!direct.Fill(0), "and so does Fill");
    CHECK_MSG(direct.cells.empty(), "nothing was allocated");
    CHECK_MSG(!TilemapSystem::Bake(direct, mesh), "and the bake refuses it whole");
    CHECK_MSG(TilemapComponent::IsEmpty(direct.At(0, 1)), "and it reads as empty");

    TilemapComponent over;
    over.width = 512;
    over.height = 129;   // 66,048 cells, just over
    CHECK(!over.Set(0, 0, 0));
    CHECK_MSG(!TilemapSystem::Bake(over, mesh), "refused, not truncated to a straight edge");
}

static void testADegenerateAtlasBakesNothing() {
    TilemapComponent map = grid(1, 1, 0, 4);
    map.Set(0, 0, 0);
    MeshData mesh;
    CHECK_MSG(!TilemapSystem::Bake(map, mesh), "a zero-column atlas is a division, not a picture");
}

// --- change detection -------------------------------------------------------------

static void testTheContentHashSeesWhatTheBakeReads() {
    TilemapComponent a = grid(4, 4);
    a.Set(1, 1, 3);
    TilemapComponent b = grid(4, 4);
    b.Set(1, 1, 3);
    CHECK_MSG(TilemapSystem::ContentHash(a) == TilemapSystem::ContentHash(b), "equal maps agree");
    CHECK_MSG(TilemapSystem::ContentHash(a) != 0, "never zero, which means never baked");

    b.Set(1, 1, 4);
    CHECK_MSG(TilemapSystem::ContentHash(a) != TilemapSystem::ContentHash(b), "a cell changed");

    b.Set(1, 1, 3);
    b.atlasColumns = 8;
    CHECK_MSG(TilemapSystem::ContentHash(a) != TilemapSystem::ContentHash(b),
              "the atlas grid moves every texture corner, so it is a rebake");

    b.atlasColumns = 4;
    b.Resize(5, 4);
    CHECK_MSG(TilemapSystem::ContentHash(a) != TilemapSystem::ContentHash(b), "and so is a resize");

    // Scratch is not content. Otherwise the hash could never match the
    // value it is compared against.
    TilemapComponent c = grid(4, 4);
    c.Set(1, 1, 3);
    c.bakedHash = 12345;
    c.meshID = 9;
    CHECK_MSG(TilemapSystem::ContentHash(a) == TilemapSystem::ContentHash(c),
              "what was baked last time is not part of what to bake this time");
}

static void testTheShapeOfAnEmptyMapIsStillContent() {
    // Sixteen empty cells laid out 4x4 and 2x8 are different maps, and a map
    // that has never been touched is not the same content as one whose vector
    // was cut down: the hash covers the cells the map DESCRIBES, and the
    // count of them, not the vector's length.
    TilemapComponent square;
    square.width = 4;
    square.height = 4;
    TilemapComponent tall;
    tall.width = 2;
    tall.height = 8;
    CHECK_MSG(TilemapSystem::ContentHash(square) != TilemapSystem::ContentHash(tall),
              "sixteen cells laid out 4x4 and 2x8 are different maps");

    // A vector longer than the map, left over from a size written directly,
    // holds entries no bake reads - and none the hash reads either, or a map
    // shrunk that way would rebake every frame against a hash it never
    // settles on.
    TilemapComponent shrunk = grid(4, 4);
    shrunk.Fill(TilemapComponent::MakeCell(1));
    TilemapComponent same = grid(4, 4);
    same.Fill(TilemapComponent::MakeCell(1));
    shrunk.width = 2;
    same.Resize(2, 4);
    CHECK_MSG(TilemapSystem::ContentHash(shrunk) == TilemapSystem::ContentHash(same),
              "reads the first eight cells either way; what is past the map is not content");
}

// --- the sync ---------------------------------------------------------------------

static void testFirstSightUploadsAndASettledMapCostsNothing() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& map = registry.emplace<TilemapComponent>(entity, grid(2, 2));
    auto& renderable = registry.emplace<RenderableComponent>(entity);
    map.Set(0, 0, 1);

    FakeMeshes meshes;
    TilemapSystem::Sync(registry, meshes);
    CHECK_EQ(meshes.calls.size(), size_t{1});
    CHECK_MSG(meshes.call(0) == "upload " + TilemapSystem::MeshKey(entity) + " 1",
              "first sight is an upload of one quad, got: " + meshes.call(0));
    CHECK_EQ(renderable.meshID, 7u);
    CHECK_EQ(map.meshID, 7u);
    CHECK_MSG(map.bakedHash == TilemapSystem::ContentHash(map), "remembers what it baked");

    TilemapSystem::Sync(registry, meshes);
    TilemapSystem::Sync(registry, meshes);
    CHECK_MSG(meshes.calls.size() == 1, "nothing changed, so nothing was uploaded or replaced");
    CHECK_EQ(renderable.meshID, 7u);
}

static void testARenderableRecreatedInTheInspectorGetsTheMapBack() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& map = registry.emplace<TilemapComponent>(entity, grid(2, 2));
    registry.emplace<RenderableComponent>(entity);
    map.Set(0, 0, 1);

    FakeMeshes meshes;
    TilemapSystem::Sync(registry, meshes);

    // Removed and added again: it is born holding id zero, which in the real
    // registry is the cube. The map still hashes as baked, so nothing is
    // re-uploaded - and the renderable must still be handed the map's mesh.
    registry.remove<RenderableComponent>(entity);
    auto& reborn = registry.emplace<RenderableComponent>(entity);
    CHECK_EQ(reborn.meshID, 0u);
    TilemapSystem::Sync(registry, meshes);
    CHECK_EQ(reborn.meshID, 7u);
    CHECK_MSG(meshes.calls.size() == 1, "and it cost no upload");
}

static void testAChangedMapReplacesBehindTheSameId() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& map = registry.emplace<TilemapComponent>(entity, grid(2, 2));
    auto& renderable = registry.emplace<RenderableComponent>(entity);
    map.Set(0, 0, 1);

    FakeMeshes meshes;
    TilemapSystem::Sync(registry, meshes);
    map.Set(1, 1, 2);
    TilemapSystem::Sync(registry, meshes);

    CHECK_EQ(meshes.calls.size(), size_t{2});
    CHECK_MSG(meshes.call(1) == "replace 7 2", "the SAME id, now two quads: " + meshes.call(1));
    CHECK_EQ(renderable.meshID, 7u);
    CHECK_MSG(meshes.lookup.size() == 1, "an edit is not a new mesh per stroke");
}

static void testAnEmptiedMapStopsDrawingAndKeepsItsSlot() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& map = registry.emplace<TilemapComponent>(entity, grid(2, 2));
    auto& renderable = registry.emplace<RenderableComponent>(entity);
    map.Set(0, 0, 1);

    FakeMeshes meshes;
    TilemapSystem::Sync(registry, meshes);

    // What the resource sync would have copied off the mesh by now.
    renderable.localBoundsMin = glm::vec3(0.0f, -1.0f, 0.0f);
    renderable.localBoundsMax = glm::vec3(1.0f, 0.0f, 0.0f);

    map.Fill(TilemapComponent::kEmpty);
    TilemapSystem::Sync(registry, meshes);

    CHECK_MSG(meshes.calls.size() == 1, "nothing is dropped and nothing is uploaded");
    CHECK_MSG(renderable.meshID == TilemapComponent::kNoMesh,
              "kInvalidMesh, which the draw loop skips - never zero, which is the cube");
    CHECK_MSG(meshes.lookup.size() == 1, "the slot is kept, because the registry never recycles one");

    // The bounds go back to a fresh renderable's. Nothing refreshes them
    // while the id is invalid, and left alone they would keep the box of the
    // last bake - so an emptied map would still be picked across ground it
    // no longer draws, in front of whatever is really there.
    const RenderableComponent fresh;
    CHECK_MSG(renderable.localBoundsMin == fresh.localBoundsMin &&
              renderable.localBoundsMax == fresh.localBoundsMax,
              "an emptied map is not pickable by the box it used to fill");

    // And back again, three times. A REPLACE behind the same id each time,
    // never a new slot: a game that clears and refills a map on a tick would
    // otherwise grow the registry for ever.
    for (int cycle = 0; cycle < 3; ++cycle) {
        map.Set(1, 0, 1);
        TilemapSystem::Sync(registry, meshes);
        CHECK_MSG(meshes.last() == "replace 7 1", "refilled behind the same id: " + meshes.last());
        CHECK_EQ(renderable.meshID, 7u);
        map.Fill(TilemapComponent::kEmpty);
        TilemapSystem::Sync(registry, meshes);
        CHECK_EQ(renderable.meshID, TilemapComponent::kNoMesh);
    }
    CHECK_MSG(meshes.lookup.size() == 1 && meshes.nextId == 8,
              "three empty-and-refill cycles allocated no slot");
}

static void testAMapThatNeverDrewNeverTouchesTheRegistry() {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TilemapComponent>(entity, grid(2, 2));
    auto& renderable = registry.emplace<RenderableComponent>(entity);

    FakeMeshes meshes;
    TilemapSystem::Sync(registry, meshes);
    CHECK_MSG(meshes.calls.empty(), "nothing to invalidate, nothing to upload");
    CHECK_EQ(renderable.meshID, TilemapComponent::kNoMesh);
}

static void testTwoMapsAreTwoKeysAndAReloadReusesASlot() {
    entt::registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    registry.emplace<TilemapComponent>(first, grid(2, 2)).Set(0, 0, 1);
    registry.emplace<TilemapComponent>(second, grid(2, 2)).Set(0, 0, 2);
    registry.emplace<RenderableComponent>(first);
    registry.emplace<RenderableComponent>(second);

    FakeMeshes meshes;
    TilemapSystem::Sync(registry, meshes);
    CHECK_EQ(meshes.lookup.size(), size_t{2});
    CHECK_MSG(TilemapSystem::MeshKey(first) != TilemapSystem::MeshKey(second),
              "keyed by entity, so two maps never share a mesh");

    // A scene load clears the registry and builds the next scene into the
    // same slots. The registry that owns the meshes outlives that, so the
    // next map to land on a slot finds a mesh already there and REPLACES it
    // - a reload is not a leak.
    registry.clear();
    const auto reloaded = registry.create();
    CHECK_MSG(entt::to_entity(reloaded) == entt::to_entity(first) ||
              entt::to_entity(reloaded) == entt::to_entity(second),
              "EnTT recycles the slot");
    registry.emplace<TilemapComponent>(reloaded, grid(3, 3)).Set(2, 2, 5);
    auto& renderable = registry.emplace<RenderableComponent>(reloaded);

    const size_t before = meshes.calls.size();
    TilemapSystem::Sync(registry, meshes);
    CHECK_EQ(meshes.calls.size(), before + 1);
    CHECK_MSG(meshes.last().rfind("replace", 0) == 0,
              "the slot's old mesh is replaced: " + meshes.last());
    CHECK_EQ(meshes.lookup.size(), size_t{2});
    CHECK_MSG(renderable.meshID == meshes.Find(TilemapSystem::MeshKey(reloaded)),
              "and the renderable holds that slot's id");
}

// --- the scene file ---------------------------------------------------------------

static void testAMapSurvivesARoundTrip() {
    entt::registry source;
    const auto entity = source.create();
    source.emplace<TagComponent>(entity, "Ground");
    source.emplace<TransformComponent>(entity);
    auto& map = source.emplace<TilemapComponent>(entity, grid(3, 2, 8, 4));
    map.Set(0, 0, TilemapComponent::MakeCell(5));
    map.Set(2, 1, TilemapComponent::MakeCell(31, true, true));
    map.bakedHash = 99;   // scratch, must not come back
    map.meshID = 4;

    std::ostringstream out;
    out << "{\n";
    ComponentCodec::Write(source, entity, out, "  ");
    out << "}\n";

    Json::Value node;
    std::string error;
    CHECK_MSG(Json::Parse(out.str(), node, error), "valid JSON: " + error);

    entt::registry loaded;
    const auto restored = loaded.create();
    ComponentCodec::Read(loaded, restored, node);

    CHECK_MSG(loaded.all_of<TilemapComponent>(restored), "the component came back");
    const auto& back = loaded.get<TilemapComponent>(restored);
    CHECK_EQ(back.atlasColumns, 8u);
    CHECK_EQ(back.atlasRows, 4u);
    CHECK_EQ(back.width, 3u);
    CHECK_EQ(back.height, 2u);
    CHECK_EQ(back.cells.size(), size_t{6});
    CHECK_EQ(back.At(0, 0), TilemapComponent::MakeCell(5));
    CHECK_MSG(back.At(2, 1) == TilemapComponent::MakeCell(31, true, true),
              "flips ride inside the integer");
    CHECK_MSG(TilemapComponent::IsEmpty(back.At(1, 0)), "an empty cell is written as empty");
    CHECK_MSG(back.bakedHash == 0 && back.meshID == TilemapComponent::kNoMesh,
              "scratch is not persisted");

    // The cells are laid out one map row per line, so a person can read the
    // map off the file and a diff of one tile is one line. Both rows are
    // checked: a writer that put every cell on one line after the bracket
    // would still have row zero "on its own line".
    const std::string text = out.str();
    const size_t cellsAt = text.find("\"Cells\": [");
    CHECK_MSG(cellsAt != std::string::npos, "the cells are written under Cells");
    const size_t rowZeroStart = text.find('\n', cellsAt) + 1;
    const size_t rowZeroEnd = text.find('\n', rowZeroStart);
    const size_t rowOneEnd = text.find('\n', rowZeroEnd + 1);
    const std::string rowZero = text.substr(rowZeroStart, rowZeroEnd - rowZeroStart);
    const std::string rowOne = text.substr(rowZeroEnd + 1, rowOneEnd - rowZeroEnd - 1);
    const std::string flipped = std::to_string(TilemapComponent::MakeCell(31, true, true));
    CHECK_MSG(rowZero.find("5, -1, -1") != std::string::npos && rowZero.find(flipped) == std::string::npos,
              "row zero is its own line and holds only row zero: " + rowZero);
    CHECK_MSG(rowOne.find("-1, -1, " + flipped) != std::string::npos,
              "row one is the NEXT line: " + rowOne);
}

static void testAFileNamingAnOverCapSizeLoadsEmptyAtTheDefaultSize() {
    // A file can say any size and the cells are allocated by what it says,
    // so this is where a hundred-thousand-square map would have asked for
    // forty gigabytes from inside a scene load.
    Json::Value node;
    std::string error;
    CHECK_MSG(Json::Parse("{ \"Tilemap\": { \"Width\": 100000, \"Height\": 100000, "
                          "\"Cells\": [ 1, 2 ] } }", node, error), error);
    entt::registry registry;
    const auto entity = registry.create();
    ComponentCodec::Read(registry, entity, node);

    CHECK_MSG(registry.all_of<TilemapComponent>(entity), "the entity keeps its component");
    const auto& map = registry.get<TilemapComponent>(entity);
    const TilemapComponent defaults;
    CHECK_EQ(map.width, defaults.width);
    CHECK_EQ(map.height, defaults.height);
    CHECK_MSG(map.cells.size() <= map.cellCount(), "nothing past the default size was allocated");
    CHECK_MSG(TilemapComponent::IsEmpty(map.At(0, 0)),
              "the file's cells meant something at its size, not at this one");
}

static void testAShortOrLongCellsArrayIsToleratedAndAMissingOneIsEmpty() {
    Json::Value node;
    std::string error;
    CHECK_MSG(Json::Parse("{ \"Tilemap\": { \"Width\": 2, \"Height\": 2, \"Cells\": [ 7 ] } }",
                          node, error), error);
    entt::registry registry;
    const auto entity = registry.create();
    ComponentCodec::Read(registry, entity, node);
    const auto& map = registry.get<TilemapComponent>(entity);
    CHECK_EQ(map.width, 2u);
    CHECK_EQ(map.atlasColumns, 1u);
    CHECK_EQ(map.At(0, 0), 7);
    CHECK_MSG(TilemapComponent::IsEmpty(map.At(1, 1)), "the rest of a short array is empty");

    Json::Value longer;
    CHECK(Json::Parse("{ \"Tilemap\": { \"Width\": 1, \"Height\": 1, \"Cells\": [ 1, 2, 3 ] } }",
                      longer, error));
    const auto other = registry.create();
    ComponentCodec::Read(registry, other, longer);
    const auto& small = registry.get<TilemapComponent>(other);
    CHECK_EQ(small.cells.size(), size_t{1});
    CHECK_MSG(small.At(0, 0) == 1, "the extra cells are dropped, not wrapped");

    Json::Value bare;
    CHECK(Json::Parse("{ \"Tilemap\": { } }", bare, error));
    const auto third = registry.create();
    ComponentCodec::Read(registry, third, bare);
    const auto& defaults = registry.get<TilemapComponent>(third);
    const TilemapComponent fresh;
    CHECK_EQ(defaults.width, fresh.width);
    CHECK_EQ(defaults.height, fresh.height);
    CHECK_MSG(defaults.atlasColumns == 1 && defaults.atlasRows == 1,
              "no atlas grid means the whole texture, as it does for a sprite");
    MeshData mesh;
    CHECK_MSG(!TilemapSystem::Bake(defaults, mesh), "and nothing to draw yet");
}

// --- the state hash ---------------------------------------------------------------

static void testTheStateHashSeesTheCellsAndNotTheAtlas() {
    entt::registry a, b;
    for (entt::registry* registry : {&a, &b}) {
        const auto entity = registry->create();
        registry->emplace<TransformComponent>(entity);
        auto& map = registry->emplace<TilemapComponent>(entity, grid(4, 4));
        map.Set(1, 1, TilemapComponent::MakeCell(2));
    }
    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b), "equal maps agree");

    // A tile knocked down on a tick is state the next tick reads.
    b.view<TilemapComponent>().each([](TilemapComponent& map) { map.Set(1, 1, TilemapComponent::kEmpty); });
    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b), "a cell changed");

    b.view<TilemapComponent>().each([](TilemapComponent& map) { map.Set(1, 1, TilemapComponent::MakeCell(2)); });
    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b), "and back");

    b.view<TilemapComponent>().each([](TilemapComponent& map) { map.Resize(5, 4); });
    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b), "so is the size of the map");

    b.view<TilemapComponent>().each([](TilemapComponent& map) { map.Resize(4, 4); });
    b.view<TilemapComponent>().each([](TilemapComponent& map) { map.atlasColumns = 8; });
    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b),
              "the atlas grid is how a cell looks, and retuning it is an edit, not a divergence");

    b.view<TilemapComponent>().each([](TilemapComponent& map) {
        map.atlasColumns = 4;
        map.bakedHash = 77;
        map.meshID = 3;
    });
    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b), "renderer scratch is not state");
}

static void testANeverWrittenMapAndOneFilledWithEmptiesHashAlike() {
    // Both read as empty through At and both draw nothing, so by the rule
    // that admits state - what a tick reads - they are the same state. The
    // first version mixed in the vector's length and told them apart, which
    // is a divergence no tick could have caused: a map built in code and the
    // same map loaded from a file would have disagreed at tick zero.
    TilemapComponent never;
    TilemapComponent filled;
    filled.Fill(TilemapComponent::kEmpty);
    CHECK_MSG(TilemapSystem::ContentHash(never) == TilemapSystem::ContentHash(filled),
              "the renderer sees one map");

    entt::registry a, b;
    {
        const auto entity = a.create();
        a.emplace<TransformComponent>(entity);
        a.emplace<TilemapComponent>(entity);
    }
    {
        const auto entity = b.create();
        b.emplace<TransformComponent>(entity);
        b.emplace<TilemapComponent>(entity).Fill(TilemapComponent::kEmpty);
    }
    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b), "and so does the oracle");

    // A cell in the vector's half-written tail is still a difference.
    filled.Set(15, 15, 1);
    CHECK(TilemapSystem::ContentHash(never) != TilemapSystem::ContentHash(filled));
}

int main() {
    testAFreshMapIsEmptyEverywhereAndDrawsNothing();
    testSetGrowsTheVectorAndRefusesTheOutside();
    testACellPacksItsIndexAndFlips();
    testResizeKeepsWhatStillFits();

    testCellZeroHangsFromTheOriginAndRowsRunDown();
    testCellFromLocalIsTheInverseOfCellRect();
    testARayFindsTheCellItLandsOn();

    testOneCellIsOneQuadInTheRightPlace();
    testTheWindingFacesTheCameraASpriteFaces();
    testTheTextureCornersAreTheAtlasCell();
    testFlipsSwapOneAxisAndLeaveTheOther();
    testEmptyCellsAreSkippedAndIndicesStayInRange();
    testACellPastTheAtlasIsNotDrawnAndIsCounted();
    testAnAtlasPastThirtyTwoBitsStillDrawsItsCells();
    testTheCapIsEnforcedWhereTheCellsAreAllocated();
    testADegenerateAtlasBakesNothing();

    testTheContentHashSeesWhatTheBakeReads();
    testTheShapeOfAnEmptyMapIsStillContent();

    testFirstSightUploadsAndASettledMapCostsNothing();
    testARenderableRecreatedInTheInspectorGetsTheMapBack();
    testAChangedMapReplacesBehindTheSameId();
    testAnEmptiedMapStopsDrawingAndKeepsItsSlot();
    testAMapThatNeverDrewNeverTouchesTheRegistry();
    testTwoMapsAreTwoKeysAndAReloadReusesASlot();

    testAMapSurvivesARoundTrip();
    testAShortOrLongCellsArrayIsToleratedAndAMissingOneIsEmpty();
    testAFileNamingAnOverCapSizeLoadsEmptyAtTheDefaultSize();

    testTheStateHashSeesTheCellsAndNotTheAtlas();
    testANeverWrittenMapAndOneFilledWithEmptiesHashAlike();

    return test::summary("test_tilemap", 100);
}
