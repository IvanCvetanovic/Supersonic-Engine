#include "core/ScreenOverlay.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace Supersonic {

namespace {

// Must match kCorners in assets/shaders/screen_overlay.vert, in the same order.
constexpr std::array<glm::vec2, ScreenOverlay::kVerticesPerQuad> kCorners = {
    glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f),
    glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f),
};

} // namespace

void ScreenOverlay::Add(Quad quad) {
    if (m_quads.size() >= kMaxQuads) {
        ++m_dropped;
        return;
    }
    m_quads.push_back(std::move(quad));
}

glm::mat2 ScreenOverlay::Rotation(float radians, float aspect) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float a = aspect > 0.0f ? aspect : 1.0f;
    // In square pixels, on an image whose +y runs down, a counter-clockwise turn
    // takes (x, y) to (x cos + y sin, -x sin + y cos). A fraction is a pixel over
    // the image's size, so x is scaled by the width and y by the height going in
    // and back out, which leaves the aspect on the off-diagonal. Columns.
    return glm::mat2(glm::vec2(c, -s * a), glm::vec2(s / a, c));
}

ScreenOverlay::Corner ScreenOverlay::CornerOf(const Quad& quad, int index) {
    const int wrapped = ((index % kVerticesPerQuad) + kVerticesPerQuad) % kVerticesPerQuad;
    const glm::vec2 c = kCorners[static_cast<std::size_t>(wrapped)];
    Corner corner;
    glm::vec2 at = glm::mix(quad.min, quad.max, c);
    // The branch the shader takes: an unturned quad keeps the arithmetic it
    // always had, to the bit.
    if (quad.basis != glm::mat2(1.0f)) {
        const glm::vec2 centre = (quad.min + quad.max) * 0.5f;
        at = centre + quad.basis * (at - centre);
    }
    // Fractions run +y down and so does Vulkan's clip space, so no flip.
    corner.clip = at * 2.0f - 1.0f;
    corner.uv = glm::mix(quad.uvMin, quad.uvMax, c);
    return corner;
}

bool ScreenOverlay::AtWindowResolution(Placement placement, glm::uvec2 picture, glm::uvec2 image) {
    if (picture.x == 0 || picture.y == 0 || image.x == 0 || image.y == 0) return false;
    switch (placement) {
    case Placement::Window:
        return true;
    case Placement::WindowWhenScaled:
        return picture != image;
    case Placement::Picture:
    default:
        return false;
    }
}

ScreenOverlay::WindowRect ScreenOverlay::WindowRectFor(glm::vec2 imageMin, glm::vec2 imageMax, glm::vec2 displayPos,
                                                       glm::vec2 framebufferScale, glm::uvec2 framebuffer) {
    WindowRect rect;
    rect.offset = (imageMin - displayPos) * framebufferScale;
    rect.extent = (imageMax - imageMin) * framebufferScale;
    // Written as "not greater" so that a NaN is no image either.
    if (!(rect.extent.x > 0.0f && rect.extent.y > 0.0f) || framebuffer.x == 0 || framebuffer.y == 0) return rect;

    const glm::vec2 limit(static_cast<float>(framebuffer.x), static_cast<float>(framebuffer.y));
    const glm::vec2 clipMin = glm::clamp(glm::floor(rect.offset), glm::vec2(0.0f), limit);
    const glm::vec2 clipMax = glm::clamp(glm::ceil(rect.offset + rect.extent), glm::vec2(0.0f), limit);
    if (!(clipMax.x > clipMin.x && clipMax.y > clipMin.y)) return rect;

    // Whole numbers inside [0, framebuffer] by now, so the conversions are exact.
    rect.scissorOffset = glm::ivec2(static_cast<int>(clipMin.x), static_cast<int>(clipMin.y));
    rect.scissorExtent = glm::uvec2(static_cast<unsigned>(clipMax.x - clipMin.x),
                                    static_cast<unsigned>(clipMax.y - clipMin.y));
    rect.visible = true;
    return rect;
}

} // namespace Supersonic
