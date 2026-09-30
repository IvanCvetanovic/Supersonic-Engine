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

static void runTests() {
    testBgraComesOutAsRgba();
    testRgbaIsCopiedAsItIs();
    testEveryAlphaComesOutOpaque();
    testPaddedRowsArePackedTight();
    testNothingToReadGivesNothing();
}

TEST_MAIN("test_imagepixels", 17)
