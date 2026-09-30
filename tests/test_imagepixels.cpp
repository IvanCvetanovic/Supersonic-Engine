// Pixels in memory, with no device.
//
// --screenshot-ui reads the swapchain back, and on Windows the swapchain is
// usually B8G8R8A8: written to a PNG as it comes, every red would be blue.
// Nothing looks broken about a picture with its channels swapped until
// somebody compares it with the game - which is the whole use of the capture.
// The copy also leaves whatever alpha the last pass wrote, and a PNG with
// alpha below 255 looks empty in some viewers. So the packing is pinned here,
// channel by channel, where no GPU is needed to see it.

#include "TestHarness.hpp"
#include "core/ImagePixels.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace Supersonic;
using ImagePixels::ChannelOrder;

namespace {

// One pixel of the output, as a string a failure can print.
std::string pixelAt(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
    if (i + 3 >= rgba.size()) return "(out of range)";
    return "(" + std::to_string(rgba[i]) + ", " + std::to_string(rgba[i + 1]) + ", " +
           std::to_string(rgba[i + 2]) + ", " + std::to_string(rgba[i + 3]) + ")";
}

bool pixelIs(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y,
             uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
    return i + 3 < rgba.size() && rgba[i] == r && rgba[i + 1] == g && rgba[i + 2] == b &&
           rgba[i + 3] == a;
}

} // namespace

static void testBgraComesOutAsRgba() {
    // Pure red, green and blue as a B8G8R8A8 swapchain stores them.
    const std::vector<uint8_t> bgra = {
        0, 0, 255, 255,   // red
        0, 255, 0, 255,   // green
        255, 0, 0, 255,   // blue
    };
    const auto rgba = ImagePixels::PackOpaqueRgba(bgra.data(), 3, 1, 12, ChannelOrder::Bgra);
    CHECK_EQ(rgba.size(), std::size_t{12});
    CHECK_MSG(pixelIs(rgba, 3, 0, 0, 255, 0, 0, 255), "red stays red: " + pixelAt(rgba, 3, 0, 0));
    CHECK_MSG(pixelIs(rgba, 3, 1, 0, 0, 255, 0, 255), "green stays green: " + pixelAt(rgba, 3, 1, 0));
    CHECK_MSG(pixelIs(rgba, 3, 2, 0, 0, 0, 255, 255), "blue stays blue: " + pixelAt(rgba, 3, 2, 0));
}

static void testRgbaIsCopiedAsItIs() {
    // Every channel distinct, so a swap of any two shows.
    const std::vector<uint8_t> source = {10, 20, 30, 255, 40, 50, 60, 255};
    const auto rgba = ImagePixels::PackOpaqueRgba(source.data(), 2, 1, 8, ChannelOrder::Rgba);
    CHECK_MSG(pixelIs(rgba, 2, 0, 0, 10, 20, 30, 255), "unchanged: " + pixelAt(rgba, 2, 0, 0));
    CHECK_MSG(pixelIs(rgba, 2, 1, 0, 40, 50, 60, 255), "unchanged: " + pixelAt(rgba, 2, 1, 0));

    // And the same bytes read as BGRA swap only the outer two.
    const auto swapped = ImagePixels::PackOpaqueRgba(source.data(), 2, 1, 8, ChannelOrder::Bgra);
    CHECK_MSG(pixelIs(swapped, 2, 0, 0, 30, 20, 10, 255), "red and blue trade places, green stays: " +
                                                           pixelAt(swapped, 2, 0, 0));
}

static void testEveryAlphaComesOutOpaque() {
    // The last pass decides the swapchain's alpha - ImGui blends into it - and
    // a PNG keeps it.
    const std::vector<uint8_t> source = {1, 2, 3, 0, 4, 5, 6, 17, 7, 8, 9, 254};
    for (const ChannelOrder order : {ChannelOrder::Rgba, ChannelOrder::Bgra}) {
        const auto rgba = ImagePixels::PackOpaqueRgba(source.data(), 3, 1, 12, order);
        bool opaque = rgba.size() == 12;
        for (std::size_t i = 3; i < rgba.size(); i += 4) opaque = opaque && rgba[i] == 255;
        CHECK_MSG(opaque, "every alpha is 255, whatever the source said");
    }
}

static void testPaddedRowsArePackedTight() {
    // Two rows of two pixels, each row padded to 12 bytes with junk that must
    // not reach the output - a readback's rows may be pitched wider than the
    // image, and a packer that ignored the pitch would shear the picture.
    const std::vector<uint8_t> source = {
        1, 2, 3, 255,   4, 5, 6, 255,     99, 99, 99, 99,
        7, 8, 9, 255,   10, 11, 12, 255,  99, 99, 99, 99,
    };
    const auto rgba = ImagePixels::PackOpaqueRgba(source.data(), 2, 2, 12, ChannelOrder::Rgba);
    CHECK_EQ(rgba.size(), std::size_t{16});
    CHECK_MSG(pixelIs(rgba, 2, 0, 1, 7, 8, 9, 255),
              "the second row starts at the pitch, not after the first row's pixels: " +
                  pixelAt(rgba, 2, 0, 1));
    CHECK_MSG(pixelIs(rgba, 2, 1, 1, 10, 11, 12, 255), "and ends where it should: " +
                                                         pixelAt(rgba, 2, 1, 1));
    bool noJunk = true;
    for (const uint8_t byte : rgba) noJunk = noJunk && byte != 99;
    CHECK_MSG(noJunk, "and none of the padding came along");
}

static void testNothingToReadGivesNothing() {
    const std::vector<uint8_t> source(16, 7);
    CHECK_MSG(ImagePixels::PackOpaqueRgba(nullptr, 2, 2, 8, ChannelOrder::Rgba).empty(),
              "a null source");
    CHECK_MSG(ImagePixels::PackOpaqueRgba(source.data(), 0, 2, 8, ChannelOrder::Rgba).empty(),
              "a zero width");
    CHECK_MSG(ImagePixels::PackOpaqueRgba(source.data(), 2, 0, 8, ChannelOrder::Rgba).empty(),
              "a zero height");
    CHECK_MSG(ImagePixels::PackOpaqueRgba(source.data(), 2, 2, 7, ChannelOrder::Rgba).empty(),
              "a pitch shorter than a row, which would read rows into each other");
}

// --- FixAlphaBorder: Godot's process/fix_alpha_border -----------------------
//
// Wolf Brigade's sprites were imported by Godot with the flag on, and PIL wrote
// black under every transparent texel. Drawn linearly filtered without it, each
// sprite gets a dark rim. The rules below are the ones that decide which colour
// a texel is given, each pinned where getting it wrong would pick another.

namespace {

// A w x h image of transparent black, with `opaque` texels set.
struct Canvas {
    uint32_t w, h;
    std::vector<uint8_t> px;
    Canvas(uint32_t width, uint32_t height) : w(width), h(height), px(width * height * 4, 0) {}
    void set(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    }
    bool is(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
        return pixelIs(px, w, x, y, r, g, b, a);
    }
    std::string at(uint32_t x, uint32_t y) const { return pixelAt(px, w, x, y); }
    void fix() { ImagePixels::FixAlphaBorder(px.data(), w, h); }
};

} // namespace

static void testATransparentTexelTakesItsOpaqueNeighboursColour() {
    Canvas c(3, 1);
    c.set(0, 0, 200, 100, 50, 255);
    c.fix();
    CHECK_MSG(c.is(1, 0, 200, 100, 50, 0), "the colour, and its own alpha kept: " + c.at(1, 0));
    CHECK_MSG(c.is(2, 0, 200, 100, 50, 0), "two texels away as well: " + c.at(2, 0));
    CHECK_MSG(c.is(0, 0, 200, 100, 50, 255), "and the opaque texel is left alone");
}

static void testTheThresholdIsTwentyNotOne() {
    // Godot fills anything below alpha 20, and only reads from 20 up.
    Canvas c(4, 1);
    c.set(0, 0, 10, 20, 30, 255);
    c.set(1, 0, 99, 99, 99, 19);   // faint: filled, and not a source
    c.set(2, 0, 50, 60, 70, 20);   // at the threshold: kept, and a source
    c.set(3, 0, 0, 0, 0, 0);
    c.fix();
    CHECK_MSG(c.is(1, 0, 10, 20, 30, 19),
              "alpha 19 is filled from its nearest opaque texel, keeping its alpha: " + c.at(1, 0));
    CHECK_MSG(c.is(2, 0, 50, 60, 70, 20), "alpha 20 keeps its colour: " + c.at(2, 0));
    CHECK_MSG(c.is(3, 0, 50, 60, 70, 0),
              "and is the nearest source for its neighbour, not the 19 beside it: " + c.at(3, 0));
}

static void testTheReachIsFourTexels() {
    // A row: one opaque texel, then transparent. Four away is filled, five is
    // not - and five is not filled from four either, since what is filled
    // stays transparent and is never a source.
    Canvas c(7, 1);
    c.set(0, 0, 1, 2, 3, 255);
    for (uint32_t x = 1; x < 7; ++x) c.set(x, 0, 9, 9, 9, 0);
    c.fix();
    CHECK_MSG(c.is(4, 0, 1, 2, 3, 0), "four texels away is in reach: " + c.at(4, 0));
    CHECK_MSG(c.is(5, 0, 9, 9, 9, 0), "five is out, and keeps its colour: " + c.at(5, 0));
    CHECK_MSG(c.is(6, 0, 9, 9, 9, 0), "as does six: " + c.at(6, 0));

    // The reach is a square, so a diagonal four each way is in it.
    Canvas d(5, 5);
    d.set(4, 4, 7, 8, 9, 255);
    d.fix();
    CHECK_MSG(d.is(0, 0, 7, 8, 9, 0), "the square's far corner is in reach: " + d.at(0, 0));
}

static void testTheNearestIsByDistanceNotBySquare() {
    // In the square both are four texels or fewer away. By distance, (4,8) is
    // 4 from (4,4) and (1,1) is 4.24 - so (4,8) wins, although (1,1) comes
    // first in row order and is nearer by the square's own measure.
    Canvas c(9, 9);
    c.set(1, 1, 255, 0, 0, 255);
    c.set(4, 8, 0, 0, 255, 255);
    c.fix();
    CHECK_MSG(c.is(4, 4, 0, 0, 255, 0), "the Euclidean nearest: " + c.at(4, 4));
}

static void testATieGoesToTheFirstInRowOrder() {
    // Four texels at distance two; the one above comes first when the rows are
    // read top to bottom, left to right.
    Canvas c(5, 5);
    c.set(2, 0, 10, 0, 0, 255);   // above
    c.set(0, 2, 0, 10, 0, 255);   // left
    c.set(4, 2, 0, 0, 10, 255);   // right
    c.set(2, 4, 10, 10, 0, 255);  // below
    c.fix();
    CHECK_MSG(c.is(2, 2, 10, 0, 0, 0), "the one above: " + c.at(2, 2));
}

static void testNothingInReachLeavesATexelAsItWas() {
    Canvas c(1, 1);
    c.set(0, 0, 40, 50, 60, 0);
    c.fix();
    CHECK_MSG(c.is(0, 0, 40, 50, 60, 0), "a lone transparent texel keeps its colour");

    Canvas all(3, 3);
    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 3; ++x) all.set(x, y, 5, 6, 7, 255);
    }
    const std::vector<uint8_t> before = all.px;
    all.fix();
    CHECK_MSG(all.px == before, "and an image with nothing transparent is unchanged");

    ImagePixels::FixAlphaBorder(nullptr, 4, 4);
    std::vector<uint8_t> empty;
    ImagePixels::FixAlphaBorder(empty.data(), 0, 0);
    CHECK_MSG(true, "and nothing to fix is not a crash");
}


static void runTests() {
    testBgraComesOutAsRgba();
    testRgbaIsCopiedAsItIs();
    testEveryAlphaComesOutOpaque();
    testPaddedRowsArePackedTight();
    testNothingToReadGivesNothing();

    testATransparentTexelTakesItsOpaqueNeighboursColour();
    testTheThresholdIsTwentyNotOne();
    testTheReachIsFourTexels();
    testTheNearestIsByDistanceNotBySquare();
    testATieGoesToTheFirstInRowOrder();
    testNothingInReachLeavesATexelAsItWas();
}

TEST_MAIN("test_imagepixels", 32)
