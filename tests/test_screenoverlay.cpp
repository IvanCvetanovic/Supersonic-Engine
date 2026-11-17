// The screen overlay: quads drawn over the finished image in display values.
//
// What a suite can hold without a device is the part a game relies on: that a
// quad lands where its fractions say, shows the part of its texture it names,
// keeps the order it was added in, is blended as a picture over what is behind
// it, and that the C++ statement of the vertex table is the one the shader
// actually carries. The pixels themselves need a device; the Magic Portals
// captures in the remaster's step 39 are where they were looked at.
//
// And the composite it is drawn over: the numbers BloomPass hands
// bloom_composite.frag for each RenderSettings::SceneEncoding, and that the
// shader's branches are numbered as the enum is (remaster step 43).

#include "TestHarness.hpp"
#include "core/RenderSettings.hpp"
#include "core/ScreenOverlay.hpp"
#include "renderer/BloomPass.hpp"
#include "renderer/VulkanPipeline.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

bool near2(const glm::vec2& a, const glm::vec2& b, float eps = 1e-5f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps;
}

ScreenOverlay::Quad quadAt(const glm::vec2& min, const glm::vec2& max) {
    ScreenOverlay::Quad quad;
    quad.min = min;
    quad.max = max;
    return quad;
}

void testAWholeImageQuadCoversClipSpaceTopLeftFirst() {
    const ScreenOverlay::Quad whole = quadAt(glm::vec2(0.0f), glm::vec2(1.0f));
    // Vertex 0 is the top-left corner, and Vulkan's clip space puts the top of
    // the image at y = -1. A flip here would draw every HUD upside down.
    CHECK_MSG(near2(ScreenOverlay::CornerOf(whole, 0).clip, glm::vec2(-1.0f, -1.0f)), "top-left is (-1, -1)");
    CHECK_MSG(near2(ScreenOverlay::CornerOf(whole, 2).clip, glm::vec2(1.0f, 1.0f)), "bottom-right is (1, 1)");
    CHECK_MSG(near2(ScreenOverlay::CornerOf(whole, 5).clip, glm::vec2(-1.0f, 1.0f)), "bottom-left is (-1, 1)");
    CHECK_MSG(near2(ScreenOverlay::CornerOf(whole, 1).clip, glm::vec2(1.0f, -1.0f)), "top-right is (1, -1)");
}

void testACornerButtonLandsOnItsPixels() {
    // The Magic Portals pause button: 32 of 455.11 design units wide, flush in
    // the top-right corner of a 1280x720 image, which the original draws at
    // pixels 1190..1280 x 0..90.
    const glm::vec2 view(1280.0f / 2.8125f, 256.0f);
    const ScreenOverlay::Quad pause = quadAt(glm::vec2((view.x - 32.0f) / view.x, 0.0f),
                                             glm::vec2(1.0f, 32.0f / view.y));
    const glm::vec2 topLeftPx = (ScreenOverlay::CornerOf(pause, 0).clip + 1.0f) * 0.5f * glm::vec2(1280.0f, 720.0f);
    const glm::vec2 bottomRightPx =
        (ScreenOverlay::CornerOf(pause, 2).clip + 1.0f) * 0.5f * glm::vec2(1280.0f, 720.0f);
    CHECK_MSG(near2(topLeftPx, glm::vec2(1190.0f, 0.0f), 1e-2f), "its top-left pixel is (1190, 0)");
    CHECK_MSG(near2(bottomRightPx, glm::vec2(1280.0f, 90.0f), 1e-2f), "and it ends at (1280, 90)");
}

void testTheSixVerticesAreTwoTrianglesCoveringTheRectangle() {
    const ScreenOverlay::Quad quad = quadAt(glm::vec2(0.25f, 0.5f), glm::vec2(0.75f, 1.0f));
    // Each triangle's area, in fractions; together they must be the rectangle's,
    // with neither degenerate - a repeated corner would leave half a quad.
    const auto area = [&quad](int a, int b, int c) {
        const glm::vec2 p = ScreenOverlay::CornerOf(quad, a).clip;
        const glm::vec2 q = ScreenOverlay::CornerOf(quad, b).clip;
        const glm::vec2 r = ScreenOverlay::CornerOf(quad, c).clip;
        return 0.5f * std::fabs((q.x - p.x) * (r.y - p.y) - (r.x - p.x) * (q.y - p.y)) * 0.25f;
    };
    const float first = area(0, 1, 2);
    const float second = area(3, 4, 5);
    CHECK_MSG(first > 0.0f && second > 0.0f, "neither triangle is degenerate");
    CHECK_MSG(std::fabs(first + second - 0.5f * 0.5f) < 1e-5f,
              "and together they are the rectangle: " + std::to_string(first + second));
    CHECK_EQ(ScreenOverlay::kVerticesPerQuad, 6);
}

void testTheTextureRectangleRidesTheSameCorners() {
    // One glyph of a font page: the corner that is the quad's top-left samples
    // the cell's top-left.
    ScreenOverlay::Quad glyph = quadAt(glm::vec2(0.1f), glm::vec2(0.2f));
    glyph.uvMin = glm::vec2(0.5f, 0.25f);
    glyph.uvMax = glm::vec2(0.625f, 0.5f);
    CHECK_MSG(near2(ScreenOverlay::CornerOf(glyph, 0).uv, glm::vec2(0.5f, 0.25f)), "top-left samples uvMin");
    CHECK_MSG(near2(ScreenOverlay::CornerOf(glyph, 2).uv, glm::vec2(0.625f, 0.5f)), "bottom-right samples uvMax");
    CHECK_MSG(near2(ScreenOverlay::CornerOf(glyph, 7).uv, ScreenOverlay::CornerOf(glyph, 1).uv),
              "an index past five wraps rather than reading past the table");
}

void testATurnedQuadTurnsInSquarePixels() {
    // A 64 x 16 px arrow centred at (640, 360) on a 1280 x 720 image, turned 90
    // degrees counter-clockwise: in square pixels it stands 16 wide and 64 tall,
    // its right end - the first corner's right neighbour - now at the top.
    const glm::vec2 image(1280.0f, 720.0f);
    ScreenOverlay::Quad arrow = quadAt(glm::vec2(608.0f, 352.0f) / image, glm::vec2(672.0f, 368.0f) / image);
    arrow.basis = ScreenOverlay::Rotation(glm::radians(90.0f), image.x / image.y);
    const auto px = [&](int index) { return (ScreenOverlay::CornerOf(arrow, index).clip + 1.0f) * 0.5f * image; };
    CHECK_MSG(near2(px(0), glm::vec2(632.0f, 392.0f), 1e-3f), "the top-left corner turns down to (632, 392)");
    CHECK_MSG(near2(px(1), glm::vec2(632.0f, 328.0f), 1e-3f), "the top-right corner turns up to (632, 328)");
    CHECK_MSG(near2(px(2), glm::vec2(648.0f, 328.0f), 1e-3f), "the bottom-right corner to (648, 328)");
    CHECK_MSG(near2(px(5), glm::vec2(648.0f, 392.0f), 1e-3f), "and the bottom-left to (648, 392)");
    CHECK_MSG(near2(ScreenOverlay::CornerOf(arrow, 0).uv, glm::vec2(0.0f)), "the texture turns with it");

    // 23 degrees, the popup's arrow: every corner stays 33 px from the centre.
    arrow.basis = ScreenOverlay::Rotation(glm::radians(23.0f), image.x / image.y);
    bool round = true;
    for (int i = 0; i < ScreenOverlay::kVerticesPerQuad; ++i) {
        round = round && std::fabs(glm::length(px(i) - glm::vec2(640.0f, 360.0f)) - std::sqrt(32.0f * 32.0f + 8.0f * 8.0f)) < 1e-3f;
    }
    CHECK_MSG(round, "a turn keeps each corner's distance from the centre in pixels");
    CHECK_MSG(px(1).y < 360.0f - 8.0f, "and counter-clockwise lifts the right end");

    // Unturned, the identity, and the arithmetic is the one it always was.
    CHECK(ScreenOverlay::Rotation(0.0f, 16.0f / 9.0f) == glm::mat2(1.0f));
    const ScreenOverlay::Quad plain = quadAt(glm::vec2(0.1f, 0.2f), glm::vec2(0.3f, 0.7f));
    CHECK(ScreenOverlay::CornerOf(plain, 2).clip == glm::mix(plain.min, plain.max, glm::vec2(1.0f)) * 2.0f - 1.0f);
}

void testQuadsKeepTheOrderTheyWereAddedIn() {
    // No depth and no sort: the order of Add is the order of drawing, which is
    // what lets a game put a black between two of its own pictures.
    ScreenOverlay overlay;
    CHECK(overlay.Empty());
    for (int i = 0; i < 5; ++i) {
        ScreenOverlay::Quad quad;
        quad.color = glm::vec4(static_cast<float>(i));
        quad.texture = "layer" + std::to_string(i) + ".png";
        overlay.Add(quad);
    }
    CHECK_EQ(static_cast<int>(overlay.Quads().size()), 5);
    bool ordered = true;
    for (int i = 0; i < 5; ++i) {
        ordered = ordered && overlay.Quads()[static_cast<std::size_t>(i)].texture ==
                                 "layer" + std::to_string(i) + ".png";
    }
    CHECK_MSG(ordered, "quads come back in the order they were added");
}

void testClearEmptiesTheListAndTheDropCount() {
    ScreenOverlay overlay;
    for (std::size_t i = 0; i < ScreenOverlay::kMaxQuads + 3; ++i) overlay.Add(ScreenOverlay::Quad{});
    CHECK_EQ(overlay.Quads().size(), ScreenOverlay::kMaxQuads);
    CHECK_EQ(overlay.DroppedQuads(), std::size_t{3});
    overlay.Clear();
    CHECK(overlay.Empty());
    CHECK_EQ(overlay.DroppedQuads(), std::size_t{0});
}

void testTheOverlayBlendsAsAPictureOverWhatIsThere() {
    // The pipeline the renderer builds for it: blended, mixing rather than
    // adding. In display values that is exactly Ethanon's and ImGui's
    // src * a + dst * (1 - a), so a translucent white over a background b reads
    // a + (1 - a) * b whatever b is.
    VulkanPipelineOptions options{};
    options.blendEnable = true;
    const vk::PipelineColorBlendAttachmentState blend = ColorBlendFor(options);
    CHECK(blend.blendEnable == VK_TRUE);
    CHECK(blend.srcColorBlendFactor == vk::BlendFactor::eSrcAlpha);
    CHECK(blend.dstColorBlendFactor == vk::BlendFactor::eOneMinusSrcAlpha);
    CHECK(blend.colorBlendOp == vk::BlendOp::eAdd);
    CHECK_EQ(sizeof(ScreenOverlayPushConstants), std::size_t{64});
}

void testTheShaderCarriesTheSameVertexTable() {
    // screen_overlay.vert builds its corners from a table of its own, and
    // CornerOf states the same table in C++. Nothing would notice them
    // disagreeing but a picture with a triangle missing, so they are compared.
    std::ifstream file("assets/shaders/screen_overlay.vert");
    CHECK_MSG(file.good(), "assets/shaders/screen_overlay.vert opens (the suite runs from the project root)");
    if (!file.good()) return;
    std::stringstream text;
    text << file.rdbuf();
    const std::string source = text.str();
    const std::size_t table = source.find("kCorners[6]");
    CHECK_MSG(table != std::string::npos, "the shader declares kCorners[6]");
    if (table == std::string::npos) return;
    const std::size_t end = source.find(");", table);
    const std::string body = source.substr(table, end - table);
    const std::regex pair(R"(vec2\(\s*([0-9.]+)\s*,\s*([0-9.]+)\s*\))");
    std::vector<glm::vec2> corners;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), pair); it != std::sregex_iterator(); ++it) {
        corners.emplace_back(std::stof((*it)[1].str()), std::stof((*it)[2].str()));
    }
    CHECK_EQ(static_cast<int>(corners.size()), ScreenOverlay::kVerticesPerQuad);
    const ScreenOverlay::Quad unit = quadAt(glm::vec2(0.0f), glm::vec2(1.0f));
    bool same = corners.size() == static_cast<std::size_t>(ScreenOverlay::kVerticesPerQuad);
    for (std::size_t i = 0; same && i < corners.size(); ++i) {
        same = near2(ScreenOverlay::CornerOf(unit, static_cast<int>(i)).uv, corners[i]);
    }
    CHECK_MSG(same, "and its six corners are CornerOf's, in the same order");

    // The turn: the push block's fourth vec4 is the basis ScreenOverlayPushConstants
    // sends after the colour, and the shader turns the corner about the centre the
    // way CornerOf does, only when the basis is not the identity.
    const std::size_t colour = source.find("vec4 color;");
    const std::size_t basis = source.find("vec4 basis;");
    CHECK_MSG(colour != std::string::npos && basis != std::string::npos && basis > colour &&
                  source.find("vec4", colour + 1) == basis,
              "the shader's push block carries vec4 basis straight after vec4 color");
    CHECK_MSG(source.find("if (quad.basis != vec4(1.0, 0.0, 0.0, 1.0))") != std::string::npos &&
                  source.find("at = centre + mat2(quad.basis.xy, quad.basis.zw) * (at - centre);") != std::string::npos,
              "and turns a corner about the rectangle's centre as CornerOf does, identity excepted");
}

void testTheCompositeUnderTheOverlayIsUnchangedByDefault() {
    // The overlay loads whatever the composite wrote. With every setting at its
    // default the composite must be handed exactly the four numbers it always
    // was - (intensity, exposure, 0, 0) - or every existing scene's pixels move
    // before the overlay draws a thing.
    BloomPass::Settings settings{};
    settings.intensity = 0.55f;
    settings.exposure = 1.0f;
    const glm::vec4 value = BloomPass::CompositeValue(settings);
    CHECK(BloomPass::RunsBloomChain(settings));
    CHECK_NEAR(value.x, 0.55f);
    CHECK_NEAR(value.y, 1.0f);
    CHECK_MSG(value.z == 0.0f && value.w == 0.0f, "the two new numbers are zero by default");
}

void testADisplayEncodedCompositeRunsNoBloomWhateverItsIntensity() {
    BloomPass::Settings settings{};
    settings.intensity = 2.0f;
    settings.exposure = 0.75f;
    settings.encoding = RenderSettings::SceneEncoding::DisplayEncoded;
    glm::vec4 value = BloomPass::CompositeValue(settings);
    CHECK_MSG(!BloomPass::RunsBloomChain(settings), "no bright or blur pass is recorded");
    CHECK_MSG(value.x == 0.0f, "and the composite adds no bloom, whatever the scene's intensity");
    CHECK_NEAR(value.y, 0.75f);
    CHECK_MSG(value.z == 2.0f, "DisplayEncoded reaches the shader as 2");
    CHECK_MSG(value.w == 0.0f, "quantisation is its own switch");

    settings.quantize = RenderSettings::OutputQuantize::Rgb565;
    value = BloomPass::CompositeValue(settings);
    CHECK_MSG(value.w == 1.0f, "and Rgb565 reaches it as 1");

    // Only DisplayEncoded skips the chain: a linear scene without a tone map
    // still has radiance above white for the bloom to find.
    settings.encoding = RenderSettings::SceneEncoding::LinearNoToneMap;
    value = BloomPass::CompositeValue(settings);
    CHECK(BloomPass::RunsBloomChain(settings));
    CHECK_NEAR(value.x, 2.0f);
    CHECK_MSG(value.z == 1.0f, "LinearNoToneMap reaches the shader as 1");
}

void testTheCompositeShaderBranchesOnTheEnumsNumbers() {
    // bloom_composite.frag picks its branch by comparing params.value.z with
    // thresholds of its own, and CompositeValue sends the enum's value. Each
    // mode must land in the branch of the same index, which a mode inserted in
    // the middle of the enum, or a threshold edited in the shader, would break
    // with nothing to show for it but a wrong picture.
    std::ifstream file("assets/shaders/bloom_composite.frag");
    CHECK_MSG(file.good(), "assets/shaders/bloom_composite.frag opens (the suite runs from the project root)");
    if (!file.good()) return;
    std::stringstream text;
    text << file.rdbuf();
    const std::string source = text.str();

    const std::regex threshold(R"(params\.value\.z\s*<\s*([0-9.]+))");
    std::vector<float> thresholds;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), threshold); it != std::sregex_iterator(); ++it) {
        thresholds.push_back(std::stof((*it)[1].str()));
    }
    CHECK_EQ(static_cast<int>(thresholds.size()), 2);

    const RenderSettings::SceneEncoding modes[] = {RenderSettings::SceneEncoding::LinearHdr,
                                                   RenderSettings::SceneEncoding::LinearNoToneMap,
                                                   RenderSettings::SceneEncoding::DisplayEncoded};
    bool agree = thresholds.size() == 2;
    for (int i = 0; agree && i < 3; ++i) {
        BloomPass::Settings settings{};
        settings.encoding = modes[i];
        const float z = BloomPass::CompositeValue(settings).z;
        int branch = 0;
        for (float t : thresholds) branch += z < t ? 0 : 1;
        agree = branch == i;
    }
    CHECK_MSG(agree, "LinearHdr, LinearNoToneMap and DisplayEncoded take the shader's first, second and third branch");

    CHECK_MSG(source.find("params.value.w > 0.5") != std::string::npos, "the quantise switch is w above one half");
    CHECK_MSG(source.find("vec3(31.0, 63.0, 31.0)") != std::string::npos, "to 5, 6 and 5 bits");
}

void testThePlacementDecidesWhereTheOverlayIsDrawn() {
    // A game that sets nothing draws into the picture, whatever the picture's
    // size; WindowWhenScaled leaves the picture only when it is not the image's
    // size, down to one line; Window leaves it always; and no extent of zero is
    // a picture to decide about.
    CHECK_MSG(ScreenOverlay::Placement{} == ScreenOverlay::Placement::Picture, "the default is the picture");
    using P = ScreenOverlay::Placement;
    const glm::uvec2 window(1340u, 800u);
    const struct {
        P placement;
        glm::uvec2 picture;
        glm::uvec2 image;
        bool atWindow;
        const char* what;
    } rows[] = {
        {P::Picture, window, window, false, "Picture, the picture at the image's size"},
        {P::Picture, glm::uvec2(402u, 240u), window, false, "Picture, a picture at 0.3"},
        {P::Picture, glm::uvec2(1340u, 799u), window, false, "Picture, a picture one line short"},
        {P::Picture, glm::uvec2(0u, 800u), window, false, "Picture, no picture"},
        {P::Picture, window, glm::uvec2(0u, 0u), false, "Picture, no image"},
        {P::WindowWhenScaled, window, window, false, "WindowWhenScaled, the picture at the image's size"},
        {P::WindowWhenScaled, glm::uvec2(402u, 240u), window, true, "WindowWhenScaled, a picture at 0.3"},
        {P::WindowWhenScaled, glm::uvec2(1340u, 799u), window, true, "WindowWhenScaled, a picture one line short"},
        {P::WindowWhenScaled, glm::uvec2(0u, 800u), window, false, "WindowWhenScaled, no picture"},
        {P::WindowWhenScaled, window, glm::uvec2(0u, 0u), false, "WindowWhenScaled, no image"},
        {P::Window, window, window, true, "Window, the picture at the image's size"},
        {P::Window, glm::uvec2(402u, 240u), window, true, "Window, a picture at 0.3"},
        {P::Window, glm::uvec2(1340u, 799u), window, true, "Window, a picture one line short"},
        {P::Window, glm::uvec2(0u, 800u), window, false, "Window, no picture"},
        {P::Window, window, glm::uvec2(0u, 0u), false, "Window, no image"},
    };
    for (const auto& row : rows) {
        CHECK_MSG(ScreenOverlay::AtWindowResolution(row.placement, row.picture, row.image) == row.atWindow, row.what);
    }
}

void testTheWindowRectIsWhereThePictureIsShown() {
    using Rect = ScreenOverlay::WindowRect;
    const auto viewportIs = [](const Rect& rect, glm::vec2 offset, glm::vec2 extent) {
        return near2(rect.offset, offset) && near2(rect.extent, extent);
    };
    const auto scissorIs = [](const Rect& rect, glm::ivec2 offset, glm::uvec2 extent) {
        return rect.scissorOffset == offset && rect.scissorExtent == extent;
    };
    const glm::uvec2 tablet(1340u, 800u);

    // The game view: the picture shown edge to edge on a 1340x800 window.
    Rect rect = ScreenOverlay::WindowRectFor(glm::vec2(0.0f), glm::vec2(1340.0f, 800.0f), glm::vec2(0.0f),
                                             glm::vec2(1.0f), tablet);
    CHECK(rect.visible);
    CHECK_MSG(viewportIs(rect, glm::vec2(0.0f), glm::vec2(1340.0f, 800.0f)), "the viewport is the window");
    CHECK_MSG(scissorIs(rect, glm::ivec2(0), tablet), "and so is the scissor");

    // A retina display: ImGui's points are half the framebuffer's pixels.
    rect = ScreenOverlay::WindowRectFor(glm::vec2(0.0f), glm::vec2(670.0f, 400.0f), glm::vec2(0.0f), glm::vec2(2.0f),
                                        tablet);
    CHECK_MSG(rect.visible && viewportIs(rect, glm::vec2(0.0f), glm::vec2(1340.0f, 800.0f)),
              "at a framebuffer scale of 2 the viewport is in pixels");
    CHECK(scissorIs(rect, glm::ivec2(0), tablet));

    // An image placed inside the display, whose own origin is not (0, 0).
    rect = ScreenOverlay::WindowRectFor(glm::vec2(150.0f, 100.0f), glm::vec2(450.0f, 300.0f), glm::vec2(100.0f, 50.0f),
                                        glm::vec2(1.0f), tablet);
    CHECK_MSG(rect.visible && viewportIs(rect, glm::vec2(50.0f, 50.0f), glm::vec2(300.0f, 200.0f)),
              "the display's origin is taken off");
    CHECK(scissorIs(rect, glm::ivec2(50, 50), glm::uvec2(300u, 200u)));

    // Half off the framebuffer: the quads are fractions of the WHOLE image, so
    // the viewport keeps it whole, and only the scissor is clamped.
    rect = ScreenOverlay::WindowRectFor(glm::vec2(-670.0f, 0.0f), glm::vec2(670.0f, 800.0f), glm::vec2(0.0f),
                                        glm::vec2(1.0f), tablet);
    CHECK(rect.visible);
    CHECK_MSG(viewportIs(rect, glm::vec2(-670.0f, 0.0f), glm::vec2(1340.0f, 800.0f)), "the viewport is not clamped");
    CHECK_MSG(scissorIs(rect, glm::ivec2(0), glm::uvec2(670u, 800u)), "the scissor is");
    rect = ScreenOverlay::WindowRectFor(glm::vec2(670.0f, 400.0f), glm::vec2(2010.0f, 1200.0f), glm::vec2(0.0f),
                                        glm::vec2(1.0f), tablet);
    CHECK_MSG(rect.visible && scissorIs(rect, glm::ivec2(670, 400), glm::uvec2(670u, 400u)),
              "off the bottom-right too");

    // Nothing to draw: an empty image, one wholly off the framebuffer, no framebuffer.
    CHECK(!ScreenOverlay::WindowRectFor(glm::vec2(10.0f), glm::vec2(10.0f, 50.0f), glm::vec2(0.0f), glm::vec2(1.0f),
                                        tablet).visible);
    CHECK(!ScreenOverlay::WindowRectFor(glm::vec2(-500.0f, 0.0f), glm::vec2(-10.0f, 800.0f), glm::vec2(0.0f),
                                        glm::vec2(1.0f), tablet).visible);
    CHECK(!ScreenOverlay::WindowRectFor(glm::vec2(0.0f), glm::vec2(1340.0f, 800.0f), glm::vec2(0.0f), glm::vec2(1.0f),
                                        glm::uvec2(0u)).visible);

    // A fractional edge is rounded outwards, so a pixel whose centre the image
    // covers is not cut by the scissor.
    rect = ScreenOverlay::WindowRectFor(glm::vec2(0.5f, 0.5f), glm::vec2(100.25f, 50.75f), glm::vec2(0.0f),
                                        glm::vec2(1.0f), tablet);
    CHECK(scissorIs(rect, glm::ivec2(0), glm::uvec2(101u, 51u)));
}

void testTheWindowPipelineBlendsAsImGuiDoes() {
    // On the window the overlay is drawn over the picture ImGui has just drawn,
    // and the claim that an opaque HUD pixel is the same on both paths, and a
    // translucent one mixes the same way, rests on the two blends being one.
    // ImGui's is in its backend's source (imgui_impl_vulkan.cpp, the pipeline it
    // builds: color_attachment[0]), which is read here, as the shaders are.
    std::ifstream file("third_party/imgui/backends/imgui_impl_vulkan.cpp");
    CHECK_MSG(file.good(), "third_party/imgui/backends/imgui_impl_vulkan.cpp opens (the suite runs from the project root)");
    if (!file.good()) return;
    std::stringstream text;
    text << file.rdbuf();
    const std::string source = text.str();

    const std::map<std::string, int> constants = {
        {"VK_TRUE", static_cast<int>(VK_TRUE)},
        {"VK_FALSE", static_cast<int>(VK_FALSE)},
        {"VK_BLEND_FACTOR_ZERO", static_cast<int>(VK_BLEND_FACTOR_ZERO)},
        {"VK_BLEND_FACTOR_ONE", static_cast<int>(VK_BLEND_FACTOR_ONE)},
        {"VK_BLEND_FACTOR_SRC_ALPHA", static_cast<int>(VK_BLEND_FACTOR_SRC_ALPHA)},
        {"VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA", static_cast<int>(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)},
        {"VK_BLEND_OP_ADD", static_cast<int>(VK_BLEND_OP_ADD)},
    };
    const auto imguiValue = [&](const std::string& field) {
        const std::regex line("color_attachment\\[0\\]\\." + field + R"(\s*=\s*(VK_[A-Z_]+)\s*;)");
        std::smatch match;
        if (!std::regex_search(source, match, line)) return -1;
        const auto known = constants.find(match[1].str());
        return known == constants.end() ? -1 : known->second;
    };

    const VulkanPipelineOptions options = ScreenOverlayPipelineOptions(nullptr);
    const vk::PipelineColorBlendAttachmentState blend = ColorBlendFor(options);
    CHECK_MSG(imguiValue("blendEnable") == static_cast<int>(blend.blendEnable), "both blend");
    CHECK_MSG(imguiValue("srcColorBlendFactor") == static_cast<int>(blend.srcColorBlendFactor), "colour from the source's alpha");
    CHECK_MSG(imguiValue("dstColorBlendFactor") == static_cast<int>(blend.dstColorBlendFactor), "over one minus it");
    CHECK_MSG(imguiValue("colorBlendOp") == static_cast<int>(blend.colorBlendOp), "added");
    CHECK_MSG(imguiValue("srcAlphaBlendFactor") == static_cast<int>(blend.srcAlphaBlendFactor), "alpha taken whole");
    CHECK_MSG(imguiValue("dstAlphaBlendFactor") == static_cast<int>(blend.dstAlphaBlendFactor), "over one minus it");
    CHECK_MSG(imguiValue("alphaBlendOp") == static_cast<int>(blend.alphaBlendOp), "added");

    // And the rest of the shape both overlay pipelines are built with: what the
    // picture's pipeline always was.
    CHECK_MSG(options.blendEnable && !options.depthWrite &&
                  options.cullMode == vk::CullModeFlags(vk::CullModeFlagBits::eNone) &&
                  !options.useVertexInput && options.samples == vk::SampleCountFlagBits::e1 &&
                  options.pushConstantSize == sizeof(ScreenOverlayPushConstants) &&
                  options.blendEquation == BlendEquation::Mix,
              "blended (Mix), no depth, no vertices, one sample, the quad's push block");
}

} // namespace

static void runTests() {
    testAWholeImageQuadCoversClipSpaceTopLeftFirst();
    testACornerButtonLandsOnItsPixels();
    testTheSixVerticesAreTwoTrianglesCoveringTheRectangle();
    testTheTextureRectangleRidesTheSameCorners();
    testATurnedQuadTurnsInSquarePixels();
    testQuadsKeepTheOrderTheyWereAddedIn();
    testClearEmptiesTheListAndTheDropCount();
    testTheOverlayBlendsAsAPictureOverWhatIsThere();
    testTheShaderCarriesTheSameVertexTable();
    testTheCompositeUnderTheOverlayIsUnchangedByDefault();
    testADisplayEncodedCompositeRunsNoBloomWhateverItsIntensity();
    testTheCompositeShaderBranchesOnTheEnumsNumbers();

    testThePlacementDecidesWhereTheOverlayIsDrawn();
    testTheWindowRectIsWhereThePictureIsShown();
    testTheWindowPipelineBlendsAsImGuiDoes();
}

TEST_MAIN("test_screenoverlay", 80)
