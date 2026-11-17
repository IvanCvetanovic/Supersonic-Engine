#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// Pictures drawn over the FINISHED image, in display values: a game's HUD.
//
// Everything else a game draws goes through the scene target, which is linear
// light and floating point, and then through BloomPass's composite, which adds
// the bloom back, applies Reinhard and encodes once (ARCHITECTURE.md section 5).
// That is right for a world and wrong for a HUD in three ways that no choice of
// colour or alpha can undo:
//
//   * THE TONE MAP CAPS WHITE. Linear 1.0 leaves Reinhard as 0.5 and is encoded
//     to 186 of 255, and no finite input reaches 255. A white caption drawn
//     through the scene reads light grey, and the plaque's whites were measured
//     60 grey levels down.
//   * THE BRIGHT PASS HALOES IT. Its soft knee starts at half the threshold, so
//     a white glyph in the scene target blooms a glow around every letter.
//   * THE BLEND IS IN LINEAR LIGHT. A translucent button over a picture reads
//     with a contrast that depends on what is behind it: 0.32 over a bright
//     background and 0.56 over a dark one at the same alpha of 0.47. A game
//     whose art was authored for display-space blending - every 2D engine of
//     the last thirty years, and the one Magic Portals was written in - blends
//     to exactly alpha over ANY background, and that invariance is what the
//     captures of it measure.
//
// So these are drawn after all of that, into the composited image itself, with
// blending on and no conversion: a texel is the byte in the file, the colour
// multiplies it, and the result mixes over the encoded value already there.
// That is the arithmetic ImGui and Ethanon both do, and --screenshot reads the
// same image, so a capture shows what is drawn here.
//
// IMMEDIATE, and cleared by the renderer once it has drawn them - the shape
// WorldShapes has, for the same reasons. A game emits its HUD once a FRAME
// (EngineLayer::OnUpdate), not once a tick: a frame with no tick would
// otherwise draw nothing and a frame with two would draw twice.
//
// IN ORDER. There is no depth and no sort: a quad added later is drawn over
// one added earlier, which is how a HUD is thought about.
class ScreenOverlay {
public:
    struct Quad {
        // Where, as fractions of the image: (0, 0) its top-left corner, (1, 1)
        // its bottom-right, +y DOWN. Fractions rather than pixels because the
        // caller rarely knows the target's size - the editor's viewport and a
        // game's window are both whatever they are this frame - and a HUD laid
        // out in its own units divides by its own view.
        glm::vec2 min{0.0f, 0.0f};
        glm::vec2 max{1.0f, 1.0f};

        // The part of the texture to show, 0..1. An atlas is the reason: a
        // bitmap font is a page of glyphs, one quad a letter.
        glm::vec2 uvMin{0.0f, 0.0f};
        glm::vec2 uvMax{1.0f, 1.0f};

        // DISPLAY-REFERRED, multiplied into the texel as stored. Alpha is the
        // opacity the picture mixes over what is behind it at.
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

        // An image file, read with NO colour conversion - its bytes are already
        // display values. Empty draws plain `color`.
        std::string texture;

        // How the quad is TURNED about its own centre: a 2x2 matrix applied to
        // each corner's offset from that centre, in FRACTIONS of the image. The
        // identity, the default, draws the rectangle min and max say exactly as
        // before this existed; the shader takes the plain path then.
        //
        // A matrix in fractions rather than an angle, because a turn is only a
        // turn in square pixels and this class never knows the image's shape
        // (see min above). A caller that does - a HUD laid out in its own units
        // knows its view's aspect - passes Rotation(angle, aspect). A sprite a 2D
        // engine draws at an angle is why: Magic Portals' help popups turn an
        // arrow 23 degrees and a wall 90.
        glm::mat2 basis{1.0f};

        // Read the texture clamped to its edges rather than with the wrap its
        // image was loaded with (repeat, for a file): a HUD picture is drawn
        // once, edge to edge, and under repeat a quad whose edge falls between
        // texels takes half a texel of the picture's OPPOSITE edge along its
        // border. MaterialComponent::clampToEdge is the same switch for a
        // world sprite, and says why. Off, the default, draws as before.
        bool clampToEdge{false};
    };

    // The basis that turns a quad `radians` COUNTER-CLOCKWISE on the screen, on
    // an image `aspect` (width / height) wide: the turn in square pixels, taken
    // into fractions and back.
    static glm::mat2 Rotation(float radians, float aspect);

    // Appended. Past kMaxQuads a quad is dropped and counted, for the reason
    // WorldShapes gives: a frame drawing ten thousand HUD pictures is a loop in
    // the caller that forgot its bound.
    void Add(Quad quad);

    // Once the renderer has drawn them. A caller never needs to.
    void Clear() {
        m_quads.clear();
        m_dropped = 0;
    }

    const std::vector<Quad>& Quads() const { return m_quads; }
    bool Empty() const { return m_quads.empty(); }
    std::size_t DroppedQuads() const { return m_dropped; }

    static constexpr std::size_t kMaxQuads = 4096;

    // Two triangles, generated from the vertex index: no vertex buffer.
    static constexpr int kVerticesPerQuad = 6;

    // Where vertex `index` (0..5) of a quad lands - in Vulkan clip space, where
    // (-1, -1) is the image's top-left - turned by its basis about its centre,
    // and which texture coordinate it carries. screen_overlay.vert computes
    // exactly this; it is stated here so a suite can hold the mapping without a
    // device. An index outside 0..5 is taken modulo six.
    struct Corner {
        glm::vec2 clip{0.0f};
        glm::vec2 uv{0.0f};
    };
    static Corner CornerOf(const Quad& quad, int index);

    // WHERE the overlay is drawn: into the picture, or over it on the window.
    //
    // The picture is the scene target, and a game that scales it to keep its
    // frame rate (DynamicResolution) shrinks everything drawn into it: a HUD
    // drawn at 0.3 of a phone's window and stretched back up by a linear sampler
    // is a HUD whose text cannot be read. Drawn on the window instead - the same
    // quads, the same shaders and the same blend, straight onto the swapchain
    // image right after the picture - it is drawn at the window's resolution
    // whatever the picture's.
    //
    //   Picture           into the picture, as the overlay always was. The
    //                     default: a game that sets nothing is unchanged, and
    //                     --screenshot (which reads the picture) shows the HUD.
    //   WindowWhenScaled  on the window when the picture is not the image's size,
    //                     into the picture when it is: a game that scales only
    //                     some of the time keeps the old path, and its captures,
    //                     whenever it does not.
    //   Window            on the window whenever it can be: for a developer, and
    //                     for a test that compares the two paths at one size.
    //
    // The renderer may still refuse the window (a swapchain whose format it
    // cannot blend in display values), and then draws into the picture.
    enum class Placement { Picture, WindowWhenScaled, Window };

    // Whether `placement` draws on the window for a picture of `picture` pixels
    // shown as an image of `image` pixels. Extents, not a scale: the scaled
    // extent is rounded and evened (EditorLayer::ScaledExtent), so a scale below
    // 1 can still give the image's size, and the comparison is what decides
    // whether anything is stretched. A zero extent is no picture at all: false.
    static bool AtWindowResolution(Placement placement, glm::uvec2 picture, glm::uvec2 image);

    // The viewport and scissor that put the overlay on the window exactly where
    // the picture is shown, in framebuffer pixels.
    //
    // The viewport is the image's rectangle, NOT clamped: a quad's position is a
    // fraction of the image (Quad::min), so the viewport must be the whole image
    // even where part of it lies off the framebuffer, or every quad would shift.
    // The scissor is that rectangle clamped to the framebuffer, as ImGui clamps
    // its own (vkCmdSetScissor takes nothing outside it), and rounded outwards
    // so that a pixel whose centre the image covers is not cut.
    struct WindowRect {
        glm::vec2 offset{0.0f};
        glm::vec2 extent{0.0f};
        glm::ivec2 scissorOffset{0};
        glm::uvec2 scissorExtent{0u};
        // False when nothing of the image is on the framebuffer, or the image is
        // empty: then nothing may be recorded (a zero-sized viewport is invalid).
        bool visible{false};
    };

    // `imageMin` and `imageMax` are the image's corners in ImGui's coordinates
    // (points), `displayPos` and `framebufferScale` the draw data's, and
    // `framebuffer` the pixels the pass renders to.
    static WindowRect WindowRectFor(glm::vec2 imageMin, glm::vec2 imageMax, glm::vec2 displayPos,
                                    glm::vec2 framebufferScale, glm::uvec2 framebuffer);

private:
    std::vector<Quad> m_quads;
    std::size_t m_dropped{0};
};

} // namespace Supersonic
