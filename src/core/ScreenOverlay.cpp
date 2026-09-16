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

} // namespace Supersonic
