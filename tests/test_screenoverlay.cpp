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
    CHECK_EQ(sizeof(ScreenOverlayPushConstants), std::size_t{48});
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

} // namespace

static void runTests() {
    testAWholeImageQuadCoversClipSpaceTopLeftFirst();
    testACornerButtonLandsOnItsPixels();
    testTheSixVerticesAreTwoTrianglesCoveringTheRectangle();
    testTheTextureRectangleRidesTheSameCorners();
    testQuadsKeepTheOrderTheyWereAddedIn();
    testClearEmptiesTheListAndTheDropCount();
    testTheOverlayBlendsAsAPictureOverWhatIsThere();
    testTheShaderCarriesTheSameVertexTable();
    testTheCompositeUnderTheOverlayIsUnchangedByDefault();
    testADisplayEncodedCompositeRunsNoBloomWhateverItsIntensity();
    testTheCompositeShaderBranchesOnTheEnumsNumbers();
}

TEST_MAIN("test_screenoverlay", 40)
