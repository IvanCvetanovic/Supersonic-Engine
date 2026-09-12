// The BMFont reader, and the glyph quads a string turns into.
//
// TESTED ON FONTS THIS SUITE WRITES, and never on the game's own. Magic
// Portals' Matura fonts are Asantee's, they live outside this repository by the
// same rule its art and sounds do, and a test that needed them would be a test
// that only runs on one machine. Everything below is a descriptor written into
// a scratch directory, which is also how the refusals get exercised: a
// malformed file is easy to write and impossible to find.

#include "TestHarness.hpp"

#include "core/BitmapFont.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace Supersonic;

namespace {

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-bitmapfont";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Write(const char* name, const std::string& text) {
    const std::filesystem::path path = Scratch() / name;
    std::ofstream file(path, std::ios::trunc);
    file << text;
    file.close();
    return path.string();
}

// A two-glyph font on one 64 x 64 page. 'A' is 10 wide and advances 12; 'B' is
// 20 wide and advances 22, so the two are told apart by every number.
std::string TwoGlyphFont() {
    return "info face=\"Test Face\" size=16 bold=0\n"
           "common lineHeight=20 base=15 scaleW=64 scaleH=64 pages=1\n"
           "page id=0 file=\"page_0.png\"\n"
           "chars count=2\n"
           "char id=65 x=0  y=0  width=10 height=14 xoffset=1 yoffset=2 xadvance=12 page=0\n"
           "char id=66 x=16 y=0  width=20 height=18 xoffset=0 yoffset=1 xadvance=22 page=0\n";
}

void TheDescriptorReads() {
    BitmapFont font;
    std::string error;
    CHECK_MSG(font.Load(Write("ok.fnt", TwoGlyphFont()), error), error);
    CHECK(font.IsLoaded());
    CHECK_EQ(font.LineHeight(), 20);
    CHECK_EQ(font.Base(), 15);
    CHECK_EQ(font.PageSize().x, 64);
    CHECK_EQ(font.PageSize().y, 64);

    // The page is resolved BESIDE THE DESCRIPTOR, not against the working
    // directory - a font loaded by absolute path from outside the project has
    // to find its own pages.
    CHECK_MSG(font.Pages().size() == std::size_t{1}, "one page");
    if (!font.Pages().empty()) {
        CHECK_MSG(font.Pages()[0].find("page_0.png") != std::string::npos, font.Pages()[0]);
        CHECK_MSG(font.Pages()[0].find("supersonic-test-bitmapfont") != std::string::npos,
                  "the page path must sit beside the .fnt: " + font.Pages()[0]);
    }

    const FontGlyph* a = font.Find('A');
    CHECK_MSG(a != nullptr, "the font has an A");
    if (a != nullptr) {
        CHECK_EQ(a->width, 10);
        CHECK_EQ(a->xadvance, 12);
        CHECK_EQ(a->yoffset, 2);
    }
    CHECK_MSG(font.Find('Z') == nullptr, "and no Z");
}

void MeasureTakesTheWidestLine() {
    BitmapFont font;
    std::string error;
    CHECK_MSG(font.Load(Write("measure.fnt", TwoGlyphFont()), error), error);

    // A + B = 12 + 22 = 34 across, one line of 20.
    const glm::vec2 one = font.Measure("AB");
    CHECK_NEAR(one.x, 34.0f);
    CHECK_NEAR(one.y, 20.0f);

    // THE WIDEST LINE, not the sum of everything. "AB\nA" is 34 wide and two
    // lines tall - summing the advances would call it 46, and text centred on
    // that measurement sits visibly off to one side.
    const glm::vec2 two = font.Measure("AB\nA");
    CHECK_NEAR(two.x, 34.0f);
    CHECK_NEAR(two.y, 40.0f);

    // A character the font has no glyph for contributes nothing rather than
    // being guessed at.
    CHECK_NEAR(font.Measure("AZB").x, 34.0f);
    CHECK_NEAR(font.Measure("").x, 0.0f);
}

void TextBecomesQuads() {
    BitmapFont font;
    std::string error;
    CHECK_MSG(font.Load(Write("quads.fnt", TwoGlyphFont()), error), error);

    MeshData mesh;
    CHECK(font.BuildText("AB", 0, mesh));
    CHECK_EQ(mesh.vertices.size(), std::size_t{8});  // two quads
    CHECK_EQ(mesh.indices.size(), std::size_t{12});

    // The first glyph starts at the pen plus its xoffset, and hangs DOWN from
    // the line by its yoffset - BMFont's y runs the opposite way to this
    // engine's, and the flip belongs here rather than in every caller.
    //
    // A: xoffset 1, so left = 1; yoffset 2, so top = -2; height 14, so
    // bottom = -16.
    float minX = mesh.vertices[0].pos.x;
    float maxY = mesh.vertices[0].pos.y;
    float minY = mesh.vertices[0].pos.y;
    for (const Vertex& v : mesh.vertices) {
        minX = v.pos.x < minX ? v.pos.x : minX;
        maxY = v.pos.y > maxY ? v.pos.y : maxY;
        minY = v.pos.y < minY ? v.pos.y : minY;
    }
    CHECK_NEAR(minX, 1.0f);
    CHECK_NEAR(maxY, -1.0f);  // B's yoffset is 1, the higher of the two
    CHECK_NEAR(minY, -19.0f); // B: top -1, height 18

    // The second glyph sits one advance along: B's left is 12 + 0.
    bool foundB = false;
    for (const Vertex& v : mesh.vertices) {
        if (::test::nearly(v.pos.x, 12.0f)) foundB = true;
    }
    CHECK_MSG(foundB, "B starts at A's advance");

    // Texture coordinates are normalised against the page, so a caller never
    // needs to know the atlas size.
    for (const Vertex& v : mesh.vertices) {
        CHECK_MSG(v.texCoord.x >= 0.0f && v.texCoord.x <= 1.0f && v.texCoord.y >= 0.0f &&
                      v.texCoord.y <= 1.0f,
                  "a glyph's uv must lie on its page");
    }

    // And the bounds are real, which is the whole reason the quad bug was
    // worth a test of its own: a mesh whose AABB is a point is culled against
    // its own centre.
    const glm::vec3 extent = mesh.boundsMax - mesh.boundsMin;
    CHECK_MSG(extent.x > 0.0f && extent.y > 0.0f, "text has real bounds, or culling eats it");
}

void APageIsOneMesh() {
    // A two-page font: the glyphs of one page are emitted, the other's are
    // skipped, and the PEN STILL ADVANCES over them so the two meshes line up
    // when drawn together.
    BitmapFont font;
    std::string error;
    const std::string text =
        "common lineHeight=20 base=15 scaleW=64 scaleH=64 pages=2\n"
        "page id=0 file=\"p0.png\"\n"
        "page id=1 file=\"p1.png\"\n"
        "char id=65 x=0 y=0 width=10 height=10 xoffset=0 yoffset=0 xadvance=12 page=0\n"
        "char id=66 x=0 y=0 width=10 height=10 xoffset=0 yoffset=0 xadvance=22 page=1\n";
    CHECK_MSG(font.Load(Write("pages.fnt", text), error), error);
    CHECK_EQ(font.Pages().size(), std::size_t{2});

    MeshData first;
    CHECK(font.BuildText("BA", 0, first));
    CHECK_EQ(first.vertices.size(), std::size_t{4}); // only the A

    // B advances 22 before A, so A's left edge is at 22 on BOTH passes.
    bool atAdvance = false;
    for (const Vertex& v : first.vertices) {
        if (::test::nearly(v.pos.x, 22.0f)) atAdvance = true;
    }
    CHECK_MSG(atAdvance, "the skipped page's glyph still moves the pen");

    MeshData second;
    CHECK(font.BuildText("BA", 1, second));
    CHECK_EQ(second.vertices.size(), std::size_t{4}); // only the B
}

void WhatIsRefused() {
    BitmapFont font;
    std::string error;

    CHECK_MSG(!font.Load((Scratch() / "not-here.fnt").string(), error), "a missing file is refused");
    CHECK_MSG(error.find("cannot open") != std::string::npos, error);

    // No common line: not a BMFont descriptor at all.
    CHECK_MSG(!font.Load(Write("nocommon.fnt",
                               "info face=\"x\"\n"
                               "page id=0 file=\"p.png\"\n"
                               "char id=65 x=0 y=0 width=1 height=1 xoffset=0 yoffset=0 xadvance=1 page=0\n"),
                         error),
              "a file with no common line is refused");

    // A char line missing a field a glyph cannot be placed without. Defaulting
    // it would be text that looks almost right, which is worse than a refusal.
    CHECK_MSG(!font.Load(Write("badchar.fnt",
                               "common lineHeight=20 base=15 scaleW=64 scaleH=64 pages=1\n"
                               "page id=0 file=\"p.png\"\n"
                               "char id=65 x=0 y=0 width=10 height=10 xoffset=0 page=0\n"),
                         error),
              "a char line missing xadvance is refused");

    // A glyph naming a page the descriptor never described.
    CHECK_MSG(!font.Load(Write("gappage.fnt",
                               "common lineHeight=20 base=15 scaleW=64 scaleH=64 pages=2\n"
                               "page id=1 file=\"p1.png\"\n"
                               "char id=65 x=0 y=0 width=1 height=1 xoffset=0 yoffset=0 xadvance=1 page=1\n"),
                         error),
              "a page named by a glyph but never described is refused");

    // And a descriptor with no characters at all.
    CHECK_MSG(!font.Load(Write("nochars.fnt",
                               "common lineHeight=20 base=15 scaleW=64 scaleH=64 pages=1\n"
                               "page id=0 file=\"p.png\"\n"),
                         error),
              "a font describing no characters is refused");
}

void AnUnloadedFontDrawsNothing() {
    // The failure that must not be a crash: a font nobody loaded, asked for
    // geometry. Every caller is a game that failed to find its own asset.
    BitmapFont font;
    MeshData mesh;
    CHECK_MSG(!font.BuildText("AB", 0, mesh), "an unloaded font builds nothing");
    CHECK_MSG(mesh.vertices.empty(), "and leaves no half-built mesh behind");
    CHECK_NEAR(font.Measure("AB").x, 0.0f);
}

void runTests() {
    TheDescriptorReads();
    MeasureTakesTheWidestLine();
    TextBecomesQuads();
    APageIsOneMesh();
    WhatIsRefused();
    AnUnloadedFontDrawsNothing();
}

} // namespace

TEST_MAIN("test_bitmapfont", 40)
