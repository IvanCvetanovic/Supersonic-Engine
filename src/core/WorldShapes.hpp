#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// Immediate-mode shapes drawn IN THE WORLD, against the scene's depth.
//
// The engine already had a shape layer, and it is a different thing: UICanvas
// draws into an ImGui list over the finished image, so its circles are screen
// circles, its sizes are authored units at a reference height, and nothing it
// draws can be occluded by geometry. Its own header says what it is for - "a 2D
// game with a fixed camera zoom" - and it is right for that.
//
// This is for the other case, and there are three differences that matter, each
// of which is a thing the UI layer cannot do rather than a thing it does badly:
//
//   * WORLD UNITS. A ring of radius 3 is three metres across at any camera
//     distance. The UI layer's sizes are fixed fractions of the screen, so a
//     ring drawn there stays the same size as the camera pulls back - which
//     reads as the ring growing.
//   * PERSPECTIVE. A circle on the ground is an ELLIPSE once the camera is
//     pitched over, and the amount depends on where it is on screen. A screen
//     circle is a circle wherever it is put.
//   * DEPTH. A decal on the ground goes BEHIND the wall in front of it. Drawing
//     over the finished image cannot express that at all, and the symptom is
//     range indicators and selection rings floating through terrain.
//
// IMMEDIATE, and cleared every frame. That is the shape a game's effect code
// already has - Bevy spells it Gizmos, and HUSK's whole combat FX layer is
// eighteen calls per frame with no retained state - and it means an effect that
// stops being emitted stops being drawn, with nothing to clean up. A retained
// component would make every transient ring somebody's bookkeeping problem.
//
// Everything is LINES. A filled shape needs a triangulation, a sort and an
// opinion about lighting; a line needs none of those and is what an indicator,
// a range ring, a path preview and a debug overlay are actually made of.
class WorldShapes {
public:
    struct Vertex {
        glm::vec3 position{0.0f};
        glm::vec4 color{1.0f};
    };

    // Two vertices, appended. The unit of everything below.
    void AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    // A closed ring of `segments` line segments, in the plane whose normal is
    // `normal`. Defaults to the ground plane, because that is where nearly
    // every one of these goes.
    //
    // CLOSED, so the last segment returns to the first point - a ring drawn as
    // an open polyline has a visible notch, and at low segment counts it reads
    // as a rendering fault rather than as a missing segment.
    //
    // Segments are clamped to at least three: two would be a line drawn twice
    // and zero would be a silent nothing.
    void AddCircle(const glm::vec3& center, float radius, const glm::vec4& color,
                   int segments = 32, const glm::vec3& normal = glm::vec3(0.0f, 1.0f, 0.0f));

    // An axis-aligned rectangle on the ground, from two opposite corners. What
    // a selection marquee projected into the world looks like, and what a
    // building footprint preview is.
    void AddGroundRect(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    // The twelve edges of an axis-aligned box. A bounds preview, and the thing
    // a debug overlay wants most often.
    void AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    // Cleared by the renderer once it has consumed them, at the same point in
    // the frame every time. A caller that clears its own would race with
    // whatever else is drawing.
    void Clear() { m_vertices.clear(); }

    const std::vector<Vertex>& Vertices() const { return m_vertices; }
    bool Empty() const { return m_vertices.empty(); }

    // Lines, not vertices - the number a caller thinks in.
    size_t LineCount() const { return m_vertices.size() / 2; }

    // A ceiling, because an immediate-mode API invites a loop that forgot its
    // bound. Past this, further shapes are DROPPED rather than growing the
    // buffer without limit: a frame that tries to draw a million rings is a bug
    // in the caller, and the useful failure is a visibly truncated overlay at a
    // steady frame rate rather than an allocation spike nobody attributes.
    static constexpr size_t kMaxVertices = 1u << 18;   // 262,144 - 131,072 lines

    // How many vertices were refused since the last Clear. Non-zero means the
    // overlay on screen is incomplete, and it is worth a caller being able to
    // say so rather than wondering.
    size_t DroppedVertices() const { return m_dropped; }

private:
    bool Reserve(size_t additional);

    std::vector<Vertex> m_vertices;
    size_t m_dropped{0};
};

} // namespace Supersonic
