#include "core/ScreenOverlay.hpp"

#include <array>
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

ScreenOverlay::Corner ScreenOverlay::CornerOf(const Quad& quad, int index) {
    const int wrapped = ((index % kVerticesPerQuad) + kVerticesPerQuad) % kVerticesPerQuad;
    const glm::vec2 c = kCorners[static_cast<std::size_t>(wrapped)];
    Corner corner;
    // Fractions run +y down and so does Vulkan's clip space, so no flip.
    corner.clip = glm::mix(quad.min, quad.max, c) * 2.0f - 1.0f;
    corner.uv = glm::mix(quad.uvMin, quad.uvMax, c);
    return corner;
}

} // namespace Supersonic
